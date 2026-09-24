"""Mila, the kitten of the Mila app, as a coloured OBJ + MTL for the 3D viewer.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P mila_obj.py -- --out /tmp/mila \
        [--anim c_sit] [--frame 0] [--dir s] [--items hat_bow,neck_bell]

It builds her with the game's own rig and poses (apps/mila/tools/blender:
mila_rig.py, mila_poses.py), so she is the same cat the game renders, and
writes every mesh triangulated, in world space, Y up. The game's materials are
node shaders made for the renders (a fur with a glowing rim, amber eyes with a
ring); an OBJ carries one flat colour per material, so each gets the colour it
reads as, and the accessories the palette the game paints them with. Then
convert it with the portal's /3d page, which reduces it and keeps the colours.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'mila', 'tools', 'blender'))

import bmesh                   # noqa: E402
import bpy                     # noqa: E402
import ml_common as C          # noqa: E402
import mila_rig as R           # noqa: E402
import mila_poses as MP        # noqa: E402

# The accessories' colours, as mila.py paints them (copied, not imported:
# importing mila.py runs it, and it starts rendering the game's sprites).
PALETTES = {
    'hat_bow': {'1': [245, 120, 170], '2': [220, 80, 140]},
    'hat_party': {'1': [90, 215, 165], '2': [250, 250, 250], '3': [255, 120, 160]},
    'hat_crown': {'1': [255, 196, 60], '3': [230, 40, 95]},
    'hat_beret': {'1': [205, 45, 65], '2': [150, 28, 45]},
    'hat_beanie': {'1': [95, 150, 235], '2': [245, 245, 245]},
    'hat_flower': {'1': [255, 150, 195], '2': [255, 210, 70], '3': [95, 190, 95]},
    'hat_bunny': {'1': [245, 245, 250], '2': [255, 165, 195]},
    'hat_witch': {'1': [120, 72, 195], '2': [255, 150, 60], '3': [255, 215, 80]},
    'neck_bell': {'1': [225, 45, 55], '3': [255, 200, 70]},
    'neck_fish': {'1': [60, 125, 235], '3': [215, 225, 235]},
    'neck_pearls': {'1': [248, 242, 232]},
    'neck_bandana': {'1': [230, 60, 75]},
    'neck_dots': {'1': [70, 130, 230], '2': [255, 255, 255]},
    'neck_bow': {'1': [235, 55, 85], '2': [190, 35, 65]},
}

# What each of her own materials reads as, sRGB 0..255. The fur is not pure
# black: a flat black cat would be a hole in any background but a light one.
FLAT = {
    'fur': (46, 40, 56), 'eye': (255, 186, 24), 'pupil': (12, 10, 14), 'glint': (255, 255, 255),
    'pink': (242, 115, 140), 'blush': (217, 77, 107), 'whisker': (220, 220, 232),
    'lash': (128, 138, 178), 'mouthline': (140, 102, 117), 'mouth': (82, 8, 18),
    'tongue': (255, 128, 153), 'L_dark': (48, 36, 46),
}


def arg(argv, k, default):
    return argv[argv.index(k) + 1] if k in argv else default


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = arg(argv, '--out', '/tmp/mila')
    anim = arg(argv, '--anim', 'c_sit')
    frame = int(arg(argv, '--frame', '0'))
    d = arg(argv, '--dir', 's')
    items = [i for i in arg(argv, '--items', 'hat_bow,neck_bell').split(',') if i]

    C.reset('neutral')
    R.register_materials()
    P = MP.pose(anim, frame, d)
    rig = R.build(P, items=items, anchor=C.cell(0, 0, 0), yaw=R.FACING[d])
    item_of = {}
    for it, objs in rig['layers'].items():
        for o in objs:
            item_of[o.name] = it

    dg = bpy.context.evaluated_depsgraph_get()
    verts, faces, mats = [], [], {}
    for ob in bpy.context.scene.objects:
        if ob.type != 'MESH' or ob.hide_render:
            continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        base = len(verts)
        mw = ob.matrix_world
        for v in bm.verts:
            w = mw @ v.co
            verts.append((w.x, w.z, -w.y))          # Z up -> Y up
        for f in bm.faces:
            m = ob.material_slots[f.material_index].material if ob.material_slots else None
            key = m.name.split('.')[0] if m else 'fur'
            it = item_of.get(ob.name)
            if key in FLAT:
                col = FLAT[key]
            elif it and it in PALETTES:
                m_ = C._MATS.get(key)            # the registry: id = which palette colour
                mid = str(m_.id if m_ is not None and m_.id else 1)
                col = tuple(PALETTES[it].get(mid, list(PALETTES[it].values())[0]))
            else:
                col = (200, 200, 200)
            name = '%s_%02x%02x%02x' % (key, *col)
            mats[name] = col
            faces.append((name, [base + v.index for v in f.verts]))
        bm.free()
        ev.to_mesh_clear()

    os.makedirs(out, exist_ok=True)
    stem = 'mila'
    with open(os.path.join(out, stem + '.mtl'), 'w') as f:
        for name, (r, g, b) in sorted(mats.items()):
            f.write('newmtl %s\nKd %.4f %.4f %.4f\n' % (name, r / 255, g / 255, b / 255))
    with open(os.path.join(out, stem + '.obj'), 'w') as f:
        f.write('# Mila (%s, frame %d, facing %s) - AmoledOS apps/visor3d/tools/mila_obj.py\n' % (anim, frame, d))
        f.write('mtllib %s.mtl\n' % stem)
        for v in verts:
            f.write('v %.5f %.5f %.5f\n' % v)
        cur = None
        for name, idx in faces:
            if name != cur:
                f.write('usemtl %s\n' % name)
                cur = name
            f.write('f %s\n' % ' '.join(str(i + 1) for i in idx))
    print('[mila_obj] %d vertices, %d triangles, %d materials -> %s' % (len(verts), len(faces), len(mats), out))


main()
