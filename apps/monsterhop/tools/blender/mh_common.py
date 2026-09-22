"""Monster Hop - what every Blender script of the game shares.

The game camera, the lighting of each zone, the material registry that swaps
an object between its render passes, the passes themselves and the output
files. Every asset script imports this module; none of them sets up a camera
or a light of its own. Read SPEC.md next to it first.

    /Applications/Blender.app/Contents/MacOS/Blender -b -P <script>.py -- --out <dir>

Blender 3.3.1, Cycles on Metal.

The projection is ORTHOGRAPHIC and chosen so that one cell and one floor are
whole pixels: +1 m along X moves a point (60, 14) px on the screen, +1 m along
Y (forward, away from the camera) moves it (20, -42) px, one floor up moves it
(0, -23) px. Everything the watch draws is placed with those three vectors,
so sprites rendered one at a time line up exactly when the watch puts them
side by side.
"""

import bpy
import json
import math
import os
import struct
import sys
import zlib

import numpy as np
from mathutils import Matrix, Vector as V

# ---------------------------------------------------------------------------
# The projection (the engine hard-codes these numbers: never change them)
# ---------------------------------------------------------------------------

PX_X = (60, 14)        # +1 m along X on the screen (x right, y down)
PX_Y = (20, -42)       # +1 m along Y
FLOOR_PX = 23          # one floor up = (0, -23) px

_U, _V, SIN_E = 60.0, 20.0, 0.7
S = math.hypot(_U, _V)                       # 63.2456 px per metre
PSI = math.atan2(_V, _U)                     # camera yaw, 18.43 deg
ELEV = math.asin(SIN_E)                      # camera elevation, 44.43 deg
COS_E = math.cos(ELEV)
FLOOR_M = FLOOR_PX / (S * COS_E)             # 0.50923 m: the height of one floor

F = V((-math.sin(PSI) * COS_E, math.cos(PSI) * COS_E, -SIN_E))   # view direction
R = V((math.cos(PSI), math.sin(PSI), 0.0))                         # screen right
UP = R.cross(F)                                                    # screen up
if UP.z < 0:
    UP = -UP

DEPTH_UNIT = 1.0 / 32.0    # metres per step in the _z pass
DEPTH_EMPTY = 255          # _z value where nothing was rendered


def to_screen(p, anchor=V((0, 0, 0))):
    """Offset in pixels (x right, y down) of world point p from the anchor."""
    d = V(p) - V(anchor)
    return (S * d.dot(R), -S * d.dot(UP))


def depth(p):
    """Depth of a world point along the view direction, in metres."""
    return V(p).dot(F)


def _check_projection():
    for vec, want in (((1, 0, 0), PX_X), ((0, 1, 0), PX_Y), ((0, 0, FLOOR_M), (0, -FLOOR_PX))):
        got = to_screen(V(vec))
        assert abs(got[0] - want[0]) < 1e-3 and abs(got[1] - want[1]) < 1e-3, (vec, got, want)


_check_projection()


def cell(x, y, floor=0):
    """World point at the centre of cell (x, y), on the top of floor `floor`."""
    return V((x + 0.5, y + 0.5, floor * FLOOR_M))


# ---------------------------------------------------------------------------
# Scene and lights
# ---------------------------------------------------------------------------

SUN_ELEV = 55.0     # degrees above the horizon
SUN_AZ = 210.0      # degrees from +X: the sun comes from the front-left

# Zone lighting. Tiles and props are rendered in final colour under their
# zone's light; characters under 'neutral' (the watch tints them per zone).
# sun: colour, strength; sky: colour, strength; mist: extra flat ambient.
LIGHTS = {
    'neutral': dict(sun=((1.00, 0.97, 0.92), 1.75), sky=((0.80, 0.85, 1.00), 0.28)),
    'city':    dict(sun=((0.92, 0.94, 1.00), 2.3), sky=((0.62, 0.66, 0.78), 0.55)),
    'castle':  dict(sun=((0.78, 0.70, 1.00), 1.7), sky=((0.42, 0.30, 0.62), 0.60)),
    'desert':  dict(sun=((1.00, 0.84, 0.62), 3.2), sky=((0.95, 0.72, 0.52), 0.45)),
    'forest':  dict(sun=((0.82, 0.96, 0.86), 2.2), sky=((0.40, 0.62, 0.50), 0.55)),
    'map':     dict(sun=((1.00, 0.95, 0.88), 3.0), sky=((0.70, 0.75, 0.95), 0.50)),
}


def sun_dir():
    """Unit vector pointing TOWARDS the sun."""
    el, az = math.radians(SUN_ELEV), math.radians(SUN_AZ)
    return V((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))


_STATE = {}


def reset(light='neutral', cpu=False):
    """Empty scene with the game camera and the given zone light."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    ok = False
    if not cpu:
        try:
            pr = bpy.context.preferences.addons['cycles'].preferences
            pr.compute_device_type = 'METAL'
            pr.get_devices()
            for d in pr.devices:
                d.use = (d.type == 'METAL')
                ok = ok or d.use
        except Exception as e:  # noqa
            print('GPU setup failed:', e)
    sc.cycles.device = 'GPU' if ok else 'CPU'
    r = sc.render
    r.film_transparent = True
    r.dither_intensity = 0.0
    r.resolution_percentage = 100
    r.image_settings.file_format = 'OPEN_EXR'
    r.image_settings.color_mode = 'RGBA'
    r.image_settings.color_depth = '32'
    r.image_settings.exr_codec = 'ZIP'
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    sc.cycles.use_adaptive_sampling = False
    sc.cycles.seed = 7

    w = bpy.data.worlds.new('sky')
    sc.world = w
    w.use_nodes = True
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sc.collection.objects.link(sun)
    sun.rotation_euler = (-sun_dir()).to_track_quat('-Z', 'Y').to_euler()
    sun.data.angle = math.radians(3.0)

    cam = bpy.data.objects.new('mh_cam', bpy.data.cameras.new('mh_cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = 'ORTHO'
    cam.data.sensor_fit = 'VERTICAL'
    cam.data.clip_start = 0.1
    cam.data.clip_end = 400.0

    # ground for shadow catching (hidden unless a shadow pass asks for it)
    me = bpy.data.meshes.new('mh_ground')
    s = 20.0
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new('mh_ground', me)
    sc.collection.objects.link(g)
    gm = bpy.data.materials.new('mh_ground')
    gm.use_nodes = True
    gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.8, 0.8, 0.8, 1)
    me.materials.append(gm)
    g.hide_render = True

    _STATE.clear()
    _STATE.update(scene=sc, cam=cam, sun=sun, world=w, ground=g, meta={}, light=light)
    set_light(light)
    _MATS.clear()
    return sc


def set_light(name):
    L = LIGHTS[name]
    sun = _STATE['sun']
    sun.data.color = L['sun'][0]
    sun.data.energy = L['sun'][1]
    bg = _STATE['world'].node_tree.nodes['Background']
    bg.inputs['Color'].default_value = tuple(L['sky'][0]) + (1,)
    bg.inputs['Strength'].default_value = L['sky'][1]
    _STATE['light'] = name


def add_point_light(pos, color=(1.0, 0.7, 0.35), power=30.0, radius=0.05, name='lamp'):
    """A small warm light (torches, candles, street lamps). Stays in the scene
    until removed; hidden objects' lights still shine unless you delete them."""
    lt = bpy.data.lights.new(name, 'POINT')
    lt.color = color
    lt.energy = power
    lt.shadow_soft_size = radius
    ob = bpy.data.objects.new(name, lt)
    ob.location = pos
    _STATE['scene'].collection.objects.link(ob)
    return ob


# ---------------------------------------------------------------------------
# Materials: one key, four passes
# ---------------------------------------------------------------------------
#
# mat(key, ...) registers a material. assign(obj, key) gives it to an object
# (appending a slot). Before each pass, every object's slots are swapped to
# the variant of that pass:
#   color  final colour, lit (tiles, props, things that are not recoloured)
#   light  the same shading on neutral grey 0.8 albedo (characters: the
#          watch colours them per region id)
#   id     flat emission id/16 (region ids, see SPEC.md)
#   z      flat emission of the depth along the view direction
#
# `build(nt, neutral)` may replace the default Principled BSDF: it gets the
# node tree (already cleared, with an output node named 'out') and must return
# the shader socket to plug into the surface; when `neutral` is True it must
# use grey 0.8 instead of its colours (keep bump, roughness, gloss).

_MATS = {}


class _Mat:
    def __init__(self, key, base, rough, metal, emit, emit_strength, id_, build, spec):
        self.key, self.base, self.rough, self.metal = key, base, rough, metal
        self.emit, self.emit_strength, self.id, self.build, self.spec = emit, emit_strength, id_, build, spec
        self.variants = {}


def mat(key, base=(0.8, 0.8, 0.8), rough=0.6, metal=0.0, emit=None, emit_strength=0.0,
        id=0, build=None, spec=0.5):
    _MATS[key] = _Mat(key, base, rough, metal, emit, emit_strength, id, build, spec)
    return key


def _new_tree(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.name = 'out'
    return m, nt, out


def _variant(m, kind):
    if kind in m.variants:
        return m.variants[kind]
    mm, nt, out = _new_tree('%s.%s' % (m.key, kind))
    if kind in ('color', 'light'):
        neutral = (kind == 'light')
        if m.build is not None:
            sock = m.build(nt, neutral)
        else:
            b = nt.nodes.new('ShaderNodeBsdfPrincipled')
            col = (0.8, 0.8, 0.8) if neutral else m.base
            b.inputs['Base Color'].default_value = tuple(col) + (1,)
            b.inputs['Roughness'].default_value = m.rough
            b.inputs['Metallic'].default_value = m.metal
            b.inputs['Specular'].default_value = m.spec
            if m.emit is not None and m.emit_strength > 0:
                b.inputs['Emission'].default_value = tuple(m.emit) + (1,)
                b.inputs['Emission Strength'].default_value = m.emit_strength
            sock = b.outputs['BSDF']
        nt.links.new(sock, out.inputs['Surface'])
    elif kind == 'id':
        e = nt.nodes.new('ShaderNodeEmission')
        e.inputs['Color'].default_value = (m.id / 16.0, 0.0, 0.0, 1.0)
        e.inputs['Strength'].default_value = 1.0
        nt.links.new(e.outputs['Emission'], out.inputs['Surface'])
    elif kind == 'z':
        return _depth_material()
    m.variants[kind] = mm
    return mm


def _depth_material():
    if 'zmat' in _STATE:
        return _STATE['zmat']
    mm, nt, out = _new_tree('mh_depth')
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    dot = nt.nodes.new('ShaderNodeVectorMath')
    dot.operation = 'DOT_PRODUCT'
    dot.inputs[1].default_value = tuple(F)
    nt.links.new(geo.outputs['Position'], dot.inputs[0])
    d0 = nt.nodes.new('ShaderNodeValue')
    d0.name = 'd0'
    sub = nt.nodes.new('ShaderNodeMath')
    sub.operation = 'SUBTRACT'
    nt.links.new(dot.outputs['Value'], sub.inputs[0])
    nt.links.new(d0.outputs['Value'], sub.inputs[1])
    # encode: 0.5 + d / 16 (d in metres, +-8 m)
    mul = nt.nodes.new('ShaderNodeMath')
    mul.operation = 'MULTIPLY_ADD'
    nt.links.new(sub.outputs['Value'], mul.inputs[0])
    mul.inputs[1].default_value = 1.0 / 16.0
    mul.inputs[2].default_value = 0.5
    comb = nt.nodes.new('ShaderNodeCombineRGB')
    nt.links.new(mul.outputs['Value'], comb.inputs['R'])
    e = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(comb.outputs['Image'], e.inputs['Color'])
    e.inputs['Strength'].default_value = 1.0
    nt.links.new(e.outputs['Emission'], out.inputs['Surface'])
    _STATE['zmat'] = mm
    return mm


def assign(obj, key):
    """Give obj the registered material `key` (appends a slot)."""
    if key not in _MATS:
        raise KeyError('material %r is not registered (call mat() first)' % key)
    keys = list(obj.get('mh_keys', []))
    keys.append(key)
    obj['mh_keys'] = keys
    obj.data.materials.append(_variant(_MATS[key], 'color'))
    return obj


def _use_pass(objs, kind):
    for ob in objs:
        keys = list(ob.get('mh_keys', []))
        for i, k in enumerate(keys):
            ob.data.materials[i] = _variant(_MATS[k], kind)


# ---------------------------------------------------------------------------
# Camera placement and framing
# ---------------------------------------------------------------------------

def place_camera(anchor, w, h, ax, ay, zoom=1.0):
    """Frame a w x h image so that world point `anchor` lands on pixel
    coordinate (ax, ay) (integers: the corner between pixels). zoom > 1 only
    for pictures that are not composited with the game (the shop's big Tommy)."""
    sc, cam = _STATE['scene'], _STATE['cam']
    S = globals()['S'] * zoom
    sc.render.resolution_x, sc.render.resolution_y = int(w), int(h)
    if w > h:
        cam.data.sensor_fit = 'HORIZONTAL'
        cam.data.ortho_scale = w / S
    else:
        cam.data.sensor_fit = 'VERTICAL'
        cam.data.ortho_scale = h / S
    A = V(anchor)
    c0 = A + R * ((w / 2.0 - ax) / S) + UP * ((ay - h / 2.0) / S)
    M = Matrix((R, UP, -F)).transposed().to_4x4()
    cam.matrix_world = Matrix.Translation(c0 - F * 60.0) @ M


def _world_points(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for ob in objs:
        if ob.type != 'MESH':
            continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        mw = ev.matrix_world
        pts.extend(mw @ v.co for v in me.vertices)
        ev.to_mesh_clear()
    return pts


def fit(objs, anchor, margin=3, shadow_z=None, zoom=1.0):
    """Image size and anchor pixel that hold objs (and their shadow on the
    plane z = shadow_z, when given)."""
    A = V(anchor)
    pts = _world_points(objs)
    if shadow_z is not None:
        L = sun_dir()
        pts = pts + [p - L * ((p.z - shadow_z) / L.z) for p in pts if p.z > shadow_z]
    xs, ys = [], []
    for p in pts:
        sx, sy = to_screen(p, A)
        xs.append(sx * zoom)
        ys.append(sy * zoom)
    x0, x1 = math.floor(min(xs)) - margin, math.ceil(max(xs)) + margin
    y0, y1 = math.floor(min(ys)) - margin, math.ceil(max(ys)) + margin
    return (x1 - x0, y1 - y0, -x0, -y0)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def _render_exr(path, samples, filt, denoise, bounces):
    sc = _STATE['scene']
    sc.cycles.samples = samples
    sc.cycles.use_denoising = denoise
    if denoise:
        sc.cycles.denoiser = 'OPENIMAGEDENOISE'
    # single-sample passes: Cycles does not honour a tiny box filter, one
    # camera ray per pixel is the only way to get exact regions
    sc.cycles.pixel_filter_type = 'BLACKMAN_HARRIS' if filt > 0.5 else 'GAUSSIAN'
    sc.cycles.filter_width = filt
    sc.cycles.max_bounces = bounces
    sc.cycles.diffuse_bounces = min(bounces, 3)
    sc.cycles.glossy_bounces = min(bounces, 2)
    sc.cycles.transmission_bounces = min(bounces, 2)
    sc.cycles.transparent_max_bounces = 4
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1].copy()


def _srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def _to_rgba(a):
    al = a[..., 3]
    rgb = a[..., :3] / np.maximum(al[..., None], 1e-6)
    out = np.zeros(a.shape, np.uint8)
    out[..., :3] = np.round(_srgb(rgb) * 255)
    out[..., 3] = np.round(np.clip(al, 0, 1) * 255)
    out[out[..., 3] == 0] = 0
    return out


def _dilate_fill(vals, have, need, empty):
    """Give every `need` pixel without a value the value of a neighbour."""
    vals = vals.copy()
    have = have.copy()
    for _ in range(16):
        miss = need & ~have
        if not miss.any():
            break
        best = np.full(vals.shape, empty, vals.dtype)
        got = np.zeros(vals.shape, bool)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sv = np.roll(np.roll(vals, dy, 0), dx, 1)
            sh = np.roll(np.roll(have, dy, 0), dx, 1)
            take = ~got & sh
            best = np.where(take, sv, best)
            got |= sh
        fill = miss & got
        vals = np.where(fill, best, vals)
        have |= fill
    return vals


def write_png(path, arr):
    arr = np.ascontiguousarray(arr.astype(np.uint8))
    h, w = arr.shape[:2]
    ch = 1 if arr.ndim == 2 else arr.shape[2]
    ctype = {1: 0, 2: 4, 3: 2, 4: 6}[ch]
    raw = b''.join(b'\x00' + arr[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def _set_vis(visible, holdout):
    """Show `visible` normally, `holdout` as hold-out, hide every other mesh."""
    vis = set(id(o) for o in visible)
    hold = set(id(o) for o in holdout)
    for ob in _STATE['scene'].objects:
        if ob.type not in ('MESH', 'CURVE', 'SURFACE', 'META', 'FONT'):
            continue
        if ob.name == 'mh_ground':
            continue
        if id(ob) in vis:
            ob.hide_render = False
            ob.is_holdout = False
            ob.visible_camera = True
        elif id(ob) in hold:
            ob.hide_render = False
            ob.is_holdout = True
            ob.visible_camera = True
        else:
            ob.hide_render = True
            ob.is_holdout = False


def render_sprite(out, name, objs, anchor, passes=('color', 'z'), holdout=(), size=None,
                  samples=48, shadow_z=None, shadow_samples=64, bounce_ground=None, margin=3,
                  kind='sprite', extra=None, zoom=1.0):
    """Render objs as one sprite and write its PNGs into `out`.

    passes: any of 'color' (final colour RGBA), 'light' (neutral shading RGBA),
            'id' (L, id * 16), 'z' (L, depth), 'shadow' (L, darkness cast on the
            plane z = shadow_z, nothing of the object itself), 'glow' (RGB,
            the light its lamps/emitters throw on a white ground at shadow_z;
            black where none: the watch adds it). A 'glow' needs a size that
            covers the light pool: pass size=, fit() only sees the object.
    anchor: the world point the watch places the sprite by (usually cell(...)).
    holdout: objects that hide what is behind them without being drawn (a cap
            rendered with the head as hold-out).
    size:   (w, h, ax, ay) or None to fit the objects (and their shadow).
    bounce_ground: z of an invisible ground plane that bounces light and takes
            no part in the image (characters look grounded), or None.
    Files: <name>.png (colour or light), <name>_id.png, <name>_z.png,
    <name>_sh.png, <name>_gl.png. The sizes and the anchor pixel go to meta.json (save_meta).
    """
    out = os.path.abspath(out)      # Blender mangles relative render paths
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, '_tmp')
    os.makedirs(tmp, exist_ok=True)
    A = V(anchor)
    objs = list(objs)
    if size is None:
        size = fit(objs, A, margin, shadow_z if 'shadow' in passes else None, zoom)
    w, h, ax, ay = size
    place_camera(A, w, h, ax, ay, zoom)
    g = _STATE['ground']
    zmat = _depth_material()
    zmat.node_tree.nodes['d0'].outputs[0].default_value = depth(A)
    cover = None
    files = {}

    def ground(mode, z=0.0):
        if mode is None:
            g.hide_render = True
            return
        g.location = (A.x, A.y, z)
        g.hide_render = False
        g.is_holdout = False
        g.is_shadow_catcher = (mode == 'catch')
        g.visible_camera = (mode == 'catch')

    shade_kind = 'color' if 'color' in passes else ('light' if 'light' in passes else None)
    if shade_kind:
        _set_vis(objs, holdout)
        _use_pass(objs + list(holdout), shade_kind)
        ground('bounce' if bounce_ground is not None else None, bounce_ground or 0.0)
        a = _render_exr(os.path.join(tmp, 's.exr'), samples, 1.5, True, 4)
        rgba = _to_rgba(a)
        cover = rgba[..., 3] > 0
        write_png(os.path.join(out, name + '.png'), rgba)
        files['img'] = name + '.png'
    ground(None)
    if 'id' in passes:
        _set_vis(objs, holdout)
        _use_pass(objs + list(holdout), 'id')
        a = _render_exr(os.path.join(tmp, 'i.exr'), 1, 0.01, False, 0)
        al = a[..., 3]
        v = a[..., 0] / np.maximum(al, 1e-6)
        ids = np.where(al > 0.5, np.clip(np.round(v * 16), 0, 15), 0).astype(np.int32)
        if cover is not None:
            ids = _dilate_fill(ids, ids > 0, cover, 0)
            ids = np.where(cover, ids, 0)
        write_png(os.path.join(out, name + '_id.png'), (ids * 16).astype(np.uint8))
        files['id'] = name + '_id.png'
    if 'z' in passes:
        _set_vis(objs, holdout)
        _use_pass(objs + list(holdout), 'z')
        a = _render_exr(os.path.join(tmp, 'z.exr'), 1, 0.01, False, 0)
        al = a[..., 3]
        d = (a[..., 0] / np.maximum(al, 1e-6) - 0.5) * 16.0
        q = np.clip(np.round(d / DEPTH_UNIT) + 128, 0, 254).astype(np.int32)
        have = al > 0.5
        q = np.where(have, q, DEPTH_EMPTY)
        if cover is not None:
            q = _dilate_fill(q, have, cover, DEPTH_EMPTY)
            q = np.where(cover, q, DEPTH_EMPTY)
        write_png(os.path.join(out, name + '_z.png'), q.astype(np.uint8))
        files['z'] = name + '_z.png'
    if 'shadow' in passes:
        _set_vis([], holdout)
        for ob in objs:           # cast, but not seen
            ob.hide_render = False
            ob.is_holdout = False
            ob.visible_camera = False
        _use_pass(objs, 'color' if shade_kind != 'light' else 'light')
        ground('catch', shadow_z if shadow_z is not None else A.z)
        a = _render_exr(os.path.join(tmp, 'h.exr'), shadow_samples, 1.5, True, 3)
        sh = np.round(np.clip(a[..., 3], 0, 1) * 255).astype(np.uint8)
        write_png(os.path.join(out, name + '_sh.png'), sh)
        files['sh'] = name + '_sh.png'
        g.is_shadow_catcher = False
        ground(None)
        for ob in objs:
            ob.visible_camera = True
    if 'glow' in passes:
        # the light the object's lamps and emitters throw on the ground around
        # it, alone: sun and sky off, the object unseen, a white ground
        sc = _STATE['scene']
        sun = _STATE['sun']
        bg = _STATE['world'].node_tree.nodes['Background']
        keep = (sun.data.energy, bg.inputs['Strength'].default_value)
        sun.data.energy = 0.0
        bg.inputs['Strength'].default_value = 0.0
        _set_vis([], holdout)
        for ob in objs:
            ob.hide_render = False
            ob.is_holdout = False
            ob.visible_camera = False
        _use_pass(objs, 'color')
        gmat = g.data.materials[0]
        gmat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (1, 1, 1, 1)
        ground('bounce', shadow_z if shadow_z is not None else A.z)
        g.visible_camera = True
        a = _render_exr(os.path.join(tmp, 'g.exr'), max(64, samples), 1.5, True, 3)
        gl = np.round(_srgb(np.clip(a[..., :3], 0, 1)) * 255).astype(np.uint8)
        write_png(os.path.join(out, name + '_gl.png'), gl)
        files['gl'] = name + '_gl.png'
        gmat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.8, 0.8, 0.8, 1)
        ground(None)
        sun.data.energy, bg.inputs['Strength'].default_value = keep
        for ob in objs:
            ob.visible_camera = True
    info = dict(kind=kind, w=int(w), h=int(h), ax=int(ax), ay=int(ay),
                anchor=[round(A.x, 4), round(A.y, 4), round(A.z, 4)], files=files)
    if 'shadow' in passes:
        info['shadow_z'] = round(shadow_z if shadow_z is not None else A.z, 4)
    if zoom != 1.0:
        info['zoom'] = zoom
    if extra:
        info.update(extra)
    _STATE['meta'][name] = info
    return info


def save_meta(out, merge=True):
    """Write (or merge into) <out>/meta.json."""
    out = os.path.abspath(out)
    path = os.path.join(out, 'meta.json')
    meta = {}
    if merge and os.path.exists(path):
        with open(path) as f:
            meta = json.load(f)
    meta.update(_STATE['meta'])
    info = dict(projection=dict(px_x=PX_X, px_y=PX_Y, floor_px=FLOOR_PX, floor_m=FLOOR_M,
                                px_per_m=S, depth_unit_m=DEPTH_UNIT, depth_empty=DEPTH_EMPTY,
                                view_dir=list(F)))
    meta['_projection'] = info['projection']
    with open(path, 'w') as f:
        json.dump(meta, f, indent=1, sort_keys=True)
    tmp = os.path.join(out, '_tmp')
    if os.path.isdir(tmp):
        for fn in os.listdir(tmp):
            os.remove(os.path.join(tmp, fn))
        os.rmdir(tmp)


# ---------------------------------------------------------------------------
# Small geometry helpers (optional)
# ---------------------------------------------------------------------------

def args(defaults=None):
    """Parse `-- --out DIR [--only a,b] [--samples N] [--cpu]` after Blender's own."""
    import argparse
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    ap = argparse.ArgumentParser(allow_abbrev=False)
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='')
    ap.add_argument('--samples', type=int, default=(defaults or {}).get('samples', 48))
    ap.add_argument('--cpu', action='store_true')
    a, _ = ap.parse_known_args(argv)
    a.only = [s for s in a.only.split(',') if s]
    return a


def link(ob):
    _STATE['scene'].collection.objects.link(ob)
    return ob


def mesh_object(name, verts, faces, key=None, smooth=False):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.validate()
    ob = bpy.data.objects.new(name, me)
    link(ob)
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    if key:
        assign(ob, key)
    return ob


def box(name, x0, y0, z0, x1, y1, z1, key=None, bevel=0.0, segments=2):
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = mesh_object(name, v, f, key)
    if bevel > 0:
        m = ob.modifiers.new('bevel', 'BEVEL')
        m.width = bevel
        m.segments = segments
        m.limit_method = 'ANGLE'
    return ob


def remove(objs):
    for ob in objs:
        bpy.data.objects.remove(ob, do_unlink=True)
