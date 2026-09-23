#!/usr/bin/env python3
"""Merge the parts of a split mila.py run: python3 mila_merge.py ../../assets/mila"""
import glob
import json
import os
import shutil
import sys

out = os.path.abspath(sys.argv[1])
path = os.path.join(out, 'meta.json')
meta = json.load(open(path)) if os.path.exists(path) else {}
for d in sorted(glob.glob(os.path.join(out, '_part*'))):
    m = json.load(open(os.path.join(d, 'meta.json')))
    meta.update(m)
    for fn in os.listdir(d):
        if fn.endswith('.png'):
            shutil.move(os.path.join(d, fn), os.path.join(out, fn))
        elif fn.startswith('_timing'):
            shutil.move(os.path.join(d, fn), os.path.join(out, fn))
    shutil.rmtree(d)
with open(path, 'w') as f:
    json.dump(meta, f, indent=1, sort_keys=True)
print('merged', len([k for k in meta if not k.startswith('_')]), 'sprites')
