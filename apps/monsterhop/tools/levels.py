#!/usr/bin/env python3
"""
MONSTER HOP - the level builder.

    python3 tools/levels.py [name ...] [--preview] [--check]

Levels are written in tools/levels/<zone>.py as ASCII maps plus a few calls
(see the Level class) and saved to assets/levels/<name>.bin, the format
main/mh_level.h reads. --preview renders each level whole with compose.py
into assets/levels/<name>.png; --check walks the level with the hop rules
(start -> every key -> the exit) and fails loudly if something is out of
reach.

A map is two layers, the far row first (the top of the screen is +Y):

  terrain: two characters per cell, a tile letter and a floor digit
           ('a0' asphalt on the ground, 'b2' brick two floors up).
           Special letters: '.' pit, '~' water (the digit is the banks'
           floor), '#' a bridge deck over water, '%' quicksand.
  things:  one character per cell: '.' nothing, or a letter of the zone's
           legend (props) or of THINGS below (keys, coins, ...).
"""
import importlib
import json
import os
import struct
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'levels')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'levels'))

ZONES = {'city': 0, 'castle': 1, 'desert': 2, 'forest': 3, 'test': 4}
DIRS = {'n': 0, 'e': 1, 's': 2, 'w': 3}
DXY = {0: (0, 1), 1: (1, 0), 2: (0, -1), 3: (-1, 0)}

CK_GROUND, CK_PIT, CK_WATER, CK_QUICK, CK_BRIDGE = range(5)
CF_SOLID, CF_ORIGIN, CF_HIGH, CF_GROUP = 1, 2, 4, 8

(ENT_NONE, ENT_KEY, ENT_COIN, ENT_HEART, ENT_HOURGLASS, ENT_CHEST, ENT_CHECKPOINT, ENT_EXIT,
 ENT_LEVER, ENT_CRATE, ENT_GROUPCELL, ENT_PLATFORM, ENT_MONSTER, ENT_LANE, ENT_TRAP, ENT_STICKER) = range(16)

MONSTERS = ['zombie', 'vampire', 'mummy', 'werewolf', 'zombiedog', 'armor', 'crow',
            'brute', 'count', 'pharaoh', 'alpha']
LANES = ['car', 'log', 'boulder', 'scarab', 'bat', 'lily']
TRAPS = ['spikes', 'vent', 'darts', 'bear']
MF_PINGPONG, MF_NOTICE = 1, 2

# things every zone understands
THINGS = {'K': 'key', 'o': 'coin', 'H': 'heart', 'T': 'hourglass', 'B': 'chest',
          'P': 'checkpoint', 'E': 'exit', 'S': 'start', 'C': 'crate', 'L': 'lever', '*': 'sticker'}


class Level:
    def __init__(self, name, zone, title, time_s=180, par_s=120):
        self.name, self.zone, self.title = name, zone, title
        self.time_s, self.par_s = time_s, par_s
        self.assets = ['']
        self.cells = None
        self.w = self.h = 0
        self.ents = []
        self.paths = []
        self.start = (0, 0, 0)
        self.legend = {}          # tile letter -> {'top':, 'fill':}
        self.props = {}           # thing letter -> {'name':, 'fp': (w, d), 'high': bool}
        self.surf = None          # default surface asset for '~'
        self.quick = None
        self.deck = None
        self.deck_y = None
        self.crate = None         # the zone's pushable crate
        self.exit_dir = 0

    # ---- assets ----
    def asset(self, name):
        if not name:
            return 0
        if name not in self.assets:
            self.assets.append(name)
        return self.assets.index(name)

    # ---- the maps ----
    def terrain(self, text):
        rows = [r.rstrip() for r in text.strip('\n').split('\n')]
        rows = [r.strip() for r in rows if r.strip()]
        self.h = len(rows)
        self.w = len(rows[0]) // 2
        for r in rows:
            assert len(r) == self.w * 2, 'row of %d chars, want %d: %r' % (len(r), self.w * 2, r)
        self.cells = [[None] * self.w for _ in range(self.h)]
        for j, r in enumerate(rows):
            y = self.h - 1 - j
            for x in range(self.w):
                t, d = r[2 * x], r[2 * x + 1]
                fl = int(d) if d.isdigit() else 0
                c = dict(h=fl, kind=CK_GROUND, top=0, fill=0, prop=0, flags=0, surf=0, deck=0)
                if t == '.':
                    c['kind'] = CK_PIT
                elif t == '~':
                    c['kind'] = CK_WATER
                    c['surf'] = self.asset(self.surf)
                elif t in '#|':
                    # '#' a deck you cross along X, '|' one you cross along Y
                    c['kind'] = CK_BRIDGE
                    c['surf'] = self.asset(self.surf)
                    deck = self.deck if t == '#' else (self.deck_y or self.deck)
                    c['deck'] = self.asset(deck)
                elif t == '%':
                    c['kind'] = CK_QUICK
                    c['surf'] = self.asset(self.quick)
                else:
                    lg = self.legend[t]
                    c['top'] = self.asset(lg['top'])
                    c['fill'] = self.asset(lg['fill'])
                    if lg.get('high'):
                        c['flags'] |= CF_HIGH
                self.cells[y][x] = c

    def map2(self, tiles, heights=None):
        """the terrain as two layers of one character per cell: the tile
        letters, and the floors ('0'-'3'; missing = 0). Far row first."""
        tr = [r.strip() for r in tiles.strip('\n').split('\n') if r.strip()]
        hr = [r.strip() for r in heights.strip('\n').split('\n') if r.strip()] if heights else ['0' * len(tr[0])] * len(tr)
        assert len(tr) == len(hr), '%s: %d tile rows, %d height rows' % (self.name, len(tr), len(hr))
        rows = []
        for j, (t, h) in enumerate(zip(tr, hr)):
            assert len(t) == len(h) == len(tr[0]), '%s row %d: %d tiles, %d heights' % (self.name, j, len(t), len(h))
            rows.append(''.join(a + b for a, b in zip(t, h)))
        self.terrain('\n'.join(rows))

    def things(self, text):
        rows = [r.strip() for r in text.strip('\n').split('\n') if r.strip()]
        assert len(rows) == self.h, 'things: %d rows, terrain %d' % (len(rows), self.h)
        for j, r in enumerate(rows):
            y = self.h - 1 - j
            assert len(r) == self.w, 'things row %r: %d chars, want %d' % (r, len(r), self.w)
            for x, ch in enumerate(r):
                if ch != '.':
                    self.thing(ch, x, y)

    def place(self, ch, pts):
        """the things of one letter at a list of (x, y) cells"""
        for (x, y) in pts:
            assert 0 <= x < self.w and 0 <= y < self.h, '%s: %r out of the map' % (self.name, (x, y))
            self.thing(ch, x, y)

    def thing(self, ch, x, y):
        if True:
            if True:
                c = self.cells[y][x]
                z = c['h']
                if ch in self.props:
                    p = self.props[ch]
                    self.prop(x, y, p['name'], p.get('fp', (1, 1)), p.get('walk', False))
                    return
                kind = THINGS.get(ch)
                if kind is None:
                    raise ValueError('%s: unknown thing %r at %d,%d' % (self.name, ch, x, y))
                if kind == 'start':
                    self.start = (x, y, 0)
                elif kind == 'key':
                    self.ent(ENT_KEY, x, y, z)
                elif kind == 'coin':
                    self.ent(ENT_COIN, x, y, z)
                elif kind == 'heart':
                    self.ent(ENT_HEART, x, y, z)
                elif kind == 'sticker':
                    self.ent(ENT_STICKER, x, y, z)
                elif kind == 'hourglass':
                    self.ent(ENT_HOURGLASS, x, y, z)
                elif kind == 'chest':
                    self.ent(ENT_CHEST, x, y, z, dir=2, a=10)
                elif kind == 'checkpoint':
                    self.ent(ENT_CHECKPOINT, x, y, z)
                elif kind == 'exit':
                    self.ent(ENT_EXIT, x, y, z, dir=self.exit_dir)
                elif kind == 'crate':
                    self.ent(ENT_CRATE, x, y, z, a=self.asset(self.crate))
                elif kind == 'lever':
                    self.ent(ENT_LEVER, x, y, z, dir=2, a=1)

    def prop(self, x, y, name, fp=(1, 1), walk=False):
        c = self.cells[y][x]
        c['prop'] = self.asset(name)
        c['flags'] |= CF_ORIGIN
        for dx in range(fp[0]):
            for dy in range(fp[1]):
                if 0 <= x + dx < self.w and 0 <= y + dy < self.h and not walk:
                    self.cells[y + dy][x + dx]['flags'] |= CF_SOLID

    # ---- entities ----
    def ent(self, t, x, y, z=None, dir=0, a=0, b=0, c=0, p0=0, p1=0):
        if z is None:
            z = self.cells[y][x]['h']
        self.ents.append((t, x, y, z, dir, a, b, c, p0 & 0xFFFF, p1 & 0xFFFF))

    def path(self, pts, pingpong=False):
        self.paths.append((list(pts), 1 if pingpong else 0))
        return len(self.paths) - 1

    def monster(self, kind, x, y, dir='s', path=None, ms=600, param=0, pingpong=False, notice=False):
        flags = (MF_PINGPONG if pingpong else 0) | (MF_NOTICE if notice else 0)
        pi = self.path(path, pingpong) if path else 255
        self.ent(ENT_MONSTER, x, y, None, DIRS[dir], MONSTERS.index(kind), pi, flags, ms, param)

    def lane(self, kind, x, y, dir, length, size=1, ms=400, gap=3, z=None):
        self.ent(ENT_LANE, x, y, z, DIRS[dir], LANES.index(kind), size, length, ms, gap)

    def trap(self, kind, x, y, dir='s', period=2000, phase=0):
        self.ent(ENT_TRAP, x, y, None, DIRS[dir], TRAPS.index(kind), 0, 0, period, phase)

    def platform(self, pts, ms=900, group=0, floor=0, pingpong=True):
        """a floating slab that follows a path (its top at floor); group = the
        lever that starts it (0 = always moving)"""
        pi = self.path(pts, pingpong)
        x, y = pts[0]
        self.ent(ENT_PLATFORM, x, y, floor, 0, pi, group, floor, ms, 0)

    def lever(self, x, y, group, dir='s'):
        self.ent(ENT_LEVER, x, y, None, DIRS[dir], group)

    def chest(self, x, y, coins=10, bonus=None):
        b = {'heart': ENT_HEART, 'hourglass': ENT_HOURGLASS}.get(bonus, 0)
        self.ent(ENT_CHEST, x, y, None, 2, coins, b)

    def group_cell(self, x, y, group, floor=None):
        """a bridge deck that the lever of `group` lays over this water/pit cell"""
        c = self.cells[y][x]
        c['flags'] |= CF_GROUP
        if floor is None:
            floor = c['h']
        self.ent(ENT_GROUPCELL, x, y, None, c['kind'], group, CK_BRIDGE, floor, self.asset(self.deck), 0)

    # ---- output ----
    def pack(self):
        z = ZONES[self.zone]
        n_pts = sum(len(p) for p, _ in self.paths)
        b = bytearray(b'MHLV')
        b += struct.pack('<BBBB', 1, z, self.w, self.h)
        b += struct.pack('<BBBB', self.start[0], self.start[1], self.start[2], 0)
        b += struct.pack('<HH', self.time_s, self.par_s)
        b += struct.pack('<HHHH', len(self.assets), len(self.ents), len(self.paths), n_pts)
        for a in self.assets:
            assert len(a) < 32, a
            b += a.encode().ljust(32, b'\0')
        for y in range(self.h):
            for x in range(self.w):
                c = self.cells[y][x]
                b += struct.pack('<8B', c['h'], c['kind'], c['top'], c['fill'], c['prop'], c['flags'],
                                 c['surf'], c['deck'])
        for e in self.ents:
            b += struct.pack('<8BHH', *e)
        first = 0
        for pts, fl in self.paths:
            b += struct.pack('<HBB', first, len(pts), fl)
            first += len(pts)
        for pts, _ in self.paths:
            for (px, py) in pts:
                b += struct.pack('<BB', px, py)
        return bytes(b)

    # ---- checks ----
    def walkable(self, x, y):
        if not (0 <= x < self.w and 0 <= y < self.h):
            return None
        c = self.cells[y][x]
        if c['flags'] & CF_SOLID:
            return None
        if c['kind'] in (CK_PIT, CK_WATER):
            return None
        return c['h']

    def reach(self, extra=()):
        """cells reachable from the start with hops (1 floor up) and super hops
        (2 cells or 2 floors); water lanes with logs count as crossable"""
        seen = set()
        sx, sy, _ = self.start
        q = deque([(sx, sy)])
        seen.add((sx, sy))
        ride = set(extra)
        for e in self.ents:
            if e[0] == ENT_LANE and LANES[e[5]] in ('log', 'lily'):
                t, x, y, zz, d, a, size, length = e[:8]
                dx, dy = DXY[d]
                for k in range(length):
                    ride.add((x + dx * k, y + dy * k))
            if e[0] == ENT_CRATE:
                # a crate can be pushed into the pit or the water next to it
                for dx, dy in DXY.values():
                    nx, ny = e[1] + dx, e[2] + dy
                    if 0 <= nx < self.w and 0 <= ny < self.h and self.cells[ny][nx]['kind'] in (CK_PIT, CK_WATER):
                        ride.add((nx, ny))
            if e[0] == ENT_GROUPCELL:
                ride.add((e[1], e[2]))
        for pts, _ in self.paths:
            pass
        for e in self.ents:
            if e[0] == ENT_PLATFORM:
                pts = self.paths[e[5]][0]
                for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                    for k in range(max(abs(bx - ax), abs(by - ay)) + 1):
                        ride.add((ax + (bx > ax) * k - (bx < ax) * k, ay + (by > ay) * k - (by < ay) * k))
        while q:
            x, y = q.popleft()
            h0 = self.walkable(x, y)
            if h0 is None:
                h0 = self.cells[y][x]['h']
            for d in range(4):
                dx, dy = DXY[d]
                for step, up in ((1, 1), (2, 2), (1, 2)):
                    nx, ny = x + dx * step, y + dy * step
                    if (nx, ny) in seen:
                        continue
                    h1 = self.walkable(nx, ny)
                    if h1 is None and (nx, ny) in ride:
                        h1 = self.cells[ny][nx]['h']
                    if h1 is None or h1 - h0 > up:
                        continue
                    seen.add((nx, ny))
                    q.append((nx, ny))
        return seen

    def check(self):
        errs = []
        seen = self.reach()
        keys = [(e[1], e[2]) for e in self.ents if e[0] == ENT_KEY]
        exits = [(e[1], e[2]) for e in self.ents if e[0] == ENT_EXIT]
        if len(keys) != 5:
            errs.append('%d keys (want 5)' % len(keys))
        if len(exits) != 1:
            errs.append('%d exits (want 1)' % len(exits))
        stk = [(e[1], e[2]) for e in self.ents if e[0] == ENT_STICKER]
        if self.zone != 'test' and len(stk) != 1:
            errs.append('%d stickers (want 1)' % len(stk))
        for k in stk:
            if k not in seen:
                errs.append('out of reach: sticker at %d,%d' % k)
        # things must stand on free ground, and monsters walk on it
        names = {ENT_KEY: 'key', ENT_COIN: 'coin', ENT_STICKER: 'sticker', ENT_HEART: 'heart',
                 ENT_HOURGLASS: 'hourglass', ENT_CHEST: 'chest', ENT_CHECKPOINT: 'checkpoint', ENT_LEVER: 'lever',
                 ENT_CRATE: 'crate', ENT_EXIT: 'exit'}
        taken = {}
        for e in self.ents:
            if e[0] not in names:
                continue
            c = self.cells[e[2]][e[1]]
            if c['flags'] & CF_SOLID and e[0] != ENT_EXIT:
                errs.append('%s at %d,%d is on a prop' % (names[e[0]], e[1], e[2]))
            if c['kind'] in (CK_PIT, CK_WATER, CK_QUICK):
                errs.append('%s at %d,%d is over a hole' % (names[e[0]], e[1], e[2]))
            if (e[1], e[2]) in taken and e[0] != ENT_COIN:
                errs.append('%s at %d,%d shares the cell with a %s' % (names[e[0]], e[1], e[2], taken[(e[1], e[2])]))
            taken[(e[1], e[2])] = names[e[0]]
        for e in self.ents:
            if e[0] != ENT_MONSTER or e[6] == 255:
                continue
            pts = self.paths[e[6]][0]
            for (ax, ay), (bx, by) in zip(pts, pts[1:] + ([pts[0]] if not self.paths[e[6]][1] else [])):
                for k in range(max(abs(bx - ax), abs(by - ay)) + 1):
                    x = ax + (bx > ax) * k - (bx < ax) * k
                    y = ay + (by > ay) * k - (by < ay) * k
                    c = self.cells[y][x]
                    if c['flags'] & CF_SOLID or c['kind'] in (CK_PIT, CK_WATER):
                        errs.append('%s walks through %d,%d' % (MONSTERS[e[5]], x, y))
                        break
        for k in keys + exits:
            if k not in seen:
                errs.append('out of reach: %s at %d,%d' % ('exit' if k in exits else 'key', k[0], k[1]))
        return errs

    # ---- preview with compose.py ----
    def scene(self):
        import compose
        items = []
        for y in range(self.h):
            for x in range(self.w):
                c = self.cells[y][x]
                kind = c['kind']
                a = self.assets
                if kind in (CK_GROUND, CK_QUICK) and c['top']:
                    lo = -1
                    for k in range(lo, c['h']):
                        items.append([a[c['fill']] or a[c['top']], x, y, k])
                    top = a[c['top']]
                    items.append([top, x, y, c['h']])
                if kind == CK_QUICK and c['surf']:
                    items.append([a[c['surf']], x, y, 0, {'at': [x + .5, y + .5, c['h'] * compose.FLOOR_M]}])
                if kind in (CK_WATER, CK_BRIDGE) and c['surf']:
                    items.append([a[c['surf']], x, y, 0, {'at': [x + .5, y + .5, c['h'] * compose.FLOOR_M - 0.18]}])
                if kind == CK_BRIDGE and c['deck']:
                    items.append([a[c['deck']], x, y, c['h']])
                if c['prop']:
                    items.append([a[c['prop']], x, y, c['h']])
        return items


def load_zone(zone):
    mod = importlib.import_module(zone)
    return mod.levels()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    preview = '--preview' in sys.argv
    os.makedirs(OUT, exist_ok=True)
    files = sorted(f[:-3] for f in os.listdir(os.path.join(ROOT, 'tools', 'levels')) if f.endswith('.py'))
    bad = 0
    for zf in files:
        for lv in load_zone(zf):
            if args and lv.name not in args:
                continue
            data = lv.pack()
            with open(os.path.join(OUT, lv.name + '.bin'), 'wb') as fh:
                fh.write(data)
            errs = lv.check()
            print('%-12s %2dx%-2d %3d assets %3d things %5d B %s' %
                  (lv.name, lv.w, lv.h, len(lv.assets) - 1, len(lv.ents), len(data),
                   'OK' if not errs else 'PROBLEMS: ' + '; '.join(errs)))
            bad += bool(errs)
            if preview:
                import compose
                sc = {'assets': lv.preview_assets, 'size': lv.preview_size, 'look_at': lv.preview_look,
                      'bg': lv.preview_bg, 'items': lv.scene()}
                img = compose.render_scene(sc, os.path.join(ROOT, 'assets'))
                img.save(os.path.join(OUT, lv.name + '.png'))
    sys.exit(1 if bad and '--check' in sys.argv else 0)


if __name__ == '__main__':
    main()
