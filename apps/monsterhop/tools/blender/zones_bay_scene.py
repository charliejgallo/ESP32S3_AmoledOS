#!/usr/bin/env python3
"""Monster Hop - Abyss Bay: compose scenes, contact sheets and the memory
budget (plain python3: numpy + PIL).

    python3 zones_bay_scene.py scene      # assets/tiles_bay/_scene*.png: the harbour (the approved sample's layout)
    python3 zones_bay_scene.py scene_b    # assets/tiles_bay/_scene_b*.png: the open bay (raft, buoy-raft,
                                          #   piranhas, a tentacle slamming a row, a wave, the exit gate)
    python3 zones_bay_scene.py sheet      # assets/tiles_bay/_sheet{,_tiles,_props,_dyn}.png
    python3 zones_bay_scene.py monsters   # assets/monsters/_sheet_{fishman,crab,jelly}.png,
                                          #   assets/bosses/_sheet_kraken.png, assets/monsters/_scene_bay*.png
    python3 zones_bay_scene.py budget     # bytes per sprite group with the packer's encoding

Scenes are built from a small ASCII map: rows from the BACK of the level
(top of the screen) to the FRONT (y = 0); a cell is a legend letter plus a
height character (0-3, or '.' for a pit: the legend's surface goes in it).
The _glow versions add the glow passes the way the watch does.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
import compose  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from zones_df_scene import render_glow, TOMMY  # noqa: E402

FLOOR_M = compose.FLOOR_M
SURF_DZ = -0.18
Z = 'bay'
TILES = os.path.join(ROOT, 'assets', 'tiles_bay')
MONS = os.path.join(ROOT, 'assets', 'monsters')
BOSS = os.path.join(ROOT, 'assets', 'bosses')

LEGEND = {
    's': dict(top='sand', fill='bay_fill_sand'),
    'r': dict(top='rock', fill='bay_fill_rock', salt=3),
    'q': dict(top='quay', fill='bay_fill_quay', salt=5),
    'p': dict(top='plank_x', fill='bay_fill_sand'),
    'P': dict(top='plank_y', fill='bay_fill_sand'),
    't': dict(top='bay_tide_00', fill='bay_fill_sand'),
    'u': dict(top='bay_tide_01', fill='bay_fill_sand'),
    'w': dict(top='bay_tide_02', fill='bay_fill_sand'),
    '~': dict(surf='bay_surf_sea'),
    '@': dict(surf='bay_surf_deep'),
    'X': dict(surf='bay_surf_sea', bridge='bay_bridge_x'),
    'Y': dict(surf='bay_surf_sea', bridge='bay_bridge_y'),
}


def _pick(vs, x, y, salt=0):
    if not vs:
        return None
    h = (x * 73856093) ^ (y * 19349663) ^ (salt * 83492791)
    return vs[(h >> 3) % len(vs)]


def build(d, types, heights, items, look_at, bg, extra_assets=(), palettes=None):
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    types = list(reversed(types))
    heights = list(reversed(heights))
    out = []
    floor_of = {}
    for y in range(len(types)):
        for x in range(len(types[0])):
            c, hc = types[y][x], heights[y][x]
            if c == ' ':
                continue
            Lg = LEGEND[c]
            if hc == '.':
                sv = sorted(k for k in meta if k.startswith(Lg['surf'] + '_v'))
                name = _pick(sv, x, y, 7)
                if name:
                    out.append([name, x, y, 0, {"at": [x + 0.5, y + 0.5, SURF_DZ]}])
                if 'bridge' in Lg and Lg['bridge'] in meta:
                    out.append([Lg['bridge'], x, y, 0, {"at": [x + 0.5, y + 0.5, 0.0]}])
                    floor_of[(x, y)] = 0
                else:
                    floor_of[(x, y)] = SURF_DZ / FLOOR_M
                continue
            h = int(hc)
            top = Lg['top']
            name = top if top in meta else _pick(sorted(k for k in meta if k.startswith('%s_blk_%s_v' % (Z, top))),
                                                  x, y, Lg.get('salt', 0))
            if name is None:
                print('missing block', top)
                continue
            out.append([name, x, y, h])
            for k in range(h - 1, -2, -1):
                if Lg['fill'] in meta:
                    out.append([Lg['fill'], x, y, k])
            floor_of[(x, y)] = h
    for it in items:
        name, x, y = it[:3]
        opt = dict(it[3]) if len(it) > 3 else {}
        fl = opt.pop('floor', floor_of.get((x, y), 0))
        out.append([name, x, y, fl, opt] if opt else [name, x, y, fl])
    assets = [".", os.path.relpath(os.path.join(ROOT, 'assets', '_test'), d)] + list(extra_assets)
    pals = {"tommy": TOMMY}
    pals.update(palettes or {})
    return {"assets": assets, "size": [368, 448], "look_at": look_at, "bg": bg, "palettes": pals, "items": out}


def _monster_palettes(mdir):
    p = os.path.join(mdir, 'palettes.json')
    if not os.path.exists(p):
        return {}
    with open(p) as fh:
        pals = json.load(fh)
    return {k: v for k, v in pals.items() if not k.startswith('_')}


def _have(mdir, name):
    p = os.path.join(mdir, 'meta.json')
    if not os.path.exists(p):
        return False
    with open(p) as fh:
        return name in json.load(fh)




def _extras(d, dirs):
    """Asset dirs (relative to d) and palettes of the monster/boss folders."""
    rels, pals = [], {}
    for md in dirs:
        rels.append(os.path.relpath(md, d))
        pals.update(_monster_palettes(md))
    return rels, pals


def _have_any(dirs, name):
    return any(_have(md, name) for md in dirs)


def scene(d=TILES):
    """The harbour at night, as the 368 x 448 window sees it (the approved
    sample's layout): the wreck on the back beach, the lighthouse on its
    rocks, tide cells in their three states, a mossy quay with crates, a
    bollard and a lobster trap, the pier on stilts over the sea with Tommy on
    it, a whirlpool, a buoy; the fish-man climbing out next to the pier, a
    jellyfish and a crab."""
    types = [
        "sssssw~~",
        "sssssu~~",
        "ssssst~~",
        "sssstrr~",
        "qqqqwrr~",
        "qqqq~~~~",
        "qqqXXX~~",
        "sss~~Y@~",
        "sss~~Y~~",
    ]
    heights = [
        "000000..",
        "000000..",
        "000000..",
        "0000000.",
        "0000000.",
        "0000....",
        "000.....",
        "000.....",
        "000.....",
    ]
    items = [
        ['bay_wreck_stern', 1, 6],
        ['bay_wreck_bow', 3, 6],
        ['bay_lighthouse', 5, 4],
        ['bay_tidepool', 3, 5],
        ['bay_rock_low', 2, 5],
        ['bay_anchor', 1, 5],
        ['bay_trap', 2, 4],
        ['bay_crates', 3, 3],
        ['bay_bollard', 2, 3],
        ['bay_buoy', 7, 2, {"at": [7.5, 2.5, SURF_DZ]}],
        ['tommy_test', 4, 2, {"palette": "tommy", "floor": 0}],
        ['fishman_emerge_s_02', 3, 1, {"palette": "fishman", "at": [3.5, 1.5, SURF_DZ]}],
        ['jelly_float_s_00', 6, 3, {"palette": "jelly", "at": [6.5, 3.5, SURF_DZ]}],
        ['crab_idle_s_00', 3, 4, {"palette": "crab"}],
    ]
    rels, pals = _extras(d, [MONS])
    return build(d, types, heights, items, look_at=[4.11, 5.47, 0.0], bg=[6, 16, 28], extra_assets=rels,
                 palettes=pals)


def scene_b(d=TILES):
    """The open bay: the exit gate on the quay with a harbour lamp, a drying
    net, barrels and a cargo crate; a driftwood raft and a sinking buoy-raft;
    the Kraken's tentacle raised over a row (its shadow marks the row); a row
    of boiling piranhas; a wave rolling in; the fish-man lurking."""
    types = [
        "qqqqqq~~",
        "qqqqqq~~",
        "qqqqqq~~",
        "qqqqqq~~",
        "~~~~~~~~",
        "~~~~~~~~",
        "~~~~~~~~",
        "sss~~~~~",
        "ssss~~~~",
    ]
    heights = [
        "000000..",
        "000000..",
        "000000..",
        "000000..",
        "........",
        "........",
        "........",
        "000.....",
        "0000....",
    ]
    items = [
        ['bay_gate_06', 2, 7],
        ['bay_lamp', 4, 7],
        ['bay_net', 1, 8],
        ['bay_barrels', 5, 6],
        ['bay_crate', 3, 5],
        ['tommy_test', 2, 5, {"palette": "tommy"}],
        ['fishman_walk_e_01', 4, 5, {"palette": "fishman"}],
        ['bay_logfloat_w', 2, 4, {"at": [2.5, 4.5, -0.02]}],
        ['bay_logfloat_m', 3, 4, {"at": [3.5, 4.5, -0.02]}],
        ['bay_logfloat_e', 4, 4, {"at": [4.5, 4.5, -0.02]}],
        ['bay_lily_00', 6, 4, {"at": [6.5, 4.5, -0.17]}],
        ['bay_tentacle_w_01', 2, 3, {"at": [2.5, 3.5, SURF_DZ]}],
        ['bay_tentacle_m_01', 3, 3, {"at": [3.5, 3.5, SURF_DZ]}],
        ['bay_tentacle_m_01', 4, 3, {"at": [4.5, 3.5, SURF_DZ]}],
        ['bay_tentacle_m_01', 5, 3, {"at": [5.5, 3.5, SURF_DZ]}],
        ['bay_tentacle_e_01', 6, 3, {"at": [6.5, 3.5, SURF_DZ]}],
        ['bay_piranha_01', 3, 2, {"at": [3.5, 2.5, SURF_DZ]}],
        ['bay_piranha_03', 4, 2, {"at": [4.5, 2.5, SURF_DZ]}],
        ['bay_piranha_02', 5, 2, {"at": [5.5, 2.5, SURF_DZ]}],
        ['bay_piranha_00', 6, 2, {"at": [6.5, 2.5, SURF_DZ]}],
        ['bay_wave_y_01', 6, 1, {"at": [6.5, 1.5, SURF_DZ]}],
        ['fishman_lurk_s_00', 5, 1, {"palette": "fishman", "at": [5.5, 1.5, SURF_DZ]}],
        ['crab_walk_s_02', 3, 0, {"palette": "crab"}],
    ]
    rels, pals = _extras(d, [MONS])
    return build(d, types, heights, items, look_at=[4.11, 5.47, 0.0], bg=[6, 16, 28], extra_assets=rels,
                 palettes=pals)


def render(d, tag, sc):
    js = os.path.join(d, '_%s.json' % tag)
    with open(js, 'w') as fh:
        json.dump(sc, fh, indent=0)
    img = compose.render_scene(sc, d)
    img.save(os.path.join(d, '_%s.png' % tag))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_%s_2x.png' % tag))
    img = render_glow(sc, d)
    img.save(os.path.join(d, '_%s_glow.png' % tag))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(d, '_%s_glow_2x.png' % tag))
    print('wrote', os.path.join(d, '_%s.png' % tag))


def _sheet(d, meta, names, out, scale=2, cols=6):
    ims = [Image.open(os.path.join(d, meta[n]['files']['img'])).convert('RGBA') for n in names]
    sc = [scale if im.width <= 110 else 1 for im in ims]
    W = max(i.width * s for i, s in zip(ims, sc))
    Hh = max(i.height * s for i, s in zip(ims, sc)) + 14
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new('RGBA', (cols * (W + 8), rows * (Hh + 8)), (10, 20, 32, 255))
    dr = ImageDraw.Draw(sh)
    for k, (n, im, s) in enumerate(zip(names, ims, sc)):
        x = (k % cols) * (W + 8)
        y = (k // cols) * (Hh + 8)
        sh.alpha_composite(im.resize((im.width * s, im.height * s), Image.NEAREST), (x, y + 14))
        dr.text((x + 2, y + 1), '%s %dx%d%s' % (n.replace(Z + '_', ''), meta[n]['w'], meta[n]['h'],
                                                ' (1x)' if s == 1 else ''), fill=(230, 230, 230, 255))
    sh.convert('RGB').save(out)
    print('wrote', out)


def sheet(d=TILES):
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    names = sorted(k for k in meta if not k.startswith('_'))
    groups = {'tiles': ('tile', 'surf'), 'props': ('prop', 'bridge'), 'dyn': ('dyn',)}
    for g, kinds in groups.items():
        _sheet(d, meta, [n for n in names if meta[n]['kind'] in kinds], os.path.join(d, '_sheet_%s.png' % g),
               cols=6 if g != 'dyn' else 8)
    _sheet(d, meta, names, os.path.join(d, '_sheet.png'), cols=10)


# ---------------------------------------------------------------------------
# monsters
# ---------------------------------------------------------------------------

def _recolour(d, info, pal):
    f = info['files']
    img = np.asarray(Image.open(os.path.join(d, f['img'])).convert('RGBA')).astype(np.float32)
    ids = np.asarray(Image.open(os.path.join(d, f['id'])).convert('L')) // 16
    lut = np.zeros((16, 3), np.float32)
    lut[:] = (255, 0, 255)
    for k, c in pal.items():
        lut[int(k)] = c
    col = np.clip(lut[ids] * img[..., :3] / 196.0, 0, 255)
    return col, img[..., 3] / 255.0


ANIM_ORDER = ['lurk', 'emerge', 'idle', 'walk', 'dive', 'snap', 'float', 'slam']


def frames_sheet(d, who, out, scale=2, bg=(18, 40, 58)):
    """Every frame of one monster/boss recoloured, 1x-scaled, a row per
    anim and facing, the card at the end."""
    with open(os.path.join(d, 'meta.json')) as fh:
        meta = json.load(fh)
    pals = _monster_palettes(d)
    names = sorted(k for k, v in meta.items() if not k.startswith('_') and (v.get('monster') == who or
                                                                           v.get('boss') == who))
    rows = {}
    for n in names:
        info = meta[n]
        key = 'card' if info.get('kind') == 'card' else '%s_%s' % (info['anim'], info['dir'])
        rows.setdefault(key, []).append(n)
    order = sorted(rows, key=lambda k: (ANIM_ORDER.index(k.split('_')[0]) if k != 'card' else 99,
                                        'sewn'.index(k[-1]) if k != 'card' else 0))
    cw = max(meta[n]['w'] for n in names if meta[n].get('kind') != 'card') * scale + 6
    ch = max(meta[n]['h'] for n in names if meta[n].get('kind') != 'card') * scale + 18
    card = [n for n in names if meta[n].get('kind') == 'card']
    ncol = max(len(v) for k, v in rows.items() if k != 'card')
    W = max(110 + ncol * cw, 110 + (meta[card[0]]['w'] + 6 if card else 0))
    H = (len(order) - (1 if card else 0)) * ch + (meta[card[0]]['h'] + 18 if card else 0)
    im = Image.new('RGB', (W, H), (12, 14, 20))
    dr = ImageDraw.Draw(im)
    y = 0
    for key in order:
        dr.text((6, y + 12), key, fill=(220, 220, 220))
        s = 1 if key == 'card' else scale
        rh = 0
        for c, n in enumerate(sorted(rows[key])):
            info = meta[n]
            col, al = _recolour(d, info, pals[who])
            h, w = al.shape
            cv = np.zeros((h, w, 3), np.float32)
            cv[:] = bg
            if 'sh' in info['files']:
                shd = np.asarray(Image.open(os.path.join(d, info['files']['sh'])).convert('L')) / 255.0 * 0.55
                cv *= (1 - shd[..., None])
            cv = cv * (1 - al[..., None]) + col * al[..., None]
            t = Image.fromarray(np.clip(cv, 0, 255).astype(np.uint8)).resize((w * s, h * s), Image.NEAREST)
            im.paste(t, (110 + c * cw, y))
            dr.text((110 + c * cw, y + h * s + 2), '%s %dx%d' % (n.split('_')[-1] if key != 'card' else n, w, h),
                    fill=(160, 160, 160))
            rh = max(rh, h * s + 18)
        y += rh
    im.crop((0, 0, W, y)).save(out)
    print('wrote', out)


def monsters(d=TILES):
    """Contact sheets of the bay monsters and the Kraken, and a compose scene
    of them in the bay (assets/monsters/_scene_bay*.png)."""
    for who in ('fishman', 'crab', 'jelly'):
        frames_sheet(MONS, who, os.path.join(MONS, '_sheet_%s.png' % who))
    frames_sheet(BOSS, 'kraken', os.path.join(BOSS, '_sheet_kraken.png'), scale=1)
    types = ["~~~~~~~~~"] * 5 + ["qqqPqqqqq", "sssPsssss", "sssssssss", "~~~~~~~~~", "sssssssss", "sssssssss"]
    heights = ["........."] * 5 + ["000000000", "000000000", "000000000", ".........", "000000000", "000000000"]
    items = [
        ['kraken_slam_s_01', 4, 8, {"palette": "kraken", "at": [5.0, 9.0, SURF_DZ]}],
        ['jelly_float_s_00', 1, 7, {"palette": "jelly", "at": [1.5, 7.5, SURF_DZ]}],
        ['jelly_float_s_02', 7, 6, {"palette": "jelly", "at": [7.5, 6.5, SURF_DZ]}],
        ['fishman_lurk_s_00', 2, 9, {"palette": "fishman", "at": [2.5, 9.5, SURF_DZ]}],
        ['fishman_emerge_s_02', 6, 7, {"palette": "fishman", "at": [6.5, 7.5, SURF_DZ]}],
        ['fishman_dive_s_02', 0, 6, {"palette": "fishman", "at": [0.5, 6.5, SURF_DZ]}],
        ['fishman_idle_s_00', 1, 5, {"palette": "fishman"}],
        ['fishman_walk_e_01', 6, 5, {"palette": "fishman"}],
        ['crab_snap_s_02', 4, 4, {"palette": "crab"}],
        ['tommy_test', 2, 4, {"palette": "tommy"}],
        ['fishman_idle_w_00', 7, 1, {"palette": "fishman"}],
        ['crab_walk_e_02', 1, 0, {"palette": "crab"}],
    ]
    for x in range(0, 9):
        items.append(['bay_piranha_%02d' % (1 + x % 4), x, 2, {"at": [x + 0.5, 2.5, SURF_DZ]}])
    rels, pals = _extras(d, [MONS, BOSS])
    sc = build(d, types, heights, items, look_at=[4.5, 5.3, 0.2], bg=[6, 16, 28], extra_assets=rels, palettes=pals)
    # the JSON lives in assets/monsters: asset paths relative to it
    sc['assets'] = [os.path.relpath(os.path.normpath(os.path.join(d, a)), MONS) for a in sc['assets']]
    with open(os.path.join(MONS, '_scene_bay.json'), 'w') as fh:
        json.dump(sc, fh, indent=0)
    img = compose.render_scene(sc, MONS)
    img.save(os.path.join(MONS, '_scene_bay.png'))
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(MONS, '_scene_bay_2x.png'))
    img = render_glow(sc, MONS)
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(os.path.join(MONS, '_scene_bay_glow_2x.png'))
    print('wrote', os.path.join(MONS, '_scene_bay.png'))


def budget():
    """Bytes each sprite takes on the watch, with the packer's own frame
    encoding (tools/pack_assets.py: cropped spans, 3 B/px light+id+z for the
    recoloured, 4 B/px colour+alpha+z for tiles, 1 B/px shadow, 2 B/px glow),
    uncompressed, as it sits in PSRAM."""
    import pack_assets as PK

    def size(dd, info):
        f = info['files']
        n = len(PK.frame_main(dd, info)[1])
        s = len(PK.frame_plane(dd, f['sh'], info)) if 'sh' in f else 0
        g = len(PK.frame_glow(dd, f['gl'], info)) if 'gl' in f else 0
        return n, s, g

    def load(dd):
        with open(os.path.join(dd, 'meta.json')) as fh:
            return json.load(fh)
    print('%-22s %6s %9s %9s %8s %9s  %s' % ('group', 'frames', 'main B', 'shadow B', 'glow B', 'total KB',
                                             'max w x h'))
    for dd, key, whos in ((MONS, 'monster', ('fishman', 'crab', 'jelly', 'piranha')), (BOSS, 'boss', ('kraken',))):
        meta = load(dd)
        for who in whos:
            tot = [0, 0, 0, 0, 0, 0]
            card = 0
            for k, v in sorted(meta.items()):
                if k.startswith('_') or v.get(key) != who:
                    continue
                n, s, g = size(dd, v)
                if v.get('kind') == 'card':
                    card += n
                    continue
                tot[0] += 1
                tot[1] += n
                tot[2] += s
                tot[3] += g
                tot[4] = max(tot[4], v['w'])
                tot[5] = max(tot[5], v['h'])
            t = tot[1] + tot[2] + tot[3]
            print('%-22s %6d %9d %9d %8d %9.1f  %dx%d   (+ card %.1f KB, album only)' %
                  (who, tot[0], tot[1], tot[2], tot[3], t / 1024, tot[4], tot[5], card / 1024))
    meta = load(TILES)
    groups = {}
    for k, v in sorted(meta.items()):
        if k.startswith('_'):
            continue
        base = k
        for suf in ('_v0', '_v1', '_v2'):
            if base.endswith(suf):
                base = base[:-3]
        if base[-3:-2] == '_' and base[-2:].isdigit():
            base = base[:-3]
        n, s, g = size(TILES, v)
        a = groups.setdefault((v['kind'], base), [0, 0, 0, 0])
        a[0] += 1
        a[1] += n
        a[2] += s
        a[3] += g
    kinds = {}
    for (kind, base), (c, n, s, g) in groups.items():
        kinds[kind] = kinds.get(kind, 0) + n + s + g
    print()
    print('tiles_bay by sprite (frames/variants, KB):')
    for (kind, base), (c, n, s, g) in sorted(groups.items(), key=lambda t: -(t[1][1] + t[1][2] + t[1][3])):
        print('   %-6s %-26s %2d  %6.1f' % (kind, base, c, (n + s + g) / 1024))
    print('tiles_bay by kind: ' + ', '.join('%s %.0f KB' % (k, b / 1024) for k, b in sorted(kinds.items())))
    print('tiles_bay total: %d sprites, %.0f KB' % (sum(v[0] for v in groups.values()), sum(kinds.values()) / 1024))


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'scene'
    if what == 'budget':
        budget()
    elif what == 'scene':
        render(TILES, 'scene', scene())
    elif what == 'scene_b':
        render(TILES, 'scene_b', scene_b())
    elif what == 'sheet':
        sheet()
    elif what == 'monsters':
        monsters()
