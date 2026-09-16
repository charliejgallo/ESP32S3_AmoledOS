/*
 * Test bench for Burbujas. No LVGL, no HAL, no simulator: the four game files
 * and 'cc'.
 *
 *   cc -O1 -I../main bb_harness.c ../main/bb_pixel.c ../main/bb_art.c \
 *      ../main/bb_game.c ../main/bb_draw.c -o /tmp/bbh
 *
 *   /tmp/bbh verify [frames]
 *       The bot plays the three modes, with a jittery frame time. After EVERY
 *       frame the incremental drawing is compared with a from-scratch one,
 *       and every five frames each visible slot is painted alone on a
 *       sentinel canvas to check that nothing falls outside its box. Those
 *       are the two ways a dirty-rectangle scheme goes wrong, and on the
 *       board both look the same: a trail stuck on the screen.
 *
 *       It also checks the rules after every shot: nothing may be left
 *       hanging from nothing, no cell may hold a colour that does not exist,
 *       and no offset row may use its eighth column.
 *
 *   /tmp/bbh scene out.ppm [mode|title] [ms]
 *       The whole screen, x2, after 'ms' of the bot playing 'mode'.
 *
 *   /tmp/bbh strip out.ppm pop|blast|fall|bubbles
 *       An animation as a strip of frames, x3, to look at.
 *
 * PPM to PNG: python3 ../../../tools/ppm2png.py out.ppm
 */
#include "bb_art.h"
#include "burbujas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bb_sfx(int freq_hz, int ms)
{
    (void)freq_hz;
    (void)ms;
}

static uint16_t s_fb[BB_W * BB_H];
static uint16_t s_bg[BB_W * BB_H];
static uint16_t s_ref[BB_W * BB_H];
static uint16_t s_sent[BB_W * BB_H];
static bb_game_t G;

#define SENTINEL        0xF81F          /* magenta: no drawing uses it       */

static const char *const MODE_TAG[BB_MODES] = { "classic", "levels", "timed" };

static void setup(bb_game_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    bb_buf_init(&g->fb, s_fb, BB_W, BB_H);
    bb_buf_init(&g->bg, s_bg, BB_W, BB_H);
    bb_game_init(g, seed);
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
static int compare(bb_game_t *g, long frame, const char *tag)
{
    bb_render_full(g, s_ref);
    int diffs = 0, fx = -1, fy = -1;
    for (int i = 0; i < BB_W * BB_H; i++) {
        if (s_ref[i] != s_fb[i]) {
            if (!diffs) {
                fx = i % BB_W;
                fy = i / BB_W;
            }
            diffs++;
        }
    }
    if (diffs) {
        printf("  %s frame %ld: %d pixels differ from a full redraw, first at %d,%d\n",
               tag, frame, diffs, fx, fy);
        char path[64];
        snprintf(path, sizeof(path), "/tmp/bbh_%s_%ld_inc.ppm", tag, frame);
        ppm(path, s_fb, BB_W, BB_H, 2);
        snprintf(path, sizeof(path), "/tmp/bbh_%s_%ld_ref.ppm", tag, frame);
        ppm(path, s_ref, BB_W, BB_H, 2);
    }
    return diffs != 0;
}

/* each slot alone on a sentinel canvas: every pixel it puts down has to fall
 * inside the box the compositor thinks it has */
static int check_boxes(bb_game_t *g, long frame, const char *tag)
{
    int fails = 0;
    const int y0 = g->state == GS_TITLE ? 0 : BB_HUD_H;

    for (int i = 0; i < BB_SLOTS; i++) {
        const bb_slot_t *s = &g->slot[i];
        if (!s->vis) {
            continue;
        }
        for (int k = 0; k < BB_W * BB_H; k++) {
            s_sent[k] = SENTINEL;
        }
        bb_buf_t b;
        bb_buf_init(&b, s_sent, BB_W, BB_H);
        bb_clip(&b, 0, y0, BB_W, BB_H);
        bb_slot_paint(&b, &s->p);

        for (int y = 0; y < BB_H && fails < 4; y++) {
            for (int x = 0; x < BB_W; x++) {
                if (s_sent[y * BB_W + x] == SENTINEL) {
                    continue;
                }
                if (x < s->box.x0 || x >= s->box.x1 ||
                    y < s->box.y0 || y >= s->box.y1) {
                    printf("  %s frame %ld: slot %d (kind %d) paints %d,%d outside its box "
                           "%d,%d..%d,%d\n", tag, frame, i, s->p.kind, x, y,
                           s->box.x0, s->box.y0, s->box.x1, s->box.y1);
                    fails++;
                    y = BB_H;
                    break;
                }
            }
        }
    }
    return fails;
}

/* --------------------------------------------------------------------------
 * The rules, which a drawing check cannot see
 * -------------------------------------------------------------------------- */

static int check_board(bb_game_t *g, long frame, const char *tag)
{
    int bad = 0;

    /* every bubble hangs from the ceiling, directly or through others */
    uint8_t seen[BB_ROWS][BB_COLS];
    int8_t  stack[BB_ROWS * BB_COLS][2];
    int     top = 0;
    memset(seen, 0, sizeof(seen));

    for (int c = 0; c < bb_ncols(g, 0); c++) {
        if (g->cell[0][c]) {
            seen[0][c] = 1;
            stack[top][0] = 0;
            stack[top][1] = (int8_t)c;
            top++;
        }
    }
    for (int i = 0; i < top; i++) {
        int r = stack[i][0], c = stack[i][1];
        const int up = bb_indent(g, r) ? c : c - 1;
        const int8_t nb[6][2] = {
            { (int8_t)r, (int8_t)(c - 1) },     { (int8_t)r, (int8_t)(c + 1) },
            { (int8_t)(r - 1), (int8_t)up },    { (int8_t)(r - 1), (int8_t)(up + 1) },
            { (int8_t)(r + 1), (int8_t)up },    { (int8_t)(r + 1), (int8_t)(up + 1) },
        };
        for (int j = 0; j < 6; j++) {
            int rr = nb[j][0], cc = nb[j][1];
            if (bb_valid(g, rr, cc) && g->cell[rr][cc] && !seen[rr][cc]) {
                seen[rr][cc] = 1;
                stack[top][0] = (int8_t)rr;
                stack[top][1] = (int8_t)cc;
                top++;
            }
        }
    }

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            uint8_t v = g->cell[r][c];
            if (!v) {
                continue;
            }
            if (c >= bb_ncols(g, r)) {
                printf("  %s frame %ld: a bubble at %d,%d, and that row only has %d columns\n",
                       tag, frame, r, c, bb_ncols(g, r));
                bad++;
            } else if (v < BC_RED || v > BC_PURPLE) {
                printf("  %s frame %ld: colour %u on the board at %d,%d\n",
                       tag, frame, v, r, c);
                bad++;
            } else if (!seen[r][c] && g->sub != PS_FLY) {
                printf("  %s frame %ld: the bubble at %d,%d hangs from nothing\n",
                       tag, frame, r, c);
                bad++;
            }
        }
    }
    return bad;
}

static int verify(long frames)
{
    int bad = 0;

    for (int mode = 0; mode < BB_MODES; mode++) {
        setup(&G, 1234u + (uint32_t)mode * 77u);
        bb_game_start(&G, mode, 1);
        bb_bg_build(&G, false);
        G.autoplay = 1;

        long area = 0;
        int  maxr = 0, games = 0, level = 1;
        uint32_t best = 0, shots = 0, popped = 0, dropped = 0, rows = 0;
        uint16_t last_shots = 0;

        for (long f = 0; f < frames; f++) {
            int dt = 25 + (int)(bb_rand(&G) % 21u);     /* 25..45 ms */
            bb_game_bot(&G, dt);
            bb_game_step(&G, dt);
            G.events = 0;

            if (G.shots != last_shots) {
                last_shots = G.shots;
                bad += check_board(&G, f, MODE_TAG[mode]);
            }
            if (G.state == GS_OVER) {
                games++;
                if (G.score > best) best = G.score;
                shots   += G.shots;
                popped  += G.popped;
                dropped += G.dropped;
                rows    += G.rows_pushed;
                if (mode == MODE_LEVELS) {
                    level = G.won ? level + 1 : level;
                }
                bb_game_start(&G, mode, level);
                bb_bg_build(&G, false);
                last_shots = 0;
            }
            bb_present(&G);
            bad += compare(&G, f, MODE_TAG[mode]);
            if (f % 5 == 0) {
                bad += check_boxes(&G, f, MODE_TAG[mode]);
            }
            area += bb_dirty_area(&G.push);
            if (G.push.n > maxr) {
                maxr = G.push.n;
            }
            if (bad > 12) {
                printf("too many failures, stopping\n");
                return 1;
            }
        }
        /* the game still in progress counts too, or a run too short to
         * finish one reports zero shots and looks like a bot that never
         * played */
        printf("%-8s %ld frames, %d games%s, best %u | shots %u, burst %u, dropped %u, "
               "rows %u | pushed %ld%% of the field on average, up to %d rects\n",
               MODE_TAG[mode], frames, games,
               mode == MODE_LEVELS && games ? " (levels cleared)" : "",
               (unsigned)(G.score > best ? G.score : best),
               (unsigned)(shots + G.shots), (unsigned)(popped + G.popped),
               (unsigned)(dropped + G.dropped), (unsigned)(rows + G.rows_pushed),
               area * 100 / (frames * (long)BB_W * (BB_H - BB_HUD_H)), maxr);
    }

    setup(&G, 99u);
    bb_bg_build(&G, true);
    for (long f = 0; f < 900; f++) {
        G.title_t += 33;
        bb_present(&G);
        bad += compare(&G, f, "title");
        if (f % 5 == 0) {
            bad += check_boxes(&G, f, "title");
        }
    }
    printf("title    900 frames\n");

    printf(bad ? "FAILED (%d)\n" : "all frames identical to a full redraw, all boxes tight, board sane\n",
           bad);
    return bad ? 1 : 0;
}

static int scene(const char *out, const char *what, int ms)
{
    if (strcmp(what, "title") == 0) {
        setup(&G, 5u);
        bb_bg_build(&G, true);
        G.title_t = (uint32_t)ms;
        bb_present(&G);
        return ppm(out, s_fb, BB_W, BB_H, 2);
    }
    int mode = atoi(what);
    setup(&G, 42u);
    bb_game_start(&G, mode, 3);
    bb_bg_build(&G, false);
    G.autoplay = 1;
    for (int t = 0; t < ms; t += 33) {
        bb_game_bot(&G, 33);
        bb_game_step(&G, 33);
        G.events = 0;
        bb_present(&G);
        if (G.state == GS_OVER) {
            break;
        }
    }
    /* leave it aiming, which is the screen one wants to look at */
    bb_game_press(&G, 40, 70);
    bb_game_step(&G, 33);
    bb_present(&G);
    return ppm(out, s_fb, BB_W, BB_H, 2);
}

static int strip(const char *out, const char *name)
{
    setup(&G, 7u);
    bb_game_start(&G, MODE_CLASSIC, 1);
    bb_bg_build(&G, false);

    const int W = 60, H = 60, COLS = 8;
    int frames = 8;

    if (strcmp(name, "bubbles") == 0) {
        /* one of each, plus the specials: the art, at a glance */
        uint16_t *img = calloc((size_t)W * COLS * H, sizeof(uint16_t));
        bb_buf_t b;
        bb_buf_init(&b, img, W * COLS, H);
        for (int i = 0; i < 8; i++) {
            int color = i < 6 ? BC_RED + i : (i == 6 ? BC_RAINBOW : BC_BOMB);
            bb_bubble(&b, i * W + W / 2, H / 2 + 4, color, BS_NORMAL, 1);
        }
        int r = ppm(out, img, W * COLS, H, 3);
        free(img);
        return r;
    }
    if (strcmp(name, "pop") == 0) {
        frames = BB_POP_FRAMES;
    } else if (strcmp(name, "blast") == 0) {
        frames = BB_BLAST_FRAMES;
    } else if (strcmp(name, "fall") != 0) {
        printf("no such strip: %s\n", name);
        return 1;
    }

    const int rows = (frames + COLS - 1) / COLS;
    uint16_t *img = calloc((size_t)W * COLS * H * rows, sizeof(uint16_t));
    bb_buf_t b;
    bb_buf_init(&b, img, W * COLS, H * rows);

    for (int f = 0; f < frames; f++) {
        int cx = (f % COLS) * W + W / 2;
        int cy = (f / COLS) * H + H / 2;
        if (strcmp(name, "pop") == 0) {
            bb_pop_draw(&b, cx, cy, BC_BLUE, f, 3);
        } else if (strcmp(name, "blast") == 0) {
            bb_blast_draw(&b, cx, cy, f);
        } else {
            bb_bubble(&b, cx, cy + f, BC_GREEN, BS_NORMAL, 0);
        }
    }
    int r = ppm(out, img, W * COLS, H * rows, 3);
    free(img);
    return r;
}

/* Shot by shot, for when the board on the watch ends in a way the bench never
 * produced: how many misses, how far the ceiling has come down, and how close
 * to the line the lowest bubble is. */
static int trace_mode(int mode, int level, int shots)
{
    setup(&G, 11u);
    bb_game_start(&G, mode, level);
    bb_bg_build(&G, false);
    G.autoplay = 1;

    uint16_t last = 0;
    for (long f = 0; f < 40000 && G.state == GS_PLAY; f++) {
        bb_game_bot(&G, 33);
        bb_game_step(&G, 33);
        G.events = 0;
        bb_present(&G);

        if (G.shots != last) {
            last = G.shots;
            int lowest = -1;
            for (int r = BB_ROWS - 1; r >= 0 && lowest < 0; r--) {
                for (int c = 0; c < bb_ncols(&G, r); c++) {
                    if (G.cell[r][c]) {
                        lowest = bb_cell_y(&G, r) + BB_R;
                        break;
                    }
                }
            }
            printf("shot %2u | misses %u/%u | drops %u | top %d | lowest %d (line %d)"
                   " | burst %u | score %u\n",
                   (unsigned)G.shots, (unsigned)G.misses, (unsigned)G.miss_limit,
                   (unsigned)G.drops, (int)G.board_top, lowest, BB_DEAD_Y,
                   (unsigned)G.popped, (unsigned)G.score);
            if ((int)last >= shots) {
                break;
            }
        }
    }
    printf("state %u, won %u, shots %u, score %u\n",
           (unsigned)G.state, (unsigned)G.won, (unsigned)G.shots,
           (unsigned)G.score);
    return 0;
}

/* The same four aims over and over, which is what an injected drag on the
 * watch does. A bot that aims well can hide a rule that only bites a player
 * who keeps shooting at the same place. */
static int poke_mode(int mode, int level)
{
    static const int AIM[4][2] = { { 55, 75 }, { 130, 65 }, { 45, 90 }, { 115, 75 } };
    int k = 0;
    uint16_t last = 0;

    setup(&G, 11u);
    bb_game_start(&G, mode, level);
    bb_bg_build(&G, false);

    for (long f = 0; f < 6000 && G.state == GS_PLAY; f++) {
        if (f % 25 == 0 && G.sub == PS_AIM && !G.reload_ms && !G.push_pending) {
            bb_game_press(&G, BB_LAUNCH_X, 160);
            bb_game_drag(&G, AIM[k][0], AIM[k][1]);
            bb_game_release(&G, AIM[k][0], AIM[k][1]);
            k = (k + 1) % 4;
        }
        bb_game_step(&G, 33);
        G.events = 0;
        bb_present(&G);

        if (G.shots != last) {
            last = G.shots;
            int lowest = -1, cells = 0;
            for (int r = BB_ROWS - 1; r >= 0; r--) {
                for (int c = 0; c < bb_ncols(&G, r); c++) {
                    if (G.cell[r][c]) {
                        cells++;
                        if (lowest < 0) {
                            lowest = bb_cell_y(&G, r) + BB_R;
                        }
                    }
                }
            }
            printf("shot %2u | misses %u/%u | drops %u | top %d | lowest %d (line %d)"
                   " | cells %d | burst %u | score %u\n",
                   (unsigned)G.shots, (unsigned)G.misses, (unsigned)G.miss_limit,
                   (unsigned)G.drops, (int)G.board_top, lowest, BB_DEAD_Y, cells,
                   (unsigned)G.popped, (unsigned)G.score);
        }
    }
    printf("state %u, won %u, shots %u, score %u, burst %u\n",
           (unsigned)G.state, (unsigned)G.won, (unsigned)G.shots,
           (unsigned)G.score, (unsigned)G.popped);
    bb_present(&G);
    return ppm("/tmp/bb_poke.ppm", s_fb, BB_W, BB_H, 2);
}

int main(int argc, char **argv)
{
    if (!bb_art_init()) {
        printf("bb_art_init failed\n");
        return 1;
    }
    int r = 1;
    if (argc >= 2 && strcmp(argv[1], "verify") == 0) {
        r = verify(argc > 2 ? atol(argv[2]) : 20000);
    } else if (argc >= 3 && strcmp(argv[1], "scene") == 0) {
        r = scene(argv[2], argc > 3 ? argv[3] : "0", argc > 4 ? atoi(argv[4]) : 30000);
    } else if (argc >= 4 && strcmp(argv[1], "strip") == 0) {
        r = strip(argv[2], argv[3]);
    } else if (argc >= 2 && strcmp(argv[1], "trace") == 0) {
        r = trace_mode(argc > 2 ? atoi(argv[2]) : MODE_LEVELS,
                       argc > 3 ? atoi(argv[3]) : 1,
                       argc > 4 ? atoi(argv[4]) : 30);
    } else if (argc >= 2 && strcmp(argv[1], "poke") == 0) {
        r = poke_mode(argc > 2 ? atoi(argv[2]) : MODE_LEVELS,
                      argc > 3 ? atoi(argv[3]) : 1);
    } else {
        printf("usage: bbh verify [frames] | scene out.ppm [mode|title] [ms] | "
               "strip out.ppm pop|blast|fall|bubbles\n");
    }
    bb_art_free();
    return r;
}
