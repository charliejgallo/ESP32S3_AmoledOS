#!/usr/bin/env python3
"""levels.py - the worlds and their levels, solved and written for the pack.

    python3 tools/levels.py [-v]

Reads tools/levels/worlds.py (the worlds table, in map order) and
tools/levels/<world>.py (each world's LEVELS), solves every level with the
game's own rules (tools/solve.c on main/ml_rules.c), and writes
assets/levels/<level>.bin (par, pushes, text, furniture) and
assets/levels/worlds.txt (the table the watch reads: main/ml_level.h).
Exits non-zero, saying why, if any level is malformed or unsolvable.

A world file:
    LEVELS = [
        dict(title=dict(es='...', en='...', de='...'),
             text='''
    #####
    #@$.#
    #####'''),
        ...
    ]
with an optional deco='x,y:k ...' (furniture by hand, ml_level.c).
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'assets', 'levels')
sys.path.insert(0, HERE)
import gen  # noqa: E402  (its solver runner)

MECH = dict(wet=1, plate=2, flap=4, hole=8, ball=16)


def load_module(name):
    path = os.path.join(HERE, 'levels', name + '.py')
    spec = importlib.util.spec_from_file_location('lv_' + name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def multi(t):
    return '|'.join('%s=%s' % (k, t[k]) for k in ('es', 'en', 'de') if k in t)


def clean(text):
    rows = text.strip('\n').split('\n')
    return '\n'.join(r.rstrip() for r in rows)


def main():
    verbose = '-v' in sys.argv
    worlds = load_module('worlds').WORLDS
    os.makedirs(OUT, exist_ok=True)
    for fn in os.listdir(OUT):
        if fn.endswith('.bin'):
            os.remove(os.path.join(OUT, fn))
    table = []
    problems = []
    total = 0
    for w in worlds:
        if not os.path.exists(os.path.join(HERE, 'levels', w['id'] + '.py')):
            print('world %s: no levels yet, left out' % w['id'])
            continue
        mod = load_module(w['id'])
        levels = mod.LEVELS
        texts = [clean(l['text']) for l in levels]
        res = gen.solve_many(texts, limit=6000000)
        table.append('world %s' % w['id'])
        table.append('name %s' % multi(w['name']))
        table.append('kit %s' % w.get('kit', w['id']))
        table.append('panel %s' % w.get('panel', 'map_' + w['id']))
        table.append('need %d' % w.get('need', 0))
        table.append('music %d' % w.get('music', 0))
        table.append('mech %d' % sum(MECH[m] for m in w.get('mech', ())))
        table.append('gift %s' % (w.get('gift') or '-'))
        for i, (l, t, r) in enumerate(zip(levels, texts, res)):
            name = '%s_%d' % (w['id'], i + 1)
            if not r['ok']:
                problems.append('%s: %s' % (name, r['why']))
                continue
            with open(os.path.join(OUT, name + '.bin'), 'wb') as fh:
                fh.write(struct.pack('<HH', r['moves'], r['pushes']))
                fh.write(t.encode() + b'\0' + l.get('deco', '').encode() + b'\0')
            table.append('level %s %s' % (name, multi(l['title'])))
            total += 1
            if verbose:
                print('%-10s par %3d  pushes %3d  states %8d  %s' % (name, r['moves'], r['pushes'],
                                                                     r['states'], l['title'].get('es', '')))
        table.append('end')
    with open(os.path.join(OUT, 'worlds.txt'), 'w') as fh:
        fh.write('\n'.join(table) + '\n')
    if problems:
        print('\n'.join(problems))
        sys.exit(1)
    print('%d worlds, %d levels' % (len(worlds), total))


if __name__ == '__main__':
    main()
