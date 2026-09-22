"""Monster Hop - the four bosses (SPEC.md section 5, "Bosses").

    brute    a huge zombie in a torn suit and a crooked hard hat, 2 x 2
    count    the vampire lord, 1 x 1 with a wide cape
    pharaoh  a giant mummy king with a striped headdress and a crook, 2 x 2
    alpha    the werewolf pack leader, 2 x 2

Everything is built here from primitives (no .blend), posed by a small FK/IK
rig (bosses_geo.py) and rendered with mh_common: light + id + z + shadow under
the neutral light, like every recoloured character. The watch colours them
with the palettes in <out>/palettes.json.

    Blender -b -P bosses.py -- --out ../../assets/bosses [--sample] [--only brute,count_glide]
                                [--samples 48] [--dirs s,e]

--sample renders only the style sample frames; --only takes boss names,
boss_anim or full frame names; --dirs limits the facings.
"""
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mh_common as C  # noqa: E402
import bosses_geo as G  # noqa: E402
from bosses_geo import (V, Matrix, rot, rad, lerp, smooth, sphere, tube, lathe, lathe_arc, strip, cone, box,  # noqa: E402,F401
                        band_on, tube_band, resample)
import bpy  # noqa: E402

DIRS = {'s': 0.0, 'e': 90.0, 'n': 180.0, 'w': -90.0}

# ---------------------------------------------------------------------------
# Palettes (id -> sRGB). Shared monster ids: 1 skin/fur/wrap A, 2 B, 3 dark,
# 4 white, 5 eye glow, 6 hair, 7-9 clothes, 10 shoes, 11-12 accessories.
# ---------------------------------------------------------------------------

PALETTES = {
    'brute': {1: [138, 168, 124], 2: [96, 124, 88], 3: [34, 30, 36], 4: [240, 234, 212],
              5: [214, 255, 72], 6: [74, 62, 52], 7: [74, 84, 112], 8: [212, 206, 186],
              9: [66, 74, 98], 10: [118, 76, 44], 11: [255, 196, 32], 12: [212, 52, 48]},
    'count': {1: [218, 204, 238], 2: [176, 156, 208], 3: [28, 20, 38], 4: [250, 250, 255],
              5: [255, 44, 52], 6: [34, 28, 50], 7: [40, 30, 62], 8: [206, 28, 50],
              9: [96, 44, 124], 10: [34, 28, 44], 11: [255, 204, 64], 12: [224, 32, 64]},
    'pharaoh': {1: [230, 216, 180], 2: [182, 160, 122], 3: [40, 28, 22], 4: [250, 245, 230],
                5: [96, 255, 212], 7: [244, 238, 218], 8: [196, 64, 44], 11: [244, 186, 44],
                12: [36, 84, 204]},
    'alpha': {1: [168, 174, 186], 2: [226, 229, 234], 3: [30, 30, 38], 4: [246, 244, 236],
              5: [255, 222, 40], 6: [120, 126, 142], 7: [132, 64, 40], 8: [70, 84, 120],
              11: [92, 64, 44]},
}

IDS = {
    'brute': {1: 'skin', 2: 'skin B (lips, patch)', 3: 'dark (mouth, brow)', 4: 'teeth',
              5: 'eye glow', 6: 'hair tuft', 7: 'suit jacket', 8: 'shirt', 9: 'trousers',
              10: 'boots', 11: 'hard hat', 12: 'tie'},
    'count': {1: 'skin', 2: 'skin B (ears inside, lips)', 3: 'dark (brows, mouth)',
              4: 'white (fangs, cravat, cuffs)', 5: 'eye glow', 6: 'hair', 7: 'cape outer + collar',
              8: 'cape inner (red)', 9: 'waistcoat, sleeves', 10: 'trousers, boots',
              11: 'medallion + chain', 12: 'medallion gem'},
    'pharaoh': {1: 'wraps A', 2: 'wraps B (overlapping, loose ends)', 3: 'dark (eye sockets, mouth)',
                4: 'white', 5: 'eye glow', 7: 'kilt', 8: 'apron', 11: 'gold (headdress, mask, collar, crook)',
                12: 'blue stripes'},
    'alpha': {1: 'fur', 2: 'fur B (muzzle, chest, ruff, ear inside)', 3: 'dark (nose, mouth, brows)',
              4: 'white (teeth, claws)', 5: 'eye glow', 6: 'fur C (back stripe, ear tips)', 7: 'vest',
              8: 'shorts', 11: 'belt'},
}

# ---------------------------------------------------------------------------
# Materials (neutral grey in the light pass; bump carries the texture)
# ---------------------------------------------------------------------------


def _bumped(rough=0.6, spec=0.5, tex='noise', scale=20.0, strength=0.25, dist=0.0,
            stretch=(1, 1, 1), detail=2.0, clearcoat=0.0):
    def build(nt, neutral):
        b = nt.nodes.new('ShaderNodeBsdfPrincipled')
        b.inputs['Base Color'].default_value = (0.8, 0.8, 0.8, 1)
        b.inputs['Roughness'].default_value = rough
        b.inputs['Specular'].default_value = spec
        b.inputs['Clearcoat'].default_value = clearcoat
        tc = nt.nodes.new('ShaderNodeTexCoord')
        mp = nt.nodes.new('ShaderNodeMapping')
        mp.inputs['Scale'].default_value = stretch
        nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
        if tex == 'noise':
            t = nt.nodes.new('ShaderNodeTexNoise')
            t.inputs['Scale'].default_value = scale
            t.inputs['Detail'].default_value = detail
            t.inputs['Distortion'].default_value = dist
            out = t.outputs['Fac']
        else:  # bands along Z (bandages)
            t = nt.nodes.new('ShaderNodeTexWave')
            t.wave_type = 'BANDS'
            t.bands_direction = 'Z'
            t.wave_profile = 'SAW'
            t.inputs['Scale'].default_value = scale
            t.inputs['Distortion'].default_value = dist
            t.inputs['Detail'].default_value = detail
            out = t.outputs['Fac']
        nt.links.new(mp.outputs['Vector'], t.inputs['Vector'])
        bu = nt.nodes.new('ShaderNodeBump')
        bu.inputs['Strength'].default_value = strength
        bu.inputs['Distance'].default_value = 0.02
        nt.links.new(out, bu.inputs['Height'])
        nt.links.new(bu.outputs['Normal'], b.inputs['Normal'])
        return b.outputs['BSDF']
    return build


def materials(kind):
    """Register the material keys every boss uses (ids are the shared ones)."""
    cloth = _bumped(0.8, 0.3, 'noise', 34.0, 0.18)
    C.mat('skin', rough=0.55, spec=0.45, id=1, build=_bumped(0.55, 0.45, 'noise', 14.0, 0.10))
    C.mat('skin2', rough=0.6, id=2)
    C.mat('dark', rough=0.45, spec=0.4, id=3)
    C.mat('white', rough=0.25, spec=0.7, id=4)
    C.mat('eye', emit=(1.0, 1.0, 1.0), emit_strength=6.0, rough=0.3, id=5)
    C.mat('hair', rough=0.4, spec=0.6, id=6, build=_bumped(0.4, 0.6, 'noise', 30.0, 0.25,
                                                         stretch=(1, 1, 0.3)))
    C.mat('c7', id=7, build=cloth)
    C.mat('c8', id=8, build=cloth)
    C.mat('c9', id=9, build=cloth)
    C.mat('boot', rough=0.45, spec=0.5, id=10)
    C.mat('a11', rough=0.3, spec=0.8, id=11)
    C.mat('a12', rough=0.35, spec=0.6, id=12)
    if kind == 'alpha':
        fur = _bumped(0.75, 0.35, 'noise', 22.0, 0.45, dist=2.0, stretch=(1.4, 1.4, 0.5), detail=4.0)
        C.mat('skin', id=1, build=fur)
        C.mat('skin2', id=2, build=fur)
        C.mat('hair', id=6, build=fur)
    if kind == 'pharaoh':
        wrap = _bumped(0.85, 0.3, 'wave', 7.0, 0.55, dist=1.2, detail=1.0)
        C.mat('skin', id=1, build=wrap)
        C.mat('skin2', id=2, build=wrap)
        C.mat('a11', rough=0.28, spec=0.9, id=11, build=_bumped(0.28, 0.9, 'noise', 40.0, 0.04))


# ---------------------------------------------------------------------------
# A boss: rig + rigid parts + per-frame deformables
# ---------------------------------------------------------------------------

class Boss:
    name = ''
    height = 1.0
    footprint = (1, 1)
    anims = []           # (anim, frames, ms, dirs)

    def __init__(self):
        self.rig = G.Rig()
        self.parts = []      # (object, bone, tag)
        self.tmp = []

    @property
    def anchor(self):
        return V((1.0, 1.0, 0.0)) if self.footprint == (2, 2) else C.cell(0, 0, 0)

    part_scale = {}      # bone -> uniform scale of its parts about the bone head

    def part(self, name, g, keys, bone, sub=1, tag=None):
        ob = G.make_obj('%s_%s' % (self.name, name), g, keys, sub, local_origin=self.rig.head[bone])
        self.parts.append((ob, bone, tag))
        return ob

    def mirror(self, fn):
        """Call fn(side, sx) for the left (+X, 'L', sx=1) and right side."""
        for s, sx in (('L', 1.0), ('R', -1.0)):
            fn(s, sx)

    # per frame --------------------------------------------------------------
    def show(self, tag, P):
        return True

    def deform(self, P):
        return []

    def place(self, P, yaw):
        self.rig.solve(P)
        W = Matrix.Translation(self.anchor) @ Matrix.Rotation(rad(yaw), 4, 'Z')
        objs = []
        for ob, bone, tag in self.parts:
            ob.matrix_world = W @ self.rig.M[bone] @ Matrix.Scale(self.part_scale.get(bone, 1.0), 4)
            if self.show(tag, P):
                objs.append(ob)
        self.tmp = []
        for g, keys, sub, nm in self.deform(P):
            ob = G.make_obj('%s_def_%s' % (self.name, nm), g, keys, sub)
            ob.matrix_world = W
            self.tmp.append(ob)
        return objs + self.tmp

    def clear(self):
        G.drop(self.tmp)
        self.tmp = []


def cyc(f, n, ph=0.0):
    return 2 * math.pi * (f / n) + ph


def jag(amp, k, ph=0.0):
    """Torn hem offset: a sawtooth of k teeth around the ring."""
    def fn(th):
        x = (th / (2 * math.pi) * k + ph) % 1.0
        return amp * (abs(2 * x - 1) - 0.5) * 2
    return fn


# ===========================================================================
# BRUTE - a huge zombie, 2.0 m, 2 x 2
# ===========================================================================

class Brute(Boss):
    name = 'brute'
    height = 2.0
    footprint = (2, 2)
    anims = [('walk', 6, 140, 'nesw'), ('stomp', 6, 100, 'nesw')]

    def build(self):
        r = self.rig
        r.bone('pelvis', None, (0, 0.05, 0.66))
        r.bone('spine', 'pelvis', (0, 0.05, 0.84))
        r.bone('head', 'spine', (0, -0.34, 1.48))
        for s, sx in (('L', 1), ('R', -1)):
            r.bone('sh' + s, 'spine', (0.62 * sx, 0.04, 1.42))
            r.bone('el' + s, 'sh' + s, (0.75 * sx, 0.02, 1.00))
            r.bone('fi' + s, 'el' + s, (0.79 * sx, -0.05, 0.60))
            r.bone('th' + s, 'pelvis', (0.30 * sx, 0.05, 0.62))
            r.bone('kn' + s, 'th' + s, (0.32 * sx, 0.01, 0.35))
            r.bone('an' + s, 'kn' + s, (0.33 * sx, 0.06, 0.13))

        # --- torso: the shirt under a torn, open jacket -------------------
        T = [(0.54, 0, 0, 0, 0.05), (0.57, 0.30, 0.26, 0, 0.04), (0.66, 0.47, 0.39, 0, 0.02),
             (0.80, 0.55, 0.45, 0, 0.0), (0.98, 0.59, 0.48, 0, -0.03), (1.15, 0.62, 0.47, 0, -0.02),
             (1.30, 0.64, 0.44, 0, 0.0), (1.40, 0.62, 0.41, 0, 0.03), (1.48, 0.52, 0.36, 0, 0.06),
             (1.54, 0.36, 0.28, 0, 0.08), (1.58, 0.18, 0.16, 0, 0.08), (1.60, 0, 0, 0, 0.08)]
        self.part('torso', lathe(T, 32), 'c8', 'spine')

        def sc(rg, k=1.035, d=0.014):
            z, rx, ry, cx, cy = rg
            return (z, rx * k + d, ry * k + d, cx, cy) if rx > 0 else rg
        J = [sc(t) for t in T[2:10]]
        top = [sc(t) for t in T[7:]]
        self.part('jacket_top', lathe(top, 32), 'c7', 'spine')
        hem = jag(0.06, 7)

        def jz(i, th):
            return hem(th) if i == 0 else 0.0
        # open at the front: half-width of the opening per ring (belly wide, V to the neck)
        opens = [30, 27, 24, 20, 16, 12, 8, 4]

        g = G.Geo()
        n = 36
        rows = []
        for i, (z, rx, ry, cx, cy) in enumerate(J):
            a0 = rad(-90 + opens[i])
            a1 = rad(270 - opens[i])
            idx = []
            for j in range(n + 1):
                th = lerp(a0, a1, j / n)
                idx.append(g.vert((cx + rx * math.cos(th), cy + ry * math.sin(th), z + jz(i, th))))
            rows.append(idx)
        for i in range(len(rows) - 1):
            for j in range(n):
                g.face((rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]))
        self.part('jacket', g, 'c7', 'spine')
        # lapels: two flat folds along the opening, top half
        for sx in (1, -1):
            pts, ws = [], []
            for i in (3, 4, 5, 6, 7):
                z, rx, ry, cx, cy = J[i]
                th = rad(-90 + opens[i] * sx)
                pts.append(V((cx + (rx + 0.01) * math.cos(th) + sx * 0.035, cy + (ry + 0.012) * math.sin(th), z)))
                ws.append(0.09 - 0.012 * (i - 3))
            self.part('lapel' + ('L' if sx > 0 else 'R'),
                      strip(pts, ws, (0.35 * sx, -1, 0.2), 0.02), 'c7', 'spine')
        # tie: knot + blade lying on the belly, a bit crooked
        tp = [V((0.0, -0.37, 1.52)), V((0.005, -0.44, 1.41)), V((0.02, -0.49, 1.27)),
              V((0.035, -0.515, 1.13)), V((0.05, -0.52, 1.01)), V((0.058, -0.515, 0.95))]
        self.part('tie', strip(tp, [0.07, 0.10, 0.13, 0.14, 0.12, 0.02], (0, -1, 0.25), 0.025), 'a12', 'spine')
        self.part('knot', sphere((0, -0.37, 1.525), (0.05, 0.035, 0.04)), 'a12', 'spine')
        # collar points
        for sx in (1, -1):
            self.part('collar' + str(sx), cone((0.05 * sx, -0.33, 1.56), (0.11 * sx, -0.39, 1.47), 0.04, 8),
                      'c8', 'spine')
        # a patch on the jacket back and one torn hole showing skin at the belly side

        # --- trousers: hips, thighs, shins with torn hems; big boots -------
        H = [(0.44, 0, 0, 0, 0.05), (0.47, 0.36, 0.30, 0, 0.05), (0.56, 0.50, 0.40, 0, 0.04),
             (0.72, 0.53, 0.43, 0, 0.02), (0.82, 0.50, 0.41, 0, 0.02), (0.84, 0, 0, 0, 0.02)]
        self.part('hips', lathe(H, 28), 'c9', 'pelvis')

        def leg(s, sx):
            hp, kp, ap = r.head['th' + s], r.head['kn' + s], r.head['an' + s]
            self.part('thigh' + s, tube([hp + V((0, 0, 0.04)), hp.lerp(kp, 0.5), kp],
                                        [0.23, 0.22, 0.19], 18), 'c9', 'th' + s)
            ja = jag(0.03, 7, 0.3 if sx > 0 else 0.7)
            self.part('shin' + s, tube([kp, kp.lerp(ap, 0.5), ap + V((0, 0, 0.02))],
                                       [0.195, 0.19, 0.20], 18, cap1=False,
                                       jag=lambda i, th: ja(th) if i == 2 else 0.0), 'c9', 'kn' + s)
            # sock of skin under the torn hem, then the boot
            self.part('ankle' + s, tube([ap + V((0, 0, 0.10)), ap + V((0, 0, -0.04))], [0.14, 0.15], 14),
                      'skin', 'an' + s)
            b = box(ap + V((0.0, -0.08, -0.045)), (0.17, 0.27, 0.085))
            self.part('boot' + s, b, 'boot', 'an' + s)
            self.part('toe' + s, sphere(ap + V((0.0, -0.25, -0.02)), (0.16, 0.12, 0.10)), 'boot', 'an' + s)
            self.part('sole' + s, box(ap + V((0.0, -0.09, -0.11)), (0.18, 0.30, 0.025)), 'dark', 'an' + s)
        self.mirror(leg)
        # a patch on the left thigh (jacket cloth)
        self.part('patch', box((0.36, -0.20, 0.52), (0.07, 0.02, 0.07), rot_m=(0, 0, -15)), 'c7', 'thL')

        # --- arms: jacket sleeves torn at the elbow, huge forearms and fists
        def arm(s, sx):
            sp, ep, wp = r.head['sh' + s], r.head['el' + s], r.head['fi' + s]
            self.part('delt' + s, sphere(sp + V((0.02 * sx, 0.01, 0.07)), (0.29, 0.28, 0.27)), 'c7', 'sh' + s)
            ja = jag(0.06, 5, 0.2 if sx > 0 else 0.6)
            self.part('sleeve' + s, tube([sp, sp.lerp(ep, 0.5), ep + (ep - sp).normalized() * 0.04],
                                         [0.22, 0.21, 0.205], 18, cap1=False,
                                         jag=lambda i, th: ja(th) if i == 2 else 0.0), 'c7', 'sh' + s)
            self.part('upper' + s, tube([sp, ep], [0.18, 0.17], 16), 'skin', 'sh' + s)
            self.part('fore' + s, tube([ep, ep.lerp(wp, 0.55), wp], [0.17, 0.195, 0.20], 18), 'skin', 'el' + s)
            fc = wp + V((0.02 * sx, -0.03, -0.18))
            self.part('fist' + s, sphere(fc, (0.23, 0.25, 0.24)), 'skin', 'fi' + s)
            for k in range(4):
                x = (k - 1.5) * 0.095
                self.part('knuck%s%d' % (s, k), sphere(fc + V((x * 0.9, -0.17 + abs(x) * 0.25, -0.12)),
                                                       (0.058, 0.07, 0.06)), 'skin', 'fi' + s)
            self.part('thumb' + s, tube([fc + V((-0.14 * sx, -0.10, 0.06)), fc + V((-0.12 * sx, -0.22, -0.02))],
                                        [0.065, 0.058], 12), 'skin', 'fi' + s)
        self.mirror(arm)

        # --- the tiny head -------------------------------------------------
        hb = 'head'
        hy, hz = -0.44, 1.63           # skull centre
        self.part('skull', sphere((0, hy, hz), (0.20, 0.19, 0.20)), 'skin', hb)
        self.part('jaw', sphere((0, hy - 0.05, hz - 0.10), (0.225, 0.17, 0.125)), 'skin', hb)
        fy = hy - 0.05 - 0.165
        self.part('mouth', sphere((0, fy + 0.012, hz - 0.095), (0.13, 0.026, 0.036), rot_m=(0, -7, 0)), 'dark', hb)
        self.part('lip', sphere((0, fy + 0.004, hz - 0.126), (0.13, 0.03, 0.022), rot_m=(0, -7, 0)), 'skin2', hb)
        for sx in (1, -1):
            self.part('tooth%d' % sx, box((0.056 * sx, fy - 0.004, hz - 0.074 + 0.004 * sx), (0.024, 0.013, 0.028)),
                      'white', hb)
        ey = hy - 0.165
        self.part('eyeL', sphere((0.082, ey, hz + 0.022), (0.058, 0.03, 0.058)), 'eye', hb)
        self.part('eyeR', sphere((-0.078, ey - 0.003, hz + 0.012), (0.046, 0.03, 0.046)), 'eye', hb)
        self.part('brow', tube([V((-0.15, ey + 0.01, hz + 0.075)), V((0, ey - 0.02, hz + 0.10)),
                                V((0.15, ey + 0.01, hz + 0.092))],
                               [(0.03, 0.022)] * 3, 10, xhint=(0, 0, 1)), 'dark', hb)
        self.part('nose', sphere((0, ey - 0.03, hz - 0.035), (0.036, 0.032, 0.032)), 'skin', hb)
        for sx in (1, -1):
            self.part('ear%d' % sx, sphere((0.20 * sx, hy + 0.01, hz - 0.01), (0.035, 0.055, 0.065)), 'skin', hb)
        # stitches across the left temple
        st = [V((0.16, hy - 0.06, hz + 0.06)), V((0.19, hy + 0.0, hz + 0.02)), V((0.19, hy + 0.07, hz - 0.03))]
        self.part('scar', tube(st, [0.012] * 3, 8), 'dark', hb)
        for k in range(3):
            c = st[0].lerp(st[2], (k + 0.5) / 3) + V((0.004, 0, 0))
            self.part('stitch%d' % k, tube([c + V((0, -0.02, -0.03)), c + V((0.006, 0.02, 0.03))], [0.01, 0.01], 6),
                      'dark', hb)
        # hair tufts poking out under the hat
        for k, (x, y, z, dx, dy, dz) in enumerate([(0.13, -0.30, 1.71, 0.10, 0.03, 0.06),
                                                   (-0.14, -0.32, 1.70, -0.10, 0.0, 0.05),
                                                   (0.05, -0.20, 1.72, 0.02, 0.12, 0.03),
                                                   (-0.06, -0.22, 1.72, -0.04, 0.11, 0.04)]):
            self.part('tuft%d' % k, cone((x, y, z), (x + dx, y + dy, z + dz), 0.045, 8), 'hair', hb)
        # hard hat, too small and crooked, pushed back on the head
        Rh = rot((-10, 18, 8))
        piv = V((0.03, hy + 0.05, hz + 0.175))

        def hat(g):
            return g.xform(Rh).move(piv)
        dome = lathe([(0, 0.205, 0.215), (0.06, 0.20, 0.21), (0.11, 0.175, 0.185), (0.15, 0.12, 0.13),
                      (0.172, 0, 0)], 28)
        self.part('hat', hat(dome), 'a11', hb)
        brim = lathe([(-0.01, 0.20, 0.21, 0, 0), (-0.012, 0.265, 0.28, 0, -0.035), (0.012, 0.27, 0.285, 0, -0.035),
                      (0.014, 0.205, 0.215, 0, 0)], 28)
        self.part('brim', hat(brim), 'a11', hb)
        rid = [V((0, -0.20 + 0.4 * k / 8, 0)) for k in range(9)]
        rid = [V((0, p.y, 0.172 * math.sqrt(max(0, 1 - (p.y / 0.215) ** 2)) + 0.01)) for p in rid]
        self.part('ridge', hat(tube(rid, [0.03] * 9, 10)), 'a11', hb)

    # ------------------------------------------------------------------
    def pose(self, anim, f, n):
        P = {'rot': {}, 'off': {}, 'ik': [], 'world': {}}
        R, O = P['rot'], P['off']
        rest_an = {s: self.rig.head['an' + s] for s in 'LR'}
        if anim == 'walk':
            ph = cyc(f, n)
            O['pelvis'] = V((0.03 * math.sin(ph), 0, -0.035 * math.cos(2 * ph)))
            R['pelvis'] = (0, 5 * math.sin(ph), 7 * math.sin(ph))
            R['spine'] = (16, -3 * math.sin(ph), -10 * math.sin(ph))
            R['head'] = (-16 - 14, 4 * math.sin(ph), 8 * math.sin(ph))
            for s, sx, p0 in (('L', 1, 0.0), ('R', -1, math.pi)):
                p = ph + p0
                y = -0.26 * math.cos(p)
                lift = 0.15 * max(0.0, -math.sin(p))
                t = rest_an[s] + V((0, y, lift))
                P['ik'].append(('th' + s, 'kn' + s, 'an' + s, t, (0, -1, 0.1)))
                P['world']['an' + s] = rot((-18 * max(0.0, -math.sin(p)) + 8 * math.cos(p) * 0, 0, 0))
                # arms swing against the legs
                R['sh' + s] = (26 * math.cos(p), -4 * sx, 0)
                R['el' + s] = (-18 - 8 * math.cos(p), 0, 0)
                R['fi' + s] = (-10, 0, 0)
        elif anim == 'stomp':
            # hd = the head's pitch in the world (negative = face up)
            k = [dict(pz=-0.06, sp=8, hd=-12, sh=(24, -28), el=-30, fi=-10),     # 00 wind up
                 dict(pz=0.02, sp=-8, hd=-24, sh=(-115, -14), el=-50, fi=-20),  # 01 fists up
                 dict(pz=0.06, sp=-16, hd=-30, sh=(-168, -6), el=-18, fi=-10),  # 02 high, on toes
                 dict(pz=-0.20, sp=40, hd=-12, ik=True),                        # 03 SLAM
                 dict(pz=-0.24, sp=44, hd=-8, ik=True, sq=True),                # 04 impact hold
                 dict(pz=-0.10, sp=22, hd=-14, sh=(-30, -18), el=-40, fi=-10)][f]  # 05 recover
            O['pelvis'] = V((0, 0.06 if k.get('ik') else 0, k['pz']))
            R['spine'] = (k['sp'], 0, 0)
            R['head'] = (k['hd'] - k['sp'], 0, 0)
            for s, sx in (('L', 1), ('R', -1)):
                t = rest_an[s] + V((0.03 * sx, 0.0, 0.0))
                P['ik'].append(('th' + s, 'kn' + s, 'an' + s, t, (0.25 * sx, -1, 0.0)))
                P['world']['an' + s] = rot((0, 0, -6 * sx))
                if k.get('ik'):
                    fz = 0.43 if k.get('sq') else 0.46
                    P['ik'].append(('sh' + s, 'el' + s, 'fi' + s, V((0.30 * sx, -1.0, fz)),
                                    (0.9 * sx, 0.6, 0.3), (0, 1, 0)))
                    P['world']['fi' + s] = rot((-8, 0, 0))
                else:
                    R['sh' + s] = (k['sh'][0], k['sh'][1] * sx, 0)
                    R['el' + s] = (k['el'], 0, 0)
                    R['fi' + s] = (k['fi'], 0, 0)
            if f == 2:
                for s in 'LR':
                    P['ik'][0 if s == 'L' else 1] = ('th' + s, 'kn' + s, 'an' + s,
                                                     rest_an[s] + V((0, 0.02, 0.05)), (0, -1, 0))
        return P


# ===========================================================================
# COUNT - the vampire lord, 1.60 m, 1 x 1, a wide cape
# ===========================================================================

def shell(P, nu, nv, thick, closed_u=False):
    """A two-sided sheet from a grid of points P[v][u]: slot 0 on the outer
    side (the side the normals point to), slot 1 inside and on the rim."""
    g = G.Geo()
    rows_o, rows_i = [], []
    cu = nu if closed_u else nu + 1
    for j in range(nv + 1):
        ro, ri = [], []
        for i in range(cu):
            p = P[j][i]
            a = P[j][(i + 1) % cu if closed_u else min(i + 1, nu)] - P[j][(i - 1) % cu if closed_u else max(i - 1, 0)]
            b = P[min(j + 1, nv)][i] - P[max(j - 1, 0)][i]
            n = b.cross(a)
            n = n.normalized() if n.length > 1e-9 else V((0, 0, 1))
            ro.append(g.vert(p))
            ri.append(g.vert(p - n * thick))
        rows_o.append(ro)
        rows_i.append(ri)
    for j in range(nv):
        for i in range(nu):
            i1 = (i + 1) % cu if closed_u else i + 1
            g.face((rows_o[j][i], rows_o[j + 1][i], rows_o[j + 1][i1], rows_o[j][i1]), 0)
            g.face((rows_i[j][i], rows_i[j][i1], rows_i[j + 1][i1], rows_i[j + 1][i]), 1)
    # rim
    loop = [(0, i) for i in range(cu)] + [(j, cu - 1) for j in range(1, nv + 1)] + \
           [(nv, i) for i in range(cu - 2, -1, -1)] + [(j, 0) for j in range(nv - 1, 0, -1)]
    if closed_u:
        loop = None
    if loop:
        for k in range(len(loop)):
            (ja, ia), (jb, ib) = loop[k], loop[(k + 1) % len(loop)]
            g.face((rows_o[ja][ia], rows_o[jb][ib], rows_i[jb][ib], rows_i[ja][ia]), 1)
    else:
        for j in (0, nv):
            for i in range(nu):
                i1 = (i + 1) % cu
                g.face((rows_o[j][i], rows_o[j][i1], rows_i[j][i1], rows_i[j][i]), 1)
    return g


class Count(Boss):
    name = 'count'
    height = 1.60
    footprint = (1, 1)
    anims = [('glide', 6, 90, 'nesw'), ('cast', 6, 90, 'nesw')]
    HOVER = 0.12

    def build(self):
        r = self.rig
        r.bone('pelvis', None, (0, 0, 0.50))
        r.bone('spine', 'pelvis', (0, 0, 0.58))
        r.bone('head', 'spine', (0, -0.01, 0.97))
        for s, sx in (('L', 1), ('R', -1)):
            r.bone('sh' + s, 'spine', (0.19 * sx, 0.02, 0.90))
            r.bone('el' + s, 'sh' + s, (0.34 * sx, 0.05, 0.70))
            r.bone('ha' + s, 'el' + s, (0.42 * sx, 0.0, 0.52))
            r.bone('th' + s, 'pelvis', (0.085 * sx, 0.0, 0.47))
            r.bone('kn' + s, 'th' + s, (0.09 * sx, -0.01, 0.28))
            r.bone('an' + s, 'kn' + s, (0.09 * sx, 0.01, 0.10))

        # torso: waistcoat, a white cravat, the medallion on a chain
        T = [(0.44, 0, 0, 0, 0), (0.46, 0.13, 0.11), (0.55, 0.16, 0.13), (0.70, 0.165, 0.13),
             (0.82, 0.19, 0.14), (0.92, 0.19, 0.14), (0.97, 0.12, 0.10), (1.00, 0, 0)]
        self.part('torso', lathe(T, 24), 'c9', 'spine')
        self.part('hips', lathe([(0.36, 0, 0), (0.38, 0.13, 0.11), (0.50, 0.155, 0.125), (0.56, 0.15, 0.12),
                                 (0.58, 0, 0)], 20), 'boot', 'pelvis')
        for k, z in enumerate((0.60, 0.68, 0.76)):
            self.part('button%d' % k, sphere((0, -0.135, z), 0.016, 10, 6), 'a11', 'spine')
        crav = [V((0, -0.10, 0.96)), V((0, -0.15, 0.90)), V((0, -0.165, 0.83))]
        self.part('cravat', tube(crav, [(0.05, 0.035), (0.07, 0.04), (0.035, 0.03)], 14), 'white', 'spine')
        for sx in (1, -1):
            self.part('ruffle%d' % sx, sphere((0.035 * sx, -0.15, 0.885), (0.045, 0.03, 0.05), rot_m=(0, 20 * sx, 0)),
                      'white', 'spine')
        ch = [V((0.14 * math.cos(a), -0.02 + 0.12 * math.sin(a) - (0.05 if math.sin(a) < 0 else 0) * -math.sin(a),
                 0.95 - 0.12 * max(0, -math.sin(a)))) for a in [rad(-90 + 20 * k) for k in range(-6, 7)]]
        self.part('chain', tube(ch, [0.012] * len(ch), 8, cap0=True, cap1=True), 'a11', 'spine')
        self.part('medal', lathe([(-0.018, 0, 0), (-0.018, 0.055, 0.055), (0.012, 0.06, 0.06), (0.018, 0.04, 0.04),
                                  (0.02, 0, 0)], 20).xform(rot((90, 0, 0))).move((0, -0.19, 0.82)), 'a11', 'spine')
        self.part('gem', sphere((0, -0.205, 0.82), (0.032, 0.02, 0.032)), 'a12', 'spine')

        # arms in waistcoat sleeves, white cuffs, long pale hands
        def arm(s, sx):
            sp, ep, hp = r.head['sh' + s], r.head['el' + s], r.head['ha' + s]
            self.part('upper' + s, tube([sp, ep], [0.065, 0.058], 14), 'c9', 'sh' + s)
            self.part('fore' + s, tube([ep, hp + (ep - hp).normalized() * 0.04], [0.058, 0.055], 14), 'c9', 'el' + s)
            d = (hp - ep).normalized()
            self.part('cuff' + s, tube([hp - d * 0.07, hp - d * 0.02], [0.068, 0.07], 14), 'white', 'ha' + s)
            self.part('hand' + s, sphere(hp + d * 0.04, (0.05, 0.035, 0.065)), 'skin', 'ha' + s)
            for k in range(3):
                b = hp + d * 0.08 + V(((k - 1) * 0.022, -0.01, 0))
                self.part('fin%s%d' % (s, k), cone(b, b + d * 0.07 + V((0, -0.015, 0)), 0.014, 6), 'skin', 'ha' + s)
        self.mirror(arm)

        # legs and pointed boots (they peek under the cape)
        def leg(s, sx):
            hp, kp, ap = r.head['th' + s], r.head['kn' + s], r.head['an' + s]
            self.part('thigh' + s, tube([hp, kp], [0.075, 0.065], 12), 'boot', 'th' + s)
            self.part('shin' + s, tube([kp, ap], [0.065, 0.055], 12), 'boot', 'kn' + s)
            self.part('boot' + s, tube([ap + V((0, 0.04, -0.02)), ap + V((0, -0.06, -0.05)), ap + V((0, -0.15, -0.075))],
                                       [0.06, 0.055, 0.012], 12), 'boot', 'an' + s)
        self.mirror(leg)

        # head: big, pale, pointed ears, slicked hair with a widow's peak
        hb = 'head'
        c = V((0, -0.03, 1.20))
        self.part('skull', sphere(c, (0.245, 0.235, 0.255), 28, 14), 'skin', hb)
        self.part('chin', sphere(c + V((0, -0.08, -0.14)), (0.13, 0.12, 0.10)), 'skin', hb)
        fy = c.y - 0.228
        for sx in (1, -1):
            self.part('eye%d' % sx, sphere((0.088 * sx, fy + 0.012, c.z + 0.005), (0.062, 0.03, 0.036),
                                           rot_m=(0, -16 * sx, 0)), 'eye', hb, tag='eye')
            self.part('eyeB%d' % sx, sphere((0.09 * sx, fy + 0.006, c.z + 0.01), (0.075, 0.035, 0.05),
                                            rot_m=(0, -16 * sx, 0)), 'eye', hb, tag='eyebig')
            self.part('brow%d' % sx, tube([V((0.035 * sx, fy + 0.008, c.z + 0.045)), V((0.10 * sx, fy + 0.018, c.z + 0.07)),
                                           V((0.16 * sx, fy + 0.045, c.z + 0.085))],
                                          [(0.02, 0.016), (0.022, 0.018), (0.012, 0.012)], 10, xhint=(0, 0, 1)),
                      'dark', hb)
            self.part('fang%d' % sx, cone((0.036 * sx, fy + 0.004, c.z - 0.10), (0.032 * sx, fy - 0.012, c.z - 0.17),
                                          0.022, 8), 'white', hb)
            # bat ears
            eb = V((0.228 * sx, c.y + 0.0, c.z + 0.0))
            et = V((0.37 * sx, c.y + 0.05, c.z + 0.15))
            ear = tube([eb, eb.lerp(et, 0.45) + V((0.0, 0.0, -0.01)), et], [(0.06, 0.02), (0.05, 0.016), (0.004, 0.004)],
                       12, xhint=(0, 0, 1), cap1=False)
            self.part('ear%d' % sx, ear, 'skin', hb)
            self.part('earin%d' % sx, tube([eb.lerp(et, 0.1) + V((0, -0.012, 0)), eb.lerp(et, 0.75) + V((0, -0.012, 0))],
                                            [(0.035, 0.008), (0.006, 0.004)], 10, xhint=(0, 0, 1)), 'skin2', hb)
        mo = [V((-0.075, fy + 0.035, c.z - 0.085)), V((-0.02, fy + 0.018, c.z - 0.104)),
              V((0.04, fy + 0.02, c.z - 0.10)), V((0.085, fy + 0.04, c.z - 0.07))]
        self.part('mouth', tube(mo, [(0.012, 0.012), (0.022, 0.012), (0.02, 0.012), (0.01, 0.01)], 10,
                                xhint=(0, 0, 1)), 'dark', hb)
        self.part('nose', cone((0, fy + 0.03, c.z - 0.02), (0, fy - 0.035, c.z - 0.065), 0.035, 10), 'skin', hb)

        hc = c + V((0, 0.035, 0.03))
        hr = (0.262, 0.262, 0.262)

        def hairmap(p):
            d = p - hc
            dn = V((d.x / hr[0], d.y / hr[1], d.z / hr[2]))
            front = smooth(-0.05, -0.55, dn.y)        # 0 at the back, 1 at the face
            peak = math.exp(-(dn.x / 0.22) ** 2)
            line = lerp(-0.55, 0.52 - 0.38 * peak, front)
            if dn.z < line:
                return hc + d * 0.86
            return p
        hair = sphere(hc, hr, 32, 16).map(hairmap)
        self.part('hair', hair, 'hair', hb)
        # slicked-back swoosh
        self.part('swoosh', tube([hc + V((0, 0.05, 0.20)), hc + V((0, 0.20, 0.17)), hc + V((0, 0.30, 0.06))],
                                 [(0.13, 0.07), (0.10, 0.06), (0.03, 0.03)], 14, xhint=(1, 0, 0)), 'hair', hb)

    def show(self, tag, P):
        big = P.get('flare', False)
        if tag == 'eye':
            return not big
        if tag == 'eyebig':
            return big
        return True

    # the collar and the cape are sheets rebuilt every frame
    def deform(self, P):
        r = self.rig
        out = []
        # collar: a flared cone behind the head, scalloped points on top
        K = 4
        nu, nv = 32, 5
        a0, a1 = rad(-38), rad(218)
        pts = []
        for j in range(nv + 1):
            v = j / nv
            row = []
            for i in range(nu + 1):
                u = i / nu
                th = lerp(a0, a1, u)
                w = (u * K) % 1.0
                top = 0.15 * (1 - math.sin(math.pi * w)) ** 2 if j == nv else 0.0
                rr = lerp(0.17, 0.44, v ** 1.2)
                z = lerp(0.91, 1.43, v) + top * v
                y0 = 0.03 + 0.20 * v * v
                p = V((rr * math.cos(th), y0 + rr * 0.85 * math.sin(th), z))
                row.append(r.pt('spine', p))
            pts.append(row)
        out.append((shell(pts, nu, nv, 0.018), ['c8', 'c7'], 1, 'collar'))

        # cape: from the shoulders round the back to the hands, bat-wing hem
        sway = P.get('sway', 0.0)
        trail = P.get('trail', 0.08)
        spread = P.get('spread', 0.0)
        pel = r.M['pelvis'].translation
        hov = pel.z - r.head['pelvis'].z
        K = 7
        nu, nv = 56, 14
        f0, f1 = rad(-68), rad(248)
        hands = {s: r.pt('ha' + s, r.head['ha' + s] + (r.head['ha' + s] - r.head['el' + s]).normalized() * 0.05)
                 for s in 'LR'}
        tops = {'L': r.pt('spine', V((0.10, -0.15, 0.93))), 'R': r.pt('spine', V((-0.10, -0.15, 0.93)))}
        zh = 0.10 + hov
        pts = []
        for j in range(nv + 1):
            v = j / nv
            row = []
            for i in range(nu + 1):
                u = i / nu
                th = lerp(f0, f1, u)
                rr = lerp(0.23, 0.50 + 0.10 * spread, v ** 0.75) * (1 + 0.05 * v * math.sin(3 * th + sway))
                w = (u * K) % 1.0
                hem = 0.09 * math.sin(math.pi * w) ** 0.8
                z = lerp(0.95, zh + hem, v)
                tp = r.pt('spine', V((0.23 * math.cos(th), 0.02 + 0.20 * math.sin(th), 0.95)))
                base = V((pel.x + rr * math.cos(th), pel.y + 0.02 + rr * 0.9 * math.sin(th) + trail * v * v,
                          lerp(tp.z, z, v)))
                base = tp.lerp(base, smooth(0.0, 0.25, v)) if v < 0.25 else base
                # the front edges run to the hands and hang from them
                side = 'L' if u < 0.5 else 'R'
                ue = u if side == 'L' else 1 - u
                we = 1 - smooth(0.0, 0.30, ue)
                H = hands[side]
                T0 = tops[side]
                vh = 0.42
                if v <= vh:
                    e = T0.lerp(H, smooth(0, 1, v / vh) * 0.3 + 0.7 * (v / vh))
                else:
                    t = (v - vh) / (1 - vh)
                    bot = V((H.x * 1.08, H.y + 0.05 + trail * 0.6, zh + hem))
                    e = H.lerp(bot, t)
                row.append(base.lerp(e, we))
            pts.append(row)
        out.append((shell(pts, nu, nv, 0.02), ['c7', 'c8'], 1, 'cape'))
        return out

    def pose(self, anim, f, n):
        P = {'rot': {}, 'off': {}, 'ik': [], 'world': {}}
        R, O = P['rot'], P['off']
        if anim == 'glide':
            ph = cyc(f, n)
            O['pelvis'] = V((0, 0, self.HOVER + 0.035 * math.sin(ph)))
            R['pelvis'] = (8, 0, 0)
            R['spine'] = (4, 0, 3 * math.sin(ph))
            R['head'] = (-16, 0, -3 * math.sin(ph))
            for s, sx in (('L', 1), ('R', -1)):
                t = V((0.44 * sx, -0.16, 0.60 + self.HOVER + 0.05 * math.sin(ph - 0.9)))
                P['ik'].append(('sh' + s, 'el' + s, 'ha' + s, t, (0.3 * sx, 1, -0.4), (0, 1, 0)))
                R['ha' + s] = (0, 0, 0)
                R['th' + s] = (-8 + 4 * math.sin(ph + (0 if sx > 0 else 1)), 0, 0)
                R['kn' + s] = (14, 0, 0)
                R['an' + s] = (30, 0, 0)
            P['sway'] = ph
            P['trail'] = 0.10 + 0.03 * math.sin(ph)
        elif anim == 'cast':
            k = [0.15, 0.55, 1.0, 1.0, 0.95, 0.4][f]
            ph = cyc(f, n)
            O['pelvis'] = V((0, 0, self.HOVER + 0.06 * k))
            R['pelvis'] = (-4 * k, 0, 0)
            R['head'] = (-18 - 8 * k, 0, 0)
            for s, sx in (('L', 1), ('R', -1)):
                t = V((lerp(0.44, 0.66, k) * sx, lerp(-0.16, -0.10, k),
                       lerp(0.60, 1.18, k) + self.HOVER + 0.06 * k + (0.02 * math.sin(ph * 3) if f in (2, 3, 4) else 0)))
                P['ik'].append(('sh' + s, 'el' + s, 'ha' + s, t, (0.2 * sx, 1, -0.6), (0, 1, 0)))
                R['th' + s] = (-6, 0, 0)
                R['kn' + s] = (12, 0, 0)
                R['an' + s] = (30, 0, 0)
            P['flare'] = f in (2, 3, 4)
            P['spread'] = k
            P['trail'] = 0.04
            P['sway'] = ph
        return P


# ===========================================================================
# PHARAOH - a giant mummy king, 1.90 m, 2 x 2
# ===========================================================================

class Pharaoh(Boss):
    name = 'pharaoh'
    height = 1.90
    footprint = (2, 2)
    anims = [('walk', 6, 130, 'nesw'), ('whip', 6, 80, 'nesw')]
    STRIPE = 0.065

    def build(self):
        r = self.rig
        r.bone('pelvis', None, (0, 0.02, 0.62))
        r.bone('spine', 'pelvis', (0, 0.02, 0.74))
        r.bone('head', 'spine', (0, -0.03, 1.30))
        for s, sx in (('L', 1), ('R', -1)):
            r.bone('sh' + s, 'spine', (0.45 * sx, 0.03, 1.17))
            r.bone('el' + s, 'sh' + s, (0.58 * sx, 0.05, 0.88))
            r.bone('ha' + s, 'el' + s, (0.63 * sx, -0.01, 0.62))
            r.bone('th' + s, 'pelvis', (0.19 * sx, 0.02, 0.58))
            r.bone('kn' + s, 'th' + s, (0.20 * sx, 0.0, 0.33))
            r.bone('an' + s, 'kn' + s, (0.21 * sx, 0.03, 0.11))
        ST = self.STRIPE

        # --- wrapped torso with overlapping bands -------------------------
        T = [(0.52, 0, 0, 0, 0.02), (0.55, 0.24, 0.20, 0, 0.02), (0.64, 0.33, 0.26, 0, 0.02),
             (0.80, 0.36, 0.28, 0, 0.01), (0.96, 0.41, 0.30, 0, 0.0), (1.08, 0.47, 0.31, 0, 0.0),
             (1.18, 0.49, 0.30, 0, 0.01), (1.26, 0.41, 0.26, 0, 0.02), (1.32, 0.24, 0.17, 0, 0.02),
             (1.35, 0, 0, 0, 0.02)]
        self.part('torso', lathe(T, 32), 'skin', 'spine')
        for k, (z, tl, td) in enumerate(((0.86, 13, 30), (0.97, -11, -40), (1.07, 14, 160), (1.15, -9, 70))):
            self.part('wrap%d' % k, band_on(T, z, 0.075, 0.008, tl, td, 36), 'skin2', 'spine')

        # --- broad collar (usekh): gold and lapis rings, bead drops ------
        CR = [(1.335, 0.18, 0.155, 0, 0.0), (1.315, 0.235, 0.195, 0, -0.005), (1.29, 0.29, 0.232, 0, -0.01),
              (1.26, 0.345, 0.265, 0, -0.012), (1.225, 0.40, 0.295, 0, -0.012), (1.19, 0.45, 0.32, 0, -0.01),
              (1.17, 0.465, 0.33, 0, -0.01), (1.15, 0.455, 0.322, 0, -0.01), (1.16, 0.40, 0.29, 0, -0.01)]
        self.part('collar', lathe(CR, 40, mat=lambda i, j, th: 1 if i in (1, 3) else 0), ['a11', 'a12'], 'spine')
        for k in range(15):
            th = rad(-90 + (k - 7) * 13.5)
            p = V((0.47 * math.cos(th), -0.01 + 0.335 * math.sin(th), 1.145))
            self.part('bead%d' % k, sphere(p, (0.028, 0.028, 0.04), 10, 6), 'a12' if k % 2 else 'a11', 'spine')

        # --- kilt, belt, apron ------------------------------------------
        K = [(0.48, 0.44, 0.36, 0, 0.0), (0.52, 0.435, 0.355, 0, 0.0), (0.61, 0.405, 0.325, 0, 0.0),
             (0.71, 0.375, 0.295, 0, 0.0), (0.80, 0.365, 0.285, 0, 0.0)]
        self.part('kilt', lathe(K, 48, rfun=lambda i, th: 1 + 0.018 * math.sin(20 * th) * (1 - i / 4)),
                  'c7', 'pelvis')
        self.part('belt', band_on(K, 0.775, 0.075, 0.012, 0, 0, 40, 0.03), 'a11', 'pelvis')
        ap = [V((0, -0.31 - 0.085 * k / 4, 0.76 - 0.26 * k / 4)) for k in range(5)]
        self.part('apron', strip(ap, [0.20, 0.23, 0.26, 0.29, 0.32], (0, -1, 0.2), 0.03,
                                 mat=lambda i: 0 if i % 2 else 1), ['a12', 'a11'], 'pelvis')
        self.part('buckle', box((0, -0.305, 0.775), (0.05, 0.02, 0.045)), 'a12', 'pelvis')

        # --- legs --------------------------------------------------------
        def leg(s, sx):
            hp, kp, ap_ = r.head['th' + s], r.head['kn' + s], r.head['an' + s]
            self.part('thigh' + s, tube([hp, kp], [0.15, 0.135], 16), 'skin', 'th' + s)
            self.part('shin' + s, tube([kp, ap_], [0.135, 0.115], 16), 'skin', 'kn' + s)
            for k, (t, tl) in enumerate(((0.3, 14), (0.7, -12))):
                self.part('lw%s%d' % (s, k), tube_band(kp, ap_, 0.135, 0.115, t, 0.06, 0.006, tl * sx), 'skin2', 'kn' + s)
            self.part('foot' + s, box(ap_ + V((0, -0.07, -0.05)), (0.11, 0.18, 0.065)), 'skin', 'an' + s)
            self.part('toe' + s, sphere(ap_ + V((0, -0.20, -0.05)), (0.10, 0.08, 0.06)), 'skin2', 'an' + s)
        self.mirror(leg)

        # --- arms: wraps, gold armlets and bracelets ----------------------
        def arm(s, sx):
            sp, ep, hp = r.head['sh' + s], r.head['el' + s], r.head['ha' + s]
            self.part('shoulder' + s, sphere(sp + V((0.01 * sx, 0, 0.02)), (0.17, 0.16, 0.16)), 'skin', 'sh' + s)
            self.part('upper' + s, tube([sp, ep], [0.15, 0.13], 16), 'skin', 'sh' + s)
            self.part('armlet' + s, tube_band(sp, ep, 0.15, 0.13, 0.55, 0.055, 0.012, 0, thick=0.03), 'a11', 'sh' + s)
            self.part('fore' + s, tube([ep, hp], [0.13, 0.11], 16), 'skin', 'el' + s)
            self.part('fw' + s, tube_band(ep, hp, 0.13, 0.11, 0.35, 0.06, 0.006, 16 * sx), 'skin2', 'el' + s)
            self.part('bracelet' + s, tube_band(ep, hp, 0.13, 0.11, 0.84, 0.075, 0.014, 0, thick=0.03), 'a12', 'el' + s)
            d = (hp - ep).normalized()
            self.part('hand' + s, sphere(hp + d * 0.08, (0.095, 0.08, 0.105)), 'skin', 'ha' + s)
            self.part('thumb' + s, tube([hp + d * 0.05 + V((-0.07 * sx, -0.05, 0)), hp + d * 0.10 + V((-0.08 * sx, -0.10, 0))],
                                        [0.035, 0.03], 10), 'skin', 'ha' + s)
        self.mirror(arm)
        # the crook in the right hand (held upright; the pose keeps the hand level)
        hp = r.head['haR'] + V((0, -0.02, -0.08))
        cp = [hp + V((0, 0, -0.34 + 0.96 * k / 12)) for k in range(13)]
        top = cp[-1]
        cc = top + V((0, -0.09, 0))
        for k in range(1, 12):
            a = math.pi * 1.2 * k / 11
            cp.append(cc + V((0, 0.09 * math.cos(a), 0.09 * math.sin(a))))
        L = 0.0
        segl = []
        for a_, b_ in zip(cp, cp[1:]):
            segl.append(L)
            L += (b_ - a_).length
        self.part('crook', tube(cp, [0.03] * len(cp), 12,
                                mat=lambda i, j: int(segl[min(i, len(segl) - 1)] / 0.08) % 2), ['a11', 'a12'], 'haR')

        # --- the head: nemes headdress, golden mask, false beard ----------
        hb = 'head'
        hc = V((0, -0.05, 1.53))
        self.part('skull', sphere(hc, (0.27, 0.26, 0.28)), 'skin', hb)
        H = [(1.34, 0.50, 0.28, 0, 0.07), (1.42, 0.45, 0.29, 0, 0.04), (1.50, 0.385, 0.30, 0, 0.02),
             (1.60, 0.34, 0.30, 0, 0.01), (1.68, 0.318, 0.298, 0, 0.0), (1.77, 0.29, 0.285, 0, 0.0),
             (1.83, 0.22, 0.22, 0, 0.0), (1.86, 0.12, 0.12, 0, 0.0), (1.87, 0, 0, 0, 0.0)]
        lower = resample(H, 1.34, 1.66, ST / 2)
        arcs = []
        for (z, rx, ry, cx, cy) in lower:
            op = lerp(76, 60, smooth(1.34, 1.60, z))
            arcs.append((rad(-90 + op), rad(270 - op)))
        nb = len(lower) - 1
        self.part('hood', lathe_arc(lower, arcs, 36, mat=lambda i, j: (nb - 1 - i) // 2 % 2), ['a11', 'a12'], hb)
        upper = resample(H, 1.64, 1.86, ST / 2) + [(1.87, 0, 0, 0, 0)]
        self.part('crown', lathe(upper, 36, mat=lambda i, j, th: 1 if (i // 2) % 2 == 1 and i < 5 else 0),
                  ['a11', 'a12'], hb)
        self.part('headband', band_on(H, 1.655, 0.05, 0.012, 0, 0, 40, 0.03), 'a11', hb)
        # lappets hanging in front of the shoulders
        for sx in (1, -1):
            lp = [V((0.285 * sx, -0.14 - 0.04 * k / 5, 1.44 - 0.39 * k / 5)) for k in range(6)]
            nseg = 5
            self.part('lappet%d' % sx, strip(lp, [0.15, 0.155, 0.16, 0.165, 0.17, 0.17], (0.25 * sx, -1, 0.1), 0.035,
                                             mat=lambda i: i % 2), ['a11', 'a12'], hb)
        # braided tail at the back
        self.part('tail', tube([V((0, 0.27, 1.44)), V((0, 0.31, 1.32)), V((0, 0.32, 1.20))], [0.07, 0.065, 0.05], 12,
                               mat=lambda i, j: i % 2), ['a12', 'a11'], hb)
        # golden mask
        mc = V((0, -0.16, 1.49))
        self.part('mask', sphere(mc, (0.235, 0.15, 0.215), 28, 14), 'a11', hb)
        fy = mc.y - 0.15
        for sx in (1, -1):
            ex = 0.092 * sx
            self.part('socket%d' % sx, sphere((ex, fy + 0.018, 1.535), (0.07, 0.03, 0.04), rot_m=(0, -6 * sx, 0)),
                      'dark', hb)
            self.part('eye%d' % sx, sphere((ex, fy + 0.008, 1.535), (0.05, 0.024, 0.03), rot_m=(0, -6 * sx, 0)),
                      'eye', hb)
            self.part('kohl%d' % sx, tube([V((0.15 * sx, fy + 0.05, 1.53)), V((0.205 * sx, fy + 0.10, 1.54))],
                                          [0.014, 0.012], 8), 'a12', hb)
            self.part('brow%d' % sx, tube([V((0.035 * sx, fy + 0.015, 1.60)), V((0.10 * sx, fy + 0.02, 1.615)),
                                           V((0.175 * sx, fy + 0.06, 1.595))], [0.016, 0.017, 0.012], 8), 'a12', hb)
        self.part('nose', tube([V((0, fy + 0.005, 1.56)), V((0, fy - 0.03, 1.465))], [0.024, 0.032], 10), 'a11', hb)
        self.part('mouth', sphere((0, fy + 0.02, 1.40), (0.06, 0.02, 0.014)), 'dark', hb)
        bp = [V((0, fy + 0.07, 1.335)), V((0, fy + 0.05, 1.27)), V((0, fy + 0.035, 1.20)), V((0, fy + 0.02, 1.15)),
              V((0, fy - 0.02, 1.12))]
        self.part('beard', tube(bp, [0.05, 0.055, 0.055, 0.05, 0.04], 14, mat=lambda i, j: i % 2), ['a11', 'a12'], hb)
        self.part('uraeus', tube([V((0, fy + 0.02, 1.66)), V((0, fy - 0.02, 1.71)), V((0, fy - 0.005, 1.75))],
                                 [0.024, 0.022, 0.018], 10), 'a11', hb)
        self.part('cobra', sphere((0, fy - 0.005, 1.755), (0.038, 0.02, 0.04)), 'a12', hb)

    # loose bandages and the whip: ribbons rebuilt per frame
    def deform(self, P):
        r = self.rig
        out = []
        ph = P.get('ph', 0.0)
        # a loose end hanging from the left forearm
        a = r.pt('elL', r.head['elL'].lerp(r.head['haL'], 0.4) + V((0.05, 0.08, 0.0)))
        pts = [a]
        for k in range(1, 6):
            t = k / 5
            pts.append(a + V((0.03 * t + 0.02 * math.sin(ph + t * 3), 0.10 * t + 0.03 * math.sin(ph * 1 + t * 4),
                              -0.34 * t)))
        out.append((strip(pts, [0.09, 0.09, 0.085, 0.08, 0.075, 0.07], (1, 0.2, 0), 0.02), 'skin2', 1, 'loose1'))
        # one trailing from the back of the belt
        b = r.pt('pelvis', V((0.12, 0.30, 0.74)))
        pts = [b]
        for k in range(1, 7):
            t = k / 6
            pts.append(b + V((0.05 * t + 0.02 * math.sin(ph + t * 4), 0.12 * t + 0.14 * t * t, -0.58 * t + 0.1 * t * t)))
        out.append((strip(pts, 0.10, (0.3, -1, 0.4), 0.02), 'skin2', 1, 'loose2'))
        # the whip bandage from the left hand
        wp = P.get('whip')
        if wp:
            h = r.pt('haL', r.head['haL'] + (r.head['haL'] - r.head['elL']).normalized() * 0.1)
            pts = [h + V(p) for p in wp]
            out.append((strip(pts, [0.12] * (len(pts) - 1) + [0.08], P.get('whip_up', (0, 0, 1)), 0.02), 'skin', 1, 'whip'))
        return out

    def pose(self, anim, f, n):
        P = {'rot': {}, 'off': {}, 'ik': [], 'world': {}}
        R, O = P['rot'], P['off']
        rest_an = {s: self.rig.head['an' + s] for s in 'LR'}
        crook_arm = True
        if anim == 'walk':
            ph = cyc(f, n)
            P['ph'] = ph
            O['pelvis'] = V((0.025 * math.sin(ph), 0, -0.025 * math.cos(2 * ph)))
            R['pelvis'] = (0, 4 * math.sin(ph), 5 * math.sin(ph))
            R['spine'] = (4, -3 * math.sin(ph), -6 * math.sin(ph))
            R['head'] = (-14, 3 * math.sin(ph), 4 * math.sin(ph))
            for s, sx, p0 in (('L', 1, 0.0), ('R', -1, math.pi)):
                p = ph + p0
                t = rest_an[s] + V((0, -0.20 * math.cos(p), 0.10 * max(0.0, -math.sin(p))))
                P['ik'].append(('th' + s, 'kn' + s, 'an' + s, t, (0, -1, 0.1)))
                P['world']['an' + s] = rot((-10 * max(0.0, -math.sin(p)), 0, 0))
            # the mummy arm reaching forward, the crook held up in the other
            R['shL'] = (-80 + 5 * math.sin(ph), -34, 0)
            R['elL'] = (-12, 0, 0)
            R['haL'] = (-10, 0, 0)
        elif anim == 'whip':
            # 00 wind up, 01 swing, 02 half out, 03 two cells out, 04 recoil, 05 back
            P['ph'] = cyc(f, n)
            sh = [(-150, -20), (-110, -12), (-80, -8), (-75, -6), (-78, -8), (-60, -10)][f]
            R['shL'] = (sh[0], sh[1], 0)
            R['elL'] = ([-60, -30, -5, 0, -10, -30][f], 0, 0)
            R['spine'] = ([-10, 4, 12, 14, 10, 4][f], 0, [-14, -4, 6, 8, 4, 0][f])
            R['head'] = ([-4, -16, -24, -24, -20, -16][f], 0, 0)
            O['pelvis'] = V((0, 0, [0.0, -0.02, -0.05, -0.06, -0.04, -0.02][f]))
            for s in 'LR':
                t = rest_an[s] + (V((0, -0.12, 0)) if s == 'L' else V((0, 0.10, 0)))
                P['ik'].append(('th' + s, 'kn' + s, 'an' + s, t, (0, -1, 0.1)))
                P['world']['an' + s] = rot((0, 0, 0))
            # the bandage, in the character's frame from the hand
            if f == 0:
                wp = [(0, 0.05 * k, 0.05 * k - 0.02 * k * k) for k in range(6)]
            elif f == 1:
                wp = [(0, 0.10 * k, 0.12 * k - 0.035 * k * k) for k in range(6)]
            elif f == 2:
                wp = [(0.01 * math.sin(k), -0.22 * k, -0.02 * k + 0.010 * k * k) for k in range(8)]
            elif f == 3:          # two cells past the footprint: the tip ~3 m ahead of the anchor
                wp = [(0.02 * math.sin(k * 1.3), -0.27 * k, -0.012 * k - 0.0032 * k * k) for k in range(11)]
            elif f == 4:
                wp = [(0.03 * math.sin(k * 1.7), -0.19 * k, 0.05 * math.sin(k * 1.4) - 0.01 * k) for k in range(9)]
            else:
                wp = [(0, -0.07 * k, -0.07 * k) for k in range(5)]
            P['whip'] = wp
            P['whip_up'] = (0, 0, 1)
        if crook_arm:
            R['shR'] = (-24, 6, 0)
            R['elR'] = (-62, 0, 0)
            P['world']['haR'] = rot((0, 0, 0))
        return P


# ===========================================================================
# ALPHA - the werewolf pack leader, 2.0 m, 2 x 2
# ===========================================================================

class Alpha(Boss):
    name = 'alpha'
    height = 2.0
    footprint = (2, 2)
    anims = [('run', 6, 60, 'nesw'), ('howl', 5, 120, 'nesw'), ('stun', 4, 150, 'nesw')]
    HUNCH = 24.0
    HEAD = 1.22            # chibi: the head is modelled at wolf size and scaled up
    part_scale = {'head': HEAD, 'jaw': HEAD}

    def build(self):
        r = self.rig
        r.bone('pelvis', None, (0, 0.10, 0.84))
        r.bone('spine', 'pelvis', (0, 0.08, 0.96))
        hp = V((0, -0.30, 1.50))
        r.bone('head', 'spine', hp)
        # the jaw hinge moves with the head's scale
        r.bone('jaw', 'head', hp + (V((0, -0.50, 1.58)) - hp) * self.HEAD)
        self._jaw_rest = V((0, -0.50, 1.58))
        for s, sx in (('L', 1), ('R', -1)):
            r.bone('sh' + s, 'spine', (0.44 * sx, -0.10, 1.40))
            r.bone('el' + s, 'sh' + s, (0.60 * sx, -0.08, 1.04))
            r.bone('ha' + s, 'el' + s, (0.64 * sx, -0.20, 0.70))
            r.bone('th' + s, 'pelvis', (0.24 * sx, 0.12, 0.80))
            r.bone('kn' + s, 'th' + s, (0.27 * sx, -0.10, 0.52))
            r.bone('ho' + s, 'kn' + s, (0.28 * sx, 0.16, 0.26))
            r.bone('pa' + s, 'ho' + s, (0.28 * sx, 0.06, 0.07))
        pel = r.head['pelvis']
        Rt = Matrix.Translation(pel) @ rot((self.HUNCH, 0, 0)).to_4x4()

        # --- torso: a barrel chest leaning forward ------------------------
        T = [(-0.14, 0, 0, 0, 0.0), (-0.10, 0.25, 0.22, 0, 0.0), (0.04, 0.33, 0.28, 0, 0.0),
             (0.22, 0.37, 0.32, 0, -0.02), (0.40, 0.45, 0.38, 0, -0.05), (0.55, 0.49, 0.40, 0, -0.05),
             (0.66, 0.46, 0.37, 0, -0.03), (0.74, 0.34, 0.29, 0, 0.0), (0.79, 0, 0, 0, 0.0)]
        self.part('torso', lathe(T, 32).xform(Rt), 'skin', 'spine')
        # the light chest and belly
        self.part('chest', sphere(V((0, -0.24, 0.42)), (0.33, 0.16, 0.30)).xform(Rt), 'skin2', 'spine')
        # a torn leather vest, open on the chest, sleeves ripped off
        VT = [(0.14, 0.37, 0.32, 0, -0.01), (0.22, 0.385, 0.335, 0, -0.02), (0.40, 0.465, 0.395, 0, -0.05),
              (0.55, 0.505, 0.415, 0, -0.05), (0.66, 0.475, 0.385, 0, -0.03), (0.73, 0.37, 0.31, 0, 0.0)]
        hem = jag(0.05, 7, 0.1)
        arcs = [(rad(-90 + o), rad(270 - o)) for o in (48, 46, 40, 34, 26, 18)]
        self.part('vest', lathe_arc(VT, arcs, 40, zfun=lambda i, th: hem(th) if i == 0 else 0.0).xform(Rt), 'c7', 'spine')
        # the ruff: tufts round the neck and over the shoulders
        tufts = []
        for k in range(11):
            a = rad(-10 + 200 * k / 10)          # round the back from side to side
            base = V((0.30 * math.cos(a), 0.08 + 0.24 * math.sin(a), 0.70))
            tip = base + V((0.20 * math.cos(a), 0.14 * math.sin(a) + 0.06, 0.12 + 0.04 * (k % 2)))
            tufts.append((base, tip, 0.10))
        for k, (b, t, rr) in enumerate(tufts):
            self.part('mane%d' % k, cone(b, t, rr, 10).xform(Rt), 'skin', 'spine')
        for k in range(5):
            x = (k - 2) * 0.10
            b = V((x, -0.28, 0.62 - abs(x) * 0.4))
            t = b + V((x * 0.4, -0.12, -0.20))
            self.part('ruff%d' % k, cone(b, t, 0.085, 10).xform(Rt), 'skin2', 'spine')
        # a darker stripe of fur down the back
        self.part('back', sphere(V((0, 0.24, 0.52)), (0.20, 0.14, 0.28)).xform(Rt), 'hair', 'spine')

        # --- torn shorts and a belt --------------------------------------
        cy = pel.y
        SH = [(0.62, 0, 0, 0, cy), (0.645, 0.25, 0.22, 0, cy), (0.75, 0.34, 0.29, 0, cy),
              (0.88, 0.35, 0.30, 0, cy - 0.01), (0.97, 0.33, 0.28, 0, cy - 0.02), (1.00, 0, 0, 0, cy - 0.02)]
        self.part('hips', lathe(SH, 28), 'c8', 'pelvis')
        self.part('belt', band_on(SH, 0.93, 0.06, 0.012, -6, 90, 36, 0.03), 'a11', 'pelvis')

        # --- legs: digitigrade, clawed paws ------------------------------
        def leg(s, sx):
            hp, kp, hk, pp = (r.head[b + s] for b in ('th', 'kn', 'ho', 'pa'))
            self.part('thigh' + s, tube([hp, hp.lerp(kp, 0.5), kp], [0.19, 0.18, 0.14], 16), 'skin', 'th' + s)
            ja = jag(0.05, 5, 0.4 if sx > 0 else 0.8)
            self.part('shorts' + s, tube([hp + V((0, 0, 0.06)), hp.lerp(kp, 0.62)], [0.21, 0.195], 18, cap1=False,
                                         jag=lambda i, th: ja(th) if i == 1 else 0.0), 'c8', 'th' + s)
            self.part('shin' + s, tube([kp, kp.lerp(hk, 0.5), hk], [0.13, 0.10, 0.075], 14), 'skin', 'kn' + s)
            self.part('calf' + s, cone(kp.lerp(hk, 0.3) + V((0, 0.05, 0)), kp.lerp(hk, 0.45) + V((0, 0.17, 0.02)), 0.06, 8),
                      'skin', 'kn' + s)
            self.part('meta' + s, tube([hk, pp + V((0, 0.02, 0.02))], [0.075, 0.07], 12), 'skin', 'ho' + s)
            self.part('paw' + s, sphere(pp + V((0, -0.08, -0.005)), (0.11, 0.15, 0.065)), 'skin', 'pa' + s)
            for k in range(3):
                c = pp + V(((k - 1) * 0.06, -0.22, -0.02))
                self.part('tclaw%s%d' % (s, k), cone(c, c + V((0, -0.06, -0.035)), 0.022, 6), 'white', 'pa' + s)
        self.mirror(leg)

        # --- arms: brawny, fur tufts at the elbows, big clawed hands ------
        def arm(s, sx):
            sp, ep, hp = r.head['sh' + s], r.head['el' + s], r.head['ha' + s]
            self.part('delt' + s, sphere(sp + V((0.02 * sx, 0.0, 0.03)), (0.19, 0.18, 0.18)), 'skin', 'sh' + s)
            self.part('upper' + s, tube([sp, ep], [0.155, 0.12], 16), 'skin', 'sh' + s)
            self.part('etuft' + s, cone(ep + V((0, 0.02, 0.04)), ep + V((0.10 * sx, 0.14, -0.04)), 0.07, 8), 'skin', 'el' + s)
            self.part('fore' + s, tube([ep, ep.lerp(hp, 0.5), hp], [0.12, 0.13, 0.11], 16), 'skin', 'el' + s)
            d = (hp - ep).normalized()
            hc = hp + d * 0.09
            self.part('hand' + s, sphere(hc, (0.12, 0.11, 0.12)), 'skin', 'ha' + s)
            side = V((1, 0, 0))
            fwd = V((0, -1, 0))
            for k in range(4):
                b = hc + d * 0.08 + side * ((k - 1.5) * 0.055) + fwd * 0.06
                self.part('claw%s%d' % (s, k), cone(b, b + d * 0.06 + fwd * 0.06, 0.022, 6), 'white', 'ha' + s)
            self.part('thumb' + s, tube([hc + V((-0.08 * sx, -0.06, 0.02)), hc + V((-0.10 * sx, -0.14, -0.03))],
                                        [0.04, 0.035], 10), 'skin', 'ha' + s)
        self.mirror(arm)

        # --- the head: a proud wolf ---------------------------------------
        hb, jb = 'head', 'jaw'
        hc = V((0, -0.42, 1.68))
        self.part('skull', sphere(hc, (0.25, 0.25, 0.23), 28, 14), 'skin', hb)
        self.part('forehead', sphere(hc + V((0, -0.10, 0.06)), (0.17, 0.14, 0.12)), 'skin', hb)
        for sx in (1, -1):
            for k, (b, t) in enumerate((((0.20, -0.46, 1.62), (0.37, -0.40, 1.55)),
                                        ((0.19, -0.44, 1.54), (0.33, -0.36, 1.44)))):
                self.part('cheek%d%d' % (sx, k), cone(V((b[0] * sx, b[1], b[2])), V((t[0] * sx, t[1], t[2])), 0.08, 8),
                          'skin2', hb)
            eb = V((0.13 * sx, -0.36, 1.84))
            et = V((0.25 * sx, -0.28, 2.02))
            self.part('ear%d' % sx, tube([eb, eb.lerp(et, 0.5), et], [(0.085, 0.035), (0.06, 0.028), (0.006, 0.006)], 12,
                                         xhint=(1, 0, 0), cap1=False), 'skin', hb)
            self.part('earin%d' % sx, tube([eb.lerp(et, 0.1) + V((0, -0.028, 0)), eb.lerp(et, 0.72) + V((0, -0.02, 0))],
                                           [(0.05, 0.01), (0.008, 0.006)], 10, xhint=(1, 0, 0)), 'skin2', hb)
            self.part('eye%d' % sx, sphere((0.105 * sx, -0.625, 1.735), (0.058, 0.03, 0.04), rot_m=(0, -14 * sx, 0)),
                      'eye', hb, tag='eye')
            self.part('eyeD%d' % sx, sphere((0.105 * sx, -0.625, 1.73), (0.055, 0.03, 0.014), rot_m=(0, -14 * sx, 0)),
                      'dark', hb, tag='eyedizzy')
            self.part('brow%d' % sx, tube([V((0.04 * sx, -0.64, 1.785)), V((0.12 * sx, -0.64, 1.80)),
                                           V((0.19 * sx, -0.60, 1.815))], [0.024, 0.026, 0.016], 10), 'dark', hb)
            self.part('fang%d' % sx, cone((0.055 * sx, -0.80, 1.58), (0.052 * sx, -0.81, 1.51), 0.02, 6), 'white', hb)
            self.part('lfang%d' % sx, cone((0.05 * sx, -0.75, 1.52), (0.048 * sx, -0.76, 1.565), 0.016, 6), 'white', jb)
        # the snout (muzzle) and the big nose
        sn = [V((0, -0.54, 1.66)), V((0, -0.68, 1.635)), V((0, -0.84, 1.62))]
        self.part('snout', tube(sn, [(0.15, 0.12), (0.12, 0.10), (0.09, 0.08)], 18, xhint=(1, 0, 0)), 'skin2', hb)
        self.part('bridge', tube([V((0, -0.52, 1.74)), V((0, -0.70, 1.69)), V((0, -0.80, 1.67))], [0.08, 0.06, 0.04], 12),
                  'skin', hb)
        self.part('nose', sphere((0, -0.875, 1.655), (0.07, 0.05, 0.05)), 'dark', hb)
        self.part('mouthin', sphere((0, -0.68, 1.575), (0.085, 0.16, 0.04)), 'dark', hb)
        self.part('lip', tube([V((-0.10, -0.62, 1.58)), V((0, -0.84, 1.57)), V((0.10, -0.62, 1.58))], [0.018] * 3, 8),
                  'dark', hb)
        self.part('jawb', tube([V((0, -0.54, 1.55)), V((0, -0.70, 1.53)), V((0, -0.80, 1.525))],
                               [(0.12, 0.07), (0.10, 0.055), (0.07, 0.045)], 16, xhint=(1, 0, 0)), 'skin2', jb)
        self.part('tongue', sphere((0, -0.70, 1.555), (0.05, 0.10, 0.02)), 'dark', jb)

    def part(self, name, g, keys, bone, sub=1, tag=None):
        # jaw parts are modelled round the unscaled hinge
        if bone == 'jaw':
            ob = G.make_obj('%s_%s' % (self.name, name), g, keys, sub, local_origin=self._jaw_rest)
            self.parts.append((ob, bone, tag))
            return ob
        return Boss.part(self, name, g, keys, bone, sub, tag)

    def show(self, tag, P):
        dz = P.get('dizzy', False)
        if tag == 'eye':
            return not dz
        if tag == 'eyedizzy':
            return dz
        return True

    def mouth(self):
        """A dark wedge filling the open mouth between the snout and the jaw
        (so an open jaw reads as a mouth, not as a gap)."""
        r = self.rig
        g = G.Geo()
        hs = self.HEAD
        hp = r.head['head']

        def hpt(p):         # a rest point of the (scaled) head, posed
            return r.M['head'] @ ((V(p) - hp) * hs)

        def jpt(p):
            return r.M['jaw'] @ ((V(p) - self._jaw_rest) * hs)
        sides = []
        for x in (-0.055, 0.055):
            loop = [hpt((x, -0.50, 1.575)), hpt((x, -0.62, 1.585)), hpt((x, -0.74, 1.58)), hpt((x, -0.82, 1.575)),
                    jpt((x, -0.79, 1.545)), jpt((x, -0.72, 1.55)), jpt((x, -0.62, 1.555))]
            sides.append([g.vert(q) for q in loop])
        A, B = sides
        for S_ in (A, B):              # h u1 u2 u3 l3 l2 l1
            g.face((S_[0], S_[1], S_[6]))
            g.face((S_[1], S_[2], S_[5], S_[6]))
            g.face((S_[2], S_[3], S_[4], S_[5]))
        for k in range(7):
            k1 = (k + 1) % 7
            g.face((A[k], A[k1], B[k1], B[k]))
        return g

    def deform(self, P):
        """The bushy tail, swishing behind the hips; the open mouth."""
        r = self.rig
        sw = P.get('tail', 0.0)
        up = P.get('tail_up', 0.0)
        pts, rs = [], []
        for k in range(8):
            t = k / 7
            p = V((0.16 * math.sin(sw + t * 2.0) * t, 0.34 + 0.46 * t, 0.84 + (0.06 + up) * t - 0.34 * t * t * (1 - up)))
            pts.append(r.pt('pelvis', p))
            rs.append(0.07 + 0.13 * math.sin(math.pi * min(1.0, 0.15 + t * 0.95)))
        rs[-1] = 0.05
        g = tube(pts, rs, 16, xhint=(1, 0, 0))
        out = [(g, 'skin', 1, 'tail')]
        # a light tip and a few tufts so it reads bushy
        d = (pts[-1] - pts[-2]).normalized()
        out.append((cone(pts[-2], pts[-1] + d * 0.12, 0.10, 12), 'skin2', 1, 'tailtip'))
        for k, t in enumerate((2, 3, 4, 5)):
            a = pts[t]
            dd = (pts[t + 1] - pts[t - 1]).normalized()
            side = dd.cross(V((0, 0, 1))).normalized() * (1 if k % 2 else -1)
            out.append((cone(a, a + side * 0.12 + dd * 0.14 + V((0, 0, 0.04)), 0.07, 8), 'skin', 1, 'tt%d' % k))
        if P['rot'].get('jaw', (0, 0, 0))[0] > 12:
            out.append((self.mouth(), 'dark', 0, 'mouth'))
        return out

    def pose(self, anim, f, n):
        P = {'rot': {}, 'off': {}, 'ik': [], 'world': {}}
        R, O = P['rot'], P['off']
        r = self.rig
        if anim == 'run':
            ph = cyc(f, n)
            tilt = 48 + 5 * math.sin(ph)
            O['pelvis'] = V((0, 0.12, -0.20 + 0.06 * math.sin(2 * ph + 0.5)))
            R['pelvis'] = (tilt - 6, 0, 0)
            R['spine'] = (6 + 6 * math.cos(ph), 0, 0)
            # the head looks ahead and a little up, lifted on the neck
            wp = -14
            R['head'] = (wp - (tilt - 6) - (6 + 6 * math.cos(ph)), 0, 0)
            O['head'] = V((0, 0.02, 0.10))
            R['jaw'] = (14, 0, 0)
            for s, sx, dp in (('L', 1, 0.0), ('R', -1, 0.45)):
                p = ph + dp
                y = -0.80 - 0.20 * math.cos(p)
                lift = 0.20 * max(0.0, -math.sin(p))
                P['ik'].append(('sh' + s, 'el' + s, 'ha' + s, V((0.42 * sx, y, 0.14 + lift)), (0.4 * sx, 1, 0.3), (0, 1, 0)))
                P['world']['ha' + s] = rot((-10 + 40 * max(0.0, -math.sin(p)), 0, 0))
                y = 0.52 + 0.30 * math.cos(p)
                lift = 0.18 * max(0.0, math.sin(p))
                P['ik'].append(('th' + s, 'kn' + s, 'ho' + s, V((0.28 * sx, y + 0.08, 0.24 + lift)), (0, -1, 0.3)))
                P['world']['ho' + s] = rot((-35 + 25 * math.cos(p), 0, 0))
                P['world']['pa' + s] = rot((0, 0, 0))
            P['tail'] = 0.5 * math.sin(ph)
            P['tail_up'] = 0.10
        elif anim == 'howl':
            # 00 straighten, 01 chest up, 02 head back and howl, 03 hold, 04 back down
            k = [0.35, 0.75, 1.0, 1.0, 0.45][f]
            hw = [0.0, 0.3, 1.0, 1.0, 0.2][f]
            pp, sp = -8 * k, -20 * k
            R['pelvis'] = (pp, 0, 0)
            R['spine'] = (sp, 0, 0)
            O['pelvis'] = V((0, 0, 0.05 * k))
            wp = lerp(-8, -60, hw)                   # the snout's pitch in the world
            R['head'] = (wp - pp - sp, 0, 0)
            R['jaw'] = (30 * hw + (4 if f == 3 else 0), 0, 0)
            for s, sx in (('L', 1), ('R', -1)):
                # arms hang down in the world (the spine leans back), a bit out
                R['sh' + s] = (-(pp + sp) + 10 * k, lerp(0, 12, k) * sx, 0)
                R['el' + s] = (lerp(-25, -30, k), 0, 0)
                R['ha' + s] = (-10, 0, 0)
                P['ik'].append(('th' + s, 'kn' + s, 'ho' + s, r.head['ho' + s] + V((0, 0.02 * k, 0)), (0, -1, 0.3)))
            P['tail'] = 0.2 * math.sin(f)
            P['tail_up'] = 0.15 * k
        elif anim == 'stun':
            # sitting on his haunches, leaning back, head wobbling (the watch adds stars)
            wob = [-1, 0, 1, 0][f]
            pp, sp = -30, -12
            O['pelvis'] = V((0, 0.14, -0.50))
            R['pelvis'] = (pp, 0, 0)
            R['spine'] = (sp, 5 * wob, 0)
            R['head'] = (8 - pp - sp, 14 * wob, 8 * wob)
            R['jaw'] = (16, 0, 0)
            for s_, sx in (('L', 1), ('R', -1)):
                R['sh' + s_] = (-(pp + sp) - 18, 6 * sx, 0)
                R['el' + s_] = (-38, 0, 0)
                R['ha' + s_] = (-20, 0, 0)
                P['ik'].append(('th' + s_, 'kn' + s_, 'ho' + s_, V((0.34 * sx, -0.50, 0.12)), (0.3 * sx, 0.2, 1), (0, -1, 0)))
                P['world']['ho' + s_] = rot((-90, 0, 0))
            P['dizzy'] = True
            P['tail'] = 0.3 * wob
            P['tail_up'] = -0.4
        return P


# ===========================================================================
# registry and main
# ===========================================================================

BOSSES = {'brute': Brute, 'count': Count, 'pharaoh': Pharaoh, 'alpha': Alpha}

SAMPLE = [('brute', 'walk', 's', 0), ('brute', 'stomp', 's', 3), ('count', 'glide', 's', 0),
          ('pharaoh', 'walk', 's', 0), ('alpha', 'run', 's', 0), ('alpha', 'howl', 'e', 2)]


def own_args():
    """Our own flags, taken out of sys.argv before mh_common's parser sees
    them (argparse would read --sample as an abbreviation of --samples)."""
    argv = sys.argv
    k = argv.index('--') + 1 if '--' in argv else len(argv)
    o = {'sample': False, 'dirs': None, 'frames': None, 'joints': False}
    rest = []
    i = k
    while i < len(argv):
        x = argv[i]
        if x == '--sample':
            o['sample'] = True
        elif x == '--joints':
            o['joints'] = True
        elif x in ('--dirs', '--frames') and i + 1 < len(argv):
            v = argv[i + 1].split(',')
            o[x[2:]] = v if x == '--dirs' else [int(t) for t in v]
            i += 1
        else:
            rest.append(x)
        i += 1
    sys.argv[k:] = rest
    return o


def wanted(only, boss, anim, d, f):
    if not only:
        return True
    fname = '%s_%s_%s_%02d' % (boss, anim, d, f)
    return any(o in (boss, boss + '_' + anim, fname) for o in only)


def main():
    o = own_args()
    a = C.args()
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'palettes.json'), 'w') as fh:
        json.dump({b: {str(k): v for k, v in p.items()} for b, p in PALETTES.items()}, fh, indent=1)
    times = []
    for bname, cls in BOSSES.items():
        jobs = []
        for anim, frames, ms, dirs in cls.anims:
            for d in dirs:
                if o['dirs'] and d not in o['dirs']:
                    continue
                for f in range(frames):
                    if o['frames'] is not None and f not in o['frames']:
                        continue
                    if o['sample'] and (bname, anim, d, f) not in SAMPLE:
                        continue
                    if not wanted(a.only, bname, anim, d, f):
                        continue
                    jobs.append((anim, frames, ms, d, f))
        # the album card: the first pose facing s, bigger (SPEC section 12)
        a0 = cls.anims[0]
        if not o['sample'] and (not a.only or any(x in (bname, 'card_' + bname) for x in a.only)) \
                and (not o['dirs'] or 's' in o['dirs']):
            jobs.append(('card',) + (a0[0], a0[1], a0[2], 's', 0))
        if not jobs:
            continue
        C.reset('neutral', cpu=a.cpu)
        materials(bname)
        boss = cls()
        boss.build()
        for job in jobs:
            card = job[0] == 'card'
            anim, frames, ms, d, f = job[1:] if card else job
            t0 = time.time()
            P = boss.pose(anim, f, frames)
            objs = boss.place(P, DIRS[d])
            bpy.context.view_layer.update()
            if o['joints']:
                for b in boss.rig.order:
                    t = boss.rig.M[b].translation
                    print('  %-8s rest %s  posed (%.2f, %.2f, %.2f)' % (b, tuple(round(c, 2) for c in boss.rig.head[b]),
                                                                    t.x, t.y, t.z))
            nm = '%s_%s_%s_%02d' % (bname, anim, d, f)
            extra = {'anim': anim, 'dir': d, 'frame': f, 'frames': frames, 'ms': ms,
                     'boss': bname, 'height_m': cls.height, 'footprint': list(cls.footprint),
                     'ids': {str(k): v for k, v in IDS[bname].items()}}
            if card:
                nm = 'card_' + bname
                extra.update(anim=anim, frame=0, zoom=1.2)
                for k in ('frames', 'ms'):
                    extra.pop(k)
                C.render_sprite(out, nm, objs, boss.anchor, passes=('light', 'id'), samples=a.samples,
                                bounce_ground=0.0, kind='card', extra=extra, zoom=1.2)
            else:
                C.render_sprite(out, nm, objs, boss.anchor, passes=('light', 'id', 'z', 'shadow'),
                                samples=a.samples, shadow_z=0.0, bounce_ground=0.0, kind='char',
                                extra=extra)
            boss.clear()
            dt = time.time() - t0
            times.append(dt)
            print('[bosses] %s %.1fs' % (nm, dt), flush=True)
        C.save_meta(out)
    if times:
        print('[bosses] %d frames, %.1fs avg, %.0fs total' % (len(times), sum(times) / len(times), sum(times)))


main()
