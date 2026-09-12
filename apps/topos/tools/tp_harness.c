/*
 * Test bench for Topos. No LVGL, no HAL, no simulator: the four game files
 * and 'cc'.
 *
 *   cc -O1 -I../main tp_harness.c ../main/tp_pixel.c ../main/tp_art.c \
 *      ../main/tp_game.c ../main/tp_draw.c -o /tmp/tph
 *
 *   /tmp/tph verify [frames]
 *       The bot plays the three modes (and the title idles), with a jittery
 *       frame time. After EVERY frame the incremental drawing is compared with
 *       a from-scratch one, and every five frames each visible slot is painted
 *       alone on a sentinel canvas to check that nothing falls outside its
 *       box. Those are the two ways a dirty-rectangle scheme goes wrong, and
 *       on the board both look the same: a trail stuck on the screen.
 *
 *   /tmp/tph scene out.ppm [mode|title] [ms]
 *       The whole screen, x2, after 'ms' of the bot playing 'mode'.
 *
 *   /tmp/tph strip out.ppm mole|whack|helmet|gold|bomb|boom
 *       One hole's animation as a strip of frames, x3, to look at.
 *
 * PPM to PNG: python3 ../../../tools/ppm2png.py out.ppm
 */
#include "topos.h"
#include "tp_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tp_sfx(int freq_hz, int ms)
{
    (void)freq_hz;
    (void)ms;
}

static uint16_t  s_fb[TP_W * TP_H];
static uint16_t  s_bg[TP_W * TP_H];
static uint16_t  s_ref[TP_W * TP_H];
static uint16_t  s_sent[TP_W * TP_H];
static tp_game_t G;

static const char *const MODE_TAG[TP_MODES] = { "classic", "survival", "frenzy" };

static void setup(tp_game_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    tp_buf_init(&g->fb, s_fb, TP_W, TP_H);
    tp_buf_init(&g->bg, s_bg, TP_W, TP_H);
    tp_game_init(g, seed);
}

static int ppm(const char *path, const uint16_t *px, int w, int h, int zoom)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w * zoom, h * zoom);
    unsigned char *line = malloc((size_t)w * zoom * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t c = px[y * w + x];
            unsigned char r = (unsigned char)(((c >> 11) & 0x1F) * 255 / 31);
            unsigned char g = (unsigned char)(((c >> 5) & 0x3F) * 255 / 63);
            unsigned char b = (unsigned char)((c & 0x1F) * 255 / 31);
            for (int k = 0; k < zoom; k++) {
                line[(x * zoom + k) * 3 + 0] = r;
                line[(x * zoom + k) * 3 + 1] = g;
                line[(x * zoom + k) * 3 + 2] = b;
            }
        }
        for (int k = 0; k < zoom; k++) {
            fwrite(line, 1, (size_t)w * zoom * 3, f);
        }
    }
    free(line);
    fclose(f);
    printf("-> %s (%dx%d)\n", path, w * zoom, h * zoom);
    return 0;
}

/* fb, drawn frame by frame, against bg + every slot drawn from scratch */
static int compare(tp_game_t *g, long frame, const char *tag)
{
    tp_render_full(g, s_ref);
    int diffs = 0, fx = -1, fy = -1;
    for (int i = 0; i < TP_W * TP_H; i++) {
        if (s_ref[i] != s_fb[i]) {
            if (!diffs) {
                fx = i % TP_W;
                fy = i / TP_W;
            }
            diffs++;
        }
    }
    if (diffs) {
        printf("  %s frame %ld: %d pixels differ from a full redraw, first at %d,%d\n",
               tag, frame, diffs, fx, fy);
        char path[64];
        snprintf(path, sizeof(path), "/tmp/tph_%s_%ld_inc.ppm", tag, frame);
        ppm(path, s_fb, TP_W, TP_H, 2);
        snprintf(path, sizeof(path), "/tmp/tph_%s_%ld_ref.ppm", tag, frame);
        ppm(path, s_ref, TP_W, TP_H, 2);
    }
    return diffs != 0;
}

/* each slot alone on a sentinel canvas: every pixel it puts down has to fall
 * inside the box the compositor thinks it has */
static int check_boxes(tp_game_t *g, long frame, const char *tag)
{
    int fails = 0;
    const int y0 = g->state == GS_TITLE ? 0 : TP_HUD_H;

    for (int i = 0; i < TP_SLOTS; i++) {
        const tp_slot_t *s = &g->slot[i];
        if (!s->vis) {
            continue;
        }
        for (int k = 0; k < TP_W * TP_H; k++) {
            s_sent[k] = TP_KEY;
        }
        tp_buf_t b;
        tp_buf_init(&b, s_sent, TP_W, TP_H);
        tp_clip(&b, 0, y0, TP_W, TP_H);
        tp_slot_paint(&b, &s->p);

        for (int y = 0; y < TP_H; y++) {
            for (int x = 0; x < TP_W; x++) {
                if (s_sent[y * TP_W + x] == TP_KEY) {
                    continue;
                }
                if (x < s->box.x0 || x >= s->box.x1 || y < s->box.y0 || y >= s->box.y1) {
                    printf("  %s frame %ld: slot %d (kind %d) paints %d,%d outside its box "
                           "%d,%d..%d,%d\n", tag, frame, i, s->p.kind, x, y,
                           s->box.x0, s->box.y0, s->box.x1, s->box.y1);
                    fails++;
                    y = TP_H;
                    break;
                }
            }
        }
    }
    return fails;
}

static int verify(long frames)
{
    int bad = 0;

    for (int mode = 0; mode < TP_MODES; mode++) {
        setup(&G, 1234u + (uint32_t)mode * 77u);
        tp_bg_build(&G, false);
        tp_game_start(&G, mode);
        G.autoplay = 1;

        long area = 0;
        int maxr = 0, games = 0;
        uint32_t best = 0, hits = 0, bombs = 0, escaped = 0, helmets = 0, golds = 0;

        for (long f = 0; f < frames; f++) {
            int dt = 25 + (int)(tp_rand(&G) % 21u);     /* 25..45 ms */
            tp_game_bot(&G, dt);
            tp_game_step(&G, dt);
            G.events = 0;
            if (G.state == GS_OVER) {
                games++;
                if (G.score > best) best = G.score;
                hits += G.hits;
                bombs += G.bombs;
                escaped += G.escaped;
                helmets += G.helmets;
                golds += G.golds;
                tp_game_start(&G, mode);
            }
            tp_present(&G);
            bad += compare(&G, f, MODE_TAG[mode]);
            if (f % 5 == 0) {
                bad += check_boxes(&G, f, MODE_TAG[mode]);
            }
            area += tp_dirty_area(&G.push);
            if (G.push.n > maxr) {
                maxr = G.push.n;
            }
            if (bad > 12) {
                printf("too many failures, stopping\n");
                return 1;
            }
        }
        printf("%-9s %ld frames, %d games, best %u | hits %u, hats %u, gold %u, "
               "bombs %u, escaped %u | pushed %ld%% of the field on average, up to %d rects\n",
               MODE_TAG[mode], frames, games, (unsigned)best, (unsigned)hits,
               (unsigned)helmets, (unsigned)golds, (unsigned)bombs, (unsigned)escaped,
               area * 100 / (frames * (long)TP_W * (TP_H - TP_HUD_H)), maxr);
    }

    setup(&G, 99u);
    tp_bg_build(&G, true);
    for (long f = 0; f < 900; f++) {
        G.title_t += 33;
        tp_present(&G);
        bad += compare(&G, f, "title");
        if (f % 5 == 0) {
            bad += check_boxes(&G, f, "title");
        }
    }
    printf("title     900 frames\n");

    printf(bad ? "FAILED (%d)\n" : "all frames identical to a full redraw, all boxes tight\n", bad);
    return bad ? 1 : 0;
}

static int scene(const char *out, const char *what, int ms)
{
    if (strcmp(what, "title") == 0) {
        setup(&G, 5u);
        tp_bg_build(&G, true);
        G.title_t = (uint32_t)ms;
        tp_present(&G);
        return ppm(out, s_fb, TP_W, TP_H, 2);
    }
    int mode = atoi(what);
    setup(&G, 42u);
    tp_bg_build(&G, false);
    tp_game_start(&G, mode);
    G.autoplay = 1;
    for (int t = 0; t < ms; t += 33) {
        tp_game_bot(&G, 33);
        tp_game_step(&G, 33);
        G.events = 0;
        tp_present(&G);
        if (G.state == GS_OVER) {
            break;
        }
    }
    return ppm(out, s_fb, TP_W, TP_H, 2);
}

static void put_hole(tp_game_t *g, int i, int occ)
{
    tp_hole_t *h = &g->holes[i];
    memset(h, 0, sizeof(*h));
    h->occ     = (uint8_t)occ;
    h->phase   = PH_RISE;
    h->helmet  = occ == OCC_HELMET;
    h->seed    = 5;
    h->rise_ms = 180;
    h->up_ms   = 1100;
    h->fuse_ms = 1800;
}

static int strip(const char *out, const char *name)
{
    struct {
        const char *name;
        int occ;
        int taps[3];
        int step, n;
    } const S[] = {
        { "mole",   OCC_MOLE,   { -1, -1, -1 },   66, 40 },
        { "whack",  OCC_MOLE,   { 330, -1, -1 },  40, 24 },
        { "helmet", OCC_HELMET, { 330, 760, -1 }, 45, 32 },
        { "gold",   OCC_GOLD,   { 700, -1, -1 },  50, 24 },
        { "bomb",   OCC_BOMB,   { -1, -1, -1 },   66, 44 },
        { "boom",   OCC_BOMB,   { 330, -1, -1 },  38, 28 },
    };
    int k = -1;
    for (int i = 0; i < (int)(sizeof(S) / sizeof(S[0])); i++) {
        if (strcmp(S[i].name, name) == 0) {
            k = i;
        }
    }
    if (k < 0) {
        printf("no such strip: %s\n", name);
        return 1;
    }

    setup(&G, 7u);
    tp_bg_build(&G, false);
    tp_game_start(&G, MODE_CLASSIC);
    G.state   = GS_PLAY;
    G.time_ms = 1000000;
    put_hole(&G, 4, S[k].occ);

    const int X0 = 40, Y0 = 48, WW = 104, WH = 116, COLS = 8;
    const int rows = (S[k].n + COLS - 1) / COLS;
    uint16_t *img = calloc((size_t)WW * COLS * WH * rows, sizeof(uint16_t));
    int t = 0, tap = 0;

    for (int f = 0; f < S[k].n; f++) {
        while (t < f * S[k].step) {
            if (tap < 3 && S[k].taps[tap] >= 0 && t >= S[k].taps[tap]) {
                const tp_hole_t *h = &G.holes[4];
                int y = h->occ == OCC_BOMB ? TP_BOMB_CY(132, h->rise)
                                           : TP_HEAD_TOP(132, h->rise) + 8;
                tp_game_tap(&G, 92, y);
                tap++;
            }
            G.spawn_ms = 1 << 30;
            tp_game_step(&G, 10);
            G.events = 0;
            t += 10;
        }
        tp_present(&G);
        int cx = (f % COLS) * WW, cy = (f / COLS) * WH;
        for (int y = 0; y < WH; y++) {
            memcpy(img + (size_t)(cy + y) * WW * COLS + cx,
                   s_fb + (size_t)(Y0 + y) * TP_W + X0, WW * sizeof(uint16_t));
        }
        /* a dark line between frames */
        for (int y = 0; y < WH; y++) {
            img[(size_t)(cy + y) * WW * COLS + cx] = 0;
        }
    }
    int r = ppm(out, img, WW * COLS, WH * rows, 3);
    free(img);
    return r;
}

int main(int argc, char **argv)
{
    if (!tp_art_init()) {
        printf("tp_art_init failed\n");
        return 1;
    }
    int r = 1;
    if (argc >= 2 && strcmp(argv[1], "verify") == 0) {
        r = verify(argc > 2 ? atol(argv[2]) : 20000);
    } else if (argc >= 3 && strcmp(argv[1], "scene") == 0) {
        r = scene(argv[2], argc > 3 ? argv[3] : "0", argc > 4 ? atoi(argv[4]) : 30000);
    } else if (argc >= 4 && strcmp(argv[1], "strip") == 0) {
        r = strip(argv[2], argv[3]);
    } else {
        printf("usage: tph verify [frames] | scene out.ppm [mode|title] [ms] | "
               "strip out.ppm mole|whack|helmet|gold|bomb|boom\n");
    }
    tp_art_free();
    return r;
}
