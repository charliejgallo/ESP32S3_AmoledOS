/*
 * TOPOS - the sprites (see tp_art.h)
 *
 * Two kinds of sprite, on purpose:
 *
 *   PROCEDURAL for the big rounded things -a mole's body, the hard hat, the
 *   bomb, the mallet-. Each is a shape tested per pixel and lit from the upper
 *   left, and the light is QUANTISED into four tones (dark, mid, light,
 *   highlight) before an outline goes round it. That is the pixel-art look,
 *   and it is what lets the hat be rendered at eight angles and the mole in
 *   brown and in gold from the same code.
 *
 *   HAND-DRAWN ASCII for everything where a single pixel carries the
 *   expression: eyes, brows, mouths, paws, stars, icons. The palette is
 *   tp_pal() below.
 *
 * Coordinates of the face parts are relative to the head's top centre, which
 * is how tp_draw.c places a mole (TP_HEAD_TOP in topos.h). The eyes image,
 * for instance, has its anchor at (9, -5): its first row lands five rows
 * below the top of the head.
 */
#include "tp_art.h"

#include <stdlib.h>
#include <string.h>

static tp_img_t s_img[TP_IMG_COUNT];

const tp_img_t *tp_img(int id)
{
    return &s_img[id];
}

/* --------------------------------------------------------------------------
 * Palette
 * -------------------------------------------------------------------------- */

uint16_t tp_pal(char ch)
{
    uint32_t hex;

    switch (ch) {
    case 'k': hex = 0x24120A; break;    /* outline, dark brown             */
    case 'K': hex = 0x0A0608; break;    /* pupils                          */
    case 'w': hex = 0xFFFFFF; break;
    case 'W': hex = 0xE6DDCC; break;    /* shaded white                    */
    case 'g': hex = 0xA8A095; break;
    case 'G': hex = 0x5C564F; break;
    case 'p': hex = 0xFF8FA6; break;    /* nose                            */
    case 'P': hex = 0xC8466A; break;
    case 'h': hex = 0xFFD4DE; break;
    case 'r': hex = 0x4A0C1A; break;    /* inside of the mouth             */
    case 'R': hex = 0x8E1E34; break;
    case 'm': hex = 0xF46A86; break;    /* tongue                          */
    case 'M': hex = 0xC43A5C; break;
    case 'y': hex = 0xFFE14D; break;
    case 'Y': hex = 0xE09A12; break;
    case 'o': hex = 0xFF8A1E; break;
    case 'O': hex = 0xD84A12; break;
    case 'c': hex = 0xF4978A; break;    /* blush                           */
    case 'B': hex = 0x6B381C; break;    /* fur                             */
    case 'b': hex = 0x9C5A2C; break;
    case 'n': hex = 0xC47C40; break;
    case 'l': hex = 0xF4D6A8; break;    /* muzzle                          */
    case 'L': hex = 0xDDAE7C; break;
    case 'u': hex = 0x8FDCFF; break;    /* sweat drop                      */
    case 'U': hex = 0x2E8BD8; break;
    case 'e': hex = 0xFF3B4E; break;    /* heart                           */
    case 'E': hex = 0xB0182E; break;
    case 'v': hex = 0xFFB0B8; break;
    default:  return TP_KEY;            /* '.' and anything else           */
    }
    return tp_rgb(hex);
}

/* --------------------------------------------------------------------------
 * Image helpers
 * -------------------------------------------------------------------------- */

static bool img_new(int id, int w, int h, int ax, int ay)
{
    tp_img_t *im = &s_img[id];
    im->px = (uint16_t *)malloc((size_t)w * h * sizeof(uint16_t));
    if (!im->px) {
        return false;
    }
    im->w  = (int16_t)w;
    im->h  = (int16_t)h;
    im->ax = (int16_t)ax;
    im->ay = (int16_t)ay;
    for (int i = 0; i < w * h; i++) {
        im->px[i] = TP_KEY;
    }
    return true;
}

static inline void ip(tp_img_t *im, int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)im->w && (unsigned)y < (unsigned)im->h) {
        im->px[y * im->w + x] = c;
    }
}

static inline uint16_t ig(const tp_img_t *im, int x, int y)
{
    if ((unsigned)x < (unsigned)im->w && (unsigned)y < (unsigned)im->h) {
        return im->px[y * im->w + x];
    }
    return TP_KEY;
}

/* Paints over an image from ASCII rows, '.' leaving what was there. */
static void img_paint(tp_img_t *im, int x, int y, const char *const *rows, int n)
{
    for (int ry = 0; ry < n; ry++) {
        for (int rx = 0; rows[ry][rx]; rx++) {
            uint16_t c = tp_pal(rows[ry][rx]);
            if (c != TP_KEY) {
                ip(im, x + rx, y + ry, c);
            }
        }
    }
}

static bool img_ascii(int id, const char *const *rows, int n, int ax, int ay)
{
    int w = (int)strlen(rows[0]);
    if (!img_new(id, w, n, ax, ay)) {
        return false;
    }
    img_paint(&s_img[id], 0, 0, rows, n);
    return true;
}

static bool img_mirror(int dst, int src)
{
    const tp_img_t *s = &s_img[src];
    if (!img_new(dst, s->w, s->h, s->w - 1 - s->ax, s->ay)) {
        return false;
    }
    tp_img_t *d = &s_img[dst];
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            d->px[y * s->w + x] = s->px[y * s->w + (s->w - 1 - x)];
        }
    }
    return true;
}

/* Every opaque pixel touching a transparent one (or the edge) becomes the
 * outline. Inside the shape, so the silhouette does not grow. */
static uint8_t s_mask[64 * 64];

static void img_outline(tp_img_t *im, uint16_t oc)
{
    const int w = im->w, h = im->h;
    if (w * h > (int)sizeof(s_mask)) {
        return;
    }
    for (int i = 0; i < w * h; i++) {
        s_mask[i] = im->px[i] != TP_KEY;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (!s_mask[i]) {
                continue;
            }
            bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1 ||
                        !s_mask[i - 1] || !s_mask[i + 1] ||
                        !s_mask[i - w] || !s_mask[i + w];
            if (edge) {
                im->px[i] = oc;
            }
        }
    }
}

static inline int band4(int I, int a, int b, int c,
                        uint16_t hi, uint16_t lt, uint16_t md, uint16_t dk,
                        uint16_t *out)
{
    *out = I >= a ? hi : I >= b ? lt : I >= c ? md : dk;
    return 0;
}

/* --------------------------------------------------------------------------
 * The mole's body
 *
 * A dome (half ellipse) on straight sides, lit from the upper left. The
 * normal of the dome is the ellipse's; below it the body is a cylinder, and
 * it darkens row by row as it goes into the hole. Muzzle, belly and blush go
 * on top, the outline round the silhouette, and the nose last.
 *
 * The whacked pose is the same code, wider and with a flatter dome.
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t ol, dk, md, lt, hi;
    uint32_t muz, muz_sh, belly, belly_sh, blush;
} fur_t;

static const fur_t FUR_BROWN = {
    0x2A140A, 0x6E3A1C, 0x9A5A2C, 0xBF7A40, 0xDE9D60,
    0xF4D6A8, 0xD9A878, 0xE8C08C, 0xC89868, 0xF08C7C,
};

static const fur_t FUR_GOLD = {
    0x4A2A00, 0xB86E00, 0xE8A010, 0xFFC832, 0xFFEE8A,
    0xFFF6CC, 0xF2D27A, 0xFFE9A0, 0xE6C35C, 0xFFA070,
};

static const char *const NOSE[] = {
    ".PPPPP.",
    "PhhpppP",
    "PpppppP",
    ".PPPPP.",
};

/* In half-pixels: whether (X2, Y2) falls in an ellipse of semi-axes rx2, ry2. */
static inline bool in_ell(int X2, int Y2, int rx2, int ry2)
{
    return X2 * X2 * ry2 * ry2 + Y2 * Y2 * rx2 * rx2 <= rx2 * rx2 * ry2 * ry2;
}

static bool render_body(int id, const fur_t *f, int w, int ry)
{
    const int h   = 44;
    const int pad = TP_BODY_PAD;
    const int ax  = w / 2;
    const int rx2 = w;              /* the dome spans the image's width      */
    const int ry2 = ry * 2;
    const int cy2 = 2 * pad + ry2;  /* the dome's centre row, half-pixels    */

    if (!img_new(id, w, h, ax, pad)) {
        return false;
    }
    tp_img_t *im = &s_img[id];

    const uint16_t dk = tp_rgb(f->dk), md = tp_rgb(f->md);
    const uint16_t lt = tp_rgb(f->lt), hi = tp_rgb(f->hi);

    for (int y = 0; y < h; y++) {
        int Y2 = 2 * y + 1 - cy2;
        for (int x = 0; x < w; x++) {
            int X2 = 2 * x + 1 - w;
            int ny = 0;
            if (Y2 < 0) {
                if (!in_ell(X2, Y2, rx2, ry2)) {
                    continue;
                }
                ny = Y2 * 256 / ry2;
            }
            int nx  = X2 * 256 / rx2;
            int nz2 = 65536 - nx * nx - ny * ny;
            int nz  = nz2 > 0 ? tp_isqrt(nz2) : 0;
            int I   = (-100 * nx - 125 * ny + 205 * nz) / 256;

            /* into the hole it gets darker: the lip shades it */
            int below = y - (pad + ry + 12);
            if (below > 0) {
                I -= below * 8;
            }
            uint16_t c;
            band4(I, 232, 165, 80, hi, lt, md, dk, &c);
            ip(im, x, y, c);
        }
    }

    /* Belly: a paler oval low on the body, half hidden in the hole. */
    {
        const int bx2 = w - 10, by2 = 22;
        const int bc2 = 2 * (pad + 33);
        for (int y = pad + 22; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int X2 = 2 * x + 1 - w, Y2 = 2 * y + 1 - bc2;
                if (in_ell(X2, Y2, bx2, by2)) {
                    /* only its right flank and its bottom in shadow: split
                     * down the middle it read as two patches */
                    bool sh = X2 > w / 2 || Y2 > 14;
                    ip(im, x, y, tp_rgb(sh ? f->belly_sh : f->belly));
                }
            }
        }
    }

    /* Muzzle: a pale oval around the nose and the mouth, its lower rim and
     * its edge in the darker tone so it reads as a volume and not a patch. */
    {
        const int mx2 = w == TP_BODY_W ? 15 : 17;
        const int my2 = 11;
        const int mc2 = 2 * (pad + 18) + 1;
        for (int y = pad + 10; y < pad + 28; y++) {
            for (int x = 0; x < w; x++) {
                int X2 = 2 * x + 1 - w, Y2 = 2 * y + 1 - mc2;
                if (!in_ell(X2, Y2, mx2, my2)) {
                    continue;
                }
                bool edge = !in_ell(X2 - 2, Y2, mx2, my2) ||
                            !in_ell(X2 + 2, Y2, mx2, my2) ||
                            !in_ell(X2, Y2 + 2, mx2, my2);
                bool low = Y2 > 4;
                ip(im, x, y, tp_rgb(edge || low ? f->muz_sh : f->muz));
            }
        }
    }

    /* Blush, blended with whatever fur tone is there. */
    {
        const uint16_t bl = tp_rgb(f->blush);
        for (int y = pad + 15; y <= pad + 16; y++) {
            for (int k = 8; k <= 10; k++) {
                ip(im, ax - k, y, tp_mix(ig(im, ax - k, y), bl, 9));
                ip(im, ax + k, y, tp_mix(ig(im, ax + k, y), bl, 9));
            }
        }
    }

    /* Three hairs sticking up. One pixel wide, so the outline pass turns
     * them into outline colour, which is exactly what a hair should be. */
    {
        static const int8_t hair[][2] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 },
            { -3, 1 }, { -2, 2 }, { 3, 1 }, { 2, 2 },
        };
        for (unsigned i = 0; i < sizeof(hair) / sizeof(hair[0]); i++) {
            ip(im, ax + hair[i][0], hair[i][1], md);
        }
    }

    img_outline(im, tp_rgb(f->ol));
    img_paint(im, ax - 3, pad + 14, NOSE, 4);
    return true;
}

/* --------------------------------------------------------------------------
 * Faces: eyes and brows in one image, mouths in another
 * -------------------------------------------------------------------------- */

#define EYES_AX     9
#define EYES_AY     (-5)
#define MOUTH_AX    5
#define MOUTH_AY    (-18)

static const char *const EYES[EX_COUNT][9] = {
    [EX_NORMAL] = {
        "...................",
        "..kk...........kk..",
        "....kk.......kk....",
        "....KK.......KK....",
        "...KwKK.....KwKK...",
        "...KKKK.....KKKK...",
        "...KKKK.....KKKK...",
        "....KK.......KK....",
        "...................",
    },
    [EX_LEFT] = {
        "...................",
        "..kk...........kk..",
        "....kk.......kk....",
        "...KK.......KK.....",
        "..KwKK.....KwKK....",
        "..KKKK.....KKKK....",
        "..KKKK.....KKKK....",
        "...KK.......KK.....",
        "...................",
    },
    [EX_RIGHT] = {
        "...................",
        "..kk...........kk..",
        "....kk.......kk....",
        ".....KK.......KK...",
        "....KwKK.....KwKK..",
        "....KKKK.....KKKK..",
        "....KKKK.....KKKK..",
        ".....KK.......KK...",
        "...................",
    },
    [EX_BLINK] = {
        "...................",
        "..kk...........kk..",
        "....kk.......kk....",
        "...................",
        "...................",
        "...KKKK.....KKKK...",
        "....KK.......KK....",
        "...................",
        "...................",
    },
    [EX_WIDE] = {
        "..kkkk.......kkkk..",
        "...................",
        "...kkkk.....kkkk...",
        "..kwwwwk...kwwwwk..",
        "..kwKKwk...kwKKwk..",
        "..kwKKwk...kwKKwk..",
        "..kwwwwk...kwwwwk..",
        "...kkkk.....kkkk...",
        "...................",
    },
    [EX_HAPPY] = {
        "...................",
        "..kkk.........kkk..",
        "...................",
        "...................",
        "....KK.......KK....",
        "...KKKK.....KKKK...",
        "...K..K.....K..K...",
        "...................",
        "...................",
    },
    [EX_DIZZY] = {
        "...................",
        "...................",
        "...................",
        "...K..K.....K..K...",
        "....KK.......KK....",
        "....KK.......KK....",
        "...K..K.....K..K...",
        "...................",
        "...................",
    },
    [EX_SMUG] = {
        "...................",
        "..kkkk.............",
        ".............kkkk..",
        "...................",
        "...kkkk.....kkkk...",
        "...KKKK.....KKKK...",
        "...KwKK.....KwKK...",
        "....KK.......KK....",
        "...................",
    },
    [EX_ANGRY] = {
        "...................",
        ".kk.............kk.",
        "...kk.........kk...",
        ".....kk.....kk.....",
        "...KKKK.....KKKK...",
        "...KwKK.....KwKK...",
        "...KKKK.....KKKK...",
        "....KK.......KK....",
        "...................",
    },
};

static const char *const MOUTHS[MO_COUNT][6] = {
    [MO_TEETH] = {
        ".k.......k.",
        "..kkkkkkk..",
        "..kwwkwwk..",
        "..kwWkwWk..",
        "...kk.kk...",
        "...........",
    },
    [MO_O] = {
        "...........",
        "....kkk....",
        "...krrrk...",
        "...krRrk...",
        "....kkk....",
        "...........",
    },
    [MO_OPEN] = {
        "..kkkkkkk..",
        ".krrrrrrrk.",
        ".krRRRRRrk.",
        ".krRmmmRrk.",
        "..kmmmmmk..",
        "...kkkkk...",
    },
    [MO_TONGUE] = {
        ".k.......k.",
        "..kkkkkkk..",
        "...kmmmk...",
        "...kmMmk...",
        "...kmmmk...",
        "....kkk....",
    },
    [MO_SMIRK] = {
        ".........k.",
        "..kkkkkkk..",
        "......kwk..",
        "......kWk..",
        ".......k...",
        "...........",
    },
    [MO_GRIT] = {
        "...........",
        "..kkkkkkk..",
        ".kwkwkwkwk.",
        ".kWkWkWkWk.",
        "..kkkkkkk..",
        "...........",
    },
};

/* --------------------------------------------------------------------------
 * Small hand-drawn sprites
 * -------------------------------------------------------------------------- */

/* A paw gripping the rim: three claws hanging over the dirt. The anchor is
 * the row where it grips. */
static const char *const PAW[] = {
    "..kkkkk..",
    ".kbnnnbk.",
    "kbnnnnbBk",
    "kbbbbbbBk",
    ".kWkWkWk.",
    "..g.g.g..",
};

static const char *const STAR[] = {
    "...y...",
    "...y...",
    "yyyYyyy",
    ".yyYyy.",
    "..yyy..",
    ".yy.yy.",
    ".y...y.",
};

static const char *const STAR_SMALL[] = {
    "..y..",
    "yyYyy",
    ".yyy.",
    ".y.y.",
};

static const char *const TWINKLE[TP_TWINKLE_FRAMES][5] = {
    { "..w..", "..y..", "wyYyw", "..y..", "..w.." },
    { ".....", "..w..", ".wYw.", "..w..", "....." },
};

static const char *const SWEAT[] = {
    ".U..",
    ".uU.",
    "uuuU",
    "uwuU",
    "uuuU",
    ".UU.",
};

static const char *const HEART[] = {
    ".kkk.kkk.",
    "kvvekeeek",
    "kveeeeeek",
    "keeeeeeEk",
    ".keeeeEk.",
    "..keeEk..",
    "...kEk...",
    "....k....",
};

static const char *const HEART_EMPTY[] = {
    ".kkk.kkk.",
    "kggGkGGGk",
    "kgGGGGGGk",
    "kGGGGGGGk",
    ".kGGGGGk.",
    "..kGGGk..",
    "...kGk...",
    "....k....",
};

static const char *const CLOCK[] = {
    "..kkkkk..",
    ".kwwwwwk.",
    "kwwwkwwwk",
    "kwwwkwwwk",
    "kwwwkkkwk",
    "kwwwwwwwk",
    "kWwwwwwWk",
    ".kWWWWWk.",
    "..kkkkk..",
};

/* The fuse's spark: three frames that alternate straight and diagonal rays,
 * so it flickers without anything moving. */
static const char *const SPARK[TP_SPARK_FRAMES][9] = {
    {
        "....o....",
        "....y....",
        "...yyy...",
        "..ywwwy..",
        "oyywwwyyo",
        "..ywwwy..",
        "...yyy...",
        "....y....",
        "....o....",
    },
    {
        "o.......o",
        ".y.....y.",
        "..y.y.y..",
        "...ywy...",
        "..ywwwy..",
        "...ywy...",
        "..y.y.y..",
        ".y.....y.",
        "o.......o",
    },
    {
        ".........",
        "....o....",
        "..o.y.o..",
        "...ywy...",
        ".oywwwyo.",
        "...ywy...",
        "..o.y.o..",
        "....o....",
        ".........",
    },
};

/* --------------------------------------------------------------------------
 * The hard hat
 *
 * Rendered at eight angles for when it flies off. The shape is evaluated in
 * the hat's own frame -each output pixel is rotated back- so the eight come
 * out of one description: a dome, a flat brim in front of it and a raised
 * ridge over the top. Frame 0 is the one worn.
 *
 * Its origin is 4 px above the brim, near the middle of the silhouette, so it
 * spins about its centre and not about a corner.
 * -------------------------------------------------------------------------- */

#define HAT_S       40

static bool render_helmet(int id, int brad)
{
    if (!img_new(id, HAT_S, HAT_S, HAT_S / 2, HAT_S / 2)) {
        return false;
    }
    tp_img_t *im = &s_img[id];

    const uint16_t hi = tp_rgb(0xFFF7B8), lt = tp_rgb(0xFFDA45);
    const uint16_t md = tp_rgb(0xF2AE1C), dk = tp_rgb(0xB8700A);
    const int cs = tp_cos(brad), sn = tp_sin(brad);

    for (int y = 0; y < HAT_S; y++) {
        for (int x = 0; x < HAT_S; x++) {
            int dx = (2 * x + 1 - HAT_S) * 128;         /* 1/256 px */
            int dy = (2 * y + 1 - HAT_S) * 128;
            int lx = (dx * cs + dy * sn) / 256;
            int ly = (-dx * sn + dy * cs) / 256;

            int bu = lx * 256 / 4480;                   /* brim: rx 17.5 */
            int bv = (ly - 5 * 256) * 256 / 666;        /*       ry 2.6  */
            bool brim = bu * bu + bv * bv <= 65536;

            int du = lx * 256 / 3584;                   /* dome: rx 14   */
            int dv = (ly - 4 * 256) * 256 / 3072;       /*       ry 12   */
            bool dome = ly <= 4 * 256 + 128 && du * du + dv * dv <= 65536;

            if (!brim && !dome) {
                continue;
            }
            uint16_t c;
            if (dome && !(brim && ly > 3 * 256)) {
                int nz2 = 65536 - du * du - dv * dv;
                int nz  = nz2 > 0 ? tp_isqrt(nz2) : 0;
                int I   = (-110 * du - 135 * dv + 200 * nz) / 256;
                bool ridge = lx > -3 * 256 && lx < 2 * 256;
                if (ridge) {
                    bool rim = lx < -2 * 256 || lx > 1 * 256;
                    I = rim ? I - 40 : I + 45;
                }
                band4(I, 225, 150, 60, hi, lt, md, dk, &c);
            } else {
                c = bv < -40 ? lt : (bv < 110 ? md : dk);
                if (lx > 12 * 256 && c != dk) {
                    c = md;
                }
            }
            ip(im, x, y, c);
        }
    }
    img_outline(im, tp_rgb(0x3A2206));
    return true;
}

/* --------------------------------------------------------------------------
 * The bomb
 *
 * A lit sphere with a specular glint and a bounce light on its lower right,
 * and a metal cap. The fuse is NOT here: it burns down, so tp_draw.c draws it
 * at its current length.
 *
 *   variant 0  normal
 *   variant 1  hot: the last stretch of the fuse, tinted red (it blinks)
 *   variant 2  dud: the fuse went out, sooty and dull
 * -------------------------------------------------------------------------- */

#define BOMB_W      27
#define BOMB_H      30
#define BOMB_AX     13
#define BOMB_AY     17

static bool render_bomb(int id, int variant)
{
    if (!img_new(id, BOMB_W, BOMB_H, BOMB_AX, BOMB_AY)) {
        return false;
    }
    tp_img_t *im = &s_img[id];

    uint16_t hi = tp_rgb(0x8E98B8), lt = tp_rgb(0x525A78);
    uint16_t md = tp_rgb(0x2F3446), dk = tp_rgb(0x191C26);
    uint16_t rim = tp_rgb(0x3E4862);

    if (variant == 1) {
        /* Glowing, not wine-coloured: the first version mixed half-way and
         * came out maroon. The highlight goes orange. */
        const uint16_t red = tp_rgb(0xFF2A18);
        hi = tp_mix(hi, tp_rgb(0xFF9A60), 11);
        lt = tp_mix(lt, red, 12);
        md = tp_mix(md, red, 11);
        dk = tp_mix(dk, red, 8);
        rim = tp_mix(rim, tp_rgb(0xFF6A30), 12);
    } else if (variant == 2) {
        hi = tp_rgb(0x5E6270);
        lt = tp_rgb(0x3C404C);
        md = tp_rgb(0x262932);
        dk = tp_rgb(0x15171C);
        rim = md;
    }

    for (int y = 0; y < BOMB_H; y++) {
        for (int x = 0; x < BOMB_W; x++) {
            int X2 = 2 * x + 1 - BOMB_W;
            int Y2 = 2 * y + 1 - 2 * BOMB_AY - 1;
            int d2 = X2 * X2 + Y2 * Y2;
            if (d2 > 23 * 23) {
                continue;
            }
            int nx  = X2 * 256 / 23;
            int ny  = Y2 * 256 / 23;
            int nz2 = 65536 - nx * nx - ny * ny;
            int nz  = nz2 > 0 ? tp_isqrt(nz2) : 0;
            int I   = (-120 * nx - 135 * ny + 190 * nz) / 256;
            uint16_t c;
            band4(I, 215, 140, 55, hi, lt, md, dk, &c);
            if (d2 > 17 * 17 && nx > 60 && ny > 60 && (c == dk || c == md)) {
                c = rim;
            }
            ip(im, x, y, c);
        }
    }

    /* the glint, three pixels, and one softer beside it */
    if (variant != 2) {
        const uint16_t w = tp_rgb(0xF2F5FF);
        ip(im, BOMB_AX - 5, BOMB_AY - 6, w);
        ip(im, BOMB_AX - 4, BOMB_AY - 6, w);
        ip(im, BOMB_AX - 5, BOMB_AY - 5, w);
        ip(im, BOMB_AX - 2, BOMB_AY - 7, hi);
    }

    /* the cap: a short metal cylinder, lit from the left */
    {
        const uint16_t m1 = tp_rgb(0xD8DDE6), m2 = tp_rgb(0x9AA1AE);
        const uint16_t m3 = tp_rgb(0x5E6572);
        for (int y = 3; y <= 6; y++) {
            for (int x = BOMB_AX - 3; x <= BOMB_AX + 3; x++) {
                int k = x - (BOMB_AX - 3);
                uint16_t c = k < 2 ? m1 : (k < 5 ? m2 : m3);
                if (y == 3) {
                    c = tp_mix(c, 0xFFFF, 4);
                }
                ip(im, x, y, c);
            }
        }
    }

    img_outline(im, tp_rgb(0x08080C));
    return true;
}

/* --------------------------------------------------------------------------
 * The mallet
 *
 * A toy mallet: red head with two yellow bands, wooden handle. Seen from the
 * side, the head is a cylinder standing across the handle, so when the handle
 * lies flat the head stands upright and strikes with its bottom face. That
 * face's centre is the image's anchor in all three frames: the tap point.
 *
 * The raised frames rotate the whole mallet about the hand, which is at the
 * end of the handle, 28 px to the right of the head's centre. The shape is
 * evaluated in the mallet's own frame, as with the hat.
 * -------------------------------------------------------------------------- */

#define MAL_W       44
#define MAL_H       50
#define MAL_AX      8
#define MAL_AY      46

static bool render_mallet(int id, int brad)
{
    if (!img_new(id, MAL_W, MAL_H, MAL_AX, MAL_AY)) {
        return false;
    }
    tp_img_t *im = &s_img[id];

    const uint16_t r_hi = tp_rgb(0xFFB09A), r_lt = tp_rgb(0xFF6A50);
    const uint16_t r_md = tp_rgb(0xE8352A), r_dk = tp_rgb(0xA41E18);
    const uint16_t y_hi = tp_rgb(0xFFF4B0), y_lt = tp_rgb(0xFFDA45);
    const uint16_t y_md = tp_rgb(0xF2AE1C), y_dk = tp_rgb(0xB8700A);
    const uint16_t w_lt = tp_rgb(0xE8B274), w_md = tp_rgb(0xB47A40);
    const uint16_t w_dk = tp_rgb(0x7A4E22);
    const int cs = tp_cos(brad), sn = tp_sin(brad);

    for (int y = 0; y < MAL_H; y++) {
        for (int x = 0; x < MAL_W; x++) {
            int wx = (2 * (x - MAL_AX) + 1) * 128;      /* from the target */
            int wy = (2 * (y - MAL_AY) + 1) * 128;
            int qx = wx - 28 * 256, qy = wy + 9 * 256;   /* from the hand   */
            int lx = 28 * 256 + (qx * cs + qy * sn) / 256;
            int ly = (-qx * sn + qy * cs) / 256;
            int alx = lx < 0 ? -lx : lx;
            int aly = ly < 0 ? -ly : ly;

            uint16_t c;
            bool head = alx <= 6 * 256 && aly <= 9 * 256 &&
                        !(alx > 5 * 256 && aly > 8 * 256);
            if (head) {
                bool bandy = aly >= 5 * 256 && aly <= 7 * 256;
                int k = lx < -3 * 256 && lx > -5 * 256 ? 0 :
                        lx < -1 * 256 ? 1 : lx < 3 * 256 ? 2 : 3;
                if (bandy) {
                    c = k == 0 ? y_hi : k == 1 ? y_lt : k == 2 ? y_md : y_dk;
                } else {
                    c = k == 0 ? r_hi : k == 1 ? r_lt : k == 2 ? r_md : r_dk;
                }
            } else if (lx >= 5 * 256 && lx <= 31 * 256 && aly <= 2 * 256 - 64) {
                c = ly < -64 ? w_lt : (ly < 256 + 64 ? w_md : w_dk);
            } else if (lx > 30 * 256 && lx <= 33 * 256 && aly <= 5 * 128) {
                c = ly < 0 ? w_md : w_dk;
            } else {
                continue;
            }
            ip(im, x, y, c);
        }
    }
    img_outline(im, tp_rgb(0x2A0E08));
    return true;
}

/* --------------------------------------------------------------------------
 * Init and drawing
 * -------------------------------------------------------------------------- */

bool tp_art_init(void)
{
    tp_art_free();

    bool ok = true;
    ok = ok && render_body(IMG_BODY,         &FUR_BROWN, TP_BODY_W, 13);
    ok = ok && render_body(IMG_BODY_SQ,      &FUR_BROWN, TP_BODY_W + 4, 9);
    ok = ok && render_body(IMG_BODY_GOLD,    &FUR_GOLD,  TP_BODY_W, 13);
    ok = ok && render_body(IMG_BODY_GOLD_SQ, &FUR_GOLD,  TP_BODY_W + 4, 9);

    for (int i = 0; ok && i < EX_COUNT; i++) {
        ok = img_ascii(IMG_EYES0 + i, EYES[i], 9, EYES_AX, EYES_AY);
    }
    for (int i = 0; ok && i < MO_COUNT; i++) {
        ok = img_ascii(IMG_MOUTH0 + i, MOUTHS[i], 6, MOUTH_AX, MOUTH_AY);
    }

    ok = ok && img_ascii(IMG_PAW_L, PAW, 6, 4, 2);
    ok = ok && img_mirror(IMG_PAW_R, IMG_PAW_L);

    for (int i = 0; ok && i < TP_HELMET_FRAMES; i++) {
        ok = render_helmet(IMG_HELMET0 + i, i * 256 / TP_HELMET_FRAMES);
    }

    ok = ok && render_bomb(IMG_BOMB, 0);
    ok = ok && render_bomb(IMG_BOMB_HOT, 1);
    ok = ok && render_bomb(IMG_BOMB_DUD, 2);
    for (int i = 0; ok && i < TP_SPARK_FRAMES; i++) {
        ok = img_ascii(IMG_SPARK0 + i, SPARK[i], 9, 4, 4);
    }

    /* raised, halfway, on target */
    static const uint8_t mallet_brad[TP_MALLET_FRAMES] = { 44, 20, 0 };
    for (int i = 0; ok && i < TP_MALLET_FRAMES; i++) {
        ok = render_mallet(IMG_MALLET0 + i, mallet_brad[i]);
    }

    ok = ok && img_ascii(IMG_STAR, STAR, 7, 3, 3);
    ok = ok && img_ascii(IMG_STAR_SMALL, STAR_SMALL, 4, 2, 2);
    for (int i = 0; ok && i < TP_TWINKLE_FRAMES; i++) {
        ok = img_ascii(IMG_TWINKLE0 + i, TWINKLE[i], 5, 2, 2);
    }
    ok = ok && img_ascii(IMG_SWEAT, SWEAT, 6, 0, 0);
    ok = ok && img_ascii(IMG_HEART, HEART, 8, 0, 0);
    ok = ok && img_ascii(IMG_HEART_EMPTY, HEART_EMPTY, 8, 0, 0);
    ok = ok && img_ascii(IMG_CLOCK, CLOCK, 9, 0, 0);

    if (!ok) {
        tp_art_free();
    }
    return ok;
}

void tp_art_free(void)
{
    for (int i = 0; i < TP_IMG_COUNT; i++) {
        free(s_img[i].px);
        s_img[i].px = NULL;
    }
}

void tp_img_rect(int id, int x, int y, int scale, tp_rect_t *out)
{
    const tp_img_t *im = &s_img[id];
    out->x0 = (int16_t)(x - im->ax * scale);
    out->y0 = (int16_t)(y - im->ay * scale);
    out->x1 = (int16_t)(out->x0 + im->w * scale);
    out->y1 = (int16_t)(out->y0 + im->h * scale);
}

void tp_img_draw(tp_buf_t *b, int id, int x, int y, int scale)
{
    const tp_img_t *im = &s_img[id];
    if (!im->px) {
        return;
    }
    const int x0 = x - im->ax * scale;
    const int y0 = y - im->ay * scale;

    /* only the rows that fall inside the clip: most redraws are a slice */
    int iy0 = 0, iy1 = im->h;
    if (y0 < b->cy0) iy0 = (b->cy0 - y0) / scale;
    if (y0 + im->h * scale > b->cy1) iy1 = (b->cy1 - y0 + scale - 1) / scale;

    for (int iy = iy0; iy < iy1; iy++) {
        const uint16_t *row = im->px + iy * im->w;
        for (int ix = 0; ix < im->w; ix++) {
            uint16_t c = row[ix];
            if (c == TP_KEY) {
                continue;
            }
            if (scale == 1) {
                tp_px(b, x0 + ix, y0 + iy, c);
            } else {
                tp_rect(b, x0 + ix * scale, y0 + iy * scale, scale, scale, c);
            }
        }
    }
}
