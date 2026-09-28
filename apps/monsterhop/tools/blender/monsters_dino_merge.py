#!/usr/bin/env python3
"""Monster Hop - merge the Lost Valley monsters rendered by monsters_dino.py
(into a staging root) into assets/monsters and assets/bosses.

    python3 monsters_dino_merge.py STAGE
    MH_RES=2 python3 monsters_dino_merge.py STAGE     # the HD build: into assets_hd/

STAGE/monsters/*.png + meta.json   -> assets/monsters/
STAGE/bosses/*.png + meta.json     -> assets/bosses/
STAGE/palettes.json                -> the palettes.json of both (added, never replacing another monster's)

meta.json is merged, never rewritten from scratch: every existing entry is
kept, ours are added (or replace an older copy of the same dino name). The
merge re-reads the file right before writing it under a lock, since other
scripts may be writing the same folder. A copy of each meta.json and
palettes.json as it was before the first merge is kept next to it
(_meta_before_dino.json, _palettes_before_dino.json).
"""
import fcntl
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HD = os.environ.get('MH_RES', '').strip() not in ('', '1')     # the HD build (mh_common.RES == 2)
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'assets_hd' if HD else 'assets'))
MINE = ('raptor', 'trike', 'ptero', 'compy', 'trex')


def ours(name):
    return name.split('_')[0] in MINE or name in ('card_%s' % w for w in MINE)


def locked_update(path, fn):
    lock = path + '.lock'
    with open(lock, 'w') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        try:
            data = {}
            if os.path.exists(path) or not HD:      # HD: the folder may still be empty
                with open(path) as fh:
                    data = json.load(fh)
            n0 = len(data)
            data = fn(data)
            tmp = path + '.tmp_dino'
            with open(tmp, 'w') as fh:
                json.dump(data, fh, indent=1, sort_keys=True)
            os.replace(tmp, path)
        finally:
            fcntl.flock(lk, fcntl.LOCK_UN)
    os.remove(lock)
    return n0, len(data)


def merge(stage):
    with open(os.path.join(stage, 'palettes.json')) as fh:
        pj = json.load(fh)
    for sub in ('monsters', 'bosses'):
        src = os.path.join(stage, sub)
        dst = os.path.join(ASSETS, sub)
        os.makedirs(dst, exist_ok=True)
        for what, bak in (('meta.json', '_meta_before_dino.json'), ('palettes.json', '_palettes_before_dino.json')):
            if HD and not os.path.exists(os.path.join(dst, what)):
                continue
            if not os.path.exists(os.path.join(dst, bak)):
                shutil.copy2(os.path.join(dst, what), os.path.join(dst, bak))
        with open(os.path.join(src, 'meta.json')) as fh:
            new = {k: v for k, v in json.load(fh).items() if not k.startswith('_')}
        bad = [k for k in new if not ours(k)]
        assert not bad, bad
        n = 0
        for k, info in new.items():
            for fn in info['files'].values():
                shutil.copy2(os.path.join(src, fn), os.path.join(dst, fn))
                n += 1

        def add_meta(meta, new=new):
            clash = [k for k in new if k in meta and not ours(k)]
            assert not clash, clash
            meta.update(new)
            return meta
        a, b = locked_update(os.path.join(dst, 'meta.json'), add_meta)
        print('%s: %d files copied, meta.json %d -> %d entries' % (sub, n, a, b))
        whos = sorted(set(v.get('monster') or v.get('boss') for v in new.values()))

        def add_pal(pal, whos=whos, sub=sub):
            for w in whos:
                pal[w] = pj['palettes'][w]
            if sub == 'monsters':
                ids = pal.setdefault('_ids', {})
                for w in whos:
                    ids[w] = pj['ids'][w]
            return pal
        a, b = locked_update(os.path.join(dst, 'palettes.json'), add_pal)
        print('%s: palettes.json %d -> %d keys (%s)' % (sub, a, b, ', '.join(whos)))


if __name__ == '__main__':
    merge(os.path.abspath(sys.argv[1]))
