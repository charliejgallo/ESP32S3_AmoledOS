/*
 * GOLF - a hole being played (see gf_app.h)
 *
 *   AIM     the map, turned so the hole goes up and zoomed to what matters
 *           for this shot; the finger sets the line, the arrows the club
 *   SWING   the 3D view from behind the golfer; three taps: start the
 *           backswing, set the power, and hit the sweet spot on the way down
 *   FLIGHT  back to the map, the ball flies with its shadow and leaves a line
 *   PUTT    the green close up, with the slope drawn as chevrons; two taps
 */
#include "gf_app.h"
#include "gf_audio.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI          3.14159265f
#define DEG         0.017453293f

/* the meter, on the right of the 3D view (screen pixels) */
#define MTR_X       322
#define MTR_W       28
#define MTR_ZERO    352         /* the sweet spot line                         */
#define MTR_K       216.0f      /* pixels per unit of power                    */
#define MTR_MIN     (-0.14f)
#define MTR_MAX     1.10f

/* the part of the map not under the HUD */
#define MAP_TOP     70
#define MAP_BOT     338

static const float METER_SPEED[DIFF_N] = { 0.72f, 0.95f, 1.25f };
static const float SWEET_W[DIFF_N] = { 0.085f, 0.060f, 0.040f };

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * The overlay compositor
 * -------------------------------------------------------------------------- */

static void invalidate_rect(app_t *a, const gf_rect_t *r)
{
    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    lv_area_t ar = { (int32_t)(co.x1 + r->x0), (int32_t)(co.y1 + r->y0),
                     (int32_t)(co.x1 + r->x1 - 1), (int32_t)(co.y1 + r->y1 - 1) };
    lv_obj_invalidate_area(a->canvas, &ar);
}

void gfo_set_bg(app_t *a, const uint16_t *bg)
{
    a->bg = bg;
    memcpy(a->fb, bg, (size_t)GF_W * GF_H * 2);
    gf_dirty_reset(&a->dprev);
    gf_dirty_reset(&a->dcur);
    lv_obj_invalidate(a->canvas);
}

void gfo_begin(app_t *a)
{
    for (int i = 0; i < a->dprev.n; i++) {
        gf_copy_rect(a->fb, a->bg, &a->dprev.r[i]);
    }
    gf_dirty_reset(&a->dcur);
}

void gfo_mark(app_t *a, int x0, int y0, int x1, int y1)
{
    gf_dirty_add(&a->dcur, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
}

void gfo_end(app_t *a)
{
    for (int i = 0; i < a->dprev.n; i++) invalidate_rect(a, &a->dprev.r[i]);
    for (int i = 0; i < a->dcur.n; i++) invalidate_rect(a, &a->dcur.r[i]);
    a->dprev = a->dcur;
}

static gf_img_t fb_img(app_t *a)
{
    gf_img_t im;
    gf_img_init(&im, a->fb, GF_W, GF_H);
    return im;
}

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static gf_player_t *cur_player(app_t *a)
{
    return &a->game.pl[a->game.turn];
}

static const char *lie_name(int lie)
{
    static const char *const N[LIE_KINDS] = {
        [LIE_ROUGH] = N_("Rough"), [LIE_FAIRWAY] = N_("Fairway"), [LIE_FIRST] = N_("Primer corte"),
        [LIE_TEE] = N_("Salida"), [LIE_FRINGE] = N_("Antegreen"), [LIE_GREEN] = N_("Green"),
        [LIE_BUNKER] = N_("Búnker"), [LIE_WATER] = N_("Agua"), [LIE_PATH] = N_("Camino"),
        [LIE_DEEP] = N_("Rough alto"), [LIE_FOREST] = N_("Bosque"), [LIE_WASTE] = N_("Arena y matas"),
        [LIE_OB] = N_("Fuera de límites"),
    };
    return _(N[lie < LIE_KINDS ? lie : 0]);
}

static float carry_now(app_t *a)
{
    return gf_phys_carry(a->club, 1.0f, cur_player(a)->lie);
}

/* The line the caddie would pick: at the pin if the club reaches, otherwise
 * the point at the club's carry that is best placed (fairway, towards the
 * pin, away from trouble). */
static float smart_aim(app_t *a)
{
    gf_player_t *p = cur_player(a);
    const gf_world_t *w = &a->world;
    float to_pin = atan2f(w->pin_x - p->x, w->pin_y - p->y);
    float dist = gf_game_dist_to_pin(w, p->x, p->y);
    float carry = carry_now(a);
    if (a->club == CLUB_PT || carry >= dist * 0.92f) return to_pin;
    static const float PEN[LIE_KINDS] = {
        [LIE_ROUGH] = 22, [LIE_FAIRWAY] = 0, [LIE_FIRST] = 6, [LIE_TEE] = 0, [LIE_FRINGE] = 0,
        [LIE_GREEN] = 0, [LIE_BUNKER] = 40, [LIE_WATER] = 400, [LIE_PATH] = 12, [LIE_DEEP] = 50,
        [LIE_FOREST] = 90, [LIE_WASTE] = 30, [LIE_OB] = 500,
    };
    float best = to_pin, bs = 1e9f;
    for (int k = -18; k <= 18; k++) {
        float ang = to_pin + (float)k * 2.5f * DEG;
        float x = p->x + sinf(ang) * carry, y = p->y + cosf(ang) * carry;
        int lie = gf_lie(w, x, y);
        float s = gf_game_dist_to_pin(w, x, y) + PEN[lie] + fabsf((float)k) * 0.4f;
        if (s < bs) {
            bs = s;
            best = ang;
        }
    }
    return best;
}

/* The view for a shot: the line to the pin points up, and the ball, the pin
 * and the aim marker fit between the HUD bars. */
static void view_for(app_t *a, bool green)
{
    gf_player_t *p = cur_player(a);
    const gf_world_t *w = &a->world;
    float up = atan2f(w->pin_x - p->x, w->pin_y - p->y);
    float fx = sinf(up), fy = cosf(up), rx = cosf(up), ry = -sinf(up);
    float ax = p->x + sinf(a->aim) * a->aim_dist, ay = p->y + cosf(a->aim) * a->aim_dist;
    float pts[3][2] = { { p->x, p->y }, { w->pin_x, w->pin_y }, { ax, ay } };
    float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
    for (int i = 0; i < (green ? 2 : 3); i++) {
        float u = (pts[i][0] - p->x) * rx + (pts[i][1] - p->y) * ry;
        float v = (pts[i][0] - p->x) * fx + (pts[i][1] - p->y) * fy;
        if (u < u0) u0 = u;
        if (u > u1) u1 = u;
        if (v < v0) v0 = v;
        if (v > v1) v1 = v;
    }
    float pad = green ? 2.5f : 14.0f;
    u0 -= pad; u1 += pad; v0 -= pad; v1 += pad;
    float aw = GF_W - 40.0f, ah = (float)(MAP_BOT - MAP_TOP);
    float ppm = ah / (v1 - v0);
    if (aw / (u1 - u0) < ppm) ppm = aw / (u1 - u0);
    ppm = green ? clampf(ppm, 5.0f, 30.0f) : clampf(ppm, 0.7f, 7.0f);
    float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
    float ox = p->x + rx * cu + fx * cv, oy = p->y + ry * cu + fy * cv;
    gf_view_set(&a->view, ox, oy, GF_W / 2.0f, (MAP_TOP + MAP_BOT) / 2.0f, ppm, up);
}

static void draw_traces(app_t *a, uint16_t *buf)
{
    gf_img_t im;
    gf_img_init(&im, buf, GF_W, GF_H);
    static const uint32_t PC[4] = { 0xFFFFFF, 0xFF6060, 0x60C0FF, 0xFFE060 };
    for (int t = 0; t < a->ntrace; t++) {
        const gf_trace_t *tr = &a->trace[t];
        uint16_t c = gf_hex(PC[tr->player & 3]);
        for (int i = 0; i + 1 < tr->n; i++) {
            float x0, y0, x1, y1;
            gf_view_w2s(&a->view, tr->x[i], tr->y[i], &x0, &y0);
            gf_view_w2s(&a->view, tr->x[i + 1], tr->y[i + 1], &x1, &y1);
            gf_line(&im, (int)(x0 * 16), (int)(y0 * 16), (int)(x1 * 16), (int)(y1 * 16), 22, c, 150);
        }
    }
}

static void render_map(app_t *a, bool green)
{
    uint64_t t0 = aos_hal_uptime_ms();
    unsigned fl = MAP_SHADE | MAP_TREES | MAP_MARKERS | (green ? MAP_GRID : 0);
    gf_map_render(&a->world, &a->view, a->mapbuf, GF_W, GF_H, fl);
    draw_traces(a, a->mapbuf);
    aos_hal_log("golf", "map %u ms (%d px/m x100): strips %u = base %u + layers %u + light %u, trees %u",
                (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (int)(a->view.ppm * 100), (unsigned)gf_prof_get(0),
                (unsigned)gf_prof_get(2), (unsigned)gf_prof_get(3), (unsigned)gf_prof_get(4), (unsigned)gf_prof_get(1));
}

static void add_trace(app_t *a, const gf_shot_t *s)
{
    if (a->ntrace >= MAX_TRACE) {
        memmove(&a->trace[0], &a->trace[1], sizeof(a->trace[0]) * (MAX_TRACE - 1));
        a->ntrace--;
    }
    gf_trace_t *t = &a->trace[a->ntrace++];
    t->player = a->game.turn;
    t->n = 0;
    int step = s->n / (TRACE_PTS - 1) + 1;
    for (int i = 0; i < s->n && t->n < TRACE_PTS - 1; i += step) {
        t->x[t->n] = s->trk[i].x;
        t->y[t->n] = s->trk[i].y;
        t->n++;
    }
    if (s->n > 0) {
        t->x[t->n] = s->trk[s->n - 1].x;
        t->y[t->n] = s->trk[s->n - 1].y;
        t->n++;
    }
}

/* --------------------------------------------------------------------------
 * HUD text
 * -------------------------------------------------------------------------- */

static void hud_refresh(app_t *a)
{
    char buf[96], d[24];
    const gf_world_t *w = &a->world;
    gf_player_t *p = cur_player(a);
    snprintf(buf, sizeof buf, "%s %d  ·  %s %d", _("Hoyo"), gf_game_hole(&a->game) + 1, _("Par"), w->def->par);
    lv_label_set_text(a->lbl_hole, buf);
    gf_fmt_dist(a, d, sizeof d, gf_game_dist_to_pin(w, p->x, p->y));
    if (a->game.nplayers > 1) {
        snprintf(buf, sizeof buf, "%s %d  ·  %s %d  ·  %s", _("J"), a->game.turn + 1, _("Golpe"), p->cur + 1, d);
    } else {
        snprintf(buf, sizeof buf, "%s %d  ·  %s", _("Golpe"), p->cur + 1, d);
    }
    lv_label_set_text(a->lbl_info, buf);
    float ws = sqrtf(a->game.wind_x * a->game.wind_x + a->game.wind_y * a->game.wind_y);
    if (a->metres) snprintf(buf, sizeof buf, "%d km/h", (int)(long)roundf(ws * 3.6f));
    else snprintf(buf, sizeof buf, "%d mph", (int)(long)roundf(ws * 2.237f));
    lv_label_set_text(a->lbl_wind, buf);
    lv_label_set_text(a->lbl_lie, lie_name(p->lie));
    lv_label_set_text(a->lbl_club, gf_club(a->club)->name);
    if (a->club == CLUB_PT) {
        lv_label_set_text(a->lbl_carry, _("Putter"));
    } else {
        gf_fmt_dist(a, d, sizeof d, carry_now(a));
        lv_label_set_text(a->lbl_carry, d);
    }
}

/* --------------------------------------------------------------------------
 * The worker: every picture that takes longer than a frame is made here, on
 * the other core, while LVGL keeps drawing. Measured on the board, loading a
 * hole and rendering the 3D view took long enough in LVGL's task to trip the
 * five-second task watchdog. One job at a time plus one waiting; the UI polls
 * for the one that finished (job_poll) and never touches a buffer or the
 * world while a job that writes it is running.
 * -------------------------------------------------------------------------- */

enum { JOB_NONE = 0, JOB_BOOT, JOB_MENU, JOB_HOLE, JOB_MAP, JOB_3D, JOB_OUTFIT };
/* a small queue: the UI can ask for a map, a 3D view and a recolouring in
 * the same frame (a slot of one used to drop the third) */
#define JOBQ 6
static volatile int s_q[JOBQ], s_qh, s_qt, s_running, s_done;
static bool s_worker_ok;
static uint64_t s_last_yield;

static void render_map(app_t *a, bool green);
static void render_3d(app_t *a);
static void build_hole(app_t *a);
static void menu_scene_build(app_t *a);
static void turn_prepare(app_t *a);
static void turn_enter(app_t *a);

static void worker_yield(void)
{
    uint64_t now = aos_hal_uptime_ms();
    if ((uint32_t)(now - s_last_yield) > 60) {
        /* 10 ms is one tick: a 2 ms sleep is vTaskDelay(0) at 100 Hz, which
         * yields to nobody below the worker's priority (IDLE included) */
        aos_hal_worker_sleep(10);
        s_last_yield = aos_hal_uptime_ms();
    }
}

static void run_job(app_t *a, int j)
{
    switch (j) {
    case JOB_BOOT: {
        /* reading the pack is 2.5 s from the card and the calibration ~1 s:
         * both used to be in create(), in LVGL's task, and tripped the
         * watchdog */
        uint64_t t0 = aos_hal_uptime_ms();
        char path[96];
        snprintf(path, sizeof path, "%s/golf.pak", aos_hal_path_apps());
        bool art = gf_art_load(path);
        gf_map_set_tree_art(gf_art_trees());
        uint64_t t1 = aos_hal_uptime_ms();
        gf_phys_init();
        gf_tex_init();
        aos_hal_log("golf", "art %s (%u ms, golfer %s), physics %u ms", art ? "loaded" : "MISSING",
                    (unsigned)(uint32_t)(t1 - t0), gf_art_have_golfer() ? "yes" : "no",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t1));
        break;
    }
    case JOB_MENU: menu_scene_build(a); break;
    case JOB_HOLE: build_hole(a); break;
    case JOB_MAP:  render_map(a, a->job_green); break;
    case JOB_3D:   render_3d(a); break;
    case JOB_OUTFIT: {
        /* the requests pile up in job_mask / job_recolor: take them all */
        uint8_t eq[CAT_N];
        memcpy(eq, a->job_eq, CAT_N);
        unsigned mask = a->job_mask;
        bool recolor = a->job_recolor;
        a->job_mask = 0;
        a->job_recolor = false;
        if (!mask && !recolor) break;       /* an earlier job took this one's */
        uint64_t t0 = aos_hal_uptime_ms();
        if (recolor) gf_art_outfit(eq);
        size_t bytes = gf_art_prepare(mask);
        aos_hal_log("golf", "golfer coloured in %u ms: %u KB of frames",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)(bytes / 1024));
        break;
    }
    default: break;
    }
}

static uint32_t clock_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static void worker_fn(void *arg)
{
    app_t *a = (app_t *)arg;
    gf_set_clock(clock_ms);
    gf_set_yield(worker_yield);
    while (!aos_hal_worker_should_stop()) {
        if (s_qh == s_qt || s_done) {
            /* nothing queued, or the last one not yet collected by the UI */
            aos_hal_worker_sleep(10);
            continue;
        }
        int j = s_q[s_qh];
        s_running = j;
        s_last_yield = aos_hal_uptime_ms();
        run_job(a, j);
        s_qh = (s_qh + 1) % JOBQ;
        s_running = 0;
        s_done = j;
    }
    gf_set_yield(NULL);
}

void gfp_worker_start(app_t *a)
{
    s_qh = s_qt = s_running = s_done = 0;
    s_worker_ok = aos_hal_worker_start("golf", worker_fn, a, 10 * 1024);
    if (!s_worker_ok) aos_hal_log("golf", "no worker: rendering in LVGL's task");
}

void gfp_worker_stop(void)
{
    if (s_worker_ok) aos_hal_worker_stop();
    s_worker_ok = false;
}

static void job_start(app_t *a, int j)
{
    if (!s_worker_ok) {
        run_job(a, j);
        s_done = j;
        return;
    }
    /* the same job already waiting (not running) does it again anyway */
    for (int i = s_qh; i != s_qt; i = (i + 1) % JOBQ) {
        if (s_q[i] == j && !(i == s_qh && s_running)) return;
    }
    int nt = (s_qt + 1) % JOBQ;
    if (nt == s_qh) {
        aos_hal_log("golf", "job queue full, job %d dropped", j);
        return;
    }
    s_q[s_qt] = j;
    s_qt = nt;
}

void gfp_boot(app_t *a)
{
    job_start(a, JOB_BOOT);
}

bool gfp_busy(void)
{
    return s_qh != s_qt || s_running != 0;
}

void gfp_outfit(app_t *a, const uint8_t eq[CAT_N], unsigned mask, bool recolor)
{
    /* Added to what is pending rather than replacing it: the menu's
     * recolouring followed at once by the hole card's cheer used to lose
     * the palette, and the cheer came out black. */
    if (recolor) memcpy(a->job_eq, eq, CAT_N);
    a->job_mask |= mask;
    a->job_recolor |= recolor;
    a->art_busy = true;
    job_start(a, JOB_OUTFIT);
}

void gfp_poll(app_t *a)
{
    int d = s_done;
    if (!d) return;
    s_done = 0;
    switch (d) {
    case JOB_BOOT:
        gfa_booted(a);
        break;
    case JOB_OUTFIT:
        a->art_busy = a->job_mask || a->job_recolor;
        a->art_dirty = true;
        break;
    case JOB_MENU:
        if (a->state == ST_MENU) {
            gfo_set_bg(a, a->v3dbuf);
            a->menu_ready = true;
            gfa_menu_ready(a);
        }
        break;
    case JOB_HOLE:
        if (a->state == ST_LOADING) turn_prepare(a);
        break;
    case JOB_MAP:
        a->map_ready = true;
        if (a->rezoom && !a->pinching) {
            /* the sharp map for the zoom the fingers left */
            a->rezoom = false;
            if (a->state == ST_AIM || a->state == ST_PUTT) gfo_set_bg(a, a->mapbuf);
        }
        break;
    case JOB_3D:
        a->v3d_valid = true;
        a->v3d_aim = a->job_aim;
        if (a->state == ST_SWING && a->render_pending) {
            if (a->v3d_aim != a->aim) {
                /* the line moved while it was drawing: once more */
                a->v3d_valid = false;
                a->job_aim = a->aim;
                job_start(a, JOB_3D);
                break;
            }
            gfo_set_bg(a, a->v3dbuf);
            a->render_pending = false;
            if (!a->game.pl[a->game.turn].remote) {
                lv_label_set_text(a->lbl_hint, _("Tocá: carga, potencia, precisión"));
                lv_obj_remove_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(a->btn_back, LV_OBJ_FLAG_HIDDEN);
            }
        }
        break;
    }
}

/* --------------------------------------------------------------------------
 * The flow
 * -------------------------------------------------------------------------- */

void gfp_round_start(app_t *a)
{
    uint32_t seed = (uint32_t)aos_hal_uptime_ms() * 2654435761U ^ 0x9E3779B9U;
    if (a->mode != MODE_LINK) gf_course_select(a->course);
    if (a->mode == MODE_LINK) {
        seed = a->link_nonce ^ a->link_peer_nonce;      /* both watches agree */
    }
    gf_game_new(&a->game, a->mode, a->diff, a->nplayers, seed, a->practice_hole);
    if (a->mode == MODE_LINK) {
        a->game.pl[1 - a->local_player].remote = 1;
    }
    gfp_hole_start(a);
}

void gfp_hole_start(app_t *a)
{
    const gf_hole_t *h = &gf_course()->holes[gf_game_hole(&a->game)];
    char buf[64], d[24];
    snprintf(buf, sizeof buf, "%s %d / %d", _("Hoyo"), gf_game_hole(&a->game) + 1, gf_course()->nholes);
    lv_label_set_text(a->lbl_load_t, buf);
    float dx = h->pin[0].x - h->tee[a->game.tee_i].x, dy = h->pin[0].y - h->tee[a->game.tee_i].y;
    gf_fmt_dist(a, d, sizeof d, sqrtf(dx * dx + dy * dy));
    snprintf(buf, sizeof buf, "%s\n%s %d  ·  %s", h->name, _("Par"), h->par, d);
    lv_label_set_text(a->lbl_load_s, buf);
    a->ntrace = 0;
    a->map_ready = false;
    gfa_set_state(a, ST_LOADING);
    a->menu_ready = false;
    job_start(a, JOB_HOLE);
}

static void build_hole(app_t *a)
{
    uint64_t t0 = aos_hal_uptime_ms();
    gf_game_hole_start(&a->game, &a->world);
    uint64_t t1 = aos_hal_uptime_ms();
    a->ampp = 0.5f;
    a->atw = (int)((float)a->world.gw / a->ampp);
    a->ath = (int)((float)a->world.gh / a->ampp);
    gf_map_albedo(&a->world, a->albedo, a->atw, a->ath, a->ampp);
    aos_hal_log("golf", "hole %d: world %u ms, texture %dx%d %u ms, %d trees",
                gf_game_hole(&a->game) + 1, (unsigned)(uint32_t)(t1 - t0), a->atw, a->ath,
                (unsigned)(uint32_t)(aos_hal_uptime_ms() - t1), a->world.ntrees);
}

static void set_outfit_for(app_t *a, int player)
{
    if (a->shown_player == player) return;
    a->shown_player = player;
    uint8_t eq[CAT_N];
    if (a->mode == MODE_LINK) {
        if (player == a->local_player) memcpy(eq, a->wr.eq, CAT_N);
        else memcpy(eq, a->partner_eq, CAT_N);
    } else if (player == 0) {
        memcpy(eq, a->wr.eq, CAT_N);
    } else {
        gf_outfit_guest(player, eq);
    }
    gfp_outfit(a, eq, (1u << SEQ_SWING) | (1u << SEQ_IDLE), true);
}

/* Picks who plays and how, and asks the worker for the map. The screen
 * changes in turn_enter(), once the map is there. */
static void turn_prepare(app_t *a)
{
    int who = gf_game_next(&a->game, &a->world);
    a->turn_over = who < 0;
    if (a->turn_over) {
        a->map_ready = true;
        return;
    }
    gf_player_t *p = cur_player(a);
    float dist = gf_game_dist_to_pin(&a->world, p->x, p->y);
    a->putting = p->lie == LIE_GREEN;
    a->club = gf_game_suggest_club(dist, p->lie, a->putting);
    a->aim = smart_aim(a);
    a->aim_dist = a->club == CLUB_PT ? dist : carry_now(a);
    a->meter = MT_IDLE;
    a->mval = 0;
    a->prev_valid = false;
    view_for(a, a->putting);
    a->job_green = a->putting;
    a->map_ready = false;
    job_start(a, JOB_MAP);
}

static void turn_enter(app_t *a)
{
    if (a->turn_over) {
        gfa_set_state(a, ST_HOLE_END);
        return;
    }
    gf_player_t *p = cur_player(a);
    int who = a->game.turn;
    set_outfit_for(a, who);
    gfo_set_bg(a, a->mapbuf);
    hud_refresh(a);
    if (a->mode == MODE_LINK && p->remote) {
        gfa_set_state(a, ST_REMOTE);
        char buf[64];
        snprintf(buf, sizeof buf, "%s %s", _("Juega"), a->partner);
        gfa_banner(a, buf, 0xFFFFFF, 1600);
        return;
    }
    if (a->game.nplayers > 1 && a->mode == MODE_LOCAL) {
        char buf[48];
        snprintf(buf, sizeof buf, "%s %d", _("Turno del jugador"), who + 1);
        gfa_banner(a, buf, 0xFFD60A, 1200);
    }
    gfa_set_state(a, a->putting ? ST_PUTT : ST_AIM);
    if (!a->putting) {
        a->v3d_valid = false;
        a->job_aim = a->aim;
        job_start(a, JOB_3D);
    }
}

/* --------------------------------------------------------------------------
 * Drawing the moving things
 * -------------------------------------------------------------------------- */

static void draw_ball_map(app_t *a, float x, float y, float z, bool pulse)
{
    gf_img_t im = fb_img(a);
    float sx, sy;
    gf_view_w2s(&a->view, x, y, &sx, &sy);
    float r = clampf(0.9f + a->view.ppm * 0.08f, 2.4f, 5.0f);
    float lift = clampf(z * a->view.ppm * 0.55f, 0.0f, 70.0f);
    float br = r * (1.0f + clampf(z / 40.0f, 0.0f, 0.8f));
    /* shadow on the ground */
    gf_shadow(&im, (int)((sx + 1) * 16), (int)((sy + 1) * 16), (int)(r * 1.2f * 16), (int)(r * 0.9f * 16), 150);
    gfo_mark(a, (int)(sx - r * 2), (int)(sy - r * 2), (int)(sx + r * 2 + 2), (int)(sy + r * 2 + 2));
    if (pulse) {
        float ph = (float)(a->st_ms % 1200) / 1200.0f;
        float rr = r + 4 + ph * 10;
        gf_ring(&im, (int)(sx * 16), (int)(sy * 16), (int)(rr * 16), 24, gf_rgb(255, 255, 255), (int)(200 * (1 - ph)));
        gfo_mark(a, (int)(sx - rr - 2), (int)(sy - rr - 2), (int)(sx + rr + 3), (int)(sy + rr + 3));
    }
    gf_ball(&im, (int)(sx * 16), (int)((sy - lift) * 16), (int)(br * 16));
    gfo_mark(a, (int)(sx - br - 2), (int)(sy - lift - br - 2), (int)(sx + br + 3), (int)(sy - lift + br + 3));
}

static void draw_wind(app_t *a)
{
    gf_img_t im = fb_img(a);
    float wx = a->game.wind_x, wy = a->game.wind_y;
    float ws = sqrtf(wx * wx + wy * wy);
    int cx = 336, cy = 92;
    gf_disc(&im, cx * 16, cy * 16, 17 * 16, gf_rgb(0, 0, 0), 110);
    if (ws > 0.2f) {
        /* the wind's direction in screen terms */
        float sx0, sy0, sx1, sy1;
        gf_view_w2s(&a->view, 0, 0, &sx0, &sy0);
        gf_view_w2s(&a->view, wx / ws, wy / ws, &sx1, &sy1);
        float dx = sx1 - sx0, dy = sy1 - sy0, l = sqrtf(dx * dx + dy * dy);
        if (l > 0) { dx /= l; dy /= l; }
        float len = 7 + clampf(ws, 0, 9) * 0.9f;
        float tx = cx + dx * len, ty = cy + dy * len, bx = cx - dx * len, by = cy - dy * len;
        uint16_t c = ws > 6 ? gf_rgb(255, 120, 90) : (ws > 3 ? gf_rgb(255, 214, 10) : gf_rgb(200, 255, 200));
        gf_line(&im, (int)(bx * 16), (int)(by * 16), (int)(tx * 16), (int)(ty * 16), 40, c, 255);
        float hx = -dy, hy = dx;
        gf_line(&im, (int)(tx * 16), (int)(ty * 16), (int)((tx - dx * 6 + hx * 5) * 16), (int)((ty - dy * 6 + hy * 5) * 16), 40, c, 255);
        gf_line(&im, (int)(tx * 16), (int)(ty * 16), (int)((tx - dx * 6 - hx * 5) * 16), (int)((ty - dy * 6 - hy * 5) * 16), 40, c, 255);
    }
    gfo_mark(a, cx - 19, cy - 19, cx + 20, cy + 20);
}

/* Easy mode: the shot played in advance with no mishit, wind included; a
 * putt at the power that would roll the distance on the flat, so the break
 * shows. Recomputed when the line or the club changes. */
static void preview_update(app_t *a)
{
    if (a->diff != DIFF_EASY) return;
    if (a->prev_valid && a->prev_aim == a->aim && a->prev_club == a->club) return;
    static gf_shot_t s;
    gf_player_t *p = cur_player(a);
    memset(&s, 0, sizeof(s));
    s.club = a->club;
    s.aim = a->aim;
    s.wind_x = a->game.wind_x;
    s.wind_y = a->game.wind_y;
    s.x0 = p->x;
    s.y0 = p->y;
    s.lie0 = p->lie;
    if (a->club == CLUB_PT) {
        s.power = gf_putt_power(a->aim_dist) * 1.03f;
    } else {
        float full = gf_phys_carry(a->club, 1.0f, p->lie);
        float d = gf_game_dist_to_pin(&a->world, p->x, p->y);
        s.power = d < full ? d / full : 1.0f;
    }
    gf_phys_shot(&a->world, &s);
    a->prev_n = 0;
    int step = s.n / 31 + 1;
    for (int i = 0; i < s.n && a->prev_n < 31; i += step) {
        a->prev_x[a->prev_n] = s.trk[i].x;
        a->prev_y[a->prev_n] = s.trk[i].y;
        a->prev_n++;
    }
    if (s.n) {
        a->prev_x[a->prev_n] = s.trk[s.n - 1].x;
        a->prev_y[a->prev_n] = s.trk[s.n - 1].y;
        a->prev_n++;
    }
    a->prev_aim = a->aim;
    a->prev_club = a->club;
    a->prev_valid = true;
}

static void draw_preview(app_t *a)
{
    if (a->diff != DIFF_EASY || !a->prev_valid || a->prev_n < 2) return;
    gf_img_t im = fb_img(a);
    uint16_t c = gf_rgb(255, 230, 80);
    for (int i = 0; i + 1 < a->prev_n; i++) {
        float x0, y0, x1, y1;
        gf_view_w2s(&a->view, a->prev_x[i], a->prev_y[i], &x0, &y0);
        gf_view_w2s(&a->view, a->prev_x[i + 1], a->prev_y[i + 1], &x1, &y1);
        if (i & 1) continue;            /* dotted */
        gf_line(&im, (int)(x0 * 16), (int)(y0 * 16), (int)(x1 * 16), (int)(y1 * 16), 22, c, 200);
        gfo_mark(a, (int)(x0 < x1 ? x0 : x1) - 2, (int)(y0 < y1 ? y0 : y1) - 2,
                 (int)(x0 > x1 ? x0 : x1) + 3, (int)(y0 > y1 ? y0 : y1) + 3);
    }
    float ex, ey;
    gf_view_w2s(&a->view, a->prev_x[a->prev_n - 1], a->prev_y[a->prev_n - 1], &ex, &ey);
    gf_disc(&im, (int)(ex * 16), (int)(ey * 16), 56, c, 230);
    gfo_mark(a, (int)ex - 5, (int)ey - 5, (int)ex + 6, (int)ey + 6);
}

/* the aim line, the landing ring and, on easy, where it will really go */
static void draw_aim(app_t *a)
{
    gf_img_t im = fb_img(a);
    gf_player_t *p = cur_player(a);
    float bx, by, ex, ey;
    gf_view_w2s(&a->view, p->x, p->y, &bx, &by);
    float lx = p->x + sinf(a->aim) * a->aim_dist, ly = p->y + cosf(a->aim) * a->aim_dist;
    gf_view_w2s(&a->view, lx, ly, &ex, &ey);
    /* dashes */
    float dx = ex - bx, dy = ey - by, l = sqrtf(dx * dx + dy * dy);
    int nd = (int)(l / 9);
    for (int i = 1; i < nd; i += 2) {
        float t0 = (float)i / nd, t1 = (float)(i + 1) / nd;
        gf_line(&im, (int)((bx + dx * t0) * 16), (int)((by + dy * t0) * 16), (int)((bx + dx * t1) * 16),
                (int)((by + dy * t1) * 16), 30, gf_rgb(255, 255, 255), 220);
    }
    float x0 = bx < ex ? bx : ex, x1 = bx > ex ? bx : ex, y0 = by < ey ? by : ey, y1 = by > ey ? by : ey;
    gfo_mark(a, (int)x0 - 3, (int)y0 - 3, (int)x1 + 4, (int)y1 + 4);
    /* the ring: how far off a slightly mishit ball lands */
    float rr = a->club == CLUB_PT ? 5.0f : clampf(a->aim_dist * 0.05f * a->view.ppm, 6.0f, 40.0f);
    gf_ring(&im, (int)(ex * 16), (int)(ey * 16), (int)(rr * 16), 28, gf_rgb(255, 255, 255), 230);
    gf_disc(&im, (int)(ex * 16), (int)(ey * 16), 40, gf_rgb(255, 255, 255), 255);
    gfo_mark(a, (int)(ex - rr - 3), (int)(ey - rr - 3), (int)(ex + rr + 4), (int)(ey + rr + 4));
}

/* --------------------------------------------------------------------------
 * AIM and PUTT
 * -------------------------------------------------------------------------- */

static void aim_to(app_t *a, int sx, int sy)
{
    gf_player_t *p = cur_player(a);
    float wx, wy;
    gf_view_s2w(&a->view, (float)sx, (float)sy, &wx, &wy);
    float dx = wx - p->x, dy = wy - p->y;
    if (dx * dx + dy * dy < 0.01f) return;
    float na = atan2f(dx, dy);
    if (na != a->aim) {
        a->aim = na;
        a->v3d_valid = false;
        a->aim_changed_ms = (uint32_t)aos_hal_uptime_ms();
    }
    if (a->club == CLUB_PT) {
        a->aim_dist = sqrtf(dx * dx + dy * dy);
    }
}

void gfp_club_step(app_t *a, int d)
{
    gf_player_t *p = cur_player(a);
    int c = a->club;
    for (int k = 0; k < CLUB_N; k++) {
        c = (c + d + CLUB_N) % CLUB_N;
        if (c == CLUB_DR && p->lie != LIE_TEE) continue;
        if (c == CLUB_PT && p->lie != LIE_GREEN && p->lie != LIE_FRINGE && p->lie != LIE_FAIRWAY) continue;
        break;
    }
    a->club = c;
    if (c == CLUB_PT) a->aim_dist = gf_game_dist_to_pin(&a->world, p->x, p->y);
    else a->aim_dist = carry_now(a);
    hud_refresh(a);
    gf_sfx(900, 15);
}

static void enter_swing(app_t *a)
{
    gfa_set_state(a, ST_SWING);
    if (a->v3d_valid && a->v3d_aim == a->aim) {
        /* drawn ahead while aiming: no wait at all */
        a->render_pending = false;
        gf_cam_swing(&a->cam, &a->world, cur_player(a)->x, cur_player(a)->y, a->aim);
        gfo_set_bg(a, a->v3dbuf);
        if (!cur_player(a)->remote) {
            lv_label_set_text(a->lbl_hint, _("Tocá: carga, potencia, precisión"));
            lv_obj_remove_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(a->btn_back, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        a->render_pending = true;
        lv_label_set_text(a->lbl_hint, "...");
        lv_obj_remove_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
        if (!(gfp_busy() && a->job_aim == a->aim)) {
            a->v3d_valid = false;
            a->job_aim = a->aim;
            job_start(a, JOB_3D);
        }
    }
    a->meter = MT_IDLE;
    a->mval = 0;
    a->anim = 0;
}

void gfp_hit_pressed(app_t *a)
{
    if (a->state == ST_AIM) {
        if (a->club == CLUB_PT) {
            /* a putt from off the green: the meter on the map */
            a->meter = MT_UP;
            a->mval = 0;
            gfa_set_state(a, ST_PUTT);
            a->meter = MT_UP;
        } else {
            enter_swing(a);
        }
    } else if (a->state == ST_PUTT) {
        if (a->club != CLUB_PT) {
            enter_swing(a);
        } else if (a->meter == MT_IDLE) {
            a->meter = MT_UP;
            a->mval = 0;
            gf_sfx(700, 20);
        }
    }
}

void gfp_touch(app_t *a, int x, int y, int ev)
{
    switch (a->state) {
    case ST_AIM:
    case ST_PUTT:
        if (a->meter != MT_IDLE) {
            if (ev == 0) {
                /* the putt: the tap sets the power */
                a->power = clampf(a->mval, 0.02f, 1.0f);
                a->acc = 0;
                a->meter = MT_SWING;
            }
            return;
        }
        if (y < MAP_TOP - 10 || y > MAP_BOT + 6) return;
        aim_to(a, x, y);
        break;
    case ST_SWING:
        if (ev != 0 || a->render_pending) return;
        if (a->meter == MT_IDLE) {
            a->meter = MT_UP;
            a->mval = 0;
            gf_sfx(600, 15);
            lv_obj_add_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(a->btn_back, LV_OBJ_FLAG_HIDDEN);
        } else if (a->meter == MT_UP) {
            a->power = clampf(a->mval, 0.05f, MTR_MAX);
            a->meter = MT_DOWN;
            gf_sfx(800, 15);
        } else if (a->meter == MT_DOWN) {
            float e = a->mval;
            float wdt = SWEET_W[a->diff];
            float acc = clampf(-e / wdt * 0.5f, -1.0f, 1.0f);
            if (a->power > 1.0f) acc *= 1.0f + (a->power - 1.0f) * 9.0f;
            a->acc = clampf(acc, -1.3f, 1.3f);
            a->meter = MT_SWING;
            a->anim = GF_SWING_TOP + 1;
            gf_sfx(fabsf(e) < wdt * 0.25f ? 1400 : 1000, 20);
            gf_sound(SND_WHOOSH);
        }
        break;
    case ST_FLIGHT:
    case ST_ROLL:
        if (ev == 0) a->skip = true;
        break;
    case ST_RESULT:
        if (ev == 0 && a->st_ms > 400) a->result_ms = 0;
        break;
    default:
        break;
    }
}

bool gfp_back(app_t *a)
{
    if (a->state == ST_SWING && a->meter == MT_IDLE) {
        gfa_set_state(a, a->putting ? ST_PUTT : ST_AIM);
        gfo_set_bg(a, a->mapbuf);
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Two fingers on the map (v0.6.0)
 *
 * The map takes a few hundred ms to render (textures, light, trees), far
 * too slow to follow a pinch. So while the fingers move, what is shown is
 * the map already rendered, stretched by hand (nearest pixel: fast, and a
 * moment of blockiness is honest about being a preview), and a->view is
 * moved along with it so the marker, the line and the ball stay where they
 * belong. When the fingers lift, the map is rendered again for the new view
 * and swapped in when it is ready (JOB_MAP, gfp_poll).
 * -------------------------------------------------------------------------- */

#define ZOOM_PREVIEW_MS 30

static void zoom_view(app_t *a, gf_view_t *out)
{
    /* the new screen centre shows what was at c + (S - c - d) / k before */
    float sx = a->zcx + (a->view0.scx - a->zcx - a->zdx) / a->zk;
    float sy = a->zcy + (a->view0.scy - a->zcy - a->zdy) / a->zk;
    float wx, wy;
    gf_view_s2w(&a->view0, sx, sy, &wx, &wy);
    gf_view_set(out, wx, wy, a->view0.scx, a->view0.scy, a->view0.ppm * a->zk, a->view0.ang);
}

static void zoom_preview(app_t *a)
{
    static int16_t col[GF_W];
    for (int x = 0; x < GF_W; x++) {
        col[x] = (int16_t)floorf(a->zcx + ((float)x - a->zcx - a->zdx) / a->zk);
    }
    const uint16_t bgc = gf_hex(0x10301A);
    for (int y = 0; y < GF_H; y++) {
        int sy = (int)floorf(a->zcy + ((float)y - a->zcy - a->zdy) / a->zk);
        uint16_t *d = a->zoombuf + (size_t)y * GF_W;
        if (sy < 0 || sy >= GF_H) {
            for (int x = 0; x < GF_W; x++) d[x] = bgc;
            continue;
        }
        const uint16_t *s = a->mapbuf + (size_t)sy * GF_W;
        for (int x = 0; x < GF_W; x++) {
            int sx = col[x];
            d[x] = (sx >= 0 && sx < GF_W) ? s[sx] : bgc;
        }
    }
    zoom_view(a, &a->view);
    gfo_set_bg(a, a->zoombuf);
}

void gfp_pinch(app_t *a, const aos_gesture_event_t *ev, int ox, int oy)
{
    bool map_up = (a->state == ST_AIM || a->state == ST_PUTT) && a->meter == MT_IDLE;
    switch (ev->type) {
    case AOS_GESTURE_PINCH_BEGIN:
        /* not while the map is being rendered: the preview reads it */
        if (!map_up || !a->map_ready || gfp_busy()) return;
        if (!a->zoombuf) a->zoombuf = (uint16_t *)malloc((size_t)GF_W * GF_H * 2);
        if (!a->zoombuf) return;
        a->pinching = true;
        if (a->aim != a->aim_pre) {         /* the first finger aimed: undo */
            a->aim = a->aim_pre;
            a->v3d_valid = false;
        }
        a->view0 = a->view;
        a->zk = 1.0f;
        a->zdx = a->zdy = 0;
        a->zcx = ev->x - ox;
        a->zcy = ev->y - oy;
        a->zoom_ms = 0;
        break;
    case AOS_GESTURE_PINCH: {
        if (!a->pinching) return;
        bool green = a->putting;
        float lo = green ? 3.0f : 0.5f, hi = green ? 40.0f : 12.0f;
        float k = a->zk * ev->scale;
        if (a->view0.ppm * k < lo) k = lo / a->view0.ppm;
        if (a->view0.ppm * k > hi) k = hi / a->view0.ppm;
        a->zk = k;
        a->zdx += ev->dx;
        a->zdy += ev->dy;
        uint32_t now = (uint32_t)aos_hal_uptime_ms();
        if (now - a->zoom_ms >= ZOOM_PREVIEW_MS) {
            a->zoom_ms = now;
            zoom_preview(a);
        }
        break;
    }
    case AOS_GESTURE_PINCH_END:
        if (!a->pinching) return;
        a->pinching = false;
        zoom_preview(a);                    /* the last position, exactly */
        zoom_view(a, &a->view);
        a->rezoom = true;
        a->job_green = a->putting;
        job_start(a, JOB_MAP);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The shot
 * -------------------------------------------------------------------------- */

static void do_shot(app_t *a)
{
    gf_player_t *p = cur_player(a);
    gf_shot_t *s = &a->shot;
    s->club = a->club;
    s->aim = a->aim;
    s->power = a->power;
    s->acc = a->acc;
    s->wind_x = a->game.wind_x;
    s->wind_y = a->game.wind_y;
    s->x0 = p->x;
    s->y0 = p->y;
    s->lie0 = p->lie;
    gf_phys_shot(&a->world, s);
    a->play = 0;
    a->skip = false;
    if (a->mode == MODE_LINK && !p->remote) {
        gfl_send_shot(a, a->club, a->aim, a->power, a->acc);
    }
}

static void finish_shot(app_t *a)
{
    gf_shot_t *s = &a->shot;
    gf_player_t *p = cur_player(a);
    int par = a->world.def->par;
    if (a->remote_check) {
        /* the same binary runs the same physics: the ball must stop where it
         * stopped on the other watch */
        a->remote_check = false;
        float dx = s->x - a->remote_x, dy = s->y - a->remote_y;
        if (dx * dx + dy * dy > 0.0001f) {
            aos_hal_log("golf", "link: ball at %d,%d cm here, %d,%d there", (int)(s->x * 100), (int)(s->y * 100),
                        (int)(a->remote_x * 100), (int)(a->remote_y * 100));
        }
    }
    add_trace(a, s);
    gf_game_apply(&a->game, &a->world, s);
    char d[24];
    switch (s->result) {
    case RES_HOLED: {
        const char *nm = gf_score_name(p->cur, par);
        snprintf(a->result_txt, sizeof a->result_txt, "%s", _(nm));
        if (p->cur <= par - 1) gf_sound(SND_APPLAUSE);
        break;
    }
    case RES_WATER:
        snprintf(a->result_txt, sizeof a->result_txt, "%s", _("¡Al agua! +1"));
        gf_sound(SND_BAD);
        break;
    case RES_OB:
        snprintf(a->result_txt, sizeof a->result_txt, "%s", _("Fuera de límites +1"));
        gf_sound(SND_BAD);
        break;
    default:
        if (s->club == CLUB_PT || p->lie == LIE_GREEN) {
            gf_fmt_dist(a, d, sizeof d, gf_game_dist_to_pin(&a->world, p->x, p->y));
            snprintf(a->result_txt, sizeof a->result_txt, "%s  ·  %s", lie_name(p->lie), d);
        } else {
            gf_fmt_dist(a, d, sizeof d, s->total);
            snprintf(a->result_txt, sizeof a->result_txt, "%s  ·  %s", lie_name(p->lie), d);
        }
        break;
    }
    if (p->picked) {
        snprintf(a->result_txt, sizeof a->result_txt, "%s", _("Recogida"));
    }
    uint32_t col = s->result == RES_HOLED ? 0xFFD60A : (s->result == RES_OK ? 0xFFFFFF : 0xFF6A5A);
    gfa_banner(a, a->result_txt, col, 1800);
    a->result_ms = 1800;
    gfa_set_state(a, ST_RESULT);
    /* the next map is drawn while the banner is up */
    turn_prepare(a);
}

/* --------------------------------------------------------------------------
 * The 3D view
 * -------------------------------------------------------------------------- */

static void render_3d(app_t *a)
{
    gf_player_t *p = cur_player(a);
    uint64_t t0 = aos_hal_uptime_ms();
    gf_cam_swing(&a->cam, &a->world, p->x, p->y, a->job_aim);
    gf_albedo_t alb = { a->albedo, a->atw, a->ath, a->ampp };
    gf_view3d_render(&a->world, &a->cam, &alb, a->v3dbuf, a->depth);
    aos_hal_log("golf", "3D view %u ms: ground %u (%u samples, %u seen), sky %u, trees %u",
                (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)gf_prof_get(0), (unsigned)gf_prof_get(5),
                (unsigned)gf_prof_get(6), (unsigned)gf_prof_get(1), (unsigned)gf_prof_get(2));
}

/* the target, on the ground of the 3D view: an overlay, so a change of club
 * does not need a new render */
static void draw_target_3d(app_t *a)
{
    gf_player_t *p = cur_player(a);
    float lx = p->x + sinf(a->aim) * a->aim_dist, ly = p->y + cosf(a->aim) * a->aim_dist;
    float sx, sy, zc;
    if (!gf_cam_project(&a->cam, lx, ly, gf_height(&a->world, lx, ly), &sx, &sy, &zc)) return;
    gf_img_t im = fb_img(a);
    float r = clampf(3.5f * a->cam.f / zc, 4.0f, 30.0f);
    for (int k = 0; k < 2; k++) {
        float rr = r * (k ? 0.45f : 1.0f);
        for (int s2 = 0; s2 < 32; s2++) {
            float t0a = s2 / 32.0f * 2 * PI, t1a = (s2 + 1) / 32.0f * 2 * PI;
            gf_line(&im, (int)((sx + cosf(t0a) * rr) * 16), (int)((sy + sinf(t0a) * rr * 0.32f) * 16),
                    (int)((sx + cosf(t1a) * rr) * 16), (int)((sy + sinf(t1a) * rr * 0.32f) * 16),
                    20, gf_rgb(255, 255, 255), 200);
        }
    }
    gfo_mark(a, (int)(sx - r - 2), (int)(sy - r * 0.32f - 2), (int)(sx + r + 3), (int)(sy + r * 0.32f + 3));
}

static void draw_golfer(app_t *a, int seq, int frame)
{
    if (a->art_busy && !(gf_art_ready() & (1u << seq))) return;
    int x0, y0;
    gf_img_t im = fb_img(a);
    const gf_sprite_t *sh = gf_art_golfer_shadow(seq, frame, &x0, &y0);
    if (sh) {
        for (int y = 0; y < sh->h; y++) {
            int Y = y + y0;
            if (Y < 0 || Y >= GF_H) continue;
            for (int x = 0; x < sh->w; x++) {
                int X = x + x0;
                if (X < 0 || X >= GF_W) continue;
                int s = sh->a[y * sh->w + x];
                if (s) a->fb[Y * GF_W + X] = gf_scale(a->fb[Y * GF_W + X], 256 - (s * 150 >> 8));
            }
        }
        gfo_mark(a, x0, y0, x0 + sh->w, y0 + sh->h);
    }
    const gf_sprite_t *s = gf_art_golfer(seq, frame, &x0, &y0);
    if (!s) return;
    gf_sprite(&im, s, x0, y0);
    gfo_mark(a, x0, y0, x0 + s->w, y0 + s->h);
}

static void draw_meter(app_t *a)
{
    gf_img_t im = fb_img(a);
    int top = MTR_ZERO - (int)(MTR_MAX * MTR_K), bot = MTR_ZERO - (int)(MTR_MIN * MTR_K);
    gf_rrect(&im, MTR_X - 4, top - 4, MTR_W + 8, bot - top + 8, 10, gf_rgb(10, 12, 16), 170);
    /* the scale: overswing zone on top, the sweet spot at the bottom */
    int y100 = MTR_ZERO - (int)(1.0f * MTR_K);
    gf_rect_blend(&im, MTR_X, top, MTR_W, y100 - top, gf_rgb(200, 40, 40), 120);
    float wdt = SWEET_W[a->diff];
    int sw0 = MTR_ZERO - (int)(wdt * MTR_K), sw1 = MTR_ZERO + (int)(wdt * MTR_K);
    gf_rect_blend(&im, MTR_X, sw0, MTR_W, sw1 - sw0, gf_rgb(60, 220, 110), 110);
    for (int q = 1; q < 4; q++) {
        int y = MTR_ZERO - (int)(q * 0.25f * MTR_K);
        gf_rect_blend(&im, MTR_X, y, MTR_W, 1, gf_rgb(255, 255, 255), 90);
    }
    /* the power, filled from the line up */
    float pv = a->meter == MT_UP ? a->mval : (a->meter == MT_IDLE ? 0 : a->power);
    int yp = MTR_ZERO - (int)(pv * MTR_K);
    for (int y = yp; y < MTR_ZERO; y++) {
        float t = (float)(MTR_ZERO - y) / MTR_K;
        int r = t < 0.5f ? (int)(80 + 350 * t) : 255, g = t < 0.7f ? 220 : (int)(220 - (t - 0.7f) * 500);
        if (g < 40) g = 40;
        gf_rect_blend(&im, MTR_X + 3, y, MTR_W - 6, 1, gf_rgb(r > 255 ? 255 : r, g, 60), 230);
    }
    gf_rect(&im, MTR_X - 2, MTR_ZERO, MTR_W + 4, 2, gf_rgb(255, 255, 255));
    /* the moving marker */
    if (a->meter == MT_UP || a->meter == MT_DOWN) {
        int ym = MTR_ZERO - (int)(a->mval * MTR_K);
        gf_rect(&im, MTR_X - 6, ym - 1, MTR_W + 12, 3, gf_rgb(255, 255, 255));
        gf_rect(&im, MTR_X - 6, ym - 1, MTR_W + 12, 1, gf_rgb(40, 40, 40));
    }
    gfo_mark(a, MTR_X - 8, top - 6, MTR_X + MTR_W + 8, bot + 6);
}

static int swing_frames(void)
{
    int n = gf_art_frames(SEQ_SWING);
    return n > 0 ? n : 24;
}

static void swing_frame(app_t *a, int dt)
{
    if (a->render_pending) {
        return;                 /* the worker is drawing the 3D view */
    }
    float spd = METER_SPEED[a->diff];
    int nsw = swing_frames();
    int frame = 0, seq = SEQ_SWING;
    bool after = false;

    switch (a->meter) {
    case MT_IDLE: {
        if (cur_player(a)->remote && a->st_ms > 700) {
            /* the other watch's swing plays by itself */
            a->meter = MT_SWING;
            a->anim = 0;
            gf_sound(SND_WHOOSH);
        }
        int ni = gf_art_frames(SEQ_IDLE);
        if (ni > 0) {
            int t = (int)(a->st_ms / 140) % (ni * 2 - 2 > 0 ? ni * 2 - 2 : 1);
            frame = t < ni ? t : ni * 2 - 2 - t;
            seq = SEQ_IDLE;
        }
        break;
    }
    case MT_UP:
        a->mval += spd * (float)dt / 1000.0f;
        if (a->mval >= MTR_MAX) {
            a->mval = MTR_MAX;
            a->power = MTR_MAX;
            a->meter = MT_DOWN;
        }
        frame = (int)(clampf(a->mval, 0, 1) * GF_SWING_TOP + 0.5f);
        break;
    case MT_DOWN:
        a->mval -= spd * 1.25f * (float)dt / 1000.0f;
        frame = GF_SWING_TOP;
        if (a->mval < MTR_MIN) {
            /* never tapped: a shank */
            a->acc = 1.3f;
            a->meter = MT_SWING;
            a->anim = GF_SWING_TOP + 1;
        }
        break;
    case MT_SWING:
        a->anim += (float)dt / 45.0f;
        frame = (int)a->anim;
        if (frame >= GF_SWING_IMPACT) {
            frame = GF_SWING_IMPACT;
            do_shot(a);
            gf_sound(a->shot.club <= CLUB_5W ? SND_IMPACT_WOOD : SND_IMPACT);
            a->meter = MT_FOLLOW;
            a->hit_ms = 0;
        }
        break;
    case MT_FOLLOW:
        a->hit_ms += dt;
        a->anim = GF_SWING_IMPACT + (float)a->hit_ms / 70.0f;
        frame = (int)a->anim;
        if (frame >= nsw) frame = nsw - 1;
        after = true;
        break;
    }

    gfo_begin(a);
    /* the ball: under the golfer before the hit, on top after */
    gf_player_t *p = cur_player(a);
    float bsx, bsy, zc;
    gf_img_t im = fb_img(a);
    if (!after) {
        if (gf_cam_project(&a->cam, p->x, p->y, gf_height(&a->world, p->x, p->y) + 0.021f, &bsx, &bsy, &zc)) {
            float r = clampf(0.0214f * a->cam.f / zc, 1.6f, 6.0f) + 0.6f;
            gf_ball(&im, (int)(bsx * 16), (int)(bsy * 16), (int)(r * 16));
            gfo_mark(a, (int)(bsx - r - 2), (int)(bsy - r - 2), (int)(bsx + r + 3), (int)(bsy + r + 3));
        }
    }
    if (!after) draw_target_3d(a);
    draw_golfer(a, seq, frame);
    if (after) {
        /* the first second and a half of the flight, with a tracer */
        float idx = (float)a->hit_ms / 1000.0f * GF_TRK_HZ;
        int n = (int)idx;
        if (n >= a->shot.n) n = a->shot.n - 1;
        float lx = -1, ly = -1;
        for (int i = 0; i <= n; i++) {
            const gf_trk_t *t = &a->shot.trk[i];
            float sx, sy;
            if (!gf_cam_project(&a->cam, t->x, t->y, t->z + 0.02f, &sx, &sy, &zc)) continue;
            if (lx >= 0) {
                gf_line(&im, (int)(lx * 16), (int)(ly * 16), (int)(sx * 16), (int)(sy * 16), 24, gf_rgb(255, 80, 60), 200);
                gfo_mark(a, (int)(lx < sx ? lx : sx) - 2, (int)(ly < sy ? ly : sy) - 2, (int)(lx > sx ? lx : sx) + 3, (int)(ly > sy ? ly : sy) + 3);
            }
            lx = sx;
            ly = sy;
        }
        if (lx >= 0) {
            float r = clampf(0.0214f * a->cam.f / zc, 1.2f, 6.0f) + 0.6f;
            gf_ball(&im, (int)(lx * 16), (int)(ly * 16), (int)(r * 16));
            gfo_mark(a, (int)(lx - r - 2), (int)(ly - r - 2), (int)(lx + r + 3), (int)(ly + r + 3));
        }
        if (a->hit_ms > 1700 || n >= a->shot.n - 1) {
            gfo_end(a);
            gfo_set_bg(a, a->mapbuf);
            gfa_set_state(a, ST_FLIGHT);
            return;
        }
    }
    if (!cur_player(a)->remote && a->meter != MT_FOLLOW) draw_meter(a);
    gfo_end(a);
}

/* --------------------------------------------------------------------------
 * The ball on the map
 * -------------------------------------------------------------------------- */

static void flight_frame(app_t *a, int dt)
{
    const gf_shot_t *s = &a->shot;
    int prev = (int)a->play;
    const gf_trk_t *cur = &s->trk[prev < s->n ? prev : s->n - 1];
    float speed = cur->ev == TK_ROLL ? 1.25f : 1.7f;
    if (a->state == ST_ROLL) speed = 1.0f;
    a->play += speed * (float)dt * GF_TRK_HZ / 1000.0f;
    if (a->skip) a->play = (float)(s->n - 1);
    int n = (int)a->play;
    if (n >= s->n - 1) n = s->n - 1;

    /* the tracer goes into the background, so it stays */
    gf_img_t bg;
    gf_img_init(&bg, a->mapbuf, GF_W, GF_H);
    gf_img_t im = fb_img(a);
    for (int i = prev; i < n; i++) {
        float x0, y0, x1, y1;
        gf_view_w2s(&a->view, s->trk[i].x, s->trk[i].y, &x0, &y0);
        gf_view_w2s(&a->view, s->trk[i + 1].x, s->trk[i + 1].y, &x1, &y1);
        gf_line(&bg, (int)(x0 * 16), (int)(y0 * 16), (int)(x1 * 16), (int)(y1 * 16), 22, gf_rgb(255, 255, 255), 150);
        gf_line(&im, (int)(x0 * 16), (int)(y0 * 16), (int)(x1 * 16), (int)(y1 * 16), 22, gf_rgb(255, 255, 255), 150);
        gfo_mark(a, (int)(x0 < x1 ? x0 : x1) - 2, (int)(y0 < y1 ? y0 : y1) - 2, (int)(x0 > x1 ? x0 : x1) + 3, (int)(y0 > y1 ? y0 : y1) + 3);
        int ev = s->trk[i + 1].ev;
        if (ev == TK_BOUNCE) gf_sound(SND_BOUNCE);
        if (ev == TK_TREE) gf_sound(SND_TREE);
        if (ev == TK_SPLASH) gf_sound(SND_SPLASH);
        if (ev == TK_CUP) gf_sound(SND_CUP);
        if (ev == TK_LIP) gf_sfx(1300, 20);
    }
    /* the dirty list from the tracer must survive the restore: push now */
    gf_dirty_t keep = a->dcur;
    gfo_begin(a);
    for (int i = 0; i < keep.n; i++) gf_dirty_add(&a->dcur, keep.r[i].x0, keep.r[i].y0, keep.r[i].x1, keep.r[i].y1);

    const gf_trk_t *t = &s->trk[n];
    float gz = gf_height(&a->world, t->x, t->y);
    bool in_cup = t->ev == TK_CUP;
    if (!in_cup) draw_ball_map(a, t->x, t->y, t->z - gz, false);
    gfo_end(a);

    if (n >= s->n - 1) {
        finish_shot(a);
    }
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */

static void putt_meter_frame(app_t *a, int dt)
{
    /* ping-pong 0..1 until tapped */
    static int dir = 1;
    float spd = METER_SPEED[a->diff] * 0.8f;
    a->mval += (float)dir * spd * (float)dt / 1000.0f;
    if (a->mval > 1.0f) { a->mval = 1.0f; dir = -1; }
    if (a->mval < 0.0f) { a->mval = 0.0f; dir = 1; }
    if (a->meter == MT_UP && dir < 0 && a->mval <= 0.0f) dir = 1;
}

/* --------------------------------------------------------------------------
 * The bot (GF_AUTO=1 in the simulator, and the board's benchmark): taps the
 * way a decent player would, so the whole round plays by itself.
 * -------------------------------------------------------------------------- */

static void autoplay(app_t *a)
{
    gf_player_t *p = cur_player(a);
    switch (a->state) {
    case ST_AIM:
        if (a->st_ms > 900) gfp_hit_pressed(a);
        break;
    case ST_PUTT:
        if (a->club != CLUB_PT) {
            if (a->st_ms > 900) gfp_hit_pressed(a);
        } else if (a->meter == MT_IDLE && a->st_ms > 900) {
            gfp_hit_pressed(a);
        } else if (a->meter == MT_UP) {
            float want = gf_putt_power(gf_game_dist_to_pin(&a->world, p->x, p->y)) * 1.04f + 0.01f;
            if (a->mval >= want) gfp_touch(a, 180, 200, 0);
        }
        break;
    case ST_SWING:
        if (a->render_pending || p->remote) break;
        if (a->meter == MT_IDLE && a->st_ms > 600) {
            gfp_touch(a, 180, 200, 0);
        } else if (a->meter == MT_UP) {
            float full = gf_phys_carry(a->club, 1.0f, p->lie);
            float d = gf_game_dist_to_pin(&a->world, p->x, p->y);
            float want = d > full ? 1.0f : d / full;
            if (a->mval >= want) gfp_touch(a, 180, 200, 0);
        } else if (a->meter == MT_DOWN && a->mval <= 0.01f) {
            gfp_touch(a, 180, 200, 0);
        }
        break;
    default:
        break;
    }
}

void gfp_frame(app_t *a, int dt)
{
    gfp_poll(a);
    if (a->autoplay) autoplay(a);
    switch (a->state) {
    case ST_LOADING:
        if (a->map_ready && a->st_ms > 700 && !a->link_on_lobby) turn_enter(a);
        return;
    case ST_AIM:
    case ST_PUTT: {
        if (a->state == ST_AIM && !a->v3d_valid && !gfp_busy() && a->club != CLUB_PT &&
            (uint32_t)aos_hal_uptime_ms() - a->aim_changed_ms > 400) {
            a->job_aim = a->aim;
            job_start(a, JOB_3D);
        }
        if ((a->state == ST_PUTT || a->club == CLUB_PT) && a->meter == MT_UP) {
            putt_meter_frame(a, dt);
        }
        if (a->meter == MT_SWING) {
            /* the putt goes */
            do_shot(a);
            gf_sfx(900, 15);
            a->meter = MT_IDLE;
            gfa_set_state(a, ST_ROLL);
            return;
        }
        gfo_begin(a);
        gf_player_t *me = cur_player(a);
        for (int i = 0; i < a->game.nplayers; i++) {
            gf_player_t *o = &a->game.pl[i];
            if (o == me || o->done) continue;
            float sx, sy;
            gf_view_w2s(&a->view, o->x, o->y, &sx, &sy);
            gf_img_t im = fb_img(a);
            static const uint32_t PC[4] = { 0xFFFFFF, 0xFF6060, 0x60C0FF, 0xFFE060 };
            gf_disc(&im, (int)(sx * 16), (int)(sy * 16), 48, gf_hex(PC[i & 3]), 255);
            gfo_mark(a, (int)sx - 4, (int)sy - 4, (int)sx + 5, (int)sy + 5);
        }
        preview_update(a);
        draw_preview(a);
        draw_aim(a);
        draw_ball_map(a, me->x, me->y, 0, true);
        if (a->state == ST_AIM) draw_wind(a);
        if (a->meter == MT_UP) {
            /* the putt meter reuses the swing meter's drawing */
            float keep = a->power;
            a->power = a->mval;
            draw_meter(a);
            a->power = keep;
        }
        gfo_end(a);
        break;
    }
    case ST_SWING:
        swing_frame(a, dt);
        break;
    case ST_FLIGHT:
    case ST_ROLL:
        flight_frame(a, dt);
        break;
    case ST_RESULT:
        a->result_ms -= dt;
        if (a->result_ms <= 0 && a->map_ready) turn_enter(a);
        break;
    case ST_REMOTE:
        if (a->remote_shot_ready) {
            a->remote_shot_ready = false;
            enter_swing(a);
        }
        break;
    default:
        break;
    }
}

void gfp_apply_remote_shot(app_t *a, int club, float aim, float power, float acc)
{
    a->club = club;
    a->aim = aim;
    a->power = power;
    a->acc = acc;
    a->aim_dist = club == CLUB_PT ? gf_game_dist_to_pin(&a->world, cur_player(a)->x, cur_player(a)->y)
                                  : gf_phys_carry(club, 1.0f, cur_player(a)->lie);
    if (club == CLUB_PT) {
        do_shot(a);
        gfa_set_state(a, ST_ROLL);
        return;
    }
    a->remote_shot_ready = true;
}

/* --------------------------------------------------------------------------
 * The menu's scene: the first hole from the tee, and the golfer waiting
 * -------------------------------------------------------------------------- */

/* The menu's background is the same picture every time for a course, and
 * rendering it is ~4.5 s on the board: the first time it is saved next to
 * the pack, and from then on it is read back (~0.3 s). The version goes up
 * whenever the renderers change what the picture looks like. */
#define MENU_CACHE_VER  3

typedef struct {
    char     magic[4];
    uint16_t ver;
    uint8_t  course, pad;
    int16_t  ball_x16, ball_y16;
} menu_cache_t;

static void menu_cache_path(const app_t *a, char *buf, int n)
{
    snprintf(buf, (size_t)n, "%s/golf_menu%d.bin", aos_hal_path_apps(), a->course);
}

static bool menu_cache_load(app_t *a)
{
    char path[112];
    menu_cache_path(a, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    menu_cache_t h;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "GFMN", 4) && h.ver == MENU_CACHE_VER &&
              h.course == a->course &&
              fread(a->v3dbuf, 2, (size_t)GF_W * GF_H, f) == (size_t)GF_W * GF_H;
    fclose(f);
    if (ok) {
        a->menu_ball_x16 = h.ball_x16;
        a->menu_ball_y16 = h.ball_y16;
    }
    return ok;
}

static void menu_cache_save(app_t *a)
{
    char path[112];
    menu_cache_path(a, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    menu_cache_t h = { { 'G', 'F', 'M', 'N' }, MENU_CACHE_VER, (uint8_t)a->course, 0, a->menu_ball_x16, a->menu_ball_y16 };
    bool ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(a->v3dbuf, 2, (size_t)GF_W * GF_H, f) == (size_t)GF_W * GF_H;
    fclose(f);
    if (!ok) remove(path);
}

static void menu_scene_build(app_t *a)
{
    gf_course_select(a->course);
    uint64_t t0 = aos_hal_uptime_ms();
    if (menu_cache_load(a)) {
        aos_hal_log("golf", "menu from the card in %u ms", (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0));
        return;
    }
    gf_world_load(&a->world, &gf_course()->holes[0], 1, 0);
    a->ampp = 0.5f;
    a->atw = (int)((float)a->world.gw / a->ampp);
    a->ath = (int)((float)a->world.gh / a->ampp);
    gf_map_albedo(&a->world, a->albedo, a->atw, a->ath, a->ampp);
    float bx = a->world.tee_x, by = a->world.tee_y;
    float aim = atan2f(a->world.pin_x - bx, a->world.pin_y - by) - 0.12f;
    gf_cam_swing(&a->cam, &a->world, bx, by, aim);
    gf_albedo_t alb = { a->albedo, a->atw, a->ath, a->ampp };
    gf_view3d_render(&a->world, &a->cam, &alb, a->v3dbuf, a->depth);
    /* darken the right side a little, under the buttons */
    for (int y = 0; y < GF_H; y++) {
        for (int x = 170; x < GF_W; x++) {
            int k = 256 - (x - 170) * 70 / (GF_W - 170);
            a->v3dbuf[y * GF_W + x] = gf_scale(a->v3dbuf[y * GF_W + x], k);
        }
    }
    float bsx, bsy, zc;
    if (gf_cam_project(&a->cam, bx, by, gf_height(&a->world, bx, by) + 0.021f, &bsx, &bsy, &zc)) {
        a->menu_ball_x16 = (int16_t)(bsx * 16);
        a->menu_ball_y16 = (int16_t)(bsy * 16);
    }
    menu_cache_save(a);
    aos_hal_log("golf", "menu rendered and saved in %u ms", (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0));
}

void gfp_menu_scene(app_t *a)
{
    a->menu_ready = false;
    job_start(a, JOB_MENU);             /* the picture first, then the golfer */
    if (a->shown_player != 0) {
        a->shown_player = -1;
        set_outfit_for(a, 0);
    }
}

void gfp_menu_frame(app_t *a, int dt)
{
    (void)dt;
    gfp_poll(a);
    if (!a->menu_ready) return;
    int ni = gf_art_frames(SEQ_IDLE);
    int seq = ni > 0 ? SEQ_IDLE : SEQ_SWING;
    int frame = 0;
    if (ni > 0) {
        int t = (int)(a->st_ms / 150) % (ni * 2 - 2 > 0 ? ni * 2 - 2 : 1);
        frame = t < ni ? t : ni * 2 - 2 - t;
    }
    static int last = -1;
    if (frame == last && a->dprev.n && !a->art_dirty) return;
    last = frame;
    a->art_dirty = false;
    gfo_begin(a);
    gf_player_t fake = { 0 };
    (void)fake;
    gf_img_t im = fb_img(a);
    if (a->menu_ball_x16) {
        int bx = a->menu_ball_x16 / 16, by = a->menu_ball_y16 / 16;
        gf_ball(&im, a->menu_ball_x16, a->menu_ball_y16, 40);
        gfo_mark(a, bx - 4, by - 4, bx + 5, by + 5);
    }
    draw_golfer(a, seq, frame);
    gfo_end(a);
}

/* --------------------------------------------------------------------------
 * The golfer's reaction under the hole card
 * -------------------------------------------------------------------------- */

void gfp_react_begin(app_t *a, int seq)
{
    a->react_seq = seq;
    gfp_outfit(a, a->job_eq, (1u << SEQ_CHEER) | (1u << SEQ_SAD), false);
    gf_player_t *p = cur_player(a);
    (void)p;
    gfo_set_bg(a, a->v3dbuf);
    /* the reaction is drawn where the swing camera puts the golfer: the last
     * 3D view is behind it, the card on top */
}

void gfp_react_frame(app_t *a)
{
    int seq = a->react_seq;
    int n = gf_art_frames(seq);
    if (n <= 0) return;
    int frame;
    if (seq == SEQ_IDLE) {
        int t = (int)(a->st_ms / 150) % (n * 2 - 2 > 0 ? n * 2 - 2 : 1);
        frame = t < n ? t : n * 2 - 2 - t;
    } else {
        frame = (int)(a->st_ms / 110);
        if (frame >= n) {
            /* hold the last pose, with a small loop of the last two */
            frame = n - 1 - (int)((a->st_ms / 400) & 1);
        }
    }
    static int last = -1;
    if (frame == last && a->dprev.n && !a->art_dirty) return;
    last = frame;
    a->art_dirty = false;
    gfo_begin(a);
    draw_golfer(a, seq, frame);
    gfo_end(a);
}
