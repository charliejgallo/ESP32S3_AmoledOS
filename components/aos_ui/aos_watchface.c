/*
 * AmoledOS - Host for the watchfaces.
 *
 * Keeps the registry of faces, mounts the chosen one, hands out the refreshes
 * and the frames, and handles the picker, the face's touch and the dimmed
 * mode.
 *
 *   host
 *   +-- face_root      the active face is built in here
 *   +-- picker         bottom bar with arrows, name and confirm
 *
 * A face from a module (v0.15.0) is in the registry by its id, its name and
 * its file; its code is only in memory while it is the one mounted. The
 * module is opened in mount() and closed there when another face takes its
 * place, after its objects are gone: nothing of the module is left for LVGL
 * to call into.
 */
#include "aos_watchface.h"
#include "aos_internal.h"
#include "aos_control.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    aos_watchface_t desc;      /* a module's: id and name only, pointing below */
    bool            module;
    char            id[24];
    char            name[32];
    char            path[48];
} face_entry_t;

static face_entry_t s_faces[AOS_MAX_WATCHFACES];
static int          s_face_count;
static bool         s_builtin_done;
static int          s_active = -1;
static void        *s_ctx;

/* What is mounted: the entry's descriptor, or for a module the one its init
 * filled, whose functions live in the module. */
static aos_watchface_t        s_cur;
static void                  *s_module;          /* the open module's handle */
static aos_face_loader_t      s_loader;

static lv_obj_t   *s_host;
static lv_obj_t   *s_face_root;
static lv_obj_t   *s_picker;
static lv_obj_t   *s_picker_name;
static lv_obj_t   *s_picker_dots;
static lv_timer_t *s_frame_timer;
static uint32_t    s_frame_last_ms;
static int         s_preview;       /* face being looked at in the picker */
static bool        s_aod;
static bool        s_suspended;     /* torn down under an app, s_active kept */

/* The face's touch: a tap waits out the double-tap window on a face that
 * takes taps, so a double tap that opens the picker does not also toggle a
 * light twice on the Home Assistant face. */
#define DOUBLE_TAP_MS   320
#define DOUBLE_TAP_PX   60
static lv_timer_t *s_tap_timer;
static uint32_t    s_tap_ms;
static lv_point_t  s_tap_at;
static lv_point_t  s_touch_last;
static bool        s_touch_down;

/* -------------------------------------------------------------------------- */

static void register_builtin_once(void);
static int  index_of(const char *id);
static void unmount(void);
static void mount_or_fallback(int index);

void aos_watchface_register(const aos_watchface_t *face)
{
    register_builtin_once();
    if (!face || !face->id || s_face_count >= AOS_MAX_WATCHFACES) {
        return;
    }
    memset(&s_faces[s_face_count], 0, sizeof(s_faces[0]));
    s_faces[s_face_count++].desc = *face;
}

bool aos_watchface_register_module(const char *id, const char *name, const char *path)
{
    register_builtin_once();
    if (!id || !*id || !path || !*path) {
        return false;
    }
    int slot = -1;
    for (int i = 0; i < s_face_count; i++) {
        if (strcmp(s_faces[i].desc.id, id) == 0) {
            if (!s_faces[i].module) {
                return false;       /* a module never shadows a built-in face */
            }
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (s_face_count >= AOS_MAX_WATCHFACES) {
            return false;
        }
        slot = s_face_count++;
    } else if (slot == s_active && s_module) {
        return false;               /* the one on screen keeps its file */
    }
    face_entry_t *e = &s_faces[slot];
    memset(e, 0, sizeof(*e));
    e->module = true;
    snprintf(e->id, sizeof(e->id), "%s", id);
    snprintf(e->name, sizeof(e->name), "%s", name && *name ? name : id);
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->desc.id = e->id;
    e->desc.name = e->name;
    return true;
}

bool aos_watchface_unregister(const char *id)
{
    int i = index_of(id);
    if (i < 0 || !s_faces[i].module) {
        return false;               /* the firmware's own faces stay */
    }
    aos_watchface_close_picker();
    bool was_active = (i == s_active);
    if (was_active && !s_suspended) {
        unmount();                  /* its module goes before its entry */
    }
    memmove(&s_faces[i], &s_faces[i + 1], (size_t)(s_face_count - i - 1) * sizeof(s_faces[0]));
    s_face_count--;
    /* a module entry's descriptor points into the entry itself: moved, it
     * has to point at its new place */
    for (int j = i; j < s_face_count; j++) {
        if (s_faces[j].module) {
            s_faces[j].desc.id = s_faces[j].id;
            s_faces[j].desc.name = s_faces[j].name;
        }
    }
    if (s_active > i) {
        s_active--;
    } else if (was_active) {
        /* the one on the wrist went: the first face, chosen for good */
        s_active = 0;
        aos_hal_pref_set_str("face", s_faces[0].desc.id);
        if (!s_suspended) {
            mount_or_fallback(0);
        }
    }
    return true;
}

void aos_watchface_set_loader(const aos_face_loader_t *loader)
{
    if (loader) {
        s_loader = *loader;
    }
}

int aos_watchface_count(void)
{
    return s_face_count;
}

const aos_watchface_t *aos_watchface_at(int index)
{
    return (index >= 0 && index < s_face_count) ? &s_faces[index].desc : NULL;
}

const char *aos_watchface_path(int index)
{
    return (index >= 0 && index < s_face_count && s_faces[index].module)
           ? s_faces[index].path : NULL;
}

const char *aos_watchface_current(void)
{
    return (s_active >= 0) ? s_faces[s_active].desc.id : NULL;
}

static int index_of(const char *id)
{
    for (int i = 0; i < s_face_count; i++) {
        if (id && strcmp(s_faces[i].desc.id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/* Frames and liveness                                                        */
/* -------------------------------------------------------------------------- */

bool aos_watchface_live(void)
{
    return s_active >= 0 && !s_suspended && !s_aod && s_face_root && s_host &&
           !lv_obj_has_flag(s_host, LV_OBJ_FLAG_HIDDEN) && !aos_control_visible();
}

bool aos_watchface_can_blit(void)
{
    return aos_watchface_live() && !s_picker && aos_ui_face_unobstructed();
}

bool aos_watchface_gravity(float *x, float *y, float *z)
{
    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        return false;
    }
    /* docs/DECISIONES.md, "Ejes del acelerometro: MEDIDOS": standing, ax is
     * +1; lying on its right edge, ay is -1; face up, az is -1. */
    if (x) *x = -imu.ay;
    if (y) *y =  imu.ax;
    if (z) *z = -imu.az;
    return true;
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    uint32_t dt = now - s_frame_last_ms;
    s_frame_last_ms = now;
    if (!s_cur.frame || !aos_watchface_live()) {
        return;
    }
    /* after a pause (an app, the picker's other faces) the first frame does
     * not get the whole gap at once: the water would jump */
    if (dt > 100) {
        dt = 100;
    }
    s_cur.frame(s_ctx, dt);
}

/* The timer exists only while the face can be seen lit: dimmed or under an
 * app there is none, so nothing wakes the chip for it. */
static void frame_timer_update(void)
{
    bool want = s_cur.frame && s_face_root && !s_suspended && !s_aod;
    if (want && !s_frame_timer) {
        s_frame_last_ms = (uint32_t)aos_hal_uptime_ms();
        s_frame_timer = lv_timer_create(frame_cb, s_cur.frame_ms ? s_cur.frame_ms : 33, NULL);
    } else if (!want && s_frame_timer) {
        lv_timer_delete(s_frame_timer);
        s_frame_timer = NULL;
    }
}

/* -------------------------------------------------------------------------- */

static void tap_cancel(void)
{
    if (s_tap_timer) {
        lv_timer_delete(s_tap_timer);
        s_tap_timer = NULL;
    }
}

/* Takes down whatever is mounted: the face's own destroy, its objects, and
 * last its module, when nothing of it can be called any more. */
static void unmount(void)
{
    tap_cancel();
    s_touch_down = false;
    if (s_frame_timer) {
        lv_timer_delete(s_frame_timer);
        s_frame_timer = NULL;
    }
    if (s_face_root) {
        if (s_cur.destroy) {
            s_cur.destroy(s_ctx);
        }
        lv_obj_delete(s_face_root);
        s_face_root = NULL;
    }
    s_ctx = NULL;
    if (s_module) {
        if (s_loader.close) {
            s_loader.close(s_module);
        }
        s_module = NULL;
    }
    if (s_cur.id && strchr(s_cur.id, '.')) {
        aos_i18n_face_unload();
    }
    memset(&s_cur, 0, sizeof(s_cur));
}

static bool mount(int index)
{
    if (index < 0 || index >= s_face_count || !s_host) {
        return false;
    }

    unmount();
    s_suspended = false;

    face_entry_t *e = &s_faces[index];
    if (e->module) {
        aos_watchface_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        void *handle = s_loader.open ? s_loader.open(e->path, &loaded) : NULL;
        if (!handle) {
            aos_hal_log("face", "could not load %s (%s)", e->id, e->path);
            s_active = -1;
            return false;
        }
        s_module = handle;
        s_cur = loaded;
        /* the registry keeps its own copies: these outlive the module */
        s_cur.id = e->id;
        s_cur.name = e->name;
    } else {
        s_cur = e->desc;
    }
    /* A face that is not one of the firmware's seven ("face.agua", from a
     * module or, in the simulator, compiled in) has a catalogue of its own;
     * the seven have dotless ids and their strings are the system's. */
    if (strchr(s_cur.id, '.')) {
        aos_i18n_face_load(s_cur.id);
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
    s_ctx = s_cur.create ? s_cur.create(s_face_root) : NULL;

    /* No face may keep the touch: the host's long press is what opens the
     * picker, and a face that wants the finger gets it through touch(). */
    aos_make_decorative(s_face_root);

    if (s_cur.set_aod) {
        s_cur.set_aod(s_ctx, s_aod);
    }
    aos_watchface_refresh();
    frame_timer_update();

    /* the picker always above the face */
    if (s_picker) {
        lv_obj_move_foreground(s_picker);
    }
    return true;
}

/* Mounts 'index', or the first face that mounts if it does not: a module
 * whose file went away must not leave the clock black. The preference is not
 * touched, so the face comes back on the boot that finds it again. */
static void mount_or_fallback(int index)
{
    if (mount(index)) {
        return;
    }
    for (int i = 0; i < s_face_count; i++) {
        if (i != index && mount(i)) {
            return;
        }
    }
}

void aos_watchface_suspend(void)
{
    if (s_suspended || s_active < 0 || !s_face_root || aos_watchface_picker_visible()) {
        return;
    }
    int active = s_active;
    unmount();
    s_active = active;
    s_suspended = true;
}

void aos_watchface_resume(void)
{
    if (s_suspended) {
        mount_or_fallback(s_active);
    }
}

void aos_watchface_remount(void)
{
    if (s_active >= 0 && !s_suspended && s_face_root && !s_picker) {
        mount_or_fallback(s_active);
    }
}

bool aos_watchface_select(const char *id)
{
    int index = index_of(id);
    if (index < 0 || index == s_active) {
        return index >= 0;
    }
    if (s_suspended) {
        /* chosen from an app (Settings): it is built when the app goes */
        s_active = index;
        aos_hal_pref_set_str("face", s_faces[index].desc.id);
        return true;
    }
    if (!mount(index)) {
        mount_or_fallback(index);
        return false;
    }
    aos_hal_pref_set_str("face", s_faces[index].desc.id);
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
    lv_label_set_text(s_picker_name, _(s_faces[s_preview].desc.name));
    /* "3 / 12", not a dot per face: with the faces from the card the dots
     * were twelve specks nobody could count (tried on the board) */
    lv_label_set_text_fmt(s_picker_dots, "%d / %d", s_preview + 1, s_face_count);
}

void aos_watchface_picker_step(int delta)
{
    if (!s_picker || s_face_count < 2) {
        return;
    }
    /* the real face is shown, not a thumbnail; one that does not load is
     * stepped over */
    for (int tries = 0; tries < s_face_count; tries++) {
        s_preview = (s_preview + delta + s_face_count) % s_face_count;
        if (mount(s_preview)) {
            break;
        }
    }
    if (s_active < 0) {
        mount_or_fallback(0);
        s_preview = s_active < 0 ? 0 : s_active;
    }
    picker_update();
    aos_hal_activity();
}

static void picker_step(lv_event_t *event)
{
    aos_watchface_picker_step((int)(intptr_t)lv_event_get_user_data(event));
}

static void picker_confirm(lv_event_t *event)
{
    (void)event;
    if (s_active >= 0) {
        aos_hal_pref_set_str("face", s_faces[s_active].desc.id);
    }
    aos_watchface_close_picker();
    aos_ui_toast(_("Esfera cambiada"), 1200);
}

void aos_watchface_open_picker(void)
{
    if (s_picker || s_face_count < 2) {
        return;
    }
    tap_cancel();
    s_preview = s_active < 0 ? 0 : s_active;

    s_picker = lv_obj_create(s_host);
    lv_obj_remove_style_all(s_picker);
    lv_obj_set_size(s_picker, AOS_SCREEN_W - 16, 136);
    /* Lifted 52 px off the bottom, and that is not decoration.
     *
     * The digitiser reports nothing below y=395 (AOS_TOUCH_Y_MAX): flush with
     * the bottom edge, the first picker's buttons kept 22 px that could be
     * pressed and a dead visual centre. Here the card is at y 260..396 and the
     * buttons, 70 px tall at its bottom, at y 314..384: all of them inside.
     *
     * tools/audit_layout.sh does not catch this on its own -it exempts
     * anything leaving a 20 px strip- so the arithmetic has to be done by hand
     * for anything touchable this low.
     *
     * v0.15.0: bigger, for a finger and not a stylus (the 52x46 arrows were
     * hard to hit on the wrist), with the name large, "3 / 12" instead of a
     * dot per face, and a swipe sideways anywhere doing what the arrows do
     * (aos_ui.c, handle_gesture). */
    lv_obj_align(s_picker, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_bg_color(s_picker, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_picker, 30, 0);
    lv_obj_remove_flag(s_picker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev = aos_button(s_picker, LV_SYMBOL_LEFT, AOS_C_CARD2,
                                picker_step, (void *)(intptr_t)-1);
    lv_obj_set_size(prev, 84, 70);
    lv_obj_set_style_radius(prev, 22, 0);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_LEFT, 10, -12);

    lv_obj_t *next = aos_button(s_picker, LV_SYMBOL_RIGHT, AOS_C_CARD2,
                                picker_step, (void *)(intptr_t)1);
    lv_obj_set_size(next, 84, 70);
    lv_obj_set_style_radius(next, 22, 0);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -10, -12);

    lv_obj_t *ok = aos_button(s_picker, LV_SYMBOL_OK, AOS_C_ACCENT,
                              picker_confirm, NULL);
    lv_obj_set_size(ok, 128, 70);
    lv_obj_set_style_radius(ok, 22, 0);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);

    s_picker_name = aos_label(s_picker, "", aos_font_title, AOS_C_TEXT);
    lv_label_set_long_mode(s_picker_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_picker_name, AOS_SCREEN_W - 150);
    lv_obj_set_style_text_align(s_picker_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_picker_name, LV_ALIGN_TOP_MID, 0, 10);

    s_picker_dots = aos_label(s_picker, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_picker_dots, LV_ALIGN_TOP_RIGHT, -20, 18);

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
            mount_or_fallback(index);
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
        aos_watchface_resume();     /* dimmed, the face is what is shown */
    }

    if (aod) {
        aos_watchface_close_picker();
        tap_cancel();
    }
    if (s_active >= 0 && s_face_root && s_cur.set_aod) {
        s_cur.set_aod(s_ctx, aod);
    }
    frame_timer_update();
    aos_watchface_refresh();
}

bool aos_watchface_is_aod(void)
{
    return s_aod;
}

/* -------------------------------------------------------------------------- */
/* Hook into the runtime: the long press, the double tap, the face's touch     */
/* -------------------------------------------------------------------------- */

static bool face_takes_touch(void)
{
    return s_face_root && !s_picker && !s_aod && s_cur.touch &&
           (s_cur.flags & AOS_FACE_FLAG_TOUCH);
}

static lv_point_t touch_point(void)
{
    lv_point_t p = { 0, 0 };
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    return p;
}

static void tap_deliver(lv_timer_t *timer)
{
    (void)timer;
    s_tap_timer = NULL;         /* a one-shot: LVGL deletes it after this */
    if (face_takes_touch()) {
        s_cur.touch(s_ctx, AOS_FACE_TOUCH_TAP, s_tap_at.x, s_tap_at.y);
    }
}

static void host_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t p = touch_point();

    switch (code) {
    case LV_EVENT_PRESSED:
        s_touch_down = true;
        s_touch_last = p;
        if (face_takes_touch()) {
            s_cur.touch(s_ctx, AOS_FACE_TOUCH_PRESS, p.x, p.y);
        }
        break;

    case LV_EVENT_PRESSING:
        if (s_touch_down && (p.x != s_touch_last.x || p.y != s_touch_last.y)) {
            s_touch_last = p;
            if (face_takes_touch()) {
                s_cur.touch(s_ctx, AOS_FACE_TOUCH_MOVE, p.x, p.y);
            }
        }
        break;

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (s_touch_down) {
            s_touch_down = false;
            if (face_takes_touch()) {
                s_cur.touch(s_ctx, AOS_FACE_TOUCH_RELEASE, s_touch_last.x, s_touch_last.y);
            }
        }
        break;

    case LV_EVENT_SHORT_CLICKED: {
        if (s_picker || s_aod) {
            break;
        }
        uint32_t now = (uint32_t)aos_hal_uptime_ms();
        bool second = s_tap_ms && now - s_tap_ms < DOUBLE_TAP_MS &&
                      LV_ABS(p.x - s_tap_at.x) < DOUBLE_TAP_PX &&
                      LV_ABS(p.y - s_tap_at.y) < DOUBLE_TAP_PX;
        if (second) {
            s_tap_ms = 0;
            tap_cancel();
            aos_watchface_open_picker();
            break;
        }
        s_tap_ms = now;
        s_tap_at = p;
        if (face_takes_touch()) {
            tap_cancel();
            s_tap_timer = lv_timer_create(tap_deliver, DOUBLE_TAP_MS, NULL);
            lv_timer_set_repeat_count(s_tap_timer, 1);
        }
        break;
    }

    case LV_EVENT_LONG_PRESSED:
        if (aos_watchface_picker_visible()) {
            break;
        }
        if (face_takes_touch() && s_cur.touch(s_ctx, AOS_FACE_TOUCH_LONG, p.x, p.y)) {
            break;
        }
        aos_watchface_open_picker();
        break;

    default:
        break;
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
    /* one by one and not LV_EVENT_ALL: that one also brings every draw
     * event of the host, several per frame */
    static const lv_event_code_t TOUCH_EVENTS[] = {
        LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST,
        LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED,
    };
    for (unsigned i = 0; i < sizeof(TOUCH_EVENTS) / sizeof(TOUCH_EVENTS[0]); i++) {
        lv_obj_add_event_cb(s_host, host_event_cb, TOUCH_EVENTS[i], NULL);
    }

    register_builtin_once();

    char saved[24];
    int index = 0;
    if (aos_hal_pref_get_str("face", saved, sizeof(saved))) {
        int found = index_of(saved);
        if (found >= 0) {
            index = found;
        }
    }
    mount_or_fallback(index);
    return s_host;
}

void aos_watchface_refresh(void)
{
    if (s_active < 0 || s_suspended || !s_face_root || !s_cur.refresh) {
        return;
    }
    struct tm now;
    aos_hal_time_now(&now);
    s_cur.refresh(s_ctx, &now);
}

/* The built-in faces go first, whoever registers first: the face modules are
 * registered before the UI exists (main.c), so that the face chosen is in the
 * registry when the clock is mounted, and the picker still starts with the
 * firmware's seven. */
static void register_builtin_once(void)
{
    if (s_builtin_done) {
        return;
    }
    s_builtin_done = true;
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
        if (s_face_count >= AOS_MAX_WATCHFACES) {
            break;
        }
        memset(&s_faces[s_face_count], 0, sizeof(s_faces[0]));
        getters[i](&s_faces[s_face_count].desc);
        s_face_count++;
    }
}

void aos_watchfaces_register_builtin(void)
{
    register_builtin_once();
}
