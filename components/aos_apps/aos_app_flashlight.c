/* AmoledOS - Flashlight. Screen at full brightness, three colour modes. */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"

typedef struct {
    lv_obj_t *panel;
    int       mode;
    int       saved_brightness;
} flashlight_t;

static flashlight_t s_fl;

static const lv_color_t MODES[] = {
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    LV_COLOR_MAKE(0xFF, 0xC8, 0x78),   /* warm */
    LV_COLOR_MAKE(0xFF, 0x30, 0x20),   /* red, so night vision survives */
};

static void cycle_cb(lv_event_t *event)
{
    (void)event;
    s_fl.mode = (s_fl.mode + 1) % (int)(sizeof(MODES) / sizeof(MODES[0]));
    lv_obj_set_style_bg_color(s_fl.panel, MODES[s_fl.mode], 0);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    s_fl.saved_brightness = aos_hal_brightness_get();
    aos_hal_brightness_set(100);

    s_fl.panel = aos_page(root);
    lv_obj_set_style_bg_color(s_fl.panel, MODES[s_fl.mode], 0);
    lv_obj_add_flag(s_fl.panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_fl.panel, cycle_cb, LV_EVENT_CLICKED, NULL);
    return &s_fl;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    aos_hal_brightness_set(s_fl.saved_brightness);
    s_fl.panel = NULL;
}

void aos_app_flashlight_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.flashlight",
            .name     = "Linterna",
            .icon_vec = AOS_ICON_FLASHLIGHT,
            .color_a  = 0xFFD60A,
            .color_b  = 0xC8A000,
            .flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN,
            .order    = 60,
        },
        .create  = create,
        .destroy = destroy,
    };
}
