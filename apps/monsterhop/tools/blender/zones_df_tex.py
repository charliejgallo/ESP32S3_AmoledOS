"""Monster Hop - desert & forest zones: procedural textures in plain numpy.

No bpy here: the same functions run inside Blender (to build the image
textures of the blocks) and in plain python3 (to preview a texture as a PNG
while designing it). Private helper of desert.py / forest.py.

Conventions
-----------
TEX pixels per metre. Every array is indexed [row, col] with row 0 at the
BOTTOM of the texture (v = 0), the way Blender stores image pixels.

  top textures   (TEX x TEX)       x, y in [0, 1): the cell seen from above,
                                   row = y (front edge y = 0 at row 0)
  side textures  (SIDE_ROWS x TEX) u in [0, 1), z in [-FLOOR_M, 0): the block's
                                   front face (u = x) and right face (u = 1 + y,
                                   the same texture repeated), row 0 at the
                                   bottom of the block

Everything is exactly periodic (1 m in x, y, u; one floor in z) so tiles join
their neighbours and fills stack: the noise is FFT-filtered white noise on the
torus, the stripes use whole numbers of waves per cell.
"""
import math

import numpy as np

TEX = 256
FLOOR_M = 23.0 / (math.hypot(60.0, 20.0) * math.sqrt(1.0 - 0.49))   # 0.50923 m
SIDE_ROWS = int(round(FLOOR_M * TEX))                               # 130


# ---------------------------------------------------------------------------
# grids, noise, maths
# ---------------------------------------------------------------------------

def top_grid(n=TEX):
    t = (np.arange(n) + 0.5) / n
    x, y = np.meshgrid(t, t)
    return x, y


def side_grid():
    u = (np.arange(TEX) + 0.5) / TEX
    v = (np.arange(SIDE_ROWS) + 0.5) / SIDE_ROWS
    U, Vv = np.meshgrid(u, v)
    return U, -FLOOR_M + Vv * FLOOR_M          # u in m, z in m (-F..0)


def fnoise(shape, sigma, seed, aniso=(1.0, 1.0)):
    """Periodic smooth noise: white noise blurred by a Gaussian of `sigma`
    metres (times aniso = (sx, sy)) on the torus. Zero mean, unit std."""
    rng = np.random.default_rng(seed)
    h, w = shape
    n = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None] * TEX
    fx = np.fft.fftfreq(w)[None, :] * TEX
    sx, sy = sigma * aniso[0], sigma * aniso[1]
    filt = np.exp(-2.0 * math.pi ** 2 * ((fx * sx) ** 2 + (fy * sy) ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(n) * filt))
    out -= out.mean()
    return out / (out.std() + 1e-12)


def fbm(shape, sigma, seed, octaves=4, gain=0.55, aniso=(1.0, 1.0)):
    out = np.zeros(shape)
    a, tot = 1.0, 0.0
    for k in range(octaves):
        out += a * fnoise(shape, sigma / (2.0 ** k), seed * 31 + k, aniso)
        tot += a * a
        a *= gain
    return out / math.sqrt(tot)


def noise1(n, sigma, seed):
    """Periodic 1-D noise of n samples (over one metre)."""
    rng = np.random.default_rng(seed)
    x = rng.standard_normal(n)
    f = np.fft.fftfreq(n) * TEX
    out = np.real(np.fft.ifft(np.fft.fft(x) * np.exp(-2 * math.pi ** 2 * (f * sigma) ** 2)))
    out -= out.mean()
    return out / (out.std() + 1e-12)


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    t = np.asarray(t, dtype=float)
    if t.ndim and (a.ndim == 1 or b.ndim == 1 or (a.ndim == 3) or (b.ndim == 3)):
        t = t[..., None]
    return a + (b - a) * t


def rgb(*c):
    """sRGB 0-255 -> linear 0-1 (albedo design values are written in sRGB)."""
    c = np.asarray(c, dtype=float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb8(lin):
    x = np.clip(lin, 0, 1)
    s = np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)
    return np.round(s * 255).astype(np.uint8)


def fill(shape, col):
    return np.broadcast_to(np.asarray(col, float), shape + (3,)).copy()


def edge_dist(x, y):
    return np.minimum(np.minimum(x, 1 - x), np.minimum(y, 1 - y))


def cover(d, px=1.0 / TEX):
    """Antialiased coverage of a signed distance (negative inside)."""
    return np.clip(0.5 - d / px, 0.0, 1.0)


# signed distances (metres) --------------------------------------------------

def sd_circle(x, y, cx, cy, r):
    return np.hypot(x - cx, y - cy) - r


def sd_ellipse(x, y, cx, cy, rx, ry, ang=0.0):
    c, s = math.cos(ang), math.sin(ang)
    dx, dy = x - cx, y - cy
    px, py = (dx * c + dy * s) / rx, (-dx * s + dy * c) / ry
    k = np.hypot(px, py)
    return (k - 1.0) * min(rx, ry)


def sd_box(x, y, cx, cy, hx, hy, r=0.0):
    qx, qy = np.abs(x - cx) - hx + r, np.abs(y - cy) - hy + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def sd_seg(x, y, ax, ay, bx, by, r):
    px, py = x - ax, y - ay
    dx, dy = bx - ax, by - ay
    h = np.clip((px * dx + py * dy) / (dx * dx + dy * dy + 1e-12), 0, 1)
    return np.hypot(px - dx * h, py - dy * h) - r


def sd_poly(x, y, pts):
    """Signed distance to a closed polygon [(x, y), ...]."""
    pts = [tuple(p) for p in pts]
    d = np.full(x.shape, 1e9)
    inside = np.zeros(x.shape, bool)
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        d = np.minimum(d, sd_seg(x, y, ax, ay, bx, by, 0.0))
        cond = ((ay > y) != (by > y)) & (x < (bx - ax) * (y - ay) / (by - ay + 1e-12) + ax)
        inside ^= cond
    return np.where(inside, -d, d)


def sd_polyline(x, y, pts, r):
    d = np.full(x.shape, 1e9)
    for (ax, ay), (bx, by) in zip(pts[:-1], pts[1:]):
        d = np.minimum(d, sd_seg(x, y, ax, ay, bx, by, r))
    return d


# ---------------------------------------------------------------------------
# a texture = colour (linear RGB), height (m-ish, for bump), roughness
# ---------------------------------------------------------------------------

class Tex:
    def __init__(self, col, hgt=None, rough=None, emit=None, metal=None):
        self.metal = metal
        self.col = col
        self.hgt = np.zeros(col.shape[:2]) if hgt is None else hgt
        self.rough = rough
        self.emit = emit

    def preview(self, path, scale=2):
        from PIL import Image
        im = Image.fromarray(to_srgb8(self.col[::-1]), 'RGB')
        im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(path)


def bands(t, keys, period):
    """Periodic colour bands: keys [(pos, colour, soft), ...] sorted in [0,
    period); between key k and k+1 the colour eases over `soft` (in t units)
    just before key k+1, so bands have crisp but antialiased boundaries."""
    t = np.mod(t, period)
    n = len(keys)
    col = np.zeros(t.shape + (3,))
    for k in range(n):
        p0, c0, _ = keys[k]
        p1, c1, soft1 = keys[(k + 1) % n]
        if k == n - 1:
            p1 = p1 + period
        tt = np.where(t < p0, t + period, t) if k == n - 1 else t
        inside = (tt >= p0) & (tt < p1)
        w = sstep(p1 - soft1, p1, tt)
        c = lerp(c0, c1, w)
        col = np.where(inside[..., None], c, col)
    first = t < keys[0][0]
    if first.any():
        p0, c0, _ = keys[-1]
        p1, c1, soft1 = keys[0]
        tt = t + period
        w = sstep(p1 + period - soft1, p1 + period, tt)
        col = np.where(first[..., None], lerp(c0, c1, w), col)
    return col


def darken_top_edges(col, x, y, amount=0.07, width=0.025):
    """The faint grid: tops darken towards the cell's edges."""
    d = edge_dist(x, y)
    k = 1.0 - amount * (1.0 - sstep(0.0, width, d))
    return col * k[..., None]


def pebbles(x, y, col, hgt, rng, n, rmin, rmax, cols, region=0.12, shade=0.55, avoid=None):
    """Scatter small stones (ellipses) away from the cell's edges, each with a
    soft contact shadow towards the back-right (the sun is front-left)."""
    placed = []
    for _ in range(n * 20):
        if len(placed) >= n:
            break
        r = rng.uniform(rmin, rmax)
        cx, cy = rng.uniform(region + r, 1 - region - r, 2)
        if any(math.hypot(cx - px, cy - py) < r + pr + 0.02 for px, py, pr in placed):
            continue
        if avoid is not None and avoid(cx, cy):
            continue
        placed.append((cx, cy, r))
    for cx, cy, r in placed:
        ang = rng.uniform(0, math.pi)
        e = rng.uniform(0.65, 0.95)
        dsh = sd_ellipse(x, y, cx + r * 0.35, cy + r * 0.35, r * 1.05, r * e * 1.05, ang)
        sh = np.clip(1 - np.maximum(dsh, 0) / (r * 0.5), 0, 1) * shade
        col *= (1 - 0.45 * sh)[..., None]
        d = sd_ellipse(x, y, cx, cy, r, r * e, ang)
        m = cover(d)
        c = np.asarray(cols[rng.integers(len(cols))], float) * rng.uniform(0.85, 1.1)
        dome = np.clip(-d / r, 0, 1) ** 0.5
        # lit from the front-left: brighter on the -x -y side of the stone
        lx = (x - cx) / r
        ly = (y - cy) / r
        light = 1.0 + 0.25 * np.clip(-(lx + ly) * 0.7, -1, 1)
        col[:] = lerp(col, c[None, None, :] * light[..., None], m)
        hgt += m * dome * r * 0.8
    return placed
