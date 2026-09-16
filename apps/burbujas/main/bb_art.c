/*
 * BURBUJAS - the bubbles (see bb_art.h)
 *
 * One sphere, worked out once: for each of the 22x22 pixels, which of five
 * tones it is. The light comes from the upper left, the tones are quantised
 * -Topos's rule: four tones and an outline read as pixel art, a smooth
 * gradient reads as a blurry circle- and a colour is just five values derived
 * from one.
 *
 * Everything else in the file is an animation drawn from that: a burst, a
 * bomb going off, and the rainbow bubble, which is the same sphere with the
 * palette chosen per diagonal band.
 */
#include "bb_art.h"
#include "burbujas.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * The tone map
 * -------------------------------------------------------------------------- */

enum { T_NONE = 0, T_OUTLINE, T_SHADOW, T_BASE, T_LIGHT, T_SPEC, T_TONES };

static uint8_t  s_map[BB_D * BB_D];
static uint16_t s_pal[BB_PALS][T_TONES];
static bool     s_ready;

/* Base colours. The six of the board are the ones of the photo this was drawn
 * from: saturated, and far enough apart that colour-blind eyes still get
 * light-dark differences between neighbours. */
static const uint32_t BASE[BB_PALS] = {
    [BC_NONE]    = 0x000000,
    [BC_RED]     = 0xE8413C,
    [BC_ORANGE]  = 0xFF8A1E,
    [BC_YELLOW]  = 0xFFD62E,
    [BC_GREEN]   = 0x43C74C,
    [BC_BLUE]    = 0x3C8CFF,
    [BC_PURPLE]  = 0xA95CF0,
    [BC_RAINBOW] = 0xE8F0FF,        /* only for dots and popups              */
    [BC_BOMB]    = 0x3C4250,
    [BC_GREY]    = 0x74767E,
};

static void palette_of(uint32_t base, uint16_t *out)
{
    uint16_t c = bb_rgb(base);
    out[T_NONE]    = 0;
    out[T_OUTLINE] = bb_tone(c, -11);
    out[T_SHADOW]  = bb_tone(c, -5);
    out[T_BASE]    = c;
    out[T_LIGHT]   = bb_tone(c, 5);
    out[T_SPEC]    = bb_tone(c, 12);
}

/* Half-pixel coordinates: a 22 px circle has no middle pixel, so the centre
 * falls between two of them and every radius here is in halves. */
bool bb_art_init(void)
{
    if (s_ready) {
        return true;
    }
    const int R2 = BB_D;                /* 11 px, in halves                   */

    for (int y = 0; y < BB_D; y++) {
        for (int x = 0; x < BB_D; x++) {
            int px = 2 * x - (BB_D - 1);
            int py = 2 * y - (BB_D - 1);
            int d2 = px * px + py * py;
            uint8_t t = T_NONE;

            if (d2 <= R2 * R2 + R2) {
                if (d2 > (R2 - 2) * (R2 - 2) + (R2 - 2)) {
                    t = T_OUTLINE;
                } else {
                    /* Lambert against (-5, -6, 4), normalised to -100..100.
                     * The brightest point lands near x=4, y=3: the glint of
                     * the photo, up and to the left, inside the outline. */
                    int nz  = bb_isqrt(R2 * R2 - d2);
                    int dot = -5 * px - 6 * py + 4 * nz;
                    int lit = dot * 100 / 193;

                    t = lit >= 88 ? T_SPEC
                      : lit >= 62 ? T_LIGHT
                      : lit >= -25 ? T_BASE
                                   : T_SHADOW;
                }
            }
            s_map[y * BB_D + x] = t;
        }
    }

    for (int i = 0; i < BB_PALS; i++) {
        palette_of(BASE[i], s_pal[i]);
    }
    s_ready = true;
    return true;
}

void bb_art_free(void)
{
    s_ready = false;
}

uint16_t bb_col_base(int color)
{
    return s_pal[(unsigned)color < BB_PALS ? color : BC_GREY][T_BASE];
}

uint16_t bb_col_light(int color)
{
    return s_pal[(unsigned)color < BB_PALS ? color : BC_GREY][T_LIGHT];
}

uint16_t bb_col_dark(int color)
{
    return s_pal[(unsigned)color < BB_PALS ? color : BC_GREY][T_OUTLINE];
}

/* --------------------------------------------------------------------------
 * A bubble
 * -------------------------------------------------------------------------- */

/* A burst starts with the bubble a tone brighter all over, which is what
 * makes it read as "it is going" and not as a bubble that vanished. */
static inline uint8_t brighten(uint8_t t)
{
    return t == T_NONE ? T_NONE : (uint8_t)(t < T_SPEC ? t + 1 : T_SPEC);
}

/* The rainbow one: six diagonal bands, each with the tone the sphere asks
 * for. Bands and not sectors because a band survives being 22 px wide. */
static inline int band_color(int x, int y)
{
    int b = (x + y) * BB_NCOLORS / (BB_D * 2 - 2);
    return BC_RED + (b < 0 ? 0 : b > BB_NCOLORS - 1 ? BB_NCOLORS - 1 : b);
}

static void bomb_fuse(bb_buf_t *b, int cx, int cy, int phase)
{
    const uint16_t cord = bb_rgb(0x8A6A3A), lit = bb_rgb(0xFFD62E);
    const uint16_t hot = bb_rgb(0xFF8A1E), white = bb_rgb(0xFFF4D0);

    bb_px(b, cx + 1, cy - 11, cord);
    bb_px(b, cx + 2, cy - 12, cord);
    bb_px(b, cx + 2, cy - 13, cord);
    bb_px(b, cx + 3, cy - 14, cord);
    bb_px(b, cx + 4, cy - 15, cord);

    /* the spark: a cross that breathes, so a bomb is never still */
    int r = 1 + (phase & 1);
    bb_px(b, cx + 4, cy - 17, phase & 1 ? white : lit);
    bb_rect(b, cx + 4 - r, cy - 17, r * 2 + 1, 1, hot);
    bb_rect(b, cx + 4, cy - 17 - r, 1, r * 2 + 1, hot);
    bb_px(b, cx + 4, cy - 17, white);
}

void bb_bubble(bb_buf_t *b, int cx, int cy, int color, int style, int phase)
{
    const int x0 = cx - BB_R, y0 = cy - BB_R;

    if (style == BS_GHOST) {
        /* Only the outline, every other pixel, blended: it has to read as
         * "here" without competing with the bubbles that are really there. */
        const uint16_t c = bb_col_light(color);
        for (int y = 0; y < BB_D; y++) {
            for (int x = 0; x < BB_D; x++) {
                if (s_map[y * BB_D + x] == T_OUTLINE && ((x + y) & 1) == 0) {
                    bb_px_mix(b, x0 + x, y0 + y, c, 11);
                }
            }
        }
        return;
    }

    const bool rainbow = color == BC_RAINBOW;
    const uint16_t *pal = s_pal[(unsigned)color < BB_PALS ? color : BC_GREY];

    for (int y = 0; y < BB_D; y++) {
        for (int x = 0; x < BB_D; x++) {
            uint8_t t = s_map[y * BB_D + x];
            if (t == T_NONE) {
                continue;
            }
            if (style == BS_FLASH) {
                t = brighten(t);
            }
            bb_px(b, x0 + x, y0 + y,
                  rainbow ? s_pal[band_color(x, y)][t] : pal[t]);
        }
    }

    if (color == BC_BOMB) {
        bomb_fuse(b, cx, cy, phase);
    }
}

void bb_bubble_box(int color, int cx, int cy, bb_rect_t *out)
{
    out->x0 = (int16_t)(cx - BB_R);
    out->y0 = (int16_t)(cy - (color == BC_BOMB ? 19 : BB_R));
    out->x1 = (int16_t)(cx + BB_R);
    out->y1 = (int16_t)(cy + BB_R);
}

/* --------------------------------------------------------------------------
 * A burst
 *
 * Two frames of flash and then a ring opening up with six pieces flying out
 * of it. The reach is BB_POP_REACH and the box is built from that constant,
 * so nothing here may go further: the harness paints every slot alone on a
 * sentinel canvas and fails if a single pixel lands outside its box.
 * -------------------------------------------------------------------------- */

void bb_pop_draw(bb_buf_t *b, int cx, int cy, int color, int frame, int seed)
{
    if (frame < 2) {
        bb_bubble(b, cx, cy, color, BS_FLASH, 0);
        return;
    }

    const int f = frame - 2;                    /* 0..5                       */
    const uint16_t light = bb_col_light(color);
    const uint16_t spec  = bb_tone(light, 6);
    const uint16_t base  = bb_col_base(color);

    /* The ring: 11, 13 ... 21 px, two pixels thick, and it STARTS SOLID. The
     * first version faded from 13/16 and on the board that reads as a bubble
     * that just went missing: what sells a burst is the first frame being as
     * bright as the bubble was. */
    int rr = 11 + f * 2;
    int m  = 16 - f * 3;
    if (m > 0) {
        bb_wave(b, cx, cy, rr, 2, f < 2 ? spec : light, m);
    }

    /* six pieces, from a rotation that depends on the bubble: two bursts side
     * by side must not look like the same drawing twice */
    int size = f < 4 ? 2 : 1;
    int dist = 9 + f * 2;
    for (int i = 0; i < 6; i++) {
        int a  = (i * 256) / 6 + seed * 7;
        int px = cx + bb_cos(a) * dist / 256;
        int py = cy + bb_sin(a) * dist / 256;
        int fm = 16 - f * 2;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                bb_px_mix(b, px + x, py + y, f < 2 ? light : base, fm);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * The bomb going off
 * -------------------------------------------------------------------------- */

void bb_blast_draw(bb_buf_t *b, int cx, int cy, int frame)
{
    const int f = frame < 0 ? 0 : frame;
    const uint16_t hot = bb_rgb(0xFFD62E), fire = bb_rgb(0xFF6A1E);
    const uint16_t smoke = bb_rgb(0x5A5560);

    /* the fireball, fading as it grows */
    int r = 10 + f * 3;
    int m = 15 - f;
    if (m > 0) {
        bb_glow(b, cx, cy, r, f < 3 ? hot : fire, m);
    }

    /* the shock ring */
    int rw = 8 + f * 4;
    if (rw <= BB_BLAST_REACH - 2) {
        bb_wave(b, cx, cy, rw, 2, f < 5 ? hot : smoke, 12 - f);
    }

    /* sparks */
    int dist = 6 + f * 4;
    for (int i = 0; i < 8; i++) {
        int a = (i * 256) / 8 + 11;
        bb_px_mix(b, cx + bb_cos(a) * dist / 256,
                  cy + bb_sin(a) * dist / 256, hot, 14 - f);
    }
}
