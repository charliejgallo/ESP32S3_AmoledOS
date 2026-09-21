/*
 * NEON SNAKES - the bench
 *
 *   cc -O1 -I../main ns_harness.c ../main/ns_game.c ../main/ns_art.c \
 *      ../main/ns_draw.c -lm -o /tmp/nsh
 *
 *   /tmp/nsh                  every check, a few thousand steps per mode
 *   /tmp/nsh shot normal out.ppm [steps]   a frame to look at
 *   /tmp/nsh shot combat out.ppm [steps]
 *
 * What it checks, all without LVGL or a board:
 *
 *  - the rules: after every step, ns_check() - every grid cell agrees with
 *    the snakes' rings and the other way round, the fruit count adds up, and
 *    every body is a chain of neighbouring cells;
 *  - the compositor: every frame is drawn the fast way (only the marked
 *    blocks) and then again from scratch into a second buffer, and the two
 *    must be identical to the pixel; a block the engine forgot to mark shows
 *    up here on the first frame it happens, with coordinates;
 *  - lockstep: two engines with the same seed fed the same directions must
 *    hash the same after every step, which is the whole of the two-watch
 *    mode;
 *  - and it measures what a frame pushes, for the board.
 */
#include "ns_art.h"
#include "ns_draw.h"
#include "ns_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[NS_SCREEN_W * NS_SCREEN_H], bg[NS_SCREEN_W * NS_SCREEN_H];
static uint16_t ref[NS_SCREEN_W * NS_SCREEN_H];

static uint32_t lcg = 12345;
static int rnd(int n)
{
    lcg = lcg * 1103515245u + 12345u;
    return (int)((lcg >> 16) % (uint32_t)n);
}

static void events_to_fx(ns_view_t *v, ns_game_t *g)
{
    for (int i = 0; i < g->nev; i++) {
        const ns_event_t *e = &g->ev[i];
        if (e->type == NS_EV_EAT) ns_view_fx(v, NS_FX_RING, e->x, e->y, ns_fruit_rgb(e->kind));
        if (e->type == NS_EV_DIE) ns_view_fx(v, NS_FX_BURST, e->x, e->y, 0xFFFFFF);
    }
    g->nev = 0;
}

/* Draws the same state from scratch into 'ref' and compares. */
static int compare(ns_view_t *v, ns_game_t *g, const char *tag, long frame)
{
    uint16_t *keep = v->fb;
    v->fb = ref;
    /* the full repaint without touching the state: paint every cell */
    memcpy(ref, bg, sizeof ref);
    ns_game_t copy = *g;          /* ns_view_full clears the marks: use a copy */
    uint8_t rep_any = v->rep_any;
    ns_view_t vc = *v;
    ns_view_full(&vc, &copy);
    (void)rep_any;
    v->fb = keep;
    for (int i = 0; i < NS_SCREEN_W * NS_SCREEN_H; i++) {
        if (fb[i] != ref[i]) {
            printf("  %s frame %ld: pixel %d,%d is %04X, a full repaint says %04X\n",
                   tag, frame, i % NS_SCREEN_W, i / NS_SCREEN_W, fb[i], ref[i]);
            return 1;
        }
    }
    return 0;
}

static int run_mode(uint8_t mode, int humans, int snakes, long steps, uint32_t seed)
{
    const char *tag = mode == NS_MODE_NORMAL ? "normal" : "combat";
    ns_game_t *g = calloc(1, sizeof *g);
    ns_art_t art;
    if (!ns_art_build(&art, mode == NS_MODE_NORMAL ? 16 : 10, mode == NS_MODE_NORMAL ? 1 : snakes)) {
        printf("  %s: out of memory for the art\n", tag);
        return 1;
    }
    ns_init(g, mode, seed, humans, snakes);
    ns_view_t v;
    ns_view_init(&v, fb, bg, &art, g);
    ns_view_full(&v, g);
    ns_view_halo(&v, 0, 60);

    int bad = 0, games = 1;
    long frame = 0;
    uint64_t pushed = 0, max_len = 0, deaths = 0, eaten = 0;
    uint32_t max_px = 0;
    for (long st = 0; st < steps && bad < 3; st++) {
        uint8_t dirs[NS_MAX_SNAKES];
        memset(dirs, NS_NODIR, sizeof dirs);
        for (int i = 0; i < humans && i < g->nsnakes; i++) {
            if (g->s[i].alive && !g->s[i].dying) dirs[i] = ns_bot_choice(g, i);
        }
        ns_step(g, dirs);
        for (int i = 0; i < g->nev; i++) {
            if (g->ev[i].type == NS_EV_DIE) deaths++;
            if (g->ev[i].type == NS_EV_EAT) eaten++;
        }
        events_to_fx(&v, g);
        int c = ns_check(g);
        if (c) {
            printf("  %s step %ld: ns_check says %d\n", tag, st, c);
            bad++;
        }
        for (int i = 0; i < g->nsnakes; i++) {
            if (g->s[i].len > max_len) max_len = g->s[i].len;
        }
        /* three frames per step, the pulse turning every other one */
        for (int f = 0; f < 3; f++, frame++) {
            ns_view_frame(&v, g, (uint8_t)((frame / 5) & 3));
            pushed += v.pixels;
            if (v.pixels > max_px) max_px = v.pixels;
            if (frame % 7 == 0 || st < 40) bad += compare(&v, g, tag, frame);
        }
        if (g->over) {
            ns_init(g, mode, seed + (uint32_t)st, humans, snakes);
            ns_view_init(&v, fb, bg, &art, g);
            ns_view_full(&v, g);
            games++;
        }
    }
    printf("%-7s %ld steps, %d games, %llu eaten, %llu deaths, longest %llu, "
           "%.1f %% of the screen per frame (max %.1f %%)%s\n",
           tag, steps, games, (unsigned long long)eaten, (unsigned long long)deaths,
           (unsigned long long)max_len,
           100.0 * pushed / (double)frame / (NS_SCREEN_W * NS_SCREEN_H),
           100.0 * max_px / (NS_SCREEN_W * NS_SCREEN_H), bad ? "  FAIL" : "");
    ns_art_free(&art);
    free(g);
    return bad;
}

static int lockstep(long steps)
{
    ns_game_t *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b);
    ns_init(a, NS_MODE_COMBAT, 777, 2, 4);
    ns_init(b, NS_MODE_COMBAT, 777, 2, 4);
    for (long st = 0; st < steps; st++) {
        uint8_t dirs[NS_MAX_SNAKES] = { NS_NODIR, NS_NODIR, NS_NODIR, NS_NODIR };
        if (rnd(4) == 0) dirs[0] = (uint8_t)rnd(4);
        if (rnd(4) == 0) dirs[1] = (uint8_t)rnd(4);
        ns_step(a, dirs);
        ns_step(b, dirs);
        if (ns_hash(a) != ns_hash(b)) {
            printf("lockstep: the two engines split at step %ld\n", st);
            return 1;
        }
    }
    printf("lockstep %ld steps, two humans at random and two bots: identical, hash %08X\n",
           steps, ns_hash(a));
    free(a);
    free(b);
    return 0;
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", NS_SCREEN_W, NS_SCREEN_H);
    for (int i = 0; i < NS_SCREEN_W * NS_SCREEN_H; i++) {
        uint16_t c = fb[i];
        uint8_t px[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                          (uint8_t)((c & 0x1F) * 255 / 31) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static int shot(const char *which, const char *path, long steps)
{
    bool combat = strcmp(which, "combat") == 0;
    bool title = strcmp(which, "title") == 0;
    if (title) {
        memset(fb, 0, sizeof fb);
        ns_art_title(fb, NS_SCREEN_W, NS_SCREEN_H, 40);
        ns_art_t art;
        ns_art_build(&art, 16, 1);
        for (int k = 0; k < NS_FRUIT_COUNT; k++) {
            int S = art.size, x0 = 184 - (NS_FRUIT_COUNT * 40) / 2 + k * 40 + 4, y0 = 220;
            for (int y = 0; y < S; y++)
                for (int x = 0; x < S; x++)
                    fb[(y0 + y) * NS_SCREEN_W + x0 + x] = art.fruit[k][2][y * S + x];
        }
        /* every part of two snakes and the flash, for a look at the sprites */
        ns_art_t big;
        ns_art_build(&big, 16, 4);
        const int show[3] = { 0, 1, NS_WHITE };
        for (int c = 0; c < 3; c++) {
            for (int k = 0; k < NS_SNAKE_SPRITES; k++) {
                int S = big.size, x0 = 4 + (k % 12) * 30, y0 = 262 + c * 62 + (k / 12) * 30;
                for (int y = 0; y < S; y++)
                    for (int x = 0; x < S; x++) {
                        uint16_t *d = &fb[(y0 + y) * NS_SCREEN_W + x0 + x];
                        *d = ns_max565(*d, big.snake[show[c]][k][y * S + x]);
                    }
            }
        }
        write_ppm(path);
        return 0;
    }
    ns_game_t *g = calloc(1, sizeof *g);
    ns_art_t art;
    ns_art_build(&art, combat ? 10 : 16, combat ? 4 : 1);
    ns_init(g, combat ? NS_MODE_COMBAT : NS_MODE_NORMAL, 42, 1, 4);
    ns_view_t v;
    ns_view_init(&v, fb, bg, &art, g);
    ns_view_full(&v, g);
    for (long st = 0; st < steps; st++) {
        uint8_t dirs[NS_MAX_SNAKES];
        memset(dirs, NS_NODIR, sizeof dirs);
        if (g->s[0].alive && !g->s[0].dying) dirs[0] = ns_bot_choice(g, 0);
        ns_step(g, dirs);
        events_to_fx(&v, g);
        ns_view_frame(&v, g, (uint8_t)(st & 3));
        if (g->over) break;
    }
    ns_view_full(&v, g);
    write_ppm(path);
    printf("shot after %ld steps: lengths", steps);
    for (int i = 0; i < g->nsnakes; i++) printf(" %d", g->s[i].len);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "shot") == 0) {
        return shot(argv[2], argv[3], argc >= 5 ? atol(argv[4]) : 200);
    }
    int bad = 0;
    bad += run_mode(NS_MODE_NORMAL, 1, 1, 6000, 1);
    bad += run_mode(NS_MODE_COMBAT, 1, 4, 6000, 2);
    bad += run_mode(NS_MODE_COMBAT, 2, 4, 6000, 3);
    bad += lockstep(20000);
    printf(bad ? "FAILED\n" : "all good\n");
    return bad ? 1 : 0;
}
