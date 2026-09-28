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


/* ---- the bot: can every level be won? ----
 *
 * Tommy cannot be hurt by what bites (g.god: monsters, cars, traps), but
 * water, pits, tar and the tide still take him: the way through is the
 * level's. Every time he stands still the bot looks for the shortest way
 * (hops, super hops, rides on logs and platforms) to the nearest key, then
 * to the exit, and tries the first step on a copy of the game half a second
 * ahead: only a step that leaves him alive where it meant goes. Levers are
 * pulled when nothing else leads on. */

#define BOT_MAXC (MH_LV_MAXW * MH_LV_MAXH)
static uint8_t s_ride[MH_LV_MAXH][MH_LV_MAXW];

static const int BDX[4] = { 0, 1, 0, -1 }, BDY[4] = { 1, 0, -1, 0 };

static void mark_ride(const mh_game_t *g)
{
    memset(s_ride, 0, sizeof s_ride);
    const mh_level_t *lv = g->lv;
    for (int i = 0; i < g->n_lane; i++) {
        const mh_lane_t *l = &g->lane[i];
        if (l->kind != LANE_LOG && l->kind != LANE_LILY) continue;
        /* lily pads float every other cell and never move */
        for (int k = 0; k < l->len; k += l->kind == LANE_LILY ? 2 : 1) {
            int x = l->x + BDX[l->dir] * k, y = l->y + BDY[l->dir] * k;
            if (mh_in(lv, x, y)) s_ride[y][x] = 1;
        }
    }
    for (int i = 0; i < g->n_plat; i++) {
        const mh_path_t *p = &lv->path[g->plat[i].path];
        for (int k = 0; k < p->n; k++) {
            int ax = lv->pt[p->first + k][0], ay = lv->pt[p->first + k][1];
            int b = (k + 1) % p->n;
            int bx = lv->pt[p->first + b][0], by = lv->pt[p->first + b][1];
            for (;;) {
                if (mh_in(lv, ax, ay)) s_ride[ay][ax] = 1;
                if (ax == bx && ay == by) break;
                ax += (bx > ax) - (bx < ax);
                ay += (by > ay) - (by < ay);
            }
        }
    }
}

/* where a move from (x, y, floor f) lands: false if the rules forbid it */
static bool bot_move(const mh_game_t *g, int x, int y, int f, int d, bool super, int *ox, int *oy, int *of)
{
    const mh_level_t *lv = g->lv;
    bool solid;
    int tx = x + BDX[d] * (super ? 2 : 1), ty = y + BDY[d] * (super ? 2 : 1);
    if (super) {
        int mf = mh_stand_floor(g, x + BDX[d], y + BDY[d], &solid);
        if (solid || (mf > -9 && mf > f + 2)) return false;
    }
    int tf = mh_stand_floor(g, tx, ty, &solid);
    if (solid || !mh_in(lv, tx, ty)) return false;
    if (tx == g->exit_x && ty == g->exit_y && !g->exit_open) return false;
    if (tf <= -9) {
        if (!s_ride[ty][tx]) return false;
        tf = mh_cell(lv, tx, ty)->h;
    } else if (tf - f > (super ? 2 : 1)) {
        return false;
    }
    *ox = tx;
    *oy = ty;
    *of = tf;
    return true;
}

/* breadth first from Tommy: distance and the first move to each cell */
static int s_dist[BOT_MAXC], s_first[BOT_MAXC];

static void bot_bfs_from(const mh_game_t *g, int sx, int sy, int sf)
{
    const mh_level_t *lv = g->lv;
    int n = lv->w * lv->h;
    for (int i = 0; i < n; i++) s_dist[i] = -1;
    static int q[BOT_MAXC], qf[BOT_MAXC];
    int h = 0, t = 0;
    if (!mh_in(lv, sx, sy)) return;
    s_dist[sy * lv->w + sx] = 0;
    s_first[sy * lv->w + sx] = -1;
    q[t] = sy * lv->w + sx;
    qf[t++] = sf;
    while (h < t) {
        int c = q[h], f = qf[h++];
        int x = c % lv->w, y = c / lv->w;
        for (int m = 0; m < 8; m++) {
            int d = m & 3;
            bool super = m >= 4;
            int tx, ty, tf;
            if (!bot_move(g, x, y, f, d, super, &tx, &ty, &tf)) continue;
            int k = ty * lv->w + tx;
            if (s_dist[k] >= 0) continue;
            s_dist[k] = s_dist[c] + (super ? 2 : 1);
            s_first[k] = s_first[c] < 0 ? m : s_first[c];
            q[t] = k;
            qf[t++] = tf;
        }
    }
}

static void bot_bfs(const mh_game_t *g)
{
    bot_bfs_from(g, g->h.cx, g->h.cy, g->h.floor);
}

/* the move tried on a copy: alive, not stuck in the air, where it meant */
static bool bot_safe(const mh_game_t *g, int m, float ahead)
{
    static mh_game_t p;
    p = *g;
    int lost = p.lost;
    if (m >= 0) {
        int d = m & 3;
        if (m >= 4) {
            p.h.dir = d;
            mh_game_action(&p);
        } else {
            mh_game_hop(&p, d);
        }
        if (p.h.state != H_HOP && p.h.state != H_SUPER) return false;
    }
    const float dt = 1.0f / 60.0f;
    for (float t = 0; t < ahead; t += dt) mh_game_step(&p, dt);
    return p.lost == lost && p.state != GS_DYING && p.state != GS_OVER;
}

static void bot_do(mh_game_t *g, int m)
{
    int d = m & 3;
    if (m >= 4) {
        g->h.dir = d;
        mh_game_action(g);
    } else {
        mh_game_hop(g, d);
    }
}

static int bot_level(const char *level, const char *pak, bool verbose)
{
    setup(level, pak);
    static mh_game_t g;
    mh_game_init(&g, &s_lv, DIFF_EASY, 1234);
    g.god = true;
    g.timer = false;
    g.lives = 999;
    mark_ride(&g);
    const float dt = 1.0f / 60.0f;
    float last_progress = 0, stuck = 0;
    uint32_t rnd = 12345;
    int keys_before = 0, deaths = 0, lever_used = 0, pushes = 0, lost = 0;
    int why[8] = { 0 };
    const char *verdict = "STUCK";
    while (g.t < 600) {
        mh_game_step(&g, dt);
        if (g.state == GS_WON) {
            verdict = "WON";
            break;
        }
        if (g.state == GS_DYING && g.lost == lost && g.st < dt * 1.5f) {
            lost = g.lost + 1;
            deaths++;
            why[g.h.die_why & 7]++;
            if (verbose) printf("  t %6.1f died (%d) at %d,%d\n", g.t, g.h.die_why, g.h.cx, g.h.cy);
        }
        if (g.keys != keys_before) {
            keys_before = g.keys;
            last_progress = g.t;
            if (verbose) printf("  t %6.1f key %d at %d,%d\n", g.t, g.keys, g.h.cx, g.h.cy);
        }
        if (g.t - last_progress > 90) break;
        if (verbose && (int)(g.t * 60) % 300 == 0)
            printf("  t %6.1f at %d,%d floor %d state %d ride %d\n", g.t, g.h.cx, g.h.cy, g.h.floor, g.h.state, g.h.ride);
        if (g.state != GS_PLAY || g.h.state != H_IDLE) continue;
        /* where to: the nearest key by the way there, else the exit */
        bot_bfs(&g);
        const mh_level_t *lv = g.lv;
        int best = -1, bd = 1 << 30;
        for (int i = 0; i < g.n_pick; i++) {
            const mh_pick_t *p = &g.pick[i];
            if (p->type != ENT_KEY || p->taken) continue;
            int k = p->y * lv->w + p->x;
            if (s_dist[k] > 0 && s_dist[k] < bd) {
                bd = s_dist[k];
                best = k;
            }
        }
        if (best < 0 && g.exit_open) {
            /* the exit: through the cell in front of it */
            int k = g.exit_y * lv->w + g.exit_x;
            if (s_dist[k] > 0) best = k;
        }
        if (best < 0 && pushes < 60) {
            /* nothing leads on: a crate that slides into a hole (water or a
             * pit) along a flat way, pushed from behind */
            int bc = -1, bdir = 0, bstand = -1, bdd = 1 << 30;
            for (int i = 0; i < g.n_crate; i++) {
                const mh_crate_t *c = &g.crate[i];
                if (c->sunk) continue;
                for (int d = 0; d < 4; d++) {
                    int sx = c->x - BDX[d], sy = c->y - BDY[d];
                    if (!mh_in(lv, sx, sy)) continue;
                    bool hole = false;
                    for (int j = 1; j <= 8; j++) {
                        int x = c->x + BDX[d] * j, y = c->y + BDY[d] * j;
                        if (!mh_in(lv, x, y)) break;
                        const mh_cell_t *hc = mh_cell(lv, x, y);
                        if (hc->kind == CK_WATER || hc->kind == CK_PIT) {
                            hole = !(hc->flags & CF_SOLID);
                            break;
                        }
                        bool solid;
                        int f = mh_stand_floor(&g, x, y, &solid);
                        if (solid || f != c->z) break;
                    }
                    if (!hole) continue;
                    int k = sy * lv->w + sx;
                    int dd = (sx == g.h.cx && sy == g.h.cy) ? 0 : s_dist[k];
                    if (dd >= 0 && dd < bdd && (dd > 0 || (sx == g.h.cx && sy == g.h.cy))) {
                        bdd = dd;
                        bc = i;
                        bdir = d;
                        bstand = k;
                    }
                }
            }
            if (bc >= 0 && bdd == 0) {
                g.h.dir = bdir;
                mh_game_action(&g);
                pushes++;
                last_progress = g.t;
                if (verbose) printf("  t %6.1f push crate %d %c\n", g.t, bc, "nesw"[bdir]);
                continue;
            }
            if (bc >= 0) best = bstand;
        }
        if (best < 0 && lever_used < g.n_lever) {
            /* nothing leads on: the next lever, from a cell beside it */
            const mh_lever_t *lvr = &g.lever[lever_used];
            for (int d = 0; d < 4 && best < 0; d++) {
                int ax = lvr->x - BDX[d], ay = lvr->y - BDY[d];
                if (!mh_in(lv, ax, ay)) continue;
                int k = ay * lv->w + ax;
                if (k == g.h.cy * lv->w + g.h.cx) {
                    g.h.dir = d;
                    mh_game_action(&g);
                    lever_used++;
                    last_progress = g.t;
                    if (verbose) printf("  t %6.1f lever %d\n", g.t, lever_used);
                    best = -2;
                } else if (s_dist[k] > 0) {
                    best = k;
                }
            }
            if (best == -2) continue;
        }
        int m = best >= 0 ? s_first[best] : -1;
        if (m >= 0 && bot_safe(&g, m, 0.6f)) {
            bot_do(&g, m);
            stuck = 0;
            continue;
        }
        /* the planned step is not safe now: another safe step that leaves
         * as short a way (a super hop instead of two hops...) */
        if (m >= 0 && best >= 0) {
            int want = s_dist[best], pick = -1;
            for (int k = 0; k < 8; k++) {
                int tx, ty, tf;
                if (k == m || !bot_move(&g, g.h.cx, g.h.cy, g.h.floor, k & 3, k >= 4, &tx, &ty, &tf)) continue;
                bot_bfs_from(&g, tx, ty, tf);
                int left = s_dist[best];
                if (left >= 0 && left + (k >= 4 ? 2 : 1) <= want && bot_safe(&g, k, 0.6f)) {
                    pick = k;
                    break;
                }
            }
            if (pick >= 0) {
                bot_do(&g, pick);
                stuck = 0;
                continue;
            }
        }
        /* never safe from here (rafts leaving at the edge, a trap in the
         * way): after a while, any safe step, and plan again from there */
        stuck += dt;
        if (stuck > 1.5f) {
            int k0 = (int)(mh_rand(&rnd) & 7);
            for (int k = 0; k < 8; k++) {
                int mm = (k0 + k) & 7;
                if (bot_safe(&g, mm, 0.8f)) {
                    bot_do(&g, mm);
                    stuck = 0;
                    break;
                }
            }
            continue;
        }
        /* waiting: only where waiting is safe, else any safe step */
        if (!bot_safe(&g, -1, 1.2f)) {
            for (int k = 0; k < 8; k++) {
                if (bot_safe(&g, k, 0.8f)) {
                    bot_do(&g, k);
                    break;
                }
            }
        }
    }
    if (verbose && g.state != GS_WON) {
        bot_bfs(&g);
        for (int i = 0; i < g.n_pick; i++)
            if (g.pick[i].type == ENT_KEY && !g.pick[i].taken)
                printf("  key left at %d,%d: way %d\n", g.pick[i].x, g.pick[i].y,
                       s_dist[g.pick[i].y * s_lv.w + g.pick[i].x]);
    }
    printf("%-10s %-5s t %5.1f s (limit %3d, par %3d)  keys %d/5  deaths %d (water %d quick %d pit %d)  at %d,%d\n",
           level, verdict, g.t, s_lv.time_s, s_lv.par_s, g.keys, deaths, why[DIE_WATER], why[DIE_QUICK], why[DIE_PIT],
           g.h.cx, g.h.cy);
    mh_world_free(&s_w);
    mh_level_free(&s_lv);
    mh_art_close();
    return strcmp(verdict, "WON") == 0;
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
    if (!strcmp(cmd, "bot")) {
        /* bot <level|all> [pak]: can it be won? (MH_BOT_V=1 tells the way) */
        const char *pak = argc > 3 ? argv[3] : "../assets/monsterhop.pak";
        static const char *const all[] = { "city_1", "city_2", "city_3", "city_4", "castle_1", "castle_2", "castle_3",
                                           "castle_4", "desert_1", "desert_2", "desert_3", "desert_4", "forest_1",
                                           "forest_2", "forest_3", "forest_4", "dino_1", "dino_2", "dino_3", "dino_4",
                                           "bay_1", "bay_2", "bay_3", "bay_4" };
        bool v = getenv("MH_BOT_V") != NULL;
        int won = 0, n = 0;
        for (int i = 0; i < 24; i++) {
            if (strcmp(level, "all") && strcmp(level, all[i])) continue;
            won += bot_level(all[i], pak, v);
            n++;
        }
        printf("%d of %d won\n", won, n);
        return won == n ? 0 : 1;
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
