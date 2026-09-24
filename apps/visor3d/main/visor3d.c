/*
 * VISOR 3D - 3D models on the watch (v0.6.0).
 *
 * The files live in <card>/3d/: STL, binary or ASCII, straight from any CAD
 * or slicer, and M3D, what the portal's /3d page makes out of STL, OBJ and
 * GLB (with colours). A list, then the model:
 *
 *   one finger ....... turn it (and let go with a flick: it keeps turning)
 *   two fingers ...... zoom about the point between them, and move it
 *   double tap ....... back to the first view
 *   tap .............. the turntable on or off
 *   long press ....... solid or wireframe
 *
 * How it is built, and why:
 *
 *   - The model is drawn by the worker on core 0 (v3_raster.c), never in
 *     LVGL's task: a busy LVGL task is what used to steal the touch's samples
 *     (docs/GESTURES.md), and a 3D view is the busiest thing on the watch.
 *     LVGL's side only moves the view and pushes finished frames to the
 *     panel with aos_hal_display_blit(), three slots, the newest wins.
 *   - While anything moves it draws at half resolution and doubles the
 *     pixels; once the view has been still for a moment, one frame at full.
 *   - The bottom strip (y 400..447), where the glass does not read reliably,
 *     is an LVGL label: name, triangles, frames per second, mode.
 *   - Loading (v3_mesh.c) welds the STL's loose triangles and reduces a model
 *     over V3_BUDGET triangles, in the worker, with its progress in the strip.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_gesture.h"
#include "lvgl.h"

#include "v3_mesh.h"
#include "v3_raster.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define VW          AOS_SCREEN_W        /* 368 */
#define VH          400                 /* the view; the strip below is text */
#define NSLOT       3
#define MAX_FILES   48
#define NAME_LEN    64
#define TICK_MS     10
#define STILL_MS    160                 /* still this long: draw at full size */
#define WORKER_STACK (8 * 1024)

enum { SLOT_FREE = 0, SLOT_BUSY, SLOT_READY, SLOT_SHOWN };

typedef struct {
    aos_app_t  *self;
    lv_obj_t   *root;
    lv_obj_t   *list;
    lv_obj_t   *viewer;
    lv_obj_t   *touch;
    lv_obj_t   *strip;
    lv_obj_t   *canvas;                 /* the simulator's way to see a frame */
    uint16_t   *cv;
    lv_timer_t *timer;

    char        names[MAX_FILES][NAME_LEN];
    int         count;
    char        path[160];
    char        name[NAME_LEN];

    /* the worker's side */
    v3_mesh_t   mesh;
    v3_scratch_t scr;
    volatile bool want_load, want_unload, loading, mesh_ready, load_failed;
    volatile int  progress;
    uint16_t   *slot[NSLOT];
    volatile uint8_t  slot_state[NSLOT];
    volatile uint32_t slot_seq[NSLOT];
    uint32_t    seq;
    uint16_t   *zb, *small, *zsmall;
    volatile uint32_t view_seq;         /* bumped by LVGL whenever the view moves */
    uint32_t    drawn_seq;
    bool        drawn_full;
    uint32_t    frames, drawn_tris;

    /* LVGL's side */
    v3_view_t   view;
    float       vyaw, vpitch;           /* spin after a flick, rad/s */
    bool        turntable;
    bool        touching;
    uint32_t    moved_ms;
    int         shown;
    uint32_t    fps_ms, fps_frames;
    unsigned    fps;
    bool        viewing;
} app_t;

/* ---------------------------------------------------------------------------
 * Files
 * ------------------------------------------------------------------------- */

static const char *models_dir(void)
{
    static char dir[96];
    if (!dir[0]) {
        const char *root = aos_hal_path_sd_root();
        snprintf(dir, sizeof dir, "%s/3d", root ? root : aos_hal_path_data());
    }
    return dir;
}

static bool is_model(const char *n)
{
    const char *dot = strrchr(n, '.');
    return dot && (!strcasecmp(dot, ".stl") || !strcasecmp(dot, ".m3d"));
}

static void scan(app_t *a)
{
    a->count = 0;
    mkdir(models_dir(), 0777);
    DIR *d = opendir(models_dir());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && a->count < MAX_FILES) {
        if (e->d_name[0] == '.' || !is_model(e->d_name)) continue;
        snprintf(a->names[a->count++], NAME_LEN, "%.63s", e->d_name);
    }
    closedir(d);
}

/* ---------------------------------------------------------------------------
 * The worker
 * ------------------------------------------------------------------------- */

static int free_slot(app_t *a)
{
    for (int i = 0; i < NSLOT; i++)
        if (a->slot_state[i] == SLOT_FREE) return i;
    return -1;
}

static void unload(app_t *a)
{
    a->mesh_ready = false;
    v3_scratch_free(&a->scr);
    v3_mesh_free(&a->mesh);
}

static void worker(void *arg)
{
    app_t *a = (app_t *)arg;
    while (!aos_hal_worker_should_stop()) {
        if (a->want_unload) {
            unload(a);
            a->want_unload = false;
            continue;
        }
        if (a->want_load) {
            a->want_load = false;
            unload(a);
            a->loading = true;
            a->load_failed = false;
            a->progress = 0;
            uint64_t t0 = aos_hal_uptime_ms();
            bool ok = v3_mesh_load(&a->mesh, a->path, &a->progress) &&
                      v3_scratch_alloc(&a->scr, a->mesh.nv);
            uint32_t fi = 0, fp = 0;
            aos_hal_heap_info(&fi, &fp);
            if (ok) {
                aos_hal_log("visor3d", "%s: %d triangles in the file, %d drawn, %d vertices, "
                            "%u ms | psram %u", a->path, a->mesh.nt_file, a->mesh.nt, a->mesh.nv,
                            (unsigned)(aos_hal_uptime_ms() - t0), (unsigned)fp);
                a->drawn_seq = a->view_seq - 1;     /* draw at once */
                a->mesh_ready = true;
            } else {
                aos_hal_log("visor3d", "%s: %s", a->path, a->mesh.err[0] ? a->mesh.err : "no memory");
                a->load_failed = true;
                unload(a);
            }
            a->loading = false;
            continue;
        }
        if (!a->mesh_ready) {
            aos_hal_worker_sleep(20);
            continue;
        }
        /* What to draw: a moving view at half size; the same view, still for
         * a moment, once more at full size; nothing new, nothing. */
        uint32_t vs = a->view_seq;
        bool moving = vs != a->drawn_seq;
        bool settle = !moving && !a->drawn_full &&
                      (uint32_t)aos_hal_uptime_ms() - a->moved_ms > STILL_MS;
        if (!moving && !settle) {
            aos_hal_worker_sleep(8);
            continue;
        }
        int i = free_slot(a);
        if (i < 0) {
            aos_hal_worker_sleep(3);
            continue;
        }
        a->slot_state[i] = SLOT_BUSY;
        v3_view_t v = a->view;
        if (moving) {
            v.px *= 0.5f;
            v.py *= 0.5f;
            a->drawn_tris = (uint32_t)v3_render(&a->mesh, &v, &a->scr, a->small, a->zsmall, VW / 2, VH / 2);
            v3_upscale2(a->small, a->slot[i], VW, VH);
            a->drawn_full = false;
        } else {
            a->drawn_tris = (uint32_t)v3_render(&a->mesh, &v, &a->scr, a->slot[i], a->zb, VW, VH);
            a->drawn_full = true;
        }
        a->drawn_seq = vs;
        a->frames++;
        a->slot_seq[i] = ++a->seq;
        a->slot_state[i] = SLOT_READY;
    }
}

/* ---------------------------------------------------------------------------
 * LVGL's side: the view, the frames, the strip
 * ------------------------------------------------------------------------- */

static void view_home(app_t *a)
{
    a->view.yaw = 0.6f;
    a->view.pitch = 0.35f;
    a->view.scale = 1.0f;
    a->view.px = a->view.py = 0;
    a->vyaw = a->vpitch = 0;
}

static void view_moved(app_t *a)
{
    a->moved_ms = (uint32_t)aos_hal_uptime_ms();
    a->view_seq++;
}

static void push_frame(app_t *a)
{
    int best = -1;
    uint32_t bs = 0;
    for (int i = 0; i < NSLOT; i++) {
        if (a->slot_state[i] == SLOT_READY && (best < 0 || a->slot_seq[i] > bs)) {
            best = i;
            bs = a->slot_seq[i];
        }
    }
    if (best < 0) return;
    for (int i = 0; i < NSLOT; i++)
        if (i != best && a->slot_state[i] == SLOT_READY) a->slot_state[i] = SLOT_FREE;
    if (!aos_hal_display_blit(0, 0, VW, VH, a->slot[best]) && a->cv) {
        /* the simulator: through a canvas, in LVGL's byte order */
        const uint16_t *s = a->slot[best];
        for (int k = 0; k < VW * VH; k++) a->cv[k] = (uint16_t)((s[k] >> 8) | (s[k] << 8));
        lv_obj_invalidate(a->canvas);
    }
    if (a->shown >= 0 && a->shown != best) a->slot_state[a->shown] = SLOT_FREE;
    a->slot_state[best] = SLOT_SHOWN;
    a->shown = best;
    a->fps_frames++;
}

static void strip_text(app_t *a)
{
    char buf[128];
    if (a->loading) {
        snprintf(buf, sizeof buf, "%s  %d%%", _("Cargando..."), a->progress);
    } else if (a->load_failed) {
        snprintf(buf, sizeof buf, "%s: %s", _("No se pudo abrir"), a->mesh.err[0] ? a->mesh.err : "?");
    } else if (a->mesh_ready) {
        const char *mode = a->view.mode == V3_WIRE ? _("alambre") : _("sólido");
        if (a->mesh.nt < a->mesh.nt_file) {
            snprintf(buf, sizeof buf, "%.24s · %d/%d tri · %u fps · %s", a->name, a->mesh.nt,
                     a->mesh.nt_file, a->fps, mode);
        } else {
            snprintf(buf, sizeof buf, "%.24s · %d tri · %u fps · %s", a->name, a->mesh.nt, a->fps, mode);
        }
    } else {
        buf[0] = 0;
    }
    lv_label_set_text(a->strip, buf);
}

static void tick(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (!a->viewing) return;
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    float dt = TICK_MS / 1000.0f;

    /* the flick's spin, dying out; the turntable, steady */
    if (!a->touching && (fabsf(a->vyaw) > 0.02f || fabsf(a->vpitch) > 0.02f)) {
        a->view.yaw += a->vyaw * dt;
        a->view.pitch += a->vpitch * dt;
        a->vyaw *= 0.95f;
        a->vpitch *= 0.95f;
        view_moved(a);
    } else if (a->turntable && !a->touching) {
        a->view.yaw += 0.6f * dt;
        view_moved(a);
    }
    push_frame(a);

    if (now - a->fps_ms >= 500) {
        a->fps = (unsigned)(a->fps_frames * 1000 / (now - a->fps_ms ? now - a->fps_ms : 1));
        a->fps_frames = 0;
        a->fps_ms = now;
        strip_text(a);
    }
}

static void gesture_cb(const aos_gesture_event_t *ev, void *user)
{
    app_t *a = (app_t *)user;
    if (!a->mesh_ready) return;
    const float K = 0.010f;             /* radians per pixel of finger */
    switch (ev->type) {
    case AOS_GESTURE_DRAG_BEGIN:
    case AOS_GESTURE_PINCH_BEGIN:
        a->touching = true;
        a->vyaw = a->vpitch = 0;
        break;
    case AOS_GESTURE_DRAG:
        a->view.yaw += ev->dx * K;
        a->view.pitch += ev->dy * K;
        view_moved(a);
        break;
    case AOS_GESTURE_DRAG_END:
        a->touching = false;
        a->vyaw = ev->vx * K;
        a->vpitch = ev->vy * K;
        break;
    case AOS_GESTURE_PINCH: {
        float ns = a->view.scale * ev->scale;
        if (ns < 0.3f) ns = 0.3f;
        if (ns > 10.0f) ns = 10.0f;
        float k = ns / a->view.scale;
        /* the point between the fingers stays under them */
        float ox = VW * 0.5f + a->view.px, oy = VH * 0.5f + a->view.py;
        a->view.px = ev->x - VW * 0.5f - (ev->x - ox) * k + ev->dx;
        a->view.py = ev->y - VH * 0.5f - (ev->y - oy) * k + ev->dy;
        a->view.scale = ns;
        view_moved(a);
        break;
    }
    case AOS_GESTURE_PINCH_END:
        a->touching = false;
        break;
    case AOS_GESTURE_DOUBLE_TAP:
        view_home(a);
        a->turntable = false;
        view_moved(a);
        break;
    case AOS_GESTURE_TAP:
        a->turntable = !a->turntable;
        break;
    case AOS_GESTURE_LONG_PRESS:
        a->view.mode = (a->view.mode + 1) % V3_MODES;
        view_moved(a);
        strip_text(a);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * The two screens
 * ------------------------------------------------------------------------- */

static void show_list(app_t *a)
{
    a->viewing = false;
    a->want_unload = true;
    a->self->desc.flags &= ~(uint32_t)(AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG);
    lv_obj_add_flag(a->viewer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(a->root);         /* what was blitted is not LVGL's */
}

static void open_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (i < 0 || i >= a->count || a->loading) return;
    snprintf(a->name, sizeof a->name, "%.63s", a->names[i]);
    snprintf(a->path, sizeof a->path, "%.90s/%.63s", models_dir(), a->names[i]);
    view_home(a);
    a->view.mode = V3_SOLID;
    a->turntable = false;
    a->mesh_ready = false;
    a->loading = true;                  /* until the worker says otherwise */
    a->want_load = true;
    a->viewing = true;
    a->self->desc.flags |= AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    lv_obj_add_flag(a->list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->viewer, LV_OBJ_FLAG_HIDDEN);
    strip_text(a);
}

static void build_list(app_t *a)
{
    a->list = lv_obj_create(a->root);
    lv_obj_remove_style_all(a->list);
    lv_obj_set_size(a->list, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_flex_flow(a->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(a->list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(a->list, 8, 0);
    lv_obj_set_style_pad_top(a->list, 22, 0);
    lv_obj_set_style_pad_bottom(a->list, 60, 0);
    lv_obj_set_scroll_dir(a->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a->list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = aos_label(a->list, _("Visor 3D"), aos_font_title, AOS_C_TEXT);
    (void)title;

    if (a->count == 0) {
        char msg[200];
        snprintf(msg, sizeof msg, _("No hay modelos.\nCopiá archivos .stl a\n%s\no convertí OBJ y GLB\nen el portal (/3d)."),
                 models_dir());
        lv_obj_t *l = aos_label(a->list, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_width(l, AOS_SCREEN_W - 40);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (int i = 0; i < a->count; i++) {
        lv_obj_t *row = lv_obj_create(a->list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 56, 56);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, a);
        lv_obj_t *l = aos_label(row, a->names[i], aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, AOS_SCREEN_W - 100);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void build_viewer(app_t *a)
{
    a->viewer = lv_obj_create(a->root);
    lv_obj_remove_style_all(a->viewer);
    lv_obj_set_size(a->viewer, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(a->viewer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->viewer, LV_OPA_COVER, 0);
    lv_obj_remove_flag(a->viewer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->viewer, LV_OBJ_FLAG_HIDDEN);

    a->cv = malloc((size_t)VW * VH * 2);
    if (a->cv) {
        memset(a->cv, 0, (size_t)VW * VH * 2);
        a->canvas = lv_canvas_create(a->viewer);
        lv_canvas_set_buffer(a->canvas, a->cv, VW, VH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(a->canvas, 0, 0);
        lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    }

    a->touch = lv_obj_create(a->viewer);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, VW, VH);
    aos_gesture_attach(a->touch, 0, gesture_cb, a);

    a->strip = lv_label_create(a->viewer);
    lv_obj_set_size(a->strip, AOS_SCREEN_W - 24, 20);   /* one line: DOTS needs the height */
    lv_label_set_long_mode(a->strip, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(a->strip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(a->strip, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(a->strip, aos_font_small, 0);
    lv_obj_set_pos(a->strip, 12, VH + 12);
}

/* ---------------------------------------------------------------------------
 * Life cycle
 * ------------------------------------------------------------------------- */

static void v3_destroy(aos_app_t *self, void *inst);

static void *v3_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    a->root = root;
    a->shown = -1;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    bool ok = true;
    for (int i = 0; i < NSLOT; i++) {
        a->slot[i] = malloc((size_t)VW * VH * 2);
        ok &= a->slot[i] != NULL;
    }
    a->zb = malloc((size_t)VW * VH * 2);
    a->small = malloc((size_t)(VW / 2) * (VH / 2) * 2);
    a->zsmall = malloc((size_t)(VW / 2) * (VH / 2) * 2);
    ok &= a->zb && a->small && a->zsmall;
    if (!ok) {
        v3_destroy(self, a);
        aos_ui_toast(_("Sin memoria"), 2000);
        return NULL;
    }

    scan(a);
    build_list(a);
    build_viewer(a);

    if (!aos_hal_worker_start_on("visor3d", worker, a, WORKER_STACK, 0, 5)) {
        aos_hal_log("visor3d", "no worker");
    }
    a->timer = lv_timer_create(tick, TICK_MS, a);
    return a;
}

static void v3_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    if (a->timer) lv_timer_delete(a->timer);
    aos_hal_worker_stop();
    unload(a);
    for (int i = 0; i < NSLOT; i++) free(a->slot[i]);
    free(a->zb);
    free(a->small);
    free(a->zsmall);
    if (self && self->root) lv_obj_clean(self->root);   /* the canvas uses cv */
    free(a->cv);
    lv_free(a);
}

static bool v3_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a && a->viewing) {
        show_list(a);
        return true;
    }
    return false;
}

/* A cube seen from a corner: three faces, three greys. */
static const uint8_t V3_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, 0, -14, 40, 40, 6, AIC_C_LIT(0x9ED8FF), 255),
    AIC_ROT(450),
    AIC_RECT(AIC_CENTER, -14, 14, 28, 38, 4, AIC_C_LIT(0x3A7BD5), 255),
    AIC_RECT(AIC_CENTER, 14, 14, 28, 38, 4, AIC_C_LIT(0x1D4E9E), 255),
    AIC_END
};

static bool v3_init(aos_app_t *app)
{
    app->desc.id       = "demo.visor3d";
    app->desc.name     = "Visor 3D";
    app->desc.icon     = LV_SYMBOL_IMAGE;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, V3_ICON, sizeof V3_ICON);
    app->desc.color_a  = 0x1C3A6B;
    app->desc.color_b  = 0x0A1428;
    app->desc.order    = 163;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN;

    app->create  = v3_create;
    app->destroy = v3_destroy;
    app->back    = v3_back;
    return true;
}

AOS_APP_ENTRY(v3_init);
