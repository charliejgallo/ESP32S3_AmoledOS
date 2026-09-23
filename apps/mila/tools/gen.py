#!/usr/bin/env python3
"""gen.py - level candidates for Mila, checked by the solver.

    python3 gen.py <world> <level 1-8> [-n TRIES] [-k KEEP] [--seed S]

Builds random rooms with the level's recipe (RECIPES below), places the
targets, the things and the world's mechanic, solves them all with
/tmp/ml_solve (tools/solve.c, the game's own rules) and prints the best
KEEP by the recipe's score, with their par and solution. The chosen ones
are copied by hand into tools/levels/<world>.py (and may be touched up
there: every level is solved again when packed).
"""
import argparse
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOLVER = '/tmp/ml_solve'


def build_solver():
    src = [os.path.join(HERE, 'solve.c'), os.path.join(HERE, '..', 'main', 'ml_rules.c')]
    if not os.path.exists(SOLVER) or any(os.path.getmtime(s) > os.path.getmtime(SOLVER) for s in src):
        subprocess.check_call(['cc', '-O2', '-I' + os.path.join(HERE, '..', 'main'), '-o', SOLVER] + src)


def solve_many(texts, limit=1500000):
    build_solver()
    out = subprocess.run([SOLVER, '-l', str(limit)], input='\n\n'.join(texts) + '\n',
                         stdout=subprocess.PIPE, text=True, check=True).stdout.strip().split('\n')
    res = []
    for line in out:
        p = line.split()
        if p[0] == 'ok':
            use = [int(v) for v in p[4].split('.')]
            res.append(dict(ok=True, moves=int(p[1]), pushes=int(p[2]), states=int(p[3]),
                            slides=use[0], falls=use[1], gates=use[2], flaps=use[3], rolls=use[4],
                            switches=use[5], path=p[5] if len(p) > 5 else ''))
        else:
            res.append(dict(ok=False, why=p[1], states=int(p[2])))
    return res


# ---------------------------------------------------------------------------
# rooms
# ---------------------------------------------------------------------------

def neighbours(x, y):
    return ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1))


def connected(cells):
    if not cells:
        return False
    start = next(iter(cells))
    seen, todo = {start}, [start]
    while todo:
        c = todo.pop()
        for q in neighbours(*c):
            if q in cells and q not in seen:
                seen.add(q)
                todo.append(q)
    return len(seen) == len(cells)


def carve(rng, W, H, fill):
    """floor cells inside a W x H box (walls on the border), about fill of the inside"""
    inner = [(x, y) for x in range(1, W - 1) for y in range(1, H - 1)]
    want = int(len(inner) * fill)
    floor = set()
    x, y = rng.randrange(1, W - 1), rng.randrange(1, H - 1)
    while len(floor) < want:
        rw, rh = rng.choice(((1, 2), (2, 1), (2, 2), (3, 1), (1, 3), (2, 3), (3, 2), (3, 3)))
        if floor:
            ax, ay = rng.choice(sorted(floor))
            x = min(max(1, ax - rng.randrange(rw)), W - 1 - rw)
            y = min(max(1, ay - rng.randrange(rh)), H - 1 - rh)
        for i in range(rw):
            for j in range(rh):
                if 1 <= x + i < W - 1 and 1 <= y + j < H - 1:
                    floor.add((x + i, y + j))
    # a few pillars, never cutting the room
    for _ in range(rng.randrange(0, 3)):
        c = rng.choice(sorted(floor))
        if connected(floor - {c}):
            floor.discard(c)
    return floor


def articulation(floor):
    """floor cells whose removal splits the room"""
    return [c for c in sorted(floor) if not connected(floor - {c})]


def to_text(W, H, floor, marks):
    rows = []
    for y in range(H):
        r = ''
        for x in range(W):
            c = (x, y)
            if c in marks:
                r += marks[c]
            elif c in floor:
                r += ' '
            elif any(q in floor for q in ((x + a, y + b) for a in (-1, 0, 1) for b in (-1, 0, 1))):
                r += '#'
            else:
                r += '-'
        rows.append(r)
    rows = [r for r in rows if r.strip('-')]
    w = len(rows[0])
    cols = [x for x in range(w) if any(r[x] != '-' for r in rows)]
    rows = [r[cols[0]:cols[-1] + 1].rstrip('-') for r in rows]
    return '\n'.join(rows)


# ---------------------------------------------------------------------------
# recipes: per world, per level (1-8)
#   size (W, H) ranges, fill, things, targets, and the mechanic's pieces
# ---------------------------------------------------------------------------

def recipe(world, n):
    t = (n - 1) / 7.0                      # 0 at level 1, 1 at level 8
    r = dict(W=(6 + round(2 * t), 7 + round(2 * t)), H=(7 + round(2 * t), 8 + round(3 * t)),
             fill=0.62 - 0.08 * t, things=1 + min(3, round(t * 3.4)), extra=0,
             wet=0, plates=0, flaps=0, holes=0, balls=0,
             pushes=(1 + round(5 * t), 4 + round(26 * t)), need={})
    if n <= 2:
        r['W'], r['H'] = (5, 6), (6, 7)
    if world == 'living':
        r['things'] = [1, 2, 2, 3, 3, 3, 4, 4][n - 1]
        r['pushes'] = [(1, 3), (3, 8), (6, 14), (8, 18), (12, 24), (15, 30), (18, 34), (22, 45)][n - 1]
    elif world == 'kitchen':
        r['things'] = [1, 2, 2, 3, 3, 3, 4, 4][n - 1]
        r['wet'] = [3, 4, 5, 6, 6, 7, 8, 9][n - 1]
        r['pushes'] = [(1, 4), (3, 10), (6, 16), (8, 20), (10, 26), (12, 30), (14, 36), (18, 48)][n - 1]
        r['need'] = dict(slides=1 if n <= 2 else 2)
    elif world == 'garden':
        r['things'] = [1, 1, 2, 2, 2, 3, 3, 3][n - 1]
        r['plates'] = [1, 1, 1, 1, 2, 1, 2, 2][n - 1]
        r['wet'] = [0, 0, 0, 3, 0, 4, 0, 4][n - 1]
        r['pushes'] = [(2, 6), (3, 10), (6, 16), (8, 20), (10, 26), (12, 30), (14, 36), (18, 48)][n - 1]
        r['need'] = dict(gates=1)
    elif world == 'attic':
        r['things'] = [1, 2, 2, 2, 3, 3, 3, 3][n - 1]
        r['flaps'] = [1, 1, 1, 2, 1, 2, 2, 2][n - 1]
        r['plates'] = [0, 0, 1, 0, 1, 0, 1, 1][n - 1]
        r['pushes'] = [(1, 6), (3, 10), (6, 16), (8, 20), (10, 26), (12, 30), (14, 36), (18, 48)][n - 1]
        r['need'] = dict(flaps=1)
    elif world == 'roofs':
        r['things'] = [1, 2, 2, 2, 3, 3, 3, 3][n - 1]
        r['holes'] = [1, 1, 0, 2, 1, 2, 1, 2][n - 1]
        r['balls'] = [0, 0, 1, 1, 1, 1, 2, 1][n - 1]
        r['flaps'] = [0, 0, 0, 0, 1, 0, 1, 0][n - 1]
        r['wet'] = [0, 0, 0, 0, 0, 3, 0, 0][n - 1]
        r['extra'] = r['holes']
        r['pushes'] = [(2, 6), (3, 10), (5, 16), (8, 22), (10, 28), (12, 34), (15, 40), (16, 60)][n - 1]
        need = {}
        if r['holes']:
            need['falls'] = 1
        if r['balls']:
            need['rolls'] = 1
        r['need'] = need
    return r


def partition(rng, W, H, floor, free, walls, marks):
    """a wall line across the floor with a cat flap; False if it cannot"""
    horiz = rng.random() < 0.5
    span = range(2, H - 2) if horiz else range(2, W - 2)
    if not span:
        return False
    k = rng.choice(list(span))
    line = [(x, k) for x in range(W)] if horiz else [(k, y) for y in range(H)]
    cells = [c for c in line if c in floor]
    if len(cells) < 3 or any(c not in free for c in cells):
        return False
    side_a = floor - set(cells)
    for c in cells:
        floor.discard(c)
        free.discard(c)
        walls.add(c)
    # the flap: a cell of the line with floor on both sides across it
    opts = []
    for (x, y) in cells:
        a, b = ((x, y - 1), (x, y + 1)) if horiz else ((x - 1, y), (x + 1, y))
        if a in floor and b in floor:
            opts.append((x, y))
    if not opts:
        for c in cells:
            floor.add(c)
            free.add(c)
            walls.discard(c)
        return False
    f = rng.choice(opts)
    marks[f] = 'v' if horiz else 'h'
    walls.discard(f)
    if rng.random() < 0.35:
        rest = [c for c in opts if c != f and abs(c[0] - f[0]) + abs(c[1] - f[1]) > 1]
        if rest:
            g = rng.choice(rest)
            floor.add(g)
            free.add(g)
            walls.discard(g)
    # both sides must still hold floor
    del side_a
    return True


def candidate(rng, world, n):
    R = recipe(world, n)
    W = rng.randint(*R['W'])
    H = rng.randint(*R['H'])
    floor = carve(rng, W, H, R['fill'])
    marks = {}
    free = set(floor)

    def take(k, pool=None):
        pool = sorted(pool if pool is not None else free)
        pool = [c for c in pool if c in free]
        if len(pool) < k:
            raise ValueError
        got = rng.sample(pool, k)
        for c in got:
            free.discard(c)
        return got

    # the mechanics first (they need special spots)
    walls = {(x, y) for x in range(W) for y in range(H)} - floor
    for _ in range(R['flaps']):
        # most of the time: a partition across the room with the flap in it
        # (and now and then a gap too), so Mila can go round and things cannot
        if rng.random() < 0.7 and partition(rng, W, H, floor, free, walls, marks):
            continue
        # a wall cell between two floor cells along one axis, walls on the other
        opts = []
        for (x, y) in sorted(walls):
            if not (1 <= x < W - 1 and 1 <= y < H - 1):
                continue
            if (x, y - 1) in free and (x, y + 1) in free and (x - 1, y) in walls and (x + 1, y) in walls:
                opts.append(((x, y), 'v'))
            if (x - 1, y) in free and (x + 1, y) in free and (x, y - 1) in walls and (x, y + 1) in walls:
                opts.append(((x, y), 'h'))
        if not opts:
            raise ValueError
        c, k = rng.choice(opts)
        marks[c] = k
        walls.discard(c)
    if R['plates']:
        arts = [c for c in articulation(floor) if c in free]
        if not arts:
            raise ValueError
        g = rng.choice(arts)
        marks[g] = 'G'
        free.discard(g)
        for c in take(R['plates']):
            marks[c] = '_'
    for c in (take(R['holes'], [c for c in articulation(floor)] or None) if R['holes'] else []):
        marks[c] = 'o'
    if R['wet']:
        # a patch grown from one cell
        seed = take(1)[0]
        patch = [seed]
        marks[seed] = '~'
        tries = 0
        while len(patch) < R['wet'] and tries < 50:
            tries += 1
            b = rng.choice(patch)
            q = rng.choice(neighbours(*b))
            if q in free:
                free.discard(q)
                patch.append(q)
                marks[q] = '~'
    ntargets = R['things'] - 0
    nthings = R['things'] + R['extra'] + R['plates']
    nballs = min(R['balls'], nthings)
    tg = take(ntargets)
    for c in tg:
        marks[c] = '.'
    th = take(nthings, [c for c in free])
    for i, c in enumerate(th):
        marks[c] = 'b' if i < nballs else '$'
    mila = take(1)[0]
    marks[mila] = '@'
    return to_text(W, H, floor, marks), R


def openness(text):
    """how many 2x2 blocks of plain floor: open rooms make easy levels"""
    rows = text.split('\n')
    g = lambda x, y: rows[y][x] if y < len(rows) and x < len(rows[y]) else '#'  # noqa: E731
    n = 0
    for y in range(len(rows) - 1):
        for x in range(len(rows[y]) - 1):
            if all(g(x + a, y + b) not in '#-' for a in (0, 1) for b in (0, 1)):
                n += 1
    return n


def score(res, R, text=''):
    lo, hi = R['pushes']
    if not res['ok'] or res['pushes'] < lo or res['pushes'] > hi:
        return None
    for k, v in R['need'].items():
        if res[k] < v:
            return None
    return (res['pushes'] + 2.0 * res['switches'] + 0.25 * res['moves'] + 1.5 * len(str(res['states']))
            - 0.8 * openness(text))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('world')
    ap.add_argument('level', type=int)
    ap.add_argument('-n', type=int, default=3000)
    ap.add_argument('-k', type=int, default=4)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    texts, recs = [], []
    tries = 0
    while len(texts) < a.n and tries < a.n * 40:
        tries += 1
        try:
            t, R = candidate(rng, a.world, a.level)
        except ValueError:
            continue
        texts.append(t)
        recs.append(R)
    best = []
    CH = 400
    for i in range(0, len(texts), CH):
        for t, r, R in zip(texts[i:i + CH], solve_many(texts[i:i + CH]), recs[i:i + CH]):
            s = score(r, R, t)
            if s is not None:
                best.append((s, t, r))
    best.sort(key=lambda b: -b[0])
    seen = set()
    shown = 0
    for s, t, r in best:
        if t in seen:
            continue
        seen.add(t)
        print('; score %.1f  moves %d  pushes %d  states %d  use s%d f%d g%d fl%d r%d sw%d' % (
            s, r['moves'], r['pushes'], r['states'], r['slides'], r['falls'], r['gates'], r['flaps'],
            r['rolls'], r['switches']))
        print('; ' + r['path'])
        print(t)
        print()
        shown += 1
        if shown >= a.k:
            break
    print('; %d candidates, %d good' % (len(texts), len(best)), file=sys.stderr)


if __name__ == '__main__':
    main()
