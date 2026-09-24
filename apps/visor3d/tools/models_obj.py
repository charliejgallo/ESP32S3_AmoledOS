"""The viewer's sample models, out of the games' own Blender builders, as
coloured OBJ + MTL files.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P models_obj.py -- \\
        --model tommy|zombie|muscle --out /tmp/models

  tommy   Monster Hop's hero, idle, facing us, with his red cap
          (apps/monsterhop/tools/blender/chars.py)
  zombie  Monster Hop's zombie, idle, in its default clothes (monsters.py)
  muscle  Turbo's muscle car, navy with white stripes (turbo/tools/blender/cars.py)

Mila has her own script, mila_obj.py. Like it, this builds each model with
the code that renders the game's sprites and writes the meshes triangulated,
in world space, Y up. The games paint their sprites on the watch by region
id, so an OBJ's flat colour per face is the game's palette for that id; for
the car, whose stripes, windows and lamps are masks in the shader rather than
separate meshes, the masks are evaluated here, per face. Convert the result
with the portal's /3d page (it reduces the model and keeps the colours).
"""
import json
import os
import re
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
APPS = os.path.join(HERE, '..', '..')

import bmesh                   # noqa: E402
import bpy                     # noqa: E402


def arg(argv, k, default):
    return argv[argv.index(k) + 1] if k in argv else default


def load(path, name):
    """Imports a Blender script whose last line runs it: the source without
    that line (running chars.py or monsters.py starts rendering sprites)."""
    d = os.path.dirname(os.path.abspath(path))
    if d not in sys.path:
        sys.path.insert(0, d)
    src = open(path).read()
    src = re.sub(r'\n(if __name__ == .__main__.:\s*\n\s+)?main\(\)\s*$', '\n', src)
    mod = types.ModuleType(name)
    mod.__file__ = path
    sys.modules[name] = mod
    exec(compile(src, path, 'exec'), mod.__dict__)
    return mod


def write_obj(out, stem, title, objs, colour, local=False, split=None, turn=False):
    """colour(ob, face, key, co, no) -> (r, g, b) 0..255; key is the face's
    material name without its suffix; co/no the face's centre and normal in
    the object's own space. split(ob) -> n cuts every edge of that object n
    times first, so a colour decided per face follows a mask more closely.
    turn: half a turn about the vertical, for a model built facing +Y (the
    viewer's first view shows what faces -Y, as the characters do)."""
    dg = bpy.context.evaluated_depsgraph_get()
    verts, faces, mats = [], [], {}
    for ob in objs:
        if ob.type != 'MESH' or ob.hide_render:
            continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        bm = bmesh.new()
        bm.from_mesh(me)
        cuts = split(ob) if split else 0
        if cuts:
            bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=cuts, use_grid_fill=True)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.normal_update()
        base = len(verts)
        mw = ob.matrix_world
        for v in bm.verts:
            w = v.co if local else mw @ v.co
            if turn:
                verts.append((-w.x, w.z, w.y))
            else:
                verts.append((w.x, w.z, -w.y))      # Z up -> Y up
        slots = ev.material_slots
        for f in bm.faces:
            m = slots[f.material_index].material if len(slots) > f.material_index else None
            key = re.sub(r'^S_', '', m.name).split('.')[0] if m else ''
            col = tuple(int(c) for c in colour(ob, f, key, f.calc_center_median(), f.normal))
            name = 'c_%02x%02x%02x' % col
            mats[name] = col
            faces.append((name, [base + v.index for v in f.verts]))
        bm.free()
        ev.to_mesh_clear()

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, stem + '.mtl'), 'w') as f:
        for name, (r, g, b) in sorted(mats.items()):
            f.write('newmtl %s\nKd %.4f %.4f %.4f\n' % (name, r / 255, g / 255, b / 255))
    with open(os.path.join(out, stem + '.obj'), 'w') as f:
        f.write('# %s - AmoledOS apps/visor3d/tools/models_obj.py\n' % title)
        f.write('mtllib %s.mtl\n' % stem)
        for v in verts:
            f.write('v %.5f %.5f %.5f\n' % v)
        cur = None
        for name, idx in faces:
            if name != cur:
                f.write('usemtl %s\n' % name)
                cur = name
            f.write('f %s\n' % ' '.join(str(i + 1) for i in idx))
    print('[models_obj] %s: %d vertices, %d triangles, %d colours -> %s' % (
        stem, len(verts), len(faces), len(mats), out))


# ---------------------------------------------------------------------------
# Monster Hop
# ---------------------------------------------------------------------------

def tommy(out):
    mh = os.path.join(APPS, 'monsterhop')
    ch = load(os.path.join(mh, 'tools', 'blender', 'chars.py'), 'mh_chars')
    C = ch.C
    pal = json.load(open(os.path.join(mh, 'assets', 'chars', 'palettes.json')))
    body = pal['looks']['default']['tommy']
    cap = pal['looks']['default']['cap_cap']

    C.reset('neutral')
    ch.register_materials()
    p = ch.ANIMS['idle'][3](0)
    root = ch.root_matrix(ch.YAW['s'], p['sq'], p['flip'])
    objs, J = ch.build_tommy(p, root)
    layer = ch.build_layer('cap', 'cap', J, root, ch.layer_ctx('idle', 0, 4, p))
    capped = set(o.name for o in layer)

    def colour(ob, f, key, co, no):
        m = C._MATS.get(key)
        mid = str(m.id if m is not None else 1)
        p = cap if ob.name in capped else body
        return p.get(mid, [200, 200, 200])

    write_obj(out, 'tommy', 'Tommy (Monster Hop), idle, facing us', list(objs) + list(layer), colour)


def zombie(out):
    mh = os.path.join(APPS, 'monsterhop')
    ms = load(os.path.join(mh, 'tools', 'blender', 'monsters.py'), 'mh_monsters')
    C = ms.C
    pal = ms.PALETTES['zombie']

    C.reset('neutral')
    ms.register_mats(pal, extra=ms.EXTRA_MATS.get('zombie'))
    rig = ms.BUILD['zombie']()
    n = [x for x in ms.ANIMS['zombie'] if x[0] == 'idle'][0][2]
    P = dict(ms.POSE['zombie']('idle', 0, n, 's'))
    hide = P.pop('_hide', ())
    P.pop('_cape', None)
    rig.pose(P, ms.YAW['s'])
    objs = rig.visible(hide)
    bpy.context.view_layer.update()

    def colour(ob, f, key, co, no):
        m = C._MATS.get(key)
        return pal.get(m.id if m is not None else 1, (200, 200, 200))

    write_obj(out, 'zombie', 'Zombie (Monster Hop), idle, facing us', objs, colour)


# ---------------------------------------------------------------------------
# Turbo
# ---------------------------------------------------------------------------

# tb_art.c: paint_base() and the second paint (navy, white stripes)
TURBO = {'paintA': 0x14285A, 'paintB': 0xF2F2F2, 'glass': 0x2A3440, 'chrome': 0xD4D8DC,
         'black': 0x1E2022, 'tyre': 0x1C1C1C, 'rim': 0xB8BCC2, 'tail': 0xB01810, 'head': 0xF0F0E4,
         'plate': 0xE6E6DC, 'interior': 0x2C2622, 'under': 0x141414, 'amber': 0xE88A12, 'extra': 0x2A2A2C}


def muscle(out):
    cars = load(os.path.join(APPS, 'turbo', 'tools', 'blender', 'cars.py'), 'tb_cars')
    masks = {}
    real = cars.make_material

    def record(name, entries, default):
        masks[name] = (entries, default)
        real(name, entries, default)

    cars.make_material = record
    bpy.ops.wm.read_factory_settings(use_empty=True)
    cars.make_base_materials()
    car = cars.BUILDERS['muscle']()
    car.finish()
    bpy.context.view_layer.update()

    def inside(reg, P, N):
        for cp, cn, c in reg:
            if P[0] * cp[0] + P[1] * cp[1] + P[2] * cp[2] + N[0] * cn[0] + N[1] * cn[1] + N[2] * cn[2] <= c:
                return False
        return True

    def colour(ob, f, key, co, no):
        # the shader's masks: (|x|, y, z) and (|nx|, ny, nz) in object space
        entries, default = masks.get(key, ([], key))
        P = (abs(co.x), co.y, co.z)
        N = (abs(no.x), no.y, no.z)
        region = default
        for k, regs in entries:            # highest priority first
            if any(inside(r, P, N) for r in regs):
                region = k
                break
        c = TURBO.get(region, 0x808080)
        return (c >> 16, (c >> 8) & 255, c & 255)

    def split(ob):
        # only what has masks (the body and the cabin: stripes, windows)
        return 2 if any(masks.get(m.name[2:], ([], ''))[0] for m in ob.data.materials) else 0

    write_obj(out, 'muscle', 'Muscle car (Turbo), navy with white stripes', car.objs, colour, local=True,
              split=split, turn=True)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = arg(argv, '--out', '/tmp/models')
    what = arg(argv, '--model', 'tommy')
    {'tommy': tommy, 'zombie': zombie, 'muscle': muscle}[what](out)


main()
