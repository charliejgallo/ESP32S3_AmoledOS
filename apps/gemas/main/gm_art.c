/*
 * GEMAS - the jewels' art (see gm_art.h)
 *
 * Everything is computed once, when the app opens. On the Mac it is a few
 * milliseconds; on the board, with the S3's FPU, it does not hurt either: it
 * is 44x44 pixels per sprite and eleven sprites.
 */
#include "gm_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Shapes
 *
 * Each jewel is a convex polygon containing the origin, with its vertices in
 * normalised coordinates: 1.0 is the sprite's radius and Y grows downwards (as
 * on the screen). The order of the vertices does not matter as long as it is
 * convex: the normals orient themselves outwards.
 * -------------------------------------------------------------------------- */

#define GM_MAX_V    12

typedef struct {
    int      nv;                /* 0 = sphere (the pearl) */
    float    v[GM_MAX_V][2];
    uint32_t base, light, dark;
    bool     rainbow;           /* the colour comes from the angle: the hypercube */
} gm_shape_t;

static const gm_shape_t s_shapes[GM_TYPES] = {
    /* 0 - ruby: a square with the corners cut off */
    { 8, {{ 0.98f, 0.58f}, { 0.58f, 0.98f}, {-0.58f, 0.98f}, {-0.98f, 0.58f},
          {-0.98f,-0.58f}, {-0.58f,-0.98f}, { 0.58f,-0.98f}, { 0.98f,-0.58f}},
      0xE81E32, 0xFF8E86, 0x6B0714, false },

    /* 1 - emerald: a tall rectangle with a step cut */
    { 8, {{ 0.78f, 0.62f}, { 0.46f, 1.00f}, {-0.46f, 1.00f}, {-0.78f, 0.62f},
          {-0.78f,-0.62f}, {-0.46f,-1.00f}, { 0.46f,-1.00f}, { 0.78f,-0.62f}},
      0x1FC24E, 0xA8FFA2, 0x085421, false },

    /* 2 - sapphire: brilliant cut, wide crown and a point below */
    { 5, {{-0.56f,-0.92f}, { 0.56f,-0.92f}, { 0.96f,-0.20f}, { 0.00f, 1.00f},
          {-0.96f,-0.20f}},
      0x2E7BFF, 0xA6D6FF, 0x0A2A8C, false },

    /* 3 - citrine: rhombus */
    { 4, {{ 0.00f,-1.00f}, { 0.86f, 0.00f}, { 0.00f, 1.00f}, {-0.86f, 0.00f}},
      0xFFD11E, 0xFFF9B0, 0x8F6000, false },

    /* 4 - amethyst: triangle */
    { 3, {{ 0.00f,-0.98f}, { 0.94f, 0.76f}, {-0.94f, 0.76f}},
      0xE23BE0, 0xFFAEF6, 0x66086A, false },

    /* 5 - amber: hexagon */
    { 6, {{-0.54f,-0.94f}, { 0.54f,-0.94f}, { 0.99f, 0.00f}, { 0.54f, 0.94f},
          {-0.54f, 0.94f}, {-0.99f, 0.00f}},
      0xFF8A14, 0xFFD79A, 0x8C3600, false },

    /* 6 - pearl: a sphere, with no facets */
    { 0, {{0}}, 0xD9E1EE, 0xFFFFFF, 0x59637A, false },
};

/* The hypercube: twelve sides and the colour taken from the angle. */
static const gm_shape_t s_hyper = {
    12, {{ 0.26f,-0.97f}, { 0.71f,-0.71f}, { 0.97f,-0.26f}, { 0.97f, 0.26f},
         { 0.71f, 0.71f}, { 0.26f, 0.97f}, {-0.26f, 0.97f}, {-0.71f, 0.71f},
         {-0.97f, 0.26f}, {-0.97f,-0.26f}, {-0.71f,-0.71f}, {-0.26f,-0.97f}},
    0xFFFFFF, 0xFFFFFF, 0x202030, true
};

uint32_t gm_art_color(int type)
{
    if (type < 0 || type >= GM_TYPES) {
        return 0xFFFFFF;
    }
    return s_shapes[type].base;
}

uint32_t gm_art_color_light(int type)
{
    if (type < 0 || type >= GM_TYPES) {
        return 0xFFFFFF;
    }
    return s_shapes[type].light;
}

/* --------------------------------------------------------------------------
 * Colour
 * -------------------------------------------------------------------------- */

static inline uint16_t rgb565(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint32_t hex_mix(uint32_t a, uint32_t b, float f)
{
    if (f <= 0.0f) return a;
    if (f >= 1.0f) return b;
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int r = ar + (int)((float)(br - ar) * f);
    int g = ag + (int)((float)(bg - ag) * f);
    int bl = ab + (int)((float)(bb - ab) * f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* A continuous rainbow in 60-degree sectors; 'h' runs from 0 to 1. */
static uint32_t rainbow_at(float h)
{
    static const uint32_t stops[7] = {
        0xFF1040, 0xFF8000, 0xFFE000, 0x20E848, 0x00B0FF, 0x8030FF, 0xFF1040
    };
    h -= floorf(h);
    float p = h * 6.0f;
    int i = (int)p;
    if (i > 5) i = 5;
    return hex_mix(stops[i], stops[i + 1], p - (float)i);
}

/* --------------------------------------------------------------------------
 * Sprites
 * -------------------------------------------------------------------------- */

static bool sprite_alloc(gm_sprite_t *sp, int w, int h, bool alpha)
{
    size_t bytes = (size_t)w * (size_t)h * (alpha ? 3u : 2u);
    sp->data = (uint8_t *)malloc(bytes);
    if (!sp->data) {
        return false;
    }
    memset(sp->data, 0, bytes);

    memset(&sp->dsc, 0, sizeof(sp->dsc));
    sp->dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    sp->dsc.header.cf     = alpha ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
    sp->dsc.header.w      = (uint32_t)w;
    sp->dsc.header.h      = (uint32_t)h;
    sp->dsc.header.stride = (uint32_t)(w * 2);
    sp->dsc.data_size     = (uint32_t)bytes;
    sp->dsc.data          = sp->data;
    return true;
}

/* In RGB565A8 the colour plane comes first and the alpha one afterwards, with
 * half the line width. lv_draw_sw_img.c says so and it has to be honoured. */
static inline void sprite_px(gm_sprite_t *sp, int w, int h, int x, int y,
                             uint16_t color, uint8_t alpha)
{
    uint16_t *cp = (uint16_t *)sp->data;
    cp[y * w + x] = color;
    sp->data[(size_t)w * h * 2 + (size_t)y * w + x] = alpha;
}

/* --------------------------------------------------------------------------
 * Tracing the jewel
 *
 * For a given direction, the distance to the polygon's edge is the smallest
 * positive t among the edges facing that way. Since the polygon is convex and
 * contains the origin, there is no need to check that the intersection falls
 * inside the segment.
 * -------------------------------------------------------------------------- */

typedef struct {
    float nx, ny;       /* normalised outward normal of each edge     */
    float d;            /* distance from the origin to the edge       */
    float vx, vy;       /* direction of the starting vertex, normalised */
} gm_edge_t;

static void edges_build(const gm_shape_t *sh, gm_edge_t *e)
{
    for (int i = 0; i < sh->nv; i++) {
        const float *a = sh->v[i];
        const float *b = sh->v[(i + 1) % sh->nv];
        float ex = b[0] - a[0], ey = b[1] - a[1];
        float nx = ey, ny = -ex;
        float len = sqrtf(nx * nx + ny * ny);
        if (len < 1e-6f) {
            len = 1e-6f;
        }
        nx /= len;
        ny /= len;
        float d = nx * a[0] + ny * a[1];
        if (d < 0.0f) {             /* orient outwards */
            nx = -nx; ny = -ny; d = -d;
        }
        e[i].nx = nx;
        e[i].ny = ny;
        e[i].d  = d;

        float vlen = sqrtf(a[0] * a[0] + a[1] * a[1]);
        if (vlen < 1e-6f) {
            vlen = 1e-6f;
        }
        e[i].vx = a[0] / vlen;
        e[i].vy = a[1] / vlen;
    }
}

/* Distance from the origin to the edge in the direction (dx,dy), which must
 * arrive normalised. It also returns the facet. */
static float edge_hit(const gm_edge_t *e, int nv, float dx, float dy, int *facet)
{
    float best = 1e9f;
    int   who  = 0;

    for (int i = 0; i < nv; i++) {
        float denom = e[i].nx * dx + e[i].ny * dy;
        if (denom <= 1e-5f) {
            continue;               /* that edge faces the other way */
        }
        float t = e[i].d / denom;
        if (t > 0.0f && t < best) {
            best = t;
            who  = i;
        }
    }
    *facet = who;
    return best;
}

/* Light: from the top left, as everywhere in the system. */
#define LX      (-0.55f)
#define LY      (-0.80f)

static void render_gem(gm_sprite_t *sp, const gm_shape_t *sh)
{
    const int   w = GM_SPRITE, h = GM_SPRITE;
    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    const float radius = (float)GM_SPRITE * 0.5f - 2.5f;    /* leaves some air */

    gm_edge_t edges[GM_MAX_V];
    if (sh->nv > 0) {
        edges_build(sh, edges);
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = ((float)x + 0.5f - cx) / radius;
            float dy = ((float)y + 0.5f - cy) / radius;
            float len = sqrtf(dx * dx + dy * dy);

            float bound = 1.0f;
            int   facet = 0;
            if (sh->nv > 0 && len > 1e-4f) {
                bound = edge_hit(edges, sh->nv, dx / len, dy / len, &facet);
            }
            float tt = (bound > 1e-4f) ? len / bound : 0.0f;

            /* Coverage: the edge pixels are sampled 3x3 so the silhouette does
             * not come out stepped. Inside and outside it is not needed. */
            int cover = 0;
            if (tt <= 0.86f) {
                cover = 9;
            } else if (tt < 1.16f) {
                for (int sy = 0; sy < 3; sy++) {
                    for (int sx = 0; sx < 3; sx++) {
                        float ux = ((float)x + 0.1667f + 0.3333f * (float)sx - cx) / radius;
                        float uy = ((float)y + 0.1667f + 0.3333f * (float)sy - cy) / radius;
                        float ul = sqrtf(ux * ux + uy * uy);
                        float ub = 1.0f;
                        if (sh->nv > 0 && ul > 1e-4f) {
                            int dummy;
                            ub = edge_hit(edges, sh->nv, ux / ul, uy / ul, &dummy);
                        }
                        if (ul <= ub) {
                            cover++;
                        }
                    }
                }
            }
            if (cover == 0) {
                continue;
            }

            uint32_t base = sh->base, light = sh->light, dark = sh->dark;
            if (sh->rainbow) {
                float ang = atan2f(dy, dx) / 6.28318f + 0.5f;
                base  = rainbow_at(ang);
                light = hex_mix(base, 0xFFFFFF, 0.42f);
                dark  = hex_mix(base, 0x0A0A18, 0.78f);
            }

            float b;
            if (sh->nv == 0) {
                /* Pearl: a real sphere, with no facets. */
                float d2 = dx * dx + dy * dy;
                if (d2 > 1.0f) d2 = 1.0f;
                float nz  = sqrtf(1.0f - d2);
                float lam = dx * (-0.45f) + dy * (-0.60f) + nz * 0.66f;
                if (lam < 0.0f) lam = 0.0f;
                b = 0.16f + 0.78f * lam;
                float s = lam * lam; s *= s; s *= s;     /* lam^8 */
                b += 0.45f * s;
                b += 0.22f * (d2 * d2);                  /* rim light */
            } else {
                const float table = 0.44f;
                const float rim   = 0.90f;
                float fnx = edges[facet].nx, fny = edges[facet].ny;
                float lam = 0.5f + 0.5f * (fnx * LX + fny * LY);

                if (tt < table) {
                    b = 0.68f - 0.17f * dy - 0.12f * tt;
                } else if (tt < rim) {
                    b = 0.24f + 0.64f * lam + 0.12f * (1.0f - tt);
                    if (facet & 1) {
                        b -= 0.05f;
                    }
                } else {
                    b = (0.20f + 0.48f * lam) * 0.88f;
                }

                /* the step between the table and the crown: a light line */
                float dt = tt - table;
                if (dt < 0.0f) dt = -dt;
                if (dt < 0.045f) {
                    b += 0.20f * (1.0f - dt / 0.045f);
                }

                /* edges between facets: slightly darker, so they read */
                if (tt > table) {
                    float cross = dx * edges[facet].vy - dy * edges[facet].vx;
                    if (cross < 0.0f) cross = -cross;
                    if (cross < 0.055f * len) {
                        b -= 0.12f * (1.0f - cross / (0.055f * len + 1e-6f));
                    }
                }

                /* dark outline fillet: it lifts the jewel off the board */
                if (tt > 0.965f) {
                    b *= 0.42f;
                }
            }

            if (b < 0.0f) b = 0.0f;
            if (b > 1.0f) b = 1.0f;

            uint32_t col = (b < 0.5f) ? hex_mix(dark, base, b * 2.0f)
                                      : hex_mix(base, light, (b - 0.5f) * 2.0f);

            /* the hypercube carries a white heart: it is what makes it
             * unmistakable among seven coloured jewels */
            if (sh->rainbow && tt < 0.30f) {
                col = hex_mix(col, 0xFFFFFF, (0.30f - tt) / 0.30f);
            }

            /* Specular highlight: an ellipse at the top left and a small spark
             * at the bottom right. It is what makes it look like glass. */
            if (tt < 0.95f) {
                float sx = (dx + 0.36f) * 1.35f;
                float sy = (dy + 0.44f) * 1.55f;
                float sp2 = 1.0f - (sx * sx + sy * sy);
                if (sp2 > 0.0f) {
                    col = hex_mix(col, 0xFFFFFF, 0.88f * sp2 * sp2);
                }
                float qx = (dx - 0.40f) * 3.0f;
                float qy = (dy - 0.46f) * 3.0f;
                float qp = 1.0f - (qx * qx + qy * qy);
                if (qp > 0.0f) {
                    col = hex_mix(col, light, 0.55f * qp * qp);
                }
            }

            sprite_px(sp, w, h, x, y,
                      rgb565((int)((col >> 16) & 0xFF), (int)((col >> 8) & 0xFF),
                             (int)(col & 0xFF)),
                      (uint8_t)(cover * 255 / 9));
        }
    }
}

/* --------------------------------------------------------------------------
 * Ornaments of the special jewels
 *
 * They go as a separate image, a child of the jewel: that way any colour can
 * carry any special without multiplying the sprites by four.
 * -------------------------------------------------------------------------- */

static void render_flame(gm_sprite_t *sp)
{
    const int w = GM_SPRITE, h = GM_SPRITE;

    /* The flame is the union of two simple shapes: a ball below and a cone
     * rising out of it ending in a point above. It comes out as a teardrop,
     * which is the fire silhouette of all time, and it reads well at 16x29
     * pixels. */
    const float ball_y = 0.40f, ball_r = 0.66f;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float nx = ((float)x + 0.5f - (float)w * 0.5f) / 12.0f;
            float ny = ((float)y + 0.5f - (float)h * 0.5f) / 14.0f;

            float dy = ny - ball_y;
            float s_ball = 1.0f - (nx * nx + dy * dy) / (ball_r * ball_r);

            float s_cone = -1.0f;
            if (ny >= -1.0f && ny <= ball_y) {
                float half = ball_r * (ny + 1.0f) / (1.0f + ball_y);
                if (half > 1e-3f) {
                    float ax = nx < 0.0f ? -nx : nx;
                    s_cone = 1.0f - ax / half;
                }
            }

            float s = s_ball > s_cone ? s_ball : s_cone;
            if (s <= 0.0f) {
                continue;
            }
            if (s > 1.0f) {
                s = 1.0f;
            }

            /* from the red of the edge to the white of the heart */
            uint32_t col = hex_mix(0xFF2A00, 0xFFB020, s * 2.2f > 1.0f ? 1.0f : s * 2.2f);
            if (s > 0.55f) {
                col = hex_mix(col, 0xFFF6D0, (s - 0.55f) / 0.45f);
            }
            uint8_t alpha = (uint8_t)(s < 0.12f ? 255.0f * (s / 0.12f) : 255.0f);
            sprite_px(sp, w, h, x, y,
                      rgb565((int)((col >> 16) & 0xFF), (int)((col >> 8) & 0xFF),
                             (int)(col & 0xFF)), alpha);
        }
    }
}

static void render_star(gm_sprite_t *sp)
{
    const int w = GM_SPRITE, h = GM_SPRITE;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float nx = ((float)x + 0.5f - (float)w * 0.5f) / 20.0f;
            float ny = ((float)y + 0.5f - (float)h * 0.5f) / 20.0f;

            /* astroid: the root of |x| plus the root of |y| less than one. It
             * gives a concave four-pointed star, which is the classic sparkle
             * shape; the same one rotated 45 degrees and smaller adds the
             * other four points */
            float a = sqrtf(nx < 0 ? -nx : nx) + sqrtf(ny < 0 ? -ny : ny);
            float rx = (nx + ny) * 0.7071f / 0.60f;
            float ry = (nx - ny) * 0.7071f / 0.60f;
            float b = sqrtf(rx < 0 ? -rx : rx) + sqrtf(ry < 0 ? -ry : ry);

            float s = 1.0f - (a < b ? a : b);

            /* round halo: without it, the star alone gets lost on top of a
             * strongly coloured jewel */
            float d2 = (nx * nx + ny * ny) / 0.16f;
            float halo = 1.0f - d2;
            if (halo < 0.0f) halo = 0.0f;

            if (s <= 0.0f && halo <= 0.0f) {
                continue;
            }

            float bright = s > 0.0f ? s : 0.0f;
            uint32_t col = hex_mix(0x35C8FF, 0xFFFFFF,
                                   bright * 1.8f > 1.0f ? 1.0f : bright * 1.8f);

            float alpha_star = (s <= 0.0f) ? 0.0f
                             : (s < 0.22f ? 255.0f * (s / 0.22f) : 255.0f);
            float alpha_halo = 190.0f * halo * halo;
            float alpha = alpha_star > alpha_halo ? alpha_star : alpha_halo;

            sprite_px(sp, w, h, x, y,
                      rgb565((int)((col >> 16) & 0xFF), (int)((col >> 8) & 0xFF),
                             (int)(col & 0xFF)), (uint8_t)alpha);
        }
    }
}

/* --------------------------------------------------------------------------
 * The board's background
 *
 * Two by two cells in a chequer, which LVGL repeats with
 * LV_IMAGE_ALIGN_TILE over the board's 352x352: a single object instead of
 * sixty-four.
 * -------------------------------------------------------------------------- */

static void render_tile(gm_sprite_t *sp)
{
    const int w = GM_CELL * 2, h = GM_CELL * 2;
    uint16_t *px = (uint16_t *)sp->data;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int cell_x = x % GM_CELL, cell_y = y % GM_CELL;
            bool odd = ((x / GM_CELL) + (y / GM_CELL)) & 1;
            uint32_t col = odd ? 0x161B2C : 0x101423;

            /* soft vignette towards the centre of the cell */
            float ux = ((float)cell_x + 0.5f) / GM_CELL - 0.5f;
            float uy = ((float)cell_y + 0.5f) / GM_CELL - 0.5f;
            float d  = (ux * ux + uy * uy) * 2.6f;
            col = hex_mix(col, 0x000000, d * 0.55f);

            /* grid line */
            if (cell_x == 0 || cell_y == 0) {
                col = hex_mix(col, 0x000000, 0.55f);
            } else if (cell_x == 1 || cell_y == 1) {
                col = hex_mix(col, 0x2A3350, 0.30f);
            }

            px[y * w + x] = rgb565((int)((col >> 16) & 0xFF),
                                   (int)((col >> 8) & 0xFF), (int)(col & 0xFF));
        }
    }
}

/* -------------------------------------------------------------------------- */

bool gm_art_init(gm_art_t *art)
{
    memset(art, 0, sizeof(*art));

    for (int i = 0; i < GM_TYPES; i++) {
        if (!sprite_alloc(&art->gem[i], GM_SPRITE, GM_SPRITE, true)) {
            gm_art_free(art);
            return false;
        }
        render_gem(&art->gem[i], &s_shapes[i]);
    }
    if (!sprite_alloc(&art->hyper, GM_SPRITE, GM_SPRITE, true) ||
        !sprite_alloc(&art->flame, GM_SPRITE, GM_SPRITE, true) ||
        !sprite_alloc(&art->star,  GM_SPRITE, GM_SPRITE, true) ||
        !sprite_alloc(&art->tile,  GM_CELL * 2, GM_CELL * 2, false)) {
        gm_art_free(art);
        return false;
    }
    render_gem(&art->hyper, &s_hyper);
    render_flame(&art->flame);
    render_star(&art->star);
    render_tile(&art->tile);

    art->ready = true;
    return true;
}

void gm_art_free(gm_art_t *art)
{
    for (int i = 0; i < GM_TYPES; i++) {
        free(art->gem[i].data);
        art->gem[i].data = NULL;
    }
    free(art->hyper.data);
    free(art->flame.data);
    free(art->star.data);
    free(art->tile.data);
    art->hyper.data = art->flame.data = art->star.data = art->tile.data = NULL;
    art->ready = false;
}
