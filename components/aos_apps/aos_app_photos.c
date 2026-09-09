/*
 * AmoledOS - Photo viewer.
 *
 * Lists the files in the photos folder and shows them full screen. Decoding is
 * done by LVGL: TJPGD for .jpg and LODEPNG for .png, plus the .bin files
 * already converted to the native format (the fastest to draw).
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PHOTOS      64
#define NAME_MAX_LEN    64

typedef struct {
    char     names[MAX_PHOTOS][NAME_MAX_LEN];
    int      count;
    int      current;
    lv_obj_t *list_view;
    lv_obj_t *photo_view;
    void     *canvas_buf;
    lv_obj_t *image;
    lv_obj_t *caption;
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
    lv_image_header_t header;
    char caption[128];
    if (lv_image_decoder_get_info(path, &header) == LV_RESULT_OK) {
        uint32_t bytes = (uint32_t)header.w * header.h * 2u;
        if (bytes > AOS_PHOTO_MAX_BYTES) {
            snprintf(caption, sizeof(caption),
                     _("%s\n%dx%d no entra en memoria\n"
                       "(necesita %u KB)\nAchicala a %dx%d o menos"),
                     s_photos.names[index], (int)header.w, (int)header.h,
                     (unsigned)(bytes / 1024), AOS_SCREEN_W, AOS_SCREEN_H);
            lv_canvas_fill_bg(s_photos.image, lv_color_hex(0x000000),
                              LV_OPA_COVER);
            lv_label_set_text(s_photos.caption, caption);
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
     * bitmap to cache never comes into existence. */
    lv_canvas_fill_bg(s_photos.image, lv_color_hex(0x000000), LV_OPA_COVER);

    int32_t escala = 256;
    if (header.w > 0 && header.h > 0) {
        int32_t ex = (int32_t)AOS_SCREEN_W * 256 / (int32_t)header.w;
        int32_t ey = (int32_t)AOS_SCREEN_H * 256 / (int32_t)header.h;
        escala = ex < ey ? ex : ey;
        if (escala > 256) {
            escala = 256;           /* we do not enlarge: it would look blurry */
        }
    }
    int32_t dw = (int32_t)header.w * escala / 256;
    int32_t dh = (int32_t)header.h * escala / 256;

    lv_layer_t capa;
    lv_canvas_init_layer(s_photos.image, &capa);

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src     = path;
    dsc.scale_x = escala;
    dsc.scale_y = escala;

    lv_area_t donde = {
        .x1 = (AOS_SCREEN_W - dw) / 2,
        .y1 = (AOS_SCREEN_H - dh) / 2,
    };
    donde.x2 = donde.x1 + (int32_t)header.w - 1;
    donde.y2 = donde.y1 + (int32_t)header.h - 1;
    lv_draw_image(&capa, &dsc, &donde);

    lv_canvas_finish_layer(s_photos.image, &capa);

    snprintf(caption, sizeof(caption), "%d / %d   %s",
             index + 1, s_photos.count, s_photos.names[index]);
    lv_label_set_text(s_photos.caption, caption);

    lv_obj_add_flag(s_photos.list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_photos.photo_view, LV_OBJ_FLAG_HIDDEN);
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
    (void)self;
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

    /* A canvas the size of the screen: it is the target of the single decode.
     * 368*448*2 = 330 KB, goes to PSRAM. */
    s_photos.canvas_buf = malloc((size_t)AOS_SCREEN_W * AOS_SCREEN_H * 2);
    s_photos.image = lv_canvas_create(s_photos.photo_view);
    if (s_photos.canvas_buf) {
        lv_canvas_set_buffer(s_photos.image, s_photos.canvas_buf,
                             AOS_SCREEN_W, AOS_SCREEN_H, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_center(s_photos.image);

    s_photos.caption = aos_label(s_photos.photo_view, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_photos.caption, LV_ALIGN_BOTTOM_MID, 0, -56);

    lv_obj_t *prev = aos_button(s_photos.photo_view, LV_SYMBOL_LEFT, AOS_C_CARD2,
                                nav_cb, (void *)(intptr_t)-1);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_LEFT, 24, -8);
    lv_obj_t *next = aos_button(s_photos.photo_view, LV_SYMBOL_RIGHT, AOS_C_CARD2,
                                nav_cb, (void *)(intptr_t)1);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -24, -8);

    return &s_photos;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    free(s_photos.canvas_buf);
    s_photos.canvas_buf = NULL;
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
