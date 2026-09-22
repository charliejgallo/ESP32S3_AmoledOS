"""Procedural course props for the golf game, rendered to sprites in Blender.

    Blender -b -P props.py -- --out ../../assets/props [--only tree_oak,flag]
                              [--ss 4] [--samples 256]

Everything is built from scratch (no .blend inputs):

  tree_pine    conical conifer: whorls of drooping branch tufts
  tree_oak     broad round deciduous crown on a flared trunk with limbs
  tree_poplar  tall narrow columnar (Lombardy) poplar
  tree_palm    curved ringed trunk, arching feathery fronds, coconuts
  bush         low round shrub
  flag         striped pin with a waving yellow flag, 4 frames

Foliage technique: the crown is a set of noise-displaced ellipsoid "clumps".
Leaf cards (small folded quads) are scattered on the clump shells and
oriented along the clump normal with jitter. Their SHADING normals are a
blend of the card, clump and whole-crown normals (the usual game-foliage
normal trick), so the crown reads as lit volumes at 10-128 px while the real
card geometry breaks up the silhouette and self-shadows in Cycles. Dark
filler ellipsoids inside each clump stop see-through holes.

Every image is rendered at --ss times the final size and box-filtered down
in linear premultiplied space, then written as a straight-alpha PNG whose
fully transparent pixels carry the colour of the nearest opaque pixel (so a
bilinear scaler on the watch never pulls in black).
"""

import bpy
import bmesh
import math
import os
import sys
import json
import zlib
import struct
import argparse
import tempfile

import numpy as np
from mathutils import Vector, Matrix

ALL_PROPS = ["tree_pine", "tree_oak", "tree_poplar", "tree_palm", "bush", "flag"]

# ---------------------------------------------------------------------------
# Arguments

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                              "..", "..", "assets", "props"))
ap.add_argument("--only", default="")
ap.add_argument("--ss", type=int, default=4, help="supersampling factor")
ap.add_argument("--samples", type=int, default=192)
ap.add_argument("--cpu", action="store_true")
ARGS = ap.parse_args(argv)
OUT = os.path.abspath(ARGS.out)
os.makedirs(OUT, exist_ok=True)
TMP = tempfile.mkdtemp(prefix="props_")

# ---------------------------------------------------------------------------
# Lighting conventions (see SPEC.md)

# Side view: sun from behind-left of the camera and above (camera looks +Y).
SIDE_SUN_FROM = Vector((-0.6, -0.8, 0.0)).normalized()
SIDE_SUN_ELEV = math.radians(50)
# Top view: sun from the image's upper-left = north-west (image up = +Y).
TOP_SUN_FROM = Vector((-1.0, 1.0, 0.0)).normalized()
TOP_SUN_ELEV = math.radians(50)

SIDE_PITCH = math.radians(11)      # camera pitched down like the swing camera
SUN_COLOR = (1.0, 0.94, 0.84)      # slightly warm
SKY_COLOR = (0.55, 0.70, 1.0)      # soft blue fill
SUN_STRENGTH = 5.0
SKY_STRENGTH = 0.6
GROUND_BOUNCE = "#4FA23A"          # invisible fairway under the prop, bounce only


def srgb_to_lin(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def hexcol(h, a=1.0):
    h = h.lstrip("#")
    rgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(float(v) for v in srgb_to_lin(rgb)) + (a,)


def sun_dir(frm, elev):
    """Direction the light travels (from the sun towards the ground)."""
    v = Vector((frm.x * math.cos(elev), frm.y * math.cos(elev), math.sin(elev)))
    return -v.normalized()


# ---------------------------------------------------------------------------
# Scene / render setup

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    cyc = sc.cycles
    dev = "CPU"
    if not ARGS.cpu:
        try:
            prefs = bpy.context.preferences.addons["cycles"].preferences
            prefs.compute_device_type = "METAL"
            prefs.get_devices()
            ok = False
            for d in prefs.devices:
                d.use = d.type == "METAL"
                ok = ok or d.use
            if ok:
                dev = "GPU"
        except Exception as e:  # pragma: no cover
            print("GPU setup failed, using CPU:", e)
    cyc.device = dev
    cyc.samples = ARGS.samples
    cyc.use_adaptive_sampling = True
    cyc.adaptive_threshold = 0.005
    cyc.use_denoising = False
    cyc.max_bounces = 6
    cyc.diffuse_bounces = 3
    cyc.glossy_bounces = 2
    cyc.transmission_bounces = 4
    cyc.transparent_max_bounces = 8
    cyc.filter_width = 1.5
    cyc.sample_clamp_indirect = 3.0
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.color_depth = "8"
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    sc.display_settings.display_device = "sRGB"
    sc.sequencer_colorspace_settings.name = "sRGB"
    print("Cycles device:", dev)
    return sc


def set_world(strength):
    sc = bpy.context.scene
    w = sc.world or bpy.data.worlds.new("World")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld")
    # A vertical sky gradient: brighter blue overhead, paler at the horizon.
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.45
    ramp.color_ramp.elements[0].color = (0.75, 0.82, 0.92, 1)
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = SKY_COLOR + (1,)
    mapr = nt.nodes.new("ShaderNodeMapRange")
    mapr.inputs[1].default_value = -1.0
    mapr.inputs[2].default_value = 1.0
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs[1].default_value = strength
    nt.links.new(tc.outputs["Generated"], sep.inputs[0])
    # Generated coords of the world are the view direction.
    nt.links.new(sep.outputs["Z"], mapr.inputs[0])
    nt.links.new(mapr.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bg.inputs[0])
    nt.links.new(bg.outputs[0], out.inputs[0])


def add_sun(direction, strength=3.2, angle_deg=3.0, color=SUN_COLOR):
    ld = bpy.data.lights.new("Sun", "SUN")
    ld.energy = strength
    ld.angle = math.radians(angle_deg)
    ld.color = color
    ob = bpy.data.objects.new("Sun", ld)
    bpy.context.scene.collection.objects.link(ob)
    ob.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
    return ob


def link(ob):
    bpy.context.scene.collection.objects.link(ob)
    return ob


# ---------------------------------------------------------------------------
# Mesh helpers

def mesh_from_arrays(name, verts, faces, attrs=None, normals=None, mat=None, smooth=True):
    """verts (N,3); faces (F,k) int array of equal-size polygons.
    attrs: {name: (N,) float per point}; normals: (N,3) custom shading normals."""
    verts = np.ascontiguousarray(verts, dtype=np.float32)
    faces = np.ascontiguousarray(faces, dtype=np.int32)
    me = bpy.data.meshes.new(name)
    nv, nf, k = len(verts), len(faces), faces.shape[1]
    me.vertices.add(nv)
    me.vertices.foreach_set("co", verts.ravel())
    me.loops.add(nf * k)
    me.loops.foreach_set("vertex_index", faces.ravel())
    me.polygons.add(nf)
    me.polygons.foreach_set("loop_start", np.arange(nf, dtype=np.int32) * k)
    me.polygons.foreach_set("loop_total", np.full(nf, k, dtype=np.int32))
    me.polygons.foreach_set("use_smooth", np.full(nf, smooth, dtype=bool))
    me.update(calc_edges=True)
    me.validate()
    for an, av in (attrs or {}).items():
        a = me.attributes.new(an, "FLOAT", "POINT")
        a.data.foreach_set("value", np.ascontiguousarray(av, dtype=np.float32))
    if normals is not None:
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.pi
        n = np.asarray(normals, dtype=np.float64)
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        me.normals_split_custom_set_from_vertices([tuple(v) for v in n])
    if mat is not None:
        me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    return link(ob)


def tube(name, pts, radii, mat, bevel_res=4, twist_noise=0.0):
    """A tapered tube along a polyline, as a curve with bevel."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 1.0
    cu.bevel_resolution = bevel_res
    cu.use_fill_caps = True
    cu.resolution_u = 4
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, (q, r) in zip(sp.points, zip(pts, radii)):
        p.co = (q[0], q[1], q[2], 1.0)
        p.radius = r
    cu.materials.append(mat)
    ob = bpy.data.objects.new(name, cu)
    return link(ob)


def smooth_path(ctrl, n):
    """Catmull-Rom through control points, n samples."""
    ctrl = np.asarray(ctrl, dtype=np.float64)
    P = np.vstack([2 * ctrl[0] - ctrl[1], ctrl, 2 * ctrl[-1] - ctrl[-2]])
    out = []
    segs = len(ctrl) - 1
    for i in range(n):
        t = i / (n - 1) * segs
        s = min(int(t), segs - 1)
        u = t - s
        p0, p1, p2, p3 = P[s], P[s + 1], P[s + 2], P[s + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    return np.array(out)


def rand_unit(rng, n):
    v = rng.normal(size=(n, 3))
    return v / np.linalg.norm(v, axis=1, keepdims=True)


class Noise3:
    """Cheap smooth 3D pseudo-noise: a sum of random plane waves, ~[-1, 1]."""

    def __init__(self, rng, freq, octaves=10):
        self.w = rand_unit(rng, octaves) * freq * rng.uniform(0.7, 2.2, (octaves, 1))
        self.ph = rng.uniform(0, 2 * math.pi, octaves)
        self.a = 1.0 / np.linalg.norm(self.w, axis=1) * freq
        self.a /= np.sqrt((self.a ** 2).sum() / 2)

    def __call__(self, p):
        # clipped so the shell displacement has a known bound (see filler)
        return np.clip((np.sin(p @ self.w.T + self.ph) * self.a).sum(axis=1), -1.5, 1.5)


def rot_from_axis(axis, up=(0, 0, 1)):
    """3x3 rotation whose local X is `axis`."""
    x = np.asarray(axis, dtype=np.float64)
    x /= np.linalg.norm(x)
    u = np.asarray(up, dtype=np.float64)
    if abs(np.dot(u, x)) > 0.95:
        u = np.array([1.0, 0, 0]) if abs(x[0]) < 0.9 else np.array([0, 1.0, 0])
    y = np.cross(u, x)
    y /= np.linalg.norm(y)
    z = np.cross(x, y)
    return np.stack([x, y, z], axis=1)


class Clump:
    def __init__(self, c, r, R=None):
        self.c = np.asarray(c, dtype=np.float64)
        self.r = np.asarray(r, dtype=np.float64)
        self.R = np.eye(3) if R is None else np.asarray(R, dtype=np.float64)

    def inside(self, p):
        """Normalised ellipsoid radius of points p (1 = on the shell)."""
        q = (p - self.c) @ self.R
        return np.sqrt(((q / self.r) ** 2).sum(axis=1))

    def area(self):
        a, b, c = self.r
        k = 1.6
        return 4 * math.pi * (((a * b) ** k + (a * c) ** k + (b * c) ** k) / 3) ** (1 / k)


def canopy_cards(rng, clumps, density, leaf_len, leaf_aspect, gcenter, gradii,
                 disp_amp=0.16, disp_freq=1.3, depth=0.22, tilt=0.55, fold=0.18,
                 w_card=0.25, w_clump=0.45, w_global=0.30, hide=0.86, droop=0.0,
                 len_jitter=0.35):
    """Scatter leaf cards on the displaced shells of the clumps.
    Returns verts (4N,3), faces (N,4), attrs dict, shading normals (4N,3)."""
    noise = Noise3(rng, disp_freq)
    P, NC, CL = [], [], []
    for ci, cl in enumerate(clumps):
        n = max(8, int(cl.area() * density))
        d = rand_unit(rng, n)
        loc_on = d * cl.r
        wp = cl.c + loc_on @ cl.R.T
        disp = 1.0 + disp_amp * noise(wp * 1.0)
        dep = 1.0 - depth * rng.random(n) ** 1.7
        loc = loc_on * (disp * dep)[:, None]
        p = cl.c + loc @ cl.R.T
        nl = d / cl.r
        nl /= np.linalg.norm(nl, axis=1, keepdims=True)
        nw = nl @ cl.R.T
        keep = np.ones(n, dtype=bool)
        for cj, other in enumerate(clumps):
            if cj != ci:
                keep &= other.inside(p) > hide
        P.append(p[keep])
        NC.append(nw[keep])
        CL.append(np.full(keep.sum(), rng.random()))
    P = np.concatenate(P)
    NC = np.concatenate(NC)
    CL = np.concatenate(CL)
    N = len(P)
    # Whole-crown normal (ellipsoid around the crown).
    NG = (P - gcenter) / np.asarray(gradii) ** 2
    NG /= np.linalg.norm(NG, axis=1, keepdims=True)
    # Card orientation.
    cn = NC + tilt * rand_unit(rng, N)
    cn /= np.linalg.norm(cn, axis=1, keepdims=True)
    t = np.cross(cn, rand_unit(rng, N))
    if droop:
        t = t + np.array([0, 0, -droop])
        t -= cn * (t * cn).sum(axis=1, keepdims=True)
    t /= np.linalg.norm(t, axis=1, keepdims=True) + 1e-9
    b = np.cross(cn, t)
    L = leaf_len * (1 + len_jitter * (rng.random(N) * 2 - 1))
    W = L * leaf_aspect
    Lc, Wc = L[:, None], W[:, None]
    v0 = P - t * Lc * 0.5
    v1 = P - b * Wc * 0.5 + t * Lc * 0.05 + cn * Wc * fold
    v2 = P + t * Lc * 0.5
    v3 = P + b * Wc * 0.5 + t * Lc * 0.05 + cn * Wc * fold
    verts = np.stack([v0, v1, v2, v3], axis=1).reshape(-1, 3)
    faces = np.arange(4 * N, dtype=np.int32).reshape(N, 4)
    # deepest a card can sit, as a fraction of its clump radius
    global MIN_SHELL
    MIN_SHELL = (1 - 1.5 * disp_amp) * (1 - depth)
    sn = w_card * cn + w_clump * NC + w_global * NG
    sn /= np.linalg.norm(sn, axis=1, keepdims=True)
    zmin, zmax = P[:, 2].min(), P[:, 2].max()
    attrs = {
        "rnd": np.repeat(rng.random(N), 4),
        "clump": np.repeat(CL, 4),
        "h": np.repeat((P[:, 2] - zmin) / max(1e-6, zmax - zmin), 4),
    }
    print("  leaf cards:", N)
    return verts, faces, attrs, np.repeat(sn, 4, axis=0)


MIN_SHELL = 0.6


def filler(name, clumps, mat, scale=None, gcenter=None, gradii=None):
    """Dark inner ellipsoids so the crown has no see-through holes. By default
    they sit just inside the deepest leaf card, so they never poke out."""
    if scale is None:
        scale = MIN_SHELL * 0.94
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=3, radius=1.0)
    base = np.array([v.co[:] for v in bm.verts])
    tris = np.array([[v.index for v in f.verts] for f in bm.faces])
    bm.free()
    V, F, Nn = [], [], []
    off = 0
    for cl in clumps:
        loc = base * cl.r * scale
        V.append(cl.c + loc @ cl.R.T)
        nl = base / cl.r
        nl /= np.linalg.norm(nl, axis=1, keepdims=True)
        Nn.append(nl @ cl.R.T)
        F.append(tris + off)
        off += len(base)
    V = np.concatenate(V)
    Nn = np.concatenate(Nn)
    if gcenter is not None:
        ng = (V - gcenter) / np.asarray(gradii) ** 2
        ng /= np.linalg.norm(ng, axis=1, keepdims=True)
        Nn = Nn * 0.5 + ng * 0.5
    n = len(V)
    return mesh_from_arrays(name, V, np.concatenate(F),
                            attrs={"rnd": np.zeros(n), "clump": np.full(n, 0.5),
                                   "h": np.full(n, 0.3), "fill": np.ones(n)},
                            normals=Nn, mat=mat)


# ---------------------------------------------------------------------------
# Materials

def _nodes(mat):
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    return nt, nt.nodes, nt.links


def mat_foliage(name, dark, mid, light, top_tint, shadow_tint, translucency=0.25,
                ao_dist=0.9, ao_strength=0.6, rough=0.55, spec=0.3, clump_var=0.45):
    """Leaf material. Colour = ramp(rnd, clump) x height tint x AO tint."""
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    a_rnd = N.new("ShaderNodeAttribute"); a_rnd.attribute_name = "rnd"
    a_cl = N.new("ShaderNodeAttribute"); a_cl.attribute_name = "clump"
    a_h = N.new("ShaderNodeAttribute"); a_h.attribute_name = "h"
    a_fill = N.new("ShaderNodeAttribute"); a_fill.attribute_name = "fill"
    # key = mix of per-card and per-clump random
    mx = N.new("ShaderNodeMixRGB"); mx.blend_type = "MIX"
    mx.inputs[0].default_value = clump_var   # 0 = per-card colour, 1 = per-clump
    L.new(a_rnd.outputs["Fac"], mx.inputs[1])
    L.new(a_cl.outputs["Fac"], mx.inputs[2])
    ramp = N.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.interpolation = "B_SPLINE"
    cr.elements[0].position = 0.0
    cr.elements[0].color = hexcol(dark)
    cr.elements[1].position = 1.0
    cr.elements[1].color = hexcol(light)
    e = cr.elements.new(0.5)
    e.color = hexcol(mid)
    L.new(mx.outputs[0], ramp.inputs[0])
    # height tint: top leaves take top_tint
    hr = N.new("ShaderNodeMapRange")
    hr.inputs[1].default_value = 0.55
    hr.inputs[2].default_value = 1.0
    hr.inputs[3].default_value = 0.0
    hr.inputs[4].default_value = 0.35
    L.new(a_h.outputs["Fac"], hr.inputs[0])
    mh = N.new("ShaderNodeMixRGB"); mh.blend_type = "MIX"
    L.new(hr.outputs[0], mh.inputs[0])
    L.new(ramp.outputs[0], mh.inputs[1])
    mh.inputs[2].default_value = hexcol(top_tint)
    # filler is darker
    mf = N.new("ShaderNodeMixRGB"); mf.blend_type = "MULTIPLY"
    L.new(a_fill.outputs["Fac"], mf.inputs[0])
    L.new(mh.outputs[0], mf.inputs[1])
    mf.inputs[2].default_value = (0.55, 0.6, 0.55, 1)
    # AO tint in crevices
    ao = N.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = ao_dist
    ao.samples = 8
    L.new(mf.outputs[0], ao.inputs["Color"])
    aor = N.new("ShaderNodeMapRange")
    aor.inputs[1].default_value = 0.0
    aor.inputs[2].default_value = 1.0
    aor.inputs[3].default_value = 1.0 - ao_strength
    aor.inputs[4].default_value = 1.0
    L.new(ao.outputs["AO"], aor.inputs[0])
    # occlusion multiplier: shadow_tint where fully occluded, white in the open
    mao = N.new("ShaderNodeMixRGB"); mao.blend_type = "MIX"
    L.new(aor.outputs[0], mao.inputs[0])
    mao.inputs[1].default_value = hexcol(shadow_tint)
    mao.inputs[2].default_value = (1, 1, 1, 1)
    mm = N.new("ShaderNodeMixRGB"); mm.blend_type = "MULTIPLY"
    mm.inputs[0].default_value = 1.0
    L.new(mf.outputs[0], mm.inputs[1])
    L.new(mao.outputs[0], mm.inputs[2])
    bsdf = N.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Specular"].default_value = spec
    L.new(mm.outputs[0], bsdf.inputs["Base Color"])
    tr = N.new("ShaderNodeBsdfTranslucent")
    L.new(mm.outputs[0], tr.inputs["Color"])
    ms = N.new("ShaderNodeMixShader")
    ms.inputs[0].default_value = translucency
    L.new(bsdf.outputs[0], ms.inputs[1])
    L.new(tr.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    return mat


def mat_bark(name, dark, light, scale=(6, 6, 1.2), bump=0.4, rings=0.0, ring_col=None):
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = scale
    L.new(tc.outputs["Object"], mp.inputs[0])
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 2.5
    nz.inputs["Detail"].default_value = 6
    nz.inputs["Roughness"].default_value = 0.6
    L.new(mp.outputs[0], nz.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.3
    ramp.color_ramp.elements[0].color = hexcol(dark)
    ramp.color_ramp.elements[1].position = 0.7
    ramp.color_ramp.elements[1].color = hexcol(light)
    L.new(nz.outputs["Fac"], ramp.inputs[0])
    col = ramp.outputs[0]
    height = nz.outputs["Fac"]
    if rings > 0:
        wv = N.new("ShaderNodeTexWave")
        wv.wave_type = "BANDS"
        wv.bands_direction = "Z"
        wv.inputs["Scale"].default_value = rings
        wv.inputs["Distortion"].default_value = 1.5
        wv.inputs["Detail"].default_value = 2
        L.new(tc.outputs["Object"], wv.inputs[0])
        wr = N.new("ShaderNodeValToRGB")
        wr.color_ramp.elements[0].position = 0.25
        wr.color_ramp.elements[1].position = 0.75
        wr.color_ramp.elements[0].color = hexcol(ring_col or dark)
        wr.color_ramp.elements[1].color = (1, 1, 1, 1)
        L.new(wv.outputs["Fac"], wr.inputs[0])
        m2 = N.new("ShaderNodeMixRGB"); m2.blend_type = "MULTIPLY"
        m2.inputs[0].default_value = 1.0
        L.new(col, m2.inputs[1])
        L.new(wr.outputs[0], m2.inputs[2])
        col = m2.outputs[0]
        height = wv.outputs["Fac"]
    bsdf = N.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.85
    bsdf.inputs["Specular"].default_value = 0.2
    L.new(col, bsdf.inputs["Base Color"])
    bp = N.new("ShaderNodeBump")
    bp.inputs["Strength"].default_value = bump
    bp.inputs["Distance"].default_value = 0.05
    L.new(height, bp.inputs["Height"])
    L.new(bp.outputs[0], bsdf.inputs["Normal"])
    L.new(bsdf.outputs[0], out.inputs["Surface"])
    return mat


def mat_simple(name, color, rough=0.5, spec=0.4, translucency=0.0, metallic=0.0):
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    bsdf = N.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = hexcol(color) if isinstance(color, str) else color
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Specular"].default_value = spec
    bsdf.inputs["Metallic"].default_value = metallic
    if translucency > 0:
        tr = N.new("ShaderNodeBsdfTranslucent")
        tr.inputs["Color"].default_value = bsdf.inputs["Base Color"].default_value
        ms = N.new("ShaderNodeMixShader")
        ms.inputs[0].default_value = translucency
        L.new(bsdf.outputs[0], ms.inputs[1])
        L.new(tr.outputs[0], ms.inputs[2])
        L.new(ms.outputs[0], out.inputs["Surface"])
    else:
        L.new(bsdf.outputs[0], out.inputs["Surface"])
    return mat


def mat_vcol(name, attr, rough=0.6, spec=0.3, translucency=0.2):
    """Colour from a float-colour point attribute (used by palm fronds, flag)."""
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    at = N.new("ShaderNodeAttribute"); at.attribute_name = attr
    bsdf = N.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Specular"].default_value = spec
    L.new(at.outputs["Color"], bsdf.inputs["Base Color"])
    tr = N.new("ShaderNodeBsdfTranslucent")
    L.new(at.outputs["Color"], tr.inputs["Color"])
    ms = N.new("ShaderNodeMixShader")
    ms.inputs[0].default_value = translucency
    L.new(bsdf.outputs[0], ms.inputs[1])
    L.new(tr.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    return mat


def add_color_attr(ob, name, cols):
    """cols (N,4) linear floats per point."""
    me = ob.data
    a = me.attributes.new(name, "FLOAT_COLOR", "POINT")
    a.data.foreach_set("color", np.ascontiguousarray(cols, dtype=np.float32).ravel())


# ---------------------------------------------------------------------------
# Props. Each builder returns a dict: trunk (x, y) base, height, canopy diam.

def build_oak(rng):
    H, cz, crx, crz, cbase = 12.0, 7.4, 5.5, 4.0, 3.4
    bark = mat_bark("oak_bark", "#4a3a2c", "#9a8670", bump=0.6)
    leaf = mat_foliage("oak_leaf", dark="#2a5a1e", mid="#468a2a", light="#7bb43a",
                       top_tint="#9cc94a", shadow_tint="#163436", translucency=0.22,
                       clump_var=0.6, ao_strength=0.7)
    # Crown: many medium clumps on the shell of a broad dome, plus a core.
    clumps = []
    gc = np.array([0.0, 0.0, cz])
    gr = np.array([crx, crx, crz])
    n = 46
    for i in range(n):
        # Fibonacci sphere with jitter, skipping the flat underside
        zz = 1 - 2 * (i + 0.5) / n
        if zz < -0.62:
            continue
        az = i * 2.39996 + rng.normal(0, 0.25)
        rxy = math.sqrt(max(0, 1 - zz * zz))
        d = np.array([rxy * math.cos(az), rxy * math.sin(az), zz]) + rng.normal(0, 0.08, 3)
        d /= np.linalg.norm(d)
        c = gc + d * gr * rng.uniform(0.76, 0.86)
        r = rng.uniform(1.2, 2.4) if i % 3 else rng.uniform(2.0, 2.6)
        clumps.append(Clump(c, (r, r, r * 0.85)))
    clumps.append(Clump(gc, gr * 0.78))
    # Trunk with root flare, then limbs to the lower clumps.
    tp = smooth_path([[0, 0, 0], [0.08, 0.05, 1.2], [0.0, 0.1, 2.4], [0.1, 0.0, cbase + 0.8]], 14)
    tr = [0.72, 0.52, 0.47, 0.45, 0.44, 0.43, 0.42, 0.41, 0.4, 0.4, 0.39, 0.39, 0.38, 0.37]
    tube("oak_trunk", tp, tr, bark, bevel_res=6)
    top = tp[-1]
    low = sorted(range(len(clumps) - 1), key=lambda i: clumps[i].c[2])[:12]
    for j, i in enumerate(low[::2]):
        c = clumps[i].c
        mid = top + (c - top) * 0.45 + np.array([0, 0, 0.5]) + rng.normal(0, 0.25, 3)
        pts = smooth_path([top - [0, 0, 0.6], mid, c * 0.8 + top * 0.2], 10)
        tube("oak_limb%d" % j, pts, np.linspace(0.3, 0.1, len(pts)), bark)
    v, f, a, nn = canopy_cards(rng, clumps, density=60, leaf_len=0.42, leaf_aspect=0.62,
                               gcenter=gc, gradii=gr, disp_amp=0.08, disp_freq=1.2,
                               depth=0.15, hide=0.9, w_card=0.15, w_clump=0.6, w_global=0.25)
    mesh_from_arrays("oak_leaves", v, f, a, nn, leaf)
    filler("oak_fill", clumps, leaf, None, gc, gr)
    return dict(base=(0.0, 0.0), height=H, canopy=2 * crx)


def build_pine(rng):
    H, hb, Rmax = 14.0, 1.8, 3.1
    bark = mat_bark("pine_bark", "#3a2418", "#8a5a3c", scale=(5, 5, 0.8), bump=0.6)
    leaf = mat_foliage("pine_leaf", dark="#0f3322", mid="#215e36", light="#4c8f4a",
                       top_tint="#6ea650", shadow_tint="#0d2a36", translucency=0.12,
                       clump_var=0.3, ao_strength=0.65)
    clumps = []
    z = hb
    k = 0
    golden = math.pi * (3 - math.sqrt(5))
    while z < H - 0.6:
        t = (z - hb) / (H - hb)
        L = Rmax * (1 - t) ** 0.95 + 0.35
        nb = 7 if t < 0.6 else 5
        for i in range(nb):
            az = k * golden + i * 2 * math.pi / nb + rng.normal(0, 0.15)
            k += 1
            dirh = np.array([math.cos(az), math.sin(az), 0.0])
            ang = math.radians(rng.uniform(12, 24))   # branches droop
            axis = dirh * math.cos(ang) + np.array([0, 0, -math.sin(ang)])
            ll = L * rng.uniform(0.85, 1.08)
            c = np.array([0, 0, z]) + axis * ll * 0.52
            R = rot_from_axis(axis)
            clumps.append(Clump(c, (ll * 0.55, max(0.3, ll * 0.33), max(0.22, ll * 0.16)), R))
        z += 0.55 + 0.1 * rng.random()
    # spire
    for zz, rr in ((H - 0.9, 0.45), (H - 0.4, 0.28)):
        clumps.append(Clump((0, 0, zz), (rr, rr, rr * 1.8)))
    tp = [[0, 0, 0], [0, 0, H * 0.5], [0, 0, H - 0.3]]
    tube("pine_trunk", smooth_path(tp, 8), np.linspace(0.34, 0.06, 8), bark, bevel_res=5)
    tube("pine_root", [[0, 0, 0], [0, 0, 0.5]], [0.5, 0.33], bark, bevel_res=5)
    gc = np.array([0, 0, hb + (H - hb) * 0.35])
    gr = (Rmax, Rmax, (H - hb) * 0.6)
    v, f, a, n = canopy_cards(rng, clumps, density=150, leaf_len=0.26, leaf_aspect=0.4,
                              gcenter=gc, gradii=gr, disp_amp=0.2, disp_freq=1.8,
                              tilt=0.7, w_card=0.3, w_clump=0.45, w_global=0.25, droop=0.4)
    mesh_from_arrays("pine_leaves", v, f, a, n, leaf)
    filler("pine_fill", clumps, leaf, None, gc, gr)
    return dict(base=(0.0, 0.0), height=H, canopy=2 * Rmax + 0.7)


def build_poplar(rng):
    H, hb, Rmax = 16.0, 1.6, 1.85
    bark = mat_bark("poplar_bark", "#4a4238", "#8d8474", bump=0.4)
    leaf = mat_foliage("poplar_leaf", dark="#24521f", mid="#46882c", light="#7fb03c",
                       top_tint="#a2c64e", shadow_tint="#1d3b34", translucency=0.28,
                       clump_var=0.5, ao_strength=0.65)

    def prof(t):
        # Lombardy column: rounded base, widest at ~1/3, long taper to a point
        return Rmax * max(0.06, math.sin(math.pi * min(1.0, t) ** 0.7)) ** 0.8

    clumps = []
    n = 120
    for i in range(n):
        t = (i + rng.random()) / n
        z = hb + 0.5 + t * (H - hb - 0.9)
        r = prof(t)
        az = i * 2.39996 + rng.normal(0, 0.3)
        rad = rng.uniform(0.2, 0.62) * r
        cr = max(0.28, r * rng.uniform(0.42, 0.66))
        clumps.append(Clump((rad * math.cos(az), rad * math.sin(az), z), (cr, cr, cr * 1.5)))
    tp = smooth_path([[0, 0, 0], [0.02, 0, 3], [0, 0.02, H * 0.7], [0, 0, H - 1.5]], 10)
    tube("poplar_trunk", tp, np.linspace(0.3, 0.05, 10), bark, bevel_res=5)
    tube("poplar_root", [[0, 0, 0], [0, 0, 0.4]], [0.45, 0.3], bark, bevel_res=5)
    gc = np.array([0, 0, hb + (H - hb) * 0.42])
    gr = (Rmax, Rmax, (H - hb) * 0.55)
    v, f, a, nn = canopy_cards(rng, clumps, density=140, leaf_len=0.24, leaf_aspect=0.7,
                               gcenter=gc, gradii=gr, disp_amp=0.12, disp_freq=2.0,
                               depth=0.15, tilt=0.45,
                               w_card=0.15, w_clump=0.35, w_global=0.5)
    mesh_from_arrays("poplar_leaves", v, f, a, nn, leaf)
    filler("poplar_fill", clumps, leaf, None, gc, gr)
    return dict(base=(0.0, 0.0), height=H, canopy=2 * Rmax)


def build_bush(rng):
    H = 1.3
    leaf = mat_foliage("bush_leaf", dark="#21501d", mid="#3f8429", light="#74ae38",
                       top_tint="#92c048", shadow_tint="#183834", translucency=0.2,
                       ao_dist=0.3, clump_var=0.55)
    gc = np.array([0, 0, 0.6])
    gr = (1.1, 1.1, 0.72)
    # a central dome and a ring of lobes, like a clipped but natural shrub
    clumps = [Clump((0, 0, 0.72), (0.62, 0.62, 0.55))]
    for i in range(7):
        az = i * 2 * math.pi / 7 + rng.normal(0, 0.2)
        rad = rng.uniform(0.45, 0.6)
        r = rng.uniform(0.38, 0.48)
        clumps.append(Clump((rad * math.cos(az), rad * math.sin(az), r * 0.95 + rng.uniform(0, 0.1)),
                            (r, r, r * 0.88)))
    v, f, a, nn = canopy_cards(rng, clumps, density=1400, leaf_len=0.12, leaf_aspect=0.62,
                               gcenter=gc, gradii=gr, disp_amp=0.1, disp_freq=4.0,
                               depth=0.15, tilt=0.35, w_card=0.15, w_clump=0.4, w_global=0.45)
    mesh_from_arrays("bush_leaves", v, f, a, nn, leaf)
    filler("bush_fill", clumps, leaf, None, gc, gr)
    return dict(base=(0.0, 0.0), height=H, canopy=2.2)


def build_palm(rng):
    H = 9.5
    trunk_m = mat_bark("palm_trunk", "#5a4a3a", "#9c8a70", scale=(4, 4, 1.0), bump=0.8,
                       rings=5.5, ring_col="#6a5a48")
    frond_m = mat_vcol("palm_frond", "col", translucency=0.3)
    nut_m = mat_simple("coconut", "#6b4a22", rough=0.6)
    # Curved trunk with a gentle lean.
    n = 24
    ts = np.linspace(0, 1, n)
    tp = np.stack([1.1 * ts ** 1.8 - 0.1 * np.sin(ts * 3), 0.25 * ts ** 2, ts * H], axis=1)
    tube("palm_trunk", tp, 0.19 + 0.08 * (1 - ts) ** 3 + 0.12 * (1 - ts) ** 12, trunk_m, bevel_res=6)
    top = tp[-1]
    # Fronds: a rachis that rises and arches over under its own weight, with
    # paired leaflets hanging from it in a V. Leaflets are tapered strips bent
    # down at the tip; shading normals lean up/out so the crown reads lit.
    V, F, C, NRM = [], [], [], []
    off = 0
    nf = 22
    green_a = srgb_to_lin([0.13, 0.33, 0.09])
    green_b = srgb_to_lin([0.30, 0.50, 0.12])
    tip_col = srgb_to_lin([0.50, 0.55, 0.20])
    dead = srgb_to_lin([0.55, 0.42, 0.22])
    for fi in range(nf + 5):
        is_dead = fi >= nf
        az = fi * 2.39996 + rng.normal(0, 0.12)
        if is_dead:
            el = math.radians(rng.uniform(-80, -62))
            Lf = rng.uniform(2.4, 2.9)
        else:
            # young fronds point up, old ones spread out and droop
            age = fi / nf
            el = math.radians(62 - 80 * age + rng.normal(0, 6))
            Lf = rng.uniform(3.6, 4.3) * (0.8 + 0.25 * min(1, age * 2))
        d = np.array([math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)])
        side = np.cross(d, [0, 0, 1.0])
        side /= np.linalg.norm(side)
        ns = 44
        s = np.linspace(0, 1, ns)
        droop = (0.85 if not is_dead else 0.1) * Lf * (0.6 + 0.4 * math.cos(el))
        spine = top + np.outer(s * Lf, d) + np.outer(droop * s ** 2.0, [0, 0, -1])
        tan = np.gradient(spine, axis=0)
        tan /= np.linalg.norm(tan, axis=1, keepdims=True)
        up = np.cross(side, tan)
        up /= np.linalg.norm(up, axis=1, keepdims=True)
        if is_dead:
            col = dead * rng.uniform(0.8, 1.05)
        else:
            col = green_a + (green_b - green_a) * rng.random()
        outward = np.array([d[0], d[1], 0.0])
        outward /= np.linalg.norm(outward) + 1e-9
        for j in range(1, ns - 1):
            if s[j] < 0.08:
                continue
            u = (s[j] - 0.06) / 0.94
            ll = (1.15 if not is_dead else 0.6) * math.sin(math.pi * min(1.0, u * 0.92 + 0.04)) ** 0.6 + 0.08
            for sd in (-1, 1):
                for rep in range(2):
                    base = spine[j] + tan[j] * rep * 0.05
                    hang = 0.75 if not is_dead else 0.25
                    dirl = sd * side * 0.75 + tan[j] * 0.35 - up[j] * hang * (0.55 + 0.3 * rep)
                    dirl += rng.normal(0, 0.1, 3)
                    dirl /= np.linalg.norm(dirl)
                    wl = 0.075
                    p0 = base
                    p1 = base + dirl * ll * 0.5 + np.array([0, 0, -0.05 * ll])
                    p2 = base + dirl * ll + np.array([0, 0, -0.3 * ll])
                    wv = tan[j]
                    vv = [p0 - wv * wl * 0.5, p0 + wv * wl * 0.5,
                          p1 + wv * wl * 0.6, p1 - wv * wl * 0.6, p2]
                    nrm = up[j] * 0.5 + np.array([0, 0, 0.7]) + outward * 0.5
                    # wind the leaflet so its face normal agrees with the shading
                    # normal; a back face would get the normal flipped (dark)
                    fn = np.cross(vv[1] - vv[0], vv[3] - vv[0])
                    if np.dot(fn, nrm) < 0:
                        vv = [vv[1], vv[0], vv[3], vv[2], vv[4]]
                    V.extend(vv)
                    F.append([off, off + 1, off + 2, off + 3])
                    F.append([off + 3, off + 2, off + 4, off + 4])
                    tint = col * (0.85 + 0.3 * rng.random())
                    if not is_dead and s[j] > 0.8:
                        k = (s[j] - 0.8) / 0.2 * 0.35
                        tint = tint * (1 - k) + tip_col * k
                    C.extend([np.append(tint, 1.0)] * 5)
                    NRM.extend([nrm] * 5)
                    off += 5
    V = np.array(V)
    F = np.array(F)
    ob = mesh_from_arrays("palm_fronds", V, F, normals=np.array(NRM), mat=frond_m)
    add_color_attr(ob, "col", np.array(C))
    print("  palm leaflets:", len(F) // 2)
    # Frond bases / crown shaft.
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=0.35)
    me = bpy.data.meshes.new("palm_crown")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("palm_crown", me)
    ob.location = Vector(top) + Vector((0, 0, -0.1))
    ob.scale = (1, 1, 1.3)
    me.materials.append(mat_simple("palm_crownm", "#5f5a2e", rough=0.8))
    for p in me.polygons:
        p.use_smooth = True
    link(ob)
    # Coconuts.
    for i in range(6):
        az = i * 1.1 + rng.normal(0, 0.2)
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.16)
        me = bpy.data.meshes.new("nut")
        bm.to_mesh(me)
        bm.free()
        for p in me.polygons:
            p.use_smooth = True
        me.materials.append(nut_m)
        ob = bpy.data.objects.new("nut%d" % i, me)
        ob.location = Vector(top) + Vector((0.28 * math.cos(az), 0.28 * math.sin(az), -0.35 - 0.12 * (i % 2)))
        link(ob)
    return dict(base=(0.0, 0.0), height=None, canopy=None)


def build_flag(rng, frame):
    """Pin with a waving flag; frame 0..3 is the wave phase."""
    H = 2.3
    pole_w = mat_simple("pole_white", "#f2f2ee", rough=0.3, spec=0.5)
    pole_r = mat_simple("pole_red", "#d42a1e", rough=0.3, spec=0.5)
    cloth = mat_vcol("flag_cloth", "col", rough=0.7, spec=0.2, translucency=0.25)
    r = 0.032   # pole radius, thickened so it stays ~2 px wide at 64 px
    nseg = 8
    for i in range(nseg):
        z0, z1 = H * i / nseg, H * (i + 1) / nseg
        tube("pole%d" % i, [[0, 0, z0], [0, 0, z1]], [r, r], pole_r if i % 2 else pole_w, bevel_res=3)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.045)
    me = bpy.data.meshes.new("pole_tip")
    bm.to_mesh(me)
    bm.free()
    me.materials.append(pole_w)
    for p in me.polygons:
        p.use_smooth = True
    ob = bpy.data.objects.new("pole_tip", me)
    ob.location = (0, 0, H + 0.02)
    link(ob)
    # Cloth: a grid in the XZ plane flapping in Y, wind blowing towards +X.
    fw, fh = 0.62, 0.42
    nx, nz = 24, 14
    phase = frame * math.pi / 2
    V, C = [], []
    yel = srgb_to_lin([1.0, 0.80, 0.08])
    red = srgb_to_lin([0.86, 0.12, 0.08])
    for j in range(nz + 1):
        for i in range(nx + 1):
            u, v = i / nx, j / nz
            x = u * fw * (1 - 0.07 * (1 + math.sin(phase)) * u)
            amp = 0.17 * u ** 0.8
            y = amp * math.sin(2 * math.pi * (1.3 * u - 0.12 * v) - phase)
            z = H - 0.02 - (1 - v) * fh - 0.07 * u ** 1.5 * (1 - v * 0.3) \
                + 0.06 * u * math.sin(2 * math.pi * (1.1 * u) - phase)
            V.append((x + r * 0.8, y, z))
            col = red if (u < 0.12) else yel     # red hem next to the pole
            C.append(np.append(col, 1.0))
    F = []
    for j in range(nz):
        for i in range(nx):
            a = j * (nx + 1) + i
            F.append([a, a + 1, a + nx + 2, a + nx + 1])
    ob = mesh_from_arrays("flag_cloth", np.array(V), np.array(F), mat=cloth)
    add_color_attr(ob, "col", np.array(C))
    return dict(base=(0.0, 0.0), height=H + 0.07, canopy=None)


BUILDERS = {
    "tree_pine": (build_pine, 11),
    "tree_oak": (build_oak, 7),
    "tree_poplar": (build_poplar, 23),
    "tree_palm": (build_palm, 5),
    "bush": (build_bush, 3),
}

# ---------------------------------------------------------------------------
# Geometry queries


def prop_objects():
    return [o for o in bpy.context.scene.objects if o.type in ("MESH", "CURVE")
            and o.name not in ("bounce_ground", "catcher")]


def world_points(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        n = len(me.vertices)
        a = np.empty(n * 3, dtype=np.float32)
        me.vertices.foreach_get("co", a)
        a = a.reshape(-1, 3).astype(np.float64)
        M = np.array(ev.matrix_world)
        pts.append(a @ M[:3, :3].T + M[:3, 3])
        ev.to_mesh_clear()
    return np.concatenate(pts)


def side_basis():
    # Camera rotation X = 90 - pitch: forward = (0, cos p, -sin p), up = (0, sin p, cos p)
    p = SIDE_PITCH
    right = np.array([1.0, 0, 0])
    up = np.array([0, math.sin(p), math.cos(p)])
    fwd = np.array([0, math.cos(p), -math.sin(p)])
    return right, up, fwd


def make_ortho_cam(name, loc, rot, scale, res):
    cd = bpy.data.cameras.new(name)
    cd.type = "ORTHO"
    cd.ortho_scale = scale
    cd.clip_start = 0.1
    cd.clip_end = 1000
    cd.sensor_fit = "AUTO"
    ob = bpy.data.objects.new(name, cd)
    link(ob)
    ob.location = loc
    ob.rotation_euler = rot
    sc = bpy.context.scene
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y = res
    return ob


# ---------------------------------------------------------------------------
# Image IO: read a rendered PNG, box-downsample, write straight-alpha PNG.

def load_png(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1].astype(np.float64)   # top row first


def downsample(rgba, ss):
    """Box filter in linear premultiplied space. Input/output straight sRGB."""
    h, w, _ = rgba.shape
    lin = srgb_to_lin(rgba[..., :3])
    al = rgba[..., 3:4]
    pm = np.concatenate([lin * al, al], axis=2)
    pm = pm.reshape(h // ss, ss, w // ss, ss, 4).mean(axis=(1, 3))
    a = pm[..., 3:4]
    rgb = np.where(a > 1e-6, pm[..., :3] / np.maximum(a, 1e-6), 0.0)
    return np.concatenate([lin_to_srgb(rgb), a], axis=2)


def bleed(rgba, iters=6):
    """Give transparent pixels the colour of their nearest covered neighbours."""
    rgb = rgba[..., :3].copy()
    known = rgba[..., 3] >= 0.5 / 255
    for _ in range(iters):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                k = np.roll(np.roll(known, dy, 0), dx, 1)
                c = np.roll(np.roll(rgb, dy, 0), dx, 1)
                acc += c * k[..., None]
                cnt += k
        new = (~known) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]
        known = known | new
    out = rgba.copy()
    out[..., :3] = rgb
    return out


def write_png(path, arr):
    """arr uint8 (h, w) for L or (h, w, 4) for RGBA."""
    arr = np.ascontiguousarray(arr, dtype=np.uint8)
    h, w = arr.shape[:2]
    ctype = 0 if arr.ndim == 2 else 6
    raw = b"".join(b"\x00" + arr[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def to_u8(x):
    return np.clip(np.round(x * 255), 0, 255).astype(np.uint8)


def render_to(path_final, mode="rgba"):
    """Render the current scene at ss x, downsample, write the final PNG.
    mode 'rgba' -> straight RGBA; 'alpha' -> L image of the alpha channel."""
    sc = bpy.context.scene
    tmp = os.path.join(TMP, "r.png")
    sc.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    big = load_png(tmp)
    small = downsample(big, ARGS.ss)
    if mode == "alpha":
        write_png(path_final, to_u8(small[..., 3]))
    else:
        write_png(path_final, to_u8(bleed(small)))
    print("  wrote", os.path.basename(path_final), small.shape[1], "x", small.shape[0])
    return small


# ---------------------------------------------------------------------------
# Views

def add_bounce_ground():
    """Invisible fairway: bounces green light and blocks the lower sky."""
    bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, 0))
    g = bpy.context.active_object
    g.name = "bounce_ground"
    g.data.materials.append(mat_simple("ground", GROUND_BOUNCE, rough=0.9, spec=0.1))
    g.visible_camera = False
    g.visible_shadow = False
    return g


def side_view(name, target_h, margin_px=1, frames=None):
    """Render the side view(s). frames: list of callables that rebuild the scene
    for each frame (flag); None = the current scene. Returns meta."""
    right, up, fwd = side_basis()
    info = {}
    # Union bbox (over frames) in camera plane.
    if frames is None:
        frames = [None]
    boxes = []
    for fb in frames:
        if fb is not None:
            fb()
        P = world_points(prop_objects())
        u, v = P @ right, P @ up
        boxes.append((u.min(), u.max(), v.min(), v.max()))
    b = np.array(boxes)
    umin, umax, vmin, vmax = b[:, 0].min(), b[:, 1].max(), b[:, 2].min(), b[:, 3].max()
    ppm = (target_h - 2 * margin_px) / (vmax - vmin)
    W = int(math.ceil((umax - umin) * ppm)) + 2 * margin_px
    W += W % 2
    Hh = target_h
    uc, vc = (umin + umax) / 2, (vmin + vmax) / 2
    ss = ARGS.ss
    scale = max(W, Hh) / ppm
    cen = uc * right + vc * up - fwd * 200
    outs = []
    for fi, fb in enumerate(frames):
        if fb is not None:
            fb()
        sc = bpy.context.scene
        for o in list(sc.objects):
            if o.type in ("CAMERA", "LIGHT") or o.name == "bounce_ground":
                bpy.data.objects.remove(o)
        make_ortho_cam("side_cam", cen, (math.pi / 2 - SIDE_PITCH, 0, 0), scale, (W * ss, Hh * ss))
        add_sun(sun_dir(SIDE_SUN_FROM, SIDE_SUN_ELEV), strength=SUN_STRENGTH)
        set_world(SKY_STRENGTH)
        add_bounce_ground()
        fn = "%s_side.png" % name if len(frames) == 1 else "%s_side_%d.png" % (name, fi)
        render_to(os.path.join(OUT, fn))
        outs.append(fn)
    # trunk base pixel
    base = np.array([0.0, 0.0, 0.0])
    bx = (base @ right - uc) * ppm + W / 2
    by = Hh / 2 - (base @ up - vc) * ppm
    info.update(side_size=[W, Hh], side_files=outs, side_px_per_m=round(ppm, 4),
                side_base_px=[round(bx, 2), round(by, 2)])
    return info


def top_views(name, size=96, fill_px=90):
    sc = bpy.context.scene
    objs = prop_objects()
    P = world_points(objs)
    # Centre the image on the canopy (the palm's crown leans off its base);
    # the trunk base pixel goes to meta.
    top = P[P[:, 2] > 0.3 * P[:, 2].max()]
    ox = (top[:, 0].min() + top[:, 0].max()) / 2
    oy = (top[:, 1].min() + top[:, 1].max()) / 2
    rmax = np.sqrt((top[:, 0] - ox) ** 2 + (top[:, 1] - oy) ** 2).max()
    ppm = (fill_px / 2) / rmax
    ss = ARGS.ss
    # colour
    for o in list(sc.objects):
        if o.type in ("CAMERA", "LIGHT") or o.name == "bounce_ground":
            bpy.data.objects.remove(o)
    make_ortho_cam("top_cam", (ox, oy, 200), (0, 0, 0), size / ppm, (size * ss, size * ss))
    ld = sun_dir(TOP_SUN_FROM, TOP_SUN_ELEV)
    add_sun(ld, strength=SUN_STRENGTH)
    set_world(SKY_STRENGTH)
    add_bounce_ground()
    render_to(os.path.join(OUT, "%s_top.png" % name))
    # shadow: extend the image right/down to hold the whole cast shadow
    t = P[:, 2] / -ld[2]
    S = P + np.outer(t, np.array(ld))
    sx = (S[:, 0] - ox) * ppm + size / 2
    sy = size / 2 - (S[:, 1] - oy) * ppm
    Ws = max(size, int(math.ceil(sx.max())) + 3)
    Hs = max(size, int(math.ceil(sy.max())) + 3)
    for o in list(sc.objects):
        if o.type in ("CAMERA", "LIGHT") or o.name == "bounce_ground":
            bpy.data.objects.remove(o)
    # same top-left origin: world x of pixel 0 = -size/2/ppm, world y of row 0 = +size/2/ppm
    x0, y0 = ox - size / 2 / ppm, oy + size / 2 / ppm
    cx, cy = x0 + Ws / 2 / ppm, y0 - Hs / 2 / ppm
    make_ortho_cam("shadow_cam", (cx, cy, 200), (0, 0, 0), max(Ws, Hs) / ppm, (Ws * ss, Hs * ss))
    add_sun(ld, strength=3.0, angle_deg=2.5)
    set_world(0.0)
    hidden = []
    for o in objs:
        o.visible_camera = False
        hidden.append(o)
    bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, 0))
    catcher = bpy.context.active_object
    catcher.name = "catcher"
    catcher.is_shadow_catcher = True
    catcher.data.materials.append(mat_simple("catcher", "#808080", rough=1.0, spec=0.0))
    render_to(os.path.join(OUT, "%s_topshadow.png" % name), mode="alpha")
    bpy.data.objects.remove(catcher)
    for o in hidden:
        o.visible_camera = True
    return dict(top_size=[size, size], top_px_per_m=round(ppm, 4), top_trunk_px=[round(size / 2 - ox * ppm, 2), round(size / 2 + oy * ppm, 2)],
                topshadow_size=[Ws, Hs], topshadow_origin="same top-left pixel and scale as the top view")


# ---------------------------------------------------------------------------
# Main

def canopy_stats():
    P = world_points(prop_objects())
    h = P[:, 2].max()
    d = 2 * np.sqrt(P[:, 0] ** 2 + P[:, 1] ** 2).max()
    dx = P[:, 0].max() - P[:, 0].min()
    dy = P[:, 1].max() - P[:, 1].min()
    return float(h), float(max(dx, dy)), float(d)


def run_tree(name):
    builder, seed = BUILDERS[name]
    print("==", name)
    reset_scene()
    rng = np.random.RandomState(seed)
    builder(rng)
    h, diam, _ = canopy_stats()
    target = 48 if name == "bush" else 128
    meta = dict(height_m=round(h, 2), canopy_diameter_m=round(diam, 2))
    meta.update(side_view(name, target))
    meta.update(top_views(name))
    return meta


def run_flag():
    print("== flag")
    reset_scene()
    rng = np.random.RandomState(1)

    def frame_builder(fi):
        def f():
            for o in list(bpy.context.scene.objects):
                if o.type in ("MESH", "CURVE"):
                    bpy.data.objects.remove(o)
            build_flag(rng, fi)
        return f

    frames = [frame_builder(i) for i in range(4)]
    info = side_view("flag", 64, frames=frames)
    h, diam, _ = canopy_stats()
    info["height_m"] = round(h, 2)
    info["frames"] = 4
    info["note"] = "base = bottom of the pin (hole centre); wind towards +x (screen right)"
    return info


def main():
    only = [s.strip() for s in ARGS.only.split(",") if s.strip()] or ALL_PROPS
    meta_path = os.path.join(OUT, "meta.json")
    meta = {}
    if os.path.exists(meta_path):
        try:
            with open(meta_path) as f:
                meta = json.load(f)
        except Exception:
            meta = {}
    meta.setdefault("_about", {})
    meta["_about"] = {
        "side": "orthographic, camera pitched down 11 deg looking +y, sun from behind-left of "
                "the camera (elev 50 deg); side_base_px = trunk base (ground contact) pixel, "
                "x right / y down, pixel-centre convention: pixel (i,j) spans [i,i+1)",
        "top": "orthographic straight down, image up = north (+y), sun from north-west "
               "(elev 50 deg); top_trunk_px = trunk centre pixel",
        "topshadow": "L 0..255 = shadow strength on the ground; may be larger than 96x96 "
                     "(extends right/down only), same origin and px/m as the top view",
        "alpha": "straight (unassociated) alpha; transparent pixels carry the nearest edge colour",
        "units": "metres; px_per_m lets the engine scale by the real size",
    }
    for name in only:
        if name == "flag":
            meta["flag"] = run_flag()
        elif name in BUILDERS:
            meta[name] = run_tree(name)
        else:
            print("unknown prop", name)
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    print("meta ->", meta_path)
    import shutil
    shutil.rmtree(TMP, ignore_errors=True)   # the supersampled intermediates


main()
