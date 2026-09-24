#!/usr/bin/env python3
"""
Sample models for the 3D viewer, written into the current directory:

  nudo.stl              a (2,3) torus knot, binary STL, 8 640 triangles
  engranaje_ascii.stl   a 14-tooth gear, ASCII STL, 448 triangles
  esfera_grande.stl     a bumpy sphere, binary STL, 80 000 triangles: over the
                        budget, so the watch reduces it on loading
  planeta.m3d           a low-poly planet with coloured faces, the portal's
                        format (Y up)

STL is Z up, as CAD writes it; the viewer turns it Y up on loading.

    python3 apps/visor3d/tools/samples.py && curl ... /api/upload?dir=3d
"""
import math, struct

def write_stl(name, tris):
    with open(name, 'wb') as f:
        f.write(b'AmoledOS visor3d sample'.ljust(80, b' '))
        f.write(struct.pack('<I', len(tris)))
        for a, b, c in tris:
            f.write(struct.pack('<3f', 0, 0, 0))
            for v in (a, b, c):
                f.write(struct.pack('<3f', *v))
            f.write(b'\0\0')

def write_ascii(name, tris):
    with open(name, 'w') as f:
        f.write('solid sample\n')
        for a, b, c in tris:
            f.write(' facet normal 0 0 0\n  outer loop\n')
            for v in (a, b, c):
                f.write('   vertex %f %f %f\n' % v)
            f.write('  endloop\n endfacet\n')
        f.write('endsolid sample\n')

def grid_surface(fn, nu, nv, wrap_u=True, wrap_v=True):
    tris = []
    for i in range(nu):
        for j in range(nv):
            i2 = (i + 1) % nu if wrap_u else i + 1
            j2 = (j + 1) % nv if wrap_v else j + 1
            a, b, c, d = fn(i, j), fn(i2, j), fn(i2, j2), fn(i, j2)
            tris += [(a, b, c), (a, c, d)]
    return tris

def knot(nu=360, nv=12, p=2, q=3, tube=0.4):
    def pt(t):
        r = math.cos(q * t) + 2
        return (r * math.cos(p * t), r * math.sin(p * t), -math.sin(q * t))
    def f(i, j):
        t = 2 * math.pi * i / nu
        s = 2 * math.pi * j / nv
        a, b = pt(t), pt(t + 1e-3)
        T = [b[k] - a[k] for k in range(3)]
        L = math.sqrt(sum(x * x for x in T)); T = [x / L for x in T]
        B = [T[1] * a[2] - T[2] * a[1], T[2] * a[0] - T[0] * a[2], T[0] * a[1] - T[1] * a[0]]
        L = math.sqrt(sum(x * x for x in B)); B = [x / L for x in B]
        N = [B[1] * T[2] - B[2] * T[1], B[2] * T[0] - B[0] * T[2], B[0] * T[1] - B[1] * T[0]]
        return tuple(a[k] + tube * (math.cos(s) * N[k] + math.sin(s) * B[k]) for k in range(3))
    return grid_surface(f, nu, nv)

def bumpy_sphere(n=200):
    def f(i, j):
        u = 2 * math.pi * i / n
        v = math.pi * j / n
        r = 1 + 0.08 * math.sin(6 * u) * math.sin(5 * v)
        return (r * math.sin(v) * math.cos(u), r * math.sin(v) * math.sin(u), r * math.cos(v))
    return grid_surface(f, n, n, True, False)

def gear(teeth=14, R=1.0, r=0.8, h=0.35, hole=0.25):
    pts = []
    for t in range(teeth):
        for ang, rad in ((0, r), (0.12, R), (0.38, R), (0.5, r)):
            a = 2 * math.pi * (t + ang) / teeth
            pts.append((rad * math.cos(a), rad * math.sin(a)))
    n = len(pts)
    inner = [(hole * math.cos(2 * math.pi * i / n), hole * math.sin(2 * math.pi * i / n)) for i in range(n)]
    tris = []
    for i in range(n):
        j = (i + 1) % n
        o0, o1, i0, i1 = pts[i], pts[j], inner[i], inner[j]
        for z, flip in ((h, False), (0, True)):
            A, B = (o0[0], o0[1], z), (o1[0], o1[1], z)
            C, D = (i1[0], i1[1], z), (i0[0], i0[1], z)
            tris += [(A, C, B), (A, D, C)] if flip else [(A, B, C), (A, C, D)]
        tris += [((o0[0], o0[1], 0), (o1[0], o1[1], 0), (o1[0], o1[1], h)),
                 ((o0[0], o0[1], 0), (o1[0], o1[1], h), (o0[0], o0[1], h))]
        tris += [((i0[0], i0[1], 0), (i1[0], i1[1], h), (i1[0], i1[1], 0)),
                 ((i0[0], i0[1], 0), (i0[0], i0[1], h), (i1[0], i1[1], h))]
    return tris

def planet(sub=4):
    t = (1 + 5 ** .5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    v = [tuple(c / math.sqrt(sum(x * x for x in p)) for c in p) for p in v]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
         (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
         (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(sub):
        cache, nf = {}, []
        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                p = [(v[a][k] + v[b][k]) / 2 for k in range(3)]
                L = math.sqrt(sum(x * x for x in p))
                v.append(tuple(x / L for x in p))
                cache[key] = len(v) - 1
            return cache[key]
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    hgt = [1 + 0.12 * (math.sin(3 * p[0] + 1) * math.cos(4 * p[1]) + math.sin(5 * p[2])) for p in v]
    v = [tuple(x * hgt[i] for x in p) for i, p in enumerate(v)]
    def c565(r, g, b):
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    cols = []
    for a, b, c in f:
        hh = (hgt[a] + hgt[b] + hgt[c]) / 3
        rgb = (30, 90, 200) if hh < 0.98 else (230, 210, 140) if hh < 1.02 else \
              (60, 170, 70) if hh < 1.12 else (240, 240, 245)
        cols.append(c565(*rgb))
    with open('planeta.m3d', 'wb') as fo:
        fo.write(b'M3D1' + struct.pack('<III', len(v), len(f), 1))
        for p in v:
            fo.write(struct.pack('<3f', *p))
        for tr in f:
            fo.write(struct.pack('<3I', *tr))
        for c in cols:
            fo.write(struct.pack('<H', c))
    return len(f)

if __name__ == '__main__':
    k = knot(); write_stl('nudo.stl', k)
    g = gear(); write_ascii('engranaje_ascii.stl', g)
    s = bumpy_sphere(); write_stl('esfera_grande.stl', s)
    n = planet()
    print('nudo.stl %d, engranaje_ascii.stl %d, esfera_grande.stl %d, planeta.m3d %d triangles'
          % (len(k), len(g), len(s), n))
