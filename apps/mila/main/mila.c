/*
 * MILA - a sweet black kitten pushes things back to their place, one cell
 * at a time, around the house: the app (see ml_app.h for the map of files)
 *
 * Every word on screen is wrapped in _(): the LVGL panels directly, and the
 * words inside the frames are rendered from _() into masks, so they follow
 * the language too. World and level names come from the pack in the three
 * languages (ml_level.h), so a new world needs no new strings in the code.
 */
#include "ml_app.h"
#include "ml_audio.h"
#include "ml_casita.h"
#include "ml_link.h"
#include "ml_map.h"
#include "ml_ui.h"

#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"
#include "aos_gesture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS         8
#define WORKER_STACK    (12 * 1024)
#define SWIPE_PX        22
#define REPEAT_MS       190
#define REPEAT_FIRST_MS 550         /* a swipe held this long starts repeating */
#define PEEK_MS         380
#define ZOOM_S          0.9f

/* ---- worlds and levels ---- */

bool mla_world_open(const app_t *a, int world)
{
    if (world < 0 || world >= a->worlds.nworlds) return false;
    if (a->dev_unlock) return true;
    return ml_prog_total_stars(&a->prog, &a->worlds) >= a->worlds.w[world].need;
}

bool mla_level_open(const app_t *a, int world, int level)
{
    if (!mla_world_open(a, world)) return false;
    if (level < 0 || level >= a->worlds.w[world].nlevels) return false;
    if (a->dev_unlock || level == 0) return true;
    return a->prog.stars[world][level - 1] > 0;
}

const char *mla_world_name(const app_t *a, int world, char *buf, int n)
{
    buf[0] = 0;
    if (world < 0 || world >= a->worlds.nworlds) return buf;
    return ml_pick_lang(a->worlds.w[world].name, buf, n);
}

const char *mla_level_name(const app_t *a, int world, int level, char *buf, int n)
{
    buf[0] = 0;
    if (world < 0 || world >= a->worlds.nworlds || level < 0 || level >= a->worlds.w[world].nlevels) return buf;
    return ml_pick_lang(a->worlds.w[world].lv[level].title, buf, n);
}

void mla_save(app_t *a)
{
    ml_prog_save(&a->prog, &a->worlds);
}

/* --------------------------------------------------------------------------
 * The worker
 * -------------------------------------------------------------------------- */

static uint64_t s_last_yield;

static void worker_yield(void)
{
    uint64_t now = aos_hal_uptime_ms();
    if ((uint32_t)(now - s_last_yield) > 1000) {
        aos_hal_worker_sleep(10);
        s_last_yield = aos_hal_uptime_ms();
    }
}

static uint32_t clock_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static void level_free(app_t *a)
{
    a->level_ok = false;
    ml_play_free(&a->play);
    ml_world_free(&a->w);
    free(a->ov);
    a->ov = NULL;
}

static void paint_overview(app_t *a, bool things)
{
    static ml_ov_item_t extra[48];
    int n = things ? ml_play_ov_items(&a->play, extra, 48) : 0;
    ml_overview_paint(&a->w, &a->ovv, a->ov, extra, n);
}

static bool load_level(app_t *a, int wi, int li)
{
    level_free(a);
    const ml_world_info_t *wd = &a->worlds.w[wi];
    int n1, n2;
    ml_kit_counts(wd->kit, &n1, &n2);
    if (!ml_level_load(&a->lv, wd->lv[li].id, n1, n2)) {
        aos_hal_log("mila", "level %s: missing or malformed", wd->lv[li].id);
        return false;
    }
    a->ov = (uint16_t *)ml_malloc((size_t)ML_W * ML_H * 2);
    if (!a->ov) return false;
    /* the rules' state lives in play; the world reads it */
    if (!ml_world_init(&a->w, &a->lv, &a->play.st, wd->kit)) {
        aos_hal_log("mila", "level %s: no memory for the cache", wd->lv[li].id);
        return false;
    }
    ml_mila_load(&a->mila, ML_SET_GAME, a->prog.hat, (uint32_t)a->prog.hat_col, a->prog.neck,
                 (uint32_t)a->prog.neck_col);
    ml_play_init(&a->play, &a->lv, &a->w, &a->mila, wd->kit);
    ml_world_sync(&a->w);
    ml_play_camera(&a->play, 0, true);
    ml_overview_fit(&a->w, ML_W, ML_H, 92, 58, &a->ovv);
    paint_overview(a, true);
    a->world = wi;
    a->level = li;
    a->lmode = LM_OVERVIEW;
    a->mode_t = 0;
    a->result_shown = false;
    a->level_ok = true;
    return true;
}

static void run_job(app_t *a, int j)
{
    uint32_t hi = 0, hp = 0;
    uint64_t t0 = aos_hal_uptime_ms();
    switch (j) {
    case JOB_BOOT: {
        char path[96];
        snprintf(path, sizeof path, "%s/mila.pak", aos_hal_path_apps());
        bool ok = ml_art_open(path) && ml_worlds_load(&a->worlds);
        if (ok) {
            ml_ui_job(a, UJ_ICONS);
            ml_hud_icons_load(&a->hud);
        }
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mila", "pack %s, %d worlds in %u ms | psram %u", ok ? "open" : "MISSING", a->worlds.nworlds,
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hp);
        a->job_ok = ok;
        break;
    }
    case JOB_LEVEL:
        mlc_close(a);
        mlm_close(a);
        a->job_ok = load_level(a, a->job_world, a->job_level);
        if (a->job_ok) {
            a->scene_seq = a->seq;
            a->scene = SC_LEVEL;
        }
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mila", "level %s %s in %u ms, art %u KB | psram %u",
                    a->worlds.w[a->job_world].lv[a->job_level].id, a->job_ok ? "ok" : "FAILED",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)(ml_art_bytes() / 1024), (unsigned)hp);
        break;
    case JOB_LEAVE:
        a->scene = SC_NONE;
        level_free(a);
        a->job_ok = true;
        break;
    case JOB_PEEK:
        paint_overview(a, true);
        a->peek_ready = true;
        aos_hal_log("mila", "peek painted in %u ms", (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0));
        a->job_ok = true;
        break;
    case JOB_UI:
        ml_ui_job(a, a->ui_job);
        a->job_ok = true;
        break;
    case JOB_OUTFIT:
        ml_mila_load(&a->mila, a->mila.sets, a->prog.hat, (uint32_t)a->prog.hat_col, a->prog.neck,
                     (uint32_t)a->prog.neck_col);
        a->job_ok = true;
        break;
    case JOB_CASITA:
        a->scene = SC_NONE;
        level_free(a);
        mlm_close(a);
        a->job_ok = mlc_open(a);
        if (a->job_ok) {
            a->scene_seq = a->seq;
            a->scene = SC_CASITA;
        }
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mila", "casita in %u ms | psram %u", (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hp);
        break;
    case JOB_MAP:
        a->scene = SC_NONE;
        level_free(a);
        mlc_close(a);
        a->job_ok = mlm_open(a);
        if (a->job_ok) {
            a->scene_seq = a->seq;
            a->scene = SC_MAP;
        }
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mila", "map in %u ms | psram %u", (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hp);
        break;
    default:
        break;
    }
}

static int free_fb(app_t *a)
{
    for (int i = 0; i < ML_NFB; i++)
        if (a->fb_state[i] == FB_FREE) return i;
    return -1;
}

/* ---- a level's frame ---- */

static float ease(float u)
{
    if (u <= 0) return 0;
    if (u >= 1) return 1;
    return u * u * (3 - 2 * u);
}

static void level_step(app_t *a, float dt)
{
    ml_play_t *p = &a->play;
    a->mode_t += dt;
    switch (a->lmode) {
    case LM_ZOOM:
        ml_play_camera(p, dt, true);
        if (a->mode_t >= ZOOM_S) {
            a->lmode = LM_PLAY;
            a->mode_t = 0;
        }
        break;
    case LM_PLAY:
    case LM_PEEK:
    case LM_WON:
        if (a->lmode != LM_PEEK) ml_play_step(p, dt);
        ml_play_camera(p, dt, false);
        if (p->won && a->lmode == LM_PLAY) {
            a->lmode = LM_WON;
            a->mode_t = 0;
        }
        break;
    default:
        break;
    }
    if (a->hud_flash_undo > 0) a->hud_flash_undo -= dt;
    if (a->hud_flash_restart > 0) a->hud_flash_restart -= dt;
    uint32_t ev = p->events;
    p->events = 0;
    if (ev) {
        a->ev_ring[a->ev_w & 15] = ev;
        a->ev_w++;
    }
}

static void level_band(app_t *a, ml_img_t *im, int y0, int y1)
{
    ml_play_t *p = &a->play;
    int mode = a->lmode;
    bool peek = mode == LM_PEEK && a->peek_ready;
    if (mode == LM_OVERVIEW || peek) {
        for (int y = y0; y < y1; y++) memcpy(im->px + (size_t)y * im->w, a->ov + (size_t)y * ML_W, ML_W * 2);
        if (mode == LM_OVERVIEW) ml_hud_overview(&a->hud, im, a->lv.par, a->mode_t, a->mode_t > 0.6f);
        return;
    }
    ml_render_band(&a->w, im, p->icam_x, p->icam_y, y0, y1, &a->dl);
    if (mode == LM_ZOOM) {
        float e = ease(a->mode_t / ZOOM_S);
        /* the whole-level picture flies in; the real frame fades in at the end */
        float s1 = 1.0f / a->ovv.scale;
        float s = powf(s1, e);                  /* ov px per screen px, 1 -> 1/scale */
        (void)s;
        float gx, gy;
        ml_play_mila_pos(p, &gx, &gy);
        float cx1 = ml_lpx(&a->w, gx), cy1 = ml_lpy(&a->w, gy, 0.3f);
        float cx0 = (ML_W / 2.0f - a->ovv.ox) / a->ovv.scale, cy0 = (ML_H / 2.0f - a->ovv.oy) / a->ovv.scale;
        float cx = cx0 + (cx1 - cx0) * e, cy = cy0 + (cy1 - cy0) * e;
        float sc = a->ovv.scale * powf(1.0f / a->ovv.scale, e);  /* screen px per LP px */
        int k = e < 0.6f ? 255 : (int)(255 * (1 - (e - 0.6f) / 0.4f));
        ml_zoom_band(a->ov, &a->ovv, im, y0, y1, cx, cy, sc, k);
        return;
    }
    ml_hud_level_t hs = {
        .moves = p->moves, .par = a->lv.par, .on = ml_play_on_target(p), .targets = a->lv.map.ntargets,
        .msg_t = mode == LM_WON ? a->mode_t : -1, .undo_flash = a->hud_flash_undo,
        .restart_flash = a->hud_flash_restart,
        .race = a->race, .rival_on = a->rival_on, .rival_moves = a->rival_moves,
    };
    ml_hud_level(&a->hud, im, &hs);
}

static void render_frame(app_t *a, int i, float dt)
{
    int sc = a->scene;
    if (sc == SC_LEVEL && a->level_ok) {
        level_step(a, dt);
        ml_play_draw(&a->play, &a->dl);
        static int px, py;
        int dirx = a->play.icam_x > px ? 1 : a->play.icam_x < px ? -1 : 0;
        int diry = a->play.icam_y > py ? 1 : a->play.icam_y < py ? -1 : 0;
        px = a->play.icam_x;
        py = a->play.icam_y;
        if (a->lmode != LM_OVERVIEW) ml_world_prepare(&a->w, a->play.icam_x, a->play.icam_y, dirx, diry, 2);
    } else if (sc == SC_CASITA) {
        mlc_step(a, dt);
    } else if (sc == SC_MAP) {
        mlm_step(a, dt);
    }
    for (int y0 = 0; y0 < ML_H; y0 += ML_BAND) {
        int y1 = y0 + ML_BAND > ML_H ? ML_H : y0 + ML_BAND;
        ml_img_t bim;
        uint16_t *band = a->band ? a->band : a->fb[i] + (size_t)y0 * ML_W;
        ml_img_init(&bim, band - (size_t)y0 * ML_W, ML_W, ML_H);
        ml_img_clip(&bim, 0, y0, ML_W, y1);
        if (sc == SC_LEVEL && a->level_ok) level_band(a, &bim, y0, y1);
        else if (sc == SC_CASITA) mlc_band(a, &bim, y0, y1);
        else if (sc == SC_MAP) mlm_band(a, &bim, y0, y1);
        else ml_rect(&bim, 0, y0, ML_W, y1 - y0, 0);
        ml_copy_swap(a->fb[i] + (size_t)y0 * ML_W, band, (size_t)(y1 - y0) * ML_W);
    }
}

static void play_frame(app_t *a)
{
    int i = free_fb(a);
    if (i < 0) {
        aos_hal_worker_sleep(10);
        s_last_yield = aos_hal_uptime_ms();
        return;
    }
    a->fb_state[i] = FB_BUSY;
    uint64_t now = aos_hal_uptime_ms();
    float dt = a->w_last_ms ? (float)(uint32_t)(now - a->w_last_ms) / 1000.0f : 0.033f;
    if (dt > 0.1f) dt = 0.1f;
    a->w_last_ms = now;
    render_frame(a, i, dt);
    a->w_frames++;
    a->fb_seq[i] = ++a->seq;
    a->fb_state[i] = FB_READY;
    worker_yield();
}

static void worker_fn(void *arg)
{
    app_t *a = (app_t *)arg;
    ml_set_clock(clock_ms);
    ml_set_yield(worker_yield);
    while (!aos_hal_worker_should_stop()) {
        if (a->job) {
            s_last_yield = aos_hal_uptime_ms();
            run_job(a, a->job);
            a->job = JOB_NONE;
            a->job_done = true;
            continue;
        }
        if (a->scene != SC_NONE && a->state != ST_PAUSE && a->state != ST_RESULT && a->state != ST_SHOP &&
            a->state != ST_SETTINGS && a->state != ST_LOADING && a->state != ST_LOBBY) {
            play_frame(a);
            continue;
        }
        a->w_last_ms = 0;
        aos_hal_worker_sleep(20);
    }
    ml_set_yield(NULL);
}

static int s_last_job;

static bool job(app_t *a, int j)
{
    if (a->job != JOB_NONE) return false;
    a->job_done = false;
    a->job_ok = false;
    s_last_job = j;
    a->job = j;
    return true;
}

/* --------------------------------------------------------------------------
 * Frames to the panel
 * -------------------------------------------------------------------------- */

static void canvas_show(app_t *a, int i)
{
    ml_copy_swap(a->cv, a->fb[i], (size_t)ML_W * ML_H);
    lv_obj_invalidate(a->canvas);
}

static void push_frame(app_t *a)
{
    int best = -1;
    uint32_t bs = 0;
    for (int i = 0; i < ML_NFB; i++) {
        if (a->fb_state[i] == FB_READY && (best < 0 || a->fb_seq[i] > bs)) {
            best = i;
            bs = a->fb_seq[i];
        }
    }
    if (best < 0) return;
    for (int i = 0; i < ML_NFB; i++)
        if (i != best && a->fb_state[i] == FB_READY) a->fb_state[i] = FB_FREE;
    if (!aos_hal_display_blit(0, 0, ML_W, ML_H, a->fb[best])) canvas_show(a, best);
    if (a->shown >= 0 && a->shown != best) a->fb_state[a->shown] = FB_FREE;
    a->fb_state[best] = FB_SHOWN;
    a->shown = best;
}

/* ---- words into masks (LVGL thread) ---- */

bool mla_text_mask(ml_mask_t *m, const char *txt, const lv_font_t *font)
{
    lv_point_t sz;
    free(m->a);
    memset(m, 0, sizeof(*m));
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = sz.x + 2, h = sz.y;
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
    m->a = (uint8_t *)ml_malloc((size_t)w * h);
    if (m->a) {
        m->w = (int16_t)w;
        m->h = (int16_t)h;
        for (int y = 0; y < h; y++) memcpy(m->a + (size_t)y * w, db->data + (size_t)y * db->header.stride, (size_t)w);
    }
    lv_obj_delete(c);
    lv_draw_buf_destroy(db);
    return m->a != NULL;
}

static void hud_build(app_t *a)
{
    static const char digits[] = "0123456789:/";
    char s[2] = { 0, 0 };
    for (int i = 0; i < 12; i++) {
        s[0] = digits[i];
        mla_text_mask(&a->hud.dig[i], s, aos_font_title);
        mla_text_mask(&a->hud.sdig[i], s, aos_font_body);
    }
    mla_text_mask(&a->hud.msg[MSG_TAP], _("Tocá para empezar"), aos_font_body);
    mla_text_mask(&a->hud.msg[MSG_PAR], _("Par"), aos_font_body);
    mla_text_mask(&a->hud.msg[MSG_WELL], _("¡Muy bien!"), aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_PAUSE], LV_SYMBOL_PAUSE, aos_font_body);
    mla_text_mask(&a->hud.sym[SYM_UNDO], LV_SYMBOL_PREV, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_RESTART], LV_SYMBOL_REFRESH, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_PLAY], LV_SYMBOL_PLAY, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_HOME], LV_SYMBOL_HOME, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_SHOP], LV_SYMBOL_TINT, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_GEAR], LV_SYMBOL_SETTINGS, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_FRIEND], LV_SYMBOL_WIFI, aos_font_title);
    mla_text_mask(&a->hud.sym[SYM_LOCK], LV_SYMBOL_EYE_CLOSE, aos_font_body);
    mla_text_mask(&a->hud.msg[MSG_PLAY], _("Jugar"), aos_font_small);
    mla_text_mask(&a->hud.msg[MSG_SHOP], _("Tienda"), aos_font_small);
    mla_text_mask(&a->hud.msg[MSG_SETTINGS], _("Ajustes"), aos_font_small);
    mla_text_mask(&a->hud.msg[MSG_FRIEND], _("Amigo"), aos_font_small);
    mla_text_mask(&a->hud.msg[MSG_SOON], _("Próximamente"), aos_font_body);
}

/* --------------------------------------------------------------------------
 * States
 * -------------------------------------------------------------------------- */

/* ---- the loader's bar ---- */

static void load_begin(app_t *a, const char *key, uint32_t guess)
{
    int32_t v = 0;
    snprintf(a->ld_key, sizeof a->ld_key, "%s", key);
    a->ld_from = ml_art_read();
    a->ld_est = aos_hal_pref_get_i32(key, &v) && v > 0 ? (uint32_t)v : guess;
    if (a->ld_est < 1) a->ld_est = 1;
}

static int load_pct(const app_t *a)
{
    return (int)((uint64_t)(ml_art_read() - a->ld_from) * 100 / a->ld_est);
}

static void load_end(app_t *a)
{
    if (!a->ld_key[0]) return;
    /* what it really read is next time's measure */
    uint32_t used = ml_art_read() - a->ld_from;
    uint32_t d = used > a->ld_est ? used - a->ld_est : a->ld_est - used;
    if (used > 0 && d * 20 > a->ld_est) aos_hal_pref_set_i32(a->ld_key, (int32_t)used);
    a->ld_key[0] = 0;
}

static int scene_of(int st)
{
    return st == ST_CASITA ? SC_CASITA : st == ST_MAP ? SC_MAP : st == ST_LEVEL ? SC_LEVEL : SC_NONE;
}

static void music_for(app_t *a, int st)
{
    switch (st) {
    case ST_CASITA: case ST_SHOP: case ST_SETTINGS: case ST_LOBBY: ml_music(MUS_CASITA); break;
    case ST_MAP: ml_music(MUS_MAP); break;
    case ST_LEVEL: case ST_PAUSE: {
        int w = a->job_world >= 0 && a->job_world < a->worlds.nworlds ? a->worlds.w[a->job_world].music : 0;
        ml_music(w >= 0 && w < 5 ? w : 0);
        break;
    }
    default: ml_music(MUS_NONE); break;
    }
}

static bool worker_scene_state(int st)
{
    return st == ST_CASITA || st == ST_MAP || st == ST_LEVEL;
}

void mla_set_state(app_t *a, int st)
{
    int prev = a->state;
    a->state = st;
    a->st_ms = 0;
    if (!worker_scene_state(st) && a->shown >= 0) canvas_show(a, a->shown);
    if (st == ST_CASITA && prev != ST_CASITA && a->scene != SC_CASITA) {
        if (!job(a, JOB_CASITA)) a->pending_job = JOB_CASITA;
        ml_ui_loading_text(a, "");
        ml_ui_loader(a, true, 0);
        load_begin(a, "ml_ldc", 2400u * 1024u);
    }
    if (st == ST_MAP && prev != ST_MAP && a->scene != SC_MAP) {
        if (!job(a, JOB_MAP)) a->pending_job = JOB_MAP;
        ml_ui_loading_text(a, "");
        ml_ui_loader(a, true, 0);
        load_begin(a, "ml_ldm", 1200u * 1024u);
    }
    music_for(a, st);
    ml_ui_show(a, st);
}

void mla_level_start(app_t *a, int wi, int li)
{
    char nm[64], wn[48];
    a->job_world = wi;
    a->job_level = li;
    mla_level_name(a, wi, li, nm, sizeof nm);
    mla_world_name(a, wi, wn, sizeof wn);
    char sub[96];
    snprintf(sub, sizeof sub, "%.40s  %d-%d", wn, wi + 1, li + 1);
    mla_text_mask(&a->hud.title, nm, aos_font_title);
    mla_text_mask(&a->hud.sub, sub, aos_font_body);
    ml_ui_loading_text(a, nm);
    snprintf(a->prog.last, sizeof a->prog.last, "%s %d", a->worlds.w[wi].id, li);
    load_begin(a, "ml_ldl", 900u * 1024u);
    mla_set_state(a, ST_LOADING);
    if (!job(a, JOB_LEVEL)) a->pending_job = JOB_LEVEL;
}

void mla_level_leave(app_t *a)
{
    mla_set_state(a, ST_MAP);
}

void mla_resume(app_t *a)
{
    if (a->state == ST_PAUSE) mla_set_state(a, ST_LEVEL);
}

static int s_ui_pending;

void mla_ui_job(app_t *a, int what)
{
    if (a->job == JOB_NONE && !a->pending_job) {
        a->ui_job = what;
        job(a, JOB_UI);
    } else {
        s_ui_pending = what;
    }
}

void mla_outfit(app_t *a)
{
    if (!job(a, JOB_OUTFIT)) a->pending_job = JOB_OUTFIT;
}

/* ---- results ---- */

static void result_show(app_t *a)
{
    ml_play_t *p = &a->play;
    ml_prog_t *pr = &a->prog;
    int wi = a->world, li = a->level;
    int stars = ml_play_stars(p);
    int before = pr->stars[wi][li];
    int earned = 0;
    if (!before) earned += 20;
    if (stars > before) earned += 10 * (stars - before);
    if (stars > before) pr->stars[wi][li] = (uint8_t)stars;
    pr->coins += earned;
    pr->stat[SX_MOVES] += p->moves;
    pr->stat[SX_PUSHES] += p->pushes;
    if (!before) pr->stat[SX_SOLVED]++;
    /* the world's gift, the first time every level is solved */
    const char *gift = NULL;
    const ml_world_info_t *wd = &a->worlds.w[wi];
    bool all = true;
    for (int k = 0; k < wd->nlevels; k++)
        if (!pr->stars[wi][k]) all = false;
    if (all && wd->gift[0] && !ml_prog_owns(pr, wd->gift)) {
        ml_prog_add(pr, wd->gift);
        gift = wd->gift;
    }
    mla_save(a);
    ml_ui_result_fill(a, stars, earned, p->moves, a->lv.par, gift);
    mla_set_state(a, ST_RESULT);
    uint32_t ms = (uint32_t)(aos_hal_uptime_ms() - a->w_fps_t0);
    unsigned fps10 = ms ? (unsigned)((uint64_t)a->w_frames * 10000u / ms) : 0;
    aos_hal_log("mila", "level %s won in %d moves (par %d), %d stars | %u.%u fps", wd->lv[li].id, p->moves,
                a->lv.par, stars, fps10 / 10, fps10 % 10);
}

static void race_result(app_t *a)
{
    ml_play_t *p = &a->play;
    bool won = p->won && !a->race_lost;
    int earned = won ? 40 : 15;
    a->prog.coins += earned;
    a->prog.stat[SX_MOVES] += p->moves;
    mla_save(a);
    ml_ui_race_fill(a, won, p->moves, a->rival_moves, earned);
    mla_set_state(a, ST_RESULT);
    aos_hal_log("mila", "race on %s: %s, %d moves against %d", a->worlds.w[a->world].lv[a->level].id,
                won ? "won" : "lost", p->moves, a->rival_moves);
}

/* ---- input ---- */

static app_t *app_of(lv_event_t *e)
{
    return (app_t *)lv_event_get_user_data(e);
}

static int swipe_dir(int dx, int dy)
{
    if (abs(dx) > abs(dy)) return dx > 0 ? D_RIGHT : D_LEFT;
    return dy > 0 ? D_DOWN : D_UP;
}

static bool in_circle(int x, int y, int cx, int cy, int r)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

/* is the point on Mila (her cell, a little generous) */
static bool on_mila(app_t *a, int x, int y)
{
    float gx, gy;
    ml_play_mila_pos(&a->play, &gx, &gy);
    float sx = ml_lpx(&a->w, gx) - a->play.icam_x, sy = ml_lpy(&a->w, gy, 0.25f) - a->play.icam_y;
    return fabsf(x - sx) < 46 && fabsf(y - sy) < 46;
}

static void level_touch(app_t *a, lv_event_code_t code, lv_point_t p)
{
    if (a->lmode == LM_OVERVIEW) {
        if (code == LV_EVENT_RELEASED && a->mode_t > 0.25f) {
            a->mode_t = 0;
            a->lmode = LM_ZOOM;
            a->w_fps_t0 = aos_hal_uptime_ms();
            a->w_frames = 0;
            ml_snd(SND_ZOOM);
        }
        return;
    }
    if (a->lmode != LM_PLAY && a->lmode != LM_PEEK) return;
    if (a->pinching) return;            /* two fingers: pinch_cb has it */
    if (code == LV_EVENT_PRESSED) {
        a->p0 = p;
        a->pressed = true;
        a->dragged = false;
        a->held_mila = on_mila(a, p.x, p.y);
        if (getenv("ML_DEBUG")) {
            float gx, gy;
            ml_play_mila_pos(&a->play, &gx, &gy);
            aos_hal_log("mila", "press %d,%d on_mila %d (mila at %d,%d)", p.x, p.y, a->held_mila,
                        (int)(ml_lpx(&a->w, gx) - a->play.icam_x), (int)(ml_lpy(&a->w, gy, 0.25f) - a->play.icam_y));
        }
        a->press_ms = lv_tick_get();
        a->held_dir = -1;
        return;
    }
    if (!a->pressed) return;
    if (code == LV_EVENT_PRESSING) {
        int dx = p.x - a->p0.x, dy = p.y - a->p0.y;
        if (!a->dragged && dx * dx + dy * dy >= SWIPE_PX * SWIPE_PX) {
            a->dragged = true;
            a->held_dir = swipe_dir(dx, dy);
            a->repeating = false;
            ml_play_push_dir(&a->play, a->held_dir);
            a->last_step_ms = lv_tick_get();
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->pressed = false;
        a->held_dir = -1;
        if (a->lmode == LM_PEEK) {
            a->lmode = LM_PLAY;
            a->peek_ready = false;
            return;
        }
        if (a->dragged) a->last_step_ms = lv_tick_get();   /* the system's own swipe comes now */
        if (a->dragged || code == LV_EVENT_PRESS_LOST) return;
        int x = a->p0.x, y = a->p0.y;
        if (in_circle(x, y, HUD_PAUSE_X, HUD_PAUSE_Y, HUD_BTN_R + 6)) {
            a->want_pause = true;
        } else if (in_circle(x, y, HUD_UNDO_X, HUD_UNDO_Y, HUD_BTN_R + 8)) {
            ml_play_undo(&a->play);
            a->hud_flash_undo = 0.15f;
            a->prog.stat[SX_UNDOS]++;
        } else if (in_circle(x, y, HUD_RESTART_X, HUD_RESTART_Y, HUD_BTN_R + 8)) {
            ml_play_restart(&a->play);
            a->hud_flash_restart = 0.15f;
        } else {
            int c = ml_play_cell_at(&a->play, x, y);
            if (c >= 0) ml_play_walk_to(&a->play, c);
        }
    }
}

static void touch_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (a->closing) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_event_code_t code = lv_event_get_code(e);
    switch (a->state) {
    case ST_LEVEL:
        if (a->level_ok) level_touch(a, code, p);
        break;
    case ST_CASITA:
        mlc_touch(a, code, p.x, p.y);
        break;
    case ST_MAP:
        mlm_touch(a, code, p.x, p.y);
        break;
    default:
        break;
    }
}

/* Two fingers (v0.6.0): pinching them together pulls the camera back to
 * the whole room -the same view a finger held on Mila gives, but it stays
 * after the fingers lift-; spreading them flies back down to her, and so
 * does a tap. The first finger of a pinch must not become a step: the
 * pinch takes the touch over as soon as the second finger is sure. */
#define PINCH_OUT       0.80f
#define PINCH_IN        1.25f

static void pinch_cb(const aos_gesture_event_t *ev, void *user)
{
    app_t *a = (app_t *)user;
    if (a->closing || a->state != ST_LEVEL || !a->level_ok) return;
    if (a->lmode != LM_PLAY && a->lmode != LM_PEEK) return;
    switch (ev->type) {
    case AOS_GESTURE_PINCH_BEGIN:
        a->pinching = true;
        a->pinch_acc = 1.0f;
        a->pressed = false;             /* whatever the first finger started */
        a->held_dir = -1;
        break;
    case AOS_GESTURE_PINCH:
        a->pinch_acc *= ev->scale;
        if (a->lmode == LM_PLAY && a->pinch_acc < PINCH_OUT) {
            a->lmode = LM_PEEK;
            a->peek_ready = false;
            if (!job(a, JOB_PEEK)) a->pending_job = JOB_PEEK;
            a->pinch_acc = 1.0f;
            ml_snd(SND_ZOOM);
        } else if (a->lmode == LM_PEEK && a->pinch_acc > PINCH_IN) {
            a->lmode = LM_PLAY;
            a->peek_ready = false;
            a->pinch_acc = 1.0f;
            ml_snd(SND_ZOOM);
        }
        break;
    case AOS_GESTURE_PINCH_END:
        a->pinching = false;
        break;
    default:
        break;
    }
}

/* held: a swipe that stays down repeats, a finger that stays on Mila peeks */
static void hold_tick(app_t *a)
{
    if (a->state != ST_LEVEL || !a->pressed) return;
    uint32_t now = lv_tick_get();
    uint32_t wait = a->repeating ? REPEAT_MS : REPEAT_FIRST_MS;
    if (a->dragged && a->held_dir >= 0 && now - a->last_step_ms >= wait && !ml_play_busy(&a->play)) {
        a->repeating = true;
        ml_play_push_dir(&a->play, a->held_dir);
        a->last_step_ms = now;
    }
    if (getenv("ML_DEBUG") && !a->dragged && a->held_mila) {
        static uint32_t last;
        if (now - last > 500) {
            last = now;
            aos_hal_log("mila", "hold: lmode %d, %u ms", a->lmode, (unsigned)(now - a->press_ms));
        }
    }
    if (!a->dragged && a->held_mila && a->lmode == LM_PLAY && now - a->press_ms >= PEEK_MS) {
        a->lmode = LM_PEEK;
        a->peek_ready = false;
        if (!job(a, JOB_PEEK)) a->pending_job = JOB_PEEK;
    }
}

/* back, from a swipe right or the runtime */
static bool go_back(app_t *a)
{
    switch (a->state) {
    case ST_CASITA: case ST_BOOT:
        return false;
    case ST_LEVEL:
        if (a->race) {
            ml_link_end(a);
            mla_set_state(a, ST_CASITA);
            return true;
        }
        a->want_pause = true;
        return true;
    case ST_PAUSE:
        mla_resume(a);
        return true;
    case ST_LOADING:
        return true;
    case ST_MAP:
        mla_set_state(a, ST_CASITA);
        return true;
    case ST_RESULT:
        mla_set_state(a, ST_MAP);
        return true;
    case ST_LOBBY:
        ml_link_end(a);
        mla_set_state(a, ST_CASITA);
        return true;
    case ST_SHOP: case ST_SETTINGS:
        mla_set_state(a, ST_CASITA);
        return true;
    default:
        return true;
    }
}

static void take_gesture(app_t *a)
{
    int gst = aos_ui_take_gesture();
    if (gst == AOS_TOUCH_GESTURE_NONE) return;
    if (a->state != ST_LEVEL) {
        if (a->state == ST_MAP) return;      /* the map scrolls with the finger */
        if (gst == AOS_TOUCH_GESTURE_RIGHT && !go_back(a)) a->want_exit = true;
        return;
    }
    if (a->lmode != LM_PLAY) return;
    /* the chip's own swipe: on the board a fast one never reaches LVGL */
    if ((uint32_t)(lv_tick_get() - a->last_step_ms) < 300 || a->pressed) return;
    int d = gst == AOS_TOUCH_GESTURE_UP ? D_UP : gst == AOS_TOUCH_GESTURE_DOWN ? D_DOWN :
            gst == AOS_TOUCH_GESTURE_LEFT ? D_LEFT : D_RIGHT;
    ml_play_push_dir(&a->play, d);
    a->last_step_ms = lv_tick_get();
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev || worker_scene_state(a->state)) return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        if (!go_back(a)) a->want_exit = true;
    }
}

static void play_events(app_t *a)
{
    while (a->ev_r != a->ev_w) {
        uint32_t ev = a->ev_ring[a->ev_r & 15];
        a->ev_r++;
        static const struct { uint32_t ev; uint8_t snd; } map[] = {
            { PE_WIN, SND_WIN }, { PE_TARGET, SND_TARGET }, { PE_FALL, SND_FALL }, { PE_GATE, SND_GATE },
            { PE_GATE_SHUT, SND_GATE }, { PE_FLAP, SND_FLAP }, { PE_SLIDE, SND_SLIDE }, { PE_ROLL, SND_ROLL },
            { PE_PUSH, SND_PUSH }, { PE_UNDO, SND_UNDO }, { PE_RESTART, SND_UNDO }, { PE_BUMP, SND_BUMP },
            { PE_STEP, SND_STEP },
        };
        int played = 0;
        for (size_t k = 0; k < sizeof map / sizeof map[0] && played < 2; k++) {
            if (ev & map[k].ev) {
                ml_snd(map[k].snd);
                played++;
            }
        }
    }
}

/* ---- the timer ---- */

#ifdef AOS_SIM_BUILTIN
static bool s_sim_link;
#endif
static char s_dev_level[24];       /* mila_dev.txt "level=roofs,8": open it */

static bool open_named_level(app_t *a, const char *e)
{
    char id[24];
    int n = 1;
    const char *comma = strchr(e, ',');
    snprintf(id, sizeof id, "%.*s", comma ? (int)(comma - e) : (int)strlen(e), e);
    if (comma) n = atoi(comma + 1);
    int wi = ml_worlds_find(&a->worlds, id);
    if (wi < 0 && id[0] >= '0' && id[0] <= '9') wi = atoi(id);
    if (wi >= 0 && wi < a->worlds.nworlds && n >= 1 && n <= a->worlds.w[wi].nlevels) {
        mla_level_start(a, wi, n - 1);
        return true;
    }
    return false;
}

static void boot_done(app_t *a)
{
    if (!a->job_ok) {
        ml_ui_boot_text(a, _("Falta mila.pak en la tarjeta"));
        return;
    }
    ml_ui_job_done(a, UJ_ICONS);
    ml_prog_load(&a->prog, &a->worlds);
    ml_audio_enable(a->prog.snd & 1, (a->prog.snd >> 1) & 1);
    for (int i = 0; i < a->worlds.nworlds; i++) {
        char nm[48];
        mla_text_mask(&a->wname[i], mla_world_name(a, i, nm, sizeof nm), aos_font_body);
    }
#ifdef AOS_SIM_BUILTIN
    /* Development switches (getenv() is NULL on the board):
     *   ML_UNLOCK=1             every world and level open
     *   ML_COINS=n              coins for the shop
     *   ML_LEVEL=<world>,<n>    straight into that level (world id or index, n from 1)
     *   ML_SCREEN=map|shop|settings
     */
    const char *e;
    if ((e = getenv("ML_UNLOCK")) && e[0]) a->dev_unlock = true;
    if ((e = getenv("ML_COINS")) && e[0]) a->prog.coins = atoi(e);
    if ((e = getenv("ML_LEVEL")) && e[0] && open_named_level(a, e)) return;
    if ((e = getenv("ML_LINK")) && e[0]) {
        /* two sims (AOS_SIM_LINK_PORT/_PARTNER): into the lobby as soon as
         * the link has its partner (the sim sets it on its first poll) */
        aos_hal_link_start();
        s_sim_link = true;
        mla_set_state(a, ST_CASITA);
        return;
    }
    if ((e = getenv("ML_SCREEN")) && e[0]) {
        if (!strcmp(e, "map")) {
            mla_set_state(a, ST_MAP);
            return;
        }
        if (!strcmp(e, "shop") || !strcmp(e, "settings")) {
            mla_set_state(a, ST_CASITA);
            mla_set_state(a, !strcmp(e, "shop") ? ST_SHOP : ST_SETTINGS);
            return;
        }
    }
#endif
    if (s_dev_level[0] && open_named_level(a, s_dev_level)) return;
    mla_set_state(a, ST_CASITA);
}

static void frame(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;
    }
    take_gesture(a);
    uint64_t now = aos_hal_uptime_ms();
    int dt = a->prev_ms ? (int)(uint32_t)(now - a->prev_ms) : TICK_MS;
    if (dt > 100) dt = 100;
    a->prev_ms = now;
    a->st_ms += (uint32_t)dt;
#ifdef AOS_SIM_BUILTIN
    if (s_sim_link && a->state == ST_CASITA && !a->link_on && ml_link_available()) {
        s_sim_link = false;
        ml_link_begin(a);
    }
#endif
    ml_link_tick(a);
    play_events(a);
    ml_audio_tick();
    ml_ui_tick(a, dt);
    hold_tick(a);

    if (a->job_done && a->job == JOB_NONE) {
        a->job_done = false;
        int j = s_last_job;
        if (j == JOB_UI) {
            ml_ui_job_done(a, a->ui_job);
        } else if (a->state == ST_BOOT) {
            boot_done(a);
        } else if (j == JOB_LEVEL) {
            if (a->job_ok) {
                mla_set_state(a, ST_LEVEL);
            } else {
                aos_ui_toast(_("No se pudo cargar el nivel"), 1800);
                mla_set_state(a, ST_MAP);
            }
        }
    }
    if (a->pending_job && a->job == JOB_NONE) {
        int j = a->pending_job;
        a->pending_job = 0;
        job(a, j);
    } else if (s_ui_pending && a->job == JOB_NONE) {
        int w = s_ui_pending;
        s_ui_pending = 0;
        a->ui_job = w;
        job(a, JOB_UI);
    }
    /* every 5 s in a level: where it is and how fast the worker draws */
    static uint32_t s_rep_ms, s_rep_frames;
    if (a->state == ST_LEVEL && a->st_ms - s_rep_ms >= 5000) {
        uint32_t fr = a->w_frames - s_rep_frames;
        aos_hal_log("mila", "level %s: mode %d, %d moves, %u.%u fps", a->level_ok ? a->worlds.w[a->world].lv[a->level].id : "-",
                    a->lmode, a->play.moves, (unsigned)(fr * 10 / 5 / 10), (unsigned)(fr * 10 / 5 % 10));
        s_rep_ms = a->st_ms;
        s_rep_frames = a->w_frames;
    } else if (a->state != ST_LEVEL || a->st_ms < 100) {
        s_rep_ms = a->state == ST_LEVEL ? a->st_ms : 0;
        s_rep_frames = a->w_frames;
    }
    /* the loader stays until the scene's first frame is ready, then that
     * frame goes to the panel and to the canvas under the LVGL panels */
    if (ml_ui_loader_on() && a->state != ST_BOOT) {
        int want = scene_of(a->state);
        bool ready = false;
        if (want == SC_NONE && a->state != ST_LOADING) {
            /* a panel was asked for meanwhile (the shop): it goes over */
            ml_ui_loader(a, false, 0);
            a->ld_key[0] = 0;
        }
        if (want != SC_NONE && a->scene == want)
            for (int i = 0; i < ML_NFB; i++)
                if (a->fb_state[i] == FB_READY && a->fb_seq[i] > a->scene_seq) ready = true;
#ifdef AOS_SIM_BUILTIN
        /* ML_LOADER=1: the loader stays up (to look at it: the sim loads in ms) */
        if (getenv("ML_LOADER")) ready = false;
#endif
        if (ready) {
            push_frame(a);
            if (a->shown >= 0) canvas_show(a, a->shown);
            ml_ui_loader(a, false, 100);
            load_end(a);
        } else if (ml_ui_loader_on()) {
            ml_ui_loader(a, true, load_pct(a));
            return;
        }
    }
    switch (a->state) {
    case ST_LEVEL:
        if (a->want_pause) {
            a->want_pause = false;
            mla_set_state(a, ST_PAUSE);
            break;
        }
        push_frame(a);
        if (a->race) {
            /* a race ends when someone solved it (a moment to see it) */
            bool mine = a->lmode == LM_WON && a->mode_t > 1.8f;
            if ((mine || (a->race_lost && a->st_ms > 0)) && !a->result_shown) {
                a->result_shown = true;
                race_result(a);
            }
            break;
        }
        if (a->lmode == LM_WON && a->mode_t > 1.8f && !a->result_shown) {
            a->result_shown = true;
            result_show(a);
        }
        break;
    case ST_CASITA:
    case ST_MAP:
        push_frame(a);
        break;
    default:
        a->want_pause = false;
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
    return a ? go_back(a) : false;
}

static bool app_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a || a->closing) return false;
    if (a->state != ST_LEVEL) return false;
    if (a->lmode == LM_OVERVIEW) {
        if (action == AOS_BUTTON_CLICK) {
            a->mode_t = 0;
            a->lmode = LM_ZOOM;
        }
        return true;
    }
    if (action == AOS_BUTTON_CLICK) {
        ml_play_undo(&a->play);
        a->hud_flash_undo = 0.15f;
        a->prog.stat[SX_UNDOS]++;
    } else if (action == AOS_BUTTON_LONG) {
        a->want_pause = true;
    }
    return true;
}

static void app_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a && a->link_on) ml_link_end(a);
    if (a && a->state == ST_LEVEL) mla_set_state(a, ST_PAUSE);
}

static void free_all(app_t *a)
{
    level_free(a);
    mlc_close(a);
    mlm_close(a);
    ml_mila_free(&a->mila);
    ml_ui_free(a);
    for (int i = 0; i < ML_NFB; i++) {
        free(a->fb[i]);
        a->fb[i] = NULL;
    }
    free(a->cv);
    a->cv = NULL;
    free(a->band);
    a->band = NULL;
    ml_hud_free(&a->hud);
    for (int i = 0; i < ML_MAX_WORLDS; i++) free(a->wname[i].a);
    ml_worlds_free(&a->worlds);
    ml_art_close();
}

static void *ml_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)ml_calloc(1, sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("mila", "opening | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    bool ok = true;
    for (int i = 0; i < ML_NFB; i++) {
        a->fb[i] = (uint16_t *)ml_malloc((size_t)ML_W * ML_H * 2);
        if (!a->fb[i]) ok = false;
    }
    a->cv = (uint16_t *)ml_malloc((size_t)ML_W * ML_H * 2);
    a->band = (uint16_t *)ml_malloc_internal((size_t)ML_W * ML_BAND * 2);
    if (!ok || !a->cv) {
        aos_hal_log("mila", "out of memory");
        free_all(a);
        free(a);
        return NULL;
    }
    memset(a->fb[0], 0, (size_t)ML_W * ML_H * 2);
    a->shown = -1;
    a->world = a->level = -1;
    a->root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    memset(a->cv, 0, (size_t)ML_W * ML_H * 2);
    lv_canvas_set_buffer(a->canvas, a->cv, ML_W, ML_H, LV_COLOR_FORMAT_RGB565);
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
    aos_gesture_attach(a->touch, 0, pinch_cb, a);

    hud_build(a);
    ml_ui_build(a, root);

    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    /* apps/mila_dev.txt on the card: "unlock" opens every level */
    {
        char path[96], buf[64] = "";
        snprintf(path, sizeof path, "%s/mila_dev.txt", aos_hal_path_apps());
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            if (strstr(buf, "unlock")) a->dev_unlock = true;
            const char *lv = strstr(buf, "level=");
            if (lv) snprintf(s_dev_level, sizeof s_dev_level, "%.23s", lv + 6);
            for (char *q = s_dev_level; *q; q++)
                if (*q == '\n' || *q == '\r' || *q == ' ') *q = 0;
        }
    }
    ml_audio_open();
    a->state = ST_BOOT;
    ml_ui_show(a, ST_BOOT);
    s_last_job = JOB_BOOT;
    a->job = JOB_BOOT;
    if (!aos_hal_worker_start_on("mila", worker_fn, a, WORKER_STACK, 0, 5)) aos_hal_log("mila", "no worker");
    a->timer = lv_timer_create(frame, TICK_MS, a);
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("mila", "ready | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    return a;
}

static void ml_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    a->closing = true;
    a->scene = SC_NONE;
    if (a->link_on) ml_link_end(a);
    if (a->timer) lv_timer_delete(a->timer);
    aos_hal_worker_stop();
    ml_audio_close();
    if (a->root) lv_obj_clean(a->root);
    if (a->worlds.nworlds) mla_save(a);
    free_all(a);
    free(a);
}

/* The launcher icon: a black cat's head with amber eyes. */
static const uint8_t ML_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, 0, 4, 44, 38, 19, AIC_C_LIT(0x1A1822), 255),
    AIC_RECT(AIC_CENTER, -14, -16, 14, 16, 4, AIC_C_LIT(0x1A1822), 255),
    AIC_RECT(AIC_CENTER, 14, -16, 14, 16, 4, AIC_C_LIT(0x1A1822), 255),
    AIC_RECT(AIC_CENTER, -9, 2, 10, 12, 5, AIC_C_LIT(0xFFB21A), 255),
    AIC_RECT(AIC_CENTER, 9, 2, 10, 12, 5, AIC_C_LIT(0xFFB21A), 255),
    AIC_RECT(AIC_CENTER, -9, 2, 3, 9, 1, AIC_C_LIT(0x000000), 255),
    AIC_RECT(AIC_CENTER, 9, 2, 3, 9, 1, AIC_C_LIT(0x000000), 255),
    AIC_RECT(AIC_CENTER, 0, 11, 5, 3, 1, AIC_C_LIT(0xF07A96), 255),
    AIC_END
};

static bool ml_init(aos_app_t *app)
{
    app->desc.id       = "demo.mila";
    app->desc.name     = "Mila";
    app->desc.icon     = LV_SYMBOL_HOME;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, ML_ICON, sizeof ML_ICON);
    app->desc.color_a  = 0xF07AA0;
    app->desc.color_b  = 0x3A2A5A;
    app->desc.order    = 161;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    app->create  = ml_create;
    app->destroy = ml_destroy;
    app->hide    = app_hide;
    app->back    = app_back;
    app->button  = app_button;
    return true;
}

AOS_APP_ENTRY(ml_init);
