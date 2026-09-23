"""_write.py <world> [pick,pick,...] - writes <world>.py from the candidates
(_cand/<world>_<n>.txt), taking candidate `pick` (0 = best) of each level."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _titles import TITLES
HERE = os.path.dirname(os.path.abspath(__file__))

def cands(world, n):
    txt = open(os.path.join(HERE, '_cand', '%s_%d.txt' % (world, n))).read()
    out = []
    for block in txt.strip().split('\n\n'):
        lines = block.split('\n')
        head = [l for l in lines if l.startswith(';')]
        grid = [l for l in lines if not l.startswith(';')]
        if grid:
            out.append((head, '\n'.join(grid)))
    return out

world = sys.argv[1]
picks = [int(v) for v in sys.argv[2].split(',')] if len(sys.argv) > 2 else [0] * 8
lines = ['"""%s - levels (from tools/gen.py candidates, chosen by hand)."""' % world, '', 'LEVELS = [']
for n in range(1, 9):
    c = cands(world, n)
    head, grid = c[min(picks[n - 1], len(c) - 1)]
    es, en, de = TITLES[world][n - 1]
    lines.append('    # %s' % head[0][2:])
    lines.append("    dict(title=dict(es=%r, en=%r, de=%r), text='''" % (es, en, de))
    lines.append(grid + "'''),")
lines.append(']')
open(os.path.join(HERE, world + '.py'), 'w').write('\n'.join(lines) + '\n')
print('wrote', world)
