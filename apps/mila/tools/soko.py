#!/usr/bin/env python3
"""soko.py - Sokoban levels for Mila: parse, check, solve.

Level text uses the usual symbols:
    #  wall        (space) floor     .  target
    $  object      *  object on a target
    @  Mila        +  Mila on a target
    -  outside (nothing, not even floor)

    python3 soko.py <file>     solve every level in the file (blank-line separated)

The solver is a breadth-first search over pushes (Mila's reachable area is
normalised), so the number it prints is the minimum number of PUSHES; the
walking in between is counted too, as moves, along the shortest path.
"""
import collections
import sys


def parse(text):
    rows = [r.rstrip('\n') for r in text.strip('\n').split('\n')]
    w = max(len(r) for r in rows)
    rows = [r.ljust(w) for r in rows]
    walls, targets, boxes, man = set(), set(), set(), None
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            p = (x, y)
            if c == '#':
                walls.add(p)
            if c in '.*+':
                targets.add(p)
            if c in '$*':
                boxes.add(p)
            if c in '@+':
                man = p
    assert man is not None, 'no Mila'
    assert len(boxes) == len(targets), 'objects %d != targets %d' % (len(boxes), len(targets))
    floor = _inside(walls, man, w, len(rows))
    return dict(w=w, h=len(rows), rows=rows, walls=walls, targets=targets,
                boxes=frozenset(boxes), man=man, floor=floor)


def _inside(walls, start, w, h):
    seen, todo = {start}, [start]
    while todo:
        x, y = todo.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (x + dx, y + dy)
            if q in walls or q in seen:
                continue
            assert 0 <= q[0] < w and 0 <= q[1] < h, 'level is not closed at %r' % (q,)
            seen.add(q)
            todo.append(q)
    return seen


DIRS = {'r': (1, 0), 'l': (-1, 0), 'd': (0, 1), 'u': (0, -1)}


def _dead(L):
    """Floor cells an object can never leave towards a target (simple corners
    plus walls along edges without targets)."""
    fl, wl, tg = L['floor'], L['walls'], L['targets']
    live = set(tg)
    todo = list(tg)
    while todo:      # pull objects backwards from every target
        x, y = todo.pop()
        for dx, dy in DIRS.values():
            b = (x + dx, y + dy)
            m = (x + 2 * dx, y + 2 * dy)
            if b in fl and m in fl and b not in live:
                live.add(b)
                todo.append(b)
    return fl - live


def _reach(L, man, boxes):
    seen, todo = {man}, [man]
    while todo:
        x, y = todo.pop()
        for dx, dy in DIRS.values():
            q = (x + dx, y + dy)
            if q in L['floor'] and q not in boxes and q not in seen:
                seen.add(q)
                todo.append(q)
    return seen


def solve(L, limit=2_000_000):
    """Minimum pushes. Returns (pushes, states) or (None, states)."""
    dead = _dead(L)
    start_boxes = L['boxes']
    reach = _reach(L, L['man'], start_boxes)
    key0 = (min(reach), start_boxes)
    prev = {key0: None}
    q = collections.deque([(L['man'], start_boxes, 0)])
    while q:
        man, boxes, n = q.popleft()
        if boxes == L['targets']:
            return n, len(prev)
        reach = _reach(L, man, boxes)
        for b in boxes:
            for dx, dy in DIRS.values():
                stand = (b[0] - dx, b[1] - dy)
                to = (b[0] + dx, b[1] + dy)
                if stand not in reach or to not in L['floor'] or to in boxes or to in dead:
                    continue
                nb = frozenset(boxes - {b} | {to})
                r2 = _reach(L, b, nb)
                k = (min(r2), nb)
                if k in prev:
                    continue
                prev[k] = 1
                if len(prev) > limit:
                    return None, len(prev)
                q.append((b, nb, n + 1))
    return None, len(prev)


def levels(path):
    with open(path) as fh:
        text = fh.read()
    out, cur = [], []
    for line in text.split('\n'):
        if line.startswith(';'):
            continue
        if line.strip() == '':
            if cur:
                out.append('\n'.join(cur))
                cur = []
        else:
            cur.append(line)
    if cur:
        out.append('\n'.join(cur))
    return out


if __name__ == '__main__':
    for i, t in enumerate(levels(sys.argv[1])):
        L = parse(t)
        n, st = solve(L)
        print('level %d: %dx%d, %d objects, pushes %s (%d states)' % (
            i, L['w'], L['h'], len(L['boxes']), n, st))
