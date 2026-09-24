/*
 * AmoledOS - Control PC: the watch as a keyboard and media controller of the
 * computer it is plugged into (USB, docs/USB.md, D3).
 *
 * Nothing here knows about USB descriptors: the HAL exposes named keys and a
 * mode switch. The screen has two faces. With the keyboard ready it is a pad
 * of buttons; otherwise it says why and offers to switch the port to KEYS,
 * which is the one thing the user can do from here. The mode is not switched
 * back on exit: whoever plugged the watch into a computer to use it as a
 * remote wants it to stay one until Settings says otherwise.
 *
 * Every button is a named key, so adding one is a row in the table. The Mac
 * mutes on "mute" but does not unmute on a second "mute" (measured); a
 * volume key unmutes, which is what people press anyway.
 *
 * The mouse (D4) is a third face: the gyroscope's rates become pointer
 * motion, 50 times a second, an air mouse. Rates and not tilt angles: an
 * angle maps to a position and a badly zeroed rest angle drifts the pointer
 * into a corner (the same lesson 2043 learnt the other way round, where
 * absolute made sense because a ship has a home). Which gyro axis is "left"
 * depends on how the wrist is held, so the three switches 2043 has are
 * here too (invert X, invert Y, swap the axes), in preferences. Tap = left
 * click, hold = right click, the side button = left click.
 *
 * Since v0.6.0 the same surface is also a touchpad (aos_gesture.h): one
 * finger moves the pointer, two fingers scroll, a two-finger tap is the
 * right click and a pinch zooms (cmd+= / cmd+-, which is what a Mac and a
 * browser zoom with). While a finger is on the surface the gyroscope is
 * ignored: a wrist steadying itself for a swipe would drag the pointer.
 *
 * The gamepad (D5) is a fifth face: the tilt is the left stick (an angle
 * again: a stick is a position, and "Centrar" makes the current rest the
 * centre), a cross for the hat, four face buttons, shoulders, select and
 * start. One finger at a time on the glass, plus the tilt.
 *
 * MIDI (D7) is a fourth face: one octave of keys, press for note on and
 * release for note off, an octave up and down, and the pitch bend from the
 * accelerometer's roll when its switch is on (here an angle IS the right
 * thing: a bend is a position, the wrist level is the centre).
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_gesture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t   *status;
    lv_obj_t   *pad;        /* the buttons, shown when the keyboard is ready */
    lv_obj_t   *off;        /* the explanation and the switch, otherwise */
    lv_obj_t   *off_text;
    lv_obj_t   *off_button;
    lv_obj_t   *pad_face;   /* the gamepad face */
    lv_timer_t *pad_timer;
    bool        pad_on, pad_tilt;
    float       pad_ax0, pad_ay0;   /* the rest, "Centrar" */
    unsigned    pad_buttons;
    int         pad_hat;
    int         pad_x, pad_y;
    lv_obj_t   *midi;       /* the MIDI face */
    lv_obj_t   *midi_oct;   /* "C4" label between the octave buttons */
    lv_timer_t *midi_timer;
    bool        midi_on, midi_bend;
    int         octave;     /* 0..8: the C of the left key is 12 * octave */
    int         note_held;  /* -1 when none */
    int         bend_last;
    lv_obj_t   *mouse;      /* the air-mouse face */
    lv_obj_t   *mouse_gain; /* its speed button, relabelled on each press */
    lv_timer_t *timer;
    lv_timer_t *mouse_timer;
    bool        ready_shown;
    bool        mouse_on;
    bool        inv_x, inv_y, swap_xy;
    int         gain;       /* 1..4 */
    /* the touchpad on the mouse face */
    aos_app_t  *self;
    bool        tp_touching;
    float       tp_ax, tp_ay;       /* pointer motion not sent yet (sub-pixel) */
    float       tp_scroll;          /* finger travel not yet a wheel notch */
    float       tp_zoom;            /* log of the pinch not yet a zoom step */
    float       tp_moved;           /* how much a two-finger touch moved */
    uint32_t    tp_t0;
} pcremote_t;

#define TP_NOTCH_PX     14.0f   /* two-finger travel per wheel notch */
#define TP_ZOOM_STEP    0.18f   /* log(1.2): pinch per cmd+= / cmd+- */
#define TP_RCLICK_MS    300     /* a two-finger touch this short and still is a right click */

#define MOUSE_PERIOD_MS 20
#define MOUSE_DEADBAND  3.0f    /* dps: the hand at rest is never at zero */

static pcremote_t s_pc;

static void key_cb(lv_event_t *event)
{
    const char *name = lv_event_get_user_data(event);
    if (!aos_hal_usb_key(name)) {
        aos_ui_toast(_("Sin teclado USB"), 1400);
        return;
    }
    aos_hal_beep(1400, 15);
}

static void enable_cb(lv_event_t *event)
{
    (void)event;
    if (!aos_hal_usb_mode_set(AOS_HAL_USB_KEYS)) {
        aos_ui_toast(_("El USB esta cambiando de modo"), 1400);
    }
}

static lv_obj_t *key_button(lv_obj_t *parent, const char *text, lv_color_t color,
                            const char *key, int x, int y, int w, int h)
{
    lv_obj_t *b = aos_button(parent, text, color, key_cb, (void *)key);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    return b;
}

/* --- the mouse face ------------------------------------------------------- */

static void mouse_tick(lv_timer_t *timer)
{
    (void)timer;
    aos_imu_t imu;
    if (!s_pc.mouse_on || !aos_hal_imu_read(&imu)) {
        return;
    }
    /* Yaw (about the axis through the glass) moves left-right, pitch
     * up-down, when the watch is held like a remote. The pitch sign was
     * measured on the wrist (2026-09-13, "had to invert Y"): the minus is
     * that measurement, so the switches start off. They fix whatever
     * another wrist does to it. */
    if (s_pc.tp_touching) {
        return;                 /* the finger drives: see the touchpad below */
    }
    float rx = imu.gz, ry = -imu.gx;
    if (s_pc.swap_xy) { float t = rx; rx = ry; ry = t; }
    if (s_pc.inv_x) rx = -rx;
    if (s_pc.inv_y) ry = -ry;
    if (rx > -MOUSE_DEADBAND && rx < MOUSE_DEADBAND) rx = 0;
    if (ry > -MOUSE_DEADBAND && ry < MOUSE_DEADBAND) ry = 0;
    if (rx == 0 && ry == 0) {
        return;
    }
    /* 0.06 px per dps per report at speed 1: a lazy 100 dps turn crosses
     * 300 px a second; 2 and 3 are the ones that felt right on the wrist,
     * 4 is for a big screen far away. */
    float k = 0.06f * s_pc.gain;
    aos_hal_usb_mouse((int)(rx * k), (int)(ry * k), 0);
}

static void mouse_show(bool on)
{
    s_pc.mouse_on = on;
    s_pc.tp_touching = false;
    /* On the surface every drag is the pointer's: no back swipe, no cut at
     * 50 px. The rest of the app goes back with a swipe as before. */
    if (s_pc.self) {
        if (on) s_pc.self->desc.flags |= AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
        else    s_pc.self->desc.flags &= ~(uint32_t)(AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG);
    }
    if (on) {
        lv_obj_remove_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        aos_hal_imu_gyro_request(true);
        if (!s_pc.mouse_timer) {
            s_pc.mouse_timer = lv_timer_create(mouse_tick, MOUSE_PERIOD_MS, NULL);
        }
    } else {
        if (s_pc.mouse_timer) {
            lv_timer_delete(s_pc.mouse_timer);
            s_pc.mouse_timer = NULL;
        }
        aos_hal_imu_gyro_request(false);
        lv_obj_add_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);
        if (s_pc.ready_shown) {
            lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* --- the gamepad face ----------------------------------------------------- */

#define PAD_HAT_UP 1
#define PAD_HAT_RIGHT 3
#define PAD_HAT_DOWN 5
#define PAD_HAT_LEFT 7

static void pad_send(void)
{
    aos_hal_usb_gamepad(s_pc.pad_x, s_pc.pad_y, s_pc.pad_hat, s_pc.pad_buttons);
}

/* user data: bit (1 << n) for a button, or 0x100 | hat for the cross */
static void pad_btn_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    unsigned what = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    bool down = code == LV_EVENT_PRESSED;
    if (what & 0x100) {
        s_pc.pad_hat = down ? (int)(what & 0xFF) : 0;
    } else if (down) {
        s_pc.pad_buttons |= what;
    } else {
        s_pc.pad_buttons &= ~what;
    }
    if (!aos_hal_usb_gamepad(s_pc.pad_x, s_pc.pad_y, s_pc.pad_hat, s_pc.pad_buttons) && down) {
        aos_ui_toast(_("Sin gamepad USB"), 1200);
    }
}

static void pad_tick(lv_timer_t *timer)
{
    (void)timer;
    aos_imu_t imu;
    if (!s_pc.pad_on || !s_pc.pad_tilt || !aos_hal_imu_read(&imu)) {
        return;
    }
    /* 0.5 g (30 degrees) past the rest is full deflection; 0.03 g of dead
     * band; the stick report goes out every tick while tilt is on, as a
     * real stick's does. */
    float dx = imu.ax - s_pc.pad_ax0, dy = imu.ay - s_pc.pad_ay0;
    if (dx > -0.03f && dx < 0.03f) dx = 0;
    if (dy > -0.03f && dy < 0.03f) dy = 0;
    s_pc.pad_x = (int)(dx * 127.0f / 0.5f);
    s_pc.pad_y = (int)(-dy * 127.0f / 0.5f);
    pad_send();
}

static void pad_center_cb(lv_event_t *event)
{
    (void)event;
    aos_imu_t imu;
    if (aos_hal_imu_read(&imu)) {
        s_pc.pad_ax0 = imu.ax;
        s_pc.pad_ay0 = imu.ay;
        aos_hal_beep(1200, 15);
    }
}

static void pad_tilt_cb(lv_event_t *event)
{
    s_pc.pad_tilt = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (!s_pc.pad_tilt) {
        s_pc.pad_x = s_pc.pad_y = 0;
        pad_send();
    }
}

static void pad_show(bool on)
{
    s_pc.pad_on = on;
    if (on) {
        lv_obj_remove_flag(s_pc.pad_face, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        if (!s_pc.pad_timer) s_pc.pad_timer = lv_timer_create(pad_tick, 20, NULL);
    } else {
        if (s_pc.pad_timer) { lv_timer_delete(s_pc.pad_timer); s_pc.pad_timer = NULL; }
        s_pc.pad_x = s_pc.pad_y = s_pc.pad_hat = 0;
        s_pc.pad_buttons = 0;
        pad_send();                 /* everything released */
        lv_obj_add_flag(s_pc.pad_face, LV_OBJ_FLAG_HIDDEN);
        if (s_pc.ready_shown) lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pad_open_cb(lv_event_t *event)  { (void)event; pad_show(true); }
static void pad_close_cb(lv_event_t *event) { (void)event; pad_show(false); }

static lv_obj_t *pad_button(lv_obj_t *parent, const char *text, lv_color_t color, unsigned what,
                            int x, int y, int w, int h)
{
    lv_obj_t *b = aos_button(parent, text, color, pad_btn_cb, (void *)(uintptr_t)what);
    lv_obj_remove_event_cb(b, pad_btn_cb);      /* aos_button hooks CLICKED; a pad wants press and release */
    lv_obj_add_event_cb(b, pad_btn_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)what);
    lv_obj_add_event_cb(b, pad_btn_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)what);
    lv_obj_add_event_cb(b, pad_btn_cb, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)what);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    return b;
}

/* --- the MIDI face -------------------------------------------------------- */

static void midi_show(bool on);

static void midi_key_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    int note = 12 * s_pc.octave + (int)(intptr_t)lv_event_get_user_data(event);
    if (code == LV_EVENT_PRESSED) {
        if (s_pc.note_held >= 0) aos_hal_usb_midi_note(s_pc.note_held, 0, false);
        s_pc.note_held = note;
        if (!aos_hal_usb_midi_note(note, 100, true)) {
            aos_ui_toast(_("Sin puerto MIDI"), 1200);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_pc.note_held == note) {
            aos_hal_usb_midi_note(note, 0, false);
            s_pc.note_held = -1;
        }
    }
}

static void midi_oct_label(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "C%d", s_pc.octave - 1);   /* MIDI 60 is C4: octave 5 here */
    lv_label_set_text(s_pc.midi_oct, buf);
}

static void midi_oct_cb(lv_event_t *event)
{
    int d = (int)(intptr_t)lv_event_get_user_data(event);
    int o = s_pc.octave + d;
    if (o < 1 || o > 8) return;
    if (s_pc.note_held >= 0) { aos_hal_usb_midi_note(s_pc.note_held, 0, false); s_pc.note_held = -1; }
    s_pc.octave = o;
    aos_hal_pref_set_i32("pc_midi_oct", o);
    midi_oct_label();
}

static void midi_bend_cb(lv_event_t *event)
{
    s_pc.midi_bend = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (!s_pc.midi_bend && s_pc.bend_last != 0) {
        aos_hal_usb_midi_bend(0);
        s_pc.bend_last = 0;
    }
}

static void midi_tick(lv_timer_t *timer)
{
    (void)timer;
    aos_imu_t imu;
    if (!s_pc.midi_on || !s_pc.midi_bend || !aos_hal_imu_read(&imu)) {
        return;
    }
    /* Roll: the wrist level is the centre, 45 degrees either way is the
     * whole range. 0.05 g of dead band around level, and only a change of
     * 1/64 of the range is worth a message. */
    float a = imu.ax;
    if (a > -0.05f && a < 0.05f) a = 0;
    int bend = (int)(a * 8191.0f / 0.7f);
    if (bend > 8191) bend = 8191;
    if (bend < -8192) bend = -8192;
    if (bend / 256 != s_pc.bend_last / 256) {
        aos_hal_usb_midi_bend(bend);
        s_pc.bend_last = bend;
    }
}

static void midi_show(bool on)
{
    s_pc.midi_on = on;
    if (on) {
        lv_obj_remove_flag(s_pc.midi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        if (!s_pc.midi_timer) s_pc.midi_timer = lv_timer_create(midi_tick, 40, NULL);
    } else {
        if (s_pc.midi_timer) { lv_timer_delete(s_pc.midi_timer); s_pc.midi_timer = NULL; }
        if (s_pc.note_held >= 0) { aos_hal_usb_midi_note(s_pc.note_held, 0, false); s_pc.note_held = -1; }
        if (s_pc.bend_last) { aos_hal_usb_midi_bend(0); s_pc.bend_last = 0; }
        lv_obj_add_flag(s_pc.midi, LV_OBJ_FLAG_HIDDEN);
        if (s_pc.ready_shown) lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
    }
}

static void midi_open_cb(lv_event_t *event)  { (void)event; midi_show(true); }
static void midi_close_cb(lv_event_t *event) { (void)event; midi_show(false); }

static lv_obj_t *midi_key(lv_obj_t *parent, int semitone, bool black, int x, int y, int w, int h)
{
    lv_obj_t *k = lv_obj_create(parent);
    lv_obj_remove_style_all(k);
    lv_obj_set_size(k, w, h);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(k, black ? lv_color_hex(0x202020) : lv_color_hex(0xF2F2F2), 0);
    lv_obj_set_style_bg_color(k, AOS_C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(k, 6, 0);
    lv_obj_remove_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(k, midi_key_cb, LV_EVENT_PRESSED, (void *)(intptr_t)semitone);
    lv_obj_add_event_cb(k, midi_key_cb, LV_EVENT_RELEASED, (void *)(intptr_t)semitone);
    lv_obj_add_event_cb(k, midi_key_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)semitone);
    return k;
}

/* The surface as a touchpad. Pointer speed grows with the finger's: slow
 * for precision, fast to cross a big screen, scaled by the same "Vel." as
 * the air mouse. */
static void tp_gesture_cb(const aos_gesture_event_t *ev, void *user)
{
    (void)user;
    switch (ev->type) {
    case AOS_GESTURE_TAP:
        if (aos_hal_usb_click(1)) aos_hal_beep(1400, 10);
        break;
    case AOS_GESTURE_LONG_PRESS:
        if (aos_hal_usb_click(2)) aos_hal_beep(1000, 20);
        break;
    case AOS_GESTURE_DRAG_BEGIN:
        s_pc.tp_touching = true;
        s_pc.tp_ax = s_pc.tp_ay = 0;
        break;
    case AOS_GESTURE_DRAG: {
        float d = sqrtf(ev->dx * ev->dx + ev->dy * ev->dy);
        float k = 0.5f * (float)s_pc.gain * (1.0f + 0.12f * d);
        s_pc.tp_ax += ev->dx * k;
        s_pc.tp_ay += ev->dy * k;
        int mx = (int)s_pc.tp_ax, my = (int)s_pc.tp_ay;
        if (mx || my) {
            aos_hal_usb_mouse(mx, my, 0);
            s_pc.tp_ax -= (float)mx;
            s_pc.tp_ay -= (float)my;
        }
        break;
    }
    case AOS_GESTURE_DRAG_END:
    case AOS_GESTURE_PINCH_END:
        if (ev->type == AOS_GESTURE_PINCH_END &&
            ev->t_ms - s_pc.tp_t0 < TP_RCLICK_MS && s_pc.tp_moved < 20.0f) {
            if (aos_hal_usb_click(2)) aos_hal_beep(1000, 20);
        }
        s_pc.tp_touching = false;
        break;
    case AOS_GESTURE_PINCH_BEGIN:
        s_pc.tp_touching = true;
        s_pc.tp_t0 = ev->t_ms;
        s_pc.tp_moved = 0;
        s_pc.tp_scroll = 0;
        s_pc.tp_zoom = 0;
        break;
    case AOS_GESTURE_PINCH:
        s_pc.tp_moved += fabsf(ev->dx) + fabsf(ev->dy) + fabsf(ev->dist - ev->dist / ev->scale);
        /* Fingers down, notch "towards you": with the Mac's natural
         * scrolling (its default) the page then follows the fingers, as on
         * a trackpad. */
        s_pc.tp_scroll += ev->dy;
        while (s_pc.tp_scroll >= TP_NOTCH_PX)  { aos_hal_usb_mouse(0, 0, -1); s_pc.tp_scroll -= TP_NOTCH_PX; }
        while (s_pc.tp_scroll <= -TP_NOTCH_PX) { aos_hal_usb_mouse(0, 0, 1);  s_pc.tp_scroll += TP_NOTCH_PX; }
        s_pc.tp_zoom += logf(ev->scale);
        if (s_pc.tp_zoom >= TP_ZOOM_STEP) {
            aos_hal_usb_key("cmd+equal");
            s_pc.tp_zoom -= TP_ZOOM_STEP;
        } else if (s_pc.tp_zoom <= -TP_ZOOM_STEP) {
            aos_hal_usb_key("cmd+minus");
            s_pc.tp_zoom += TP_ZOOM_STEP;
        }
        break;
    default:
        break;
    }
}

static void mouse_open_cb(lv_event_t *event)  { (void)event; mouse_show(true); }
static void mouse_close_cb(lv_event_t *event) { (void)event; mouse_show(false); }

static void mouse_gain_label(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", _("Vel."), s_pc.gain);
    lv_label_set_text(lv_obj_get_child(s_pc.mouse_gain, 0), buf);
}

static void mouse_gain_cb(lv_event_t *event)
{
    (void)event;
    s_pc.gain = s_pc.gain % 4 + 1;
    aos_hal_pref_set_i32("pc_mouse_gain", s_pc.gain);
    mouse_gain_label();
}

static void mouse_switch_cb(lv_event_t *event)
{
    const char *key = lv_event_get_user_data(event);
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (!strcmp(key, "pc_mouse_invx")) s_pc.inv_x = on;
    else if (!strcmp(key, "pc_mouse_invy")) s_pc.inv_y = on;
    else s_pc.swap_xy = on;
    aos_hal_pref_set_i32(key, on ? 1 : 0);
}

static lv_obj_t *mouse_switch(lv_obj_t *parent, const char *text, const char *key, bool on, int x, int y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 100, 36);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = aos_label(row, text, aos_font_small, AOS_C_TEXT);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, mouse_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)key);
    return row;
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    aos_hal_usb_mode_t mode = aos_hal_usb_mode();
    bool ready = aos_hal_usb_keys_ready();
    char buf[96];

    if (aos_hal_usb_busy()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH "  %s", _("cambiando..."));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ORANGE, 0);
    } else if (ready) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB "  %s", _("teclado y red USB listos"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ACCENT, 0);
    } else if (mode == AOS_HAL_USB_KEYS) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB "  %s", _("esperando a la computadora"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ORANGE, 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  %s", _("teclado USB apagado"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_DIM, 0);
    }
    lv_label_set_text(s_pc.status, buf);

    if (ready != s_pc.ready_shown) {
        s_pc.ready_shown = ready;
        if (ready) {
            if (!s_pc.mouse_on && !s_pc.midi_on && !s_pc.pad_on) lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_pc.off, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_pc.mouse_on) mouse_show(false);
            if (s_pc.midi_on) midi_show(false);
            if (s_pc.pad_on) pad_show(false);
            lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_pc.off, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!ready) {
        /* Two different reasons, two different things to do. */
        if (mode == AOS_HAL_USB_KEYS) {
            lv_label_set_text(s_pc.off_text, _("Conecta el reloj a la computadora con el cable USB"));
            lv_obj_add_flag(s_pc.off_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_pc.off_text,
                              mode == AOS_HAL_USB_DISK
                                  ? _("El puerto esta en modo disco. Pasarlo a teclado saca la tarjeta de la computadora.")
                                  : _("El puerto USB es la consola. Como teclado, el reloj es ademas una red: el portal en http://192.168.7.1"));
            lv_obj_remove_flag(s_pc.off_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    s_pc.self = self;
    lv_obj_t *page = aos_page(root);

    s_pc.status = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM, AOS_SCREEN_W, 22);
    lv_obj_align(s_pc.status, LV_ALIGN_TOP_MID, 0, 14);

    /* --- the pad: six rows of 46 px, the last one ending at y 360, under
     * the touch ceiling (AOS_TOUCH_Y_MAX, 390). --- */
    s_pc.pad = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.pad);
    lv_obj_set_size(s_pc.pad, lv_pct(100), 320);
    lv_obj_align(s_pc.pad, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_SCROLLABLE);

    /* music */
    key_button(s_pc.pad, LV_SYMBOL_PREV,  AOS_C_CARD2,  "prev",  30, 0, 74, 46);
    key_button(s_pc.pad, LV_SYMBOL_PLAY,  AOS_C_ACCENT, "play", 138, 0, 92, 46);
    key_button(s_pc.pad, LV_SYMBOL_NEXT,  AOS_C_CARD2,  "next", 264, 0, 74, 46);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MID, AOS_C_CARD2, "voldown", 30, 54, 110, 46);
    key_button(s_pc.pad, LV_SYMBOL_MUTE,       AOS_C_CARD2, "mute",   146, 59, 76, 36);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MAX, AOS_C_CARD2, "volup", 228, 54, 110, 46);

    /* slides: page up / page down move Keynote and PowerPoint; "b" blanks */
    key_button(s_pc.pad, LV_SYMBOL_LEFT,  AOS_C_CARD2, "pgup",  30, 108, 150, 46);
    key_button(s_pc.pad, LV_SYMBOL_RIGHT, AOS_C_CARD2, "pgdn", 188, 108, 150, 46);
    key_button(s_pc.pad, _("Pantalla negra"), AOS_C_CARD2, "b",       30, 162, 150, 46);
    key_button(s_pc.pad, "cmd + tab",         AOS_C_CARD2, "cmd+tab", 188, 162, 150, 46);

    /* the three keys every dialog wants */
    key_button(s_pc.pad, "esc",     AOS_C_CARD2, "esc",    30, 216, 96, 46);
    key_button(s_pc.pad, _("espacio"), AOS_C_CARD2, "space", 136, 216, 96, 46);
    key_button(s_pc.pad, LV_SYMBOL_OK, AOS_C_ACCENT, "enter", 242, 216, 96, 46);

    /* the other three faces */
    lv_obj_t *mouse_btn = aos_button(s_pc.pad, "Mouse", AOS_C_CARD2, mouse_open_cb, NULL);
    lv_obj_set_size(mouse_btn, 96, 46);
    lv_obj_align(mouse_btn, LV_ALIGN_TOP_LEFT, 30, 270);
    lv_obj_t *midi_btn = aos_button(s_pc.pad, "MIDI", AOS_C_CARD2, midi_open_cb, NULL);
    lv_obj_set_size(midi_btn, 96, 46);
    lv_obj_align(midi_btn, LV_ALIGN_TOP_LEFT, 136, 270);
    lv_obj_t *pad_btn = aos_button(s_pc.pad, "Pad", AOS_C_CARD2, pad_open_cb, NULL);
    lv_obj_set_size(pad_btn, 96, 46);
    lv_obj_align(pad_btn, LV_ALIGN_TOP_LEFT, 242, 270);

    /* --- the gamepad: the cross left, the four buttons right, the rest below --- */
    s_pc.pad_face = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.pad_face);
    lv_obj_set_size(s_pc.pad_face, lv_pct(100), 320);
    lv_obj_align(s_pc.pad_face, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.pad_face, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_color_t g = AOS_C_CARD2;
        pad_button(s_pc.pad_face, LV_SYMBOL_UP,    g, 0x100 | PAD_HAT_UP,    72,  0, 52, 52);
        pad_button(s_pc.pad_face, LV_SYMBOL_LEFT,  g, 0x100 | PAD_HAT_LEFT,  16, 56, 52, 52);
        pad_button(s_pc.pad_face, LV_SYMBOL_RIGHT, g, 0x100 | PAD_HAT_RIGHT, 128, 56, 52, 52);
        pad_button(s_pc.pad_face, LV_SYMBOL_DOWN,  g, 0x100 | PAD_HAT_DOWN,  72, 112, 52, 52);
        pad_button(s_pc.pad_face, "Y", lv_color_hex(0xF2C744), 1u << 4, 268,  0, 52, 52);
        pad_button(s_pc.pad_face, "X", lv_color_hex(0x3D8BFF), 1u << 3, 212, 56, 52, 52);
        pad_button(s_pc.pad_face, "B", lv_color_hex(0xE4453A), 1u << 1, 324, 56, 52, 52);
        pad_button(s_pc.pad_face, "A", lv_color_hex(0x34C759), 1u << 0, 268, 112, 52, 52);
        pad_button(s_pc.pad_face, "L",  g, 1u << 6,  16, 178, 70, 40);
        pad_button(s_pc.pad_face, "Sel", g, 1u << 10, 96, 178, 82, 40);
        pad_button(s_pc.pad_face, "Start", g, 1u << 11, 188, 178, 92, 40);
        pad_button(s_pc.pad_face, "R",  g, 1u << 7, 290, 178, 70, 40);
    }
    lv_obj_t *center = aos_button(s_pc.pad_face, _("Centrar"), AOS_C_CARD2, pad_center_cb, NULL);
    lv_obj_set_size(center, 96, 44);
    lv_obj_align(center, LV_ALIGN_TOP_LEFT, 16, 236);
    {
        lv_obj_t *row = lv_obj_create(s_pc.pad_face);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 110, 44);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 122, 236);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = aos_label(row, _("Inclinar"), aos_font_small, AOS_C_TEXT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 44, 24);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, pad_tilt_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    lv_obj_t *pad_back = aos_button(s_pc.pad_face, _("Teclas"), AOS_C_ACCENT, pad_close_cb, NULL);
    lv_obj_set_size(pad_back, 110, 44);
    lv_obj_align(pad_back, LV_ALIGN_TOP_LEFT, 242, 236);
    lv_obj_add_flag(s_pc.pad_face, LV_OBJ_FLAG_HIDDEN);

    /* --- MIDI: an octave of keys, the octave buttons, the bend switch --- */
    s_pc.note_held = -1;
    int32_t oct_pref;
    s_pc.octave = aos_hal_pref_get_i32("pc_midi_oct", &oct_pref) && oct_pref >= 1 && oct_pref <= 8 ? (int)oct_pref : 5;
    s_pc.midi = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.midi);
    lv_obj_set_size(s_pc.midi, lv_pct(100), 320);
    lv_obj_align(s_pc.midi, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.midi, LV_OBJ_FLAG_SCROLLABLE);
    {
        /* Seven white keys of 46 px with 2 px between them, from x 14 to
         * 350; the five black ones sit on the joints, 28 px wide and 100
         * tall, created LAST so they are on top. */
        static const int white[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int black[5] = { 1, 3, 6, 8, 10 };
        static const int black_after[5] = { 0, 1, 3, 4, 5 };   /* the white key each one follows */
        for (int i = 0; i < 7; i++) {
            midi_key(s_pc.midi, white[i], false, 14 + i * 48, 0, 46, 170);
        }
        for (int i = 0; i < 5; i++) {
            midi_key(s_pc.midi, black[i], true, 14 + black_after[i] * 48 + 32, 0, 28, 100);
        }
    }
    lv_obj_t *oct_down = aos_button(s_pc.midi, LV_SYMBOL_MINUS, AOS_C_CARD2, midi_oct_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(oct_down, 70, 44);
    lv_obj_align(oct_down, LV_ALIGN_TOP_LEFT, 30, 184);
    s_pc.midi_oct = aos_label_boxed(s_pc.midi, "", aos_font_body, AOS_C_TEXT, 60, 44);
    lv_obj_align(s_pc.midi_oct, LV_ALIGN_TOP_LEFT, 104, 184);
    midi_oct_label();
    lv_obj_t *oct_up = aos_button(s_pc.midi, LV_SYMBOL_PLUS, AOS_C_CARD2, midi_oct_cb, (void *)(intptr_t)1);
    lv_obj_set_size(oct_up, 70, 44);
    lv_obj_align(oct_up, LV_ALIGN_TOP_LEFT, 168, 184);
    {
        lv_obj_t *row = lv_obj_create(s_pc.midi);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 96, 44);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 246, 184);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = aos_label(row, "Bend", aos_font_small, AOS_C_TEXT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 44, 24);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, midi_bend_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    lv_obj_t *midi_back = aos_button(s_pc.midi, _("Teclas"), AOS_C_ACCENT, midi_close_cb, NULL);
    lv_obj_set_size(midi_back, 308, 46);
    lv_obj_align(midi_back, LV_ALIGN_TOP_LEFT, 30, 240);
    lv_obj_add_flag(s_pc.midi, LV_OBJ_FLAG_HIDDEN);

    /* --- the mouse: a touch surface with the switches along the bottom --- */
    int32_t v;
    s_pc.inv_x   = aos_hal_pref_get_i32("pc_mouse_invx", &v) && v;
    s_pc.inv_y   = aos_hal_pref_get_i32("pc_mouse_invy", &v) && v;
    s_pc.swap_xy = aos_hal_pref_get_i32("pc_mouse_xy",   &v) && v;
    s_pc.gain    = aos_hal_pref_get_i32("pc_mouse_gain", &v) && v >= 1 && v <= 4 ? (int)v : 2;

    s_pc.mouse = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.mouse);
    lv_obj_set_size(s_pc.mouse, lv_pct(100), 320);
    lv_obj_align(s_pc.mouse, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.mouse, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *touch = lv_obj_create(s_pc.mouse);
    lv_obj_remove_style_all(touch);
    lv_obj_set_style_bg_color(touch, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(touch, 18, 0);
    lv_obj_set_size(touch, AOS_SCREEN_W - 40, 170);
    lv_obj_align(touch, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    /* FAST_TAP: a double click is two taps, and the computer times it */
    aos_gesture_attach(touch, AOS_GESTURE_FLAG_FAST_TAP, tp_gesture_cb, NULL);
    lv_obj_t *hint = aos_label(touch, _("Inclina el reloj o desliza el dedo.\nToca: clic. Dos dedos: scroll,\nzoom y clic derecho."),
                               aos_font_small, AOS_C_DIM);
    lv_obj_set_width(hint, AOS_SCREEN_W - 70);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(hint);
    aos_make_decorative(hint);      /* the taps belong to the surface, not the text */

    mouse_switch(s_pc.mouse, _("Inv X"), "pc_mouse_invx", s_pc.inv_x, 30, 184);
    mouse_switch(s_pc.mouse, _("Inv Y"), "pc_mouse_invy", s_pc.inv_y, 134, 184);
    mouse_switch(s_pc.mouse, _("Ejes"),  "pc_mouse_xy",   s_pc.swap_xy, 238, 184);

    s_pc.mouse_gain = aos_button(s_pc.mouse, "", AOS_C_CARD2, mouse_gain_cb, NULL);
    lv_obj_set_size(s_pc.mouse_gain, 120, 46);
    lv_obj_align(s_pc.mouse_gain, LV_ALIGN_TOP_LEFT, 30, 240);
    mouse_gain_label();
    lv_obj_t *back = aos_button(s_pc.mouse, _("Teclas"), AOS_C_ACCENT, mouse_close_cb, NULL);
    lv_obj_set_size(back, 170, 46);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 168, 240);
    lv_obj_add_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);

    /* --- the other face --- */
    s_pc.off = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.off);
    lv_obj_set_size(s_pc.off, lv_pct(100), 320);
    lv_obj_align(s_pc.off, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.off, LV_OBJ_FLAG_SCROLLABLE);

    s_pc.off_text = aos_label(s_pc.off, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(s_pc.off_text, AOS_SCREEN_W - 60);
    lv_label_set_long_mode(s_pc.off_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_pc.off_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pc.off_text, LV_ALIGN_TOP_MID, 0, 60);

    s_pc.off_button = aos_button(s_pc.off, _("Activar teclado USB"), AOS_C_ACCENT, enable_cb, NULL);
    lv_obj_set_size(s_pc.off_button, AOS_SCREEN_W - 90, 56);
    lv_obj_align(s_pc.off_button, LV_ALIGN_TOP_MID, 0, 200);

    /* Start on the "off" face; refresh() flips it if the keyboard is ready. */
    lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
    s_pc.ready_shown = false;

    s_pc.timer = lv_timer_create(refresh, 500, NULL);
    refresh(NULL);
    return &s_pc;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_pc.mouse_on) {
        mouse_show(false);      /* the gyro request and the 20 ms timer */
    }
    if (s_pc.midi_on) {
        midi_show(false);       /* the note off, the bend back to centre, the timer */
    }
    if (s_pc.pad_on) {
        pad_show(false);        /* everything released, the timer */
    }
    if (s_pc.timer) {
        lv_timer_delete(s_pc.timer);
        s_pc.timer = NULL;
    }
}

static bool button(aos_app_t *self, void *inst, int action)
{
    (void)self; (void)inst;
    if (s_pc.mouse_on && action == AOS_BUTTON_CLICK) {
        aos_hal_usb_click(1);
        return true;
    }
    if (s_pc.pad_on && (action == AOS_BUTTON_PRESS || action == AOS_BUTTON_CLICK || action == AOS_BUTTON_LONG)) {
        /* Runs in the HAL's task with the LVGL lock held: note and send. */
        if (action == AOS_BUTTON_PRESS) s_pc.pad_buttons |= 1u << 0; else s_pc.pad_buttons &= ~(1u << 0);
        pad_send();
        return true;
    }
    return false;
}

void aos_app_pcremote_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.pcremote",
            .name     = "Control PC",
            .icon     = LV_SYMBOL_USB,
            .color_a  = 0x5E5CE6,
            .color_b  = 0x2C2A80,
            .order    = 47,
        },
        .create  = create,
        .destroy = destroy,
        .button  = button,
    };
}
