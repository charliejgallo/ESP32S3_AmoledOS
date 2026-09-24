/*
 * AmoledOS - Photo viewer.
 *
 * Lists the files in the photos folder and shows them full screen. Decoding is
 * done by LVGL: TJPGD for .jpg and LODEPNG for .png, plus the .bin files
 * already converted to the native format (the fastest to draw).
 *
 * Since v0.6 the photo is decoded at its own resolution (up to 2 MB) and
 * shown through a scale: pinch to zoom, drag to pan with a fling, double tap
 * to zoom in or back to the whole photo, tap to show or hide the controls,
 * and a horizontal fling with the whole photo in view changes photo. It is
 * the first app on aos_gesture.h (docs/GESTURES.md).
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_gesture.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MAX_PHOTOS      64
#define NAME_MAX_LEN    64

/* How far in you may go: MAX_ZOOM screen pixels per photo pixel, or twice
 * the whole-photo view if that is already closer. */
#define MAX_ZOOM        4.0f
#define DOUBLE_TAP_ZOOM 3.0f    /* times the whole-photo view */
#define EASE            0.45f   /* of the remaining distance per frame */
#define FLING_DECAY     0.90f   /* speed kept per 16 ms frame */
#define PAGE_FLING      500.0f  /* px/s sideways that changes photo */
#define FRAME_MS        16

typedef struct {
    char     names[MAX_PHOTOS][NAME_MAX_LEN];
    int      count;
    int      current;
    aos_app_t *self;
    lv_obj_t *list_view;
    lv_obj_t *photo_view;
    void     *canvas_buf;
    lv_obj_t *image;            /* canvas at the photo's own size, scaled */
    lv_obj_t *touch;            /* transparent, over the photo: the gestures */
    lv_obj_t *caption;
    lv_obj_t *prev, *next;

    /* View: s = screen px per photo px, (ox, oy) = where the photo's top-left
     * corner lands. What is on screen eases towards the target, because the
     * a photo redraws slower than the touch moves. */
    int32_t   img_w, img_h;
    int32_t   vw, vh;           /* the view's size and where it sits on the  */
    int32_t   vx0, vy0;         /* screen: under the status bar, not at 0,0  */
    float     fit;              /* s that shows the whole photo */
    float     s, ox, oy;
    float     ts, tox, toy;
    float     vx, vy;           /* fling, px/s */
    bool      touching, chrome, smooth;
    lv_timer_t *anim;
} photos_t;

AOS_BSS_PSRAM static photos_t s_photos;

static bool has_image_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 ||
           strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".bmp") == 0 ||
           strcasecmp(dot, ".bin") == 0;
}

static void scan(void)
{
    s_photos.count = 0;

    DIR *dir = opendir(aos_hal_path_photos());
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_photos.count < MAX_PHOTOS) {
        if (entry->d_name[0] == '.' || !has_image_ext(entry->d_name)) {
            continue;
        }
        /* bounded copy: d_name can be up to 255 bytes and our slot is 63 */
        size_t len = strnlen(entry->d_name, NAME_MAX_LEN - 1);
        memcpy(s_photos.names[s_photos.count], entry->d_name, len);
        s_photos.names[s_photos.count][len] = '\0';
        s_photos.count++;
    }
    closedir(dir);
}

/* --------------------------------------------------------------------------
 * The zoomable view
 * -------------------------------------------------------------------------- */

static float max_scale(void)
{
    float m = s_photos.fit * 2.0f;
    return m > MAX_ZOOM ? m : MAX_ZOOM;
}

/* Keeps the target inside the photo: a photo narrower than the screen is
 * centred, a wider one cannot leave a gap at either side. */
static void clamp_target(void)
{
    if (s_photos.ts < s_photos.fit) s_photos.ts = s_photos.fit;
    if (s_photos.ts > max_scale())  s_photos.ts = max_scale();

    float sw = (float)s_photos.img_w * s_photos.ts;
    float sh = (float)s_photos.img_h * s_photos.ts;
    if (sw <= s_photos.vw) {
        s_photos.tox = (s_photos.vw - sw) * 0.5f;
    } else if (s_photos.tox > 0.0f) {
        s_photos.tox = 0.0f;
    } else if (s_photos.tox < s_photos.vw - sw) {
        s_photos.tox = s_photos.vw - sw;
    }
    if (sh <= s_photos.vh) {
        s_photos.toy = (s_photos.vh - sh) * 0.5f;
    } else if (s_photos.toy > 0.0f) {
        s_photos.toy = 0.0f;
    } else if (s_photos.toy < s_photos.vh - sh) {
        s_photos.toy = s_photos.vh - sh;
    }
}

static void apply_view(void)
{
    uint32_t scale = (uint32_t)(s_photos.s * 256.0f + 0.5f);
    lv_image_set_scale(s_photos.image, scale ? scale : 1);
    lv_obj_set_pos(s_photos.image, (int32_t)lroundf(s_photos.ox),
                   (int32_t)lroundf(s_photos.oy));
}

static void set_chrome(bool on)
{
    s_photos.chrome = on;
    lv_obj_t *parts[] = { s_photos.caption, s_photos.prev, s_photos.next };
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (on) lv_obj_remove_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* Smooth (antialiased) only at rest: interpolating every pixel of a moving
 * photo costs frames exactly while the finger wants them. */
static void set_smooth(bool on)
{
    if (s_photos.smooth != on) {
        s_photos.smooth = on;
        lv_image_set_antialias(s_photos.image, on);
    }
}

static void anim_kick(void)
{
    set_smooth(false);
    lv_timer_resume(s_photos.anim);
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_photos.touching &&
        (fabsf(s_photos.vx) > 20.0f || fabsf(s_photos.vy) > 20.0f)) {
        float dt = FRAME_MS / 1000.0f;
        float nx = s_photos.tox + s_photos.vx * dt;
        float ny = s_photos.toy + s_photos.vy * dt;
        s_photos.tox = nx;
        s_photos.toy = ny;
        clamp_target();
        /* Against an edge the fling stops on that axis. */
        s_photos.vx = s_photos.tox != nx ? 0.0f : s_photos.vx * FLING_DECAY;
        s_photos.vy = s_photos.toy != ny ? 0.0f : s_photos.vy * FLING_DECAY;
    } else if (!s_photos.touching) {
        s_photos.vx = s_photos.vy = 0.0f;
    }

    float ds = s_photos.ts - s_photos.s;
    float dx = s_photos.tox - s_photos.ox;
    float dy = s_photos.toy - s_photos.oy;
    bool settled = fabsf(dx) < 0.5f && fabsf(dy) < 0.5f &&
                   fabsf(ds) < 0.002f * s_photos.ts;
    if (settled) {
        s_photos.s = s_photos.ts;
        s_photos.ox = s_photos.tox;
        s_photos.oy = s_photos.toy;
    } else {
        s_photos.s  += ds * EASE;
        s_photos.ox += dx * EASE;
        s_photos.oy += dy * EASE;
    }
    apply_view();

    if (settled && !s_photos.touching && s_photos.vx == 0 && s_photos.vy == 0) {
        set_smooth(true);
        lv_timer_pause(s_photos.anim);
    }
}

/* Zoom by k keeping the photo point under (cx, cy) where it is. */
static void zoom_at(float k, float cx, float cy)
{
    float ns = s_photos.ts * k;
    float lo = s_photos.fit * 0.75f, hi = max_scale() * 1.25f;  /* a bit of give */
    if (ns < lo) ns = lo;
    if (ns > hi) ns = hi;
    k = ns / s_photos.ts;
    s_photos.tox = cx - (cx - s_photos.tox) * k;
    s_photos.toy = cy - (cy - s_photos.toy) * k;
    s_photos.ts  = ns;
}

static bool at_fit(void)
{
    return s_photos.ts <= s_photos.fit * 1.02f;
}

static void show_photo(int index);

static void gesture_cb(const aos_gesture_event_t *ev, void *user)
{
    (void)user;
    if (!s_photos.img_w) {
        return;
    }
    /* One line per finished gesture, for /api/log: what the finger asked
     * for and where the view ended up. Cheap, and it is how a test on the
     * watch gets read from the Mac. */
    if (ev->type == AOS_GESTURE_TAP || ev->type == AOS_GESTURE_DOUBLE_TAP ||
        ev->type == AOS_GESTURE_DRAG_END || ev->type == AOS_GESTURE_PINCH_END ||
        ev->type == AOS_GESTURE_PINCH_BEGIN || ev->type == AOS_GESTURE_LONG_PRESS) {
        aos_hal_log("photos", "gesture %d at %d,%d v=%d,%d  zoom %d%% of fit",
                    (int)ev->type, (int)ev->x, (int)ev->y, (int)ev->vx, (int)ev->vy,
                    (int)(s_photos.ts / s_photos.fit * 100.0f));
    }
    switch (ev->type) {
    case AOS_GESTURE_TAP:
        set_chrome(!s_photos.chrome);
        break;

    case AOS_GESTURE_DOUBLE_TAP:
        if (at_fit()) {
            zoom_at(DOUBLE_TAP_ZOOM * s_photos.fit / s_photos.ts,
                    ev->x - s_photos.vx0, ev->y - s_photos.vy0);
            set_chrome(false);
        } else {
            s_photos.ts = s_photos.fit;
        }
        clamp_target();
        anim_kick();
        break;

    case AOS_GESTURE_DRAG_BEGIN:
    case AOS_GESTURE_PINCH_BEGIN:
        s_photos.touching = true;
        s_photos.vx = s_photos.vy = 0;
        anim_kick();
        break;

    case AOS_GESTURE_DRAG: {
        /* Past the edge it gives, at a third of the finger, and springs
         * back on release; with the whole photo in view that give is what
         * says "there is another photo there". */
        float sw = (float)s_photos.img_w * s_photos.ts;
        float sh = (float)s_photos.img_h * s_photos.ts;
        float nx = s_photos.tox + ev->dx, ny = s_photos.toy + ev->dy;
        bool out_x = sw <= s_photos.vw || nx > 0 || nx < s_photos.vw - sw;
        bool out_y = sh <= s_photos.vh || ny > 0 || ny < s_photos.vh - sh;
        s_photos.tox += out_x ? ev->dx / 3.0f : ev->dx;
        s_photos.toy += out_y ? ev->dy / 3.0f : ev->dy;
        break;
    }

    case AOS_GESTURE_DRAG_END:
        s_photos.touching = false;
        if (at_fit() && fabsf(ev->vx) > PAGE_FLING && fabsf(ev->vx) > fabsf(ev->vy)) {
            int step = ev->vx < 0 ? 1 : -1;
            int n = (s_photos.current + step + s_photos.count) % s_photos.count;
            show_photo(n);
            return;
        }
        s_photos.vx = ev->vx;
        s_photos.vy = ev->vy;
        clamp_target();
        anim_kick();
        break;

    case AOS_GESTURE_PINCH:
        zoom_at(ev->scale, ev->x - s_photos.vx0, ev->y - s_photos.vy0);
        s_photos.tox += ev->dx;
        s_photos.toy += ev->dy;
        if (!at_fit()) set_chrome(false);
        break;

    case AOS_GESTURE_PINCH_END:
        s_photos.touching = false;
        clamp_target();
        anim_kick();
        break;

    default:
        break;
    }
}

/* Ceiling of what it dares decode. LVGL's cache is 4 MB; half is left so a
 * single photo does not evict everything else. */
#define AOS_PHOTO_MAX_BYTES     (2u * 1024u * 1024u)

static void show_photo(int index)
{
    if (index < 0 || index >= s_photos.count) {
        return;
    }
    s_photos.current = index;

    /* 'A:' is the drive letter we registered for the OS's filesystem */
    char path[160];
    snprintf(path, sizeof(path), "A:%s/%s", aos_hal_path_photos(),
             s_photos.names[index]);
    /* The header is inspected BEFORE decoding. A camera photo decompresses to
     * tens of MB and LVGL, when it does not fit, fails without saying
     * anything: you see an empty screen and there is no way to know why. */
    lv_image_header_t header = { 0 };
    char caption[128];
    if (lv_image_decoder_get_info(path, &header) == LV_RESULT_OK) {
        uint32_t bytes = (uint32_t)header.w * header.h * 2u;
        if (bytes > AOS_PHOTO_MAX_BYTES) {
            snprintf(caption, sizeof(caption),
                     _("%s\n%dx%d no entra en memoria\n"
                       "(necesita %u KB)\nAchicala a %dx%d o menos"),
                     s_photos.names[index], (int)header.w, (int)header.h,
                     (unsigned)(bytes / 1024), AOS_SCREEN_W, AOS_SCREEN_H);
            free(s_photos.canvas_buf);
            s_photos.canvas_buf = NULL;
            s_photos.img_w = s_photos.img_h = 0;
            lv_obj_add_flag(s_photos.image, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_photos.caption, caption);
            set_chrome(true);
            lv_obj_add_flag(s_photos.list_view, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }

    /* It is decoded ONCE into a canvas and then that canvas is shown.
     *
     * Reason: LVGL's JPEG decoder works in streaming mode (it marks the image
     * as LV_COLOR_FORMAT_RAW and decodes it in blocks during the drawing,
     * rewinding the file on every refresh). Which means an lv_image pointing
     * at the .jpg re-decodes the WHOLE photo from the microSD on every frame:
     * measured, 2950 ms per frame. Enlarging the cache does not help because a
     * bitmap to cache never comes into existence.
     *
     * The canvas is the photo's OWN size (the header check above keeps it
     * under 2 MB), so zooming in shows real detail; the scale on the canvas
     * does the fitting and the zooming. */
    free(s_photos.canvas_buf);
    s_photos.canvas_buf = NULL;
    s_photos.img_w = s_photos.img_h = 0;

    /* Gestures come in screen pixels and the photo is placed in the view's:
     * the view starts under the status bar. */
    lv_obj_update_layout(s_photos.photo_view);
    lv_area_t va;
    lv_obj_get_coords(s_photos.photo_view, &va);
    s_photos.vx0 = va.x1;
    s_photos.vy0 = va.y1;
    s_photos.vw  = lv_area_get_width(&va);
    s_photos.vh  = lv_area_get_height(&va);

    int32_t w = header.w > 0 ? (int32_t)header.w : AOS_SCREEN_W;
    int32_t h = header.h > 0 ? (int32_t)header.h : AOS_SCREEN_H;
    s_photos.canvas_buf = malloc((size_t)w * (size_t)h * 2u);
    if (!s_photos.canvas_buf) {
        snprintf(caption, sizeof(caption), _("%s\n%dx%d no entra en memoria\n"
                 "(necesita %u KB)\nAchicala a %dx%d o menos"),
                 s_photos.names[index], (int)w, (int)h,
                 (unsigned)((uint32_t)w * h * 2u / 1024), AOS_SCREEN_W, AOS_SCREEN_H);
        lv_label_set_text(s_photos.caption, caption);
        set_chrome(true);
        lv_obj_add_flag(s_photos.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_canvas_set_buffer(s_photos.image, s_photos.canvas_buf, w, h,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_photos.image, lv_color_hex(0x000000), LV_OPA_COVER);

    lv_layer_t capa;
    lv_canvas_init_layer(s_photos.image, &capa);
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = path;
    lv_area_t donde = { 0, 0, w - 1, h - 1 };
    lv_draw_image(&capa, &dsc, &donde);
    lv_canvas_finish_layer(s_photos.image, &capa);
    lv_obj_remove_flag(s_photos.image, LV_OBJ_FLAG_HIDDEN);

    s_photos.img_w = w;
    s_photos.img_h = h;
    float fx = (float)s_photos.vw / (float)w;
    float fy = (float)s_photos.vh / (float)h;
    s_photos.fit = fx < fy ? fx : fy;
    if (s_photos.fit > 1.0f) {
        s_photos.fit = 1.0f;        /* a small photo is shown 1:1 to start */
    }
    s_photos.ts = s_photos.fit;
    s_photos.vx = s_photos.vy = 0;
    s_photos.touching = false;
    clamp_target();
    s_photos.s = s_photos.ts;
    s_photos.ox = s_photos.tox;
    s_photos.oy = s_photos.toy;
    set_smooth(true);
    apply_view();
    set_chrome(true);

    snprintf(caption, sizeof(caption), "%d / %d   %s",
             index + 1, s_photos.count, s_photos.names[index]);
    lv_label_set_text(s_photos.caption, caption);

    lv_obj_add_flag(s_photos.list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);
    /* On the photo every drag is ours: no global back swipe, and no cut of
     * a long drag at 50 px. The button still goes back to the list. */
    s_photos.self->desc.flags |= AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
}

static void open_cb(lv_event_t *event)
{
    show_photo((int)(intptr_t)lv_event_get_user_data(event));
}

static void nav_cb(lv_event_t *event)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(event);
    int next = s_photos.current + delta;
    if (next < 0) {
        next = s_photos.count - 1;
    } else if (next >= s_photos.count) {
        next = 0;
    }
    show_photo(next);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    s_photos.self = self;
    lv_obj_t *page = aos_page(root);
    scan();

    /* --- list --- */
    s_photos.list_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_photos.list_view);
    lv_obj_set_size(s_photos.list_view, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_photos.list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_photos.list_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_photos.list_view, 8, 0);
    lv_obj_set_style_pad_ver(s_photos.list_view, 16, 0);
    lv_obj_set_scroll_dir(s_photos.list_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_photos.list_view, LV_SCROLLBAR_MODE_OFF);

    if (s_photos.count == 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), _("No hay fotos en\n%s"), aos_hal_path_photos());
        lv_obj_t *empty = aos_label(s_photos.list_view, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }

    for (int i = 0; i < s_photos.count; i++) {
        lv_obj_t *row = lv_obj_create(s_photos.list_view);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 56, 56);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = aos_label(row, s_photos.names[i], aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, AOS_SCREEN_W - 100);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);
    }

    /* --- viewer --- */
    s_photos.photo_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_photos.photo_view);
    lv_obj_set_size(s_photos.photo_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_photos.photo_view, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_photos.photo_view, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);

    /* The photo is laid out by hand (position and scale), and a 2000 px
     * canvas inside a scrollable parent would make the parent scroll. */
    lv_obj_remove_flag(s_photos.photo_view, LV_OBJ_FLAG_SCROLLABLE);

    /* The canvas that receives the single decode; its buffer is allocated
     * per photo, at the photo's size (show_photo). Scaled around its
     * top-left corner, so (ox, oy) is simply its position. */
    s_photos.image = lv_canvas_create(s_photos.photo_view);
    lv_image_set_pivot(s_photos.image, 0, 0);
    lv_obj_add_flag(s_photos.image, LV_OBJ_FLAG_HIDDEN);
    s_photos.smooth = true;

    /* Over the photo and under the controls: where the fingers land. */
    s_photos.touch = lv_obj_create(s_photos.photo_view);
    lv_obj_remove_style_all(s_photos.touch);
    lv_obj_set_size(s_photos.touch, lv_pct(100), lv_pct(100));
    aos_gesture_attach(s_photos.touch, 0, gesture_cb, NULL);

    s_photos.caption = aos_label(s_photos.photo_view, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_photos.caption, LV_ALIGN_BOTTOM_MID, 0, -56);

    s_photos.prev = aos_button(s_photos.photo_view, LV_SYMBOL_LEFT, AOS_C_CARD2,
                               nav_cb, (void *)(intptr_t)-1);
    lv_obj_align(s_photos.prev, LV_ALIGN_BOTTOM_LEFT, 24, -8);
    s_photos.next = aos_button(s_photos.photo_view, LV_SYMBOL_RIGHT, AOS_C_CARD2,
                               nav_cb, (void *)(intptr_t)1);
    lv_obj_align(s_photos.next, LV_ALIGN_BOTTOM_RIGHT, -24, -8);

    s_photos.anim = lv_timer_create(anim_cb, FRAME_MS, NULL);
    lv_timer_pause(s_photos.anim);

    return &s_photos;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_photos.anim) {
        lv_timer_delete(s_photos.anim);
        s_photos.anim = NULL;
    }
    free(s_photos.canvas_buf);
    s_photos.canvas_buf = NULL;
    s_photos.img_w = s_photos.img_h = 0;
    s_photos.touch = NULL;
    s_photos.prev = s_photos.next = NULL;
    s_photos.list_view = NULL;
    s_photos.photo_view = NULL;
    s_photos.image = NULL;
    s_photos.caption = NULL;
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_photos.photo_view && !lv_obj_has_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_photos.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(s_photos.anim);
        /* The list scrolls and goes back with a swipe, as always. */
        s_photos.self->desc.flags &= ~(uint32_t)(AOS_APP_FLAG_NO_SWIPE |
                                                 AOS_APP_FLAG_LONG_DRAG);
        return true;
    }
    return false;
}

void aos_app_photos_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id      = "aos.photos",
            .name    = "Fotos",
            .icon    = LV_SYMBOL_IMAGE,
            .color_a = 0xBF5AF2,
            .color_b = 0x7A2FA0,
            .order   = 50,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
