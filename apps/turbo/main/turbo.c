/*
 * TURBO - an arcade racer: the app (see tb_app.h for the map of the files)
 *
 * Every word on screen is wrapped in _(): the LVGL panels directly, and the
 * words inside the frame (the clock's "TIME", "CHECKPOINT"...) are rendered
 * from _() into masks when the app opens, so they follow the language too.
 * Proper names (the stages, the cars) are not translated.
 */
#include "tb_app.h"
#include "tb_audio.h"

#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS         8           /* the timer: pushes frames as they come  */
#define ACCENT          0xFF8A1E
#define WORKER_STACK    (12 * 1024)

#define KEY_COINS   "tb_coins"
#define KEY_CARS    "tb_cars"
#define KEY_PAINTS  "tb_paints"
#define KEY_CAR     "tb_car"
#define KEY_PNT     "tb_pnt"
#define KEY_DIFF    "tb_diff"
#define KEY_SENS    "tb_sens"
#define KEY_SFX     "tb_sfx"
#define KEY_UNL     "tb_unl"
#define KEY_TOUR    "tb_tour"

/* one key per stage and per car, numbered: a new stage or car needs no
 * new key names, and the first five stages keep the keys they had */
static const char *key_n(char *buf, size_t n, const char *prefix, int i)
{
    snprintf(buf, n, "%s%d", prefix, i);
    return buf;
}

static bool s_sfx = true;

static void snd(int id)
{
    if (s_sfx) tb_snd(id);
}

static void fmt_time(char *b, size_t n, float t)
{
    if (t < 0) t = 0;
    int ds = (int)(t * 10.0f);         /* truncated, like the HUD's clock */
    snprintf(b, n, "%d:%02d.%d", ds / 600, (ds / 10) % 60, ds % 10);
}

/* --------------------------------------------------------------------------
 * Preferences
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    int32_t v;
    a->coins = aos_hal_pref_get_i32(KEY_COINS, &v) ? v : 0;
    a->own_cars = aos_hal_pref_get_i32(KEY_CARS, &v) ? (uint32_t)v | 1u : 1u;
    a->own_paints = aos_hal_pref_get_i32(KEY_PAINTS, &v) ? (uint32_t)v | 1u : 1u;
    a->car = aos_hal_pref_get_i32(KEY_CAR, &v) && v >= 0 && v < CAR_N ? v : 0;
    if (!(a->own_cars & (1u << a->car))) a->car = 0;
    char k[16];
    /* the paint of each car: one key per car since v0.4.12 (tb_pc<car>);
     * before, 4 bits per car in tb_pnt, which capped 16 paints and 8 cars */
    int32_t old = 0;
    bool have_old = aos_hal_pref_get_i32(KEY_PNT, &old);
    for (int c = 0; c < CAR_N; c++) {
        int p = 0;
        if (aos_hal_pref_get_i32(key_n(k, sizeof k, "tb_pc", c), &v)) p = v;
        else if (have_old && c < 8) p = (int)(((uint32_t)old >> (c * 4)) & 15);
        a->paint[c] = (uint8_t)(p >= 0 && p < tb_paint_n() && (a->own_paints & (1u << p)) ? p : 0);
    }
    a->diff = aos_hal_pref_get_i32(KEY_DIFF, &v) && v >= 0 && v < DIFF_N ? v : DIFF_NORMAL;
    a->sens = aos_hal_pref_get_i32(KEY_SENS, &v) && v >= 0 && v < 3 ? v : 1;
    s_sfx = !(aos_hal_pref_get_i32(KEY_SFX, &v) && v == 0);
    a->sfx = s_sfx;
    /* the stages open from the start are always open, whatever was saved */
    a->unlocked = (aos_hal_pref_get_i32(KEY_UNL, &v) ? (uint32_t)v : 0u) | tb_stage_open_mask();
    a->best_tour = aos_hal_pref_get_i32(KEY_TOUR, &v) ? v : 0;
    for (int i = 0; i < STAGE_N; i++) {
        a->best[i] = aos_hal_pref_get_i32(key_n(k, sizeof k, "tb_best", i), &v) ? v : 0;
        a->rival_best[i] = aos_hal_pref_get_i32(key_n(k, sizeof k, "tb_rb", i), &v) ? v : 0;
    }
    if (!aos_hal_pref_get_str("tb_rname", a->rival_name, sizeof a->rival_name)) a->rival_name[0] = 0;
}

void tba_prefs_save(app_t *a)
{
    aos_hal_pref_set_i32(KEY_COINS, a->coins);
    aos_hal_pref_set_i32(KEY_CARS, (int32_t)a->own_cars);
    aos_hal_pref_set_i32(KEY_PAINTS, (int32_t)a->own_paints);
    aos_hal_pref_set_i32(KEY_CAR, a->car);
    char k[16];
    for (int c = 0; c < CAR_N; c++) aos_hal_pref_set_i32(key_n(k, sizeof k, "tb_pc", c), a->paint[c]);
    aos_hal_pref_set_i32(KEY_DIFF, a->diff);
    aos_hal_pref_set_i32(KEY_SENS, a->sens);
    aos_hal_pref_set_i32(KEY_SFX, s_sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_UNL, (int32_t)a->unlocked);
    aos_hal_pref_set_i32(KEY_TOUR, a->best_tour);
    for (int i = 0; i < STAGE_N; i++) {
        aos_hal_pref_set_i32(key_n(k, sizeof k, "tb_best", i), a->best[i]);
        aos_hal_pref_set_i32(key_n(k, sizeof k, "tb_rb", i), a->rival_best[i]);
    }
    aos_hal_pref_set_str("tb_rname", a->rival_name);
}

/* --------------------------------------------------------------------------
 * The worker: loading, scenes, and the race itself
 * -------------------------------------------------------------------------- */

static uint64_t s_last_yield;
static uint32_t s_wait_cyc;         /* cycles the worker slept: the profile's "wait" */

static void worker_yield(void)
{
    uint64_t now = aos_hal_uptime_ms();
    if ((uint32_t)(now - s_last_yield) > 1000) {
        /* one tick (a 2 ms sleep is vTaskDelay(0) at 100 Hz, Golf's trap);
         * once a second keeps the idle task fed (the watchdog waits 5 s)
         * for 1 % of the frames: every 200 ms it was 5 % */
        uint32_t w0 = tb_cycles();
        aos_hal_worker_sleep(10);
        s_wait_cyc += tb_cycles() - w0;
        s_last_yield = aos_hal_uptime_ms();
    }
}

static uint32_t clock_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static void ensure_stage(app_t *a, int stage)
{
    if (a->loaded_stage == stage) return;
    uint64_t t0 = aos_hal_uptime_ms();
    /* the coloured car goes while the stage loads: the new props and the
     * backdrop (490 KB until composited) with it still in memory left
     * 371 KB of PSRAM free at the worst moment */
    tb_render_car_drop(a->ren);
    /* and the previous race's vehicles: the new stage loads its own after
     * its props (JOB_STAGE), never both sets at once */
    tb_art_load_vehicles(0);
    tb_track_free(&a->trk);
    if (!tb_track_build(&a->trk, stage)) {
        aos_hal_log("turbo", "stage %d: out of memory", stage);
        a->loaded_stage = -1;
        return;
    }
    tb_art_load_stage(&a->trk);
    tb_render_stage(a->ren, &a->trk);
    tb_art_drop_backdrop();
    a->loaded_stage = stage;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("turbo", "stage %d: %d segments, %d props, %u ms | internal %u, psram %u", stage,
                a->trk.nseg, a->trk.nprop, (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hi, (unsigned)hp);
}

/* the menu's and the garage's picture: the car on the start line */
static void render_scene(app_t *a)
{
    ensure_stage(a, a->job_stage);
    if (a->loaded_stage < 0) return;
    tb_paint_t p;
    tb_paint_get(a->scene_paint, &p);
    tb_render_paint(a->ren, &p, &p);
    tb_game_t *g = &a->game;
    g->rival_on = false;
    tb_game_start(g, &a->trk, a->scene_car, DIFF_EASY, 3);
    g->ntraffic = 0;
    g->yaw = a->scene_yaw;
    tb_img_t im;
    tb_img_init(&im, a->fb[0], TB_W, TB_H);
    tb_render_world(a->ren, &im, g, 0);
#ifdef AOS_SIM_BUILTIN
    if (getenv("TB_DUMP")) {
        FILE *f = fopen(getenv("TB_DUMP"), "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", TB_W, TB_H);
            for (int i = 0; i < TB_W * TB_H; i++) {
                int r, gg, b;
                tb_unpack(a->fb[0][i], &r, &gg, &b);
                fputc(r, f); fputc(gg, f); fputc(b, f);
            }
            fclose(f);
        }
    }
#endif
}

static void run_job(app_t *a, int j)
{
    switch (j) {
    case JOB_BOOT: {
        uint64_t t0 = aos_hal_uptime_ms();
        char path[96];
        snprintf(path, sizeof path, "%s/turbo.pak", aos_hal_path_apps());
        bool art = tb_art_open(path);
        /* no vehicles yet: each race loads the ones its stage uses */
        if (art) tb_art_load_vehicles(0);
        uint64_t t1 = aos_hal_uptime_ms();
        aos_hal_log("turbo", "pack %s in %u ms", art ? "open" : "MISSING", (unsigned)(uint32_t)(t1 - t0));
        render_scene(a);
        break;
    }
    case JOB_STAGE: {
        ensure_stage(a, a->job_stage);
        if (a->loaded_stage < 0) break;
        /* the vehicles of this race: the stage's traffic and the rival's car */
        uint64_t t0 = aos_hal_uptime_ms();
        uint32_t mask = tb_track_vehicles(&a->trk);
        if (a->mode == MODE_LINK) mask |= 1u << a->rival_car;
        tb_art_load_vehicles(mask);
        uint32_t hi = 0, hp = 0;
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("turbo", "vehicles %03x in %u ms | psram %u", (unsigned)tb_art_vehicles_loaded(),
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hp);
        break;
    }
    case JOB_SCENE:
        render_scene(a);
        break;
    default:
        break;
    }
}

static int free_fb(app_t *a)
{
    for (int i = 0; i < a->nfb; i++) {
        if (a->fb_state[i] == FB_FREE) return i;
    }
    return -1;
}

/* A fourth frame if PSRAM allows: with three the worker waited 2-4 ms a
 * frame for LVGL to push one (its sleep is a whole 10 ms tick), and a
 * fourth made the stages 1-5 fps faster. Asked for after the first frame,
 * when the player's car is coloured (at night it is coloured again at the
 * start, and that peak is what the spare margin must survive). */
static void spare_frame(app_t *a)
{
    a->spare_checked = true;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    if (!a->fb[3] && hp > (uint32_t)(TB_W * TB_H * 2) + TB_FB_SPARE) {
        uint16_t *f = (uint16_t *)tb_malloc((size_t)TB_W * TB_H * 2);
        if (f) {
            a->fb_state[3] = FB_FREE;
            a->fb[3] = f;
            a->nfb = 4;                 /* last: the UI loops up to nfb */
        }
    }
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("turbo", "race with %d frame buffers | psram %u B free", a->nfb, (unsigned)hp);
}

static void race_frame(app_t *a)
{
    int i = free_fb(a);
    if (i < 0) {
        uint32_t w0 = tb_cycles();
        aos_hal_worker_sleep(10);
        s_last_yield = aos_hal_uptime_ms();
        s_wait_cyc += tb_cycles() - w0;
        return;
    }
    uint32_t ks = tb_cycles();
    a->fb_state[i] = FB_BUSY;
    uint64_t now = aos_hal_uptime_ms();
    float dt = a->w_last_ms ? (float)(uint32_t)(now - a->w_last_ms) / 1000.0f : 0.033f;
    if (dt > 0.1f) dt = 0.1f;
    a->w_last_ms = now;
    tb_game_t *g = &a->game;
    if (!a->paused) {
        if (a->autoplay) {
            tb_game_bot(g);
        } else {
            g->in_steer = a->steer;
            g->in_gas = a->gas;
            g->in_brake = a->brake;
        }
        tb_game_step(g, dt);
        tb_render_cycles(a->ren)[TB_PROF_STEP] += tb_cycles() - ks;
        uint32_t ev = g->events;
        g->events = 0;
        tb_hud_events(&a->hs, g, ev, dt);
        if (ev) {
            a->ev_ring[a->ev_w & 15] = ev;
            a->ev_w++;
        }
    }
    a->hs.gas = g->in_gas;
    a->hs.brake = g->in_brake;
    tb_hud_prepare(&a->hs, g);
    uint64_t r0 = aos_hal_uptime_ms();
    uint32_t *cyc = tb_render_cycles(a->ren);
    uint32_t k0 = tb_cycles();
    tb_render_prepare(a->ren, g, dt);
    cyc[TB_PROF_PREP] += tb_cycles() - k0;
    a->w_ms_prep += (uint32_t)(aos_hal_uptime_ms() - r0);
    if (a->band) {
        /* band by band in internal RAM, each copied out once: every blend
         * and every overdraw in PSRAM was a read and a write at ~22 MB/s,
         * and the frame took 47 ms that way */
        for (int y0 = 0; y0 < TB_H; y0 += TB_BAND) {
            int y1 = y0 + TB_BAND > TB_H ? TB_H : y0 + TB_BAND;
            tb_img_t bim;
            tb_img_init(&bim, a->band - (size_t)y0 * TB_W, TB_W, TB_H);
            tb_img_clip(&bim, 0, y0, TB_W, y1);
            tb_render_band(a->ren, &bim, g, y0, y1);
            uint32_t k1 = tb_cycles();
            tb_hud_draw(&a->hud, &bim, g, &a->hs);
            uint32_t k2 = tb_cycles();
            memcpy(a->fb[i] + (size_t)y0 * TB_W, a->band, (size_t)(y1 - y0) * TB_W * 2);
            cyc[TB_PROF_HUD] += k2 - k1;
            cyc[TB_PROF_COPY] += tb_cycles() - k2;
        }
    } else {
        tb_img_t im;
        tb_img_init(&im, a->fb[i], TB_W, TB_H);
        tb_render_band(a->ren, &im, g, 0, TB_H);
        tb_hud_draw(&a->hud, &im, g, &a->hs);
    }
    a->w_ms_render += (uint32_t)(aos_hal_uptime_ms() - r0);
    /* the counters are 32-bit (18 s at 240 MHz): fold them each frame */
    cyc[TB_PROF_WAIT] += s_wait_cyc;
    s_wait_cyc = 0;
    for (int k = 0; k < TB_PROF_N; k++) {
        a->w_cyc[k] += cyc[k];
        cyc[k] = 0;
    }
    a->w_frames++;
    a->fb_seq[i] = ++a->seq;
    a->fb_state[i] = FB_READY;
    if (!a->spare_checked) spare_frame(a);
    worker_yield();
}

static void worker_fn(void *arg)
{
    app_t *a = (app_t *)arg;
    tb_set_clock(clock_ms);
    tb_set_yield(worker_yield);
    while (!aos_hal_worker_should_stop()) {
        if (a->job) {
            s_last_yield = aos_hal_uptime_ms();
            run_job(a, a->job);
            a->job = JOB_NONE;
            a->job_done = true;
            continue;
        }
        if (a->racing && !a->paused) {
            race_frame(a);
            continue;
        }
        a->w_last_ms = 0;
        aos_hal_worker_sleep(20);
    }
    tb_set_yield(NULL);
}

static void job(app_t *a, int j)
{
    a->job_done = false;
    a->job = j;
}

/* --------------------------------------------------------------------------
 * Interface pieces
 * -------------------------------------------------------------------------- */

static lv_obj_t *panel(lv_obj_t *parent, int dim)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    if (dim) {
        lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(p, (lv_opa_t)dim, 0);
    }
    return p;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint32_t accent,
                        lv_event_cb_t cb, void *data, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x14161C), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, h >= 44 ? aos_font_title : aos_font_body, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w - 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (lbl_out) *lbl_out = l;
    return b;
}

void tba_toast(app_t *a, const char *txt)
{
    (void)a;
    aos_ui_toast(txt, 2000);
}

static lv_obj_t *panel_of(app_t *a, int i)
{
    lv_obj_t *const p[] = { a->p_boot, a->p_menu, a->p_select, a->p_garage, a->p_settings,
                            a->p_loading, a->p_result, a->p_pause, a->p_lobby };
    return i < (int)(sizeof(p) / sizeof(p[0])) ? p[i] : NULL;
}

static void show_panel(app_t *a, lv_obj_t *show)
{
    for (int i = 0; i < 9; i++) {
        lv_obj_t *p = panel_of(a, i);
        if (p && p != show) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    if (show) {
        lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(show);
    }
}

/* the canvas shows this frame under the panels. It has a buffer of its own
 * in LVGL's byte order: LVGL 9.5 draws nothing from an RGB565_SWAPPED
 * canvas (measured in the simulator: all black), so the panel-order frame
 * is swapped into it, ~3 ms, only when a panel opens (and in the simulator,
 * which has no panel to blit to, every frame) */
static void canvas_show(app_t *a, int i)
{
    /* byte by byte: written as a word swap the compiler calls __bswapsi2,
     * which the firmware does not export */
    const uint8_t *s = (const uint8_t *)a->fb[i];
    uint8_t *d = (uint8_t *)a->cv;
    for (int k = 0; k < TB_W * TB_H * 2; k += 2) {
        uint8_t hi = s[k];
        d[k] = s[k + 1];
        d[k + 1] = hi;
    }
    lv_obj_invalidate(a->canvas);
}

/* --------------------------------------------------------------------------
 * Text in the frame: LVGL renders it once into masks, the HUD bakes them
 * -------------------------------------------------------------------------- */

static bool text_mask(tb_mask_t *m, const char *txt, const lv_font_t *font)
{
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2, h = sz.y;
    memset(m, 0, sizeof(*m));
    if (w <= 2 || h <= 0) return false;
    lv_draw_buf_t *db = lv_draw_buf_create((uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_L8, 0);
    if (!db) return false;
    lv_obj_t *c = lv_canvas_create(lv_layer_top());
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(c, db);
    lv_canvas_fill_bg(c, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(c, &layer);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = lv_color_hex(0xFFFFFF);
    d.font = font;
    d.text = txt;
    lv_area_t area = { 1, 0, w - 1, h - 1 };
    lv_draw_label(&layer, &d, &area);
    lv_canvas_finish_layer(c, &layer);
    m->a = (uint8_t *)tb_malloc((size_t)w * h);
    if (m->a) {
        m->w = (int16_t)w;
        m->h = (int16_t)h;
        for (int y = 0; y < h; y++) memcpy(m->a + (size_t)y * w, db->data + (size_t)y * db->header.stride, (size_t)w);
    }
    lv_obj_delete(c);
    lv_draw_buf_destroy(db);
    return m->a != NULL;
}

static void bake_text(tb_sprite_t *out, const char *txt, const lv_font_t *font, uint32_t top, uint32_t bot, int ol)
{
    tb_mask_t m;
    if (text_mask(&m, txt, font)) {
        tb_hud_bake(out, &m, top, bot, ol);
        free(m.a);
    }
}

static void hud_build(app_t *a)
{
    static const char glyphs[TB_GLYPHS + 1] = "0123456789:.+-";
    tb_hud_t *h = &a->hud;
    for (int i = 0; i < TB_GLYPHS; i++) {
        char s[2] = { glyphs[i], 0 };
        bake_text(&h->big[i], s, &aos_montserrat_48, 0xFFF45A, 0xFF8A1E, 3);
        bake_text(&h->mid[i], s, &aos_montserrat_36, 0xFFFFFF, 0xC8D4E0, 2);
        bake_text(&h->sml[i], s, &aos_montserrat_20, 0xFFFFFF, 0xD8E0E8, 2);
    }
    bake_text(&h->word[TX_TIME], _("TIEMPO"), &aos_montserrat_20, 0xFFE040, 0xFFB020, 2);
    bake_text(&h->word[TX_KMH], _("km/h"), &aos_montserrat_14, 0xD8E0E8, 0xB8C0C8, 1);
    bake_text(&h->word[TX_CHECKPOINT], _("PUNTO DE CONTROL"), &aos_montserrat_28, 0xFFFFFF, 0x9CE0FF, 3);
    bake_text(&h->word[TX_EXTRA], _("¡TIEMPO EXTRA!"), &aos_montserrat_28, 0xFFF45A, 0xFF8A1E, 3);
    bake_text(&h->word[TX_GO], _("¡YA!"), &aos_montserrat_48, 0x9CFF6A, 0x30C040, 3);
    bake_text(&h->word[TX_FINISH], _("¡LLEGADA!"), &aos_montserrat_48, 0xFFFFFF, 0xFFD040, 3);
    bake_text(&h->word[TX_TIMEUP], _("SIN TIEMPO"), &aos_montserrat_48, 0xFF8A70, 0xE02020, 3);
    bake_text(&h->word[TX_HURRY], _("¡APURATE!"), &aos_montserrat_28, 0xFF8A70, 0xE02020, 3);
    tb_hud_make_pedals(h);
    h->ok = true;
}

/* --------------------------------------------------------------------------
 * States
 * -------------------------------------------------------------------------- */

static void menu_refresh(app_t *a);
static void select_refresh(app_t *a);

/* the 4th frame is only for racing: loading a stage or recolouring the car
 * in the garage want the room */
static void spare_free(app_t *a)
{
    if (!a->fb[3] || a->racing) return;
    if (a->shown == 3) a->shown = -1;
    a->nfb = 3;
    free(a->fb[3]);
    a->fb[3] = NULL;
}
static void garage_refresh(app_t *a);
static void settings_refresh(app_t *a);

static void scene(app_t *a, int stage, int car, int paint, float yaw)
{
    a->job_stage = stage;
    a->scene_car = car;
    a->scene_paint = paint;
    a->scene_yaw = yaw;
    job(a, JOB_SCENE);
}

void tba_set_state(app_t *a, int st)
{
    int prev = a->state;
    a->state = st;
    a->st_ms = 0;
    switch (st) {
    case ST_MENU:
        spare_free(a);
        menu_refresh(a);
        show_panel(a, a->p_menu);
        if (prev != ST_BOOT) scene(a, a->loaded_stage >= 0 ? a->loaded_stage : 0, a->car, a->paint[a->car], 0);
        break;
    case ST_SELECT:
        select_refresh(a);
        show_panel(a, a->p_select);
        break;
    case ST_GARAGE:
        a->g_car = a->car;
        a->g_paint = a->paint[a->car];
        garage_refresh(a);
        show_panel(a, a->p_garage);
        scene(a, a->loaded_stage >= 0 ? a->loaded_stage : 0, a->g_car, a->g_paint, 0);
        break;
    case ST_SETTINGS:
        settings_refresh(a);
        show_panel(a, a->p_settings);
        break;
    case ST_LOADING:
        show_panel(a, a->p_loading);
        break;
    case ST_RACE:
        show_panel(a, NULL);
        break;
    case ST_RESULT:
        show_panel(a, a->p_result);
        break;
    case ST_LOBBY:
        show_panel(a, a->p_lobby);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The race
 * -------------------------------------------------------------------------- */

static void tour_begin(app_t *a)
{
    a->mode = MODE_TOUR;
    a->tour_time = 0;
    a->tour_i = 0;
    tba_race_start(a, tb_tour_stage(0));
}

void tba_race_start(app_t *a, int stage)
{
    /* each race decides again, after its stage and its car are in memory */
    spare_free(a);
    a->stage = stage;
    char b[64];
    snprintf(b, sizeof b, "%s\n%s", _("Cargando"), tb_stage_name(stage));
    lv_label_set_text(a->load_lbl, b);
    tba_set_state(a, ST_LOADING);
    a->job_stage = stage;
    job(a, JOB_STAGE);
}

static void race_go(app_t *a)
{
    tb_game_t *g = &a->game;
    tb_paint_t p, rp;
    tb_paint_get(a->paint[a->car], &p);
    tb_paint_get(a->rival_paint, &rp);
    tb_render_paint(a->ren, &p, &rp);
    uint32_t seed = a->mode == MODE_LINK ? a->link_nonce ^ a->link_peer_nonce : (uint32_t)aos_hal_uptime_ms() | 1u;
    g->rival_on = a->mode == MODE_LINK;
    tb_game_start(g, &a->trk, a->car, a->diff, seed);
    g->rival_car = a->rival_car;
    g->rival_paint = a->rival_paint;
    g->rival_z = g->z;
    g->rival_x = 0;
    memset(&a->hs, 0, sizeof a->hs);
    a->hs.banner = -1;
    a->hs.rival = a->mode == MODE_LINK;
    a->rival_done = false;
    a->result_shown = false;
    a->zeroed = false;
    a->gas = a->brake = false;
    a->steer = 0;
    a->ev_r = a->ev_w;
    /* the race starts with three frames; the worker adds a fourth after
     * its first frame, once the car is coloured (spare_frame) */
    a->nfb = a->fb[3] ? 4 : 3;
    a->spare_checked = false;
    for (int i = 0; i < TB_NFB; i++) a->fb_state[i] = FB_FREE;
    a->shown = -1;
    a->paused = false;
    a->w_frames = 0;
    a->w_ms_render = 0;
    a->w_ms_prep = 0;
    memset(a->w_cyc, 0, sizeof a->w_cyc);
    memset(tb_render_cycles(a->ren), 0, sizeof(uint32_t) * TB_PROF_N);
    a->w_fps_t0 = aos_hal_uptime_ms();
    tba_set_state(a, ST_RACE);
    a->racing = true;
}

static void pause_show(app_t *a)
{
    if (a->state != ST_RACE || a->paused) return;
    a->paused = true;
    a->gas = a->brake = false;
    if (a->shown >= 0) canvas_show(a, a->shown);
    tb_audio_engine(false, 0, 0, 0, 0, 0);
    show_panel(a, a->p_pause);
}

static void resume(app_t *a)
{
    a->paused = false;
    a->w_last_ms = 0;
    show_panel(a, NULL);
}

static int stage_coins(app_t *a, const tb_game_t *g, bool finished)
{
    int c = finished ? 40 + (int)g->time_left * 2 : (int)(tb_game_progress(g) * 30.0f);
    c += g->passes / 3;
    if (a->diff == DIFF_EASY) c = c * 7 / 10;
    if (a->diff == DIFF_HARD) c = c * 3 / 2;
    return c;
}

/* the results' text; the rival's line changes when its result arrives */
static void result_text(app_t *a)
{
    tb_game_t *g = &a->game;
    bool fin = g->state == RS_FINISHED;
    char body[400], t[24], bt[24];
    int s = a->stage;
    fmt_time(t, sizeof t, g->elapsed);
    fmt_time(bt, sizeof bt, (float)a->best[s] / 10.0f);
    int n = 0;
    n += snprintf(body + n, sizeof body - (size_t)n, "%s\n", tb_stage_name(s));
    if (fin) n += snprintf(body + n, sizeof body - (size_t)n, "%s  %s\n", _("Tiempo"), t);
    else n += snprintf(body + n, sizeof body - (size_t)n, "%s %d %%\n", _("Recorrido"), (int)(tb_game_progress(g) * 100.0f));
    if (a->best[s]) n += snprintf(body + n, sizeof body - (size_t)n, "%s  %s%s\n", _("Récord"), bt,
                                  a->new_record ? _("  ¡nuevo!") : "");
    n += snprintf(body + n, sizeof body - (size_t)n, "%s %d km/h · %s %d\n", _("Punta"), (int)g->top_speed,
                  _("choques"), g->crashes);
    if (a->mode == MODE_TOUR) {
        char tt[24];
        fmt_time(tt, sizeof tt, a->tour_time);
        n += snprintf(body + n, sizeof body - (size_t)n, "%s %d/%d  %s\n", _("Gira"), a->tour_i + 1, tb_tour_len(), tt);
        if (fin && a->tour_i == tb_tour_len() - 1) n += snprintf(body + n, sizeof body - (size_t)n, "%s\n", _("¡Gira completa!"));
    }
    if (a->mode == MODE_LINK) {
        char rt[24];
        char ll[160];
        snprintf(ll, sizeof ll, "link result: stage %d, me %s %d ds, rival %s %s %d ds (%d %%)", a->stage,
                 fin ? "finished" : "time up", (int)(g->elapsed * 10.0f), a->partner,
                 !a->rival_done ? "racing" : (a->rival_finished ? "finished" : "time up"),
                 (int)(a->rival_time * 10.0f), (int)(a->rival_dist * 100.0f));
        aos_hal_log("turbo", "%s", ll);
        if (a->rival_done) {
            char path[96];
            snprintf(path, sizeof path, "%s/turbo_stats.txt", aos_hal_path_apps());
            FILE *f = fopen(path, "a");
            if (f) {
                fprintf(f, "%s\n", ll);
                fclose(f);
            }
        }
        if (!a->rival_done) {
            n += snprintf(body + n, sizeof body - (size_t)n, "%s: %s\n", a->partner, _("todavía corre..."));
        } else {
            if (a->rival_finished) fmt_time(rt, sizeof rt, a->rival_time);
            else snprintf(rt, sizeof rt, "%d %%", (int)(a->rival_dist * 100.0f));
            n += snprintf(body + n, sizeof body - (size_t)n, "%s: %s\n", a->partner, rt);
            bool win = fin && (!a->rival_finished || g->elapsed < a->rival_time);
            if (!fin && !a->rival_finished) win = tb_game_progress(g) > a->rival_dist;
            n += snprintf(body + n, sizeof body - (size_t)n, "%s\n", win ? _("¡Ganaste!") : _("Ganó el otro reloj"));
            if (win && !a->res_win_paid) {
                a->res_win_paid = true;
                a->coins_won += 60;
                a->coins += 60;
                tba_prefs_save(a);
            }
        }
        a->res_rival_seen = a->rival_done;
    }
    snprintf(body + n, sizeof body - (size_t)n, "+%d %s", a->coins_won, _("monedas"));
    lv_label_set_text(a->res_body, body);
}

static void result_show(app_t *a)
{
    tb_game_t *g = &a->game;
    bool fin = g->state == RS_FINISHED;
    int s = a->stage;
    a->coins_won = stage_coins(a, g, fin);
    a->res_win_paid = false;
    int32_t ds = (int32_t)(g->elapsed * 10.0f);
    a->new_record = fin && (a->best[s] == 0 || ds < a->best[s]);
    if (a->new_record) a->best[s] = ds;
    /* finishing a stage opens the ones that wait for it; the tour opens the
     * ones that wait for the tour (tb_track.c's table says which) */
    bool tour_done = a->mode == MODE_TOUR && fin && a->tour_i == tb_tour_len() - 1;
    for (int k = 0; k < STAGE_N; k++) {
        int after = -1;
        int rule = tb_stage_unlock(k, &after);
        if ((rule == UNL_AFTER && fin && after == s) || (rule == UNL_TOUR && tour_done)) a->unlocked |= 1u << k;
    }
    if (a->mode == MODE_TOUR) {
        a->tour_time += g->elapsed;
        if (tour_done) {
            a->coins_won += 200;
            int32_t tds = (int32_t)(a->tour_time * 10.0f);
            if (a->best_tour == 0 || tds < a->best_tour) a->best_tour = tds;
        }
    }
    a->coins += a->coins_won;
    lv_label_set_text(a->res_title, fin ? _("¡LLEGADA!") : _("SIN TIEMPO"));
    result_text(a);
    bool next = a->mode == MODE_TOUR && fin && a->tour_i + 1 < tb_tour_len();
    lv_label_set_text(a->res_btn_next_lbl, next ? _("Siguiente") : _("Menú"));
    lv_obj_set_user_data(a->res_btn_next, (void *)(intptr_t)(next ? 1 : 0));
    tba_prefs_save(a);
    if (a->shown >= 0) canvas_show(a, a->shown);
    tba_set_state(a, ST_RESULT);
}

/* after the finish or the time running out: a few seconds of the car
 * rolling to a stop, then the results */
static void race_tick(app_t *a, int dt)
{
    tb_game_t *g = &a->game;
    /* the events of the worker's steps */
    while (a->ev_r != a->ev_w) {
        uint32_t ev = a->ev_ring[a->ev_r & 15];
        a->ev_r++;
        if (ev & EV_COUNT) snd(SND_COUNT);
        if (ev & EV_GO) snd(SND_GO);
        if (ev & EV_CHECKPOINT) snd(SND_CHECKPOINT);
        if (ev & EV_CRASH) snd(SND_CRASH);
        else if (ev & EV_BUMP) snd(SND_BUMP);
        if (ev & EV_PASS) snd(SND_PASS);
        if (ev & EV_GEAR) snd(SND_GEAR);
        if (ev & EV_LOW_TIME) snd(SND_LOW);
        if (ev & EV_FINISH) {
            snd(SND_FINISH);
            if (a->mode == MODE_LINK) tbl_send_result(a);
        }
        if (ev & EV_TIMEUP) {
            snd(SND_TIMEUP);
            if (a->mode == MODE_LINK) tbl_send_result(a);
        }
    }
    if (s_sfx && !a->paused) {
        float sq = fabsf(g->slide) > 0.6f ? (fabsf(g->slide) - 0.6f) * 2.0f : 0.0f;
        if (g->in_brake && g->v > 20.0f) sq += 0.5f;
        tb_audio_engine(true, g->rpm, g->in_gas ? 1.0f : 0.0f, g->v / 86.0f, sq > 1 ? 1 : sq,
                        g->offroad && g->v > 5.0f ? 0.8f : 0.0f);
    }
    if ((g->state == RS_FINISHED || g->state == RS_TIMEUP) && g->t_state > 3.0f && !a->result_shown) {
        a->result_shown = true;
        a->racing = false;
        tb_audio_engine(false, 0, 0, 0, 0, 0);
        /* the fps of the race, for the log */
        uint32_t ms = (uint32_t)(aos_hal_uptime_ms() - a->w_fps_t0);
        if (ms && a->w_frames) {
            unsigned fps10 = (unsigned)(a->w_frames * 10000u / ms);
            unsigned r10 = (unsigned)(a->w_ms_render * 10u / a->w_frames);
            unsigned p10 = (unsigned)(a->w_ms_prep * 10u / a->w_frames);
            char line[256];
            int n = snprintf(line, sizeof line, "race: stage %d, %u frames in %u ms, %u.%u fps, render %u.%u ms/frame (prepare %u.%u), %s;",
                             a->stage, (unsigned)a->w_frames, (unsigned)ms, fps10 / 10, fps10 % 10, r10 / 10, r10 % 10,
                             p10 / 10, p10 % 10, a->band ? "bands" : "PSRAM");
            static const char *const part[TB_PROF_N] = { "prep", "sky", "road", "props", "traffic", "car", "hud", "copy", "step", "wait" };
            for (int k = 0; k < TB_PROF_N && n < (int)sizeof line - 16; k++) {
                /* cycles to tenths of a ms per frame: / 24000 */
                unsigned t10 = (unsigned)(a->w_cyc[k] / a->w_frames / 24000u);
                n += snprintf(line + n, sizeof line - (size_t)n, " %s %u.%u", part[k], t10 / 10, t10 % 10);
            }
            aos_hal_log("turbo", "%s", line);
            /* and on the card: the log's ring turns over in a few minutes */
            char path[96];
            snprintf(path, sizeof path, "%s/turbo_stats.txt", aos_hal_path_apps());
            FILE *f = fopen(path, "a");
            if (f) {
                fprintf(f, "%s\n", line);
                fclose(f);
            }
        }
        result_show(a);
    }
    (void)dt;
}

/* the steering: the watch tilted left or right */
static void read_tilt(app_t *a)
{
    if (a->imu_skip > 0) {
        a->imu_skip--;
        return;
    }
    a->imu_skip = 2;                    /* every third tick: ~40 Hz */
    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) return;
    /* HARDWARE.md: the right of the screen is -ay. Held flat or upright
     * like a wheel, turning it moves gravity along that axis. */
    if (!a->zeroed || (a->game.state == RS_COUNTDOWN && a->game.t_state < 2.5f)) {
        a->zero_ay = imu.ay;
        a->zeroed = true;
    }
    static const float range[3] = { 0.55f, 0.40f, 0.28f };
    float s = -(imu.ay - a->zero_ay) / range[a->sens];
    float dz = 0.05f;
    if (s > -dz && s < dz) s = 0;
    else s = s > 0 ? (s - dz) / (1 - dz) : (s + dz) / (1 - dz);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    a->steer += (s - a->steer) * 0.6f;
}

static void push_frame(app_t *a)
{
    int best = -1;
    uint32_t bs = 0;
    for (int i = 0; i < a->nfb; i++) {
        if (a->fb_state[i] == FB_READY && (best < 0 || a->fb_seq[i] > bs)) {
            best = i;
            bs = a->fb_seq[i];
        }
    }
    if (best < 0) return;
    /* older ready frames are dropped */
    for (int i = 0; i < a->nfb; i++) {
        if (i != best && a->fb_state[i] == FB_READY) a->fb_state[i] = FB_FREE;
    }
    if (!aos_hal_display_blit(0, 0, TB_W, TB_H, a->fb[best])) {
        /* the simulator (or the panel asleep): through the canvas */
        canvas_show(a, best);
    }
    if (a->shown >= 0 && a->shown != best) a->fb_state[a->shown] = FB_FREE;
    a->fb_state[best] = FB_SHOWN;
    a->shown = best;
}

/* --------------------------------------------------------------------------
 * Panels
 * -------------------------------------------------------------------------- */

static app_t *app_of(lv_event_t *e)
{
    return (app_t *)lv_event_get_user_data(e);
}

static void menu_tour_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    tour_begin(a);
}

static void menu_trial_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->mode = MODE_TRIAL;
    tba_set_state(a, ST_SELECT);
}

static void menu_link_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->mode = MODE_LINK;
    tba_set_state(a, ST_SELECT);
}

static void menu_garage_cb(lv_event_t *e) { tba_set_state(app_of(e), ST_GARAGE); }
static void menu_settings_cb(lv_event_t *e) { tba_set_state(app_of(e), ST_SETTINGS); }

static void menu_refresh(app_t *a)
{
    char b[48];
    snprintf(b, sizeof b, "%d " LV_SYMBOL_BULLET, (int)a->coins);
    lv_label_set_text(a->lbl_coins, b);
    char name[28];
    if (tbl_available(a, name, sizeof name)) {
        snprintf(b, sizeof b, "%s %s", _("Contra"), name);
        lv_label_set_text(a->lbl_link, b);
        lv_obj_remove_flag(a->btn_link, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(a->btn_link, LV_OBJ_FLAG_HIDDEN);
    }
    /* without a partner the last row moves up into its place */
    int y = lv_obj_has_flag(a->btn_link, LV_OBJ_FLAG_HIDDEN) ? 150 + 108 : 150 + 162;
    lv_obj_set_y(a->btn_garage, y);
    lv_obj_set_y(a->btn_settings, y);
}

static void build_menu(app_t *a, lv_obj_t *root)
{
    a->p_menu = panel(root, 0);
    lv_obj_t *band = lv_obj_create(a->p_menu);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, AOS_SCREEN_W, 124);
    lv_obj_set_style_bg_color(band, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_50, 0);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_CLICKABLE);
    label(a->p_menu, "TURBO", &aos_montserrat_48, ACCENT, 10, 22, AOS_SCREEN_W - 20);
    label(a->p_menu, _("carreras contra el reloj"), aos_font_body, 0xE0E4EA, 10, 80, AOS_SCREEN_W - 20);
    a->lbl_coins = label(a->p_menu, "", aos_font_body, 0xFFD040, AOS_SCREEN_W - 120, 8, 110);
    lv_obj_set_style_text_align(a->lbl_coins, LV_TEXT_ALIGN_RIGHT, 0);
    int y = 150, x = 34, w = AOS_SCREEN_W - 68;
    button(a->p_menu, _("Gira completa"), x, y, w, 46, ACCENT, menu_tour_cb, a, NULL);
    button(a->p_menu, _("Contrarreloj"), x, y + 54, w, 46, ACCENT, menu_trial_cb, a, NULL);
    a->btn_link = button(a->p_menu, "", x, y + 108, w, 46, 0x2AD8E8, menu_link_cb, a, &a->lbl_link);
    /* half-width: the body font (German "Einstellungen" does not fit the title's) */
    a->btn_garage = button(a->p_menu, _("Garage"), x, y + 162, w / 2 - 4, 42, 0xFFD040, menu_garage_cb, a, NULL);
    a->btn_settings = button(a->p_menu, _("Ajustes"), x + w / 2 + 4, y + 162, w / 2 - 4, 42, 0x9098A8, menu_settings_cb, a, NULL);
}

/* ---- stage select ---- */

/* what opens a locked stage, in words */
static void lock_hint(char *b, size_t n, int s)
{
    int after = -1;
    int rule = tb_stage_unlock(s, &after);
    if (rule == UNL_TOUR) snprintf(b, n, "%s", _("Se abre con una gira"));
    else if (rule == UNL_AFTER && after >= 0) snprintf(b, n, "%s %s", _("Terminá"), tb_stage_name(after));
    else b[0] = 0;
}

static void stage_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int s = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (!(a->unlocked & (1u << s))) {
        char h[64];
        lock_hint(h, sizeof h, s);
        snd(SND_NO);
        tba_toast(a, h);
        return;
    }
    if (a->mode == MODE_LINK) {
        a->stage = s;
        tbl_begin(a);
        return;
    }
    tba_race_start(a, s);
}

static void select_refresh(app_t *a)
{
    lv_label_set_text(a->sel_title, a->mode == MODE_LINK ? _("Elegí el tramo") : _("Contrarreloj"));
    for (int row = 0; row < STAGE_N; row++) {
        int i = tb_stage_order(row);
        lv_obj_t *b = lv_obj_get_child(a->sel_list, row);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        char t[24], r[24], txt[112];
        bool open = (a->unlocked & (1u << i)) != 0;
        if (!open) {
            char h[64];
            lock_hint(h, sizeof h, i);
            snprintf(txt, sizeof txt, LV_SYMBOL_CLOSE " %s\n%s", tb_stage_name(i), h);
        } else {
            if (a->best[i]) fmt_time(t, sizeof t, (float)a->best[i] / 10.0f);
            else snprintf(t, sizeof t, "--:--");
            int n = snprintf(txt, sizeof txt, "%s\n%s %s", tb_stage_name(i), _("récord"), t);
            if (a->rival_best[i] && a->rival_name[0]) {
                fmt_time(r, sizeof r, (float)a->rival_best[i] / 10.0f);
                snprintf(txt + n, sizeof txt - (size_t)n, " · %s %s", a->rival_name, r);
            }
        }
        lv_label_set_text(l, txt);
        lv_obj_set_style_border_color(b, lv_color_hex(open ? ACCENT : 0x505560), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(open ? 0xFFFFFF : 0x9098A8), 0);
    }
}

/* the stages as a list that scrolls: five fit on the screen, more scroll */
static void build_select(app_t *a, lv_obj_t *root)
{
    a->p_select = panel(root, 170);
    a->sel_title = label(a->p_select, "", aos_font_title, 0xFFFFFF, 0, 16, AOS_SCREEN_W);
    a->sel_list = lv_obj_create(a->p_select);
    lv_obj_remove_style_all(a->sel_list);
    lv_obj_set_size(a->sel_list, AOS_SCREEN_W, AOS_SCREEN_H - 60);
    lv_obj_set_pos(a->sel_list, 0, 60);
    lv_obj_add_flag(a->sel_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(a->sel_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a->sel_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_bottom(a->sel_list, 12, 0);
    for (int row = 0; row < STAGE_N; row++) {
        lv_obj_t *l;
        lv_obj_t *b = button(a->sel_list, "", 24, row * 74, AOS_SCREEN_W - 48, 66, ACCENT, stage_cb, a, &l);
        lv_obj_set_style_text_font(l, aos_font_body, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_user_data(b, (void *)(intptr_t)tb_stage_order(row));
        /* a drag on a row scrolls the list instead of being eaten by it */
        lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    }
}

/* ---- garage ---- */

static void garage_paint_scene(app_t *a)
{
    scene(a, a->loaded_stage >= 0 ? a->loaded_stage : 0, a->g_car, a->g_paint, 0);
}

static void garage_refresh(app_t *a)
{
    const tb_car_spec_t *sp = tb_car_spec(a->g_car);
    lv_label_set_text(a->g_name, sp->name);
    const float v[4] = { (sp->vmax - 66.0f) / 22.0f, (sp->accel - 8.0f) / 5.0f, (sp->grip - 0.7f) / 0.7f,
                         (sp->offroad + sp->tough) / 1.5f };
    for (int i = 0; i < 4; i++) lv_bar_set_value(a->g_stats[i], (int32_t)(v[i] * 100.0f), LV_ANIM_OFF);
    char b[48];
    snprintf(b, sizeof b, "%d " LV_SYMBOL_BULLET, (int)a->coins);
    lv_label_set_text(a->g_coins, b);
    bool car_owned = (a->own_cars & (1u << a->g_car)) != 0;
    bool paint_owned = (a->own_paints & (1u << a->g_paint)) != 0;
    for (int i = 0; i < tb_paint_n() && i < 16; i++) {
        lv_obj_set_style_border_width(a->g_sw[i], i == a->g_paint ? 3 : 1, 0);
        lv_obj_set_style_border_color(a->g_sw[i], lv_color_hex(i == a->g_paint ? 0xFFFFFF : 0x606060), 0);
        lv_obj_set_style_bg_opa(a->g_sw[i], (a->own_paints & (1u << i)) ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    int price = 0;
    if (!car_owned) price += sp->price;
    if (!paint_owned) price += tb_paint_price(a->g_paint);
    if (price) {
        snprintf(b, sizeof b, "%s  %d " LV_SYMBOL_BULLET, _("Comprar"), price);
        lv_label_set_text(a->g_price, "");
    } else {
        snprintf(b, sizeof b, "%s", (a->g_car == a->car && a->g_paint == a->paint[a->car]) ? _("Tu auto") : _("Elegir"));
        lv_label_set_text(a->g_price, "");
    }
    lv_label_set_text(a->g_btn_lbl, b);
}

static void garage_step(app_t *a, int d)
{
    a->g_car = (a->g_car + d + CAR_N) % CAR_N;
    a->g_paint = a->paint[a->g_car];
    snd(SND_TICK);
    garage_refresh(a);
    garage_paint_scene(a);
}

static void garage_prev_cb(lv_event_t *e) { garage_step(app_of(e), -1); }
static void garage_next_cb(lv_event_t *e) { garage_step(app_of(e), 1); }

static void swatch_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->g_paint = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    snd(SND_TICK);
    garage_refresh(a);
    garage_paint_scene(a);
}

static void garage_buy_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    const tb_car_spec_t *sp = tb_car_spec(a->g_car);
    bool car_owned = (a->own_cars & (1u << a->g_car)) != 0;
    bool paint_owned = (a->own_paints & (1u << a->g_paint)) != 0;
    int price = (car_owned ? 0 : sp->price) + (paint_owned ? 0 : tb_paint_price(a->g_paint));
    if (price > a->coins) {
        snd(SND_NO);
        tba_toast(a, _("No alcanzan las monedas"));
        return;
    }
    if (price) {
        a->coins -= price;
        a->own_cars |= 1u << a->g_car;
        a->own_paints |= 1u << a->g_paint;
        snd(SND_BUY);
    } else {
        snd(SND_TICK);
    }
    a->car = a->g_car;
    a->paint[a->car] = (uint8_t)a->g_paint;
    tba_prefs_save(a);
    garage_refresh(a);
}

static void build_garage(app_t *a, lv_obj_t *root)
{
    a->p_garage = panel(root, 0);
    lv_obj_t *top = lv_obj_create(a->p_garage);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, AOS_SCREEN_W, 176);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_60, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);
    label(a->p_garage, _("Garage"), aos_font_body, 0xFFD040, 12, 8, 120);
    a->g_coins = label(a->p_garage, "", aos_font_body, 0xFFD040, AOS_SCREEN_W - 130, 8, 118);
    lv_obj_set_style_text_align(a->g_coins, LV_TEXT_ALIGN_RIGHT, 0);
    a->g_name = label(a->p_garage, "", aos_font_title, 0xFFFFFF, 60, 34, AOS_SCREEN_W - 120);
    button(a->p_garage, LV_SYMBOL_LEFT, 8, 30, 48, 40, ACCENT, garage_prev_cb, a, NULL);
    button(a->p_garage, LV_SYMBOL_RIGHT, AOS_SCREEN_W - 56, 30, 48, 40, ACCENT, garage_next_cb, a, NULL);
    const char *names[4] = { _("Velocidad"), _("Aceleración"), _("Agarre"), _("Todo terreno") };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = label(a->p_garage, names[i], aos_font_small, 0xC8D0D8, 16, 84 + i * 22, 130);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_t *bar = lv_bar_create(a->p_garage);
        lv_obj_set_size(bar, 190, 10);
        lv_obj_set_pos(bar, 156, 89 + i * 22);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x30343C), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        a->g_stats[i] = bar;
    }
    a->g_price = label(a->p_garage, "", aos_font_small, 0xFFD040, 0, 176, AOS_SCREEN_W);
    /* the paints: one row of swatches, A over B */
    int n = tb_paint_n();
    int sw = 26, gap = (AOS_SCREEN_W - 16 - n * sw) / (n > 1 ? n - 1 : 1);
    for (int i = 0; i < n && i < 16; i++) {
        tb_paint_t p;
        tb_paint_get(i, &p);
        lv_obj_t *s = lv_obj_create(a->p_garage);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, sw, sw);
        lv_obj_set_pos(s, 8 + i * (sw + gap), 354);
        lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s, lv_color_hex(p.c[RG_PAINT_A]), 0);
        lv_obj_set_style_bg_grad_color(s, lv_color_hex(p.c[RG_PAINT_B]), 0);
        lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s, 1, 0);
        lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s, 4);
        lv_obj_set_user_data(s, (void *)(intptr_t)i);
        lv_obj_add_event_cb(s, swatch_cb, LV_EVENT_CLICKED, a);
        a->g_sw[i] = s;
    }
    a->g_btn = button(a->p_garage, "", 60, 392, AOS_SCREEN_W - 120, 46, 0xFFD040, garage_buy_cb, a, &a->g_btn_lbl);
}

/* ---- settings ---- */

static void chip_style(lv_obj_t *c, bool on)
{
    lv_obj_set_style_bg_color(c, lv_color_hex(on ? ACCENT : 0x14161C), 0);
}

static void settings_refresh(app_t *a)
{
    for (int i = 0; i < DIFF_N; i++) chip_style(a->chip_diff[i], i == a->diff);
    for (int i = 0; i < 3; i++) chip_style(a->chip_sens[i], i == a->sens);
    chip_style(a->chip_sfx, s_sfx);
}

static void diff_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->diff = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    settings_refresh(a);
    tba_prefs_save(a);
}

static void sens_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->sens = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    settings_refresh(a);
    tba_prefs_save(a);
}

static void sfx_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    s_sfx = !s_sfx;
    a->sfx = s_sfx;
    if (s_sfx && !tb_audio_is_open()) tb_audio_open();
    if (!s_sfx) tb_audio_close();
    settings_refresh(a);
    tba_prefs_save(a);
}

static void build_settings(app_t *a, lv_obj_t *root)
{
    a->p_settings = panel(root, 200);
    label(a->p_settings, _("Ajustes"), aos_font_title, 0xFFFFFF, 0, 16, AOS_SCREEN_W);
    label(a->p_settings, _("Dificultad"), aos_font_body, 0xC8D0D8, 0, 66, AOS_SCREEN_W);
    const char *dn[DIFF_N] = { _("Fácil"), _("Normal"), _("Difícil") };
    for (int i = 0; i < DIFF_N; i++) {
        a->chip_diff[i] = button(a->p_settings, dn[i], 16 + i * 114, 96, 108, 42, ACCENT, diff_cb, a, NULL);
        lv_obj_set_user_data(a->chip_diff[i], (void *)(intptr_t)i);
    }
    label(a->p_settings, _("Volante (inclinar el reloj)"), aos_font_body, 0xC8D0D8, 0, 162, AOS_SCREEN_W);
    const char *sn[3] = { _("Suave"), _("Medio"), _("Rápido") };
    for (int i = 0; i < 3; i++) {
        a->chip_sens[i] = button(a->p_settings, sn[i], 16 + i * 114, 192, 108, 42, ACCENT, sens_cb, a, NULL);
        lv_obj_set_user_data(a->chip_sens[i], (void *)(intptr_t)i);
    }
    a->chip_sfx = button(a->p_settings, _("Sonido"), 60, 270, AOS_SCREEN_W - 120, 46, ACCENT, sfx_cb, a, NULL);
    label(a->p_settings, _("El volante se centra solo en la cuenta regresiva: sostené el reloj como vas a manejar."),
          aos_font_small, 0x98A0A8, 20, 340, AOS_SCREEN_W - 40);
}

/* ---- loading, results, pause, lobby ---- */

static void again_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (a->mode == MODE_LINK) {
        tbl_begin(a);
        return;
    }
    if (a->mode == MODE_TOUR) {
        tour_begin(a);
        return;
    }
    tba_race_start(a, a->stage);
}

static void next_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if ((intptr_t)lv_obj_get_user_data(a->res_btn_next)) {
        a->tour_i++;
        tba_race_start(a, tb_tour_stage(a->tour_i));
        return;
    }
    if (a->mode == MODE_LINK) tbl_end(a);
    tba_set_state(a, ST_MENU);
}

static void resume_cb(lv_event_t *e) { resume(app_of(e)); }

static void restart_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->racing = false;
    if (a->mode == MODE_LINK) {
        resume(a);
        return;
    }
    race_go(a);
}

static void quit_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->racing = false;
    a->paused = false;
    if (a->mode == MODE_LINK) tbl_end(a);
    tba_set_state(a, ST_MENU);
}

static void lobby_cancel_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    tbl_end(a);
    tba_set_state(a, ST_MENU);
}

static void build_misc(app_t *a, lv_obj_t *root)
{
    a->p_loading = panel(root, 150);
    a->load_lbl = label(a->p_loading, "", aos_font_title, 0xFFFFFF, 0, 190, AOS_SCREEN_W);

    a->p_result = panel(root, 225);     /* dark enough to hide the HUD's banner behind */
    a->res_title = label(a->p_result, "", &aos_montserrat_36, ACCENT, 0, 24, AOS_SCREEN_W);
    a->res_body = label(a->p_result, "", aos_font_body, 0xFFFFFF, 16, 84, AOS_SCREEN_W - 32);
    lv_obj_set_style_text_line_space(a->res_body, 6, 0);
    button(a->p_result, _("Otra vez"), 20, 386, 158, 48, ACCENT, again_cb, a, NULL);
    a->res_btn_next = button(a->p_result, "", 190, 386, 158, 48, 0x2AD8E8, next_cb, a, &a->res_btn_next_lbl);

    a->p_pause = panel(root, 170);
    label(a->p_pause, _("Pausa"), aos_font_title, 0xFFFFFF, 0, 80, AOS_SCREEN_W);
    button(a->p_pause, _("Seguir"), 54, 150, AOS_SCREEN_W - 108, 50, ACCENT, resume_cb, a, NULL);
    button(a->p_pause, _("Reiniciar"), 54, 214, AOS_SCREEN_W - 108, 50, 0xFFD040, restart_cb, a, NULL);
    button(a->p_pause, _("Salir al menú"), 54, 278, AOS_SCREEN_W - 108, 50, 0x9098A8, quit_cb, a, NULL);

    a->p_lobby = panel(root, 170);
    label(a->p_lobby, _("Contra el otro reloj"), aos_font_title, 0x2AD8E8, 0, 90, AOS_SCREEN_W);
    a->lobby_lbl = label(a->p_lobby, "", aos_font_body, 0xFFFFFF, 20, 160, AOS_SCREEN_W - 40);
    button(a->p_lobby, _("Cancelar"), 84, 360, AOS_SCREEN_W - 168, 48, 0x9098A8, lobby_cancel_cb, a, NULL);

    a->p_boot = panel(root, 0);
    lv_obj_set_style_bg_color(a->p_boot, lv_color_hex(0x05070C), 0);
    lv_obj_set_style_bg_opa(a->p_boot, LV_OPA_COVER, 0);
    label(a->p_boot, "TURBO", &aos_montserrat_48, ACCENT, 0, 150, AOS_SCREEN_W);
    a->boot_lbl = label(a->p_boot, _("Cargando..."), aos_font_body, 0xC8D0D8, 0, 230, AOS_SCREEN_W);
    a->boot_bar = lv_bar_create(a->p_boot);
    lv_obj_set_size(a->boot_bar, 220, 8);
    lv_obj_set_pos(a->boot_bar, 74, 270);
    lv_bar_set_range(a->boot_bar, 0, 100);
    lv_obj_set_style_bg_color(a->boot_bar, lv_color_hex(0x30343C), 0);
    lv_obj_set_style_bg_color(a->boot_bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
}

/* --------------------------------------------------------------------------
 * Touch, gestures, the timer
 * -------------------------------------------------------------------------- */

static void touch_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (a->closing || a->state != ST_RACE || a->paused) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->gas = a->brake = false;
        return;
    }
    if (code == LV_EVENT_PRESSED && pt.x < TB_PAUSE_R && pt.y < TB_PAUSE_R) {
        pause_show(a);
        return;
    }
    /* the pedals: generous zones, the lower half of each side */
    bool low = pt.y >= TB_PEDAL_Y - 40;
    a->brake = low && pt.x < TB_W / 2 - 30;
    a->gas = low && pt.x > TB_W / 2 + 30;
}

static void handle_gesture(app_t *a, int dir)
{
    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - a->last_gesture_ms) < 400) return;
    a->last_gesture_ms = now;
    if (dir != LV_DIR_RIGHT) return;
    switch (a->state) {
    case ST_MENU: a->want_exit = true; break;
    case ST_SELECT: case ST_GARAGE: case ST_SETTINGS: tba_set_state(a, ST_MENU); break;
    default: break;
    }
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev || a->state == ST_RACE) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT) handle_gesture(a, (int)dir);
}

static void boot_tick(app_t *a)
{
    int v = (int)lv_bar_get_value(a->boot_bar);
    if (v < 90) lv_bar_set_value(a->boot_bar, v + 2, LV_ANIM_OFF);
    if (a->job_done && a->job == JOB_NONE) {
        a->job_done = false;
        lv_bar_set_value(a->boot_bar, 100, LV_ANIM_OFF);
        canvas_show(a, 0);
        tba_set_state(a, ST_MENU);
        if (a->dev_go >= 0 && a->dev_go < STAGE_N) {
            a->mode = MODE_TRIAL;
            tba_race_start(a, a->dev_go);
        }
#ifdef AOS_SIM_BUILTIN
        /* Development switches (getenv() is NULL on the board):
         *   TB_STAGE=0..6 straight into a time trial of that stage
         *   TB_TOUR=1     straight into the tour
         *   TB_AUTO=1     the bot drives
         *   TB_COINS=n    coins for the garage
         *   TB_SCREEN=garage|settings|select
         */
        const char *e;
        if ((e = getenv("TB_COINS")) && e[0]) a->coins = atoi(e);
        if ((e = getenv("TB_AUTO")) && e[0]) a->autoplay = true;
        if ((e = getenv("TB_SCREEN")) && e[0]) {
            if (e[0] == 'g') tba_set_state(a, ST_GARAGE);
            else if (e[0] == 's' && e[1] == 'e' && e[2] == 't') tba_set_state(a, ST_SETTINGS);
            else if (e[0] == 's') { a->mode = MODE_TRIAL; tba_set_state(a, ST_SELECT); }
        }
        if ((e = getenv("TB_LINKGO")) && e[0]) {
            /* TB_LINKGO=<stage>: straight into the lobby (with TB_LINK=1
             * and AOS_SIM_LINK_PORT/PARTNER crossed on two simulators) */
            a->mode = MODE_LINK;
            a->unlocked = (1u << STAGE_N) - 1;
            a->stage = atoi(e) % STAGE_N;
            tbl_begin(a);
        } else if ((e = getenv("TB_TOUR")) && e[0]) {
            tour_begin(a);
        } else if ((e = getenv("TB_STAGE")) && e[0]) {
            a->mode = MODE_TRIAL;
            a->unlocked = (1u << STAGE_N) - 1;
            tba_race_start(a, atoi(e) % STAGE_N);
        }
#endif
    }
}

static void frame(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;
    }
    if ((aos_touch_gesture_t)aos_ui_take_gesture() == AOS_TOUCH_GESTURE_RIGHT && a->state != ST_RACE) {
        handle_gesture(a, LV_DIR_RIGHT);
    }
    uint64_t now = aos_hal_uptime_ms();
    int dt = a->prev_ms ? (int)(uint32_t)(now - a->prev_ms) : TICK_MS;
    if (dt > 100) dt = 100;
    a->prev_ms = now;
    a->st_ms += (uint32_t)dt;

    tb_audio_tick();
    if (a->link_on) tbl_tick(a);

    switch (a->state) {
    case ST_BOOT:
        boot_tick(a);
        break;
    case ST_MENU:
    case ST_GARAGE:
        if (a->job_done && a->job == JOB_NONE) {
            a->job_done = false;
            canvas_show(a, 0);
        }
        break;
    case ST_LOADING:
        if (a->job_done && a->job == JOB_NONE && !tbl_hold_loading(a)) {
            a->job_done = false;
            if (a->loaded_stage == a->stage) race_go(a);
            else {
                tba_toast(a, _("No hay memoria para el tramo"));
                tba_set_state(a, ST_MENU);
            }
        }
        break;
    case ST_RACE:
        if (!a->paused) {
            read_tilt(a);
            push_frame(a);
        }
        race_tick(a, dt);
        break;
    case ST_RESULT:
        if (a->mode == MODE_LINK && a->rival_done && !a->res_rival_seen) result_text(a);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return false;
    switch (a->state) {
    case ST_MENU:
    case ST_BOOT:
        return false;
    case ST_RACE:
        if (a->paused) resume(a);
        else pause_show(a);
        return true;
    case ST_LOBBY:
        tbl_end(a);
        tba_set_state(a, ST_MENU);
        return true;
    case ST_LOADING:
        return true;
    case ST_RESULT:
        if (a->mode == MODE_LINK) tbl_end(a);
        tba_set_state(a, ST_MENU);
        return true;
    default:
        tba_set_state(a, ST_MENU);
        return true;
    }
}

static void turbo_hide(aos_app_t *self, void *inst)
{
    (void)self;
    if (inst) pause_show((app_t *)inst);
}

static void free_all(app_t *a)
{
    for (int i = 0; i < TB_NFB; i++) {
        free(a->fb[i]);
        a->fb[i] = NULL;
    }
    free(a->cv);
    a->cv = NULL;
    free(a->band);
    a->band = NULL;
    tb_track_free(&a->trk);
    tb_render_free(a->ren);
    a->ren = NULL;
    tb_hud_free(&a->hud);
    tb_art_close();
}

static void *turbo_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("turbo", "opening | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        a->fb[i] = (uint16_t *)tb_malloc((size_t)TB_W * TB_H * 2);
        if (!a->fb[i]) ok = false;
    }
    a->nfb = 3;
    a->cv = (uint16_t *)tb_malloc((size_t)TB_W * TB_H * 2);
    a->band = (uint16_t *)tb_malloc_internal((size_t)TB_W * TB_BAND * 2);
    a->ren = tb_render_new();
    if (!ok || !a->ren || !a->cv) {
        aos_hal_log("turbo", "out of memory");
        free_all(a);
        lv_free(a);
        return NULL;
    }
    memset(a->fb[0], 0, (size_t)TB_W * TB_H * 2);
    a->loaded_stage = -1;
    a->shown = -1;
    a->root = root;
    prefs_load(a);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    memset(a->cv, 0, (size_t)TB_W * TB_H * 2);
    lv_canvas_set_buffer(a->canvas, a->cv, TB_W, TB_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);

    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESS_LOST, a);

    hud_build(a);
    build_menu(a, root);
    build_select(a, root);
    build_garage(a, root);
    build_settings(a, root);
    build_misc(a, root);

    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    /* a development switch on the card, for measuring on the board:
     * apps/turbo_dev.txt with "auto" (the bot drives), "unlock", "reset",
     * and "go N" (a time trial of stage N as soon as the app has loaded) */
    a->dev_go = -1;
    {
        char path[96], buf[64] = "";
        snprintf(path, sizeof path, "%s/turbo_dev.txt", aos_hal_path_apps());
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            if (strstr(buf, "auto")) a->autoplay = true;
            if (strstr(buf, "unlock")) a->unlocked = (1u << STAGE_N) - 1;
            const char *go = strstr(buf, "go ");
            if (go) a->dev_go = (int8_t)atoi(go + 3);
            if (strstr(buf, "reset")) {
                /* back to a fresh install: what the bot's measuring races left */
                a->coins = 0;
                a->own_cars = a->own_paints = 1;
                a->car = 0;
                memset(a->paint, 0, sizeof a->paint);
                a->unlocked = tb_stage_open_mask();
                a->best_tour = 0;
                memset(a->best, 0, sizeof a->best);
                memset(a->rival_best, 0, sizeof a->rival_best);
                a->rival_name[0] = 0;
                tba_prefs_save(a);
            }
            aos_hal_log("turbo", "dev switches: %s", buf);
        }
    }
    if (s_sfx) tb_audio_open();
    a->state = ST_BOOT;
    show_panel(a, a->p_boot);
    a->job_stage = STAGE_CITY;
    a->scene_car = a->car;
    a->scene_paint = a->paint[a->car];
    a->job = JOB_BOOT;
    /* core 0: LVGL is pinned to core 1 and pushes the frames; the render
     * must not keep it off the CPU (aos_hal.h, aos_hal_worker_start_on) */
    if (!aos_hal_worker_start_on("turbo", worker_fn, a, WORKER_STACK, 0, 5)) {
        aos_hal_log("turbo", "no worker");
    }
    a->timer = lv_timer_create(frame, TICK_MS, a);

    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("turbo", "ready | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    return a;
}

static void turbo_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    a->closing = true;
    a->racing = false;
    if (a->timer) lv_timer_delete(a->timer);
    aos_hal_worker_stop();
    tb_audio_close();
    tbl_end(a);
    if (a->root) lv_obj_clean(a->root);
    tba_prefs_save(a);
    free_all(a);
    lv_free(a);
}

/* The launcher icon: a red wedge car from behind on a road to the horizon. */
static const uint8_t TURBO_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER,   0,  22, 84, 30, 4,          AIC_C_LIT(0x50545C), 255),
    AIC_RECT(AIC_CENTER,   0,  22,  4, 26, 1,          AIC_C_TEXT,          255),
    AIC_RECT(AIC_CENTER,   0,   2, 60, 22, 8,          AIC_C_LIT(0xE02020), 255),
    AIC_RECT(AIC_CENTER,   0,  -8, 36, 10, 4,          AIC_C_LIT(0x1A2230), 255),
    AIC_RECT(AIC_CENTER, -20,   6, 14,  5, 2,          AIC_C_LIT(0xFFB020), 255),
    AIC_RECT(AIC_CENTER,  20,   6, 14,  5, 2,          AIC_C_LIT(0xFFB020), 255),
    AIC_RECT(AIC_CENTER, -24,  16, 12,  8, 2,          AIC_C_LIT(0x101010), 255),
    AIC_RECT(AIC_CENTER,  24,  16, 12,  8, 2,          AIC_C_LIT(0x101010), 255),
    AIC_END
};

static bool turbo_init(aos_app_t *app)
{
    app->desc.id       = "demo.turbo";
    app->desc.name     = "Turbo";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, TURBO_ICON, sizeof TURBO_ICON);
    app->desc.color_a  = 0xE0501E;
    app->desc.color_b  = 0x3A1060;
    app->desc.order    = 159;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;

    app->create  = turbo_create;
    app->destroy = turbo_destroy;
    app->hide    = turbo_hide;
    app->back    = app_back;
    return true;
}

AOS_APP_ENTRY(turbo_init);
