#!/usr/bin/env python3
"""memsum.py <mem.txt>... — one-line-per-file summary of /api/mem snapshots (internal/exec free, heaps, stacks)."""
import re, sys
def parse(path):
    t = open(path).read()
    d = {'file': path.split('/')[-2] if '/' in path else path}
    for name in ('internal', 'exec', 'dma', 'psram'):
        m = re.search(r"^%s\s+free\s+(\d+)\s+alloc\s+(\d+)\s+largest\s+(\d+)\s+minfree\s+(\d+)" % name, t, re.M)
        if m: d[name] = tuple(int(x) for x in m.groups())
    heaps = []
    for m in re.finditer(r"^(\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$", t, re.M):
        heaps.append((m.group(1), int(m.group(2),16), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8)), int(m.group(9))))
    d['heaps'] = heaps
    m = re.search(r"stacks: internal (\d+)\s+psram (\d+)", t)
    if m: d['stacks'] = (int(m.group(1)), int(m.group(2)))
    m = re.search(r"traced live: internal (\d+) B in (\d+) blocks, psram (\d+) B", t)
    if m: d['traced'] = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
    m = re.search(r"untraced internal[^:]*: (\d+) B", t)
    if m: d['untraced'] = int(m.group(1))
    m = re.search(r"free (\d+) largest (\d+) in-use (\d+)\s+loaded\((\d+)\): (.*)", t)
    if m: d['pool'] = m.groups()
    return d
for p in sys.argv[1:]:
    d = parse(p)
    print(f"== {d['file']} ==")
    for k in ('internal','exec','dma','psram'):
        if k in d: print(f"  {k:9s} free {d[k][0]:8d} alloc {d[k][1]:8d} largest {d[k][2]:7d} minfree {d[k][3]:7d}")
    for h in d['heaps']:
        if h[0] != 'PSRAM':
            print(f"  heap {h[0]:5s} @0x{h[1]:08x} size {h[2]:7d} used {h[3]:7d} free {h[4]:7d} largest {h[5]:7d} nused {h[6]:5d} nfree {h[7]:4d}")
    if 'stacks' in d: print(f"  stacks internal {d['stacks'][0]} psram {d['stacks'][1]}")
    if 'traced' in d: print(f"  traced internal {d['traced'][0]} in {d['traced'][1]} blocks; untraced internal {d.get('untraced','?')}")
    if 'pool' in d: print(f"  pool free {d['pool'][0]} largest {d['pool'][1]} in-use {d['pool'][2]} loaded {d['pool'][4]}")
