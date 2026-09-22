"""Procedural scenery props and backdrops for Turbo, rendered to sprites in Blender.

    Blender -b -P props.py -- --out ../../assets/props
                              [--only cone,palm] [--stage city,coast]
                              [--no-props] [--no-bg] [--ss 3] [--samples 128]

Everything is built from code (no .blend inputs). See SPEC.md, "Props" and
"Backdrops". Summary of the conventions used here:

Props
  Camera at (0, 0, 2) looking +Y, no pitch (rotation X = 90), the prop's
  base-centre at (0, 40, 0). A long-lens pinhole camera whose focal length
  and principal point (lens shift) are chosen so the prop fills its image.
  ppm = focal_px / 40 = pixels per metre at the prop's base plane (Y = 40).
  Asymmetric props are modelled for the RIGHT side of the road (the road is
  towards -X); buildings and roadside blocks are yawed a little (positive =
  counter-clockwise seen from above) so the face that looks at the road
  (-X) shows, as it does in the game; the yaw is in meta.json.
  Colour: RGBA PNG, straight alpha, transparent pixels carry the nearest
  edge colour. Optional <name>_shadow.png: L 0..255 = how much the prop
  darkens the ground, same camera, its own image size and anchor.
  Night (mountain) props are lit by a dim blue moon with warm emitters, and
  space props by a cold key light; their emitters get a soft glow baked into
  the colour and alpha.

Backdrops
  bg_<stage>.png, 1024 x 160: a 360 degree equirectangular band from the
  horizon (bottom row) up to 56.25 degrees, with SQUARE angular pixels
  (1024 px = 360 deg, so 160 px = 56.25 deg) so the shapes are not
  stretched when the watch draws it 1:1. The x = 0 column looks along +Y.
  Alpha = 0 where the sky shows through.

Renders go to EXR (linear float, supersampled --ss times) and are box
filtered down in linear premultiplied space, then written as 8-bit PNG.
"""

import bpy
import bmesh
import math
import os
import sys
import json
import time
import zlib
import struct
import argparse
import tempfile
import shutil

import numpy as np
from mathutils import Vector, Matrix, Euler

# ---------------------------------------------------------------------------
# Arguments

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                              "..", "..", "assets", "props"))
ap.add_argument("--only", default="", help="comma list of prop names (or bg_<stage>)")
ap.add_argument("--stage", default="", help="comma list of stages")
ap.add_argument("--no-props", action="store_true")
ap.add_argument("--no-bg", action="store_true")
ap.add_argument("--ss", type=int, default=3, help="supersampling factor")
ap.add_argument("--samples", type=int, default=128)
ap.add_argument("--cpu", action="store_true")
ARGS = ap.parse_args(argv)
OUT = os.path.abspath(ARGS.out)
os.makedirs(OUT, exist_ok=True)
TMP = tempfile.mkdtemp(prefix="turbo_props_")

CAM_H = 2.0
PROP_Y = 40.0
STAGES = ["common", "city", "coast", "desert", "mountain", "space"]

# ---------------------------------------------------------------------------
# Colour helpers


def srgb_to_lin(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def hexcol(h, a=1.0):
    if not isinstance(h, str):
        return tuple(h[:3]) + (a,)
    h = h.lstrip("#")
    rgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(float(v) for v in srgb_to_lin(rgb)) + (a,)


def sun_vec(frm_xy, elev_deg):
    """Direction the light travels (from the sun to the ground)."""
    e = math.radians(elev_deg)
    f = Vector((frm_xy[0], frm_xy[1], 0)).normalized()
    v = Vector((f.x * math.cos(e), f.y * math.cos(e), math.sin(e)))
    return -v.normalized()


# Lighting rigs. Day: sun from behind-left of the camera (-X, -Y), 55 deg up,
# warm white, soft blue sky. Night: the same direction but a dim blue moon.
LIGHTS = {
    "day": dict(sun_from=(-1, -1), elev=55, sun_col=(1.0, 0.95, 0.86), sun=4.2,
                sky_top="#5b8fd6", sky_hor="#c9dcf0", sky=0.9, ground="#8a8a80", glow=0.0),
    "night": dict(sun_from=(-1, -1), elev=50, sun_col=(0.62, 0.74, 1.0), sun=1.1,
                  sky_top="#0a1430", sky_hor="#1f3560", sky=0.55, ground="#5a6a88", glow=1.0),
    "space": dict(sun_from=(-1, -1), elev=45, sun_col=(0.92, 0.9, 1.0), sun=2.6,
                  sky_top="#12052a", sky_hor="#3a1760", sky=0.45, ground="#301848", glow=1.0,
                  rim=((1.0, 1.0), 20, 5.0, (1.0, 0.35, 0.85))),
}

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
    cyc.adaptive_threshold = 0.01
    cyc.use_denoising = False
    cyc.max_bounces = 6
    cyc.diffuse_bounces = 3
    cyc.glossy_bounces = 3
    cyc.transmission_bounces = 6
    cyc.transparent_max_bounces = 12
    cyc.filter_width = 1.5
    cyc.sample_clamp_indirect = 4.0
    cyc.seed = 3
    sc.render.film_transparent = True
    sc.render.dither_intensity = 0.0
    im = sc.render.image_settings
    im.file_format = "OPEN_EXR"
    im.color_mode = "RGBA"
    im.color_depth = "32"
    im.exr_codec = "ZIP"
    sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0
    sc.view_settings.gamma = 1.0
    sc.display_settings.display_device = "sRGB"
    return sc


def set_world(top, hor, strength, ground=None):
    """Vertical gradient world: `top` overhead, `hor` at the horizon, `ground` below."""
    sc = bpy.context.scene
    w = bpy.data.worlds.new("World")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.elements[0].position = 0.0
    cr.elements[0].color = hexcol(ground or hor)
    cr.elements[1].position = 1.0
    cr.elements[1].color = hexcol(top)
    e = cr.elements.new(0.5)
    e.color = hexcol(hor)
    e2 = cr.elements.new(0.49)
    e2.color = hexcol(ground or hor)
    mapr = nt.nodes.new("ShaderNodeMapRange")
    mapr.inputs[1].default_value = -1.0
    mapr.inputs[2].default_value = 1.0
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs[1].default_value = strength
    nt.links.new(tc.outputs["Generated"], sep.inputs[0])
    nt.links.new(sep.outputs["Z"], mapr.inputs[0])
    nt.links.new(mapr.outputs[0], ramp.inputs[0])
    nt.links.new(ramp.outputs[0], bg.inputs[0])
    nt.links.new(bg.outputs[0], out.inputs[0])


def add_sun(direction, strength, color, angle_deg=3.0):
    ld = bpy.data.lights.new("Sun", "SUN")
    ld.energy = strength
    ld.angle = math.radians(angle_deg)
    ld.color = color
    ob = bpy.data.objects.new("Sun", ld)
    bpy.context.scene.collection.objects.link(ob)
    ob.rotation_euler = Vector(direction).to_track_quat("-Z", "Y").to_euler()
    return ob


def add_point(loc, energy, color, radius=0.2, name="Lamp"):
    ld = bpy.data.lights.new(name, "POINT")
    ld.energy = energy
    ld.color = color
    ld.shadow_soft_size = radius
    ob = bpy.data.objects.new(name, ld)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = loc
    return ob


def setup_lights(kind):
    L = LIGHTS[kind]
    set_world(L["sky_top"], L["sky_hor"], L["sky"], L["ground"])
    add_sun(sun_vec(L["sun_from"], L["elev"]), L["sun"], L["sun_col"])
    if L.get("rim"):
        frm, elev, strength, col = L["rim"]
        add_sun(sun_vec(frm, elev), strength, col, angle_deg=6.0)


def link(ob):
    bpy.context.scene.collection.objects.link(ob)
    return ob


# ---------------------------------------------------------------------------
# Materials


def _nodes(mat):
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    return nt, nt.nodes, nt.links


_MATS = {}


def M(color, rough=0.5, spec=0.4, metallic=0.0, emit=None, emit_str=0.0, name=None,
      clearcoat=0.0, transmission=0.0, alpha=1.0):
    key = (str(color), rough, spec, metallic, str(emit), emit_str, clearcoat, transmission, alpha)
    if key in _MATS and name is None:
        return _MATS[key]
    mat = bpy.data.materials.new(name or "m")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Base Color"].default_value = hexcol(color)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Clearcoat"].default_value = clearcoat
    b.inputs["Transmission"].default_value = transmission
    b.inputs["Alpha"].default_value = alpha
    if emit is not None:
        b.inputs["Emission"].default_value = hexcol(emit)
        b.inputs["Emission Strength"].default_value = emit_str
    L.new(b.outputs[0], out.inputs["Surface"])
    if alpha < 1:
        mat.blend_method = "BLEND"
    _MATS[key] = mat
    return mat


def M_emit(color, strength, name="emit"):
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    e = N.new("ShaderNodeEmission")
    e.inputs[0].default_value = hexcol(color)
    e.inputs[1].default_value = strength
    L.new(e.outputs[0], out.inputs["Surface"])
    return mat


def M_holdout():
    mat = bpy.data.materials.new("holdout")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    h = N.new("ShaderNodeHoldout")
    L.new(h.outputs[0], out.inputs["Surface"])
    return mat


def _coord(N, L, space="Object", scale=(1, 1, 1), offset=(0, 0, 0)):
    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = scale
    mp.inputs["Location"].default_value = offset
    L.new(tc.outputs[space], mp.inputs[0])
    return mp.outputs[0]


def M_noise(c1, c2, scale=2.0, detail=6, rough=0.7, spec=0.3, bump=0.3, bump_dist=0.05,
            lo=0.35, hi=0.65, coord_scale=(1, 1, 1), metallic=0.0, c3=None, c3_amt=0.0,
            c3_scale=0.5, name="noise", space="Object", strata=0.0, strata_col=None,
            strata_scale=1.0, snow=0.0, snow_col="#f4f8ff", snow_lo=0.55, snow_hi=0.75,
            emit_col=None, emit_str=0.0):
    """Two-colour noise material, optional third-colour blotches (rust, moss),
    horizontal strata (rock layers), snow on up-facing surfaces."""
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    co = _coord(N, L, space, coord_scale)
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = scale
    nz.inputs["Detail"].default_value = detail
    nz.inputs["Roughness"].default_value = 0.6
    L.new(co, nz.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = lo
    ramp.color_ramp.elements[0].color = hexcol(c1)
    ramp.color_ramp.elements[1].position = hi
    ramp.color_ramp.elements[1].color = hexcol(c2)
    L.new(nz.outputs["Fac"], ramp.inputs[0])
    col = ramp.outputs[0]
    height = nz.outputs["Fac"]
    if strata > 0:
        wv = N.new("ShaderNodeTexWave")
        wv.wave_type = "BANDS"
        wv.bands_direction = "Z"
        wv.inputs["Scale"].default_value = strata_scale
        wv.inputs["Distortion"].default_value = 3.0
        wv.inputs["Detail"].default_value = 3
        L.new(co, wv.inputs[0])
        wr = N.new("ShaderNodeValToRGB")
        wr.color_ramp.elements[0].position = 0.2
        wr.color_ramp.elements[0].color = hexcol(strata_col or c1)
        wr.color_ramp.elements[1].position = 0.8
        wr.color_ramp.elements[1].color = (1, 1, 1, 1)
        L.new(wv.outputs["Fac"], wr.inputs[0])
        m2 = N.new("ShaderNodeMixRGB")
        m2.blend_type = "MULTIPLY"
        m2.inputs[0].default_value = strata
        L.new(col, m2.inputs[1])
        L.new(wr.outputs[0], m2.inputs[2])
        col = m2.outputs[0]
    if c3 is not None and c3_amt > 0:
        n3 = N.new("ShaderNodeTexNoise")
        n3.inputs["Scale"].default_value = c3_scale
        n3.inputs["Detail"].default_value = 4
        L.new(co, n3.inputs[0])
        r3 = N.new("ShaderNodeMapRange")
        r3.inputs[1].default_value = 1.0 - c3_amt
        r3.inputs[2].default_value = 1.0 - c3_amt + 0.08
        L.new(n3.outputs["Fac"], r3.inputs[0])
        m3 = N.new("ShaderNodeMixRGB")
        L.new(r3.outputs[0], m3.inputs[0])
        L.new(col, m3.inputs[1])
        m3.inputs[2].default_value = hexcol(c3)
        col = m3.outputs[0]
    if snow > 0:
        geo = N.new("ShaderNodeNewGeometry")
        sepn = N.new("ShaderNodeSeparateXYZ")
        L.new(geo.outputs["Normal"], sepn.inputs[0])
        # noisy threshold so the snow line is ragged
        add = N.new("ShaderNodeMath")
        add.operation = "MULTIPLY_ADD"
        L.new(nz.outputs["Fac"], add.inputs[0])
        add.inputs[1].default_value = 0.5
        L.new(sepn.outputs["Z"], add.inputs[2])
        sr = N.new("ShaderNodeMapRange")
        sr.inputs[1].default_value = snow_lo + 0.25
        sr.inputs[2].default_value = snow_hi + 0.25
        sr.inputs[4].default_value = snow
        L.new(add.outputs[0], sr.inputs[0])
        ms = N.new("ShaderNodeMixRGB")
        L.new(sr.outputs[0], ms.inputs[0])
        L.new(col, ms.inputs[1])
        ms.inputs[2].default_value = hexcol(snow_col)
        col = ms.outputs[0]
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    b.inputs["Metallic"].default_value = metallic
    L.new(col, b.inputs["Base Color"])
    if emit_col is not None:
        b.inputs["Emission"].default_value = hexcol(emit_col)
        b.inputs["Emission Strength"].default_value = emit_str
    if bump > 0:
        bp = N.new("ShaderNodeBump")
        bp.inputs["Strength"].default_value = bump
        bp.inputs["Distance"].default_value = bump_dist
        L.new(height, bp.inputs["Height"])
        L.new(bp.outputs[0], b.inputs["Normal"])
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


def M_grid(wall, win, cell=(3.0, 3.5), frac=(0.55, 0.5), rough_win=0.15, win_emit=None,
           emit_str=0.0, wall2=None, name="grid", win_var=0.25, lit_frac=0.0, lit_col=None,
           lit_str=0.0, bump_wall=0.0, spec_win=0.6):
    """Facade: a grid of windows (win) in a wall. Axis-aligned boxes: the grid
    runs on (x + y, z) in object space. win_var varies the glass per window."""
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    sep = N.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])
    sx = N.new("ShaderNodeMath"); sx.operation = "ADD"
    L.new(sep.outputs["X"], sx.inputs[0]); L.new(sep.outputs["Y"], sx.inputs[1])

    def cellf(inp, size, fr):
        d = N.new("ShaderNodeMath"); d.operation = "DIVIDE"
        L.new(inp, d.inputs[0]); d.inputs[1].default_value = size
        fl = N.new("ShaderNodeMath"); fl.operation = "FLOOR"
        L.new(d.outputs[0], fl.inputs[0])
        fr_ = N.new("ShaderNodeMath"); fr_.operation = "FRACT"
        L.new(d.outputs[0], fr_.inputs[0])
        # inside window if |fract - 0.5| < fr/2
        s = N.new("ShaderNodeMath"); s.operation = "SUBTRACT"
        L.new(fr_.outputs[0], s.inputs[0]); s.inputs[1].default_value = 0.5
        a = N.new("ShaderNodeMath"); a.operation = "ABSOLUTE"
        L.new(s.outputs[0], a.inputs[0])
        lt = N.new("ShaderNodeMath"); lt.operation = "LESS_THAN"
        L.new(a.outputs[0], lt.inputs[0]); lt.inputs[1].default_value = fr / 2
        return lt.outputs[0], fl.outputs[0]

    mx, fx = cellf(sx.outputs[0], cell[0], frac[0])
    mz, fz = cellf(sep.outputs["Z"], cell[1], frac[1])
    m = N.new("ShaderNodeMath"); m.operation = "MULTIPLY"
    L.new(mx, m.inputs[0]); L.new(mz, m.inputs[1])
    # per-window random from the cell index
    cmb = N.new("ShaderNodeCombineXYZ")
    L.new(fx, cmb.inputs[0]); L.new(fz, cmb.inputs[1])
    wn = N.new("ShaderNodeTexWhiteNoise")
    wn.noise_dimensions = "3D"
    L.new(cmb.outputs[0], wn.inputs[0])
    wr = N.new("ShaderNodeMapRange")
    wr.inputs[3].default_value = 1 - win_var
    wr.inputs[4].default_value = 1 + win_var
    L.new(wn.outputs["Value"], wr.inputs[0])
    wc = N.new("ShaderNodeMixRGB"); wc.blend_type = "MULTIPLY"
    wc.inputs[0].default_value = 1.0
    wc.inputs[1].default_value = hexcol(win)
    L.new(wr.outputs[0], wc.inputs[2])
    # wall colour (with a slow noise for dirt)
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 0.35
    L.new(tc.outputs["Object"], nz.inputs[0])
    wmix = N.new("ShaderNodeMixRGB")
    wmix.inputs[1].default_value = hexcol(wall)
    wmix.inputs[2].default_value = hexcol(wall2 or wall)
    L.new(nz.outputs["Fac"], wmix.inputs[0])
    col = N.new("ShaderNodeMixRGB")
    L.new(m.outputs[0], col.inputs[0])
    L.new(wmix.outputs[0], col.inputs[1])
    L.new(wc.outputs[0], col.inputs[2])
    rgh = N.new("ShaderNodeMapRange")
    rgh.inputs[3].default_value = 0.8
    rgh.inputs[4].default_value = rough_win
    L.new(m.outputs[0], rgh.inputs[0])
    spc = N.new("ShaderNodeMapRange")
    spc.inputs[3].default_value = 0.25
    spc.inputs[4].default_value = spec_win
    L.new(m.outputs[0], spc.inputs[0])
    b = N.new("ShaderNodeBsdfPrincipled")
    L.new(col.outputs[0], b.inputs["Base Color"])
    L.new(rgh.outputs[0], b.inputs["Roughness"])
    L.new(spc.outputs[0], b.inputs["Specular"])
    if lit_frac > 0:
        lt = N.new("ShaderNodeMath"); lt.operation = "LESS_THAN"
        L.new(wn.outputs["Value"], lt.inputs[0]); lt.inputs[1].default_value = lit_frac
        lm = N.new("ShaderNodeMath"); lm.operation = "MULTIPLY"
        L.new(lt.outputs[0], lm.inputs[0]); L.new(m.outputs[0], lm.inputs[1])
        ls = N.new("ShaderNodeMath"); ls.operation = "MULTIPLY"
        L.new(lm.outputs[0], ls.inputs[0]); ls.inputs[1].default_value = lit_str
        b.inputs["Emission"].default_value = hexcol(lit_col or "#ffc070")
        L.new(ls.outputs[0], b.inputs["Emission Strength"])
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


def M_vcol(attr="col", rough=0.5, spec=0.4, emit_attr=None, emit_str=0.0, metallic=0.0):
    """Colour from a per-corner colour attribute (face colours)."""
    mat = bpy.data.materials.new("vcol")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    at = N.new("ShaderNodeAttribute"); at.attribute_name = attr
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    b.inputs["Metallic"].default_value = metallic
    L.new(at.outputs["Color"], b.inputs["Base Color"])
    if emit_attr:
        ae = N.new("ShaderNodeAttribute"); ae.attribute_name = emit_attr
        L.new(ae.outputs["Color"], b.inputs["Emission"])
        b.inputs["Emission Strength"].default_value = emit_str
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


def M_checker(c1, c2, size, rough=0.5, spec=0.3):
    mat = bpy.data.materials.new("checker")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    co = _coord(N, L, "Object", (1.0 / size, 1.0 / size, 1.0 / size), (0.013, 0.013, 0.013))
    ck = N.new("ShaderNodeTexChecker")
    ck.inputs["Scale"].default_value = 1.0
    ck.inputs["Color1"].default_value = hexcol(c1)
    ck.inputs["Color2"].default_value = hexcol(c2)
    L.new(co, ck.inputs[0])
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    L.new(ck.outputs["Color"], b.inputs["Base Color"])
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


def M_bands(colors, axis="Z", size=1.0, rough=0.5, spec=0.4, offset=0.0, emit=0.0):
    """Hard stripes along an object axis cycling through colours."""
    mat = bpy.data.materials.new("bands")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    sep = N.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])
    d = N.new("ShaderNodeMath"); d.operation = "MULTIPLY_ADD"
    L.new(sep.outputs[axis], d.inputs[0])
    d.inputs[1].default_value = 1.0 / (size * len(colors))
    d.inputs[2].default_value = offset
    fr = N.new("ShaderNodeMath"); fr.operation = "FRACT"
    L.new(d.outputs[0], fr.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.interpolation = "CONSTANT"
    n = len(colors)
    cr.elements[0].position = 0.0
    cr.elements[0].color = hexcol(colors[0])
    cr.elements[1].position = 1.0 / n if n > 1 else 1.0
    cr.elements[1].color = hexcol(colors[1 % n])
    for i in range(2, n):
        e = cr.elements.new(i / n)
        e.color = hexcol(colors[i])
    L.new(fr.outputs[0], ramp.inputs[0])
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    L.new(ramp.outputs[0], b.inputs["Base Color"])
    if emit > 0:
        L.new(ramp.outputs[0], b.inputs["Emission"])
        b.inputs["Emission Strength"].default_value = emit
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


# ---------------------------------------------------------------------------
# Geometry helpers


def _obj_from_bm(name, bm, mat, smooth=False):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    if mat is not None:
        if isinstance(mat, (list, tuple)):
            for m_ in mat:
                me.materials.append(m_)
        else:
            me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = smooth
    ob = bpy.data.objects.new(name, me)
    return link(ob)


def mesh(name, verts, faces, mat=None, smooth=False, mat_idx=None, fcols=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(map(float, v)) for v in verts], [], [tuple(int(i) for i in f) for f in faces])
    me.update(calc_edges=True)
    if mat is not None:
        if isinstance(mat, (list, tuple)):
            for m_ in mat:
                me.materials.append(m_)
        else:
            me.materials.append(mat)
    if mat_idx is not None:
        me.polygons.foreach_set("material_index", np.asarray(mat_idx, dtype=np.int32))
    for p in me.polygons:
        p.use_smooth = smooth
    if fcols is not None:
        a = me.attributes.new("col", "FLOAT_COLOR", "CORNER")
        cc = []
        for p, c in zip(me.polygons, fcols):
            cc.extend([c] * p.loop_total)
        a.data.foreach_set("color", np.asarray(cc, dtype=np.float32).ravel())
    ob = bpy.data.objects.new(name, me)
    return link(ob)


def bevel(ob, w, segs=2, angle=None):
    m = ob.modifiers.new("bevel", "BEVEL")
    m.width = w
    m.segments = segs
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle or 40)
    m.use_clamp_overlap = True
    return ob


def box(name, c, s, mat, bev=0.0, segs=2, rot_z=0.0, smooth=False):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * s[0], v.co.y * s[1], v.co.z * s[2]))
    ob = _obj_from_bm(name, bm, mat, smooth)
    ob.location = c
    ob.rotation_euler = (0, 0, rot_z)
    if bev > 0:
        bevel(ob, bev, segs)
    return ob


def box2(name, lo, hi, mat, bev=0.0, segs=2):
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    return box(name, (lo + hi) / 2, hi - lo, mat, bev, segs)


def cyl(name, p0, p1, r0, mat, r1=None, n=20, smooth=True, caps=True):
    """Cylinder / cone frustum between two points."""
    p0, p1 = Vector(p0), Vector(p1)
    r1 = r0 if r1 is None else r1
    d = p1 - p0
    Lh = d.length
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=n,
                          radius1=r0, radius2=r1, depth=Lh)
    ob = _obj_from_bm(name, bm, mat, smooth)
    ob.location = (p0 + p1) / 2
    ob.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    if smooth:
        ob.data.use_auto_smooth = True
        ob.data.auto_smooth_angle = math.radians(50)
    return ob


def sphere(name, c, r, mat, scale=(1, 1, 1), subdiv=3, smooth=True):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=r)
    ob = _obj_from_bm(name, bm, mat, smooth)
    ob.location = c
    ob.scale = scale
    return ob


def uvsphere(name, c, r, mat, scale=(1, 1, 1), u=24, v=12):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=u, v_segments=v, radius=r)
    ob = _obj_from_bm(name, bm, mat, True)
    ob.location = c
    ob.scale = scale
    return ob


def prism(name, poly_xz, y0, y1, mat, smooth=False):
    """Extrude a 2D polygon given in (x, z) between y0 and y1."""
    n = len(poly_xz)
    V = [(x, y0, z) for x, z in poly_xz] + [(x, y1, z) for x, z in poly_xz]
    # orientation: make the front face (y0) point -Y
    area = sum(poly_xz[i][0] * poly_xz[(i + 1) % n][1] - poly_xz[(i + 1) % n][0] * poly_xz[i][1]
               for i in range(n))
    idx = list(range(n))
    if area < 0:
        idx = idx[::-1]
    F = [idx, [i + n for i in idx[::-1]]]
    for k in range(n):
        a, b = idx[k], idx[(k + 1) % n]
        F.append([a, b, b + n, a + n][::-1] if True else None)
    ob = mesh(name, V, F, mat, smooth)
    ob.data.validate()
    # recalc normals
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def tube(name, pts, radii, mat, bevel_res=4, fill_caps=True):
    """A tapered tube along a polyline (curve with bevel)."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 1.0
    cu.bevel_resolution = bevel_res
    cu.use_fill_caps = fill_caps
    cu.resolution_u = 4
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    if np.isscalar(radii):
        radii = [radii] * len(pts)
    for p, q, r in zip(sp.points, pts, radii):
        p.co = (float(q[0]), float(q[1]), float(q[2]), 1.0)
        p.radius = float(r)
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
        return np.clip((np.sin(p @ self.w.T + self.ph) * self.a).sum(axis=1), -1.5, 1.5)


def rock(name, c, r, mat, rng, scale=(1, 1, 1), amp=0.25, freq=1.2, subdiv=4, flat_bottom=True,
         facets=0.0):
    """Noise-displaced icosphere; optional flattening of the base at z = c.z."""
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    nz = Noise3(rng, freq)
    nz2 = Noise3(rng, freq * 3.1)
    P = np.array([v.co[:] for v in bm.verts])
    d = 1 + amp * nz(P) + amp * 0.35 * nz2(P)
    if facets > 0:
        # quantise the radius into ledges for a chunky, chipped look
        d = d + facets * np.round(d / facets * 1.0) * 0 + facets * np.sin(P @ np.array([7.1, 3.3, 5.7])) * 0.0
    P = P * d[:, None] * np.asarray(scale) * r
    if flat_bottom:
        P[:, 2] = np.maximum(P[:, 2], -0.15 * r * scale[2])
    for v, p in zip(bm.verts, P):
        v.co = Vector(p)
    ob = _obj_from_bm(name, bm, mat, True)
    ob.location = c
    return ob


def ribbed_tube(name, path, radii, mat, ribs=12, rib_amp=0.08, nring=36, cap=True):
    """A tube with a star (ribbed) cross-section along a path; rounded top."""
    path = np.asarray(path, float)
    radii = np.asarray(radii, float)
    n = len(path)
    tan = np.gradient(path, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    V, F = [], []
    ref = np.array([1.0, 0, 0])
    for i in range(n):
        t = tan[i]
        a = ref - t * np.dot(ref, t)
        if np.linalg.norm(a) < 1e-3:
            a = np.array([0, 1.0, 0]) - t * t[1]
        a /= np.linalg.norm(a)
        b = np.cross(t, a)
        for k in range(nring):
            ph = 2 * math.pi * k / nring
            rr = radii[i] * (1 + rib_amp * math.cos(ribs * ph))
            V.append(path[i] + rr * (math.cos(ph) * a + math.sin(ph) * b))
    for i in range(n - 1):
        for k in range(nring):
            a0 = i * nring + k
            a1 = i * nring + (k + 1) % nring
            F.append([a0, a1, a1 + nring, a0 + nring])
    if cap:
        V.append(path[-1] + tan[-1] * radii[-1] * 0.2)
        c = len(V) - 1
        for k in range(nring):
            F.append([(n - 1) * nring + k, (n - 1) * nring + (k + 1) % nring, c])
        V.append(path[0] - tan[0] * 0.01)
        c0 = len(V) - 1
        for k in range(nring):
            F.append([(k + 1) % nring, k, c0])
    ob = mesh(name, V, F, mat, smooth=True)
    return ob


def join(objs, name):
    """Join mesh objects (after applying modifiers they keep)."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    objs[0].name = name
    return objs[0]


# ---------------------------------------------------------------------------
# Foliage (the Golf technique: leaf cards on noise-displaced clump shells,
# shading normals blended with clump and crown normals, dark inner fillers)


def mesh_from_arrays(name, verts, faces, attrs=None, normals=None, mat=None, smooth=True):
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


def rot_from_axis(axis, up=(0, 0, 1)):
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
        q = (p - self.c) @ self.R
        return np.sqrt(((q / self.r) ** 2).sum(axis=1))

    def area(self):
        a, b, c = self.r
        k = 1.6
        return 4 * math.pi * (((a * b) ** k + (a * c) ** k + (b * c) ** k) / 3) ** (1 / k)


MIN_SHELL = 0.6


def canopy_cards(rng, clumps, density, leaf_len, leaf_aspect, gcenter, gradii,
                 disp_amp=0.16, disp_freq=1.3, depth=0.22, tilt=0.55, fold=0.18,
                 w_card=0.25, w_clump=0.45, w_global=0.30, hide=0.86, droop=0.0,
                 len_jitter=0.35):
    global MIN_SHELL
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
    NG = (P - gcenter) / np.asarray(gradii) ** 2
    NG /= np.linalg.norm(NG, axis=1, keepdims=True)
    cn = NC + tilt * rand_unit(rng, N)
    cn /= np.linalg.norm(cn, axis=1, keepdims=True)
    t = np.cross(cn, rand_unit(rng, N))
    if droop:
        t = t + np.array([0, 0, -droop])
        t -= cn * (t * cn).sum(axis=1, keepdims=True)
    t /= np.linalg.norm(t, axis=1, keepdims=True) + 1e-9
    b = np.cross(cn, t)
    Ln = leaf_len * (1 + len_jitter * (rng.random(N) * 2 - 1))
    W = Ln * leaf_aspect
    Lc, Wc = Ln[:, None], W[:, None]
    v0 = P - t * Lc * 0.5
    v1 = P - b * Wc * 0.5 + t * Lc * 0.05 + cn * Wc * fold
    v2 = P + t * Lc * 0.5
    v3 = P + b * Wc * 0.5 + t * Lc * 0.05 + cn * Wc * fold
    verts = np.stack([v0, v1, v2, v3], axis=1).reshape(-1, 3)
    faces = np.arange(4 * N, dtype=np.int32).reshape(N, 4)
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


def filler(name, clumps, mat, scale=None, gcenter=None, gradii=None):
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


def mat_foliage(name, dark, mid, light, top_tint, shadow_tint, translucency=0.25,
                ao_dist=0.9, ao_strength=0.6, rough=0.55, spec=0.3, clump_var=0.45,
                snow=0.0, snow_col="#eef4ff"):
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    a_rnd = N.new("ShaderNodeAttribute"); a_rnd.attribute_name = "rnd"
    a_cl = N.new("ShaderNodeAttribute"); a_cl.attribute_name = "clump"
    a_h = N.new("ShaderNodeAttribute"); a_h.attribute_name = "h"
    a_fill = N.new("ShaderNodeAttribute"); a_fill.attribute_name = "fill"
    mx = N.new("ShaderNodeMixRGB")
    mx.inputs[0].default_value = clump_var
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
    hr = N.new("ShaderNodeMapRange")
    hr.inputs[1].default_value = 0.55
    hr.inputs[2].default_value = 1.0
    hr.inputs[3].default_value = 0.0
    hr.inputs[4].default_value = 0.35
    L.new(a_h.outputs["Fac"], hr.inputs[0])
    mh = N.new("ShaderNodeMixRGB")
    L.new(hr.outputs[0], mh.inputs[0])
    L.new(ramp.outputs[0], mh.inputs[1])
    mh.inputs[2].default_value = hexcol(top_tint)
    col = mh.outputs[0]
    if snow > 0:
        geo = N.new("ShaderNodeNewGeometry")
        sepn = N.new("ShaderNodeSeparateXYZ")
        L.new(geo.outputs["Normal"], sepn.inputs[0])
        ad = N.new("ShaderNodeMath"); ad.operation = "MULTIPLY_ADD"
        L.new(a_rnd.outputs["Fac"], ad.inputs[0]); ad.inputs[1].default_value = 0.35
        L.new(sepn.outputs["Z"], ad.inputs[2])
        sr = N.new("ShaderNodeMapRange")
        sr.inputs[1].default_value = 0.55
        sr.inputs[2].default_value = 0.8
        sr.inputs[4].default_value = snow
        L.new(ad.outputs[0], sr.inputs[0])
        ms = N.new("ShaderNodeMixRGB")
        L.new(sr.outputs[0], ms.inputs[0])
        L.new(col, ms.inputs[1])
        ms.inputs[2].default_value = hexcol(snow_col)
        col = ms.outputs[0]
    mf = N.new("ShaderNodeMixRGB"); mf.blend_type = "MULTIPLY"
    L.new(a_fill.outputs["Fac"], mf.inputs[0])
    L.new(col, mf.inputs[1])
    mf.inputs[2].default_value = (0.55, 0.6, 0.55, 1)
    ao = N.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = ao_dist
    ao.samples = 8
    L.new(mf.outputs[0], ao.inputs["Color"])
    aor = N.new("ShaderNodeMapRange")
    aor.inputs[3].default_value = 1.0 - ao_strength
    aor.inputs[4].default_value = 1.0
    L.new(ao.outputs["AO"], aor.inputs[0])
    mao = N.new("ShaderNodeMixRGB")
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


def add_color_attr(ob, name, cols):
    me = ob.data
    a = me.attributes.new(name, "FLOAT_COLOR", "POINT")
    a.data.foreach_set("color", np.ascontiguousarray(cols, dtype=np.float32).ravel())


# ---------------------------------------------------------------------------
# Image IO


def load_exr(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1].astype(np.float64)   # top row first, premultiplied linear


def downsample_pm(pm, ss):
    h, w, _ = pm.shape
    return pm.reshape(h // ss, ss, w // ss, ss, 4).mean(axis=(1, 3))


def gauss_blur(img, sigma):
    """Separable gaussian on an (h, w, c) array."""
    if sigma <= 0:
        return img
    r = int(math.ceil(sigma * 3))
    x = np.arange(-r, r + 1)
    k = np.exp(-0.5 * (x / sigma) ** 2)
    k /= k.sum()
    out = img
    for ax in (0, 1):
        pad = [(0, 0)] * img.ndim
        pad[ax] = (r, r)
        p = np.pad(out, pad, mode="constant")
        acc = np.zeros_like(out)
        for i, kv in enumerate(k):
            sl = [slice(None)] * img.ndim
            sl[ax] = slice(i, i + out.shape[ax])
            acc += kv * p[tuple(sl)]
        out = acc
    return out


def add_glow(pm, amount, sigmas=(2.0, 6.0, 14.0), thresh=1.0, extra=None, taper=0):
    """Bloom: the HDR excess over `thresh` (and an optional extra light
    image, premultiplied linear rgb) blurred and added as a light that also
    raises alpha, so it shows over any background."""
    rgb = pm[..., :3]
    ex = np.maximum(rgb - thresh, 0.0)
    if extra is not None:
        ex = ex + extra
    g = np.zeros_like(ex)
    wts = (0.5, 0.3, 0.2)
    for s, wv in zip(sigmas, wts):
        g += wv * gauss_blur(ex, s)
    g *= amount
    if taper > 0:
        # fade the halo out towards the image border so it never ends in a hard edge
        h, w = g.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        dist = np.minimum.reduce([xx, w - 1 - xx, yy, h - 1 - yy]).astype(float)
        g *= np.clip(dist / taper, 0, 1)[..., None] ** 1.5
    out = pm.copy()
    out[..., :3] = np.minimum(rgb, thresh) + g
    # alpha of the glow: its brightness (so that "over" ~ adds it)
    ga = np.clip(g.max(axis=2), 0, 1)
    out[..., 3] = out[..., 3] + (1 - out[..., 3]) * ga
    return out


def bleed(rgba, iters=6):
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


def pm_to_straight_srgb(pm):
    a = np.clip(pm[..., 3:4], 0, 1)
    rgb = np.where(a > 1e-6, pm[..., :3] / np.maximum(a, 1e-6), 0.0)
    return np.concatenate([lin_to_srgb(rgb), a], axis=2)


def write_png(path, arr):
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


def render_pm():
    """Render the current scene to EXR; return premultiplied linear RGBA at 1x."""
    sc = bpy.context.scene
    tmp = os.path.join(TMP, "r.exr")
    sc.render.filepath = tmp
    t0 = time.time()
    bpy.ops.render.render(write_still=True)
    big = load_exr(tmp)
    print("  render %dx%d in %.1f s" % (sc.render.resolution_x, sc.render.resolution_y, time.time() - t0))
    return downsample_pm(big, ARGS.ss)


# ---------------------------------------------------------------------------
# Prop camera


def scene_objs():
    return [o for o in bpy.context.scene.objects if o.type in ("MESH", "CURVE")
            and not o.get("helper")]


def world_points(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        n = len(me.vertices)
        if n == 0:
            ev.to_mesh_clear()
            continue
        a = np.empty(n * 3, dtype=np.float32)
        me.vertices.foreach_get("co", a)
        a = a.reshape(-1, 3).astype(np.float64)
        Mw = np.array(ev.matrix_world)
        pts.append(a @ Mw[:3, :3].T + Mw[:3, 3])
        ev.to_mesh_clear()
    return np.concatenate(pts)


def project(P, f, cx, cy):
    """Pinhole at (0, 0, CAM_H) looking +Y: pixel x right, y down."""
    return cx + f * P[:, 0] / P[:, 1], cy - f * (P[:, 2] - CAM_H) / P[:, 1]


def make_cam(f, cx, cy, W, H, ss):
    cd = bpy.data.cameras.new("cam")
    cd.type = "PERSP"
    cd.sensor_fit = "AUTO"
    cd.sensor_width = 36.0
    mx = max(W, H)
    cd.lens = 36.0 * f / mx
    cd.shift_x = (W / 2 - cx) / mx
    cd.shift_y = (cy - H / 2) / mx
    cd.clip_start = 1.0
    cd.clip_end = 20000
    ob = bpy.data.objects.new("cam", cd)
    ob["helper"] = True
    link(ob)
    ob.location = (0, 0, CAM_H)
    ob.rotation_euler = (math.pi / 2, 0, 0)
    sc = bpy.context.scene
    sc.camera = ob
    sc.render.resolution_x = W * ss
    sc.render.resolution_y = H * ss
    return ob


def check_cam(cam, f, cx, cy, W, H):
    from bpy_extras.object_utils import world_to_camera_view
    sc = bpy.context.scene
    bpy.context.view_layer.update()
    for p in [(0, PROP_Y, 0), (3, PROP_Y + 5, 4)]:
        v = world_to_camera_view(sc, cam, Vector(p))
        bx, by = v.x * W, (1 - v.y) * H
        px, py = project(np.array([p], float), f, cx, cy)
        if abs(bx - px[0]) > 0.05 or abs(by - py[0]) > 0.05:
            raise RuntimeError("camera mismatch %s: blender %.2f,%.2f ours %.2f,%.2f"
                               % (p, bx, by, px[0], py[0]))


def frame_prop(P, target_h, max_w=512, max_h=384, margin=2, glow_px=0, min_w=0):
    """Choose focal (px), image size and principal point so the projected
    points fill the image. Returns f, cx, cy, W, H."""
    ux = P[:, 0] / P[:, 1]
    uy = -(P[:, 2] - CAM_H) / P[:, 1]
    wn, hn = ux.max() - ux.min(), uy.max() - uy.min()
    m = margin + glow_px
    f = (target_h - 2 * m) / hn
    if f * wn > max_w - 2 * m:
        f = (max_w - 2 * m) / wn
    if f * hn > max_h - 2 * m:
        f = (max_h - 2 * m) / hn
    W = int(math.ceil(f * wn)) + 2 * m
    H = int(math.ceil(f * hn)) + 2 * m
    W = max(W, min_w)
    W += W % 2
    H += H % 2
    W, H = min(W, max_w), min(H, max_h)
    cx = W / 2 - f * (ux.max() + ux.min()) / 2
    cy = H / 2 - f * (uy.max() + uy.min()) / 2
    return f, cx, cy, W, H


def clear_helpers():
    for o in list(bpy.context.scene.objects):
        if o.get("keep"):
            continue
        if o.get("helper") or o.type in ("CAMERA", "LIGHT"):
            bpy.data.objects.remove(o)


def add_bounce_ground(color):
    bpy.ops.mesh.primitive_plane_add(size=3000, location=(0, PROP_Y, 0))
    g = bpy.context.active_object
    g.name = "bounce_ground"
    g["helper"] = True
    g.data.materials.append(M(color, rough=0.95, spec=0.1))
    g.visible_camera = False
    g.visible_shadow = False
    return g


def render_prop(name, spec):
    """Render the current scene as prop `name`. spec: dict with target_h,
    light, shadow, glow ... Returns meta."""
    ss = ARGS.ss
    light = spec.get("light", "day")
    objs = scene_objs()
    P = world_points([o for o in objs if not o.get("noframe")])
    if spec.get("clip_z0"):
        P = P[P[:, 2] >= -0.05]
    Pf = P
    if spec.get("frame_pts"):
        Pf = np.vstack([P, np.asarray(spec["frame_pts"], float) + [0, PROP_Y, 0]])
    glow_px = spec.get("glow_px", 0)
    f, cx, cy, W, H = frame_prop(Pf, spec["h"], glow_px=glow_px, max_w=spec.get("max_w", 512))
    ppm = f / PROP_Y
    lo, hi = P.min(axis=0), P.max(axis=0)
    # colour
    clear_helpers()
    cam = make_cam(f, cx, cy, W, H, ss)
    check_cam(cam, f, cx, cy, W, H)
    setup_lights(light)
    for extra in spec.get("lights", []):
        add_point(*extra)
    add_bounce_ground(spec.get("ground", LIGHTS[light]["ground"]))
    pm = render_pm()
    if LIGHTS[light]["glow"] > 0 or spec.get("glow", 0) > 0:
        extra = None
        if spec.get("pool"):
            extra = light_pool(spec["pool"], f, cx, cy, W, H)
        pm = add_glow(pm, spec.get("glow", LIGHTS[light]["glow"]),
                      sigmas=spec.get("glow_sigmas", (1.2, 3.5, 8.0)), extra=extra, taper=12)
    out = pm_to_straight_srgb(pm)
    write_png(os.path.join(OUT, name + ".png"), to_u8(bleed(out)))
    bx, by = project(np.array([[0, PROP_Y, 0.0]]), f, cx, cy)
    meta = dict(stage=spec["stage"], file=name + ".png", size=[W, H], ppm=round(ppm, 4),
                anchor=[round(float(bx[0]), 2), round(float(by[0]), 2)],
                width_m=round(float(hi[0] - lo[0]), 2), height_m=round(float(hi[2]), 2),
                depth_m=round(float(hi[1] - lo[1]), 2), light=light)
    if spec.get("yaw"):
        meta["yaw_deg"] = spec["yaw"]
    if spec.get("note"):
        meta["note"] = spec["note"]
    print("  %s: %dx%d ppm %.2f anchor %.1f,%.1f" % (name, W, H, ppm, bx[0], by[0]))
    # shadow
    if spec.get("shadow"):
        meta.update(render_shadow(name, P, f, light))
    return meta


def render_shadow(name, P, f, light):
    """Shadow on the ground (Z = 0) with the same camera focal; the image
    is framed on the shadow's footprint."""
    ss = ARGS.ss
    L = LIGHTS[light]
    ld = np.array(sun_vec(L["sun_from"], L["elev"]))
    t = P[:, 2] / -ld[2]
    S = P + np.outer(t, ld)
    S[:, 2] = 0.0
    S = np.vstack([S, P[P[:, 2] < 0.3]])
    S[:, 2] = 0.0
    S = S[S[:, 1] > 5]
    ux = S[:, 0] / S[:, 1]
    uy = -(S[:, 2] - CAM_H) / S[:, 1]
    m = 3
    W = int(math.ceil(f * (ux.max() - ux.min()))) + 2 * m
    H = int(math.ceil(f * (uy.max() - uy.min()))) + 2 * m
    W += W % 2
    H += H % 2
    H = max(H, 8)
    W, H = min(W, 512), min(H, 384)
    cx = W / 2 - f * (ux.max() + ux.min()) / 2
    cy = H / 2 - f * (uy.max() + uy.min()) / 2
    clear_helpers()
    objs = scene_objs()
    make_cam(f, cx, cy, W, H, ss)
    add_sun(sun_vec(L["sun_from"], L["elev"]), 3.0, (1, 1, 1), angle_deg=2.5)
    set_world("#000000", "#000000", 0.0)
    for o in objs:
        o.visible_camera = False
    bpy.ops.mesh.primitive_plane_add(size=4000, location=(0, PROP_Y, 0))
    catcher = bpy.context.active_object
    catcher.name = "catcher"
    catcher["helper"] = True
    catcher.is_shadow_catcher = True
    catcher.data.materials.append(M("#808080", rough=1.0, spec=0.0))
    pm = render_pm()
    bpy.data.objects.remove(catcher)
    for o in objs:
        o.visible_camera = True
    write_png(os.path.join(OUT, name + "_shadow.png"), to_u8(np.clip(pm[..., 3], 0, 1)))
    bx, by = project(np.array([[0, PROP_Y, 0.0]]), f, cx, cy)
    return dict(shadow_file=name + "_shadow.png", shadow_size=[W, H],
                shadow_anchor=[round(float(bx[0]), 2), round(float(by[0]), 2)])


def light_pool(pool, f, cx, cy, W, H):
    """A warm pool of light on the ground (premultiplied linear rgb image)
    around a ground point: pool = (x, y, radius_m, colour hex, strength)."""
    x0, y0, R, col, strength = pool
    yy, xx = np.mgrid[0:H, 0:W] + 0.5
    # invert the projection for ground points: Y = f*CAM_H/(py - cy), X = (px - cx) * Y / f
    dy = yy - cy
    with np.errstate(divide="ignore", invalid="ignore"):
        Yg = np.where(dy > 0.5, f * CAM_H / dy, np.inf)
        Xg = (xx - cx) * Yg / f
    d2 = ((Xg - x0) ** 2 + (Yg - (PROP_Y + y0)) ** 2) / R ** 2
    I = np.where(np.isfinite(d2), np.exp(-2.5 * d2), 0.0) * strength
    c = np.array(hexcol(col)[:3])
    return I[..., None] * c[None, None, :]


# ---------------------------------------------------------------------------
# PROPS: common


def build_cone(rng):
    org = M("#ff5a10", rough=0.45, spec=0.5)
    wht = M("#f4f4f0", rough=0.3, spec=0.6)
    blk = M("#1c1c1c", rough=0.8)
    box("base", (0, 0, 0.02), (0.42, 0.42, 0.04), blk, bev=0.02)
    # cone in bands
    zs = [0.04, 0.2, 0.3, 0.42, 0.5, 0.72]
    r = lambda z: 0.15 - 0.13 * (z - 0.04) / 0.68
    for i in range(len(zs) - 1):
        cyl("c%d" % i, (0, 0, zs[i]), (0, 0, zs[i + 1]), r(zs[i]), wht if i == 2 else org,
            r1=r(zs[i + 1]), n=24)
    cyl("cap", (0, 0, 0.72), (0, 0, 0.74), r(0.72), org, r1=0.01, n=24)


def arch(rng, finish=False):
    """Checkpoint / finish arch: two pylons 14 m apart, banner beam 6 m up."""
    red = M("#e8202a", rough=0.35, spec=0.5, clearcoat=0.3)
    wht = M("#f2f2ee", rough=0.35, spec=0.5)
    yel = M("#ffc814", rough=0.35, spec=0.5, clearcoat=0.3)
    blue = M("#1060e0", rough=0.4, spec=0.4)
    dark = M("#26282c", rough=0.6, spec=0.3)
    chrome = M("#c8ccd4", rough=0.2, metallic=1.0)
    for s in (-1, 1):
        x = s * 7.0
        # foot
        box("foot%d" % s, (x, 0, 0.25), (1.8, 1.8, 0.5), dark, bev=0.08)
        # pylon in bands (red / white or yellow), slightly tapered
        nb = 6
        for i in range(nb):
            z0, z1 = 0.5 + i * 7.3 / nb, 0.5 + (i + 1) * 7.3 / nb
            w0 = 1.25 - 0.25 * i / nb
            w1 = 1.25 - 0.25 * (i + 1) / nb
            m = (red if i % 2 == 0 else wht) if not finish else (yel if i % 2 == 0 else dark)
            cyl("py%d_%d" % (s, i), (x, 0, z0), (x, 0, z1), w0 / 2, m, r1=w1 / 2, n=8, smooth=False)
        # top light
        sphere("lt%d" % s, (x, 0, 8.05), 0.35, yel if not finish else red)
    # banner beam: a frame and a panel from 6 to 7.8 m
    z0, z1 = 6.0, 7.8
    box("beam_top", (0, 0, z1 + 0.12), (14.0, 0.7, 0.24), chrome, bev=0.05)
    box("beam_bot", (0, 0, z0 - 0.12), (14.0, 0.7, 0.24), chrome, bev=0.05)
    if finish:
        pan = M_checker("#101010", "#f4f4f4", 0.6)
        box("panel", (0, 0, (z0 + z1) / 2), (13.4, 0.4, z1 - z0), pan)
    else:
        box("panel", (0, 0, (z0 + z1) / 2), (13.4, 0.4, z1 - z0), blue)
        # a yellow border so the blank panel reads as a banner
        for zz in (z0 + 0.1, z1 - 0.1):
            box("trim%.1f" % zz, (0, -0.22, zz), (13.2, 0.06, 0.12), yel)
    # small flags / lamps along the top
    for i in range(7):
        x = -6 + 2 * i
        sphere("bulb%d" % i, (x, -0.1, z1 + 0.4), 0.18, yel if i % 2 == 0 else red, subdiv=2)


def build_checkpoint(rng):
    arch(rng, False)


def build_finish(rng):
    arch(rng, True)


def build_sign_curve(rng):
    """Yellow board with black chevrons pointing left (towards -X)."""
    yel = M("#ffcc00", rough=0.35, spec=0.5)
    blk = M("#141414", rough=0.5, spec=0.3)
    post = M("#9a9ea4", rough=0.4, metallic=0.8)
    Wb, Hb, zb = 1.6, 1.2, 1.0
    for x in (-0.5, 0.5):
        box("post%.1f" % x, (x, 0.08, zb / 2 + 0.3), (0.08, 0.08, zb + 0.6), post)
    box("board", (0, 0, zb + Hb / 2), (Wb, 0.06, Hb), yel, bev=0.02)
    box("rim", (0, 0.001, zb + Hb / 2), (Wb + 0.08, 0.05, Hb + 0.08), blk)
    # two chevrons "<<"
    for k, xc in enumerate((-0.32, 0.28)):
        t, a = 0.2, 0.42    # stroke, half height
        pts = [(xc - 0.3, 0), (xc + 0.02, a), (xc + 0.02 + t, a), (xc - 0.3 + t, 0),
               (xc + 0.02 + t, -a), (xc + 0.02, -a)]
        pts = [(x, z + zb + Hb / 2) for x, z in pts]
        prism("chev%d" % k, pts, -0.045, -0.03, blk)


def build_barrier(rng):
    """2 m concrete jersey barrier along the road (Y)."""
    conc = M_noise("#86827a", "#a39f95", scale=3.0, c3="#6e6a62", c3_amt=0.25, c3_scale=1.5,
                   rough=0.85, bump=0.2)
    prof = [(-0.30, 0.0), (-0.30, 0.08), (-0.22, 0.33), (-0.08, 0.81), (0.08, 0.81),
            (0.22, 0.33), (0.30, 0.08), (0.30, 0.0)]
    # extrude along Y: build as prism in (x, z) then extrude y
    ob = prism("jersey", prof, -1.0, 1.0, conc)
    bevel(ob, 0.015, 1)
    # a stripe of reflective yellow paint near the top
    ob2 = prism("stripe", [(-0.105, 0.70), (-0.075, 0.79), (-0.07, 0.79), (-0.10, 0.70)], -1.0, 1.0,
                M("#f0c020", rough=0.4))
    ob2.location.x -= 0.004


# ---------------------------------------------------------------------------
# PROPS: city


def concrete(light="#c4bfb4", dark="#a39d92", stain="#7e786e", amt=0.3, name="concrete"):
    return M_noise(dark, light, scale=1.6, c3=stain, c3_amt=amt, c3_scale=0.8, rough=0.85,
                   bump=0.15, name=name)


def rusty_green():
    return M_noise("#2f6e3e", "#3f8a4c", scale=2.5, c3="#8a4a26", c3_amt=0.42, c3_scale=1.4,
                   rough=0.55, spec=0.45, bump=0.25, name="girder")


def build_overpass(rng):
    """Concrete highway bridge over the road. Local y = 0 is the face towards
    the camera; the deck runs 12 m deep. Deck underside 6 m up, span 26 m."""
    conc = concrete()
    conc_d = concrete("#9d978c", "#857f75", "#6c665c", name="conc_dark")
    grn = rusty_green()
    grn_d = M_noise("#1e4a2a", "#2a5e36", scale=2.0, c3="#5e3218", c3_amt=0.25, rough=0.6, bump=0.2)
    dark = M("#2a2c2e", rough=0.8)
    X0, X1 = -17.0, 17.0
    D = 12.0
    zu = 6.0          # underside of the girders
    zg = 7.7          # top of the girder fascia / underside of the slab
    # deck slab + parapet
    box2("slab", (X0, 0.4, zg), (X1, D, zg + 0.55), conc_d)
    box2("parapet", (X0, 0.0, zg + 0.1), (X1, 0.45, zg + 1.35), conc, bev=0.04)
    box2("parapet_cap", (X0, -0.05, zg + 1.35), (X1, 0.5, zg + 1.5), conc, bev=0.03)
    box2("parapet_back", (X0, D - 0.45, zg + 0.1), (X1, D, zg + 1.35), conc)
    # front steel girder: web with top/bottom flanges and stiffener ribs
    box2("web", (X0, 0.35, zu), (X1, 0.55, zg), grn)
    box2("flange_b", (X0, 0.05, zu), (X1, 0.85, zu + 0.18), grn)
    box2("flange_t", (X0, 0.05, zg - 0.18), (X1, 0.85, zg), grn)
    nrib = 20
    for i in range(nrib + 1):
        x = X0 + 0.4 + (X1 - X0 - 0.8) * i / nrib
        box2("rib%d" % i, (x - 0.08, 0.1, zu + 0.18), (x + 0.08, 0.35, zg - 0.18), grn)
    # rivet line / bolt heads along the flanges
    # inner girders under the deck (seen from below), darker
    for y in (3.0, 5.5, 8.0, 10.5):
        box2("ig%.0f" % y, (X0, y - 0.15, zu + 0.1), (X1, y + 0.15, zg), grn_d)
    # cross bracing between the front girders (dark)
    # pier caps and pillars, outside the road (|x| > 9)
    for s in (-1, 1):
        xc = s * 12.5
        box2("cap%d" % s, (xc - 1.6, -0.2, zu - 1.0), (xc + 1.6, D + 0.2, zu), conc, bev=0.06)
        for y in (1.2, D - 1.2):
            cyl("col%d_%.0f" % (s, y), (xc, y, 0), (xc, y, zu - 1.0), 0.75, conc, n=24)
            box2("colfoot%d_%.0f" % (s, y), (xc - 1.0, y - 1.0, 0), (xc + 1.0, y + 1.0, 0.35), conc_d)
        # a sloped embankment wall behind each pier (abutment)
    for s in (-1, 1):
        xa = s * 16.2
        box2("abut%d" % s, (min(xa, xa + s * 1.6), 0.6, 0), (max(xa, xa + s * 1.6), D, zg), conc_d)
    # a dark strip line on the parapet (drain)
    box2("drip", (X0, -0.06, zg + 0.08), (X1, 0.0, zg + 0.16), dark)


def build_lamp(rng, night=False):
    """Tall highway lamp: pole at the base, curved arm reaching -X over the road."""
    metal = M("#8e949c", rough=0.35, metallic=0.85)
    dark = M("#3a3e44", rough=0.5, metallic=0.6)
    H = 11.0
    cyl("pole", (0, 0, 0.4), (0, 0, H), 0.17, metal, r1=0.09, n=16)
    box("base", (0, 0, 0.2), (0.6, 0.6, 0.4), M("#9c978c", rough=0.8), bev=0.03)
    arm = smooth_path([[0, 0, H - 0.6], [0, 0, H + 0.3], [-0.9, 0, H + 1.0], [-2.6, 0, H + 1.15],
                       [-3.4, 0, H + 1.05]], 20)
    tube("arm", arm, np.linspace(0.085, 0.06, len(arm)), metal, bevel_res=3)
    # cobra-head luminaire
    head = uvsphere("head", (-3.9, 0, H + 0.98), 0.62, dark, scale=(1.3, 0.6, 0.3))
    if night:
        lens = M_emit("#ffd9a0", 30.0, "lamp_lens")
    else:
        lens = M("#f4f2e8", rough=0.2, spec=0.6, emit="#fff4d8", emit_str=0.6)
    uvsphere("lens", (-3.95, 0, H + 0.84), 0.55, lens, scale=(1.15, 0.5, 0.14))


def build_lamp_day(rng):
    build_lamp(rng, False)


def tower_common_top(w, d, h, mat, rng):
    # roof parapet + a mechanical box + an antenna
    box2("roofp", (-w / 2 - 0.3, -d / 2 - 0.3, h), (w / 2 + 0.3, d / 2 + 0.3, h + 1.0), mat)


def build_tower_brick(rng):
    w, d, h = 16.0, 14.0, 38.0
    brick = M_grid("#9a4a32", "#253040", cell=(2.6, 3.4), frac=(0.5, 0.5), wall2="#b35a3c",
                   win_var=0.35, name="brick")
    trim = M("#e8dcc4", rough=0.7)
    dark = M("#3a3530", rough=0.8)
    ob = box2("body", (-w / 2, -d / 2, 0), (w / 2, d / 2, h), brick)
    # base storefront
    box2("base", (-w / 2 - 0.2, -d / 2 - 0.2, 0), (w / 2 + 0.2, d / 2 + 0.2, 4.2), trim)
    box2("shop", (-w / 2 - 0.25, -d / 2 - 0.25, 0.4), (w / 2 + 0.25, d / 2 + 0.25, 3.4),
         M("#1c2838", rough=0.1, spec=0.8))
    # cornices
    for z in (4.2, h - 0.2):
        box2("corn%.0f" % z, (-w / 2 - 0.5, -d / 2 - 0.5, z), (w / 2 + 0.5, d / 2 + 0.5, z + 0.7), trim)
    box2("roof", (-w / 2 + 0.2, -d / 2 + 0.2, h), (w / 2 - 0.2, d / 2 - 0.2, h + 0.5), dark)
    # water tower on the roof
    wood = M_noise("#6a4a30", "#8a6440", scale=4, rough=0.8)
    cyl("tank", (2.5, 1.5, h + 3.0), (2.5, 1.5, h + 6.0), 1.8, wood, n=20)
    cyl("tankr", (2.5, 1.5, h + 6.0), (2.5, 1.5, h + 7.2), 1.9, dark, r1=0.15, n=20)
    for a in range(4):
        ang = a * math.pi / 2 + math.pi / 4
        x, y = 2.5 + 1.3 * math.cos(ang), 1.5 + 1.3 * math.sin(ang)
        cyl("leg%d" % a, (x, y, h), (x, y, h + 3.0), 0.12, dark, n=6)
    box2("hvac", (-5, -3, h + 0.5), (-1.5, 1.0, h + 2.3), M("#8a8c8e", rough=0.5))


def build_tower_glass(rng):
    w, d, h = 18.0, 18.0, 72.0
    glass = M_grid("#8fa6b8", "#3f7fc0", cell=(3.0, 3.6), frac=(0.86, 0.78), wall2="#a0b4c4",
                   win_var=0.12, rough_win=0.05, spec_win=0.9, name="glass")
    steel = M("#b8c2cc", rough=0.3, metallic=0.8)
    box2("body", (-w / 2, -d / 2, 0), (w / 2, d / 2, h * 0.78), glass)
    box2("mid", (-w / 2 + 2.5, -d / 2 + 2.5, h * 0.78), (w / 2 - 2.5, d / 2 - 2.5, h * 0.92), glass)
    box2("top", (-w / 2 + 5, -d / 2 + 5, h * 0.92), (w / 2 - 5, d / 2 - 5, h), glass)
    for z, inset in ((h * 0.78, 0), (h * 0.92, 2.5), (h, 5)):
        box2("band%.0f" % z, (-w / 2 + inset - 0.3, -d / 2 + inset - 0.3, z - 0.5),
             (w / 2 - inset + 0.3, d / 2 - inset + 0.3, z + 0.3), steel)
    box2("lobby", (-w / 2 - 0.4, -d / 2 - 0.4, 0), (w / 2 + 0.4, d / 2 + 0.4, 5.0), steel)
    box2("lobbyg", (-w / 2 - 0.5, -d / 2 - 0.5, 0.3), (w / 2 + 0.5, d / 2 + 0.5, 4.4),
         M("#23344a", rough=0.05, spec=0.9))
    cyl("spire", (0, 0, h), (0, 0, h + 12), 0.35, steel, r1=0.08, n=10)
    sphere("tip", (0, 0, h + 12.1), 0.35, M("#ff3020", emit="#ff3020", emit_str=2.0))


def build_tree_round(rng):
    """Broad round street tree (golf's oak, smaller and brighter)."""
    H, cz, crx, crz, cbase = 8.5, 5.4, 3.4, 2.7, 2.6
    bark = M_noise("#4a3a2c", "#8a7662", scale=6, bump=0.5, name="bark")
    leaf = mat_foliage("leaf", dark="#2a5e1e", mid="#4a9a2c", light="#86c43c",
                       top_tint="#a8d450", shadow_tint="#183a34", translucency=0.22,
                       clump_var=0.6, ao_strength=0.65)
    clumps = []
    gc = np.array([0.0, 0.0, cz])
    gr = np.array([crx, crx, crz])
    n = 34
    for i in range(n):
        zz = 1 - 2 * (i + 0.5) / n
        if zz < -0.6:
            continue
        az = i * 2.39996 + rng.normal(0, 0.25)
        rxy = math.sqrt(max(0, 1 - zz * zz))
        dd = np.array([rxy * math.cos(az), rxy * math.sin(az), zz]) + rng.normal(0, 0.08, 3)
        dd /= np.linalg.norm(dd)
        c = gc + dd * gr * rng.uniform(0.74, 0.84)
        r = rng.uniform(0.9, 1.6) if i % 3 else rng.uniform(1.4, 1.8)
        clumps.append(Clump(c, (r, r, r * 0.85)))
    clumps.append(Clump(gc, gr * 0.78))
    tp = smooth_path([[0, 0, 0], [0.06, 0.03, 1.0], [0.0, 0.08, 2.0], [0.08, 0.0, cbase + 0.8]], 12)
    tube("trunk", tp, np.linspace(0.42, 0.26, 12), bark, bevel_res=5)
    top = tp[-1]
    low = sorted(range(len(clumps) - 1), key=lambda i: clumps[i].c[2])[:10]
    for j, i in enumerate(low[::2]):
        c = clumps[i].c
        mid = top + (c - top) * 0.45 + np.array([0, 0, 0.4]) + rng.normal(0, 0.2, 3)
        pts = smooth_path([top - [0, 0, 0.5], mid, c * 0.8 + top * 0.2], 10)
        tube("limb%d" % j, pts, np.linspace(0.2, 0.07, len(pts)), bark)
    v, f, a, nn = canopy_cards(rng, clumps, density=55, leaf_len=0.34, leaf_aspect=0.62,
                               gcenter=gc, gradii=gr, disp_amp=0.08, disp_freq=1.5,
                               depth=0.15, hide=0.9, w_card=0.15, w_clump=0.55, w_global=0.3)
    mesh_from_arrays("leaves", v, f, a, nn, leaf)
    filler("fill", clumps, leaf, None, gc, gr)


def build_billboard(rng):
    steel = M("#8a9098", rough=0.4, metallic=0.8)
    dark = M("#34383e", rough=0.6, metallic=0.5)
    Wb, Hb, z0 = 10.0, 4.2, 5.5
    for x in (-2.8, 2.8):
        cyl("post%.0f" % x, (x, 0.4, 0), (x, 0.4, z0 + 0.5), 0.32, steel, n=16)
    # catwalk
    box2("walk", (-Wb / 2, -0.9, z0 - 0.35), (Wb / 2, 0.1, z0 - 0.25), dark)
    for i in range(21):
        x = -Wb / 2 + Wb * i / 20
        box2("rail%d" % i, (x - 0.02, -0.88, z0 - 0.25), (x + 0.02, -0.84, z0 + 0.5), dark)
    box2("railtop", (-Wb / 2, -0.9, z0 + 0.45), (Wb / 2, -0.82, z0 + 0.52), dark)
    # frame and panel: a blank two-tone panel (no text)
    box2("frame", (-Wb / 2 - 0.2, 0.0, z0 - 0.2), (Wb / 2 + 0.2, 0.3, z0 + Hb + 0.2), M("#f2f0ea", rough=0.5))
    pan = M_bands(["#ff3c28", "#ff3c28", "#ffb800", "#2a7de0"], axis="Z", size=Hb / 4 * 1.0,
                  offset=-z0 / Hb, rough=0.45)
    box2("panel", (-Wb / 2, -0.05, z0), (Wb / 2, 0.1, z0 + Hb), pan)
    # spot lights on arms at the bottom
    for x in (-3.5, 0, 3.5):
        box2("larm%.0f" % x, (x - 0.04, -1.5, z0 - 0.3), (x + 0.04, -0.9, z0 - 0.22), dark)
        uvsphere("spot%.0f" % x, (x, -1.55, z0 - 0.15), 0.2, dark, scale=(1, 1, 0.6))


def truss(name, p0, p1, size, mat, n=None, rod=0.06):
    """A square box truss between two points (along X or Z)."""
    p0, p1 = np.array(p0, float), np.array(p1, float)
    d = p1 - p0
    Lh = np.linalg.norm(d)
    ax = d / Lh
    up = np.array([0, 0, 1.0]) if abs(ax[2]) < 0.9 else np.array([1.0, 0, 0])
    s1 = np.cross(ax, up); s1 /= np.linalg.norm(s1)
    s2 = np.cross(ax, s1)
    n = n or max(2, int(Lh / size))
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    h = size / 2
    for k, (a, b) in enumerate(corners):
        off = (a * s1 + b * s2) * h
        cyl("%s_ch%d" % (name, k), p0 + off, p1 + off, rod * 1.4, mat, n=6)
    for i in range(n):
        q0 = p0 + ax * Lh * i / n
        q1 = p0 + ax * Lh * (i + 1) / n
        for k in range(4):
            a0, b0 = corners[k]
            a1, b1 = corners[(k + 1) % 4]
            o0 = (a0 * s1 + b0 * s2) * h
            o1 = (a1 * s1 + b1 * s2) * h
            cyl("%s_d%d_%d" % (name, i, k), q0 + o0, q1 + o1, rod, mat, n=5)


def build_sign_gantry(rng):
    """Overhead sign on a truss spanning 18 m, two blank green panels."""
    galv = M("#a8adb2", rough=0.45, metallic=0.8)
    green = M("#0e7a3e", rough=0.4, spec=0.4)
    white = M("#f4f4f0", rough=0.4)
    Hs = 7.0
    for s in (-1, 1):
        x = s * 9.0
        truss("leg%d" % s, (x, 0.4, 0.3), (x, 0.4, Hs + 1.2), 0.9, galv, rod=0.05)
        box2("foot%d" % s, (x - 0.8, -0.4, 0), (x + 0.8, 1.2, 0.4), M("#b0aba0", rough=0.85))
    truss("beam", (-9.4, 0.4, Hs + 0.6), (9.4, 0.4, Hs + 0.6), 1.0, galv, rod=0.05)
    for xc, wv in ((-3.6, 6.4), (3.6, 6.4)):
        box2("border%.0f" % xc, (xc - wv / 2 - 0.1, -0.15, Hs - 2.1), (xc + wv / 2 + 0.1, -0.05, Hs + 1.4), white)
        box2("sign%.0f" % xc, (xc - wv / 2, -0.2, Hs - 2.0), (xc + wv / 2, -0.12, Hs + 1.3), green)
        # a blank white arrow bar at the bottom of the right panel (no text)
    box2("arrow", (3.4, -0.23, Hs - 1.6), (3.8, -0.2, Hs - 0.6), white)
    prism("arrowhead", [(3.1, Hs - 1.6), (4.1, Hs - 1.6), (3.6, Hs - 2.0)], -0.23, -0.2, white)
    for xc in (-3.6, 3.6):
        for s in (-1, 1):
            box2("hang%.0f%d" % (xc, s), (xc + s * 2.2 - 0.05, -0.05, Hs + 1.3), (xc + s * 2.2 + 0.05, 0.4, Hs + 1.1), galv)


# ---------------------------------------------------------------------------
# PROPS: coast


def mat_vcol_leaf(name, attr="col", rough=0.6, spec=0.3, translucency=0.3):
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    at = N.new("ShaderNodeAttribute"); at.attribute_name = attr
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular"].default_value = spec
    L.new(at.outputs["Color"], b.inputs["Base Color"])
    tr = N.new("ShaderNodeBsdfTranslucent")
    L.new(at.outputs["Color"], tr.inputs["Color"])
    ms = N.new("ShaderNodeMixShader")
    ms.inputs[0].default_value = translucency
    L.new(b.outputs[0], ms.inputs[1])
    L.new(tr.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    return mat


def palm(rng, H=9.5, lean=(1.1, 0.25), nf=22, frond_len=(3.6, 4.3)):
    """Golf's palm: ringed curved trunk, arching fronds of hanging leaflets."""
    trunk_m = M_noise("#6a5a48", "#a89478", scale=4.0, bump=0.8, strata=0.55, strata_col="#5a4a3a",
                      strata_scale=4.5, name="palm_trunk")
    frond_m = mat_vcol_leaf("palm_frond")
    nut_m = M("#6b4a22", rough=0.6)
    n = 24
    ts = np.linspace(0, 1, n)
    tp = np.stack([lean[0] * ts ** 1.8 - 0.1 * np.sin(ts * 3), lean[1] * ts ** 2, ts * H], axis=1)
    tube("palm_trunk", tp, 0.19 + 0.08 * (1 - ts) ** 3 + 0.12 * (1 - ts) ** 12, trunk_m, bevel_res=6)
    top = tp[-1]
    V, F, C, NRM = [], [], [], []
    off = 0
    green_a = srgb_to_lin([0.14, 0.38, 0.09])
    green_b = srgb_to_lin([0.34, 0.58, 0.12])
    tip_col = srgb_to_lin([0.56, 0.62, 0.20])
    dead = srgb_to_lin([0.60, 0.46, 0.24])
    for fi in range(nf + 5):
        is_dead = fi >= nf
        az = fi * 2.39996 + rng.normal(0, 0.12)
        if is_dead:
            el = math.radians(rng.uniform(-80, -62))
            Lf = rng.uniform(2.4, 2.9)
        else:
            age = fi / nf
            el = math.radians(62 - 80 * age + rng.normal(0, 6))
            Lf = rng.uniform(*frond_len) * (0.8 + 0.25 * min(1, age * 2))
        d = np.array([math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)])
        side = np.cross(d, [0, 0, 1.0])
        side /= np.linalg.norm(side)
        ns = 30
        s = np.linspace(0, 1, ns)
        droop = (0.85 if not is_dead else 0.1) * Lf * (0.6 + 0.4 * math.cos(el))
        spine = top + np.outer(s * Lf, d) + np.outer(droop * s ** 2.0, [0, 0, -1])
        tan = np.gradient(spine, axis=0)
        tan /= np.linalg.norm(tan, axis=1, keepdims=True)
        up = np.cross(side, tan)
        up /= np.linalg.norm(up, axis=1, keepdims=True)
        col = dead * rng.uniform(0.8, 1.05) if is_dead else green_a + (green_b - green_a) * rng.random()
        outward = np.array([d[0], d[1], 0.0])
        outward /= np.linalg.norm(outward) + 1e-9
        for j in range(1, ns - 1):
            if s[j] < 0.08:
                continue
            u = (s[j] - 0.06) / 0.94
            ll = (1.15 if not is_dead else 0.6) * math.sin(math.pi * min(1.0, u * 0.92 + 0.04)) ** 0.6 + 0.08
            for sd in (-1, 1):
                for rep in range(2):
                    base = spine[j] + tan[j] * rep * 0.07
                    hang = 0.75 if not is_dead else 0.25
                    dirl = sd * side * 0.75 + tan[j] * 0.35 - up[j] * hang * (0.55 + 0.3 * rep)
                    dirl += rng.normal(0, 0.1, 3)
                    dirl /= np.linalg.norm(dirl)
                    wl = 0.11
                    p0 = base
                    p1 = base + dirl * ll * 0.5 + np.array([0, 0, -0.05 * ll])
                    p2 = base + dirl * ll + np.array([0, 0, -0.3 * ll])
                    wv = tan[j]
                    vv = [p0 - wv * wl * 0.5, p0 + wv * wl * 0.5, p1 + wv * wl * 0.6, p1 - wv * wl * 0.6, p2]
                    nrm = up[j] * 0.5 + np.array([0, 0, 0.7]) + outward * 0.5
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
    ob = mesh_from_arrays("palm_fronds", np.array(V), np.array(F), normals=np.array(NRM), mat=frond_m)
    add_color_attr(ob, "col", np.array(C))
    uvsphere("crown", tuple(top + np.array([0, 0, -0.1])), 0.35, M("#5f5a2e", rough=0.8), scale=(1, 1, 1.3))
    for i in range(6):
        az = i * 1.1 + rng.normal(0, 0.2)
        uvsphere("nut%d" % i, tuple(top + np.array([0.28 * math.cos(az), 0.28 * math.sin(az),
                                                    -0.35 - 0.12 * (i % 2)])), 0.16, nut_m, u=12, v=8)


def build_palm(rng):
    palm(rng, 8.5, lean=(-1.2, 0.2))


def build_palm_tall(rng):
    palm(rng, 14.0, lean=(-2.4, 0.4), nf=20, frond_len=(3.4, 4.0))


def stack_rock(name, mat, rng, R, Hh, taper=0.3, nring=48, nz=24, amp=0.18, top_round=0.08):
    """Sea stack / crag: an irregular column, wider at the foot, cracked
    vertically, with a nearly flat top."""
    nzs = Noise3(rng, 1.6)
    nz3 = Noise3(rng, 5.0)
    V, F = [], []
    zs = np.linspace(0, Hh, nz)
    for z in zs:
        t = z / Hh
        for k in range(nring):
            ph = 2 * math.pi * k / nring
            d = np.array([[math.cos(ph), math.sin(ph), t * 1.3]])
            r = R * (1 - taper * t) * (1 + amp * nzs(d)[0] + 0.06 * nz3(d * np.array([[1, 1, 3]]))[0])
            # ledges: the radius steps in and out every metre or so
            r *= 1 + 0.035 * math.copysign(1, math.sin(z * 1.7 + 2.0 * nzs(d * 0.5)[0]))
            V.append((r * math.cos(ph), 0.8 * r * math.sin(ph), z))
    for j in range(nz - 1):
        for k in range(nring):
            a0 = j * nring + k
            a1 = j * nring + (k + 1) % nring
            F.append([a0, a1, a1 + nring, a0 + nring])
    # top cap: a slightly domed fan
    c = len(V)
    V.append((0, 0, Hh + R * top_round))
    top0 = (nz - 1) * nring
    for k in range(nring):
        F.append([top0 + k, top0 + (k + 1) % nring, c])
    ob = mesh(name, V, F, mat, smooth=True)
    ob.data.use_auto_smooth = True
    ob.data.auto_smooth_angle = math.radians(70)
    return ob


def build_rock_cliff(rng):
    """A sea-side cliff chunk ~15 m tall: layered grey-brown rock, grassy top."""
    rk = M_noise("#6a5c4a", "#b09c7c", scale=0.7, bump=0.8, bump_dist=0.25, strata=0.35,
                 strata_col="#4a4036", strata_scale=0.22, snow=1.0, snow_col="#5a8c30",
                 snow_lo=0.62, snow_hi=0.78, rough=0.9, name="cliff")
    stack_rock("cliff", rk, rng, 7.5, 15.0, taper=0.28, nz=48)
    grass = M_noise("#3e7a26", "#6aa83a", scale=2.0, bump=0.4, rough=0.8, name="grass")
    # grassy caps drooping over the edges (the top itself is not seen from the road)
    for (x, y, zt, r) in ((0, 0, 15.0, 5.6), (-7.5, -2.0, 8.0, 2.6), (6.8, -3.0, 3.6, 1.6)):
        rock("cap%.0f" % x, (x, y, zt - 0.35 * r / 5.6), 1.0, grass, rng, scale=(r, r * 0.8, 0.5 * max(1, r / 3)),
             amp=0.15, freq=2.5, flat_bottom=False)
    ob = stack_rock("cliff2", rk, rng, 3.8, 8.0, taper=0.35)
    ob.location = (-7.5, -2.0, 0)
    ob = stack_rock("cliff3", rk, rng, 2.6, 3.6, taper=0.4)
    ob.location = (6.8, -3.0, 0)
    rock("boulder", (-3.0, -6.0, 0), 1.0, rk, rng, scale=(1.6, 1.4, 1.1), amp=0.25, freq=2.0)


def build_lighthouse(rng):
    Ht = 17.0
    bands = M_bands(["#f4f2ec", "#d8262a"], axis="Z", size=2.6, rough=0.5, spec=0.4)
    white = M("#f0eee6", rough=0.6)
    red = M("#c82024", rough=0.4, spec=0.5)
    dark = M("#26292e", rough=0.5, metallic=0.6)
    glass = M("#fff2c0", rough=0.1, spec=0.8, emit="#ffe8a0", emit_str=3.0)
    rk = M_noise("#5e564c", "#948876", scale=1.5, bump=0.5, rough=0.9, name="lrock")
    rock("base", (0, 0, 0), 1.0, rk, rng, scale=(4.2, 4.2, 1.6), amp=0.2, freq=2.0)
    cyl("tower", (0, 0, 1.0), (0, 0, Ht), 1.9, bands, r1=1.25, n=32)
    cyl("gallery", (0, 0, Ht), (0, 0, Ht + 0.35), 1.85, dark, n=32)
    for i in range(20):
        a = 2 * math.pi * i / 20
        cyl("rail%d" % i, (1.75 * math.cos(a), 1.75 * math.sin(a), Ht + 0.35),
            (1.75 * math.cos(a), 1.75 * math.sin(a), Ht + 1.2), 0.03, dark, n=4)
    cyl("railtop", (0, 0, Ht + 1.15), (0, 0, Ht + 1.25), 1.78, dark, n=32, caps=False)
    cyl("lantern", (0, 0, Ht + 0.35), (0, 0, Ht + 2.4), 1.05, glass, n=16)
    for i in range(8):
        a = 2 * math.pi * i / 8
        cyl("mull%d" % i, (1.07 * math.cos(a), 1.07 * math.sin(a), Ht + 0.35),
            (1.07 * math.cos(a), 1.07 * math.sin(a), Ht + 2.4), 0.05, dark, n=4)
    cyl("roof", (0, 0, Ht + 2.4), (0, 0, Ht + 3.4), 1.3, red, r1=0.2, n=24)
    sphere("ball", (0, 0, Ht + 3.55), 0.22, dark)
    # door and two small windows
    box2("door", (-0.45, -1.95, 1.0), (0.45, -1.6, 2.9), red)
    for z in (6.5, 11.0):
        box2("win%.0f" % z, (-0.3, -1.8 + (z - 1) * 0.04, z), (0.3, -1.3 + (z - 1) * 0.04, z + 0.9), dark)
    # keeper's hut
    box2("hut", (2.0, -0.5, 0.8), (5.0, 2.5, 3.4), white, bev=0.05)
    prism("hutroof", [(1.8, 3.4), (5.2, 3.4), (3.5, 4.6)], -0.7, 2.7, red)


def build_beach_hut(rng):
    planks = M_bands(["#48b8d8", "#f4f0e0", "#48b8d8", "#f4f0e0"], axis="X", size=0.3, rough=0.7)
    planks2 = M_bands(["#48b8d8", "#f4f0e0"], axis="Y", size=0.3, rough=0.7)
    wood = M_noise("#8a6a48", "#b08a60", scale=5, rough=0.8, name="wood")
    roof = M("#e05a3a", rough=0.6)
    white = M("#f6f4ec", rough=0.5)
    dark = M("#2a3036", rough=0.4)
    zf = 0.9
    for x in (-1.3, 1.3):
        for y in (-1.1, 1.1):
            cyl("stilt%.0f%.0f" % (x, y), (x, y, 0), (x, y, zf), 0.1, wood, n=8)
    box2("floor", (-1.7, -1.9, zf), (1.7, 1.4, zf + 0.15), wood)
    box2("front", (-1.5, -1.25, zf + 0.15), (1.5, -1.15, zf + 2.5), planks)
    box2("back", (-1.5, 1.15, zf + 0.15), (1.5, 1.25, zf + 2.5), planks)
    box2("sideL", (-1.5, -1.2, zf + 0.15), (-1.4, 1.2, zf + 2.5), planks2)
    box2("sideR", (1.4, -1.2, zf + 0.15), (1.5, 1.2, zf + 2.5), planks2)
    prism("gableF", [(-1.5, zf + 2.5), (1.5, zf + 2.5), (0, zf + 3.5)], -1.25, 1.25, white)
    # roof slabs
    for s in (-1, 1):
        ob = box("roof%d" % s, (s * 0.85, 0, zf + 3.05), (1.95, 3.0, 0.12), roof)
        ob.rotation_euler = (0, s * math.atan2(1.0, 1.5), 0)
    box2("door", (-0.5, -1.3, zf + 0.15), (0.5, -1.24, zf + 2.0), white)
    box2("doorin", (-0.4, -1.32, zf + 0.25), (0.4, -1.28, zf + 1.9), M("#e8a030", rough=0.6))
    for x in (-1.05, 1.05):
        box2("win%.0f" % x, (x - 0.25, -1.3, zf + 1.1), (x + 0.25, -1.26, zf + 1.7), dark)
    # steps
    for i in range(3):
        box2("step%d" % i, (-0.6, -1.9 - 0.35 * (i + 1), zf * (2 - i) / 3), (0.6, -1.9 - 0.35 * i, zf * (2 - i) / 3 + 0.1), wood)
    # a surfboard leaning on the side
    ob = uvsphere("board", (1.75, -0.4, 1.6), 1.0, M("#ffcc20", rough=0.3, spec=0.6), scale=(0.08, 0.32, 1.1))
    ob.rotation_euler = (0.0, 0.15, 0)


def build_guardrail(rng):
    """4 m W-beam guardrail along the road (Y) on three posts."""
    galv = M_noise("#9ea4aa", "#c4c8cc", scale=3, rough=0.35, metallic=0.85, bump=0.05, name="galv")
    post = M("#6a6e72", rough=0.5, metallic=0.6)
    for y in (-1.9, 0.0, 1.9):
        box2("post%.0f" % y, (0.05, y - 0.08, 0), (0.25, y + 0.08, 0.72), post)
        box2("block%.0f" % y, (-0.02, y - 0.07, 0.42), (0.06, y + 0.07, 0.66), post)
    # W profile in (x, z), extruded along Y
    prof = []
    for i in range(13):
        t = i / 12
        z = 0.40 + 0.31 * t
        x = -0.05 - 0.05 * math.cos(2 * math.pi * 2 * t)
        prof.append((x, z))
    back = [(x + 0.012, z) for x, z in prof[::-1]]
    poly = prof + back
    ob = prism("beam", poly, -2.0, 2.0, galv, smooth=False)
    # the prism builder takes (x, z) and extrudes along y; y0 < y1
    reflect = M("#ffb020", rough=0.3, emit="#ff9a10", emit_str=0.3)
    for y in (-1.9, 1.9):
        box2("refl%.0f" % y, (-0.13, y - 0.05, 0.53), (-0.1, y + 0.05, 0.6), reflect)


# ---------------------------------------------------------------------------
# PROPS: desert


def cactus_mat():
    return M_noise("#2f6a2a", "#4f8f3a", scale=3.0, bump=0.2, rough=0.6, spec=0.35,
                   c3="#7a9a48", c3_amt=0.2, c3_scale=2.0, name="cactus")


def saguaro(rng, H, arms):
    m = cactus_mat()
    R = 0.36 * H / 7.0 + 0.1
    tp = smooth_path([[0, 0, 0], [0.03, 0, H * 0.5], [0, 0.02, H - R]], 16)
    rr = np.full(16, R)
    rr[-3:] *= [0.97, 0.9, 0.75]
    ribbed_tube("trunk", tp, rr, m, ribs=14, rib_amp=0.07)
    sphere("top", tuple(tp[-1]), R * 0.78, m, scale=(1, 1, 0.9))
    for (side, z0, reach, rise) in arms:
        # an arm leaves sideways, bends up (J shape)
        s = side
        pts = smooth_path([[s * R * 0.6, 0, z0], [s * (R + reach * 0.6), 0, z0 + 0.15],
                           [s * (R + reach), 0, z0 + 0.7], [s * (R + reach), 0, z0 + rise]], 18)
        ra = np.full(18, R * 0.62)
        ra[-3:] *= [0.95, 0.88, 0.7]
        ribbed_tube("arm%.1f" % z0, pts, ra, m, ribs=10, rib_amp=0.08)
        sphere("armtop%.1f" % z0, tuple(pts[-1]), R * 0.62 * 0.72, m, scale=(1, 1, 0.9))


def build_saguaro(rng):
    saguaro(rng, 7.0, [(-1, 2.6, 1.0, 2.4), (1, 3.4, 0.9, 2.0), (-1, 4.4, 0.6, 1.2)])


def build_saguaro_small(rng):
    saguaro(rng, 3.6, [(1, 1.4, 0.6, 1.2)])


def build_butte(rng):
    """Red sandstone mesa ~60 m tall: flat top, sheer banded cliffs, talus skirt."""
    nx, ny = 180, 90
    xs = np.linspace(-80, 80, nx)
    ys = np.linspace(-14, 62, ny)       # the front cliff foot is near y = 0
    X, Y = np.meshgrid(xs, ys)
    nz = Noise3(rng, 0.06)
    nz2 = Noise3(rng, 0.2)
    P = np.stack([X.ravel(), Y.ravel(), np.zeros(X.size)], axis=1)
    n1 = nz(P).reshape(X.shape)
    n2 = nz2(P).reshape(X.shape)
    d = np.sqrt((X / 48) ** 2 + ((Y - 26) / 26) ** 2) + 0.07 * n1 + 0.025 * n2
    cliff = np.clip((1.0 - d) / 0.06, 0, 1)
    # a lower step on one side
    d2 = np.sqrt(((X - 34) / 22) ** 2 + ((Y - 20) / 18) ** 2) + 0.06 * n1
    step = np.clip((1.0 - d2) / 0.08, 0, 1)
    talus = np.clip((1.45 - d) / 0.5, 0, 1) ** 1.6
    Hh = np.maximum.reduce([60 * cliff, 36 * step, 20 * talus])
    Hh += 0.8 * n2 * (Hh > 1)
    Hh -= 1.0
    V = np.stack([X.ravel(), Y.ravel(), Hh.ravel()], axis=1)
    F = []
    for j in range(ny - 1):
        for i in range(nx - 1):
            a = j * nx + i
            F.append([a, a + 1, a + nx + 1, a + nx])
    rockm = M_noise("#a8452a", "#d27a48", scale=0.08, bump=0.4, bump_dist=0.5, strata=0.5,
                    strata_col="#7a3020", strata_scale=0.09, snow=1.0, snow_col="#c88a5a",
                    snow_lo=0.62, snow_hi=0.8, rough=0.95, name="sandstone")
    ob = mesh("butte", V, F, rockm, smooth=True)
    ob.data.use_auto_smooth = True
    ob.data.auto_smooth_angle = math.radians(60)


def build_rock_red(rng):
    rk = M_noise("#9a3e24", "#cf7040", scale=0.8, bump=0.5, bump_dist=0.15, strata=0.4,
                 strata_col="#7a3020", strata_scale=1.2, rough=0.9, name="redrock")
    rock("r1", (0, 0, 0), 1.0, rk, rng, scale=(2.2, 1.8, 2.8), amp=0.22, freq=1.4)
    rock("r2", (2.0, -0.6, 0), 1.0, rk, rng, scale=(1.3, 1.2, 1.4), amp=0.25, freq=1.8)
    rock("r3", (-1.9, -0.9, 0), 1.0, rk, rng, scale=(0.9, 0.9, 0.8), amp=0.25, freq=2.0)


def build_dead_tree(rng):
    wood = M_noise("#6a5a4c", "#a8998a", scale=4, bump=0.6, rough=0.85, name="deadwood")

    def branch(p, d, length, r, depth):
        n = 6
        pts = [np.array(p, float)]
        dd = np.array(d, float)
        for i in range(n):
            dd = dd + rng.normal(0, 0.18, 3)
            dd[2] += 0.05
            dd /= np.linalg.norm(dd)
            pts.append(pts[-1] + dd * length / n)
        tube("br", pts, np.linspace(r, r * 0.55, len(pts)), wood, bevel_res=2)
        if depth <= 0:
            return
        k = 2 if depth < 3 else 3
        for j in range(k):
            t = rng.uniform(0.55, 1.0)
            q = pts[int(t * n)]
            nd = dd + rng.normal(0, 0.6, 3)
            nd[2] = abs(nd[2]) * 0.6 + 0.2
            nd /= np.linalg.norm(nd)
            branch(q, nd, length * rng.uniform(0.55, 0.75), r * 0.55, depth - 1)

    branch((0, 0, 0), (0.1, 0, 1), 3.2, 0.32, 3)
    cyl("flare", (0, 0, 0), (0, 0, 0.7), 0.5, wood, r1=0.3, n=10)


def build_diner_sign(rng):
    """Tall googie roadside sign on a pole: blank panel, bulb border, arrow."""
    pole = M("#c8cbd0", rough=0.35, metallic=0.8)
    red = M("#e02a30", rough=0.35, spec=0.6, clearcoat=0.5)
    cream = M("#fff0c8", rough=0.5)
    teal = M("#20c0b0", rough=0.35, spec=0.6, clearcoat=0.5)
    bulb = M("#fff4c0", rough=0.2, emit="#ffe070", emit_str=4.0)
    yel = M("#ffc820", rough=0.35, clearcoat=0.4)
    Hp = 8.0
    cyl("pole", (0, 0.3, 0), (0, 0.3, Hp + 5.5), 0.28, pole, n=16)
    # main board: rounded rectangle 6 x 3.6 m
    def rrect(cx, cz, w, h, r, n=6):
        pts = []
        for (sx, sz, a0) in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
            ccx, ccz = cx + sx * (w / 2 - r), cz + sz * (h / 2 - r)
            for k in range(n + 1):
                a = math.radians(a0 + 90 * k / n)
                pts.append((ccx + r * math.cos(a), ccz + r * math.sin(a)))
        return pts
    zc = Hp + 2.2
    prism("board", rrect(0, zc, 6.4, 3.8, 0.9), -0.25, 0.25, red)
    prism("panel", rrect(0, zc, 5.2, 2.6, 0.5), -0.3, -0.24, cream)
    # bulbs around the border
    nb = 26
    pts = rrect(0, zc, 5.85, 3.25, 0.65, n=3)
    per = np.array(pts)
    seg = np.sqrt(((np.roll(per, -1, 0) - per) ** 2).sum(1))
    cum = np.concatenate([[0], np.cumsum(seg)])
    for i in range(nb):
        s = cum[-1] * i / nb
        k = np.searchsorted(cum, s) - 1
        k = max(0, min(k, len(per) - 1))
        t = (s - cum[k]) / max(seg[k], 1e-6)
        p = per[k] + (per[(k + 1) % len(per)] - per[k]) * t
        sphere("bulb%d" % i, (p[0], -0.28, p[1]), 0.1, bulb, subdiv=2)
    # a teal arrow pointing down-left to the road (-X)
    ar = [(-3.8, Hp - 1.2), (-2.6, Hp - 0.6), (-2.8, Hp - 0.35), (1.8, Hp - 0.35), (1.8, Hp + 0.35),
          (-2.8, Hp + 0.35), (-2.6, Hp + 0.6)]
    ar = [(-4.2, Hp), (-3.0, Hp + 0.8), (-3.0, Hp + 0.3), (2.0, Hp + 0.3), (2.0, Hp - 0.3),
          (-3.0, Hp - 0.3), (-3.0, Hp - 0.8)]
    prism("arrow", ar, -0.2, 0.2, teal)
    for i in range(7):
        sphere("abulb%d" % i, (-2.6 + i * 0.7, -0.22, Hp), 0.09, bulb, subdiv=2)
    # a star on top
    star = []
    for k in range(10):
        a = math.pi / 2 + k * math.pi / 5
        r = 1.0 if k % 2 == 0 else 0.45
        star.append((r * math.cos(a), zc + 2.9 + r * math.sin(a)))
    prism("star", star, -0.15, 0.15, yel)


# ---------------------------------------------------------------------------
# PROPS: mountain (night)


def pine(rng, H=11.0, Rmax=2.8, snow=0.0):
    hb = 1.4
    bark = M_noise("#3a2418", "#7a5038", scale=5, bump=0.6, name="pine_bark")
    leaf = mat_foliage("pine_leaf", dark="#0f3322", mid="#1f5a36", light="#468a4a",
                       top_tint="#5e9a50", shadow_tint="#0d2a36", translucency=0.12,
                       clump_var=0.3, ao_strength=0.65, snow=snow)
    clumps = []
    z = hb
    k = 0
    golden = math.pi * (3 - math.sqrt(5))
    while z < H - 0.6:
        t = (z - hb) / (H - hb)
        Lb = Rmax * (1 - t) ** 0.95 + 0.3
        nb = 7 if t < 0.6 else 5
        for i in range(nb):
            az = k * golden + i * 2 * math.pi / nb + rng.normal(0, 0.15)
            k += 1
            dirh = np.array([math.cos(az), math.sin(az), 0.0])
            ang = math.radians(rng.uniform(14, 26))
            axis = dirh * math.cos(ang) + np.array([0, 0, -math.sin(ang)])
            ll = Lb * rng.uniform(0.85, 1.08)
            c = np.array([0, 0, z]) + axis * ll * 0.52
            R = rot_from_axis(axis)
            clumps.append(Clump(c, (ll * 0.55, max(0.28, ll * 0.33), max(0.2, ll * 0.17)), R))
        z += 0.62 + 0.1 * rng.random()
    for zz, rr in ((H - 0.8, 0.4), (H - 0.35, 0.25)):
        clumps.append(Clump((0, 0, zz), (rr, rr, rr * 1.8)))
    tube("pine_trunk", smooth_path([[0, 0, 0], [0, 0, H * 0.5], [0, 0, H - 0.3]], 8),
         np.linspace(0.3, 0.05, 8), bark, bevel_res=5)
    gc = np.array([0, 0, hb + (H - hb) * 0.35])
    gr = (Rmax, Rmax, (H - hb) * 0.6)
    v, f, a, n = canopy_cards(rng, clumps, density=110, leaf_len=0.28, leaf_aspect=0.42,
                              gcenter=gc, gradii=gr, disp_amp=0.2, disp_freq=1.8,
                              tilt=0.6, w_card=0.25, w_clump=0.5, w_global=0.25, droop=0.4)
    mesh_from_arrays("pine_leaves", v, f, a, n, leaf)
    filler("pine_fill", clumps, leaf, None, gc, gr)
    if snow > 0:
        # snow blobs lying on the branch tiers
        sm = M_noise("#dfe8f6", "#ffffff", scale=3, bump=0.2, rough=0.6, name="snow")
        for i, cl in enumerate(clumps[:-2]):
            if rng.random() < 0.45:
                top = cl.c + np.array([0, 0, cl.r[2] * 0.8])
                sphere("sn%d" % i, tuple(top), 1.0, sm, scale=(cl.r[0] * 0.6, cl.r[1] * 0.7, cl.r[2] * 0.75),
                       subdiv=3)


def build_pine_snow(rng):
    pine(rng, 11.0, 2.9, snow=0.85)


def build_pine(rng):
    pine(rng, 12.5, 2.6, snow=0.0)


def snow_rock_mat():
    return M_noise("#4a4a52", "#7a7a84", scale=1.2, bump=0.6, bump_dist=0.15, snow=1.0,
                   snow_col="#eef4ff", snow_lo=0.35, snow_hi=0.5, rough=0.85, name="snowrock")


def build_rock_snow(rng):
    rk = snow_rock_mat()
    rock("r1", (0, 0, 0), 1.0, rk, rng, scale=(3.0, 2.4, 3.2), amp=0.25, freq=1.3)
    rock("r2", (2.6, -0.8, 0), 1.0, rk, rng, scale=(1.5, 1.4, 1.6), amp=0.25, freq=1.8)
    sm = M_noise("#dfe8f6", "#ffffff", scale=2, bump=0.2, rough=0.6, name="snow")
    rock("drift", (0.5, -1.2, 0), 1.0, sm, rng, scale=(4.2, 2.2, 0.6), amp=0.12, freq=1.5)


def build_snowbank(rng):
    sm = M_noise("#d6e0f0", "#ffffff", scale=1.5, bump=0.3, bump_dist=0.1, rough=0.6, name="snow",
                 c3="#9aa8c0", c3_amt=0.15, c3_scale=3.0)
    rock("bank", (0, 0, 0), 1.0, sm, rng, scale=(0.9, 2.2, 0.9), amp=0.14, freq=1.5)
    rock("bank2", (0.3, 1.6, 0), 1.0, sm, rng, scale=(0.7, 1.2, 0.6), amp=0.14, freq=1.8)
    # a marker pole (orange) in the bank, as on mountain roads
    cyl("pole", (-0.3, -0.5, 0), (-0.3, -0.5, 1.8), 0.04, M("#ff6a10", rough=0.4), n=8)
    box2("refl", (-0.35, -0.55, 1.5), (-0.25, -0.49, 1.7), M("#ffffff", emit="#ffffff", emit_str=0.8))


def build_cabin(rng, lit=True):
    logm = M_noise("#5a3a22", "#8a5a34", scale=3.0, bump=0.4, rough=0.8, name="logs")
    endm = M("#a8804a", rough=0.8)
    stone = M_noise("#5a5a60", "#8a8a90", scale=4.0, bump=0.6, rough=0.9, name="stone")
    snow = M_noise("#dfe8f6", "#ffffff", scale=2, bump=0.2, rough=0.6, name="snow")
    roofm = M("#3a2a24", rough=0.8)
    win = M_emit("#ffb050", 14.0, "window")
    frame = M("#e8dcc8", rough=0.6)
    W, D, Hw = 7.0, 5.5, 2.8
    r = 0.16
    n = int(Hw / (2 * r))
    for i in range(n):
        z = r + i * 2 * r
        cyl("lx%d" % i, (-W / 2 - 0.3, -D / 2, z), (W / 2 + 0.3, -D / 2, z), r, logm, n=10)
        cyl("lb%d" % i, (-W / 2 - 0.3, D / 2, z), (W / 2 + 0.3, D / 2, z), r, logm, n=10)
        cyl("ly%d" % i, (-W / 2, -D / 2 - 0.3, z + r), (-W / 2, D / 2 + 0.3, z + r), r, logm, n=10)
        cyl("ry%d" % i, (W / 2, -D / 2 - 0.3, z + r), (W / 2, D / 2 + 0.3, z + r), r, logm, n=10)
    box2("fill", (-W / 2, -D / 2, 0), (W / 2, D / 2, Hw), logm)
    # gables (on the X ends) and the roof along X
    ridge = Hw + 2.2
    for s in (-1, 1):
        prism("gable%d" % s, [(-D / 2, Hw), (D / 2, Hw), (0, ridge)], 0, 0.1, logm).rotation_euler = (0, 0, math.pi / 2)
    for s in (-1, 1):
        g = bpy.context.scene.objects["gable%d" % s]
        g.location = (s * W / 2 - (0.1 if s > 0 else 0), 0, 0)
    ang = math.atan2(ridge - Hw, D / 2)
    Lr = math.hypot(D / 2, ridge - Hw) + 0.7
    for s in (-1, 1):
        ob = box("roof%d" % s, (0, s * (D / 4 + 0.1), (Hw + ridge) / 2 + 0.12), (W + 1.2, Lr, 0.18), roofm)
        ob.rotation_euler = (-s * ang, 0, 0)
        ob = box("snowroof%d" % s, (0, s * (D / 4 + 0.1), (Hw + ridge) / 2 + 0.34), (W + 1.3, Lr + 0.05, 0.3), snow, bev=0.12)
        ob.rotation_euler = (-s * ang, 0, 0)
    # chimney
    box2("chim", (1.6, 0.6, Hw), (2.4, 1.4, ridge + 1.0), stone)
    box2("chimsnow", (1.55, 0.55, ridge + 1.0), (2.45, 1.45, ridge + 1.2), snow)
    # windows (lit) on the front (-Y) and the road side (-X), and a door
    for x in (-2.0, 1.8):
        box2("wf%.0f" % x, (x - 0.65, -D / 2 - 0.3, 0.9), (x + 0.65, -D / 2 - 0.2, 2.2), frame)
        box2("w%.0f" % x, (x - 0.5, -D / 2 - 0.34, 1.0), (x + 0.5, -D / 2 - 0.22, 2.1),
             win if lit else M("#1a2030", rough=0.1))
        box2("wbar%.0f" % x, (x - 0.04, -D / 2 - 0.36, 1.0), (x + 0.04, -D / 2 - 0.3, 2.1), frame)
    box2("door", (-0.5, -D / 2 - 0.3, 0.0), (0.4, -D / 2 - 0.2, 2.1), M("#4a2a18", rough=0.7))
    for y in (-1.0, 1.2):
        box2("ws%.0f" % y, (-W / 2 - 0.34, y - 0.5, 1.0), (-W / 2 - 0.22, y + 0.5, 2.1), win if lit else frame)
    # snow around the base
    rock("drift", (0, -D / 2 - 0.6, 0), 1.0, snow, rng, scale=(W * 0.7, 1.2, 0.35), amp=0.15, freq=1.5)


def build_lamp_night(rng):
    build_lamp(rng, night=True)
    # a warm light under the head so the pole and arm catch it
    add_point((-3.9, 0.0, 10.8), 400.0, (1.0, 0.72, 0.4), radius=0.4, name="lamp_pt").__setitem__("keep", True)


# ---------------------------------------------------------------------------
# PROPS: space


def M_crystal(c_lo, c_hi, strength=3.0, name="crystal"):
    """Glassy crystal whose emission runs from c_lo at the foot to c_hi at the tip."""
    mat = bpy.data.materials.new(name)
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    sep = N.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])
    mr = N.new("ShaderNodeMapRange")
    mr.inputs[1].default_value = 0.0
    mr.inputs[2].default_value = 1.0
    L.new(sep.outputs["Z"], mr.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = hexcol(c_lo)
    ramp.color_ramp.elements[1].color = hexcol(c_hi)
    L.new(mr.outputs[0], ramp.inputs[0])
    # facet shading: a fresnel term brightens the edges
    fr = N.new("ShaderNodeLayerWeight")
    fr.inputs[0].default_value = 0.35
    em = N.new("ShaderNodeMath"); em.operation = "MULTIPLY_ADD"
    L.new(fr.outputs["Facing"], em.inputs[0])
    em.inputs[1].default_value = strength * 1.2
    em.inputs[2].default_value = strength * 0.5
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = 0.08
    b.inputs["Specular"].default_value = 0.8
    L.new(ramp.outputs[0], b.inputs["Base Color"])
    L.new(ramp.outputs[0], b.inputs["Emission"])
    L.new(em.outputs[0], b.inputs["Emission Strength"])
    L.new(b.outputs[0], out.inputs["Surface"])
    return mat


def crystal_spire(name, base, axis, length, radius, mat, sides=6, tip=0.28, twist=0.0):
    """Hexagonal prism with a pointed tip; the local Z of the mesh runs 0..1
    along it so the crystal material's gradient follows each spire."""
    V, F = [], []
    for ring, (z, r) in enumerate(((0.0, radius * 0.9), (1 - tip, radius), (1.0, 0.0))):
        for k in range(sides):
            a = 2 * math.pi * k / sides + twist
            V.append((r * math.cos(a), r * math.sin(a), z))
    for ring in range(2):
        for k in range(sides):
            a0 = ring * sides + k
            a1 = ring * sides + (k + 1) % sides
            F.append([a0, a1, a1 + sides, a0 + sides])
    F.append(list(range(sides))[::-1])
    ob = mesh(name, V, F, mat, smooth=False)
    ob.scale = (1, 1, length)
    ob.location = base
    ob.rotation_euler = Vector(axis).to_track_quat("Z", "Y").to_euler()
    return ob


def build_crystal(rng):
    cy = M_crystal("#1030ff", "#40fff0", 3.2, "cryst_cyan")
    mg = M_crystal("#6010c0", "#ff40e0", 3.2, "cryst_mag")
    rk = M_noise("#1c1628", "#3a3050", scale=1.5, bump=0.6, rough=0.8, name="spacerock")
    rock("foot", (0, 0, 0), 1.0, rk, rng, scale=(3.0, 2.6, 1.2), amp=0.25, freq=1.6)
    crystal_spire("main", (0, 0, 0.3), (0.05, 0, 1), 12.0, 0.9, cy)
    spires = [((-0.9, 0.2, 0.4), (-0.5, 0.1, 1), 6.5, 0.6, mg), ((1.0, -0.3, 0.4), (0.45, -0.1, 1), 7.5, 0.6, mg),
              ((0.4, 0.8, 0.3), (0.2, 0.5, 1), 5.0, 0.5, cy), ((-1.6, -0.6, 0.2), (-1.0, -0.3, 0.7), 3.6, 0.45, cy),
              ((1.8, 0.4, 0.2), (1.0, 0.2, 0.8), 3.2, 0.4, mg), ((-0.3, -1.1, 0.2), (-0.2, -0.8, 1), 2.8, 0.38, mg)]
    for i, (b, a, Ls, r, m) in enumerate(spires):
        crystal_spire("sp%d" % i, b, a, Ls, r, m, twist=rng.uniform(0, 1))


def build_asteroid_a(rng):
    rk = M_noise("#4a4252", "#8a7e94", scale=0.6, bump=0.9, bump_dist=0.3, rough=0.9,
                 c3="#8a6a5a", c3_amt=0.2, c3_scale=0.4, name="asteroid")
    ob = rock("ast", (0, 0, 4.2), 1.0, rk, rng, scale=(5.0, 4.2, 4.2), amp=0.3, freq=0.9, subdiv=5,
              flat_bottom=False)
    # craters: push vertices in around a few centres
    me = ob.data
    P = np.array([v.co[:] for v in me.vertices])
    for _ in range(14):
        d = rand_unit(rng, 1)[0]
        if d[1] > 0.4:
            d[1] = -d[1]
        c = d * np.linalg.norm(P, axis=1).mean()
        cr = rng.uniform(0.5, 1.3)
        dist = np.linalg.norm(P - c, axis=1)
        k = np.clip(1 - (dist / cr) ** 2, 0, 1)
        rim = np.exp(-((dist - cr) / (0.3 * cr)) ** 2) * 0.12
        P = P * (1 - 0.06 * k[:, None] + rim[:, None] * 0.25)
    for v, p in zip(me.vertices, P):
        v.co = p


def build_asteroid_b(rng):
    rk = M_noise("#2a2434", "#5a5068", scale=0.9, bump=0.8, bump_dist=0.2, rough=0.9, name="asteroid2")
    rock("ast", (0, 0, 2.2), 1.0, rk, rng, scale=(3.6, 2.2, 2.1), amp=0.32, freq=1.2, flat_bottom=False)
    mg = M_crystal("#6010c0", "#ff40e0", 3.0, "ab_mag")
    cy = M_crystal("#1030ff", "#40fff0", 3.0, "ab_cyan")
    for i, (b, a, Ls, r) in enumerate((((-0.6, -0.6, 3.6), (-0.4, -0.3, 1), 2.6, 0.32),
                                       ((0.9, -0.5, 3.4), (0.5, -0.4, 1), 2.0, 0.28),
                                       ((2.2, -0.4, 2.6), (1, -0.2, 0.4), 1.6, 0.25),
                                       ((-2.4, -0.3, 2.3), (-1, -0.3, 0.2), 1.4, 0.22))):
        crystal_spire("c%d" % i, b, a, Ls * 1.7, r * 1.4, mg if i % 2 == 0 else cy, twist=0.3 * i)


def build_ring_gate(rng):
    """A neon ring ~16 m across that the road passes through; the part below
    the road (Z < 0) is held out."""
    metal = M("#2a2e3a", rough=0.35, metallic=0.9)
    neon_m = M_emit("#ff30d0", 8.0, "neon_mag")
    neon_c = M_emit("#30f0ff", 8.0, "neon_cyan")
    zc, R = 5.6, 8.0
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=0.55, major_segments=96, minor_segments=16,
                                     location=(0, 0.3, zc), rotation=(math.pi / 2, 0, 0))
    t = bpy.context.active_object
    t.data.materials.append(metal)
    for p in t.data.polygons:
        p.use_smooth = True
    # inner and front neon tubes
    bpy.ops.mesh.primitive_torus_add(major_radius=R - 0.55, minor_radius=0.14, major_segments=96,
                                     minor_segments=8, location=(0, -0.05, zc), rotation=(math.pi / 2, 0, 0))
    bpy.context.active_object.data.materials.append(neon_m)
    bpy.ops.mesh.primitive_torus_add(major_radius=R + 0.5, minor_radius=0.1, major_segments=96,
                                     minor_segments=8, location=(0, 0.0, zc), rotation=(math.pi / 2, 0, 0))
    bpy.context.active_object.data.materials.append(neon_c)
    # cyan light nodes around the ring
    for k in range(16):
        a = 2 * math.pi * k / 16
        x, z = R * math.cos(a), zc + R * math.sin(a)
        if z < 0.3:
            continue
        box("node%d" % k, (x, -0.35, z), (0.9, 0.5, 0.9), metal, rot_z=0).rotation_euler = (0, -a, 0)
        sphere("nl%d" % k, (x * 1.0, -0.62, z), 0.2, neon_c, subdiv=2)
    # hold out everything under the road surface
    bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
    h = bpy.context.active_object
    h.name = "holdout"
    h.scale = (80, 120, 1)
    h.location = (0, -20, 0)
    h.data.materials.append(M_holdout())
    h["noframe"] = True
    h["keep"] = True


def build_satellite(rng):
    gold = M_noise("#e0a830", "#ffe080", scale=6, bump=0.2, bump_dist=0.02, rough=0.35, metallic=0.35,
                   name="foil")
    white = M("#e8e8ec", rough=0.4)
    dark = M("#22252c", rough=0.4, metallic=0.7)
    panel = M_grid("#c8ccd4", "#1a3aa8", cell=(0.8, 0.8), frac=(0.9, 0.9), win_var=0.18, rough_win=0.2,
                   spec_win=0.8, name="solar")
    zc = 3.2
    box("body", (0, 0, zc), (1.8, 1.8, 2.2), gold, bev=0.05)
    box("top", (0, 0, zc + 1.2), (1.9, 1.9, 0.2), white)
    for s in (-1, 1):
        cyl("boom%d" % s, (s * 0.9, 0, zc), (s * 2.0, 0, zc), 0.07, white, n=8)
        box("wing%d" % s, (s * 4.9, 0, zc), (5.6, 0.08, 2.1), panel)
        box("wingf%d" % s, (s * 4.9, 0.05, zc), (5.7, 0.06, 2.2), dark)
    # dish on the road side
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=1.2)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z > -0.5], context="VERTS")
    ob = _obj_from_bm("dish", bm, white, True)
    ob.location = (0, -0.4, zc + 2.6)
    ob.rotation_euler = (math.radians(-150), 0, 0)
    ob.modifiers.new("solid", "SOLIDIFY").thickness = 0.05
    cyl("feed", (0, -0.4, zc + 1.3), (0, -0.9, zc + 2.0), 0.05, dark, n=6)
    cyl("strut", (0, -0.2, zc + 1.2), (0, -0.4, zc + 2.45), 0.1, white, n=8)
    cyl("ant", (0.6, 0.3, zc + 1.3), (0.8, 0.3, zc + 3.4), 0.03, white, n=6)
    sphere("blink", (0.8, 0.3, zc + 3.45), 0.12, M_emit("#ff2020", 10.0, "blink"), subdiv=2)
    cyl("thr", (0, 0, zc - 1.1), (0, 0, zc - 1.6), 0.25, dark, r1=0.4, n=12)


def build_beacon(rng, on=True):
    metal = M("#262a36", rough=0.35, metallic=0.9)
    trim = M_emit("#30f0ff", 5.0 if on else 1.2, "trim")
    light = M_emit("#ff40d0", 22.0 if on else 1.5, "beacon") if on else M("#8a2a70", rough=0.2, emit="#ff40d0",
                                                                        emit_str=0.6)
    Hh = 6.0
    cyl("pylon", (0, 0, 0), (0, 0, Hh), 0.55, metal, r1=0.22, n=6, smooth=False)
    cyl("foot", (0, 0, 0), (0, 0, 0.4), 1.0, metal, r1=0.8, n=6, smooth=False)
    for z in (1.2, 2.6, 4.0):
        r = 0.55 - 0.33 * z / Hh
        cyl("ring%.0f" % z, (0, 0, z), (0, 0, z + 0.18), r + 0.06, trim, n=6, smooth=False)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4
        cyl("fin%d" % k, (0.3 * math.cos(a), 0.3 * math.sin(a), Hh - 0.4),
            (0.75 * math.cos(a), 0.75 * math.sin(a), Hh + 0.6), 0.06, metal, n=5)
    sphere("light", (0, 0, Hh + 0.55), 0.45, light, subdiv=3)


def build_beacon_on(rng):
    build_beacon(rng, True)


def build_beacon_off(rng):
    build_beacon(rng, False)


# ---------------------------------------------------------------------------
# Registry: name -> (stage, builder, seed, spec)

PROPS = {}


def reg(name, stage, builder, seed=1, **spec):
    spec["stage"] = stage
    PROPS[name] = (builder, seed, spec)


reg("checkpoint", "common", build_checkpoint, h=256, shadow=True)
reg("finish", "common", build_finish, h=256, shadow=True)
reg("cone", "common", build_cone, h=64, shadow=True)
reg("sign_curve_l", "common", build_sign_curve, h=128, shadow=True)
reg("barrier", "common", build_barrier, h=64, shadow=True, yaw=35,
    note="runs along the road (Y), rendered yawed 35 deg to show its road side")
reg("overpass", "city", build_overpass, h=160, shadow=True,
    note="anchor = road centre under the front (camera-side) face; deck 12 m deep, underside 6 m")
reg("lamp", "city", build_lamp_day, h=256, shadow=True, note="arm reaches -X (towards the road)")
reg("tower_brick", "city", build_tower_brick, 3, h=384, yaw=25)
reg("tower_glass", "city", build_tower_glass, 4, h=384, yaw=20)
reg("tree_round", "city", build_tree_round, 7, h=192, shadow=True)
reg("billboard", "city", build_billboard, h=160, shadow=True, yaw=20)
reg("sign_gantry", "city", build_sign_gantry, h=192, shadow=True,
    note="spans the road, anchor = road centre")
reg("palm", "coast", build_palm, 5, h=256, shadow=True, note="leans towards -X (the road)")
reg("palm_tall", "coast", build_palm_tall, 8, h=384, shadow=True, note="leans towards -X (the road)")
reg("rock_cliff", "coast", build_rock_cliff, 12, h=256)
reg("lighthouse", "coast", build_lighthouse, 3, h=320)
reg("beach_hut", "coast", build_beach_hut, 2, h=128, shadow=True, yaw=25)
reg("guardrail", "coast", build_guardrail, 1, h=64, shadow=True, yaw=35,
    note="runs along the road (Y), rendered yawed 35 deg to show its road side")
reg("saguaro", "desert", build_saguaro, 1, h=224, shadow=True)
reg("saguaro_small", "desert", build_saguaro_small, 2, h=128, shadow=True)
reg("butte", "desert", build_butte, 6, h=200, note="seen far: place it hundreds of metres away")
reg("rock_red", "desert", build_rock_red, 4, h=96, shadow=True)
reg("dead_tree", "desert", build_dead_tree, 9, h=192, shadow=True)
reg("diner_sign", "desert", build_diner_sign, 1, h=256, shadow=True, note="arrow points -X (the road)")
reg("pine_snow", "mountain", build_pine_snow, 11, h=256, shadow=True, light="night")
reg("pine", "mountain", build_pine, 13, h=288, shadow=True, light="night")
reg("rock_snow", "mountain", build_rock_snow, 5, h=128, shadow=True, light="night")
reg("snowbank", "mountain", build_snowbank, 3, h=64, light="night", yaw=35,
    note="runs along the road (Y), rendered yawed 35 deg")
reg("cabin", "mountain", build_cabin, 2, h=192, light="night", yaw=30, glow_px=14,
    note="windows lit warm")
reg("lamp_night", "mountain", build_lamp_night, 1, h=256, light="night", glow_px=22, glow=1.0,
    pool=(-3.9, 0.0, 5.0, "#ffb060", 2.2),
    frame_pts=[(-9.5, 0, 0), (2.0, 0, 0), (-3.9, -5.0, 0)],
    note="lamp ON: head glow and the warm light pool on the ground are in the colour+alpha")
reg("asteroid_a", "space", build_asteroid_a, 21, h=192, light="space", rest=True,
    note="rests on its lowest point at the anchor; raise it to make it float")
reg("asteroid_b", "space", build_asteroid_b, 22, h=128, light="space", glow_px=16, rest=True)
reg("crystal", "space", build_crystal, 3, h=288, light="space", glow_px=22)
reg("ring_gate", "space", build_ring_gate, 1, h=288, light="space", glow_px=22, clip_z0=True,
    note="the road passes through it; anchor = road centre; the part below the road is cut off")
reg("satellite", "space", build_satellite, 1, h=128, light="space", glow_px=12, yaw=-20, rest=True,
    note="floats; its lowest point is at the anchor")
reg("beacon", "space", build_beacon_on, 1, h=160, light="space", glow_px=22)
reg("beacon_off", "space", build_beacon_off, 1, h=160, light="space", glow_px=22,
    note="second frame of the pulsing beacon (light dim); same size and anchor as beacon")


def apply_yaw(deg):
    if not deg:
        return
    a = math.radians(deg)
    bpy.context.view_layer.update()
    for o in scene_objs():
        if o.parent is None:
            o.matrix_world = Matrix.Rotation(a, 4, "Z") @ o.matrix_world


def place_at_base():
    """Move every object so the prop's base-centre (0, 0, 0) is at (0, PROP_Y, 0)."""
    for o in scene_objs() + [o for o in bpy.context.scene.objects if o.type == "LIGHT" and o.get("keep")]:
        if o.parent is None:
            o.location.y += PROP_Y


def run_prop(name):
    builder, seed, spec = PROPS[name]
    print("==", name)
    reset_scene()
    _MATS.clear()
    rng = np.random.RandomState(seed)
    builder(rng)
    apply_yaw(spec.get("yaw", 0))
    if spec.get("rest"):
        bpy.context.view_layer.update()
        zmin = world_points(scene_objs())[:, 2].min()
        for o in scene_objs():
            if o.parent is None:
                o.location.z -= zmin
    place_at_base()
    t0 = time.time()
    meta = render_prop(name, spec)
    meta["render_s"] = round(time.time() - t0, 1)
    return meta


# ---------------------------------------------------------------------------
# BACKDROPS: 360 deg bands, 1024 x 160, horizon on the bottom row

BG_W, BG_H = 1024, 160
BG_LAT = 360.0 * BG_H / BG_W          # 56.25 deg: square angular pixels


def add_fog(mat, fog_col, dist, max_f=0.9, power=1.0):
    """Mix the material towards a flat fog emission with the view distance."""
    nt = mat.node_tree
    N, L = nt.nodes, nt.links
    out = [n for n in N if n.type == "OUTPUT_MATERIAL"][0]
    src = out.inputs["Surface"].links[0].from_socket
    cam = N.new("ShaderNodeCameraData")
    d = N.new("ShaderNodeMath"); d.operation = "DIVIDE"
    L.new(cam.outputs["View Distance"], d.inputs[0]); d.inputs[1].default_value = -dist
    e = N.new("ShaderNodeMath"); e.operation = "EXPONENT"
    L.new(d.outputs[0], e.inputs[0])
    f = N.new("ShaderNodeMath"); f.operation = "SUBTRACT"
    f.inputs[0].default_value = 1.0
    L.new(e.outputs[0], f.inputs[1])
    pw = N.new("ShaderNodeMath"); pw.operation = "POWER"
    L.new(f.outputs[0], pw.inputs[0]); pw.inputs[1].default_value = power
    mx = N.new("ShaderNodeMath"); mx.operation = "MULTIPLY"
    L.new(pw.outputs[0], mx.inputs[0]); mx.inputs[1].default_value = max_f
    em = N.new("ShaderNodeEmission")
    em.inputs[0].default_value = hexcol(fog_col)
    em.inputs[1].default_value = 1.0
    ms = N.new("ShaderNodeMixShader")
    L.new(mx.outputs[0], ms.inputs[0])
    L.new(src, ms.inputs[1])
    L.new(em.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    return mat


def ring_terrain(name, mat, hfunc, R0, R1, nth=2048, nr=48, th0=0.0, th1=2 * math.pi):
    """Heightfield in polar coordinates around the camera. hfunc(TH, R) -> Z
    arrays (TH is the azimuth from +Y towards +X, radians)."""
    th = np.linspace(th0, th1, nth, endpoint=(th1 - th0) < 2 * math.pi - 1e-6)
    rr = R0 * (R1 / R0) ** np.linspace(0, 1, nr)
    TH, RR = np.meshgrid(th, rr)
    Z = hfunc(TH, RR)
    X = RR * np.sin(TH)
    Y = RR * np.cos(TH)
    V = np.stack([X.ravel(), Y.ravel(), Z.ravel()], axis=1)
    closed = (th1 - th0) >= 2 * math.pi - 1e-6
    F = []
    cols = nth if closed else nth - 1
    idx = np.arange(nr * nth).reshape(nr, nth)
    a = idx[:-1, :cols]
    b = np.roll(idx, -1, axis=1)[:-1, :cols]
    c = np.roll(idx, -1, axis=1)[1:, :cols]
    d = idx[1:, :cols]
    F = np.stack([a.ravel(), d.ravel(), c.ravel(), b.ravel()], axis=1)
    ob = mesh_from_arrays(name, V, F, mat=mat, smooth=True)
    return ob


def fbm1(rng, TH, octaves, base_k=3, gain=0.55):
    """Periodic 1D fractal noise of the azimuth, ~[-1, 1]."""
    out = np.zeros_like(TH)
    amp, tot = 1.0, 0.0
    k = base_k
    for _ in range(octaves):
        ph = rng.uniform(0, 2 * math.pi)
        ph2 = rng.uniform(0, 2 * math.pi)
        out += amp * (0.6 * np.sin(k * TH + ph) + 0.4 * np.sin((k + 1) * TH + ph2))
        tot += amp
        amp *= gain
        k = int(k * 2.1) + 1
    return out / tot


def ridge1(rng, TH, octaves, base_k=5):
    """Ridged periodic noise (sharp peaks)."""
    out = np.zeros_like(TH)
    amp, tot, k = 1.0, 0.0, base_k
    for _ in range(octaves):
        ph = rng.uniform(0, 2 * math.pi)
        out += amp * (1 - np.abs(np.sin(k * TH / 2 + ph)))
        tot += amp
        amp *= 0.5
        k = int(k * 2.03) + 1
    return out / tot


def bg_camera(ss):
    cd = bpy.data.cameras.new("pano")
    cd.type = "PANO"
    cd.cycles.panorama_type = "EQUIRECTANGULAR"
    cd.cycles.latitude_min = 0.0
    cd.cycles.latitude_max = math.radians(BG_LAT)
    cd.cycles.longitude_min = -math.pi
    cd.cycles.longitude_max = math.pi
    cd.clip_start = 1.0
    cd.clip_end = 100000
    ob = bpy.data.objects.new("pano", cd)
    ob["helper"] = True
    link(ob)
    ob.location = (0, 0, 0)
    ob.rotation_euler = (math.pi / 2, 0, 0)     # column 512 looks along +Y, +X at 768
    sc = bpy.context.scene
    sc.camera = ob
    sc.render.resolution_x = BG_W * ss
    sc.render.resolution_y = BG_H * ss


def az(deg):
    return math.radians(deg)


def at(azd, r, z=0.0):
    a = math.radians(azd)
    return (r * math.sin(a), r * math.cos(a), z)


def render_bg(stage, glow=0.0, post=None):
    ss = 2
    bg_camera(ss)
    t0 = time.time()
    sc = bpy.context.scene
    tmp = os.path.join(TMP, "bg.exr")
    sc.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    pm = downsample_pm(load_exr(tmp), ss)
    if glow > 0:
        pm = add_glow(pm, glow, sigmas=(1.5, 4.0, 10.0))
    if post:
        pm = post(pm)
    out = pm_to_straight_srgb(pm)
    write_png(os.path.join(OUT, "bg_%s.png" % stage), to_u8(bleed(out)))
    print("  bg_%s in %.1f s" % (stage, time.time() - t0))
    return dict(file="bg_%s.png" % stage, size=[BG_W, BG_H], lat_deg=[0.0, BG_LAT],
                px_per_deg=round(BG_W / 360.0, 4), centre_column_looks="+Y",
                render_s=round(time.time() - t0, 1))


def bg_scene(light_kind, sun_from=None, elev=None, strength=None, col=None, world=None):
    reset_scene()
    _MATS.clear()
    L = LIGHTS[light_kind]
    w = world or (L["sky_top"], L["sky_hor"], L["sky"], L["ground"])
    set_world(*w)
    add_sun(sun_vec(sun_from or L["sun_from"], elev or L["elev"]), strength or L["sun"], col or L["sun_col"])


def bg_city():
    bg_scene("day", sun_from=(-1, -0.4), elev=40)
    rng = np.random.RandomState(31)
    fog = "#b8cbe0"
    hill = add_fog(M_noise("#4a7a3a", "#6a9a48", scale=0.004, bump=0.3, bump_dist=20, rough=0.9, name="hill"),
                   fog, 1800, 0.85)
    n1 = lambda TH: fbm1(rng, TH, 5, 2)
    def hh(TH, R):
        t = (R - 2500) / 3500
        prof = np.sin(np.clip(t, 0, 1) * math.pi) ** 0.7
        return (330 + 300 * n1(TH)) * prof - 20
    ring_terrain("hills", hill, hh, 2500, 6000, nth=1536, nr=24)
    # towers: a downtown cluster ahead-left, a smaller one behind-right, scattered blocks
    clusters = [(-25, 700, 34, 1.6), (140, 800, 24, 1.2), (70, 900, 12, 0.9), (-120, 900, 16, 1.0)]
    mats = [add_fog(M_grid(w, g, cell=(4.0, 4.0), frac=(0.6, 0.55), wall2=w, win_var=0.3, name="bldg"), fog, 900, 0.8)
            for w, g in (("#c8ccd0", "#5a7898"), ("#a8b4c0", "#3a5a80"), ("#d8c8b0", "#6a7a8a"),
                         ("#8a98a8", "#2a4a70"), ("#b87a5a", "#4a5a6a"))]
    for (a0, r0, n, hs) in clusters:
        for i in range(n):
            a = a0 + rng.normal(0, 9)
            r = r0 * rng.uniform(0.85, 1.25)
            wv = rng.uniform(22, 50)
            h = (rng.uniform(60, 180) + (180 if rng.random() < 0.25 else 0)) * hs * (1.3 - abs(a - a0) / 30)
            h = max(h, 30)
            ob = box("t", at(a, r, h / 2), (wv, wv * rng.uniform(0.8, 1.2), h), mats[i % len(mats)])
            ob.rotation_euler = (0, 0, rng.uniform(0, 1.5))
            if rng.random() < 0.3:
                cyl("sp", at(a, r, h), at(a, r, h + h * 0.2), 2.0, mats[0], n=6)
    # low-rise sprawl all around
    for i in range(260):
        a = rng.uniform(-180, 180)
        r = rng.uniform(1000, 1500)
        wv = rng.uniform(30, 70)
        h = rng.uniform(15, 60) * (1 + 0.8 * (rng.random() < 0.1))
        ob = box("lr", at(a, r, h / 2), (wv, wv, h), mats[(i + 2) % len(mats)])
        ob.rotation_euler = (0, 0, math.radians(-a))
    return render_bg("city")


def bg_coast():
    bg_scene("day", sun_from=(-1, -0.2), elev=38)
    rng = np.random.RandomState(12)
    fog = "#c4d8ea"
    # the sea: a cylinder wall that fills the bottom ~3 degrees, darker low, pale at its top edge
    sea = bpy.data.materials.new("sea")
    nt, N, L = _nodes(sea)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    sep = N.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])
    mr = N.new("ShaderNodeMapRange")
    mr.inputs[1].default_value = 0.0
    mr.inputs[2].default_value = 1.0
    L.new(sep.outputs["Z"], mr.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = hexcol("#1f6aa8")
    ramp.color_ramp.elements[1].color = hexcol("#9cc8e4")
    e = ramp.color_ramp.elements.new(0.75)
    e.color = hexcol("#4a90c8")
    L.new(mr.outputs[0], ramp.inputs[0])
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 60
    L.new(tc.outputs["Generated"], nz.inputs[0])
    mix = N.new("ShaderNodeMixRGB"); mix.blend_type = "OVERLAY"
    mix.inputs[0].default_value = 0.25
    L.new(ramp.outputs[0], mix.inputs[1]); L.new(nz.outputs["Fac"], mix.inputs[2])
    em = N.new("ShaderNodeEmission")
    L.new(mix.outputs[0], em.inputs[0])
    em.inputs[1].default_value = 1.0
    L.new(em.outputs[0], out.inputs["Surface"])
    Rs = 3000.0
    zs = Rs * math.tan(math.radians(3.2))
    ob = cyl("sea", (0, 0, 0), (0, 0, zs), Rs, sea, n=256, caps=False)
    ob.scale = (1, 1, 1)
    # object Z of the cylinder runs -zs/2..zs/2 -> remap via mapping in material: shift
    mr.inputs[1].default_value = -zs / 2
    mr.inputs[2].default_value = zs / 2
    for p in ob.data.polygons:
        p.use_smooth = True
    # land: islands and a far headland (green tops, pale rock cliffs)
    land = add_fog(M_noise("#8a7a64", "#b8a88a", scale=0.01, bump=0.4, bump_dist=10, snow=1.0,
                           snow_col="#4f8a38", snow_lo=0.5, snow_hi=0.7, rough=0.9, name="land"), fog, 4500, 0.8)

    inoise = Noise3(rng, 1 / 500.0)

    def isl(TH, R):
        z = np.zeros_like(TH)
        for (a, w, h, rc, rw) in ((-40, 16, 1100, 5200, 1400), (-8, 5, 650, 4200, 600), (22, 3.5, 420, 4600, 500),
                                  (48, 8, 800, 6000, 900), (160, 48, 1700, 5500, 2200), (-150, 10, 950, 5000, 900),
                                  (95, 4, 500, 4800, 500), (-95, 6, 600, 4400, 600)):
            dth = np.angle(np.exp(1j * (TH - math.radians(a))))
            u = (dth / math.radians(w)) ** 2 + ((R - rc) / rw) ** 2
            m = np.clip(1 - u, 0, 1)
            z = np.maximum(z, h * (m ** 0.45))
        P = np.stack([(R * np.sin(TH)).ravel(), (R * np.cos(TH)).ravel(), np.zeros(TH.size)], axis=1)
        z = z * (1 + 0.22 * inoise(P).reshape(TH.shape))
        return z - 30
    ring_terrain("land", land, isl, 3300, 8000, nth=2048, nr=64)
    return render_bg("coast")


def bg_desert():
    bg_scene("day", sun_from=(-1, -0.5), elev=35, col=(1.0, 0.9, 0.78))
    rng = np.random.RandomState(7)
    fog = "#f0c8a0"
    rockm = add_fog(M_noise("#a8452a", "#d8804a", scale=0.02, bump=0.4, bump_dist=8, strata=0.5,
                            strata_col="#7a3020", strata_scale=0.025, snow=1.0, snow_col="#d89060",
                            snow_lo=0.6, snow_hi=0.8, rough=0.95, name="mesa"), fog, 3000, 0.75, 1.2)
    sand = add_fog(M_noise("#c8864a", "#e0a868", scale=0.01, bump=0.2, rough=0.95, name="sand"), fog, 2000, 0.8)
    mesas = [(-30, 8, 800, 3200, 500), (-12, 3, 900, 2400, 250), (15, 11, 650, 4200, 700), (40, 2.5, 760, 2600, 200),
             (75, 15, 900, 5200, 900), (120, 7, 850, 3000, 400), (165, 13, 800, 4500, 800), (-160, 5, 850, 2800, 300),
             (-110, 18, 900, 5000, 1000), (-70, 4, 700, 3500, 300), (-50, 1.6, 760, 2200, 120), (95, 3, 600, 2300, 200),
             (-140, 2.2, 700, 2500, 160)]

    def mesa(TH, R):
        z = np.zeros_like(TH)
        nn = fbm1(rng, TH * 1.0, 4, 11)
        for (a, w, h, rc, rw) in mesas:
            dth = np.angle(np.exp(1j * (TH - math.radians(a))))
            d = np.sqrt((dth / math.radians(w)) ** 2 + ((R - rc) / rw) ** 2) + 0.06 * nn
            cliff = np.clip((1 - d) / 0.07, 0, 1)
            talus = np.clip((1.5 - d) / 0.5, 0, 1) ** 1.8
            z = np.maximum(z, np.maximum(h * cliff, h * 0.35 * talus))
        return z - 10
    ring_terrain("mesas", rockm, mesa, 1800, 6500, nth=2048, nr=72)

    def dunes(TH, R):
        t = np.clip((R - 1200) / 1500, 0, 1)
        return (18 + 14 * fbm1(rng, TH, 5, 9)) * np.sin(t * math.pi) - 5
    ring_terrain("dunes", sand, dunes, 1200, 2700, nth=2048, nr=12)

    def haze(pm):
        # heat shimmer: offset the lowest rows sideways by a small wave
        out = pm.copy()
        for y in range(BG_H - 14, BG_H):
            k = (y - (BG_H - 14)) / 14.0
            sh = int(round(1.2 * k * math.sin(y * 1.7)))
            out[y] = np.roll(pm[y], sh, axis=0)
        return out
    return render_bg("desert", post=haze)


def bg_mountain():
    moon_az, moon_el = 30.0, 26.0
    bg_scene("night", world=("#0a1430", "#1f3560", 0.5, "#101a30"))
    # the moon lights the peaks from where it stands
    for o in list(bpy.context.scene.objects):
        if o.type == "LIGHT":
            bpy.data.objects.remove(o)
    ma = math.radians(moon_az)
    add_sun(sun_vec((math.sin(ma), math.cos(ma)), moon_el), 1.6, (0.7, 0.8, 1.0), angle_deg=1.0)
    rng = np.random.RandomState(5)
    fog = "#1c2c50"
    peaks = add_fog(M_noise("#1a2238", "#3a4660", scale=0.004, bump=0.5, bump_dist=15, snow=1.0,
                            snow_col="#dfe8ff", snow_lo=0.25, snow_hi=0.42, rough=0.8, name="peaks"),
                    fog, 9000, 0.7)
    peaks2 = add_fog(M_noise("#141a2c", "#262e44", scale=0.004, bump=0.5, bump_dist=15, snow=1.0,
                             snow_col="#b8c8e8", snow_lo=0.3, snow_hi=0.5, rough=0.85, name="peaks2"),
                     fog, 6000, 0.75)
    def ridged2(X, Y, rng_, freq, octaves):
        P = np.stack([X.ravel(), Y.ravel(), np.zeros(X.size)], axis=1)
        out = np.zeros(X.size)
        amp, tot, f = 1.0, 0.0, freq
        for _ in range(octaves):
            nz = Noise3(rng_, f, octaves=8)
            out += amp * (1 - np.abs(nz(P) / 1.5)) ** 2
            tot += amp
            amp *= 0.5
            f *= 2.1
        return (out / tot).reshape(X.shape)

    def far(TH, R):
        t = (R - 7000) / 9000
        prof = np.clip(np.sin(np.clip(t, 0, 1) * math.pi), 0, 1) ** 0.5
        k = ridged2(R * np.sin(TH), R * np.cos(TH), rng, 1 / 2600.0, 5)
        k = (k - k.min()) / (k.max() - k.min())
        return (300 + 5200 * k ** 1.6) * prof - 100
    ring_terrain("far", peaks, far, 7000, 16000, nth=2048, nr=110)

    def near(TH, R):
        t = (R - 2000) / 3000
        prof = np.clip(np.sin(np.clip(t, 0, 1) * math.pi), 0, 1) ** 0.6
        k = ridged2(R * np.sin(TH), R * np.cos(TH), rng, 1 / 1100.0, 4)
        k = (k - k.min()) / (k.max() - k.min())
        return (100 + 1300 * k ** 1.6) * prof - 60
    ring_terrain("near", peaks2, near, 2000, 5000, nth=2048, nr=70)
    # pine forest line on the nearest ridge: dark bumps
    # the moon, with a halo from the glow pass
    sphere("moon", at(moon_az, 20000, 20000 * math.tan(math.radians(moon_el))), 1100,
           M_noise("#c8d0e0", "#fffaf0", scale=0.004, bump=0.0, rough=1.0, name="moon",
                   emit_col="#fff6e0", emit_str=2.4), subdiv=4)
    # a few warm village lights at the foot of the near mountains
    lit = M_emit("#ffb050", 12.0, "village")
    for i in range(18):
        a = rng.uniform(-60, 80) if i < 12 else rng.uniform(120, 200)
        r = rng.uniform(1800, 2600)
        sphere("vl", at(a, r, rng.uniform(2, 15)), 4.0, lit, subdiv=1)
    return render_bg("mountain", glow=1.0)


def M_nebula(c1, c2, scale, density, strength, seed):
    mat = bpy.data.materials.new("nebula")
    nt, N, L = _nodes(mat)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    mp.inputs["Location"].default_value = (seed * 1.3, seed * 0.7, 0)
    L.new(tc.outputs["Object"], mp.inputs[0])
    nz = N.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = scale
    nz.inputs["Detail"].default_value = 8
    nz.inputs["Roughness"].default_value = 0.62
    nz.inputs["Distortion"].default_value = 1.2
    L.new(mp.outputs[0], nz.inputs[0])
    mr = N.new("ShaderNodeMapRange")
    mr.inputs[1].default_value = 1 - density
    mr.inputs[2].default_value = 1.0
    L.new(nz.outputs["Fac"], mr.inputs[0])
    pw = N.new("ShaderNodeMath"); pw.operation = "POWER"
    L.new(mr.outputs[0], pw.inputs[0]); pw.inputs[1].default_value = 1.6
    ramp = N.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = hexcol(c1)
    ramp.color_ramp.elements[1].color = hexcol(c2)
    L.new(nz.outputs["Color"], ramp.inputs[0])
    sepc = N.new("ShaderNodeSeparateRGB")
    L.new(nz.outputs["Color"], sepc.inputs[0])
    L.new(sepc.outputs["G"], ramp.inputs[0])
    em = N.new("ShaderNodeEmission")
    L.new(ramp.outputs[0], em.inputs[0])
    em.inputs[1].default_value = strength
    tr = N.new("ShaderNodeBsdfTransparent")
    ms = N.new("ShaderNodeMixShader")
    L.new(pw.outputs[0], ms.inputs[0])
    L.new(tr.outputs[0], ms.inputs[1])
    L.new(em.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    mat.blend_method = "BLEND"
    return mat


def bg_space():
    bg_scene("space", world=("#000000", "#000000", 0.0, "#000000"))
    for o in list(bpy.context.scene.objects):
        if o.type == "LIGHT":
            bpy.data.objects.remove(o)
    pa, pe = -22.0, 24.0         # planet azimuth / elevation
    # the star lights the planet from its right and a little behind
    sa = math.radians(pa + 100)
    add_sun(sun_vec((math.sin(sa), math.cos(sa)), 35), 4.0, (1.0, 0.95, 0.9), angle_deg=0.5)
    D = 20000.0
    Rp = D * math.tan(math.radians(14))
    c = Vector(at(pa, D, D * math.tan(math.radians(pe))))
    # banded gas giant
    pl = bpy.data.materials.new("planet")
    nt, N, L = _nodes(pl)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    wv = N.new("ShaderNodeTexWave")
    wv.wave_type = "BANDS"
    wv.bands_direction = "Z"
    wv.inputs["Scale"].default_value = 4.0
    wv.inputs["Distortion"].default_value = 1.5
    wv.inputs["Detail"].default_value = 4
    wv.inputs["Detail Scale"].default_value = 2.0
    L.new(tc.outputs["Generated"], wv.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.elements[0].color = hexcol("#6a2a8a")
    cr.elements[1].color = hexcol("#ffb880")
    for pos, col in ((0.3, "#c04a8a"), (0.55, "#f08a70"), (0.8, "#ffd8a0")):
        e = cr.elements.new(pos)
        e.color = hexcol(col)
    L.new(wv.outputs["Fac"], ramp.inputs[0])
    b = N.new("ShaderNodeBsdfPrincipled")
    b.inputs["Roughness"].default_value = 0.7
    L.new(ramp.outputs[0], b.inputs["Base Color"])
    # a faint glow of its own so the night side is not black
    L.new(ramp.outputs[0], b.inputs["Emission"])
    b.inputs["Emission Strength"].default_value = 0.06
    L.new(b.outputs[0], out.inputs["Surface"])
    ob = uvsphere("planet", tuple(c), Rp, pl, u=96, v=48)
    tilt = (math.radians(-2), math.radians(20), math.radians(-pa))
    ob.rotation_euler = tilt
    # rings: an annulus with banded alpha
    rm = bpy.data.materials.new("rings")
    nt, N, L = _nodes(rm)
    out = N.new("ShaderNodeOutputMaterial")
    tc = N.new("ShaderNodeTexCoord")
    ln = N.new("ShaderNodeVectorMath"); ln.operation = "LENGTH"
    L.new(tc.outputs["Object"], ln.inputs[0])
    mr = N.new("ShaderNodeMapRange")
    mr.inputs[1].default_value = Rp * 1.35
    mr.inputs[2].default_value = Rp * 2.3
    L.new(ln.outputs["Value"], mr.inputs[0])
    rr = N.new("ShaderNodeValToRGB")
    cr = rr.color_ramp
    cr.elements[0].position = 0.0
    cr.elements[0].color = (0, 0, 0, 1)
    cr.elements[1].position = 1.0
    cr.elements[1].color = (0, 0, 0, 1)
    for pos, v in ((0.05, 0.5), (0.2, 0.85), (0.33, 0.25), (0.38, 0.9), (0.55, 0.7), (0.62, 0.05), (0.7, 0.6),
                   (0.85, 0.45), (0.95, 0.15)):
        e = cr.elements.new(pos)
        e.color = (v, v, v, 1)
    L.new(mr.outputs[0], rr.inputs[0])
    col = N.new("ShaderNodeValToRGB")
    col.color_ramp.elements[0].color = hexcol("#ffd0a8")
    col.color_ramp.elements[1].color = hexcol("#a878d0")
    L.new(mr.outputs[0], col.inputs[0])
    b = N.new("ShaderNodeBsdfDiffuse")
    L.new(col.outputs[0], b.inputs["Color"])
    em = N.new("ShaderNodeEmission")
    L.new(col.outputs[0], em.inputs[0])
    em.inputs[1].default_value = 0.9
    ad = N.new("ShaderNodeAddShader")
    L.new(b.outputs[0], ad.inputs[0]); L.new(em.outputs[0], ad.inputs[1])
    tr = N.new("ShaderNodeBsdfTransparent")
    ms = N.new("ShaderNodeMixShader")
    L.new(rr.outputs[0], ms.inputs[0])
    L.new(tr.outputs[0], ms.inputs[1])
    L.new(ad.outputs[0], ms.inputs[2])
    L.new(ms.outputs[0], out.inputs["Surface"])
    rm.blend_method = "BLEND"
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=False, segments=256, radius=Rp * 2.3)
    inner = bmesh.ops.create_circle(bm, cap_ends=False, segments=256, radius=Rp * 1.35)
    bm.free()
    nseg = 256
    V, F = [], []
    for k in range(nseg):
        a = 2 * math.pi * k / nseg
        V.append((Rp * 1.35 * math.cos(a), Rp * 1.35 * math.sin(a), 0))
        V.append((Rp * 2.3 * math.cos(a), Rp * 2.3 * math.sin(a), 0))
    for k in range(nseg):
        a0, a1 = 2 * k, 2 * ((k + 1) % nseg)
        F.append([a0, a1, a1 + 1, a0 + 1])
    ring = mesh("rings", V, F, rm)
    ring.location = c
    ring.rotation_euler = tilt
    # a small moon
    sphere("moonlet", at(pa + 32, D, D * math.tan(math.radians(pe + 14))), Rp * 0.12,
           M_noise("#6a6070", "#b0a8b8", scale=0.002, rough=1.0, name="moonlet"), subdiv=4)
    # nebula wisps on shells around the camera (emission + transparency -> soft alpha)
    for i, (c1, c2, sc_, den, st, rad) in enumerate((("#3010a0", "#ff30b0", 0.00004, 0.55, 3.0, 60000),
                                                      ("#0040c0", "#30e0ff", 0.00006, 0.45, 2.6, 55000))):
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=5, radius=rad)
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
        _obj_from_bm("neb%d" % i, bm, M_nebula(c1, c2, sc_ * 1000 / 1000, den, st, 3 + i), True)
    return render_bg("space", glow=0.6)


BACKDROPS = {"city": bg_city, "coast": bg_coast, "desert": bg_desert, "mountain": bg_mountain,
             "space": bg_space}

# Colours the watch uses around the sprites (sRGB hex). ground_a/b alternate by
# road segment; shoulder = the strip just off the tarmac; fog = distance tint;
# preview_scroll = the backdrop column props_preview.py puts at the left edge.
SKY = {
    "city": dict(sky_top="#3f78cc", sky_horizon="#c6d8ec", fog="#b8cbe0", ground_a="#5c9a3c",
                 ground_b="#548f36", shoulder="#9a968c", road_a="#6c6c70", road_b="#666669",
                 rumble_a="#d82020", rumble_b="#f0f0f0", line="#f2f2f2", preview_scroll=257),
    "coast": dict(sky_top="#2f86d8", sky_horizon="#c8e2f4", fog="#c4d8ea", ground_a="#e8d4a0",
                  ground_b="#dcc690", sand="#e8d4a0", sea="#2a78b8", shoulder="#c8b888",
                  road_a="#707074", road_b="#6a6a6e", rumble_a="#f0f0f0", rumble_b="#2a70d0",
                  line="#f2f2f2", preview_scroll=250),
    "desert": dict(sky_top="#3a7ad0", sky_horizon="#f0d8b8", fog="#f0c8a0", ground_a="#d89a58",
                   ground_b="#cc8e50", sand="#e0aa68", shoulder="#b87a48", road_a="#6a6664",
                   road_b="#64605e", rumble_a="#e02020", rumble_b="#f0f0f0", line="#f4d040",
                   preview_scroll=300),
    "mountain": dict(sky_top="#050a1c", sky_horizon="#1c2c50", fog="#1c2c50", ground_a="#8aa0c8",
                     ground_b="#7e94bc", snow="#9ab0d8", shoulder="#5a6480", road_a="#2a2e3a",
                     road_b="#262a34", rumble_a="#c02030", rumble_b="#c8d0e0", line="#e0d8a0",
                     lamp_pool="#ffb060", preview_scroll=400),
    "space": dict(sky_top="#05020e", sky_horizon="#1a0a36", fog="#1a0a36", ground_a="#1a0a36",
                  ground_b="#140828", void="#05020e", shoulder="#30e0ff", road_a="#2a2440",
                  road_b="#242038", rumble_a="#ff30d0", rumble_b="#30e0ff", line="#30e0ff",
                  preview_scroll=270, note="the road floats: no ground, the watch draws stars"),
}

# ---------------------------------------------------------------------------
# Main


def main():
    only = [s.strip() for s in ARGS.only.split(",") if s.strip()]
    stages = [s.strip() for s in ARGS.stage.split(",") if s.strip()] or STAGES
    meta_path = os.path.join(OUT, "meta.json")
    meta = {}
    if os.path.exists(meta_path):
        try:
            with open(meta_path) as f:
                meta = json.load(f)
        except Exception:
            meta = {}
    meta["_about"] = {
        "camera": "pinhole at (0,0,2) looking +Y, no pitch; the prop's base-centre at (0,40,0); "
                  "ppm = pixels per metre at the base plane Y=40 (focal_px = 40*ppm)",
        "anchor": "pixel (x right, y down, pixel-centre convention) of the base-centre ground point",
        "side": "modelled for the RIGHT side of the road (road towards -X); mirror for the left",
        "shadow": "L 0..255 = how much the prop darkens the ground; same focal, own size, "
                  "shadow_anchor = the same ground point in the shadow image",
        "alpha": "straight alpha; transparent pixels carry the nearest edge colour",
        "backdrops": "bg_<stage>.png 1024x160, 360 deg equirectangular, horizon = bottom row, "
                     "square angular pixels (2.844 px/deg), column 512 looks along +Y, +X at 768",
    }
    t_all = time.time()
    if not ARGS.no_props:
        for name, (b, s, spec) in PROPS.items():
            if only and name not in only:
                continue
            if not only and spec["stage"] not in stages:
                continue
            meta.setdefault("props", {})[name] = run_prop(name)
            with open(meta_path, "w") as f:
                json.dump(meta, f, indent=1)
    if not ARGS.no_bg:
        for st, fn in BACKDROPS.items():
            if only and "bg_" + st not in only:
                continue
            if not only and st not in stages:
                continue
            meta.setdefault("backdrops", {})[st] = fn()
            with open(meta_path, "w") as f:
                json.dump(meta, f, indent=1)
    with open(os.path.join(OUT, "sky.json"), "w") as f:
        json.dump(SKY, f, indent=1)
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=1)
    print("meta ->", meta_path, "total %.1f s" % (time.time() - t_all))
    shutil.rmtree(TMP, ignore_errors=True)


if __name__ == "__main__":
    main()
