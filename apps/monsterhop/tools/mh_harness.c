/*
 * MONSTER HOP - the test bench on the Mac (no LVGL, no board)
 *
 *   ./build.sh
 *   /tmp/mhh frame <level> <cell x> <cell y> out.ppm [pak]   one frame looking at that cell
 *   /tmp/mhh level <level> out.ppm [pak]                     the whole level (big picture)
 *   /tmp/mhh bench <level> [pak]                             ms per frame panning over it
 *   /tmp/mhh play <level> "<script>" [pak]                   the rules alone, printed
 *   /tmp/mhh shot <level> "<script>" out.ppm [trail]         play it, then the whole scene
 *   /tmp/mhh racetest <level>                                the shared keys' rules
 */
#include "mh_art.h"
#include "mh_game.h"
#include "mh_level.h"
#include "mh_render.h"
#include "mh_world.h"
#include "mh_cast.h"
#include "mh_scene.h"
#include "mh_shop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t clk(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void write_ppm(const char *path, const uint16_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        int r, g, b;
        mh_unpack(px[i], &r, &g, &b);
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
    fclose(f);
}

static mh_level_t s_lv;
static mh_world_t s_w;
static mh_anim_t s_tommy, s_tommy_sh;
static mh_lut_t s_lut;

static void setup(const char *level, const char *pak)
{
    mh_set_clock(clk);
    if (!mh_art_open(pak)) {
        fprintf(stderr, "no pak %s\n", pak);
        exit(1);
    }
    char nm[40];
    snprintf(nm, sizeof nm, "lvl_%s", level);
    uint32_t len = 0;
    uint8_t *b = mh_art_blob(nm, &len);
    if (!b || !mh_level_parse(&s_lv, b, len)) {
        fprintf(stderr, "no level %s\n", nm);
        exit(1);
    }
    free(b);
    if (!mh_world_init(&s_w, &s_lv)) {
        fprintf(stderr, "world: out of memory\n");
        exit(1);
    }
    mh_art_load("tommy_test", &s_tommy);
    mh_art_load("tommy_test_sh", &s_tommy_sh);
    mh_pal_t pal = { { 0 } };
    pal.c[1] = 0xF5BE96;
    pal.c[3] = 0x141414;
    pal.c[5] = 0x2878F5;
    pal.c[8] = 0x282858;
    pal.c[11] = 0xE61E1E;
    mh_lut_build(&s_lut, &pal, mh_zone_look(s_lv.zone)->tint, 0);
    fprintf(stderr, "level %s: %dx%d, %d assets, art %u KB\n", level, s_lv.w, s_lv.h, s_lv.n_assets,
            (unsigned)(mh_art_bytes() / 1024));
}

static void add_tommy(mh_dlist_t *l, float x, float y, int floor)
{
    float z = mh_floor_z(floor);
    int lx = mh_iround(mh_lpx(&s_w, x, y)), ly = mh_iround(mh_lpy(&s_w, x, y, z));
    int d = mh_depth(&s_w, x, y, z);
    if (s_tommy_sh.n) {
        mh_draw_t *e = mh_dlist_add(l);
        e->s = &s_tommy_sh.f[0];
        e->fmt = MH_PX_PLANE;
        e->x = (int16_t)lx;
        e->y = (int16_t)ly;
        e->d = (int16_t)d;
        e->alpha = 140;
    }
    if (s_tommy.n) {
        mh_draw_t *e = mh_dlist_add(l);
        e->s = &s_tommy.f[0];
        e->lut = &s_lut;
        e->fmt = MH_PX_LID;
        e->x = (int16_t)lx;
        e->y = (int16_t)ly;
        e->d = (int16_t)d;
        e->flags = DR_XRAY;
        e->xray = mh_hex(0x80C0FF);
        e->prio = 1;
    }
}

static uint16_t s_fb[MH_W * MH_H];
static uint16_t s_band[MH_W * 64];

static void render(int cam_x, int cam_y, const mh_dlist_t *l, bool banded)
{
    mh_world_prepare(&s_w, cam_x, cam_y, 0, 0, 0);
    if (!banded) {
        mh_img_t im;
        mh_img_init(&im, s_fb, MH_W, MH_H);
        mh_render_band(&s_w, &im, cam_x, cam_y, 0, MH_H, l);
        return;
    }
    for (int y0 = 0; y0 < MH_H; y0 += 64) {
        int y1 = y0 + 64 > MH_H ? MH_H : y0 + 64;
        mh_img_t im;
        mh_img_init(&im, s_band - (size_t)y0 * MH_W, MH_W, MH_H);
        mh_img_clip(&im, 0, y0, MH_W, y1);
        mh_render_band(&s_w, &im, cam_x, cam_y, y0, y1, l);
        memcpy(s_fb + (size_t)y0 * MH_W, s_band, (size_t)(y1 - y0) * MH_W * 2);
    }
}

static void cam_on(float x, float y, int floor, int *cx, int *cy)
{
    *cx = mh_iround(mh_lpx(&s_w, x, y)) - MH_W / 2;
    *cy = mh_iround(mh_lpy(&s_w, x, y, mh_floor_z(floor))) - MH_H * 6 / 10;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: mhh frame|level|bench <level> ...\n");
        return 1;
    }
    const char *cmd = argv[1], *level = argv[2];
    static mh_dlist_t dl;
    if (!strcmp(cmd, "frame") && argc >= 6) {
        setup(level, argc > 6 ? argv[6] : "../assets/monsterhop.pak");
        float x = (float)atof(argv[3]) + 0.5f, y = (float)atof(argv[4]) + 0.5f;
        int fl = mh_cell(&s_lv, (int)x, (int)y)->h;
        int cx, cy;
        cam_on(x, y, fl, &cx, &cy);
        mh_dlist_clear(&dl);
        add_tommy(&dl, x, y, fl);
        /* a few more Tommys around, to see the depth test */
        for (int k = 0; k < s_lv.n_ents; k++) {
            const mh_ent_def_t *e = &s_lv.ent[k];
            if (e->type == ENT_KEY) add_tommy(&dl, e->x + 0.5f, e->y + 0.5f, e->z);
        }
        mh_dlist_sort(&dl);
        render(cx, cy, &dl, true);
        write_ppm(argv[5], s_fb, MH_W, MH_H);
        fprintf(stderr, "blocks drawn %d\n", s_w.blocks_drawn);
        return 0;
    }
    if (!strcmp(cmd, "bench")) {
        setup(level, argc > 3 ? argv[3] : "../assets/monsterhop.pak");
        mh_dlist_clear(&dl);
        add_tommy(&dl, s_lv.start_x + 0.5f, s_lv.start_y + 0.5f, 0);
        mh_dlist_sort(&dl);
        clock_t t0 = clock();
        int frames = 0;
        for (float y = 0; y < s_lv.h; y += 0.05f, frames++) {
            int cx, cy;
            cam_on(s_lv.w / 2.0f, y, 0, &cx, &cy);
            render(cx, cy, &dl, true);
        }
        double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        printf("%d frames, %.3f ms each (Mac), %d blocks drawn\n", frames, ms / frames, s_w.blocks_drawn);
        return 0;
    }
    if (!strcmp(cmd, "play") && argc >= 4) {
        /* play <level> "<script>" [pak]: n e s w = hop, a = action, . = wait
         * 0.25 s, digits = wait that many seconds, L = hop north onto whatever
         * rides there; prints what happens. MH_AT=x,y starts elsewhere */
        setup(level, argc > 4 ? argv[4] : "../assets/monsterhop.pak");
        static mh_game_t g;
        const char *at = getenv("MH_AT");   /* "x,y": start somewhere else */
        if (at) {
            s_lv.start_x = (uint8_t)atoi(at);
            s_lv.start_y = (uint8_t)atoi(strchr(at, ',') + 1);
        }
        mh_game_init(&g, &s_lv, DIFF_EASY, 1234);
        const char *sc = argv[3];
        const float dt = 1.0f / 60.0f;
        static const char *hs[] = { "idle", "hop", "super", "push", "use", "bump", "hurt", "sink", "fall", "win", "gone" };
        for (const char *c = sc; *c; c++) {
            float wait = 0.35f;
            switch (*c) {
            case 'n': mh_game_hop(&g, DIR_N); break;
            case 'e': mh_game_hop(&g, DIR_E); break;
            case 's': mh_game_hop(&g, DIR_S); break;
            case 'w': mh_game_hop(&g, DIR_W); break;
            case 'a': mh_game_action(&g); wait = 0.5f; break;
            case '.': wait = 0.25f; break;
            case 'L': {
                /* wait until a hop north would land on something that rides (and survive it) */
                int guard = 0;
                while (guard++ < 900) {
                    static mh_game_t probe;
                    probe = g;
                    mh_game_hop(&probe, DIR_N);
                    for (float t = 0; t < 0.4f; t += dt) mh_game_step(&probe, dt);
                    if (probe.h.ride >= 0 && probe.lives == g.lives && probe.h.state == 0) break;
                    mh_game_step(&g, dt);
                }
                mh_game_hop(&g, DIR_N);
                break;
            }
            default:
                if (*c >= '1' && *c <= '9') wait = (float)(*c - '0');
                else continue;
            }
            for (float t = 0; t < wait; t += dt) {
                mh_game_step(&g, dt);
                uint32_t ev = g.events;
                g.events = 0;
                if (ev & (EV_KEY | EV_HURT | EV_SPLASH | EV_FALL | EV_OPEN | EV_WIN | EV_CHECK | EV_PUSH | EV_LEVER |
                          EV_CHEST | EV_OVER | EV_RESPAWN | EV_HOWL | EV_STICKER))
                    printf("  t %6.2f  ev %06x\n", g.t, (unsigned)ev);
            }
            printf("%c -> cell %d,%d floor %d z %.2f  %s ride %d  keys %d coins %d lives %d state %d\n", *c, g.h.cx,
                   g.h.cy, g.h.floor, g.h.z, hs[g.h.state], g.h.ride, g.keys, g.coins, g.lives, g.state);
        }
        return 0;
    }
    if (!strcmp(cmd, "shot") && argc >= 5) {
        /* the script as in play, with Tommy's real layers and the scene;
         * MH_AT=x,y starts elsewhere; the frame is the last moment */
        setup(level, "../assets/monsterhop.pak");
        const char *at = getenv("MH_AT");
        if (at) {
            s_lv.start_x = (uint8_t)atoi(at);
            s_lv.start_y = (uint8_t)atoi(strchr(at, ',') + 1);
        }
        static mh_game_t g;
        static mh_cast_t cast;
        static mh_scene_t sc;
        mh_game_init(&g, &s_lv, DIFF_EASY, 1234);
        int8_t eq[CAT_N] = { 0, 0, -1, -1, 0, -1, 1, 1 };
        if (argc > 5) eq[CAT_TRAIL] = (int8_t)(atoi(argv[5]) - 1);
        /* MH_EQ=cap,shirt,back,hand,pet,trail,skin,hair (the shop's indices, -1 none) */
        const char *qe = getenv("MH_EQ");
        for (int k = 0; qe && k < CAT_N; k++) {
            eq[k] = (int8_t)atoi(qe);
            qe = strchr(qe, ',');
            if (qe) qe++;
        }
        mh_outfit_t o;
        mh_wear_t wr;
        int fx = 0, trail = 0;
        mh_shop_apply(eq, &o, &wr, &fx, &trail);
        mh_cast_hero(&cast, &wr);
        mh_cast_level(&cast, &s_lv);
        mh_scene_init(&sc, &s_w, &g, &o, fx);
        mh_scene_trail(&sc, trail);
        const float dt = 1.0f / 30.0f;
        for (const char *c = argv[3]; *c; c++) {
            float wait = 0.35f;
            switch (*c) {
            case 'n': mh_game_hop(&g, DIR_N); break;
            case 'e': mh_game_hop(&g, DIR_E); break;
            case 's': mh_game_hop(&g, DIR_S); break;
            case 'w': mh_game_hop(&g, DIR_W); break;
            case 'N': mh_game_hop(&g, DIR_N); wait = 0; break;   /* no wait: for mid-hop frames */
            case 'a': mh_game_action(&g); wait = 0.5f; break;
            case '.': wait = 0.1f; break;
            case ',': wait = 0.033f; break;
            default: if (*c >= '1' && *c <= '9') wait = (float)(*c - '0'); else continue;
            }
            for (float t = 0; t < wait; t += dt) {
                mh_game_step(&g, dt);
                g.events = 0;
                mh_scene_build(&sc, &s_w, &g, &cast, &dl, dt);
            }
        }
        mh_world_prepare(&s_w, sc.icam_x, sc.icam_y, 0, 0, 0);
        for (int y0 = 0; y0 < MH_H; y0 += 64) {
            int y1 = y0 + 64 > MH_H ? MH_H : y0 + 64;
            mh_img_t im;
            mh_img_init(&im, s_band - (size_t)y0 * MH_W, MH_W, MH_H);
            mh_img_clip(&im, 0, y0, MH_W, y1);
            mh_render_band(&s_w, &im, sc.icam_x, sc.icam_y, y0, y1, &dl);
            mh_scene_bits_draw(&sc, &s_w, &im, sc.icam_x, sc.icam_y);
            memcpy(s_fb + (size_t)y0 * MH_W, s_band, (size_t)(y1 - y0) * MH_W * 2);
        }
        write_ppm(argv[4], s_fb, MH_W, MH_H);
        printf("hero at %d,%d state %d, %d trail bits\n", g.h.cx, g.h.cy, g.h.state, sc.nbit);
        return 0;
    }
    if (!strcmp(cmd, "racetest")) {
        /* two games of the same level passing each other their keys: both
         * take key 0 (A first), B takes key 1, A takes the rest and gets out */
        setup(level, argc > 3 ? argv[3] : "../assets/monsterhop.pak");
        static mh_game_t A, B;
        static mh_level_t lb;
        lb = s_lv;
        lb.cell = malloc((size_t)s_lv.w * s_lv.h * sizeof(mh_cell_t));
        memcpy(lb.cell, s_lv.cell, (size_t)s_lv.w * s_lv.h * sizeof(mh_cell_t));
        mh_game_init(&A, &s_lv, DIFF_NORMAL, 7);
        mh_game_init(&B, &lb, DIFF_NORMAL, 7);
        A.link = B.link = true;
        A.host = true;
        int keys[8], nk = 0;
        for (int i = 0; i < A.n_pick && nk < 8; i++) if (A.pick[i].type == ENT_KEY) keys[nk++] = i;
        #define TAKE(G, i, T) do { (G).pick[i].taken = true; (G).pick[i].who = 0; (G).pick[i].at = (T); \
            (G).keys++; (G).my_keys++; } while (0)
        TAKE(A, keys[0], 5.0f);
        TAKE(B, keys[0], 5.2f);
        mh_game_rival_key(&A, keys[0], 5.2f);
        mh_game_rival_key(&B, keys[0], 5.0f);
        printf("both took key 0: A %d mine, B %d mine (want 1, 0)\n", A.my_keys, B.my_keys);
        TAKE(B, keys[1], 7.0f);
        mh_game_rival_key(&A, keys[1], 7.0f);
        for (int k = 2; k < 5; k++) {
            TAKE(A, keys[k], 8.0f + k);
            mh_game_rival_key(&B, keys[k], 8.0f + k);
        }
        if (A.keys >= MH_KEYS) A.exit_open = true;
        printf("keys: A total %d exit %d, B total %d exit %d (want 5 1 5 1)\n", A.keys, A.exit_open, B.keys, B.exit_open);
        A.state = GS_WON;
        A.t = 20.0f;
        B.t = 20.3f;
        mh_game_rival_exit(&B, 20.0f);
        printf("points: A sees %d-%d, B sees %d-%d (want 6-1 both ways)\n", mh_game_points(&A, false),
               mh_game_points(&A, true), mh_game_points(&B, true), mh_game_points(&B, false));
        /* a tie on the clock goes to the host */
        mh_game_init(&A, &s_lv, DIFF_NORMAL, 7);
        mh_game_init(&B, &lb, DIFF_NORMAL, 7);
        A.link = B.link = true;
        A.host = true;
        TAKE(A, keys[0], 3.0f);
        TAKE(B, keys[0], 3.0f);
        mh_game_rival_key(&A, keys[0], 3.0f);
        mh_game_rival_key(&B, keys[0], 3.0f);
        printf("tie on key 0: host %d, guest %d (want 1, 0)\n", A.my_keys, B.my_keys);
        return 0;
    }
    fprintf(stderr, "unknown command\n");
    return 1;
}
