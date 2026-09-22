"""Pipeline check: two blocks and a stand-in Tommy rendered one at a time,
for tools/compose.py to put side by side. Not game art.

    Blender -b -P test_tiles.py -- --out /tmp/mh_test
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
from mathutils import Vector as V  # noqa: E402
import bpy  # noqa: E402

a = C.args()
C.reset('forest', cpu=a.cpu)


def grass_build(nt, neutral):
    b = nt.nodes.new('ShaderNodeBsdfPrincipled')
    noise = nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 9.0
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (0.10, 0.40, 0.10, 1)
    ramp.color_ramp.elements[1].color = (0.22, 0.62, 0.16, 1)
    nt.links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
    if not neutral:
        nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.9
    return b.outputs['BSDF']


C.mat('grass', build=grass_build)
C.mat('dirt', base=(0.36, 0.22, 0.12), rough=0.95)
C.mat('skin', base=(0.95, 0.72, 0.55), id=1)
C.mat('shirtA', base=(0.8, 0.8, 0.8), id=5)
C.mat('pants', base=(0.8, 0.8, 0.8), id=8)
C.mat('capA', base=(0.8, 0.8, 0.8), id=11)
C.mat('eyes', base=(0.05, 0.05, 0.05), id=3)

F = C.FLOOR_M


def block(top_key, side_key):
    # a unit cell, one floor tall, its top at z = 0
    top = C.box('top', 0, 0, -0.06, 1, 1, 0, top_key)
    side = C.box('side', 0, 0, -F, 1, 1, -0.06, side_key)
    return [top, side]


objs = block('grass', 'dirt')
C.render_sprite(a.out, 'blk_grass', objs, C.cell(0, 0, 0), passes=('color', 'z'), kind='tile')
C.remove(objs)
objs = block('dirt', 'dirt')
C.render_sprite(a.out, 'blk_dirt', objs, C.cell(0, 0, 0), passes=('color', 'z'), kind='tile')
C.remove(objs)

# stand-in Tommy at the origin cell
C.set_light('neutral')
p = C.cell(0, 0, 0)


def sph(c, r, key, s=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(location=c, radius=r, segments=24, ring_count=12)
    o = bpy.context.object
    o.scale = s
    bpy.ops.object.shade_smooth()
    C.assign(o, key)
    return o


def cyl(c, r, d, key):
    bpy.ops.mesh.primitive_cylinder_add(location=c, radius=r, depth=d, vertices=20)
    o = bpy.context.object
    bpy.ops.object.shade_smooth()
    C.assign(o, key)
    return o


t = [cyl(p + V((-0.08, 0, 0.11)), 0.06, 0.22, 'pants'), cyl(p + V((0.08, 0, 0.11)), 0.06, 0.22, 'pants'),
     sph(p + V((0, 0, 0.36)), 0.17, 'shirtA', (1, 0.85, 1)), sph(p + V((0, 0, 0.72)), 0.25, 'skin'),
     sph(p + V((0, 0, 0.80)), 0.255, 'capA', (1, 1, 0.7)),
     sph(p + V((-0.08, -0.22, 0.70)), 0.035, 'eyes'), sph(p + V((0.08, -0.22, 0.70)), 0.035, 'eyes')]
C.render_sprite(a.out, 'tommy_test', t, p, passes=('light', 'id', 'z', 'shadow'), shadow_z=0.0,
                bounce_ground=0.0, kind='char')
C.save_meta(a.out)
