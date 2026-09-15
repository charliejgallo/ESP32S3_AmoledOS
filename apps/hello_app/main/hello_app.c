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
 *
 * It brings its own launcher icon, too (HELLO_ICON below): a few dozen bytes
 * of shapes the firmware has never seen, handed over from init(). No enum to
 * extend, no firmware to rebuild. docs/ICONS.md has the format.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"

#include <stdio.h>

/* A speech bubble with a face in it. Coordinates are percent of the icon
 * size, so the one drawing serves the launcher's 66, 74 and 82 px. The
 * bubble and its tail are RECTs; the eyes and the smile are children of the
 * bubble (INTO ... OUT), so they are placed against it and not the circle;
 * the smile is the bottom half of an ARC with no track. */
static const uint8_t HELLO_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -16,  20, 14, 12,  3,          AIC_C_TEXT, 255),   /* tail   */
    AIC_RECT(AIC_CENTER,   0,  -6, 62, 46, 14,          AIC_C_TEXT, 255),   /* bubble */
    AIC_INTO,
    AIC_RECT(AIC_TOP_MID, -11, 11,  8,  8, AIC_CIRCLE,  AIC_C_BG,   255),   /* eyes   */
    AIC_RECT(AIC_TOP_MID,  11, 11,  8,  8, AIC_CIRCLE,  AIC_C_BG,   255),
    AIC_ARC(AIC_CENTER, 0, 6, 30, 0, 4, 0, 360, 25, 155, 0,                /* smile  */
            AIC_C_BG, 0, AIC_C_BG, 255),
    AIC_OUT,
    AIC_END
};

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
    app->desc.id       = "demo.hello";           /* before the icon: it is keyed by id */
    app->desc.name     = "Hola";
    app->desc.icon     = LV_SYMBOL_OK;           /* the fallback, if the blob were refused */
    app->desc.icon_vec = AOS_ICON_NONE;          /* not one of the firmware's */
    app->desc.color_a  = 0x0A84FF;
    app->desc.color_b  = 0x0050A0;
    app->desc.order    = 200;
    aos_icon_set_ops(app, HELLO_ICON, sizeof HELLO_ICON);

    app->create  = hello_create;
    app->destroy = hello_destroy;
    return true;
}

AOS_APP_ENTRY(hello_init);
