/*
 * Claudito - the two scenes (see cl_scene.h)
 */
#include "cl_scene.h"
#include "aos_i18n.h"
#include "cl_sprites.h"

/* Reproducible noise: the tufts of grass and the wood grain have to fall in
 * the same place every time, or the background shivers when redrawn. */
static uint32_t s_rng;

static void noise_seed(uint32_t seed)
{
    s_rng = seed | 1u;
}

static uint32_t noise(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static int noise_range(int lo, int hi)
{
    return lo + (int)(noise() % (uint32_t)(hi - lo + 1));
}

/* --------------------------------------------------------------------------
 * Living room
 * -------------------------------------------------------------------------- */

static void draw_window(cl_buf_t *b, int x, int y, int w, int h)
{
    cl_rect(b, x, y, w, h, cl_rgb(0x8FD4F7));               /* sky indoors */
    /* soft gradient of the window's sky */
    cl_rect(b, x, y, w, h / 3, cl_rgb(0x7CC6F2));
    cl_disc(b, x + w - 5, y + 4, 3, cl_rgb(0xFFE066));      /* small sun    */

    cl_rect(b, x + 3, y + h - 7, 6, 3, cl_rgb(0xFFFFFF));   /* fixed little cloud */
    cl_rect(b, x + 4, y + h - 8, 4, 1, cl_rgb(0xFFFFFF));

    /* frame and glazing bars */
    cl_frame(b, x - 2, y - 2, w + 4, h + 4, cl_rgb(0xF4EDE0));
    cl_frame(b, x - 1, y - 1, w + 2, h + 2, cl_rgb(0xF4EDE0));
    cl_frame(b, x - 3, y - 3, w + 6, h + 6, cl_rgb(0xC9A87E));
    cl_vline(b, x + w / 2, y, h, cl_rgb(0xF4EDE0));
    cl_hline(b, x, y + h / 2, w, cl_rgb(0xF4EDE0));

    /* sill */
    cl_rect(b, x - 5, y + h + 3, w + 10, 2, cl_rgb(0xE0C49B));
    cl_hline(b, x - 5, y + h + 5, w + 10, cl_rgb(0xB98A5C));
}

static void draw_picture(cl_buf_t *b, int x, int y)
{
    cl_rect(b, x, y, 20, 16, cl_rgb(0xD9A441));
    cl_rect(b, x + 2, y + 2, 16, 12, cl_rgb(0xFBF3E2));
    /* little hills and sun: a picture within the picture */
    cl_rect(b, x + 2, y + 9, 16, 5, cl_rgb(0x9BD3A0));
    for (int i = 0; i < 5; i++) {
        cl_hline(b, x + 5 - i, y + 9 - i, 1 + i * 2, cl_rgb(0x7FBF8C));
        cl_hline(b, x + 12 - i, y + 10 - i, 1 + i * 2, cl_rgb(0x6FAE7C));
    }
    cl_disc(b, x + 14, y + 5, 2, cl_rgb(0xFFD166));
}

static void draw_rug(cl_buf_t *b, int cx, int cy)
{
    /* a flattened oval done by hand: five rows of decreasing widths */
    static const int8_t half[] = { 22, 26, 28, 29, 28, 26, 22, 16 };
    for (int i = 0; i < (int)(sizeof(half) / sizeof(half[0])); i++) {
        int hw = half[i];
        uint16_t c = (i % 2) ? cl_rgb(0x3E8F92) : cl_rgb(0x4EA8A0);
        cl_hline(b, cx - hw, cy + i, hw * 2, c);
    }
    /* cream border top and bottom */
    cl_hline(b, cx - 22, cy, 44, cl_rgb(0xEFE0C4));
    cl_hline(b, cx - 16, cy + 7, 32, cl_rgb(0xEFE0C4));
    for (int i = -20; i <= 20; i += 8) {
        cl_rect(b, cx + i, cy + 3, 3, 2, cl_rgb(0xEFE0C4));
    }
}

static void scene_home(cl_buf_t *b)
{
    const uint16_t wall  = cl_rgb(0xF0D8B8);
    const uint16_t strip = cl_rgb(0xE3C69C);

    cl_rect(b, 0, 0, CL_ART_W, 54, wall);
    for (int x = 3; x < CL_ART_W; x += 9) {
        cl_vline(b, x, 0, 54, strip);
        cl_vline(b, x + 1, 0, 54, strip);
    }
    /* the wallpaper's dots */
    for (int y = 4; y < 46; y += 9) {
        for (int x = 7; x < CL_ART_W; x += 9) {
            cl_px(b, x, y, cl_rgb(0xDCBB8C));
            cl_px(b, x + 1, y + 1, cl_rgb(0xDCBB8C));
        }
    }

    draw_window(b, 8, 8, 24, 22);
    draw_picture(b, 60, 10);

    /* skirting board */
    cl_rect(b, 0, 48, CL_ART_W, 6, cl_rgb(0xE8DCC8));
    cl_hline(b, 0, 48, CL_ART_W, cl_rgb(0xC9B79A));
    cl_hline(b, 0, 53, CL_ART_W, cl_rgb(0xA8916F));

    /* parquet: 4-row planks with staggered joints */
    const uint16_t wood[3] = { cl_rgb(0xC08A53), cl_rgb(0xB77F49), cl_rgb(0xC7935C) };
    noise_seed(0xC0FFEE);
    for (int band = 0; band * 4 + 54 < CL_ART_H; band++) {
        int y = 54 + band * 4;
        cl_rect(b, 0, y, CL_ART_W, 4, wood[band % 3]);
        cl_hline(b, 0, y, CL_ART_W, cl_rgb(0x9C6A3A));
        for (int x = (band % 2) ? 0 : 11; x < CL_ART_W; x += 23) {
            cl_vline(b, x, y, 4, cl_rgb(0x9C6A3A));
        }
        /* grain */
        for (int i = 0; i < 6; i++) {
            int vx = noise_range(0, CL_ART_W - 5);
            cl_hline(b, vx, y + 1 + (int)(noise() % 2), 3 + (int)(noise() % 3),
                     cl_rgb(0xA97747));
        }
    }

    draw_rug(b, 46, 63);

    cl_blit(b, 2, 38, CL_SPRITE(cl_spr_plant), false);
    cl_blit(b, 78, 41, CL_SPRITE(cl_spr_lamp), false);
    /* the lamp's halo on the wall */
    cl_shade(b, 74, 36, 19, 8, 3);

    cl_blit(b, 70, 68, CL_SPRITE(cl_spr_bowl), false);
    cl_blit(b, 6, 70, CL_SPRITE(cl_spr_bone), false);
}

/* --------------------------------------------------------------------------
 * Yard
 * -------------------------------------------------------------------------- */

static void draw_tree(cl_buf_t *b, int x, int base_y)
{
    const uint16_t trunk = cl_rgb(0x8A5A32);
    const uint16_t bark  = cl_rgb(0x6B4222);
    const uint16_t leaf  = cl_rgb(0x3FA353);
    const uint16_t leaf2 = cl_rgb(0x59C06B);
    const uint16_t leaf3 = cl_rgb(0x2C7C40);

    cl_rect(b, x - 3, base_y - 22, 7, 22, trunk);
    cl_vline(b, x - 3, base_y - 22, 22, bark);
    cl_vline(b, x + 2, base_y - 18, 18, bark);
    cl_rect(b, x - 5, base_y - 2, 11, 2, bark);

    /* canopy: overlapping discs, the dark tone first and then the light one
     * shifted upwards, which is how volume reads in pixel art */
    cl_disc(b, x,      base_y - 34, 11, leaf3);
    cl_disc(b, x - 9,  base_y - 28,  8, leaf3);
    cl_disc(b, x + 9,  base_y - 29,  8, leaf3);
    cl_disc(b, x,      base_y - 36,  9, leaf);
    cl_disc(b, x - 8,  base_y - 30,  6, leaf);
    cl_disc(b, x + 8,  base_y - 31,  6, leaf);
    cl_disc(b, x - 2,  base_y - 39,  5, leaf2);
    cl_disc(b, x + 6,  base_y - 35,  3, leaf2);

    /* little apples */
    cl_disc(b, x - 6, base_y - 31, 1, cl_rgb(0xE5484D));
    cl_disc(b, x + 5, base_y - 27, 1, cl_rgb(0xE5484D));
    cl_disc(b, x + 1, base_y - 25, 1, cl_rgb(0xE5484D));
}

static void draw_fence(cl_buf_t *b, int x0, int x1, int top, int bottom)
{
    const uint16_t slat = cl_rgb(0xF2EDE0);
    const uint16_t edge = cl_rgb(0xCFC6B2);

    for (int x = x0; x < x1; x += 7) {
        cl_rect(b, x, top + 2, 4, bottom - top - 2, slat);
        cl_vline(b, x + 3, top + 2, bottom - top - 2, edge);
        /* pointed tip */
        cl_hline(b, x + 1, top, 2, slat);
        cl_hline(b, x, top + 1, 4, slat);
    }
    cl_rect(b, x0, top + 5, x1 - x0, 2, slat);
    cl_rect(b, x0, bottom - 6, x1 - x0, 2, slat);
    cl_hline(b, x0, top + 6, x1 - x0, edge);
    cl_hline(b, x0, bottom - 5, x1 - x0, edge);
}

static void scene_park(cl_buf_t *b)
{
    /* sky in bands: a fine gradient is lost at this resolution, and marked
     * bands come out more retro */
    static const uint32_t sky[] = {
        0x59B7EE, 0x6BC2F1, 0x7ECDF4, 0x92D8F7, 0xA8E2FA, 0xBEEBFC,
    };
    for (int i = 0; i < 6; i++) {
        cl_rect(b, 0, i * 8, CL_ART_W, 8, cl_rgb(sky[i]));
    }
    cl_rect(b, 0, 48, CL_ART_W, 2, cl_rgb(0xCDF0FD));

    /* hills in the distance */
    for (int x = 0; x < CL_ART_W; x++) {
        int h1 = 6 - ((x - 20) * (x - 20)) / 90;
        int h2 = 8 - ((x - 68) * (x - 68)) / 70;
        int h = h1 > h2 ? h1 : h2;
        if (h > 0) {
            cl_vline(b, x, 50 - h, h, cl_rgb(0x6FBF74));
            cl_px(b, x, 50 - h, cl_rgb(0x8AD48C));
        }
    }

    /* grass */
    cl_rect(b, 0, 50, CL_ART_W, CL_ART_H, cl_rgb(0x6CC24A));
    cl_rect(b, 0, 50, CL_ART_W, 3, cl_rgb(0x57A83B));
    /* the grass in front, lighter: cheap depth */
    cl_rect(b, 0, 68, CL_ART_W, CL_ART_H - 68, cl_rgb(0x79CE55));

    noise_seed(0xA11CE);
    for (int i = 0; i < 90; i++) {
        int x = noise_range(0, CL_ART_W - 1);
        int y = noise_range(52, CL_ART_H - 2);
        uint16_t c = (y > 68) ? cl_rgb(0x63BC43) : cl_rgb(0x54A63A);
        cl_px(b, x, y, c);
        cl_px(b, x, y - 1, c);
    }

    draw_fence(b, 58, CL_ART_W, 36, 50);
    draw_tree(b, 14, 52);

    /* scattered flowers */
    cl_blit(b, 33, 71, CL_SPRITE(cl_spr_flower), false);
    cl_blit(b, 63, 73, CL_SPRITE(cl_spr_flower), true);
    cl_blit(b, 82, 60, CL_SPRITE(cl_spr_flower), false);
    cl_blit(b, 4, 63, CL_SPRITE(cl_spr_flower), true);

    /* dirt path where the critter stands */
    static const int8_t path[] = { 14, 18, 20, 21, 20, 17 };
    for (int i = 0; i < (int)(sizeof(path) / sizeof(path[0])); i++) {
        cl_hline(b, 46 - path[i], 63 + i, path[i] * 2, cl_rgb(0xCBA96F));
    }
    noise_seed(0xBEEF);
    for (int i = 0; i < 14; i++) {
        cl_px(b, noise_range(28, 64), noise_range(64, 68), cl_rgb(0xB08F58));
    }
}

/* -------------------------------------------------------------------------- */

void cl_scene_draw(cl_buf_t *b, cl_scene_id_t scene)
{
    if (scene == CL_SCENE_PARK) {
        scene_park(b);
    } else {
        scene_home(b);
    }
}

void cl_scene_anim(cl_buf_t *b, cl_scene_id_t scene, int frame)
{
    if (scene == CL_SCENE_PARK) {
        /* The sun is drawn per frame and not into the cached background: that
         * way at night it is enough not to draw it, instead of having to cover
         * it. The rays pulse into the bargain. */
        cl_disc(b, 76, 9, 7, cl_rgb(0xFFE066));
        cl_disc(b, 76, 9, 5, cl_rgb(0xFFF0A0));
        for (int i = 0; i < 8; i++) {
            static const int8_t dx[] = { 0, 7, 10, 7, 0, -7, -10, -7 };
            static const int8_t dy[] = { -10, -7, 0, 7, 10, 7, 0, -7 };
            int beat = ((frame / 6) + i) % 4 ? 5 : 6;
            cl_px(b, 76 + dx[i], 9 + dy[i], cl_rgb(0xFFE066));
            cl_px(b, 76 + dx[i] * (beat + 1) / beat,
                     9 + dy[i] * (beat + 1) / beat, cl_rgb(0xFFEC99));
        }

        /* The clouds travel along the strip of clear sky between the tree and
         * the sun, so they can be drawn over the copied background without
         * covering anything. */
        static const int8_t cloud_y[3] = { 6, 15, 3 };
        static const int8_t cloud_w[3] = { 11, 8, 6 };
        for (int i = 0; i < 3; i++) {
            int span = 40;
            int x = 28 + ((frame / (3 + i * 2) + i * 17) % span);
            int y = cloud_y[i];
            int w = cloud_w[i];
            cl_rect(b, x, y, w, 3, cl_rgb(0xFFFFFF));
            cl_rect(b, x + 2, y - 2, w - 4, 2, cl_rgb(0xFFFFFF));
            cl_hline(b, x + 1, y + 3, w - 2, cl_rgb(0xDCEEF8));
        }

        /* butterfly: it rises and falls as it crosses the grass */
        int bx = 20 + ((frame / 2) % 60);
        int by = 40 + (frame / 3) % 6;
        cl_blit(b, bx, by, CL_SPRITE(cl_spr_butterfly), (frame & 4) != 0);
    } else {
        /* indoors the window's clouds move, which is pure sky */
        int x = 10 + ((frame / 6) % 20);
        cl_rect(b, x, 20, 6, 2, cl_rgb(0xFFFFFF));
        cl_rect(b, x + 1, 19, 4, 1, cl_rgb(0xFFFFFF));
        /* dust motes floating against the light */
        for (int i = 0; i < 4; i++) {
            int px = 34 + i * 13 + ((frame / (4 + i)) % 7);
            int py = 20 + i * 6 + ((frame / (5 + i)) % 5);
            cl_px(b, px, py, cl_rgb(0xFBEFD8));
        }
    }
}

void cl_scene_night(cl_buf_t *b, cl_scene_id_t scene, int frame)
{
    cl_shade(b, 0, 0, b->w, b->h, -7);

    if (scene == CL_SCENE_PARK) {
        cl_blit(b, 72, 5, CL_SPRITE(cl_spr_moon), false);
        noise_seed(0x5EED);
        for (int i = 0; i < 22; i++) {
            int x = noise_range(2, CL_ART_W - 3);
            int y = noise_range(1, 40);
            /* they twinkle gently, each star with its own rhythm */
            if (((frame / 4) + i) % 7 != 0) {
                cl_px(b, x, y, cl_rgb(0xFFF6D0));
            }
        }
    } else {
        /* at night the window turns blue and the lamp stays lit */
        cl_rect(b, 8, 8, 24, 22, cl_rgb(0x1B2A4A));
        cl_blit(b, 20, 10, CL_SPRITE(cl_spr_moon), false);
        cl_px(b, 12, 13, cl_rgb(0xFFF6D0));
        cl_px(b, 16, 22, cl_rgb(0xFFF6D0));
        cl_px(b, 28, 26, cl_rgb(0xFFF6D0));
        cl_shade(b, 74, 36, 19, 12, 5);
        cl_shade(b, 76, 44, 15, 8, 4);
    }
}

const char *cl_scene_name(cl_scene_id_t scene)
{
    return (scene == CL_SCENE_PARK) ? _("PATIO") : _("CASA");
}
