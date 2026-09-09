/*
 * AmoledOS - Example dynamic app.
 *
 * It builds as a shared object and is copied to /sdcard/apps:
 *
 *     cd apps/hello_app
 *     idf.py set-target esp32s3
 *     idf.py so
 *     cp build/hello_app.so /Volumes/<sd>/apps/
 *
 * It links against nothing: the calls into LVGL and the HAL are left
 * unresolved and the firmware fills them in when it does the dlopen().
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>

typedef struct {
    lv_obj_t *counter_label;
    int       taps;
} hello_ctx_t;

static void tap_cb(lv_event_t *event)
{
    hello_ctx_t *ctx = (hello_ctx_t *)lv_event_get_user_data(event);
    ctx->taps++;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", ctx->taps);
    lv_label_set_text(ctx->counter_label, buf);
    aos_hal_beep(1500, 30);
}

static void *hello_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    hello_ctx_t *ctx = lv_malloc_zeroed(sizeof(hello_ctx_t));
    if (!ctx) {
        return NULL;
    }

    lv_obj_set_style_bg_color(root, lv_color_hex(0x101018), 0);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, _("Hola desde un .so"));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    ctx->counter_label = lv_label_create(root);
    lv_label_set_text(ctx->counter_label, "0");
    lv_obj_set_style_text_color(ctx->counter_label, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(ctx->counter_label, &aos_montserrat_48, 0);
    lv_obj_center(ctx->counter_label);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, _("tocar para sumar"));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_CLICKED, ctx);
    return ctx;
}

static void hello_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    lv_free(inst);
}

static bool hello_init(aos_app_t *app)
{
    app->desc.id      = "demo.hello";
    app->desc.name    = "Hola";
    app->desc.icon    = LV_SYMBOL_OK;
    app->desc.color_a = 0x0A84FF;
    app->desc.color_b = 0x0050A0;
    app->desc.order   = 200;

    app->create  = hello_create;
    app->destroy = hello_destroy;
    return true;
}

AOS_APP_ENTRY(hello_init);
