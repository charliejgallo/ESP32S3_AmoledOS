/*
 * GOLF - the test bench, with plain cc on the Mac
 *
 *   map <hole> out.ppm        the whole hole, as the aiming screen shows it
 *   green <hole> out.ppm      the green close up, as the putting screen does
 *   albedo <hole> out.ppm     the 3D view's ground texture
 *   view <hole> out.ppm [x y aimdeg]  the 3D view from behind the ball
 *
 * Build (from apps/golf/tools):
 *   cc -O2 -I../main gf_harness.c ../main/gf_*.c -o /tmp/gfh -lm
 */
#include "gf_world.h"
#include "gf_map.h"
#include "gf_gfx.h"
#include "gf_view3d.h"
#include "gf_phys.h"
#include "gf_art.h"
#include "gf_game.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the two HAL calls the engine makes, for plain cc */
#include <stdarg.h>
void aos_hal_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

uint64_t aos_hal_uptime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void write_ppm(const char *path, const uint16_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t c = px[i];
        unsigned char rgb[3] = {
            (unsigned char)(((c >> 11) & 31) * 255 / 31),
            (unsigned char)(((c >> 5) & 63) * 255 / 63),
            (unsigned char)((c & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* the overview: the playing area fitted to the screen, tee at the bottom */
static void overview(const gf_world_t *w, gf_view_t *v)
{
    const gf_hole_t *h = w->def;
    float bw = (float)(h->x1 - h->x0), bh = (float)(h->y1 - h->y0);
    float ppm = (GF_H - 16) / bh;
    if (bw * ppm > GF_W - 8) ppm = (GF_W - 8) / bw;
    gf_view_set(v, (h->x0 + h->x1) * 0.5f, (h->y0 + h->y1) * 0.5f, GF_W / 2.0f, GF_H / 2.0f, ppm, 0.0f);
}


/* the bot: the caddie's club and line, a power that fits, a little error */
static uint32_t s_r = 12345;
static float frand(void) { s_r ^= s_r << 13; s_r ^= s_r >> 17; s_r ^= s_r << 5; return (float)(s_r & 0xFFFFFF) / 16777215.0f; }

static float bot_aim(const gf_world_t *w, float px, float py, int club, int lie, float *dist_out)
{
    float to_pin = atan2f(w->pin_x - px, w->pin_y - py);
    float dist = gf_game_dist_to_pin(w, px, py);
    float carry = gf_phys_carry(club, 1.0f, lie);
    *dist_out = carry;
    if (club == CLUB_PT || carry >= dist * 0.92f) { *dist_out = dist; return to_pin; }
    static const float PEN[LIE_KINDS] = { 22, 0, 6, 0, 0, 0, 40, 400, 12, 50, 90, 30, 500 };
    float best = to_pin, bs = 1e9f;
    for (int k = -18; k <= 18; k++) {
        float ang = to_pin + k * 2.5f * 0.01745f;
        float x = px + sinf(ang) * carry, y = py + cosf(ang) * carry;
        float sc = gf_game_dist_to_pin(w, x, y) + PEN[gf_lie(w, x, y)] + fabsf((float)k) * 0.4f;
        if (sc < bs) { bs = sc; best = ang; }
    }
    return best;
}

static void bot_round(gf_world_t *w, int diff, float skill, int *hole_scores)
{
    static gf_game_t g;
    static gf_shot_t sh;
    gf_game_new(&g, MODE_TOUR, diff, 1, (uint32_t)(frand() * 1e6f) + 1, 0);
    for (;;) {
        gf_game_hole_start(&g, w);
        int guard = 0;
        while (!gf_game_hole_over(&g) && guard++ < 40) {
            gf_game_next(&g, w);
            gf_player_t *p = &g.pl[g.turn];
            float dist = gf_game_dist_to_pin(w, p->x, p->y);
            int club = gf_game_suggest_club(dist, p->lie, p->lie == LIE_GREEN);
            float target;
            float aim = bot_aim(w, p->x, p->y, club, p->lie, &target);
            memset(&sh, 0, sizeof sh);
            sh.club = club; sh.aim = aim + (frand() - 0.5f) * 0.03f * (1 - skill);
            sh.x0 = p->x; sh.y0 = p->y; sh.lie0 = p->lie; sh.wind_x = g.wind_x; sh.wind_y = g.wind_y;
            if (club == CLUB_PT) {
                sh.power = gf_putt_power(dist) * (1.0f + (frand() - 0.4f) * 0.25f * (1.2f - skill)) + 0.02f;
                sh.acc = (frand() - 0.5f) * 0.3f * (1 - skill);
            } else {
                float full = gf_phys_carry(club, 1.0f, p->lie);
                sh.power = target >= full ? 1.0f : target / full;
                sh.power *= 1.0f + (frand() - 0.5f) * 0.12f * (1 - skill);
                sh.acc = (frand() - 0.5f) * 1.2f * (1 - skill);
            }
            gf_phys_shot(w, &sh);
            if (getenv("GFV")) printf("  h%d %s lie%d d=%.1f pow=%.2f -> res%d lie%d d=%.1f carry=%.1f tot=%.1f\n", gf_game_hole(&g)+1, gf_club(club)->name, p->lie, dist, sh.power, sh.result, sh.lie, gf_game_dist_to_pin(w, sh.x, sh.y), sh.carry, sh.total);
            gf_game_apply(&g, w, &sh);
        }
        hole_scores[gf_game_hole(&g)] += g.pl[0].strokes[g.hi];
        if (!gf_game_next_hole(&g)) break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s map|green|albedo <hole 1..8> out.ppm\n", argv[0]);
        return 1;
    }
    if (gf_art_load("../assets/golf.pak")) gf_map_set_tree_art(gf_art_trees()); else fprintf(stderr, "no pak\n");
    static gf_world_t w;
    if (getenv("COURSE")) gf_course_select(atoi(getenv("COURSE")));
    if (!gf_world_init(&w, 320 * 640)) { fprintf(stderr, "no memory\n"); return 1; }
    int hole = atoi(argv[2]) - 1;
    if (hole < 0 || hole >= gf_course()->nholes) hole = 0;
    double t0 = now_ms();
    gf_world_load(&w, &gf_course()->holes[hole], 0, 0);
    double t1 = now_ms();
    fprintf(stderr, "hole %d '%s': grid %dx%d, %d polys, %d pts, %d trees, water %.2f, load %.1f ms\n",
            hole + 1, w.def->name, w.gw, w.gh, w.npoly, w.npts, w.ntrees, w.water_level, t1 - t0);

    static uint16_t screen[GF_W * GF_H];
    gf_view_t v;
    if (!strcmp(argv[1], "map")) {
        overview(&w, &v);
        t0 = now_ms();
        gf_map_render(&w, &v, screen, GF_W, GF_H, MAP_SHADE | MAP_TREES | MAP_MARKERS);
        fprintf(stderr, "map %.1f ms (ppm %.2f)\n", now_ms() - t0, v.ppm);
        write_ppm(argv[3], screen, GF_W, GF_H);
    } else if (!strcmp(argv[1], "green")) {
        gf_view_set(&v, w.green_cx, w.green_cy, GF_W / 2.0f, GF_H / 2.0f, 9.0f, 0.0f);
        t0 = now_ms();
        gf_map_render(&w, &v, screen, GF_W, GF_H, MAP_SHADE | MAP_TREES | MAP_MARKERS | MAP_GRID);
        fprintf(stderr, "green %.1f ms\n", now_ms() - t0);
        write_ppm(argv[3], screen, GF_W, GF_H);
    } else if (!strcmp(argv[1], "view")) {
        float mpp = 0.5f;
        int tw = (int)(w.gw / mpp), th = (int)(w.gh / mpp);
        uint16_t *tex = malloc((size_t)tw * th * 2);
        t0 = now_ms();
        gf_map_albedo(&w, tex, tw, th, mpp);
        fprintf(stderr, "albedo %dx%d %.1f ms\n", tw, th, now_ms() - t0);
        gf_albedo_t alb = { tex, tw, th, mpp };
        float bx = w.tee_x, by = w.tee_y;
        float aim = atan2f(w.pin_x - bx, w.pin_y - by);
        if (argc >= 7) { bx = atof(argv[4]); by = atof(argv[5]); aim = atof(argv[6]) * 0.0174533f; }
        gf_cam_t cam;
        gf_cam_swing(&cam, &w, bx, by, aim);
        static uint16_t depth[GF_W * GF_H];
        t0 = now_ms();
        gf_view3d_render(&w, &cam, &alb, screen, depth);
        fprintf(stderr, "view3d %.1f ms\n", now_ms() - t0);
        float sx, sy, zc;
        if (gf_cam_project(&cam, bx, by, gf_height(&w, bx, by) + 0.02f, &sx, &sy, &zc)) {
            gf_img_t im; gf_img_init(&im, screen, GF_W, GF_H);
            gf_ball(&im, (int)(sx * 16), (int)(sy * 16), 40);
            fprintf(stderr, "ball at %.1f %.1f\n", sx, sy);
        }
        write_ppm(argv[3], screen, GF_W, GF_H);
    } else if (!strcmp(argv[1], "clubs")) {
        gf_phys_init();
        static gf_shot_t sh;
        for (int c = 0; c < CLUB_N; c++) {
            memset(&sh, 0, sizeof sh);
            sh.club = c; sh.power = c == CLUB_PT ? 0.5f : 1.0f; sh.x0 = w.tee_x; sh.y0 = w.tee_y + 10;
            sh.aim = 0; sh.lie0 = LIE_FAIRWAY;
            gf_phys_shot(&w, &sh);
            printf("%-3s carry %5.1f yd total %5.1f yd apex %4.1f m  n=%d res=%d lie=%d\n", gf_club(c)->name,
                   sh.carry / GF_YD, sh.total / GF_YD, sh.apex, sh.n, sh.result, sh.lie);
        }
    } else if (!strcmp(argv[1], "bot")) {
        gf_phys_init();
        int rounds = atoi(argv[3]);
        for (int sk = 0; sk < 3; sk++) {
            float skill = sk == 0 ? 0.3f : (sk == 1 ? 0.6f : 0.9f);
            int hs[8] = { 0 };
            double t0b = now_ms();
            for (int r = 0; r < rounds; r++) bot_round(&w, DIFF_NORMAL, skill, hs);
            printf("skill %.1f:", skill);
            int tot = 0, par = 0;
            for (int h = 0; h < gf_course()->nholes; h++) {
                printf(" %d:%.2f(p%d)", h + 1, hs[h] / (double)rounds, gf_course()->holes[h].par);
                tot += hs[h]; par += gf_course()->holes[h].par;
            }
            printf("  total %.1f vs par %d  (%.0f ms/round)\n", tot / (double)rounds, par, (now_ms() - t0b) / rounds);
        }
    } else if (!strcmp(argv[1], "albedo")) {
        float mpp = 0.5f;
        int tw = (int)(w.gw / mpp), th = (int)(w.gh / mpp);
        uint16_t *tex = malloc((size_t)tw * th * 2);
        t0 = now_ms();
        gf_map_albedo(&w, tex, tw, th, mpp);
        fprintf(stderr, "albedo %dx%d %.1f ms\n", tw, th, now_ms() - t0);
        write_ppm(argv[3], tex, tw, th);
    }
    return 0;
}
