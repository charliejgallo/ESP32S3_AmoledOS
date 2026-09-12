#!/usr/bin/env python3
"""symbolize.py <mem.txt> <elf>  — symbolizes the call-site table of /api/mem.
Groups internal bytes by the first frame that is not a heap/LVGL/newlib wrapper."""
import re, subprocess, sys, collections
mem, elf = sys.argv[1], sys.argv[2]
txt = open(mem).read()
sec = txt.split("== internal bytes by call site")[1].split("\n==")[0] if "== internal bytes by call site" in txt else ""
rows = []
for line in sec.splitlines()[2:]:
    m = re.match(r"\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+((?:[0-9a-f]{8}\s*)+)", line)
    if m:
        pcs = m.group(5).split()
        rows.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), pcs))
tbsec = txt.split("== traced internal blocks >= 1024 B")[1].split("\n==")[0] if "== traced internal blocks" in txt else ""
tbrows = []
for line in tbsec.splitlines()[1:]:
    m = re.match(r"\s*(\d+)\s+0x([0-9a-f]+)\s+(\S+)\s+([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})", line)
    if m:
        tbrows.append((int(m.group(1)), m.group(2), m.group(3), [m.group(4), m.group(5), m.group(6)]))
pcs = set()
for r in rows: pcs.update(r[4])
for r in tbrows: pcs.update(r[3])
pcs.discard("00000000")
pcs = sorted(pcs)
sym = {}
if pcs:
    out = subprocess.run(["xtensa-esp32s3-elf-addr2line", "-fC", "-e", elf] + ["0x"+p for p in pcs], capture_output=True, text=True).stdout.splitlines()
    for i, p in enumerate(pcs):
        fn = out[2*i] if 2*i < len(out) else "?"
        loc = out[2*i+1] if 2*i+1 < len(out) else "?"
        loc = loc.split("/")[-1]
        sym[p] = (fn, loc)
def name(p):
    if p == "00000000": return "-"
    fn, loc = sym.get(p, ("?", "?"))
    return f"{fn}({loc})"
WRAP = ("heap_caps_malloc", "heap_caps_calloc", "heap_caps_realloc", "heap_caps_malloc_base", "heap_caps_malloc_default", "heap_caps_calloc_base", "heap_caps_realloc_default", "heap_caps_calloc_prefer","heap_caps_malloc_prefer",
        "malloc", "calloc", "realloc", "_malloc_r", "_calloc_r", "_realloc_r", "lv_malloc", "lv_malloc_zeroed", "lv_realloc", "lv_malloc_core", "lv_realloc_core", "pvPortMalloc", "pvPortMallocStackMem", "pvPortMallocTcbMem",
        "heap_caps_aligned_alloc", "heap_caps_aligned_calloc", "lv_mem_alloc", "esp_elf_malloc", "__wrap_esp_elf_malloc", "heap_trace_alloc_hook", "strdup", "_strdup_r", "__wrap_lv_malloc_core", "heap_caps_malloc_prefer")
def owner(pcs_):
    for p in pcs_:
        if p == "00000000": continue
        fn = sym.get(p, ("?",))[0]
        if fn not in WRAP and not fn.startswith("heap_caps_") and not fn.startswith("multi_heap"):
            return fn, sym.get(p, ("?","?"))[1]
    return "?", "?"
print(f"{'int_B':>8} {'n':>5} {'max':>7}  owner (first non-wrapper frame)  |  full stack")
for r in rows:
    fn, loc = owner(r[4])
    print(f"{r[0]:8d} {r[1]:5d} {r[2]:7d}  {fn}({loc})  |  " + " > ".join(name(p) for p in r[4] if p != "00000000"))
agg = collections.Counter(); aggn = collections.Counter()
for r in rows:
    fn, loc = owner(r[4]); agg[fn] += r[0]; aggn[fn] += r[1]
print("\n== internal bytes by owner function ==")
for fn, b in agg.most_common(60):
    print(f"{b:8d} {aggn[fn]:5d}  {fn}")
print("\n== big traced blocks ==")
for r in tbrows:
    print(f"{r[0]:7d} 0x{r[1]} {r[2]:5s} " + " > ".join(name(p) for p in r[3] if p != "00000000"))
