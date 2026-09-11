/*
 * PIXEL ART - a drawing app for 8x8 and 16x16 grids, with frames.
 *
 * Eight documents ("lienzos"), each a stack of up to 16 frames of cells
 * painted from a 32-colour palette. Frames are duplicated and retouched to
 * make an animation, played back on the watch and exported as a looping GIF;
 * a single frame goes out as a PNG. The files land on the microSD under
 * /pixel, where the web portal (/pixel) reads them, edits them with a mouse
 * and writes them back: the app notices and reloads.
 *
 * How it draws, and why it is cheap:
 *
 *   - ONE canvas of 288x288 RGB565 (166 KB, PSRAM via malloc) shown 1:1. A
 *     cell is 18 px (16x16) or 36 px (8x8). Painting a cell writes that
 *     square into the buffer and invalidates ONLY that square: LVGL blits a
 *     few hundred pixels, not the screen. Switching frames rewrites the
 *     whole buffer and invalidates the canvas once (~12 ms on the board).
 *   - The document never touches LVGL (px_file.c) and the encoders never
 *     touch the document's owner (px_export.c): both are verified on the Mac
 *     by tools/px_harness.c, byte by byte, before the board sees them.
 *   - Both screens -gallery and editor- are built once and shown/hidden.
 *     Nothing is destroyed from inside an event callback, which is the rule
 *     this system enforces the hard way.
 *
 * Layout, dictated by the panel (see HARDWARE.md): nothing touchable below
 * y=390, the glass corners eat the first rows, so the bar is at y=8..48, the
 * canvas at 52..340, the palette strip at 344..386, and the dead strip at the
 * bottom carries the one line that is only read.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include "px_file.h"
#include "px_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TAG             "pixel"

#define CV_PX           288                 /* canvas side, px              */
#define CV_X            40
#define CV_Y            52
#define BAR_Y           8
#define BAR_H           40
#define STRIP_Y         344
#define STRIP_H         42
#define SWATCH          40
#define INFO_Y          400                 /* the dead strip: text only    */

#define TH_PX           80                  /* gallery thumbnail side       */
#define TH_BOX          84
#define TH_GAP          5
#define TH_X0           8

#define GRID_COLOR      0x3A3A3E            /* between cells; on black it reads, just */

#define TIMER_MS        50
#define SAVE_RETRY_MS   10000
#define SLOT_ERR        0xFF
#define PAL_PITCH       45                  /* swatch + gap, in the strip   */
#define PAL_W           (PX_COLORS * PAL_PITCH)
#define AUTOSAVE_MS     3000
#define WATCH_MS        3000

enum { TOOL_PEN = 0, TOOL_FILL, TOOL_PICK, TOOL_COUNT };

typedef struct {
    aos_app_t *self;
    lv_obj_t  *root;
    lv_timer_t *timer;
    bool       closing;
    bool       exit_req;
    bool       exiting;

    /* the document being edited, and a scratch one for thumbnails */
    px_doc_t  *doc;
    px_doc_t  *scratch;
    int        slot;            /* -1 in the gallery                         */
    int        frame;
    int        color;
    int        tool;
    bool       dirty;
    uint32_t   changed_ms;
    uint8_t    undo[PX_CELLS];
    bool       has_undo;
    bool       stroke;          /* a finger is down and painting             */
    int        last_cell;       /* to skip repaints while dragging inside one */

    /* what the file looked like when loaded, to notice the portal */
    long       file_size;
    long       file_mtime;
    uint32_t   watch_ms;
    uint32_t   gal_sig;

    /* gallery */
    lv_obj_t  *gal;
    lv_obj_t  *slot_cv[PX_SLOTS];
    lv_obj_t  *slot_lbl[PX_SLOTS];
    uint16_t  *thumb[PX_SLOTS];
    uint8_t    slot_size[PX_SLOTS];         /* 0 = empty, SLOT_ERR = unreadable */
    uint8_t    slot_frames[PX_SLOTS];
    lv_obj_t  *sizer;                       /* the "new document" overlay   */
    int        sizer_slot;
    lv_obj_t  *gal_info;

    /* editor */
    lv_obj_t  *ed;
    lv_obj_t  *canvas;
    uint16_t  *big;
    lv_obj_t  *touch;
    lv_obj_t  *lbl_frame;
    lv_obj_t  *btn_tool;
    lv_obj_t  *lbl_tool;
    lv_obj_t  *strip;                       /* scrolls sideways             */
    lv_obj_t  *pal;                         /* ONE canvas with the 32 swatches */
    uint16_t  *palbuf;
    lv_obj_t  *info;
    lv_obj_t  *menu;                        /* built when opened, deleted when closed */
    lv_obj_t  *mi_undo, *mi_del_frame, *mi_speed, *mi_del_doc, *mi_play;
    bool       menu_del_req;                /* the timer deletes it: never from its own callback */
    bool       confirm_del;
    uint32_t   save_retry_ms;               /* after a failed save, do not hammer the card */
    bool       save_failed;
    bool       playing;
    uint32_t   play_next_ms;
} app_t;

static void go_gallery(app_t *a);
static void go_editor(app_t *a, int slot);
static void editor_refresh(app_t *a);
static void menu_close(app_t *a);

/* --------------------------------------------------------------------------
 * Paths
 *
 * With a card: /sdcard/pixel, which is what the portal serves as dir=pixel.
 * Without one: the flat data directory in SPIFFS, no sub-folder (SPIFFS
 * names are flat and a directory there is only a prefix).
 * -------------------------------------------------------------------------- */

static const char *px_dir(void)
{
    static char dir[96];
    static bool made;
    const char *sd = aos_hal_path_sd_root();
    if (sd) {
        snprintf(dir, sizeof(dir), "%s/pixel", sd);
        if (!made) {
            mkdir(dir, 0777);
            made = true;
        }
    } else {
        snprintf(dir, sizeof(dir), "%s", aos_hal_path_data());
    }
    return dir;
}

static void slot_path(int slot, char *out, size_t n)
{
    snprintf(out, n, "%s/lienzo%d.pix", px_dir(), slot + 1);
}

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

/* --------------------------------------------------------------------------
 * Drawing into the buffers
 * -------------------------------------------------------------------------- */

static void fill_rect(uint16_t *buf, int stride, int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        uint16_t *p = buf + (size_t)(y + j) * stride + x;
        for (int i = 0; i < w; i++) {
            p[i] = c;
        }
    }
}

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

/* One cell of the big canvas: the colour inset by the grid line. */
static void draw_cell(app_t *a, int x, int y, bool invalidate)
{
    int n = a->doc->size;
    int cell = CV_PX / n;
    uint16_t c = px_rgb565(a->doc->px[a->frame][y * n + x]);
    fill_rect(a->big, CV_PX, x * cell + 1, y * cell + 1, cell - 1, cell - 1, c);
    if (invalidate) {
        lv_area_t co;
        lv_obj_get_coords(a->canvas, &co);
        lv_area_t area = { co.x1 + x * cell, co.y1 + y * cell,
                           co.x1 + x * cell + cell - 1, co.y1 + y * cell + cell - 1 };
        lv_obj_invalidate_area(a->canvas, &area);
    }
}

static void draw_frame(app_t *a)
{
    int n = a->doc->size;
    uint16_t grid = rgb565(GRID_COLOR);
    /* the grid is the background: cells are painted on top, inset by 1 px */
    for (size_t i = 0; i < (size_t)CV_PX * CV_PX; i++) {
        a->big[i] = grid;
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            draw_cell(a, x, y, false);
        }
    }
    lv_obj_invalidate(a->canvas);
}

/* A 3x5 digit font for the frame badge, one bit per pixel, rows top down. */
static const uint8_t digits3x5[10][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
    { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
    { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
};

static void draw_digit(uint16_t *buf, int stride, int x, int y, int d, int scale, uint16_t c)
{
    for (int r = 0; r < 5; r++) {
        for (int k = 0; k < 3; k++) {
            if (digits3x5[d][r] & (4 >> k)) {
                fill_rect(buf, stride, x + k * scale, y + r * scale, scale, scale, c);
            }
        }
    }
}

/* The whole thumbnail is painted by code -border, the plus of an empty
 * slot, the frame badge- so that a slot is ONE canvas and ONE label. The
 * first version had a box, a plus label and a badge label per slot as well:
 * forty objects of internal RAM for eight pictures, and on the board that
 * RAM is what the card driver needs for its DMA buffers. */
static void draw_thumb(uint16_t *buf, const px_doc_t *d, int frames)
{
    int n = d->size;
    int cell = TH_PX / n;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            fill_rect(buf, TH_PX, x * cell, y * cell, cell, cell,
                      px_rgb565(d->px[0][y * n + x]));
        }
    }
    if (frames > 1) {
        /* a dark pill top right: a play triangle and the count, digits 6x10 */
        int nd = frames > 9 ? 2 : 1;
        int w = 6 + 9 + nd * 8 + 3;
        uint16_t dark = rgb565(0x141416), white = rgb565(0xFFFFFF);
        fill_rect(buf, TH_PX, TH_PX - w - 3, 3, w, 16, dark);
        int x = TH_PX - w;
        for (int r = 0; r < 7; r++) {           /* the triangle, 4 px wide */
            int len = r < 4 ? r + 1 : 7 - r;
            fill_rect(buf, TH_PX, x, 6 + r, len, 1, white);
        }
        x += 8;
        if (nd == 2) {
            draw_digit(buf, TH_PX, x, 6, frames / 10, 2, white);
            x += 8;
        }
        draw_digit(buf, TH_PX, x, 6, frames % 10, 2, white);
    }
}

static void draw_thumb_empty(uint16_t *buf, bool error)
{
    uint16_t bg = rgb565(0x000000), line = rgb565(error ? 0x8A1E22 : 0x2C2C2E);
    fill_rect(buf, TH_PX, 0, 0, TH_PX, TH_PX, bg);
    fill_rect(buf, TH_PX, 0, 0, TH_PX, 2, line);
    fill_rect(buf, TH_PX, 0, TH_PX - 2, TH_PX, 2, line);
    fill_rect(buf, TH_PX, 0, 0, 2, TH_PX, line);
    fill_rect(buf, TH_PX, TH_PX - 2, 0, 2, TH_PX, line);
    uint16_t plus = rgb565(error ? 0xFF453A : 0x8E8E93);
    fill_rect(buf, TH_PX, TH_PX / 2 - 2, TH_PX / 2 - 12, 4, 24, plus);   /* + */
    fill_rect(buf, TH_PX, TH_PX / 2 - 12, TH_PX / 2 - 2, 24, 4, plus);
}

/* --------------------------------------------------------------------------
 * Saving and loading
 * -------------------------------------------------------------------------- */

static void note_file(app_t *a, const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        a->file_size  = (long)st.st_size;
        a->file_mtime = (long)st.st_mtime;
    } else {
        a->file_size = a->file_mtime = -1;
    }
}

static bool save_doc(app_t *a)
{
    if (a->slot < 0) {
        return true;
    }
    char path[160];
    slot_path(a->slot, path, sizeof(path));
    bool ok = px_doc_save(a->doc, path);
    if (ok) {
        a->dirty = false;
        a->save_failed = false;
        note_file(a, path);
    } else {
        /* Said once and retried in ten seconds, not every tick: the first
         * version hammered a card that had run out of DMA memory fifty times
         * a second, with a toast each time. */
        a->save_retry_ms = now_ms() + SAVE_RETRY_MS;
        if (!a->save_failed) {
            a->save_failed = true;
            aos_hal_log(TAG, "could not save %s", path);
            aos_ui_toast(_("No se pudo guardar"), 1500);
        }
    }
    return ok;
}

static void mark_dirty(app_t *a)
{
    a->dirty = true;
    a->changed_ms = now_ms();
}

/* A signature of the eight files, to notice the portal from the gallery. */
static uint32_t gallery_signature(void)
{
    uint32_t sig = 0;
    for (int i = 0; i < PX_SLOTS; i++) {
        char path[160];
        slot_path(i, path, sizeof(path));
        struct stat st;
        if (stat(path, &st) == 0) {
            sig = sig * 31u + (uint32_t)st.st_size * 7u + (uint32_t)st.st_mtime + (uint32_t)i;
        } else {
            sig = sig * 31u + 1u;
        }
    }
    return sig;
}

/* --------------------------------------------------------------------------
 * Gallery
 * -------------------------------------------------------------------------- */

/* Reads the eight files and paints the thumbnails. No LVGL object is
 * touched here, and it is called BEFORE the objects exist on create(): the
 * card driver allocates DMA buffers from internal RAM, and this is the moment
 * the app holds the least of it. */
static void gallery_scan(app_t *a)
{
    for (int i = 0; i < PX_SLOTS; i++) {
        char path[160];
        slot_path(i, path, sizeof(path));
        struct stat st;
        bool exists = stat(path, &st) == 0;
        if (px_doc_load(a->scratch, path)) {
            a->slot_size[i]   = a->scratch->size;
            a->slot_frames[i] = a->scratch->frames;
            draw_thumb(a->thumb[i], a->scratch, a->scratch->frames);
        } else {
            /* A file that is there and cannot be read is the card failing,
             * not an empty slot: say so instead of offering to overwrite. */
            a->slot_size[i] = exists ? SLOT_ERR : 0;
            a->slot_frames[i] = 0;
            draw_thumb_empty(a->thumb[i], exists);
            if (exists) {
                aos_hal_log(TAG, "cannot read %s", path);
            }
        }
    }
    a->gal_sig = gallery_signature();
}

static void gallery_labels(app_t *a)
{
    int used = 0;
    for (int i = 0; i < PX_SLOTS; i++) {
        if (a->slot_size[i] == SLOT_ERR) {
            lv_label_set_text_fmt(a->slot_lbl[i], "%d · %s", i + 1, _("no se lee"));
        } else if (a->slot_size[i]) {
            lv_label_set_text_fmt(a->slot_lbl[i], "%d · %d×%d", i + 1,
                                  a->slot_size[i], a->slot_size[i]);
            used++;
        } else {
            lv_label_set_text_fmt(a->slot_lbl[i], "%d · %s", i + 1, _("vacío"));
        }
        lv_obj_invalidate(a->slot_cv[i]);
    }
    if (aos_hal_path_sd_root()) {
        lv_label_set_text_fmt(a->gal_info, "%d/%d · SD /pixel", used, PX_SLOTS);
    } else {
        lv_label_set_text_fmt(a->gal_info, "%d/%d · %s", used, PX_SLOTS,
                              _("sin tarjeta: memoria interna"));
    }
}

static void gallery_refresh(app_t *a)
{
    gallery_scan(a);
    gallery_labels(a);
}

static void sizer_show(app_t *a, int slot)
{
    a->sizer_slot = slot;
    lv_obj_remove_flag(a->sizer, LV_OBJ_FLAG_HIDDEN);
}

static void sizer_pick_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int size = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (a->closing) {
        return;
    }
    lv_obj_add_flag(a->sizer, LV_OBJ_FLAG_HIDDEN);
    if (size == 0) {
        return;                                 /* cancel */
    }
    px_doc_init(a->doc, size);
    a->slot = a->sizer_slot;
    mark_dirty(a);
    save_doc(a);                                /* the file exists from now on */
    go_editor(a, a->slot);
}

static void slot_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int slot = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (a->closing) {
        return;
    }
    if (a->slot_size[slot] == SLOT_ERR) {
        aos_ui_toast(_("No se pudo leer la tarjeta"), 1500);
    } else if (a->slot_size[slot] == 0) {
        sizer_show(a, slot);
    } else {
        go_editor(a, slot);
    }
}

static void gal_back_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->exit_req = true;                         /* applied by the timer */
}

static lv_obj_t *bar_button(lv_obj_t *parent, const char *text, int32_t x, int32_t w,
                            lv_color_t color, lv_event_cb_t cb, void *ud, lv_obj_t **lbl)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, BAR_H);
    lv_obj_set_pos(btn, x, BAR_Y);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    /* feedback by opacity, never by scale: a transform is a layer */
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    }
    lv_obj_t *l = aos_label(btn, text, aos_font_body, AOS_C_TEXT);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l);
    if (lbl) {
        *lbl = l;
    }
    return btn;
}

static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_color(c, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x3A3A3E), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);   /* swallows touches underneath */
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *menu_item(app_t *a, lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                           int32_t w, int32_t h, lv_color_t color)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, a);
    lv_obj_t *l = aos_label(btn, text, aos_font_body, AOS_C_TEXT);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_width(l, w - 32);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return btn;
}

static void build_gallery(app_t *a, lv_obj_t *root)
{
    a->gal = lv_obj_create(root);
    lv_obj_remove_style_all(a->gal);
    lv_obj_set_size(a->gal, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->gal, 0, 0);
    lv_obj_remove_flag(a->gal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->gal, LV_OBJ_FLAG_CLICKABLE);

    bar_button(a->gal, LV_SYMBOL_LEFT, 14, 44, AOS_C_CARD2, gal_back_cb, a, NULL);
    lv_obj_t *title = aos_label(a->gal, "Pixel Art", aos_font_title, AOS_C_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    for (int i = 0; i < PX_SLOTS; i++) {
        int col = i % 4, row = i / 4;
        int32_t x = TH_X0 + col * (TH_BOX + TH_GAP);
        int32_t y = 68 + row * (TH_BOX + 40);

        /* The canvas is the slot: clickable itself, border and badge painted
         * inside its buffer. */
        lv_obj_t *cv = lv_canvas_create(a->gal);
        lv_canvas_set_buffer(cv, a->thumb[i], TH_PX, TH_PX, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(cv, TH_PX, TH_PX);
        lv_obj_set_pos(cv, x + (TH_BOX - TH_PX) / 2, y + (TH_BOX - TH_PX) / 2);
        lv_image_set_antialias(cv, false);
        lv_obj_add_flag(cv, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(cv, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(cv, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cv, slot_cb, LV_EVENT_CLICKED, a);
        a->slot_cv[i] = cv;

        lv_obj_t *lbl = aos_label_boxed(a->gal, "", aos_font_small, AOS_C_DIM, TH_BOX + 4, 20);
        lv_obj_set_pos(lbl, x - 2, y + TH_BOX + 4);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        a->slot_lbl[i] = lbl;
    }

    /* A fixed box that wraps: the German hint is wider than the screen. */
    lv_obj_t *hint = aos_label(a->gal, _("Tocá un lienzo para editarlo"), aos_font_small, AOS_C_DIM);
    lv_obj_set_width(hint, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 326);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);

    a->gal_info = aos_label_boxed(a->gal, "", aos_font_small, AOS_C_DIM, AOS_SCREEN_W, 20);
    lv_obj_set_pos(a->gal_info, 0, INFO_Y);
    lv_obj_remove_flag(a->gal_info, LV_OBJ_FLAG_CLICKABLE);

    /* the "new document" overlay: which size */
    a->sizer = card(root, 24, 96, 320, 236);
    lv_obj_add_flag(a->sizer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *t = aos_label(a->sizer, _("Nuevo lienzo"), aos_font_body, AOS_C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *b8 = menu_item(a, a->sizer, "8 × 8", sizer_pick_cb, 130, 56, AOS_C_ACCENT);
    lv_obj_set_user_data(b8, (void *)(intptr_t)8);
    lv_obj_align(b8, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_obj_align(lv_obj_get_child(b8, 0), LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *b16 = menu_item(a, a->sizer, "16 × 16", sizer_pick_cb, 130, 56, AOS_C_PURPLE);
    lv_obj_set_user_data(b16, (void *)(intptr_t)16);
    lv_obj_align(b16, LV_ALIGN_TOP_RIGHT, -20, 60);
    lv_obj_align(lv_obj_get_child(b16, 0), LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *bc = menu_item(a, a->sizer, _("Cancelar"), sizer_pick_cb, 280, 48, AOS_C_CARD2);
    lv_obj_set_user_data(bc, (void *)(intptr_t)0);
    lv_obj_align(bc, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_align(lv_obj_get_child(bc, 0), LV_ALIGN_CENTER, 0, 0);
}

/* --------------------------------------------------------------------------
 * Editor: painting
 * -------------------------------------------------------------------------- */

static void undo_snapshot(app_t *a)
{
    memcpy(a->undo, a->doc->px[a->frame], PX_CELLS);
    a->has_undo = true;
}

/* The strip is ONE canvas, 32 swatches painted by code, inside a container
 * that scrolls sideways. The selected one gets a white frame. Thirty-two
 * objects of internal RAM became one, and the buffer is PSRAM. */
static void draw_palette(app_t *a)
{
    uint16_t bg = rgb565(0x000000);
    fill_rect(a->palbuf, PAL_W, 0, 0, PAL_W, SWATCH, bg);
    for (int i = 0; i < PX_COLORS; i++) {
        int x = i * PAL_PITCH + 2;
        bool on = i == a->color;
        uint16_t frame = rgb565(on ? 0xFFFFFF : 0x5A5A64);
        int b = on ? 3 : 1;
        fill_rect(a->palbuf, PAL_W, x, 0, SWATCH, SWATCH, frame);
        fill_rect(a->palbuf, PAL_W, x + b, b, SWATCH - 2 * b, SWATCH - 2 * b, px_rgb565(i));
    }
    lv_obj_invalidate(a->pal);
}

static void set_color(app_t *a, int idx)
{
    a->color = idx;
    draw_palette(a);
    /* bring it into view, centred if it can be */
    int32_t want = idx * PAL_PITCH + SWATCH / 2 - (AOS_SCREEN_W - 28) / 2;
    if (want < 0) want = 0;
    if (want > PAL_W - (AOS_SCREEN_W - 28)) want = PAL_W - (AOS_SCREEN_W - 28);
    lv_obj_scroll_to_x(a->strip, want, LV_ANIM_ON);
    editor_refresh(a);
}

static void swatch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t co;
    lv_obj_get_coords(a->pal, &co);       /* already shifted by the scroll */
    int idx = (p.x - co.x1) / PAL_PITCH;
    if (idx >= 0 && idx < PX_COLORS) {
        set_color(a, idx);
    }
}

static void play_stop(app_t *a)
{
    if (a->playing) {
        a->playing = false;
        editor_refresh(a);
    }
}

static void touch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->stroke = false;
        a->last_cell = -1;
        return;
    }
    if (a->playing) {
        if (code == LV_EVENT_PRESSED) {
            play_stop(a);               /* a tap anywhere stops the preview */
        }
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    int n = a->doc->size;
    int cell = CV_PX / n;
    int x = (p.x - co.x1) / cell, y = (p.y - co.y1) / cell;
    if (p.x < co.x1 || p.y < co.y1 || x < 0 || y < 0 || x >= n || y >= n) {
        return;
    }
    uint8_t *px = a->doc->px[a->frame];
    int idx = y * n + x;

    if (code == LV_EVENT_PRESSED) {
        a->last_cell = -1;
        switch (a->tool) {
        case TOOL_PICK:
            set_color(a, px[idx]);
            a->tool = TOOL_PEN;         /* one pick, then back to painting */
            editor_refresh(a);
            return;
        case TOOL_FILL:
            if (px[idx] != a->color) {
                undo_snapshot(a);
                px_doc_fill(a->doc, a->frame, x, y, (uint8_t)a->color);
                draw_frame(a);
                mark_dirty(a);
            }
            return;
        default:
            undo_snapshot(a);
            a->stroke = true;
            break;
        }
    }
    if (!a->stroke || idx == a->last_cell) {
        return;
    }
    a->last_cell = idx;
    if (px[idx] != a->color) {
        px[idx] = (uint8_t)a->color;
        draw_cell(a, x, y, true);
        mark_dirty(a);
    }
}

/* --------------------------------------------------------------------------
 * Editor: frames and the bar
 * -------------------------------------------------------------------------- */

static void show_frame(app_t *a, int frame)
{
    if (frame < 0) frame = a->doc->frames - 1;
    if (frame >= a->doc->frames) frame = 0;
    a->frame = frame;
    a->has_undo = false;
    draw_frame(a);
    editor_refresh(a);
}

static void editor_refresh(app_t *a)
{
    if (a->playing) {
        lv_label_set_text_fmt(a->lbl_frame, LV_SYMBOL_PLAY " %d/%d", a->frame + 1, a->doc->frames);
    } else {
        lv_label_set_text_fmt(a->lbl_frame, "%d/%d", a->frame + 1, a->doc->frames);
    }
    static const char *const glyph[TOOL_COUNT] = { LV_SYMBOL_EDIT, LV_SYMBOL_TINT, LV_SYMBOL_EYE_OPEN };
    lv_label_set_text(a->lbl_tool, glyph[a->tool]);
    /* the tool button wears the current colour; the glyph flips to black on
     * light colours so it never vanishes */
    const uint8_t *c = px_palette[a->color];
    int luma = (c[0] * 3 + c[1] * 6 + c[2]) / 10;
    lv_obj_set_style_bg_color(a->btn_tool, lv_color_make(c[0], c[1], c[2]), 0);
    lv_obj_set_style_text_color(a->lbl_tool, luma > 140 ? lv_color_hex(0x000000) : AOS_C_TEXT, 0);
    lv_obj_set_style_border_width(a->btn_tool, a->color == 0 ? 2 : 0, 0);

    lv_label_set_text_fmt(a->info, "%s %d · %d×%d · %d ms",
                          _("Lienzo"), a->slot + 1, a->doc->size, a->doc->size, a->doc->delay_ms);
}

static void ed_back_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    play_stop(a);
    menu_close(a);
    go_gallery(a);
}

static void prev_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->closing) {
        play_stop(a);
        show_frame(a, a->frame - 1);
    }
}

static void next_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->closing) {
        play_stop(a);
        show_frame(a, a->frame + 1);
    }
}

static void play_toggle(app_t *a)
{
    if (a->playing) {
        play_stop(a);
        return;
    }
    if (a->doc->frames < 2) {
        aos_ui_toast(_("Hace falta más de un cuadro"), 1200);
        return;
    }
    menu_close(a);
    a->playing = true;
    a->play_next_ms = now_ms() + a->doc->delay_ms;
    editor_refresh(a);
}

static void frame_lbl_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->closing) {
        play_toggle(a);
    }
}

static void tool_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    play_stop(a);
    a->tool = (a->tool + 1) % TOOL_COUNT;
    static const char *const names[TOOL_COUNT] = { N_("Lápiz"), N_("Rellenar"), N_("Tomar color") };
    aos_ui_toast(_(names[a->tool]), 900);
    editor_refresh(a);
}

/* --------------------------------------------------------------------------
 * Editor: the menu
 * -------------------------------------------------------------------------- */

static void menu_refresh(app_t *a)
{
    if (!a->menu) {
        return;
    }
    lv_obj_t *l;
    l = lv_obj_get_child(a->mi_speed, 0);
    lv_label_set_text_fmt(l, "%s: %d ms", _("Velocidad"), a->doc->delay_ms);
    l = lv_obj_get_child(a->mi_del_doc, 0);
    lv_label_set_text(l, a->confirm_del ? _("¿Seguro? Tocá de nuevo") : _("Borrar lienzo"));
    lv_obj_set_style_bg_opa(a->mi_undo, a->has_undo ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(a->mi_del_frame, a->doc->frames > 1 ? LV_OPA_COVER : LV_OPA_30, 0);
}

static bool menu_open(const app_t *a)
{
    return a->menu != NULL && !a->menu_del_req;
}

/* Hides it now and lets the timer delete it: the close nearly always comes
 * from a click on one of its own items, and an object must not be deleted
 * while it is dispatching an event. */
static void menu_close(app_t *a)
{
    a->confirm_del = false;
    if (a->menu) {
        lv_obj_add_flag(a->menu, LV_OBJ_FLAG_HIDDEN);
        a->menu_del_req = true;
    }
}

static void build_menu(app_t *a);

static void menu_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    play_stop(a);
    if (menu_open(a)) {
        menu_close(a);
        return;
    }
    if (!a->menu) {
        build_menu(a);
    }
    a->confirm_del = false;
    menu_refresh(a);
    lv_obj_remove_flag(a->menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(a->menu, 0, LV_ANIM_OFF);
}

static void mi_undo_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing || !a->has_undo) {
        return;
    }
    memcpy(a->doc->px[a->frame], a->undo, PX_CELLS);
    a->has_undo = false;
    draw_frame(a);
    mark_dirty(a);
    menu_close(a);
}

static void mi_dup_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    int pos = px_doc_frame_dup(a->doc, a->frame);
    if (pos < 0) {
        aos_ui_toast(_("Máximo 16 cuadros"), 1200);
        return;
    }
    mark_dirty(a);
    menu_close(a);
    show_frame(a, pos);
}

static void mi_blank_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    int pos = px_doc_frame_blank(a->doc, a->frame);
    if (pos < 0) {
        aos_ui_toast(_("Máximo 16 cuadros"), 1200);
        return;
    }
    mark_dirty(a);
    menu_close(a);
    show_frame(a, pos);
}

static void mi_del_frame_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    if (!px_doc_frame_delete(a->doc, a->frame)) {
        return;
    }
    mark_dirty(a);
    menu_close(a);
    show_frame(a, a->frame >= a->doc->frames ? a->doc->frames - 1 : a->frame);
}

static void mi_clear_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    undo_snapshot(a);
    memset(a->doc->px[a->frame], 0, PX_CELLS);
    draw_frame(a);
    mark_dirty(a);
    menu_close(a);
}

static void mi_play_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->closing) {
        play_toggle(a);
    }
}

static void mi_speed_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    static const uint16_t steps[] = { 80, 120, 200, 300, 500, 1000 };
    int i = 0;
    while (i < (int)(sizeof(steps) / sizeof(steps[0])) && steps[i] <= a->doc->delay_ms) {
        i++;
    }
    a->doc->delay_ms = steps[i % (sizeof(steps) / sizeof(steps[0]))];
    mark_dirty(a);
    menu_refresh(a);
    editor_refresh(a);
}

static void export_done(app_t *a, bool ok, const char *path)
{
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (ok) {
        aos_hal_log(TAG, "exported %s", path);
        lv_label_set_text_fmt(a->info, "%s %s", _("Exportado:"), name);
        aos_ui_toast(name, 1500);
    } else {
        aos_hal_log(TAG, "export FAILED %s", path);
        aos_ui_toast(_("No se pudo exportar"), 1500);
    }
}

static void mi_png_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/lienzo%d-%d.png", px_dir(), a->slot + 1, a->frame + 1);
    int scale = 128 / a->doc->size;
    menu_close(a);
    export_done(a, px_export_png(a->doc, a->frame, scale, path), path);
}

static void mi_gif_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/lienzo%d.gif", px_dir(), a->slot + 1);
    int scale = 128 / a->doc->size;
    menu_close(a);
    export_done(a, px_export_gif(a->doc, scale, path), path);
}

static void mi_del_doc_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    if (!a->confirm_del) {
        a->confirm_del = true;
        menu_refresh(a);
        return;
    }
    char path[160];
    slot_path(a->slot, path, sizeof(path));
    remove(path);
    a->dirty = false;               /* nothing to save on the way out */
    menu_close(a);
    go_gallery(a);
}

/* Twenty-one objects that exist only while the menu is on screen. */
static void build_menu(app_t *a)
{
    a->menu = card(a->ed, 24, CV_Y, 320, 334);
    a->menu_del_req = false;
    lv_obj_add_flag(a->menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(a->menu, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a->menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(a->menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(a->menu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(a->menu, 12, 0);
    lv_obj_set_style_pad_row(a->menu, 4, 0);

    /* Seven fit without scrolling; the ones used every minute go first and
     * the destructive one is last, behind a swipe and a second tap. */
    const int32_t w = 292, h = 38;
    a->mi_play      = menu_item(a, a->menu, _("Reproducir"),         mi_play_cb,      w, h, AOS_C_CARD2);
                      menu_item(a, a->menu, _("Duplicar cuadro"),    mi_dup_cb,       w, h, AOS_C_CARD2);
                      menu_item(a, a->menu, _("Cuadro en blanco"),   mi_blank_cb,     w, h, AOS_C_CARD2);
                      menu_item(a, a->menu, _("Exportar GIF"),       mi_gif_cb,       w, h, lv_color_hex(0x0A5A9E));
                      menu_item(a, a->menu, _("Exportar PNG"),       mi_png_cb,       w, h, lv_color_hex(0x0A5A9E));
    a->mi_undo      = menu_item(a, a->menu, _("Deshacer"),           mi_undo_cb,      w, h, AOS_C_CARD2);
    a->mi_speed     = menu_item(a, a->menu, "",                      mi_speed_cb,     w, h, AOS_C_CARD2);
    a->mi_del_frame = menu_item(a, a->menu, _("Borrar cuadro"),      mi_del_frame_cb, w, h, AOS_C_CARD2);
                      menu_item(a, a->menu, _("Limpiar cuadro"),     mi_clear_cb,     w, h, AOS_C_CARD2);
    a->mi_del_doc   = menu_item(a, a->menu, _("Borrar lienzo"),      mi_del_doc_cb,   w, h, lv_color_hex(0x8A1E22));
}

static void build_editor(app_t *a, lv_obj_t *root)
{
    a->ed = lv_obj_create(root);
    lv_obj_remove_style_all(a->ed);
    lv_obj_set_size(a->ed, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->ed, 0, 0);
    lv_obj_remove_flag(a->ed, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->ed, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->ed, LV_OBJ_FLAG_HIDDEN);

    /* the bar: back, previous, frame (tap = play), next, tool, menu */
    bar_button(a->ed, LV_SYMBOL_LEFT, 14, 44, AOS_C_CARD2, ed_back_cb, a, NULL);
    bar_button(a->ed, LV_SYMBOL_PREV, 68, 44, AOS_C_CARD2, prev_cb, a, NULL);
    bar_button(a->ed, "1/1", 116, 96, lv_color_hex(0x1E3A5F), frame_lbl_cb, a, &a->lbl_frame);
    bar_button(a->ed, LV_SYMBOL_NEXT, 216, 44, AOS_C_CARD2, next_cb, a, NULL);
    a->btn_tool = bar_button(a->ed, LV_SYMBOL_EDIT, 268, 44, AOS_C_CARD2, tool_cb, a, &a->lbl_tool);
    lv_obj_set_style_border_color(a->btn_tool, lv_color_hex(0x5A5A64), 0);
    bar_button(a->ed, LV_SYMBOL_LIST, 318, 36, AOS_C_CARD2, menu_cb, a, NULL);

    a->canvas = lv_canvas_create(a->ed);
    lv_canvas_set_buffer(a->canvas, a->big, CV_PX, CV_PX, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, CV_PX, CV_PX);
    lv_obj_set_pos(a->canvas, CV_X, CV_Y);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* the touch layer over the canvas, which is the only thing that paints */
    a->touch = lv_obj_create(a->ed);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, CV_PX, CV_PX);
    lv_obj_set_pos(a->touch, CV_X, CV_Y);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESS_LOST, a);

    /* the palette strip: one canvas inside a container that scrolls sideways */
    a->strip = lv_obj_create(a->ed);
    lv_obj_remove_style_all(a->strip);
    lv_obj_set_size(a->strip, AOS_SCREEN_W - 28, STRIP_H);
    lv_obj_set_pos(a->strip, 14, STRIP_Y);
    lv_obj_set_scroll_dir(a->strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(a->strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(a->strip, LV_OBJ_FLAG_CLICKABLE);
    a->pal = lv_canvas_create(a->strip);
    lv_canvas_set_buffer(a->pal, a->palbuf, PAL_W, SWATCH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->pal, PAL_W, SWATCH);
    lv_obj_set_pos(a->pal, 0, (STRIP_H - SWATCH) / 2);
    lv_image_set_antialias(a->pal, false);
    lv_obj_add_flag(a->pal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->pal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->pal, swatch_cb, LV_EVENT_CLICKED, a);
    draw_palette(a);

    a->info = aos_label_boxed(a->ed, "", aos_font_small, AOS_C_DIM, AOS_SCREEN_W, 20);
    lv_obj_set_pos(a->info, 0, INFO_Y);
    lv_obj_remove_flag(a->info, LV_OBJ_FLAG_CLICKABLE);

}

/* --------------------------------------------------------------------------
 * Navigation between the two screens
 * -------------------------------------------------------------------------- */

static void go_gallery(app_t *a)
{
    if (a->dirty) {
        save_doc(a);
    }
    a->slot = -1;
    a->playing = false;
    lv_obj_add_flag(a->ed, LV_OBJ_FLAG_HIDDEN);
    gallery_refresh(a);
    lv_obj_remove_flag(a->gal, LV_OBJ_FLAG_HIDDEN);
}

static void go_editor(app_t *a, int slot)
{
    char path[160];
    slot_path(slot, path, sizeof(path));
    if (!px_doc_load(a->doc, path)) {
        aos_hal_log(TAG, "cannot open %s", path);
        aos_ui_toast(_("No se pudo abrir"), 1500);
        return;
    }
    note_file(a, path);
    a->slot = slot;
    a->frame = 0;
    a->dirty = false;
    a->has_undo = false;
    a->playing = false;
    a->tool = TOOL_PEN;
    a->watch_ms = now_ms();
    lv_obj_add_flag(a->gal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->sizer, LV_OBJ_FLAG_HIDDEN);
    menu_close(a);
    lv_obj_remove_flag(a->ed, LV_OBJ_FLAG_HIDDEN);
    set_color(a, a->color);
    show_frame(a, 0);
    aos_hal_log(TAG, "opened %s: %dx%d, %d frames", path, a->doc->size, a->doc->size, a->doc->frames);
}

/* --------------------------------------------------------------------------
 * The timer: deferred exit, playback, autosave, and watching the portal
 * -------------------------------------------------------------------------- */

static void timer_cb(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (a->closing) {
        return;
    }
    if (a->exit_req) {
        a->exit_req = false;
        a->exiting = true;                  /* so back() lets the system leave */
        aos_ui_back();
        return;                             /* the context may be gone now */
    }
    uint32_t now = now_ms();

    if (a->playing && (int32_t)(now - a->play_next_ms) >= 0) {
        a->play_next_ms = now + a->doc->delay_ms;
        a->frame = (a->frame + 1) % a->doc->frames;
        draw_frame(a);
        editor_refresh(a);
    }

    if (a->menu_del_req) {
        a->menu_del_req = false;
        lv_obj_delete(a->menu);
        a->menu = NULL;
    }

    if (a->dirty && a->slot >= 0 && now - a->changed_ms > AUTOSAVE_MS && !a->stroke &&
        (int32_t)(now - a->save_retry_ms) >= 0) {
        save_doc(a);
    }

    if (now - a->watch_ms > WATCH_MS) {
        a->watch_ms = now;
        if (a->slot >= 0) {
            if (!a->dirty && !a->stroke) {
                char path[160];
                slot_path(a->slot, path, sizeof(path));
                struct stat st;
                if (stat(path, &st) == 0 &&
                    ((long)st.st_size != a->file_size || (long)st.st_mtime != a->file_mtime)) {
                    int keep = a->frame;
                    if (px_doc_load(a->doc, path)) {
                        note_file(a, path);
                        a->has_undo = false;
                        show_frame(a, keep < a->doc->frames ? keep : 0);
                        aos_hal_log(TAG, "reloaded %s: it changed on disk", path);
                        aos_ui_toast(_("Actualizado desde el portal"), 1200);
                    }
                }
            }
        } else if (gallery_signature() != a->gal_sig) {
            gallery_refresh(a);
        }
    }
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * The sample canvases
 *
 * Seeded ONCE, the first time the app opens with nothing in the folder: a
 * black kitten walking (16x16, four frames), a beating heart (8x8, three),
 * a winking face (16x16, two) and a checkerboard. They are what a new user
 * sees before drawing anything, and what the GIF export is demonstrated on.
 * A preference remembers the seeding, so deleting them is respected.
 *
 * Content is tables, not code: the kitten costs .rodata, which the loader
 * sends to PSRAM, and not one byte of the 48 KB code reservation.
 * -------------------------------------------------------------------------- */

/* The kitten's legend, one char per palette index. */
static uint8_t kit_color(char ch)
{
    switch (ch) {
    case '.': return 14;    /* sky            */
    case ',': return 29;    /* lower sky, ice */
    case 'S': return 8;     /* sun            */
    case 's': return 9;     /* sun core       */
    case 'w': return 1;     /* cloud          */
    case 'k': return 0;     /* the cat        */
    case 'e': return 12;    /* eyes, lime     */
    case 'p': return 19;    /* nose, pink     */
    case 'g': return 10;    /* grass          */
    case 'G': return 11;    /* dark grass     */
    case 'l': return 12;    /* grass tuft     */
    case 'f': return 19;    /* pink flower    */
    case 'y': return 8;     /* yellow flower  */
    case 'W': return 1;     /* white flower   */
    default:  return 0;
    }
}

/* Four frames of the walk. The legs alternate, the tail wags, and the
 * flowers in the grass slide left one cell per frame, which is what makes
 * the cat look like it is going somewhere. */
static const char *const kit_frames[4][16] = {
    { "..............SS", ".............SsS", "..............SS", "...ww...........",
      "..wwww..........", "..k.............", ".k.......k...k..", ".k.......kkkkk..",
      ",kkkkkkkkkekek,,", ",,kkkkkkkkkkkp,,", ",,kkkkkkkkkk,,,,", ",,,k,k,,k,k,,,,,",
      "gggkggggkggggggg", "gfgggyggggWgggfg", "glgggglgggggglgg", "GGGGGGGGGGGGGGGG" },
    { "..............SS", ".............SsS", "..............SS", "...ww...........",
      "..wwww..........", ".k..............", ".k.......k...k..", ".k.......kkkkk..",
      ",kkkkkkkkkekek,,", ",,kkkkkkkkkkkp,,", ",,kkkkkkkkkk,,,,", ",,,k,k,,k,k,,,,,",
      "gggkgkggkgkggggg", "fgggyggggWgggfgg", "lgggglgggggglggg", "GGGGGGGGGGGGGGGG" },
    { "..............SS", ".............SsS", "..............SS", "...ww...........",
      "..wwww..........", "..k.............", ".k.......k...k..", ".k.......kkkkk..",
      ",kkkkkkkkkekek,,", ",,kkkkkkkkkkkp,,", ",,kkkkkkkkkk,,,,", ",,,k,k,,k,k,,,,,",
      "gggggkggggkggggg", "gggyggggWgggfggg", "ggggglgggggglggg", "GGGGGGGGGGGGGGGG" },
    { "..............SS", ".............SsS", "..............SS", "...ww...........",
      "..wwww..........", "..k.............", ".kk......k...k..", ".k.......kkkkk..",
      ",kkkkkkkkkekek,,", ",,kkkkkkkkkkkp,,", ",,kkkkkkkkkk,,,,", ",,,k,k,,k,k,,,,,",
      "gggkgkggkgkggggg", "ggyggggWgggfgggg", "ggggglgggggglggg", "GGGGGGGGGGGGGGGG" },
};

static void seed_samples(app_t *a)
{
    static const char *const heart[8] = {
        "........", ".XX..XX.", "XXXXXXXX", "XXXXXXXX",
        ".XXXXXX.", "..XXXX..", "...XX...", "........" };
    static const char *const face[16] = {
        "................", ".....BBBBBB.....", "...BBYYYYYYBB...", "..BYYYYYYYYYYB..",
        ".BYYYYYYYYYYYYB.", ".BYYKKYYYYKKYYB.", "BYYYKKYYYYKKYYYB", "BYYYYYYYYYYYYYYB",
        "BYYYYYYYYYYYYYYB", "BYYYKYYYYYYKYYYB", ".BYYYKKYYYYKKYYB", ".BYYYYKKKKKKYYB.",
        "..BYYYYYYYYYYB..", "...BBYYYYYYBB...", ".....BBBBBB.....", "................" };
    char path[160];

    /* 1: the kitten */
    px_doc_init(a->doc, 16);
    for (int f = 0; f < 4; f++) {
        if (f) px_doc_frame_blank(a->doc, f - 1);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                a->doc->px[f][y * 16 + x] = kit_color(kit_frames[f][y][x]);
    }
    a->doc->delay_ms = 200;
    slot_path(0, path, sizeof(path));
    px_doc_save(a->doc, path);

    /* 2: the heart, three shades */
    px_doc_init(a->doc, 8);
    for (int f = 0; f < 3; f++) {
        if (f) px_doc_frame_blank(a->doc, f - 1);
        int color = f == 0 ? 4 : f == 1 ? 19 : 5;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                a->doc->px[f][y * 8 + x] = heart[y][x] == 'X' ? (uint8_t)color : 0;
    }
    a->doc->delay_ms = 300;
    slot_path(1, path, sizeof(path));
    px_doc_save(a->doc, path);

    /* 3: the face, which winks */
    px_doc_init(a->doc, 16);
    for (int f = 0; f < 2; f++) {
        if (f) px_doc_frame_dup(a->doc, 0);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                char ch = face[y][x];
                uint8_t c = ch == 'B' ? 7 : ch == 'Y' ? 8 : 0;
                if (ch == 'K' && f == 1 && y < 7) c = 8;
                a->doc->px[f][y * 16 + x] = c;
            }
    }
    a->doc->delay_ms = 500;
    slot_path(2, path, sizeof(path));
    px_doc_save(a->doc, path);

    /* 4: a checkerboard in blues */
    px_doc_init(a->doc, 16);
    for (int i = 0; i < 256; i++) {
        int x = i % 16, y = i / 16;
        a->doc->px[0][i] = (uint8_t)(((x / 2) + (y / 2)) % 2 ? 13 + (x / 4) : 0);
    }
    slot_path(3, path, sizeof(path));
    px_doc_save(a->doc, path);

    aos_hal_log(TAG, "sample canvases written to %s", px_dir());
}

/* True when no canvas exists at all. */
static bool folder_empty(void)
{
    for (int i = 0; i < PX_SLOTS; i++) {
        char path[160];
        int sz, fr;
        slot_path(i, path, sizeof(path));
        if (px_doc_peek(path, &sz, &fr)) {
            return false;
        }
    }
    return true;
}

#define KEY_SEEDED  "px_seed"

static void *px_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }
    a->self = self;
    a->root = root;
    a->slot = -1;
    a->color = 1;                           /* white */
    a->last_cell = -1;

    /* Everything big goes through malloc() -PSRAM-, never lv_malloc(). */
    a->doc     = malloc(sizeof(px_doc_t));
    a->scratch = malloc(sizeof(px_doc_t));
    a->big     = malloc((size_t)CV_PX * CV_PX * sizeof(uint16_t));
    a->palbuf  = malloc((size_t)PAL_W * SWATCH * sizeof(uint16_t));
    bool ok = a->doc && a->scratch && a->big && a->palbuf;
    for (int i = 0; i < PX_SLOTS; i++) {
        a->thumb[i] = malloc((size_t)TH_PX * TH_PX * sizeof(uint16_t));
        ok = ok && a->thumb[i];
        if (a->thumb[i]) {
            memset(a->thumb[i], 0, (size_t)TH_PX * TH_PX * sizeof(uint16_t));
        }
    }
    if (!ok) {
        aos_hal_log(TAG, "out of memory");
        free(a->doc);
        free(a->scratch);
        free(a->big);
        free(a->palbuf);
        for (int i = 0; i < PX_SLOTS; i++) free(a->thumb[i]);
        lv_free(a);
        return NULL;
    }
    px_doc_init(a->doc, 16);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* The samples, once. In the simulator PX_DEMO=1 forces them, for the
     * screenshots. */
    int32_t seeded = 0;
    bool force = false;
#ifdef AOS_SIM_BUILTIN
    const char *env = getenv("PX_DEMO");
    force = env && env[0];
#endif
    if (force || (!(aos_hal_pref_get_i32(KEY_SEEDED, &seeded) && seeded) && folder_empty())) {
        if (folder_empty()) {
            seed_samples(a);
        }
        aos_hal_pref_set_i32(KEY_SEEDED, 1);
    }
    /* Files first, objects after: see gallery_scan(). */
    gallery_scan(a);

    build_gallery(a, root);
    build_editor(a, root);
    gallery_labels(a);
    a->watch_ms = now_ms();
    a->timer = lv_timer_create(timer_cb, TIMER_MS, a);

#ifdef AOS_SIM_BUILTIN
    /* Development switches, simulator only (on the board getenv() is NULL):
     *   PX_DEMO=1   write the samples if the folder is empty
     *   PX_SLOT=n   open canvas n straight away
     *   PX_FRAME=f  ...on frame f
     *   PX_MENU=1   ...with the menu open (to audit its layout)
     *   PX_NEW=1    open the size chooser */
    env = getenv("PX_SLOT");
    if (env && env[0]) {
        int s = atoi(env) - 1;
        if (s >= 0 && s < PX_SLOTS && a->slot_size[s]) {
            go_editor(a, s);
            const char *fr = getenv("PX_FRAME");
            if (fr && fr[0]) {
                show_frame(a, atoi(fr) - 1);
            }
            const char *m = getenv("PX_MENU");
            if (m && m[0]) {
                build_menu(a);
                menu_refresh(a);
                lv_obj_remove_flag(a->menu, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    env = getenv("PX_NEW");
    if (env && env[0]) {
        sizer_show(a, PX_SLOTS - 1);
    }
#endif
    return a;
}

static void px_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    if (a->dirty) {
        save_doc(a);
    }
    /* Our objects go HERE, while the context is alive: the runtime deletes
     * the root after destroy(), and the touch layer listens for PRESS_LOST. */
    a->closing = true;
    if (a->root) {
        lv_obj_clean(a->root);
    }
    free(a->doc);
    free(a->scratch);
    free(a->big);
    free(a->palbuf);
    for (int i = 0; i < PX_SLOTS; i++) {
        free(a->thumb[i]);
    }
    lv_free(a);
}

static void px_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a) {
        play_stop(a);
        if (a->dirty) {
            save_doc(a);
        }
    }
}

static bool px_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a || a->exiting) {
        return false;
    }
    if (!lv_obj_has_flag(a->sizer, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(a->sizer, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    if (a->slot >= 0) {
        if (a->playing) {
            play_stop(a);
            return true;
        }
        if (menu_open(a)) {
            menu_close(a);
            return true;
        }
        go_gallery(a);
        return true;
    }
    return false;
}

static bool px_init(aos_app_t *app)
{
    app->desc.id       = "aos.pixel";
    app->desc.name     = "Pixel Art";
    app->desc.icon     = LV_SYMBOL_EDIT;    /* fallback for a firmware without the vector */
    app->desc.icon_vec = AOS_ICON_PIXEL;
    app->desc.color_a  = 0xD93A6A;
    app->desc.color_b  = 0x5B2A86;
    app->desc.order    = 79;
    /* NO_SWIPE and LONG_DRAG together: a stroke across the canvas is a drag
     * far longer than 50 px, and neither the back gesture nor
     * lv_indev_wait_release() may cut it. Back is the arrow and the button. */
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN | AOS_APP_FLAG_NO_SWIPE |
                         AOS_APP_FLAG_LONG_DRAG;

    app->create  = px_create;
    app->destroy = px_destroy;
    app->hide    = px_hide;
    app->back    = px_back;
    return true;
}

AOS_APP_ENTRY(px_init);
