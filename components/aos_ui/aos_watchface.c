/*
 * AmoledOS - Host for the watchfaces.
 *
 * Keeps the registry of faces, mounts the chosen one, hands out the refreshes
 * and handles the picker and the dimmed mode.
 *
 *   host
 *   +-- face_root      the active face is built in here
 *   +-- picker         bottom bar with arrows, name and confirm
 */
#include "aos_watchface.h"
#include "aos_internal.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

static aos_watchface_t s_faces[AOS_MAX_WATCHFACES];
static int             s_face_count;
static int             s_active = -1;
static void           *s_ctx;

static lv_obj_t *s_host;
static lv_obj_t *s_face_root;
static lv_obj_t *s_picker;
static lv_obj_t *s_picker_name;
static lv_obj_t *s_picker_dots;
static int       s_preview;        /* face being looked at in the picker */
static bool      s_aod;

/* -------------------------------------------------------------------------- */

void aos_watchface_register(const aos_watchface_t *face)
{
    if (!face || !face->id || s_face_count >= AOS_MAX_WATCHFACES) {
        return;
    }
    s_faces[s_face_count++] = *face;
}

int aos_watchface_count(void)
{
    return s_face_count;
}

const aos_watchface_t *aos_watchface_at(int index)
{
    return (index >= 0 && index < s_face_count) ? &s_faces[index] : NULL;
}

const char *aos_watchface_current(void)
{
    return (s_active >= 0) ? s_faces[s_active].id : NULL;
}

static int index_of(const char *id)
{
    for (int i = 0; i < s_face_count; i++) {
        if (id && strcmp(s_faces[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------- */

static void mount(int index)
{
    if (index < 0 || index >= s_face_count || !s_host) {
        return;
    }

    if (s_active >= 0) {
        if (s_faces[s_active].destroy) {
            s_faces[s_active].destroy(s_ctx);
        }
        if (s_face_root) {
            lv_obj_delete(s_face_root);
        }
        s_face_root = NULL;
        s_ctx = NULL;
    }

    s_face_root = lv_obj_create(s_host);
    lv_obj_remove_style_all(s_face_root);
    lv_obj_set_size(s_face_root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(s_face_root, 0, 0);
    lv_obj_set_style_bg_color(s_face_root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_face_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_face_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_face_root, LV_OBJ_FLAG_CLICKABLE);

    s_active = index;
    s_ctx = s_faces[index].create ? s_faces[index].create(s_face_root) : NULL;

    /* No face may keep the touch: the host's long press is what opens the
     * picker. */
    aos_make_decorative(s_face_root);

    if (s_faces[index].set_aod) {
        s_faces[index].set_aod(s_ctx, s_aod);
    }
    aos_watchface_refresh();

    /* the picker always above the face */
    if (s_picker) {
        lv_obj_move_foreground(s_picker);
    }
}

bool aos_watchface_select(const char *id)
{
    int index = index_of(id);
    if (index < 0 || index == s_active) {
        return index >= 0;
    }
    mount(index);
    aos_hal_pref_set_str("face", s_faces[index].id);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Picker                                                                      */
/* -------------------------------------------------------------------------- */

static void picker_update(void)
{
    if (!s_picker) {
        return;
    }
    lv_label_set_text(s_picker_name, _(s_faces[s_preview].name));

    char dots[AOS_MAX_WATCHFACES * 4 + 1] = {0};
    for (int i = 0; i < s_face_count; i++) {
        /* U+2022 BULLET and U+00B7 MIDDLE DOT, not U+25CF/U+25CB. The round
         * ones are not in the font: it is built with
         * "-r 0x20-0x7F,0xA0-0xFF,0x2022,0x20AC", so ● and ○ came out as
         * empty boxes on the watch. These two are inside those ranges. */
        strcat(dots, i == s_preview ? "\xE2\x80\xA2" : "\xC2\xB7");   /* • · */
        if (i != s_face_count - 1) {
            strcat(dots, " ");
        }
    }
    lv_label_set_text(s_picker_dots, dots);
}

static void picker_step(lv_event_t *event)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(event);
    s_preview = (s_preview + delta + s_face_count) % s_face_count;
    mount(s_preview);        /* the real face is shown, not a thumbnail */
    picker_update();
    aos_hal_activity();
}

static void picker_confirm(lv_event_t *event)
{
    (void)event;
    aos_hal_pref_set_str("face", s_faces[s_active].id);
    aos_watchface_close_picker();
    aos_ui_toast(_("Esfera cambiada"), 1200);
}

void aos_watchface_open_picker(void)
{
    if (s_picker || s_face_count < 2) {
        return;
    }
    s_preview = s_active;

    s_picker = lv_obj_create(s_host);
    lv_obj_remove_style_all(s_picker);
    lv_obj_set_size(s_picker, AOS_SCREEN_W, 96);
    /* Lifted 52 px off the bottom, and that is not decoration.
     *
     * Flush with the bottom edge the card sits at y 352..448 and its three
     * buttons -46 px tall, centred on the card- end up spanning y 369..415.
     * The digitiser reports nothing below y=395 (AOS_TOUCH_Y_MAX), so only the
     * top 22 px of each button could be pressed and the visual centre, which
     * is where a finger goes, was dead. The picker opened and then appeared
     * not to work.
     *
     * Raised, the card is at y 300..396 and the buttons at y 317..363, clear
     * of the limit with room to spare. The dots land at ~372..390, which is
     * fine: they are read, not pressed.
     *
     * tools/audit_layout.sh does not catch this on its own -it exempts
     * anything leaving a 20 px strip, and 22 px passed- so the arithmetic has
     * to be done by hand for anything touchable this low. */
    lv_obj_align(s_picker, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_bg_color(s_picker, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_picker, 28, 0);
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev = aos_button(s_picker, LV_SYMBOL_LEFT, AOS_C_CARD2,
                                picker_step, (void *)(intptr_t)-1);
    lv_obj_set_size(prev, 52, 46);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 14, -8);

    lv_obj_t *next = aos_button(s_picker, LV_SYMBOL_RIGHT, AOS_C_CARD2,
                                picker_step, (void *)(intptr_t)1);
    lv_obj_set_size(next, 52, 46);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -14, -8);

    lv_obj_t *ok = aos_button(s_picker, LV_SYMBOL_OK, AOS_C_ACCENT,
                              picker_confirm, NULL);
    lv_obj_set_size(ok, 74, 46);
    lv_obj_align(ok, LV_ALIGN_CENTER, 0, -8);

    s_picker_name = aos_label(s_picker, "", aos_font_small, AOS_C_TEXT);
    lv_obj_align(s_picker_name, LV_ALIGN_TOP_MID, 0, 4);

    s_picker_dots = aos_label(s_picker, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_picker_dots, LV_ALIGN_BOTTOM_MID, 0, -6);

    picker_update();
}

bool aos_watchface_picker_visible(void)
{
    return s_picker != NULL;
}

void aos_watchface_close_picker(void)
{
    if (!s_picker) {
        return;
    }
    lv_obj_delete(s_picker);
    s_picker = NULL;
    s_picker_name = NULL;
    s_picker_dots = NULL;

    /* if you left without confirming, it goes back to the stored face */
    char saved[24];
    if (aos_hal_pref_get_str("face", saved, sizeof(saved))) {
        int index = index_of(saved);
        if (index >= 0 && index != s_active) {
            mount(index);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Dimmed mode                                                                 */
/* -------------------------------------------------------------------------- */

void aos_watchface_set_aod(bool aod)
{
    if (aod == s_aod) {
        return;
    }
    s_aod = aod;

    if (aod) {
        aos_watchface_close_picker();
    }
    if (s_active >= 0 && s_faces[s_active].set_aod) {
        s_faces[s_active].set_aod(s_ctx, aod);
    }
    aos_watchface_refresh();
}

bool aos_watchface_is_aod(void)
{
    return s_aod;
}

/* -------------------------------------------------------------------------- */
/* Hook into the runtime                                                       */
/* -------------------------------------------------------------------------- */

static void long_press_cb(lv_event_t *event)
{
    (void)event;
    if (!aos_watchface_picker_visible()) {
        aos_watchface_open_picker();
    }
}

lv_obj_t *aos_watchface_create(lv_obj_t *parent)
{
    s_host = lv_obj_create(parent);
    lv_obj_remove_style_all(s_host);
    lv_obj_set_size(s_host, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(s_host, 0, 0);
    lv_obj_set_style_bg_color(s_host, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_host, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_host, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    aos_watchfaces_register_builtin();

    char saved[24];
    int index = 0;
    if (aos_hal_pref_get_str("face", saved, sizeof(saved))) {
        int found = index_of(saved);
        if (found >= 0) {
            index = found;
        }
    }
    mount(index);
    return s_host;
}

void aos_watchface_refresh(void)
{
    if (s_active < 0 || !s_faces[s_active].refresh) {
        return;
    }
    struct tm now;
    aos_hal_time_now(&now);
    s_faces[s_active].refresh(s_ctx, &now);
}

void aos_watchfaces_register_builtin(void)
{
    if (s_face_count > 0) {
        return;
    }
    void (*const getters[])(aos_watchface_t *) = {
        aos_face_digital_get,
        aos_face_analog_get,
        aos_face_minimal_get,
        aos_face_rings_get,
        aos_face_flip_get,
        aos_face_binary_get,
        aos_face_nixie_get,
    };
    for (unsigned i = 0; i < sizeof(getters) / sizeof(getters[0]); i++) {
        aos_watchface_t face;
        memset(&face, 0, sizeof(face));
        getters[i](&face);
        aos_watchface_register(&face);
    }
}
