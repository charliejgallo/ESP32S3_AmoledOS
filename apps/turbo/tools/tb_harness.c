/*
 * TURBO - the test bench on the Mac (no LVGL, no board)
 *
 *   ./build.sh
 *   /tmp/tbh frame <stage> <metres> out.ppm [pak]   one frame at that distance
 *   /tmp/tbh strip <stage> out.ppm [pak]            8 frames along the stage, side by side
 *   /tmp/tbh drive <stage> [car] [diff]             a bot drives it: time, crashes
 *   /tmp/tbh bench <stage> [pak]                    ms per frame over the stage
 */
#include "tb_art.h"
#include "tb_game.h"
#include "tb_render.h"
#include "tb_track.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double tb_stat_area[64];
int tb_stat_n[64];

static uint32_t clk(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void write_ppm(const char *path, const uint16_t *px, int w, int h, int stride)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r, g, b;
            tb_unpack(px[(size_t)y * stride + x], &r, &g, &b);
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    }
    fclose(f);
}

static tb_render_t *setup(tb_track_t *t, int stage, const char *pak)
{
    tb_set_clock(clk);
    if (!tb_track_build(t, stage)) {
        fprintf(stderr, "no track\n");
        exit(1);
    }
    if (pak && tb_art_open(pak)) {
        tb_art_load_vehicles(0xFFFFFFFFu);
        tb_art_load_near(0);
        tb_art_load_stage(t);
    }
    tb_render_t *r = tb_render_new();
    tb_render_stage(r, t);
    return r;
}

/* runs the bot to distance z, so the traffic is where it would be */
static void run_to(tb_game_t *g, float z)
{
    int guard = 0;
    while (g->z < z && guard++ < 200000) {
        tb_game_bot(g);
        tb_game_step(g, 1.0f / 30.0f);
        if (g->state == RS_TIMEUP) g->time_left = 60, g->state = RS_RACING;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: tbh frame|strip|drive|bench <stage> ...\n");
        return 1;
    }
    int stage = atoi(argv[2]);
    tb_track_t t;
    tb_game_t g;
    memset(&g, 0, sizeof g);
    static uint16_t fb[TB_W * TB_H];
    tb_img_t im;
    tb_img_init(&im, fb, TB_W, TB_H);

    if (!strcmp(argv[1], "frame") && argc >= 5) {
        tb_render_t *r = setup(&t, stage, argc > 5 ? argv[5] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        run_to(&g, (float)atof(argv[3]));
        tb_render_world(r, &im, &g, 1.0f / 30.0f);
        write_ppm(argv[4], fb, TB_W, TB_H, TB_W);
        printf("z %.0f  x %.1f  v %d km/h  seg %d/%d\n", g.z, g.x, tb_game_kmh(&g), (int)(g.z / 5), t.nseg);
        return 0;
    }
    if (!strcmp(argv[1], "strip") && argc >= 4) {
        tb_render_t *r = setup(&t, stage, argc > 4 ? argv[4] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        const int N = 8, SW = TB_W / 2, SH = TB_H / 2;
        static uint16_t out[TB_W / 2 * 4 * TB_H / 2 * 2];
        float len = (float)t.nreal * TB_SEG_LEN;
        for (int i = 0; i < N; i++) {
            run_to(&g, len * (float)(i + 1) / (N + 1));
            tb_render_world(r, &im, &g, 1.0f / 30.0f);
            if (getenv("TB_FULL") && atoi(getenv("TB_FULL")) == i) {
                write_ppm("/tmp/full.ppm", fb, TB_W, TB_H, TB_W);
                printf("frame %d: z %.1f x %.2f cam %.2f seg %d curve %.3f y %.2f\n", i, g.z, g.x, g.cam_x,
                       (int)(g.z / 5), tb_seg(&t, (int)(g.z / 5))->curve, tb_seg(&t, (int)(g.z / 5))->y);
            }
            int ox = (i % 4) * SW, oy = (i / 4) * SH;
            for (int y = 0; y < SH; y++)
                for (int x = 0; x < SW; x++) out[(size_t)(oy + y) * SW * 4 + ox + x] = fb[(size_t)(y * 2) * TB_W + x * 2];
        }
        write_ppm(argv[3], out, SW * 4, SH * 2, SW * 4);
        return 0;
    }
    if (!strcmp(argv[1], "cars") && argc >= 6) {
        /* one traffic model up close, to look at a new vehicle:
         * cars <stage> <model> <z> <out.ppm> [pak]; model VH_* (8 = hearse) */
        tb_render_t *r = setup(&t, stage, argc > 6 ? argv[6] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        run_to(&g, (float)atof(argv[4]));
        static const float dz[] = {9, 16, 28, 45}, dx[] = {2.2f, -2.2f, 2.0f, -1.5f};
        for (int i = 0; i < g.ntraffic; i++) {
            tb_traffic_t *c = &g.traffic[i];
            c->z = i < 4 ? g.z + dz[i] : g.z + 900.0f;
            c->x = c->tx = i < 4 ? dx[i] : 0;
            c->model = (uint8_t)atoi(argv[3]);
            c->paint = c->model == VH_HEARSE ? TB_PAINT_HEARSE : (uint8_t)i;
            c->ghost = i == 3;
            c->braking = i == 1;
        }
        tb_render_world(r, &im, &g, 1.0f / 30.0f);
        write_ppm(argv[5], fb, TB_W, TB_H, TB_W);
        return 0;
    }
    if (!strcmp(argv[1], "seq") && argc >= 5) {
        /* frames every N metres from the start, full size: seq <stage> <every> <prefix> [pak] */
        tb_render_t *r = setup(&t, stage, argc > 5 ? argv[5] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        float every = (float)atof(argv[3]);
        char nm[256];
        for (int i = 1; i <= 12; i++) {
            run_to(&g, every * (float)i);
            tb_render_world(r, &im, &g, 1.0f / 30.0f);
            snprintf(nm, sizeof nm, "%s%02d.ppm", argv[4], i);
            write_ppm(nm, fb, TB_W, TB_H, TB_W);
            printf("%s z %.0f x %.2f cam %.2f v %d\n", nm, g.z, g.x, g.cam_x, tb_game_kmh(&g));
        }
        return 0;
    }
    if (!strcmp(argv[1], "drive")) {
        setup(&t, stage, NULL);
        int car = argc > 3 ? atoi(argv[3]) : 0, diff = argc > 4 ? atoi(argv[4]) : 1;
        tb_game_start(&g, &t, car, diff, 7);
        int steps = 0;
        float mint = 999;
        while (g.state != RS_FINISHED && g.state != RS_TIMEUP && steps < 30 * 600) {
            tb_game_bot(&g);
            tb_game_step(&g, 1.0f / 30.0f);
            if (g.state == RS_RACING && g.time_left < mint) mint = g.time_left;
            if (g.events & EV_CHECKPOINT) printf("  checkpoint %d at %.1f s, %.1f s left before +%.0f\n",
                                                 g.next_cp, g.elapsed, g.time_left - g.cp_added, g.cp_added);
            if (getenv("TBV") && (g.events & EV_CRASH)) printf("  crash z %.0f x %.2f v %.0f steer %.2f\n", g.z, g.x, g.v * 3.6f, g.in_steer);
            g.events = 0;
            steps++;
        }
        printf("%s, %s: %s in %.1f s, top %.0f km/h, %d crashes, %d passes, min time left %.1f, %.1f km\n",
               tb_stage_name(stage), tb_car_spec(car)->name, g.state == RS_FINISHED ? "FINISHED" : "time up",
               g.elapsed, g.top_speed, g.crashes, g.passes, mint, (float)t.nreal * TB_SEG_LEN / 1000.0f);
        return 0;
    }
    if (!strcmp(argv[1], "bands")) {
        /* the banded render must give the same pixels as the whole frame */
        tb_render_t *r = setup(&t, stage, argc > 3 ? argv[3] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        static uint16_t fb2[TB_W * TB_H], band[TB_W * TB_H];
        int BH = getenv("TB_BH") ? atoi(getenv("TB_BH")) : 48;
        int bad_frames = 0, frames = 0;
        long worst = 0;
        while (g.z < (float)t.nreal * TB_SEG_LEN - 50 && frames < 3000) {
            for (int k = 0; k < 10; k++) { tb_game_bot(&g); tb_game_step(&g, 1.0f / 30.0f); }
            if (g.state == RS_TIMEUP) g.time_left = 60, g.state = RS_RACING;
            float keep = 0;
            tb_render_prepare(r, &g, 0);
            (void)keep;
            tb_img_init(&im, fb, TB_W, TB_H);
            tb_render_band(r, &im, &g, 0, TB_H);
            for (int y0 = 0; y0 < TB_H; y0 += BH) {
                int y1 = y0 + BH > TB_H ? TB_H : y0 + BH;
                tb_img_t bi;
                tb_img_init(&bi, band - (size_t)y0 * TB_W, TB_W, TB_H);
                tb_img_clip(&bi, 0, y0, TB_W, y1);
                tb_render_band(r, &bi, &g, y0, y1);
                memcpy(fb2 + (size_t)y0 * TB_W, band, (size_t)(y1 - y0) * TB_W * 2);
            }
            long diff = 0;
            for (int i = 0; i < TB_W * TB_H; i++) diff += fb[i] != fb2[i];
            if (diff) {
                bad_frames++;
                if (diff > worst) {
                    worst = diff;
                    write_ppm("/tmp/band_a.ppm", fb, TB_W, TB_H, TB_W);
                    write_ppm("/tmp/band_b.ppm", fb2, TB_W, TB_H, TB_W);
                }
            }
            frames++;
        }
        printf("%d frames, %d differ, worst %ld px\n", frames, bad_frames, worst);
        return 0;
    }
    if (!strcmp(argv[1], "bench")) {
        tb_render_t *r = setup(&t, stage, argc > 3 ? argv[3] : NULL);
        tb_game_start(&g, &t, 0, DIFF_NORMAL, 7);
        int frames = 0;
        uint32_t t0 = clk();
        while (g.z < (float)t.nreal * TB_SEG_LEN - 50 && frames < 20000) {
            tb_game_bot(&g);
            tb_game_step(&g, 1.0f / 30.0f);
            tb_render_world(r, &im, &g, 1.0f / 30.0f);
            frames++;
            if (g.state == RS_TIMEUP) g.time_left = 60, g.state = RS_RACING;
        }
        uint32_t ms = clk() - t0;
        printf("%d frames, %.3f ms each on the Mac\n", frames, (double)ms / frames);
        double tot = 0;
        for (int k = 0; k < PR_N; k++) tot += tb_stat_area[k];
        printf("prop px per frame %.0f\n", tot / frames);
        for (int k = 0; k < PR_N; k++) if (tb_stat_n[k]) printf("  kind %2d: %6.0f px/frame, %5.1f drawn/frame\n", k, tb_stat_area[k] / frames, (double)tb_stat_n[k] / frames);
        return 0;
    }
    return 1;
}
