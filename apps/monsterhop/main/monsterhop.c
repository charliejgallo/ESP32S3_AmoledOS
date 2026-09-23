/*
 * MONSTER HOP - Tommy hops across monster lands for the five keys of each
 * level: the app (see mh_app.h for the map of the files)
 *
 * Every word on screen is wrapped in _(): the LVGL panels directly, and the
 * words inside the frame (the banners, the digits) are rendered from _()
 * into masks when the app opens, so they follow the language too.
 */
#include "mh_app.h"
#include "mh_audio.h"
#include "mh_ui.h"

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

#define TICK_MS         8
#define WORKER_STACK    (12 * 1024)
#define SWIPE_PX        22

/* ---- the levels ---- */

/* level numbers are forever: they key the records and travel over the link */
static const mh_level_info_t *level_table(int i)
{
    static const mh_level_info_t t[MH_LEVELS] = {
        { "city_1", ZONE_CITY },     { "city_2", ZONE_CITY },     { "city_3", ZONE_CITY },     { "city_4", ZONE_CITY },
        { "castle_1", ZONE_CASTLE }, { "castle_2", ZONE_CASTLE }, { "castle_3", ZONE_CASTLE }, { "castle_4", ZONE_CASTLE },
        { "desert_1", ZONE_DESERT }, { "desert_2", ZONE_DESERT }, { "desert_3", ZONE_DESERT }, { "desert_4", ZONE_DESERT },
        { "forest_1", ZONE_FOREST }, { "forest_2", ZONE_FOREST }, { "forest_3", ZONE_FOREST }, { "forest_4", ZONE_FOREST },
    };
    static const mh_level_info_t test = { "test_1", ZONE_TEST };
    return i >= 0 && i < MH_LEVELS ? &t[i] : &test;
}

const mh_level_info_t *mha_level_info(int i)
{
    return level_table(i);
}

const char *mha_level_title(int i)
{
    switch (i) {
    case 0: return _("Calle Principal");
    case 1: return _("Las Cloacas");
    case 2: return _("El Desarmadero");
    case 3: return _("La Municipalidad");
    case 4: return _("El Foso");
    case 5: return _("El Gran Salón");
    case 6: return _("La Torre del Reloj");
    case 7: return _("La Cámara del Conde");
    case 8: return _("Mar de Dunas");
    case 9: return _("El Oasis");
    case 10: return _("Salas del Templo");
    case 11: return _("La Tumba del Faraón");
    case 12: return _("El Sendero");
    case 13: return _("El Río Bravo");
    case 14: return _("El Viejo Molino");
    case 15: return _("El Claro de la Luna");
    default: return _("Campo de Prueba");
    }
}

const char *mha_zone_title(int z)
{
    switch (z) {
    case ZONE_CITY: return _("Pueblo Zombi");
    case ZONE_CASTLE: return _("Castillo Vampiro");
    case ZONE_DESERT: return _("Desierto de las Momias");
    case ZONE_FOREST: return _("Bosque Lobizón");
    default: return "";
    }
}

int mha_zone_need(int z)
{
    static const int need[4] = { 0, 6, 14, 22 };
    return z >= 0 && z < 4 ? need[z] : 0;
}

bool mha_level_open(const app_t *a, int i)
{
    if (a->dev_auto) return true;
    if (i < 0 || i >= MH_LEVELS) return false;
    if (mh_prog_stars(&a->prog) < mha_zone_need(i / 4)) return false;
    if (i % 4 == 0) return true;
    return a->prog.stars[i - 1] > 0;
}

/* ---- the player ---- */

void mha_outfit(app_t *a)
{
    mh_shop_apply(a->prog.eq, &a->outfit, &a->wear, &a->skin_fx, &a->trail);
    a->outfit_dirty = true;
}

void mha_save(app_t *a)
{
    mh_prog_save(&a->prog);
}

/* --------------------------------------------------------------------------
 * The worker
 * -------------------------------------------------------------------------- */

static uint64_t s_last_yield;

static void worker_yield(void)
{
    uint64_t now = aos_hal_uptime_ms();
    if ((uint32_t)(now - s_last_yield) > 1000) {
        /* a whole tick once a second keeps the idle task fed (Turbo's rule) */
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
    mh_world_free(&a->world);
    mh_level_free(&a->lv);
    a->loaded_level = -2;
}

static bool load_level(app_t *a, int idx)
{
    level_free(a);
    const mh_level_info_t *li = mha_level_info(idx);
    char nm[40];
    snprintf(nm, sizeof nm, "lvl_%s", li->name);
    uint32_t len = 0;
    uint8_t *b = mh_art_blob(nm, &len);
    if (!b) {
        aos_hal_log("mhop", "level %s: missing", nm);
        return false;
    }
    bool ok = mh_level_parse(&a->lv, b, len);
    free(b);
    if (!ok) {
        aos_hal_log("mhop", "level %s: malformed", nm);
        return false;
    }
#ifdef AOS_SIM_BUILTIN
    {
        /* MH_START=x,y: the level starts in that cell (for screenshots) */
        const char *st = getenv("MH_START");
        if (st && strchr(st, ',')) {
            a->lv.start_x = (uint8_t)atoi(st);
            a->lv.start_y = (uint8_t)atoi(strchr(st, ',') + 1);
        }
    }
#endif
    if (!mh_world_init(&a->world, &a->lv)) {
        aos_hal_log("mhop", "level %s: no memory for the cache", nm);
        mh_level_free(&a->lv);
        return false;
    }
    mh_cast_level(&a->cast, &a->lv);
    uint32_t seed = (uint32_t)aos_hal_uptime_ms() | 1u;
    bool race = a->link_on && a->link_state == LK_LOADING;
    a->lk_race = race;
    /* a race: the same seed on both, the normal clock, lives that never end */
    mh_game_init(&a->game, &a->lv, race ? DIFF_NORMAL : a->prog.diff, race ? a->link_seed : seed);
    if (race) {
        a->game.link = true;
        a->game.host = a->is_host;
    }
    mh_scene_init(&a->scene, &a->world, &a->game, &a->outfit, a->skin_fx);
    mh_scene_trail(&a->scene, a->trail);
    if (race) {
        mh_outfit_t o;
        mh_wear_t wr;
        int fx = 0, tr = 0;
        mh_shop_apply(a->rival_eq, &o, &wr, &fx, &tr);
        mh_scene_rival(&a->scene, &o.body);
    }
    a->loaded_level = idx;
    a->level_ok = true;
    return true;
}

enum { JOB_MENU_BACK = JOB_UI + 1 };

static void run_job(app_t *a, int j)
{
    uint32_t hi = 0, hp = 0;
    uint64_t t0 = aos_hal_uptime_ms();
    switch (j) {
    case JOB_BOOT: {
        char path[96];
        snprintf(path, sizeof path, "%s/monsterhop.pak", aos_hal_path_apps());
        bool ok = mh_art_open(path);
        if (ok) {
            a->outfit_dirty = false;
            mh_cast_hero(&a->cast, &a->wear);
            mh_ui_job(a, UJ_MENU);
        }
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mhop", "pack %s, hero and menu in %u ms | psram %u", ok ? "open" : "MISSING",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)hp);
        a->job_ok = ok;
        break;
    }
    case JOB_LEVEL:
        mh_ui_job(a, UJ_FREE_MAP);
        if (a->outfit_dirty) {
            a->outfit_dirty = false;
            mh_cast_hero(&a->cast, &a->wear);
        }
        a->job_ok = load_level(a, a->job_level);
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mhop", "level %d %s in %u ms, art %u KB | psram %u", a->job_level, a->job_ok ? "ok" : "FAILED",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), (unsigned)(mh_art_bytes() / 1024), (unsigned)hp);
        break;
    case JOB_MENU_BACK:
        /* the level goes, its monsters and objects too (2 MB: without that
         * the map's 1 MB found no room and the menus came back blank), and
         * the menus' pictures come back */
        level_free(a);
        mh_cast_level_free(&a->cast);
        mh_ui_job(a, UJ_MENU);
        aos_hal_heap_info(&hi, &hp);
        aos_hal_log("mhop", "menus back in %u ms, map %s, art %u KB | psram %u",
                    (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0), a->ui_map.buf ? "ok" : "MISSING",
                    (unsigned)(mh_art_bytes() / 1024), (unsigned)hp);
        a->job_ok = true;
        break;
    case JOB_OUTFIT:
        mh_cast_hero(&a->cast, &a->wear);
        if (a->level_ok) {
            mh_scene_outfit(&a->scene, &a->outfit, mh_zone_look(a->lv.zone)->tint, a->skin_fx);
            mh_scene_trail(&a->scene, a->trail);
        }
        mh_ui_job(a, UJ_MENU);
        a->job_ok = true;
        break;
    case JOB_UI:
        mh_ui_job(a, a->ui_job);
        a->job_ok = true;
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

static void spare_frame(app_t *a)
{
    a->spare_checked = true;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    if (!a->fb[3] && hp > (uint32_t)(MH_W * MH_H * 2) + MH_FB_SPARE) {
        uint16_t *f = (uint16_t *)mh_malloc((size_t)MH_W * MH_H * 2);
        if (f) {
            a->fb_state[3] = FB_FREE;
            a->fb[3] = f;
            a->nfb = 4;
        }
    }
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("mhop", "playing with %d frame buffers | psram %u B free", a->nfb, (unsigned)hp);
}

/* the fly-over at a level's start: the camera visits each key, then Tommy */
static void intro_camera(app_t *a, float dt)
{
    mh_game_t *g = &a->game;
    float kx[MH_KEYS + 1], ky[MH_KEYS + 1];
    int n = 0;
    for (int i = 0; i < g->n_pick && n < MH_KEYS; i++) {
        if (g->pick[i].type != ENT_KEY) continue;
        kx[n] = g->pick[i].x + 0.5f;
        ky[n] = g->pick[i].y + 0.5f;
        n++;
    }
    /* far keys first, so the tour ends where Tommy stands */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (ky[j] > ky[i]) {
                float tx = kx[i], ty = ky[i];
                kx[i] = kx[j]; ky[i] = ky[j];
                kx[j] = tx; ky[j] = ty;
            }
        }
    }
    kx[n] = g->h.x;
    ky[n] = g->h.y;
    a->intro_t += dt;
    const float per = 0.85f;
    int i = (int)(a->intro_t / per);
    if (i > n || a->intro_skip) {
        mh_scene_look(&a->scene, &a->world, g->h.x, g->h.y, g->h.floor * MH_FLOOR_M, dt, a->intro_skip);
        a->intro_i = n + 1;
        return;
    }
    a->intro_i = i;
    mh_scene_look(&a->scene, &a->world, kx[i], ky[i], 0, dt * 0.9f, a->intro_t < 0.05f);
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
    /* a race keeps the level's clock on the wall's even through a slow
     * frame (up to half a second); alone a slow frame is simply cut */
    float run = dt > 0.5f ? 0.5f : dt;
    if (dt > 0.1f) dt = 0.1f;
    a->w_last_ms = now;
    mh_game_t *g = &a->game;
    mh_scene_t *s = &a->scene;
    if (a->playing && !a->frozen) {
        if (a->lk_race) mhl_worker_before(a);
        int hop = a->in_hop;
        if (hop) {
            a->in_hop = 0;
            mh_game_hop(g, hop - 1);
        }
        if (a->in_action) {
            a->in_action = false;
            mh_game_action(g);
        }
        if (a->lk_race) {
            /* the guest follows the host's clock: a little more or a little
             * less each frame */
            float behind = a->lk_behind;
            if (behind != 0) {
                float adj = behind > 0.1f ? 0.1f : behind < -0.1f ? -0.1f : behind;
                if (run + adj < 0) adj = -run;
                a->lk_behind = behind - adj;
                run += adj;
            }
            /* steps of at most 50 ms: collisions stay honest */
            while (run > 0.0005f) {
                float st = run > 0.05f ? 0.05f : run;
                mh_game_step(g, st);
                run -= st;
            }
            mhl_worker_after(a);
        } else {
            /* two half steps: collisions stay honest at low frame rates */
            mh_game_step(g, dt * 0.5f);
            mh_game_step(g, dt * 0.5f);
        }
        uint32_t ev = g->events;
        g->events = 0;
        mh_hud_events(&a->hs, ev, dt);
        if (ev) {
            a->ev_ring[a->ev_w & 15] = ev;
            a->ev_w++;
        }
        for (int k = 0; k < g->n_dirty; k++) mh_world_invalidate_cell(&a->world, g->dirty[k][0], g->dirty[k][1]);
        g->n_dirty = 0;
        mh_scene_build(s, &a->world, g, &a->cast, &a->dl, dt);
    } else {
        mh_hud_events(&a->hs, 0, dt);
        /* the scene without stepping: the fly-over moves the camera itself */
        float cx = s->cam_x, cy = s->cam_y;
        mh_scene_build(s, &a->world, g, &a->cast, &a->dl, 0);
        s->cam_x = cx;
        s->cam_y = cy;
        if (a->state == ST_INTRO) intro_camera(a, dt);
        s->icam_x = mh_iround(s->cam_x);
        s->icam_y = mh_iround(s->cam_y);
    }
    static int px, py;
    int dirx = s->icam_x > px ? 1 : s->icam_x < px ? -1 : 0;
    int diry = s->icam_y > py ? 1 : s->icam_y < py ? -1 : 0;
    px = s->icam_x;
    py = s->icam_y;
    mh_world_prepare(&a->world, s->icam_x, s->icam_y, dirx, diry, 2);
    a->hs.pause_icon = a->state == ST_PLAY && !a->lk_race;
    a->hs.show_title = a->state == ST_INTRO;
    for (int y0 = 0; y0 < MH_H; y0 += MH_BAND) {
        int y1 = y0 + MH_BAND > MH_H ? MH_H : y0 + MH_BAND;
        mh_img_t bim;
        uint16_t *band = a->band ? a->band : a->fb[i] + (size_t)y0 * MH_W;
        mh_img_init(&bim, band - (size_t)y0 * MH_W, MH_W, MH_H);
        mh_img_clip(&bim, 0, y0, MH_W, y1);
        mh_render_band(&a->world, &bim, s->icam_x, s->icam_y, y0, y1, &a->dl);
        mh_scene_bits_draw(s, &a->world, &bim, s->icam_x, s->icam_y);
        mh_hud_draw(&a->hud, &bim, g, &a->hs, &a->world, s->icam_x, s->icam_y);
        mh_copy_swap(a->fb[i] + (size_t)y0 * MH_W, band, (size_t)(y1 - y0) * MH_W);
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
    mh_set_clock(clock_ms);
    mh_set_yield(worker_yield);
    while (!aos_hal_worker_should_stop()) {
        if (a->job) {
            s_last_yield = aos_hal_uptime_ms();
            run_job(a, a->job);
            a->job = JOB_NONE;
            a->job_done = true;
            continue;
        }
        if (a->level_ok && (a->playing || a->frozen)) {
            play_frame(a);
            continue;
        }
        a->w_last_ms = 0;
        aos_hal_worker_sleep(20);
    }
    mh_set_yield(NULL);
}

/* one job at a time; picture jobs asked for while one runs wait their turn */
static int s_pending_ui;
static int s_done_ui;
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

void mha_ui_job(app_t *a, int what)
{
    mh_ui_before_job(a, what);
    if (a->job == JOB_NONE && !a->playing && !a->frozen && a->state != ST_LOADING) {
        a->ui_job = what;
        s_done_ui = what;
        job(a, JOB_UI);
    } else {
        s_pending_ui = what;
    }
}

/* --------------------------------------------------------------------------
 * Frames to the panel
 * -------------------------------------------------------------------------- */

/* the canvas shows a frame under the panels (in the simulator every frame:
 * it has no panel to blit to). LVGL 9.5 draws nothing from a SWAPPED
 * canvas, so the panel-order frame is swapped into LVGL's order. */
static void canvas_show(app_t *a, int i)
{
    mh_copy_swap(a->cv, a->fb[i], (size_t)MH_W * MH_H);
    lv_obj_invalidate(a->canvas);
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
    for (int i = 0; i < a->nfb; i++) {
        if (i != best && a->fb_state[i] == FB_READY) a->fb_state[i] = FB_FREE;
    }
    if (!aos_hal_display_blit(0, 0, MH_W, MH_H, a->fb[best])) canvas_show(a, best);
    if (a->shown >= 0 && a->shown != best) a->fb_state[a->shown] = FB_FREE;
    a->fb_state[best] = FB_SHOWN;
    a->shown = best;
}

/* ---- text into masks, once ---- */

static bool text_mask(mh_mask_t *m, const char *txt, const lv_font_t *font)
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
    m->a = (uint8_t *)mh_malloc((size_t)w * h);
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
        text_mask(&a->hud.dig[i], s, aos_font_title);
        text_mask(&a->hud.sdig[i], s, aos_font_body);
    }
    text_mask(&a->hud.msg[MSG_KEY], _("¡Llave!"), aos_font_title);
    text_mask(&a->hud.msg[MSG_OPEN], _("¡Se abrió la salida!"), aos_font_title);
    text_mask(&a->hud.msg[MSG_CHECK], _("Punto de control"), aos_font_title);
    text_mask(&a->hud.msg[MSG_TIMEUP], _("¡Sin tiempo!"), aos_font_title);
    text_mask(&a->hud.msg[MSG_READY], _("¿Listo?"), aos_font_title);
    text_mask(&a->hud.msg[MSG_GO], _("¡Ya!"), aos_font_title);
    text_mask(&a->hud.msg[MSG_LIFE], _("¡Una vida más!"), aos_font_title);
    text_mask(&a->hud.msg[MSG_TIME], _("+30 segundos"), aos_font_title);
    text_mask(&a->hud.msg[MSG_LOW], _("¡Rápido!"), aos_font_title);
    a->hs.msg = -1;
}

/* --------------------------------------------------------------------------
 * States
 * -------------------------------------------------------------------------- */

static void music_for(app_t *a, int st)
{
    if (st == ST_INTRO || st == ST_PLAY) {
        int z = a->level_ok ? a->lv.zone : ZONE_CITY;
        mh_music(z < 4 ? z : MUS_MENU, a->level >= 0 && (a->level % 4) == 3);
    } else if (st == ST_RESULT || st == ST_LOADING) {
        mh_music(MUS_NONE, false);
    } else if (st != ST_PAUSE) {
        mh_music(MUS_MENU, false);
    }
}

void mha_set_state(app_t *a, int st)
{
    int prev = a->state;
    a->state = st;
    a->st_ms = 0;
    music_for(a, st);
    switch (st) {
    case ST_INTRO:
        a->intro_t = 0;
        a->intro_i = 0;
        a->intro_skip = false;
        a->frozen = true;
        a->playing = false;
        break;
    case ST_PLAY:
        a->frozen = false;
        a->playing = true;
        if (prev != ST_PAUSE) {
            a->w_fps_t0 = aos_hal_uptime_ms();
            a->w_frames = 0;
        }
        break;
    case ST_PAUSE:
        a->playing = false;
        a->frozen = false;
        if (a->shown >= 0) canvas_show(a, a->shown);
        mh_ui_pause_fill(a);
        break;
    case ST_RESULT:
        a->playing = a->frozen = false;
        if (a->shown >= 0) canvas_show(a, a->shown);
        break;
    default:
        a->playing = a->frozen = false;
        break;
    }
    mh_ui_show(a, st);
}

/* ---- the loading bar ---- */

/* the pack's size on the card, all its parts, folded into a byte: the
 * measures are kept under keys that carry it, so a new pack starts over
 * from the guess instead of trusting what the old one read */
static uint8_t pak_tag(void)
{
    uint32_t total = 0;
    for (int i = 0; i < 8; i++) {
        char path[128];
        if (i) snprintf(path, sizeof path, "%s/monsterhop.pak.%d", aos_hal_path_apps(), i);
        else snprintf(path, sizeof path, "%s/monsterhop.pak", aos_hal_path_apps());
        FILE *f = fopen(path, "rb");
        if (!f) break;
        fseek(f, 0, SEEK_END);
        total += (uint32_t)ftell(f);
        fclose(f);
    }
    return (uint8_t)(total ^ (total >> 8) ^ (total >> 16) ^ (total >> 24));
}

static void load_begin(app_t *a, const char *key, uint32_t guess)
{
    int32_t v = 0;
    snprintf(a->ld_key, sizeof a->ld_key, "%s%02x", key, a->ld_tag);
    a->ld_from = mh_art_read();
    a->ld_est = aos_hal_pref_get_i32(key, &v) && v > 0 ? (uint32_t)v : guess;
    if (a->ld_est < 1) a->ld_est = 1;
    a->ld_ms = 0;
    a->ld_on = true;
}

static void load_end(app_t *a)
{
    if (!a->ld_on) return;
    a->ld_on = false;
    mh_ui_load_bar(a, false, 0);
    /* what it really read is next time's measure */
    uint32_t used = mh_art_read() - a->ld_from;
    uint32_t d = used > a->ld_est ? used - a->ld_est : a->ld_est - used;
    if (used > 0 && d * 20 > a->ld_est) aos_hal_pref_set_i32(a->ld_key, (int32_t)used);
}

static void load_tick(app_t *a, int dt)
{
    if (!a->ld_on) return;
    a->ld_ms += (uint32_t)dt;
    if (a->ld_ms < 2000) return;
    uint32_t used = mh_art_read() - a->ld_from;
    int pct = (int)((uint64_t)used * 100 / a->ld_est);
    mh_ui_load_bar(a, true, pct > 97 ? 97 : pct);
}

void mha_level_start(app_t *a, int idx)
{
    {
        /* a level reads its zone's blocks and props, its monsters and the
         * common objects: the zone's share of the pack plus a guess */
        static const char *const zp[] = { "city_", "castle_", "desert_", "forest_" };
        char key[12];
        snprintf(key, sizeof key, idx < 0 ? "mh_ldt" : "mh_ld%d", idx);
        const mh_level_info_t *li = mha_level_info(idx);
        uint32_t guess = (li && li->zone < 4 ? mh_art_prefix_bytes(zp[li->zone]) : 0) + 1500u * 1024u;
        load_begin(a, key, guess);
    }
    a->level = idx;
    a->playing = false;
    a->frozen = false;
    a->job_level = idx;
    free(a->hud.title.a);
    a->hud.title.a = NULL;
    text_mask(&a->hud.title, mha_level_title(idx), aos_font_title);
    mh_ui_loading_text(a, mha_level_title(idx));
    mh_ui_before_job(a, UJ_FREE_MAP);
    s_pending_ui = 0;
    mha_set_state(a, ST_LOADING);
    /* if a job is still running, the level goes as soon as it ends */
    job(a, JOB_LEVEL);
}

/* out of a level from the pause: the level goes and the menus' pictures
 * come back, as after the results */
void mha_level_leave(app_t *a)
{
    a->playing = a->frozen = false;
    mh_ui_before_job(a, UJ_MENU);
    if (!job(a, JOB_MENU_BACK)) s_pending_ui = UJ_MENU;
}

void mha_resume(app_t *a)
{
    if (a->state == ST_PAUSE) mha_set_state(a, ST_PLAY);
}

static void pause_show(app_t *a)
{
    if (a->state == ST_PLAY) mha_set_state(a, ST_PAUSE);
}

/* ---- results ---- */

static void result_show(app_t *a)
{
    const mh_game_t *g = &a->game;
    mh_prog_t *p = &a->prog;
    bool won = g->state == GS_WON;
    int stars = mh_game_stars(g);
    int earned = g->coins + (won ? 20 + 10 * stars : 0);
    if (a->prog.diff == DIFF_HARD) earned = earned * 3 / 2;
    p->coins += earned;
    bool best = false, sticker = false;
    int lv = a->level;
    if (lv >= 0 && lv < MH_LEVELS) {
        if (stars > p->stars[lv]) p->stars[lv] = (uint8_t)stars;
        int ms = (int)(g->t * 1000.0f);
        if (won && (p->best_ms[lv] == 0 || ms < p->best_ms[lv])) {
            best = p->best_ms[lv] != 0;
            p->best_ms[lv] = ms;
        }
        if (g->sticker && !(p->stickers & (1u << lv))) {
            p->stickers |= 1u << lv;
            sticker = true;
        }
    }
    p->stat[SX_KEYS] += g->keys;
    p->stat[SX_COINS] += g->coins;
    p->stat[SX_HOPS] += g->hops;
    p->stat[SX_LOST] += g->lost;
    p->stat[SX_PLAY_S] += (int32_t)g->t;
    if (won) p->stat[SX_LEVELS]++;
    uint32_t before = p->trophies;
    if (won && g->lost == 0) p->trophies |= 1u << TR_CLEAN;
    if (won && a->prog.diff == DIFF_HARD) p->trophies |= 1u << TR_HARD;
    mh_prog_trophies(p);
    uint32_t new_tr = p->trophies & ~before;
    mha_save(a);
    mh_ui_result_fill(a, won, stars, earned, best, sticker, new_tr);
    mha_set_state(a, ST_RESULT);
    uint32_t ms = (uint32_t)(aos_hal_uptime_ms() - a->w_fps_t0);
    unsigned fps10 = ms ? (unsigned)((uint64_t)a->w_frames * 10000u / ms) : 0;
    aos_hal_log("mhop", "level %d %s, %u frames in %u ms, %u.%u fps", a->level, won ? "won" : "lost",
                (unsigned)a->w_frames, (unsigned)ms, fps10 / 10, fps10 % 10);
    /* the level goes and the map comes back while the result shows */
    mh_ui_before_job(a, UJ_MENU);
    job(a, JOB_MENU_BACK);
}

/* ---- the key race (mh_link.c does the talking) ---- */

void mhl_start_play(app_t *a)
{
    a->hs.msg = MSG_GO;
    a->hs.msg_t = 0;
    mh_snd(SND_GO);
    a->over_ms = 0;
    mha_set_state(a, ST_PLAY);
}

static void race_result(app_t *a, bool left)
{
    mh_game_t *g = &a->game;
    /* the race is over: the link stays up for another one, but what the
     * other watch does from here (its lobby, its HELLOs) is not a desertion */
    if (a->link_on) a->link_state = LK_OFF;
    mh_prog_t *p = &a->prog;
    int me = left ? 0 : mh_game_points(g, false), them = left ? 0 : mh_game_points(g, true);
    bool won = !left && me > them;
    int earned = g->coins + (won ? 40 : 15);
    if (left) earned = g->coins;
    p->coins += earned;
    p->stat[SX_KEYS] += g->my_keys;
    p->stat[SX_COINS] += g->coins;
    p->stat[SX_HOPS] += g->hops;
    p->stat[SX_LOST] += g->lost;
    p->stat[SX_PLAY_S] += (int32_t)g->t;
    p->stat[SX_RACES]++;
    if (won) p->stat[SX_RACES_WON]++;
    uint32_t before = p->trophies;
    mh_prog_trophies(p);
    uint32_t new_tr = p->trophies & ~before;
    mha_save(a);
    mh_ui_race_fill(a, left ? -1 : won ? 1 : 0, me, them, earned, new_tr);
    mha_set_state(a, ST_RESULT);
    aos_hal_log("mhop", "race on level %d: %d to %d%s", a->level, me, them, left ? " (left)" : "");
    mh_ui_before_job(a, UJ_MENU);
    job(a, JOB_MENU_BACK);
}

/* ---- input ---- */

/* a swipe on the screen to one of the grid's four ways: the projected axes
 * are +Y (20, -42), +X (60, 14): the most aligned wins */
static int swipe_dir(int dx, int dy)
{
    static const float ax[4][2] = { { 0.4299f, -0.9029f }, { 0.9738f, 0.2272f }, { -0.4299f, 0.9029f },
                                    { -0.9738f, -0.2272f } };
    int best = 0;
    float bv = -1e9f;
    for (int d = 0; d < 4; d++) {
        float v = ax[d][0] * dx + ax[d][1] * dy;
        if (v > bv) {
            bv = v;
            best = d;
        }
    }
    return best;
}

static app_t *app_of(lv_event_t *e)
{
    return (app_t *)lv_event_get_user_data(e);
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
    if (a->state == ST_INTRO) {
        if (code == LV_EVENT_PRESSED) a->intro_skip = true;
        return;
    }
    if (a->state != ST_PLAY) return;
    if (code == LV_EVENT_PRESSED) {
        a->p0 = p;
        a->pressed = true;
        a->dragged = false;
        return;
    }
    if (!a->pressed) return;
    if (code == LV_EVENT_PRESSING) {
        int dx = p.x - a->p0.x, dy = p.y - a->p0.y;
        if (!a->dragged && dx * dx + dy * dy >= SWIPE_PX * SWIPE_PX) {
            a->in_hop = 1 + swipe_dir(dx, dy);
            a->dragged = true;
            a->last_hop_ms = lv_tick_get();
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->pressed = false;
        if (a->dragged || code == LV_EVENT_PRESS_LOST) return;
        if (a->p0.x < MH_PAUSE_R && a->p0.y < MH_PAUSE_R) {
            a->want_pause = true;
            return;
        }
        /* a tap: a hop up the screen */
        a->in_hop = 1 + DIR_N;
        a->last_hop_ms = lv_tick_get();
    }
}

/* back, from a swipe right or the runtime */
static bool go_back(app_t *a)
{
    switch (a->state) {
    case ST_MENU: case ST_BOOT:
        return false;
    case ST_PLAY:
        /* in a race the clock can't stop: back leaves it */
        if (a->lk_race) {
            mhl_end(a);
            race_result(a, true);
            return true;
        }
        a->want_pause = true;
        return true;
    case ST_PAUSE:
        mha_resume(a);
        return true;
    case ST_LOADING: case ST_INTRO:
        return true;
    case ST_MAP:
        mha_set_state(a, ST_MENU);
        return true;
    case ST_HOUSE: case ST_RESULT:
        mha_set_state(a, ST_MAP);
        return true;
    case ST_LOBBY:
        mhl_end(a);
        mha_set_state(a, ST_HOUSE);
        return true;
    case ST_SHOP: case ST_ALBUM: case ST_TROPHIES: case ST_STATS: case ST_SETTINGS:
        mha_set_state(a, ST_HOUSE);
        return true;
    default:
        return true;
    }
}

static void take_gesture(app_t *a)
{
    int gst = aos_ui_take_gesture();
    if (gst == AOS_TOUCH_GESTURE_NONE) return;
    if (a->state != ST_PLAY) {
        if (gst == AOS_TOUCH_GESTURE_RIGHT && !go_back(a)) a->want_exit = true;
        return;
    }
    if (a->lk_race) a->want_pause = false;
    /* the chip's own swipe: on the board a fast one never reaches LVGL */
    if ((uint32_t)(lv_tick_get() - a->last_hop_ms) < 200) return;
    int d = gst == AOS_TOUCH_GESTURE_UP ? DIR_N : gst == AOS_TOUCH_GESTURE_DOWN ? DIR_S :
            gst == AOS_TOUCH_GESTURE_LEFT ? DIR_W : DIR_E;
    a->in_hop = 1 + d;
    a->last_hop_ms = lv_tick_get();
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev || a->state == ST_PLAY) return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        if (!go_back(a)) a->want_exit = true;
    }
}

/* the game's events become sounds and stats here, on the UI side */
static void play_events(app_t *a)
{
    while (a->ev_r != a->ev_w) {
        uint32_t ev = a->ev_ring[a->ev_r & 15];
        a->ev_r++;
        if (ev & EV_HURT) a->prog.stat[SX_CAUGHT]++;
        if (ev & EV_SPLASH) a->prog.stat[SX_DROWNED]++;
        if (ev & EV_FALL) a->prog.stat[SX_FELL]++;
        static const struct { uint32_t ev; uint8_t snd; } map[] = {
            { EV_WIN, SND_WIN }, { EV_OVER, SND_OVER }, { EV_OPEN, SND_OPEN }, { EV_KEY, SND_KEY },
            { EV_STICKER, SND_LIFE }, { EV_HURT, SND_HURT }, { EV_SPLASH, SND_SPLASH }, { EV_FALL, SND_FALL },
            { EV_TIMEUP, SND_TIMEUP }, { EV_CHECK, SND_CHECK }, { EV_LIFE, SND_LIFE }, { EV_TIME, SND_TIME },
            { EV_CHEST, SND_CHEST }, { EV_LEVER, SND_LEVER }, { EV_PUSH, SND_PUSH }, { EV_SPLASHC, SND_SPLASH },
            { EV_STOMP, SND_STOMP }, { EV_CAST, SND_CAST }, { EV_HOWL, SND_HOWL }, { EV_SUPER, SND_SUPER },
            { EV_HOP, SND_HOP }, { EV_COIN, SND_COIN }, { EV_BUMP, SND_BUMP }, { EV_LOW_TIME, SND_LOW },
            { EV_LAND, SND_LAND }, { EV_KEYLOST, SND_HURT }, { EV_RIVAL_KEY, SND_BUMP },
        };
        int played = 0;
        for (size_t k = 0; k < sizeof map / sizeof map[0] && played < 3; k++) {
            if (ev & map[k].ev) {
                mh_snd(map[k].snd);
                played++;
            }
        }
    }
}

/* ---- the timer ---- */

#ifdef AOS_SIM_BUILTIN
static int s_sim_race = -1;     /* MH_RACE: the level to race, host side */
#endif

static void boot_done(app_t *a)
{
    if (!a->job_ok) {
        a->ld_on = false;
        mh_ui_load_bar(a, false, 0);
        mh_ui_boot_text(a, _("Falta monsterhop.pak en la tarjeta"));
        return;
    }
    load_end(a);
    mha_set_state(a, ST_MENU);
#ifdef AOS_SIM_BUILTIN
    /* Development switches (getenv() is NULL on the board):
     *   MH_LEVEL=<0..15>|test   straight into that level
     *   MH_DIFF=0..2            the difficulty
     *   MH_UNLOCK=1             every level open
     *   MH_COINS=n              coins for the shop
     *   MH_TRAIL=1..4           wear that trail (0 none)
     *   MH_START=x,y            the level starts in that cell
     *   MH_SCREEN=map|house|shop|wardrobe|album|trophies|stats|settings
     *   MH_RACE=<0..15>         into the lobby; the host starts that level
     *                           (two sims: AOS_SIM_LINK_PORT/_PARTNER)
     */
    const char *e;
    if ((e = getenv("MH_UNLOCK")) && e[0]) a->dev_auto = true;
    if ((e = getenv("MH_DIFF")) && e[0]) a->prog.diff = atoi(e) % DIFF_N;
    if ((e = getenv("MH_COINS")) && e[0]) a->prog.coins = atoi(e);
    if ((e = getenv("MH_TRAIL")) && e[0]) {
        a->prog.eq[CAT_TRAIL] = (int8_t)(atoi(e) - 1);
        mha_outfit(a);
    }
    if ((e = getenv("MH_SCREEN")) && e[0]) {
        static const struct { const char *n; int st; } sc[] = {
            { "map", ST_MAP }, { "house", ST_HOUSE }, { "shop", ST_SHOP }, { "wardrobe", ST_SHOP },
            { "album", ST_ALBUM }, { "trophies", ST_TROPHIES }, { "stats", ST_STATS }, { "settings", ST_SETTINGS },
        };
        for (size_t k = 0; k < sizeof sc / sizeof sc[0]; k++) {
            if (!strcmp(e, sc[k].n)) {
                if (sc[k].st == ST_SHOP) mh_ui_open_shop(a, !strcmp(e, "shop"));
                else mha_set_state(a, sc[k].st);
            }
        }
    }
    if ((e = getenv("MH_LEVEL")) && e[0]) mha_level_start(a, e[0] == 't' ? -1 : atoi(e));
    if ((e = getenv("MH_RACE")) && e[0]) {
        char nm[32];
        s_sim_race = atoi(e) % MH_LEVELS;
        mha_link_available(a, nm, sizeof nm);
    }
#endif
}

#ifdef AOS_SIM_BUILTIN
static void sim_race(app_t *a)
{
    if (s_sim_race < 0) return;
    char nm[32];
    if (a->state != ST_LOBBY && !a->link_on) {
        if (mha_link_available(a, nm, sizeof nm)) mha_link_begin(a);
    } else if (a->state == ST_LOBBY && a->link_peer_nonce) {
        if (a->is_host) {
            a->link_level = s_sim_race;
            mhl_go(a);
        }
        s_sim_race = -1;
    }
}
#endif

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
    mhl_tick(a);
    load_tick(a, dt);
#ifdef AOS_SIM_BUILTIN
    sim_race(a);
#endif
    play_events(a);
    mh_audio_tick();
    mh_ui_tick(a, dt);

    /* a job finished */
    if (a->job_done && a->job == JOB_NONE) {
        a->job_done = false;
        int j = s_last_job;
        if (a->state == ST_BOOT) {
            boot_done(a);
        } else if (a->state == ST_LOADING) {
            if (j == JOB_LEVEL) load_end(a);
            if (j == JOB_LEVEL && a->job_ok && a->level_ok && a->lk_race) {
                /* a race waits for the other watch (mh_link.c starts it) */
                a->spare_checked = false;
                a->lk_loaded = true;
            } else if (j == JOB_LEVEL && a->job_ok && a->level_ok) {
                a->spare_checked = false;
                mha_set_state(a, ST_INTRO);
            } else if (j != JOB_LEVEL) {
                /* another job ended first: now the level */
                job(a, JOB_LEVEL);
            } else {
                aos_ui_toast(_("No se pudo cargar el nivel"), 1800);
                mha_set_state(a, ST_MAP);
            }
        } else {
            mh_ui_job_done(a, j == JOB_UI ? s_done_ui : UJ_MENU);
            s_done_ui = 0;
        }
    }
    if (s_pending_ui && a->job == JOB_NONE && !a->playing && !a->frozen && a->state != ST_LOADING) {
        int w = s_pending_ui;
        s_pending_ui = 0;
        mha_ui_job(a, w);
    }

    if (a->lk_abort) {
        a->lk_abort = false;
        if (a->state == ST_LOADING) {
            mh_ui_before_job(a, UJ_MENU);
            if (!job(a, JOB_MENU_BACK)) s_pending_ui = UJ_MENU;
            mha_set_state(a, ST_HOUSE);
        }
    }
    switch (a->state) {
    case ST_BOOT:
        break;
    case ST_INTRO:
        push_frame(a);
        if (a->intro_i > MH_KEYS || a->intro_skip) {
            a->hs.msg = MSG_GO;
            a->hs.msg_t = 0;
            mh_snd(SND_GO);
            mha_set_state(a, ST_PLAY);
        }
        break;
    case ST_PLAY:
        if (a->lk_race) a->want_pause = a->want_map = false;
        if (a->want_pause || a->want_map) {
            a->want_pause = a->want_map = false;
            pause_show(a);
            break;
        }
        push_frame(a);
        if (a->lk_race) {
            /* the race is decided once someone is out or the clock ran out;
             * a moment more so the other's EXIT can arrive */
            if (mh_game_race_over(&a->game)) {
                if (!a->over_ms) a->over_ms = a->st_ms ? a->st_ms : 1;
                if (a->st_ms - a->over_ms > 1600) race_result(a, false);
            }
            break;
        }
        if ((a->game.state == GS_WON || a->game.state == GS_OVER) && a->game.st > 1.4f) result_show(a);
        break;
    default:
        a->want_pause = a->want_map = false;
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
    if (a->state == ST_INTRO) {
        if (action == AOS_BUTTON_CLICK) a->intro_skip = true;
        return true;
    }
    if (a->state != ST_PLAY) return false;
    if (action == AOS_BUTTON_CLICK) a->in_action = true;
    else if (action == AOS_BUTTON_LONG && !a->lk_race) a->want_map = true;
    return true;
}

static void app_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a && a->link_on) mhl_end(a);
    if (a && a->state == ST_PLAY && !a->lk_race) pause_show(a);
}

static void free_all(app_t *a)
{
    level_free(a);
    mh_cast_free(&a->cast);
    mh_ui_free(a);
    for (int i = 0; i < MH_NFB; i++) {
        free(a->fb[i]);
        a->fb[i] = NULL;
    }
    free(a->cv);
    a->cv = NULL;
    free(a->band);
    a->band = NULL;
    mh_hud_free(&a->hud);
    mh_art_close();
}

static void *mh_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)mh_calloc(1, sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("mhop", "opening | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        a->fb[i] = (uint16_t *)mh_malloc((size_t)MH_W * MH_H * 2);
        if (!a->fb[i]) ok = false;
    }
    a->nfb = 3;
    a->cv = (uint16_t *)mh_malloc((size_t)MH_W * MH_H * 2);
    a->band = (uint16_t *)mh_malloc_internal((size_t)MH_W * MH_BAND * 2);
    if (!ok || !a->cv) {
        aos_hal_log("mhop", "out of memory");
        free_all(a);
        free(a);
        return NULL;
    }
    memset(a->fb[0], 0, (size_t)MH_W * MH_H * 2);
    a->shown = -1;
    a->level = -1;
    a->loaded_level = -2;
    a->root = root;
    mh_prog_load(&a->prog);
    mha_outfit(a);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    memset(a->cv, 0, (size_t)MH_W * MH_H * 2);
    lv_canvas_set_buffer(a->canvas, a->cv, MH_W, MH_H, LV_COLOR_FORMAT_RGB565);
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
    mh_ui_build(a, root);

    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    /* apps/monsterhop_dev.txt on the card, for measuring on the board:
     * "unlock" every level open */
    {
        char path[96], buf[64] = "";
        snprintf(path, sizeof path, "%s/monsterhop_dev.txt", aos_hal_path_apps());
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = 0;
            fclose(f);
            if (strstr(buf, "unlock")) a->dev_auto = true;
            aos_hal_log("mhop", "dev switches: %s", buf);
        }
    }
    mh_audio_open();
    mh_audio_enable(a->prog.sfx, a->prog.music);
    a->state = ST_BOOT;
    mh_ui_show(a, ST_BOOT);
    a->ld_tag = pak_tag();
    load_begin(a, "mh_ldb", 3u * 1024u * 1024u);
    s_last_job = JOB_BOOT;
    a->job = JOB_BOOT;
    if (!aos_hal_worker_start_on("mhop", worker_fn, a, WORKER_STACK, 0, 5)) aos_hal_log("mhop", "no worker");
    a->timer = lv_timer_create(frame, TICK_MS, a);
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("mhop", "ready | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    return a;
}

static void mh_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    a->closing = true;
    a->playing = a->frozen = false;
    if (a->link_on) mhl_end(a);
    if (a->timer) lv_timer_delete(a->timer);
    aos_hal_worker_stop();
    mh_audio_close();
    if (a->root) lv_obj_clean(a->root);
    mha_save(a);
    free_all(a);
    free(a);
}

/* The launcher icon: a golden key under a green monster eye. */
static const uint8_t MH_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -10, -8, 28, 28, 14, AIC_C_LIT(0xFFC83A), 255),
    AIC_RECT(AIC_CENTER, -10, -8, 12, 12, 6, AIC_C_LIT(0x2A1740), 255),
    AIC_RECT(AIC_CENTER, 12, 4, 34, 8, 3, AIC_C_LIT(0xFFC83A), 255),
    AIC_RECT(AIC_CENTER, 22, 12, 6, 10, 2, AIC_C_LIT(0xFFC83A), 255),
    AIC_RECT(AIC_CENTER, 12, 12, 6, 8, 2, AIC_C_LIT(0xFFC83A), 255),
    AIC_END
};

static bool mh_init(aos_app_t *app)
{
    app->desc.id       = "demo.monsterhop";
    app->desc.name     = "Monster Hop";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, MH_ICON, sizeof MH_ICON);
    app->desc.color_a  = 0x7A3AE0;
    app->desc.color_b  = 0x1A6A3A;
    app->desc.order    = 160;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    app->create  = mh_create;
    app->destroy = mh_destroy;
    app->hide    = app_hide;
    app->back    = app_back;
    app->button  = app_button;
    return true;
}

AOS_APP_ENTRY(mh_init);
