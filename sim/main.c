/*
 * AmoledOS - Desktop simulator (SDL2).
 *
 *   cd sim && cmake -B build && cmake --build build && ./build/amoledos_sim
 *
 * A 368x448 window, the same size as the AMOLED. The mouse plays the finger
 * and the keyboard replaces the board's physical buttons.
 */
#include "lvgl.h"
#include <SDL2/SDL.h>
#include "aos_ui.h"
#include "aos_i18n.h"
#include "aos_apps.h"
#include "aos_hal.h"
#include "aos_watchface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ZOOM    1       /* set to 2 for a window twice as large */

/* implemented in hal_sim.c */
void aos_hal_sim_idle_tick(void);
void aos_hal_sim_set_pose(int pose);
void aos_hal_sim_notificacion(void);            /* key 'n' */
void aos_hal_sim_notificaciones_previas(void);  /* key 'N' */
void aos_hal_sim_rafaga(void);                  /* key 'r' */
int  aos_hal_sim_get_pose(void);
int  aos_hal_effective_brightness(void);
void aos_hal_sim_button(int action);
void aos_hal_sim_set_tilt(float x, float y);

/* --------------------------------------------------------------------------
 * Simulated dimming
 *
 * On the board the brightness is lowered by the AMOLED panel with command
 * 0x51. Here there is no panel, so we put a black veil on top with the
 * corresponding opacity: it is enough to see how the real always-on looks.
 * -------------------------------------------------------------------------- */
static lv_obj_t *s_dim_veil;

static void dim_veil_init(void)
{
    s_dim_veil = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dim_veil);
    lv_obj_set_size(s_dim_veil, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(s_dim_veil, 0, 0);
    lv_obj_set_style_bg_color(s_dim_veil, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dim_veil, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_dim_veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_dim_veil, LV_OBJ_FLAG_SCROLLABLE);
}

static void dim_veil_update(void)
{
    if (!s_dim_veil) {
        return;
    }
    int brightness = aos_hal_effective_brightness();
    int opa = brightness <= 0 ? 255 : (100 - brightness) * 2;
    if (opa > 255) opa = 255;
    if (opa < 0)   opa = 0;
    lv_obj_set_style_bg_opa(s_dim_veil, (lv_opa_t)opa, 0);
}

/* --------------------------------------------------------------------------
 * Preloaded dynamic apps
 *
 * The apps in apps/ are also compiled into the simulator with
 * AOS_SIM_BUILTIN: the AOS_APP_ENTRY macro self-registers them here instead of
 * exporting the .so's symbol. That way the same source that goes onto the
 * microSD is tested on the desktop.
 * -------------------------------------------------------------------------- */

/* Ceiling of apps/ apps the simulator preloads. It was at 12 with 14 apps in
 * the tree: the ones left over were discarded SILENTLY and the new app simply
 * did not appear ("no such app X"), with no error at all. Same problem
 * AOS_MAX_APPS and AOS_MAX_WATCHFACES have in the firmware. */
#define MAX_SIM_APPS    32

static bool (*s_sim_app_inits[MAX_SIM_APPS])(aos_app_t *);
static int    s_sim_app_count;

void aos_sim_register_app(bool (*init)(aos_app_t *app))
{
    if (s_sim_app_count < MAX_SIM_APPS) {
        s_sim_app_inits[s_sim_app_count++] = init;
    } else {
        printf("[sim] WARNING: no room for another app from apps/ (max %d)\n",
               MAX_SIM_APPS);
    }
}

static void sim_register_dynamic_apps(void)
{
    for (int i = 0; i < s_sim_app_count; i++) {
        aos_app_t app;
        memset(&app, 0, sizeof(app));
        if (s_sim_app_inits[i](&app)) {
            aos_ui_register_app(&app);
        }
    }
    if (s_sim_app_count > 0) {
        printf("[sim] %d apps from apps/ preloaded\n", s_sim_app_count);
    }
}

/* --------------------------------------------------------------------------
 * Keyboard
 *
 * On the board the swipe-right gesture is enough to go back, but on the
 * desktop dragging with the mouse is awkward, so we map keys. As a bonus it
 * leaves shortcuts for jumping between apps while designing.
 * -------------------------------------------------------------------------- */

static void open_by_index(int index)
{
    const aos_app_t *app = aos_ui_app_at(index);
    if (app) {
        aos_ui_open(app->desc.id);
    }
}

/* It subscribes to LV_EVENT_PRESSED on the input device and not to
 * LV_EVENT_KEY: the KEY event is emitted on every read cycle while the key is
 * held down, whereas PRESSED arrives once per press. */
static void key_cb(lv_event_t *event)
{
    (void)event;

    lv_indev_t *indev = lv_indev_active();
    uint32_t key = lv_indev_get_key(indev);

    /* 'a' enters and leaves dimmed mode by hand, so it can be designed without
     * waiting the 15 seconds of inactivity. Any other key wakes. */
    if (key == 'a') {
        aos_hal_display_set_state(
            aos_hal_display_state() == AOS_DISPLAY_ACTIVE ? AOS_DISPLAY_AOD
                                                          : AOS_DISPLAY_ACTIVE);
        return;
    }
    aos_hal_activity();

    switch (key) {
    case LV_KEY_ESC:
    case LV_KEY_BACKSPACE:
    case LV_KEY_LEFT:
        aos_ui_back();
        break;

    case LV_KEY_HOME:
    case 'h':
        aos_ui_home();
        break;

    case LV_KEY_UP:
    case 'm':
        aos_ui_show_launcher();
        break;

    case 'w':
        aos_ui_request_watchface_picker();
        break;

    case 'i':
        aos_hal_sim_set_pose(aos_hal_sim_get_pose() + 1);
        break;

    /* 'n' makes a notification arrive from the imaginary phone, and 'N' the
     * flood of those already pending on connecting, which is the case you have
     * to see fail before fixing it: without a filter, plugging the watch in is
     * thirty screens in a row. */
    case 'n':
        aos_hal_sim_notificacion();
        break;

    /* 'b' switches bluetooth on and off, 'B' starts pairing. Both are there so
     * the four states of the bar's icon can be looked at without having to go
     * into Settings, which does not exist yet. */
    case 'b':
        aos_hal_bt_enable(!aos_hal_bt_enabled());
        break;
    case 'B':
        aos_hal_bt_pair_begin();
        break;

    /* 'W' switches WiFi on and off. It exists for the same reason as 'b': so
     * the status bar can be seen with one radio, with both and with neither
     * without having to go into Settings. */
    case 'W':
        aos_hal_net_enable(!aos_hal_net_enabled());
        break;
    case 'N':
        aos_hal_sim_notificaciones_previas();
        break;
    case 'r':
        aos_hal_sim_rafaga();
        break;

    /* 't' rotates through whatever languages are in sim_fs/lang/, going down
     * the same deferred path as the Settings dropdown. Changing language by
     * hand is six taps and it has to be looked at on every screen being
     * checked, so this gets a lot of use during F2 and F4. */
    case 't': {
        aos_lang_t langs[AOS_LANG_MAX];
        int n = aos_i18n_scan(langs, AOS_LANG_MAX);
        if (n < 2) {
            printf("[sim] no packs in sim_fs/lang/\n");
            break;
        }
        int cur = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(langs[i].code, aos_i18n_current()) == 0) {
                cur = i;
            }
        }
        const char *next = langs[(cur + 1) % n].code;
        printf("[sim] language -> %s\n", next);
        aos_ui_request_language(next);
        break;
    }

    case 'l':
        aos_ui_launcher_set_style(AOS_LAUNCHER_LIST);
        aos_ui_show_launcher();
        break;

    case 'g':
        aos_ui_launcher_set_style(AOS_LAUNCHER_GRID);
        aos_ui_show_launcher();
        break;

    case 'p':
        aos_ui_launcher_set_style(AOS_LAUNCHER_HONEYCOMB);
        aos_ui_show_launcher();
        break;

    default:
        if (key >= '1' && key <= '9') {
            open_by_index((int)(key - '1'));
        }
        break;
    }
}

/* --------------------------------------------------------------------------
 * Side button and tilt
 *
 * The board's BOOT button is played by the space bar, with the complete
 * press/release pair: an app using the button as a trigger needs to know how
 * long it was held down, not merely that there was a click.
 *
 * SDL's keyboard events do not travel past LVGL's driver (which keeps only
 * what interests it), so we watch them with an SDL_AddEventWatch. That watch
 * runs inside SDL_PollEvent, that is, inside lv_timer_handler(): we cannot
 * call anything there that deletes LVGL objects. So we note it down and the
 * main loop dispatches it afterwards.
 *
 * The tilt comes from the mouse position, which is the only two-axis
 * continuous input there is on the desktop.
 * -------------------------------------------------------------------------- */

#define SIM_BTN_LONG_MS     800

static int      s_btn_queue[8];
static int      s_btn_queued;
static uint32_t s_btn_down_ms;

static void btn_queue(int action)
{
    if (s_btn_queued < (int)(sizeof(s_btn_queue) / sizeof(s_btn_queue[0]))) {
        s_btn_queue[s_btn_queued++] = action;
    }
}

static int sim_event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;

    if (event->type == SDL_MOUSEMOTION) {
        /* centre of the window = flat; the edges, half a g each way */
        float x = ((float)event->motion.x / (float)(AOS_SCREEN_W * ZOOM)) - 0.5f;
        float y = ((float)event->motion.y / (float)(AOS_SCREEN_H * ZOOM)) - 0.5f;
        aos_hal_sim_set_tilt(x, y);
        return 1;
    }

    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return 1;       /* SDL_Event is a union: only look at 'key' if it is one */
    }
    if (event->key.keysym.sym != SDLK_SPACE) {
        return 1;
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        s_btn_down_ms = SDL_GetTicks();
        btn_queue(0);           /* AOS_BUTTON_PRESS */
    } else if (event->type == SDL_KEYUP) {
        uint32_t held = SDL_GetTicks() - s_btn_down_ms;
        btn_queue(held >= SIM_BTN_LONG_MS ? 2 : 1);     /* LONG : CLICK */
    }
    return 1;
}

static void btn_dispatch(void)
{
    for (int i = 0; i < s_btn_queued; i++) {
        aos_hal_activity();
        aos_hal_sim_button(s_btn_queue[i]);
    }
    s_btn_queued = 0;
}

/* The same dispatch as main/main.c: it is offered to the app first and, if it
 * does not consume it, click = back and long press = clock. Without this the
 * space bar emulates the button but nobody listens to it, and the physical
 * button's path cannot be tested on the Mac. */
static void sim_button_cb(aos_button_t button, aos_button_action_t action)
{
    if (button != AOS_BUTTON_BOOT) {
        return;
    }
    if (aos_ui_button((int)action)) {
        return;
    }
    if (action == AOS_BUTTON_LONG) {
        aos_ui_home();
    } else if (action == AOS_BUTTON_CLICK) {
        aos_ui_back();
    }
}

/* --------------------------------------------------------------------------
 * Scripted keys
 *
 * AOS_SIM_KEYS="esc,m,1" pushes those keys into SDL's queue, one per second.
 * Useful for verifying navigation hands-free and for taking screenshots always
 * in the same state. It goes through LVGL's real keyboard driver, not through
 * an internal shortcut.
 * -------------------------------------------------------------------------- */

static char     s_script[512];
static char    *s_script_cursor;
static uint64_t s_script_next_ms;
static bool     s_script_release_pending;
static SDL_Keycode s_script_key;

static SDL_Keycode keycode_from_name(const char *name)
{
    if (strcmp(name, "esc") == 0)   return SDLK_ESCAPE;
    if (strcmp(name, "home") == 0)  return SDLK_HOME;
    if (strcmp(name, "up") == 0)    return SDLK_UP;
    if (strcmp(name, "down") == 0)  return SDLK_DOWN;
    if (strcmp(name, "left") == 0)  return SDLK_LEFT;
    if (strcmp(name, "right") == 0) return SDLK_RIGHT;
    if (strcmp(name, "back") == 0)  return SDLK_BACKSPACE;
    return (SDL_Keycode)name[0];
}

/* There is only one window; LVGL's driver routes by windowID. */
static uint32_t sdl_window_id(void)
{
    static uint32_t cached;
    if (cached == 0) {
        for (uint32_t id = 1; id < 16; id++) {
            if (SDL_GetWindowFromID(id) != NULL) {
                cached = id;
                break;
            }
        }
    }
    return cached;
}

/* Printable keys (letters and digits) reach LVGL's driver as SDL_TEXTINPUT,
 * not as SDL_KEYDOWN: only control keys travel there. With a real keyboard SDL
 * generates them itself; when injecting them the correct event has to be
 * built. */
static void script_push_text(char c)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_TEXTINPUT;
    event.text.windowID = sdl_window_id();
    event.text.text[0] = c;
    event.text.text[1] = '\0';
    SDL_PushEvent(&event);
}

static void script_push(SDL_Keycode key, bool down)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type            = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state       = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym  = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    SDL_PushEvent(&event);
}

/* Synthetic swipe: LVGL's mouse driver is purely event-driven, so by pushing
 * SDL_MOUSEMOTION we reproduce a real drag. Useful for testing gesture
 * navigation, which is what is used on the board. */
static int s_swipe_left;        /* steps remaining, 0 = no swipe in flight */
static int s_swipe_x, s_swipe_y, s_swipe_dx, s_swipe_dy;

/* Virtual pointer for the script.
 *
 * Pushing SDL events does not work: LVGL's mouse driver routes by windowID and
 * discards anything not coming from the real window. A second pointer-type
 * input device, on the other hand, comes in by the same path as the real mouse
 * (indev_proc_press -> indev_gesture -> LV_EVENT_GESTURE), which is exactly
 * what we want to exercise. */
static lv_indev_t *s_virtual_pointer;
static lv_point_t  s_virtual_point;
static bool        s_virtual_down;

static void virtual_pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = s_virtual_point;
    data->state = s_virtual_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void virtual_pointer_init(lv_display_t *display)
{
    s_virtual_pointer = lv_indev_create();
    lv_indev_set_type(s_virtual_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_virtual_pointer, virtual_pointer_read);
    lv_indev_set_display(s_virtual_pointer, display);
}

static int s_swipe_left;        /* steps remaining, 0 = no swipe in flight */
static int s_swipe_dx, s_swipe_dy;
static bool s_tap_pending;      /* there is a touch waiting to be released */
static uint32_t s_script_gap_ms = 900;

static void swipe_start(const char *dir)
{
    s_swipe_left = 8;

    if (strcmp(dir, "right") == 0) {
        s_virtual_point.x = 40;  s_virtual_point.y = 300; s_swipe_dx = 30;  s_swipe_dy = 0;
    } else if (strcmp(dir, "left") == 0) {
        s_virtual_point.x = 320; s_virtual_point.y = 300; s_swipe_dx = -30; s_swipe_dy = 0;
    } else if (strcmp(dir, "down") == 0) {
        s_virtual_point.x = 184; s_virtual_point.y = 120; s_swipe_dx = 0;   s_swipe_dy = 30;
    } else {    /* up */
        s_virtual_point.x = 184; s_virtual_point.y = 400; s_swipe_dx = 0;   s_swipe_dy = -30;
    }

    s_virtual_down = true;
    printf("[script] swipe %s from (%d,%d)\n", dir,
           (int)s_virtual_point.x, (int)s_virtual_point.y);
}

/* Sustained touch: press and stay. Without this there is no way to test on the
 * desktop anything depending on holding the finger still, which is how 2043
 * opens the pause. Format: hold:184x14:1200 (position and milliseconds). */
static uint64_t s_hold_until;

static void hold_start(const char *arg)
{
    int x = AOS_SCREEN_W / 2, y = AOS_SCREEN_H / 2, ms = 1200;

    if (arg && *arg) {
        sscanf(arg, "%dx%d:%d", &x, &y, &ms);
    }
    s_virtual_point.x = x;
    s_virtual_point.y = y;
    s_virtual_down = true;
    s_hold_until = aos_hal_uptime_ms() + (uint64_t)ms;
    printf("[script] hold at (%d,%d) for %d ms\n", x, y, ms);
}

/* Tilt from the script: tilt:X,Y with both between -0.5 and 0.5, which is the
 * same the mouse gives (the centre of the window is 0,0). Without this there
 * is no way to test hands-free anything driven by moving the board -Remoto's
 * dial, the spirit level- because the script's virtual touch generates no
 * mouse events. It can be asked for in tenths so as not to type decimal
 * points: tilt:-25,0 means -0.25, and tilt:-0.25,0 does too. */
static void tilt_start(const char *arg)
{
    float x = 0.0f, y = 0.0f;
    char  buf[48];
    snprintf(buf, sizeof(buf), "%s", arg ? arg : "");

    char *comma = strchr(buf, 'x');
    if (!comma) {
        comma = strchr(buf, ';');
    }
    if (comma) {
        *comma = '\0';
        y = strtof(comma + 1, NULL);
    }
    x = strtof(buf, NULL);
    if (x > 1.0f || x < -1.0f) x /= 100.0f;
    if (y > 1.0f || y < -1.0f) y /= 100.0f;

    aos_hal_sim_set_tilt(x, y);
    printf("[script] tilt %.3f, %.3f\n", (double)x, (double)y);
}

static void tap_start(const char *arg)
{
    int x = AOS_SCREEN_W / 2;
    int y = AOS_SCREEN_H / 2;
    /* separator 'x' and not a comma: the comma already separates the script's
     * steps */
    if (arg && *arg) {
        sscanf(arg, "%dx%d", &x, &y);
    }
    s_virtual_point.x = x;
    s_virtual_point.y = y;
    s_virtual_down = true;
    s_tap_pending = true;
    printf("[script] tap at (%d,%d)\n", x, y);
}

static void script_tick(uint64_t now_ms)
{
    if (now_ms < s_script_next_ms) {
        return;
    }

    if (s_hold_until) {
        if (now_ms < s_hold_until) {
            /* While the finger is down the script does not advance... except
             * to tilt the board. It is the only way to test hands-free
             * something driven by both at once: Remoto's dial latches on press
             * and then follows the turn of the wrist, and if the tilt had to
             * wait for the release, all that could be tested is the latch. */
            if (s_script_cursor && strncmp(s_script_cursor, "tilt:", 5) == 0) {
                char *comma = strchr(s_script_cursor, ',');
                if (comma) {
                    *comma = '\0';
                }
                tilt_start(s_script_cursor + 5);
                s_script_cursor = comma ? comma + 1 : NULL;
            }
            s_script_next_ms = now_ms + 30;     /* the finger is still down */
            return;
        }
        s_virtual_down = false;
        s_hold_until = 0;
        s_script_next_ms = now_ms + s_script_gap_ms;
        return;
    }

    if (s_tap_pending) {
        s_virtual_down = false;
        s_tap_pending = false;
        s_script_next_ms = now_ms + s_script_gap_ms;
        return;
    }

    /* Mind the order: the swipe and the key release are still in flight after
     * the script's last token has been consumed. */
    if (s_swipe_left > 0) {
        s_virtual_point.x += s_swipe_dx;
        s_virtual_point.y += s_swipe_dy;
        s_swipe_left--;
        if (s_swipe_left == 0) {
            s_virtual_down = false;
            s_script_next_ms = now_ms + s_script_gap_ms;
        } else {
            s_script_next_ms = now_ms + 50;
        }
        return;
    }

    if (s_script_release_pending) {
        script_push(s_script_key, false);
        s_script_release_pending = false;
        s_script_next_ms = now_ms + s_script_gap_ms;
        return;
    }

    if (!s_script_cursor) {
        return;
    }

    char *comma = strchr(s_script_cursor, ',');
    if (comma) {
        *comma = '\0';
    }

    if (strncmp(s_script_cursor, "swipe:", 6) == 0) {
        swipe_start(s_script_cursor + 6);
        s_script_next_ms = now_ms + 50;
    } else if (strncmp(s_script_cursor, "pose:", 5) == 0) {
        aos_hal_sim_set_pose(atoi(s_script_cursor + 5));
        s_script_next_ms = now_ms + 50;
    } else if (strncmp(s_script_cursor, "tilt:", 5) == 0) {
        tilt_start(s_script_cursor + 5);
        s_script_next_ms = now_ms + 50;
    } else if (strncmp(s_script_cursor, "ms:", 3) == 0) {
        s_script_gap_ms = (uint32_t)atoi(s_script_cursor + 3);
        printf("[script] gap between steps: %u ms\n",
               (unsigned)s_script_gap_ms);
        /* besides setting the spacing, it waits: that way "ms:3000,tap" is
         * enough to give something time to connect before the first step */
        s_script_next_ms = now_ms + s_script_gap_ms;
    } else if (strncmp(s_script_cursor, "hold:", 5) == 0) {
        hold_start(s_script_cursor + 5);
        s_script_next_ms = now_ms + 30;
    } else if (strncmp(s_script_cursor, "tap", 3) == 0) {
        tap_start(s_script_cursor[3] == ':' ? s_script_cursor + 4 : NULL);
        s_script_next_ms = now_ms + 60;
    } else if (s_script_cursor[0] != '\0') {
        printf("[script] key '%s'\n", s_script_cursor);
        if (s_script_cursor[1] == '\0' && s_script_cursor[0] > ' ' &&
            s_script_cursor[0] < 127) {
            script_push_text(s_script_cursor[0]);
            s_script_next_ms = now_ms + s_script_gap_ms;
        } else {
            s_script_key = keycode_from_name(s_script_cursor);
            script_push(s_script_key, true);
            s_script_release_pending = true;
            s_script_next_ms = now_ms + 120;
        }
    }

    s_script_cursor = comma ? comma + 1 : NULL;
}

/* --------------------------------------------------------------------------
 * Centring check
 *
 * AOS_SIM_LAYOUT=1 walks the screen and reports, for every scaled text, where
 * it really ends up drawn. Useful for verifying alignment without depending on
 * screenshots.
 *
 * AOS_SIM_AUDIT=1 does the version that is useful for multi-language: it walks
 * the tree and prints ONLY the problems, one line per problem and with a fixed
 * prefix, so it can be run over every app and both languages in one go.
 *
 * It looks for three things, which are the three ways a longer translation
 * breaks a screen:
 *
 *   CORTADO  the natural text does not fit in the label's own box. It is what
 *            happens with long_mode CLIP or DOTS and a fixed width designed
 *            for Spanish.
 *   AFUERA   the label runs off the 368x448. It is "AUTO OFF" running off the
 *            edge in 2043.
 *   DESBORDA the label runs out of its container. It may be intentional -text
 *            centred over a smaller background- so it is reported separately.
 *
 * It does not replace looking at the screen: it sees no overlaps and no
 * ugliness. But it finds without eyes the kind of breakage that matters, and
 * it works just as well for the fourth language as for the second.
 * -------------------------------------------------------------------------- */
static void layout_report(lv_obj_t *obj, int depth)
{
    int32_t scale = lv_obj_get_style_transform_scale_x(obj, LV_PART_MAIN);

    if (lv_obj_check_type(obj, &lv_label_class)) {
        /* get_transformed_area transforms the area you pass it: it has to be
         * seeded with the object's coordinates */
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        lv_obj_get_transformed_area(obj, &area, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
        int32_t cx = (area.x1 + area.x2) / 2;
        int32_t cy = (area.y1 + area.y2) / 2;
        printf("  %*s\"%s\"  scale %ld  box x %ld..%ld  centre x=%ld "
               "(screen %d, offset %+ld)\n",
               depth * 2, "", lv_label_get_text(obj), (long)scale,
               (long)area.x1, (long)area.x2, (long)cx,
               AOS_SCREEN_W / 2, (long)(cx - AOS_SCREEN_W / 2));
        (void)cy;
    }

    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++) {
        layout_report(lv_obj_get_child(obj, i), depth + 1);
    }
}

static int s_audit_hits;

/* A page that scrolls has content off screen by design: that is not an
 * overflow. The width is: nothing in this UI scrolls horizontally. */
static bool ancestro_scrollea(lv_obj_t *obj)
{
    for (lv_obj_t *p = lv_obj_get_parent(obj); p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE)) {
            return true;
        }
    }
    return false;
}

/* A button hardly ever has text of its own: its child label has it. Without
 * this the INTOCABLE line would only say "an object at y 398..442" and you
 * would have to go and count children to know which. */
static const char *audit_nombre(lv_obj_t *obj)
{
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *t = lv_label_get_text(obj);
        if (t && *t) {
            return t;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *h = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(h, &lv_label_class)) {
            const char *t = lv_label_get_text(h);
            if (t && *t) {
                return t;
            }
        }
    }
    return "(no text)";
}

static void audit_obj(lv_obj_t *obj, const char *tag)
{
    /* What is hidden is not audited: a tab that is not on screen has its
     * objects wherever and bothers nobody. Without this, the tuner reported a
     * text at y=478 on a 448 screen that was really in the noise tab, hidden. */
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    /* INTOCABLE: clickable ending below where the touch panel reaches.
     *
     * This board's CST816 reports nothing below y = 395 even though the panel
     * draws down to 448 (the full measurement is in aos_hal.h, next to
     * AOS_TOUCH_Y_MAX). A button down there gives no error: it simply does not
     * respond, and on top of that it looks perfect in the simulator, where the
     * mouse reaches everywhere. That is why the check lives in the audit and
     * not in the eyes: it is exactly the kind of bug a screenshot does not
     * show.
     *
     * The object entirely below the limit is reported (dead) and so is the one
     * crossing it (a usable strip is left at the top), because the second case
     * is the more deceptive: it works "sometimes" and you blame your finger. */
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        int32_t vivos = AOS_TOUCH_Y_MAX - a.y1 + 1;   /* usable px of the object */

        /* Anything hanging off something that scrolls is NOT reported: its
         * coordinates are those of this instant and a finger moves them up.
         * Without this filter, Settings turned up whole -fourteen buttons, one
         * of them at y=1366- and the noise buried the real findings. */
        /* It starts from the PARENT and not from the object: a button made
         * with lv_obj_create() is born with LV_OBJ_FLAG_SCROLLABLE set even
         * though it scrolls nothing, so by looking at itself it excluded
         * itself. With that version of the filter Vida's five buttons -which
         * are dead- did not show up, and Nivel's did, only because they use
         * aos_button(). */
        bool scrollea = false;
        for (lv_obj_t *p = lv_obj_get_parent(obj); p; p = lv_obj_get_parent(p)) {
            if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE)) {
                scrollea = true;
                break;
            }
        }

        /* Two ways of being unreachable, and for a while only the first was
         * checked.
         *
         *   1. Almost nothing is left. Under 20 px of live strip there is no
         *      target at all, whatever the object's size.
         *
         *   2. THE CENTRE IS DEAD. A finger goes to the middle of a control,
         *      not to its top edge, so a button whose centre falls below the
         *      limit does not work in the hand even if a sliver of it is
         *      technically live.
         *
         * The second one was missing and it cost a real bug: the watchface
         * picker's buttons span y 369..415, which leaves vivos = 22 and passed
         * the "< 20" test by two pixels, while their centre sat at 392 -below
         * the limit-. On the board the picker opened and then would not
         * respond. See docs/internal/HANDOFF-PUBLICACION.md, section 3.
         *
         * BUT a dead centre is NOT breakage, and that had to be learnt from the
         * board rather than from the arithmetic. Measured on the four controls
         * that sit low today:
         *
         *     picker's button   46 px tall, 22 live (47%), centre 392
         *     alarm's Nueva     60 px tall, 23 live (38%), centre 397
         *     convert's 0 , +/- 54 px tall, 25 live (46%), centre 392
         *     dice's TIRAR      62 px tall, 27 live (43%), centre 394
         *
         * They are the same case geometrically, and yet three of them are used
         * every day without trouble. What made the picker feel broken was not
         * its shape: it was that the touch calibration had been wiped at the
         * same time, so every press landed offset as well. With cal_* in place
         * a live strip of 40% is plenty of target.
         *
         * So this half is reported as LOWEDGE and does NOT count as a problem:
         * it is the list of what to look at on the board if something feels
         * unresponsive, not a list of defects. UNTOUCHABLE stays for what has
         * almost nothing left, which is breakage in any hand. */
        int32_t centro = (a.y1 + a.y2) / 2;

        /* A finger in a dead band lands on a known row (AOS_TOUCH_LAND_*,
         * see aos_hal.h): a control that contains that row is reachable from
         * the whole band, however little of it lies inside the window. */
        bool lands_bottom = a.y1 <= AOS_TOUCH_LAND_BOTTOM && a.y2 >= AOS_TOUCH_LAND_BOTTOM;
        bool lands_top    = a.y1 <= AOS_TOUCH_LAND_TOP    && a.y2 >= AOS_TOUCH_LAND_TOP;

        if (!scrollea && a.y2 > AOS_TOUCH_Y_MAX && !lands_bottom) {
            if (vivos < 20) {
                printf("AUDIT UNTOUCHABLE %s | %s | y %ld..%ld (centre %ld, %ld px live) | %s\n",
                       tag, audit_nombre(obj), (long)a.y1, (long)a.y2,
                       (long)centro, (long)vivos,
                       vivos <= 0 ? "DEAD" : "only a few px left");
                s_audit_hits++;
            } else if (centro > AOS_TOUCH_Y_MAX && vivos < 40) {
                printf("AUDIT LOWEDGE %s | %s | y %ld..%ld (centre %ld, %ld px live)\n",
                       tag, audit_nombre(obj), (long)a.y1, (long)a.y2,
                       (long)centro, (long)vivos);
            }
        }

        /* And the TOP, measured on 2026-09-11 (AOS_TOUCH_Y_MIN): the panel
         * reports nothing above y = 55. Bars of buttons at y = 8..48 had
         * "worked" for weeks only because an uncalibrated panel reported the
         * touch 50 px too high; once calibrated, every one of them was dead.
         * Same rule mirrored: what has almost nothing left below the line is
         * broken, a wide object that merely crosses it is HIGHEDGE. */
        int32_t vivos_top = a.y2 - AOS_TOUCH_Y_MIN + 1;
        if (!scrollea && a.y1 < AOS_TOUCH_Y_MIN && !lands_top) {
            if (vivos_top < 20) {
                printf("AUDIT UNTOUCHABLE %s | %s | y %ld..%ld (centre %ld, %ld px live) | %s\n",
                       tag, audit_nombre(obj), (long)a.y1, (long)a.y2,
                       (long)centro, (long)vivos_top,
                       vivos_top <= 0 ? "DEAD, above the touch panel" : "only a few px left at the top");
                s_audit_hits++;
            } else if (centro < AOS_TOUCH_Y_MIN && vivos_top < 40) {
                printf("AUDIT HIGHEDGE %s | %s | y %ld..%ld (centre %ld, %ld px live)\n",
                       tag, audit_nombre(obj), (long)a.y1, (long)a.y2,
                       (long)centro, (long)vivos_top);
            }
        }
    }

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        if (txt && *txt) {
            lv_area_t a;
            lv_obj_get_coords(obj, &a);
            int32_t w = lv_area_get_width(&a);
            int32_t h = lv_area_get_height(&a);

            /* How large the loose text is, with the font and letter spacing
             * this label has set. */
            const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
            int32_t ls = lv_obj_get_style_text_letter_space(obj, LV_PART_MAIN);
            int32_t lsp = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
            lv_label_long_mode_t lm = lv_label_get_long_mode(obj);
            int32_t maxw = (lm == LV_LABEL_LONG_MODE_WRAP) ? w : LV_COORD_MAX;

            lv_point_t nat;
            lv_text_get_size(&nat, txt, font, ls, lsp, maxw, LV_TEXT_FLAG_NONE);

            /* No "fixed width" filter.
             *
             * It had one, looking at lv_obj_get_style_width() !=
             * LV_SIZE_CONTENT, and it left out precisely the labels the PARENT
             * sizes: the ones in the app menu were clipped to "Taschenla..."
             * in German and this said not a word, because their style still
             * says SIZE_CONTENT.
             *
             * It is not needed: a content-sized label has a width EQUAL to the
             * natural one, so the comparison below is never true for it. The
             * filter only covered up real cases.
             *
             * Thresholds, not equalities. lv_text_get_size() returns the full
             * line height -descender included- and comes out some 3 px more
             * than the box even when the text fits perfectly: against that,
             * +4. In width +2 is enough, and width is what a translation
             * breaks. */
            /* LVGL with LONG_DOT OVERWRITES the label's own text: from then on
             * lv_label_get_text() returns "Taschenla..." and not
             * "Taschenlampe". Which means the clipping cannot be MEASURED,
             * only inferred: if the mode is the dotted one, the text ends in
             * "..." and its width reaches almost to the edge of the box, then
             * it was clipped.
             *
             * The closeness to the edge is what separates this from a string
             * that ends in dots in its own right -"Buscando...",
             * "verbinde..."-, which in a roomy box measures rather less. */
            if (lm == LV_LABEL_LONG_MODE_DOTS) {
                size_t n = strlen(txt);
                if (n >= 3 && strcmp(txt + n - 3, "...") == 0 && nat.x > w - 12) {
                    printf("AUDIT ELLIPSIS %s | \"%s\" | box %ldx%ld\n",
                           tag, txt, (long)w, (long)h);
                }
            }

            if (lm != LV_LABEL_LONG_MODE_SCROLL &&
                lm != LV_LABEL_LONG_MODE_SCROLL_CIRCULAR &&
                (nat.x > w + 2 || nat.y > h + 4)) {
                /* DOTS is a declaration by whoever wrote the screen: "I know
                 * it may not fit, clip it". That is not breakage and cannot
                 * count as such, but it IS worth seeing: it says which strings
                 * have to be shortened in that language to be understood. That
                 * is why it comes out under a different name and does not add
                 * to the total. */
                printf("AUDIT %s %s | \"%s\" | box %ldx%ld text %ldx%ld\n",
                       lm == LV_LABEL_LONG_MODE_DOTS ? "ELLIPSIS" : "CLIPPED ",
                       tag, txt, (long)w, (long)h, (long)nat.x, (long)nat.y);
                if (lm != LV_LABEL_LONG_MODE_DOTS) {
                    s_audit_hits++;
                }
            }

            lv_area_t t = a;
            lv_obj_get_transformed_area(obj, &t, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
            bool scroll = ancestro_scrollea(obj);
            /* LVGL's areas are inclusive, but get_transformed_area adds a
             * pixel of margin: a full-width label reports 0..368 and not
             * 0..367. Hence ">" and not ">=". */
            bool fuera_x = (t.x1 < 0 || t.x2 > AOS_SCREEN_W);
            bool fuera_y = !scroll && (t.y1 < 0 || t.y2 > AOS_SCREEN_H);
            if (fuera_x || fuera_y) {
                printf("AUDIT OFFSCREEN %s | \"%s\" | x %ld..%ld y %ld..%ld\n",
                       tag, txt, (long)t.x1, (long)t.x2, (long)t.y1, (long)t.y2);
                s_audit_hits++;
            }

            lv_obj_t *par = lv_obj_get_parent(obj);
            if (par && !lv_obj_check_type(par, &lv_label_class)) {
                lv_area_t pa;
                lv_obj_get_coords(par, &pa);
                /* Width AND height. Looking only at the width let through a
                 * calendar label that, not fitting lengthwise, wrapped onto
                 * two lines and spilled below its header: horizontally it fit
                 * perfectly. A screenshot found it, not this. */
                bool ancho = (a.x1 < pa.x1 - 1 || a.x2 > pa.x2 + 1);
                /* The height only counts if nothing scrolls, the same as in
                 * AFUERA: on a page that scrolls ALL the content below is
                 * outside the parent by design. */
                bool alto  = !scroll && (a.y1 < pa.y1 - 1 || a.y2 > pa.y2 + 1);
                if (lv_area_get_width(&pa) > 0 && (ancho || alto)) {
                    printf("AUDIT OVERFLOW %s | \"%s\" | label %ld..%ld/%ld..%ld parent %ld..%ld/%ld..%ld\n",
                           tag, txt, (long)a.x1, (long)a.x2, (long)a.y1, (long)a.y2,
                           (long)pa.x1, (long)pa.x2, (long)pa.y1, (long)pa.y2);
                    s_audit_hits++;
                }
            }
        }
    }

    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++) {
        audit_obj(lv_obj_get_child(obj, i), tag);
    }
}

static void audit_run(const char *tag)
{
    s_audit_hits = 0;
    audit_obj(lv_screen_active(), tag);
    printf("AUDIT END %s | %d problems\n", tag, s_audit_hits);
}

static void print_help(void)
{
    printf("\n  AmoledOS - simulator\n"
           "  ---------------------------------------------------------\n"
           "  mouse: drag up = menu, drag right = back\n"
           "  ESC / backspace / left arrow ....... back\n"
           "  H / Home key ....................... watch\n"
           "  M / up arrow ....................... app menu\n"
           "  L / G / P .......................... list/grid/honeycomb menu\n"
           "  1..9 ............................... open app N\n"
           "  W .................................. change watchface\n"
           "  T .................................. cycle language\n"
           "  A .................................. dimmed mode (always-on)\n"
           "  I .................................. board posture (IMU)\n"
           "  SPACE .............................. side button (BOOT)\n"
           "  mouse .............................. tilt (accelerometer)\n"
           "  ---------------------------------------------------------\n\n");
}

int main(void)
{
    /* line by line: if you redirect the output to a file, the logs appear
     * straight away and not when the buffer fills */
    setvbuf(stdout, NULL, _IOLBF, 0);

    lv_init();

    lv_display_t *display = lv_sdl_window_create(AOS_SCREEN_W * ZOOM,
                                                 AOS_SCREEN_H * ZOOM);
    lv_sdl_window_set_zoom(display, ZOOM);
    lv_sdl_window_set_title(display, "AmoledOS - ESP32-S3-Touch-AMOLED-1.8");

    SDL_AddEventWatch(sim_event_watch, NULL);

    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, display);

    /* With no group attached: we do not want the keyboard moving focus
     * between widgets, only telling us which key was pressed. */
    lv_indev_t *keyboard = lv_sdl_keyboard_create();
    lv_indev_set_display(keyboard, display);
    lv_indev_add_event_cb(keyboard, key_cb, LV_EVENT_PRESSED, NULL);

    /* The virtual pointer has to exist before aos_ui_init(), which is what
     * hooks the gesture handler onto each input device. */
    /* AOS_SIM_BT=3 fires a pairing request at startup, so
     * tools/audit_layout.sh can look at that overlay: it hangs off
     * lv_layer_top and appears only when the phone asks, so there is no key
     * script that reaches it reproducibly. Values 1 and 2 are used by
     * Settings. */
    {
        const char *sim_bt = getenv("AOS_SIM_BT");
        if (sim_bt && sim_bt[0] == '3') {
            aos_hal_bt_pair_begin();
        }
    }

    if (getenv("AOS_SIM_KEYS")) {
        virtual_pointer_init(display);
    }

    aos_hal_init();
    aos_hal_set_button_cb(sim_button_cb);
    aos_ui_init();
    dim_veil_init();
    aos_apps_register_builtin();
    sim_register_dynamic_apps();

    /* AOS_SIM_VIEW=launcher|grid|honeycomb|<app.id> starts straight on that
     * screen, to iterate on the design without having to navigate every
     * time. */
    const char *view = getenv("AOS_SIM_VIEW");
    if (view) {
        if (strcmp(view, "launcher") == 0) {
            aos_ui_show_launcher();
        } else if (strcmp(view, "grid") == 0) {
            aos_ui_launcher_set_style(AOS_LAUNCHER_GRID);
            aos_ui_show_launcher();
        } else if (strcmp(view, "honeycomb") == 0) {
            aos_ui_launcher_set_style(AOS_LAUNCHER_HONEYCOMB);
            aos_ui_show_launcher();
        } else {
            aos_ui_open(view);
        }
    }

    const char *script = getenv("AOS_SIM_KEYS");
    if (script) {
        /* Truncating silently costs a good while: the script runs half way,
         * the last step comes out split ("key 'swip'") and you start looking
         * for the bug in the app. */
        if (strlen(script) >= sizeof(s_script)) {
            printf("[script] WARNING: the script is %zu characters and the "
                   "maximum is %zu; it gets cut\n",
                   strlen(script), sizeof(s_script) - 1);
        }
        snprintf(s_script, sizeof(s_script), "%s", script);
        s_script_cursor  = s_script;
        s_script_next_ms = 1200;    /* give the screen time to draw */
    }

    /* AOS_SIM_POS="x,y" pins the window at a known place; useful for taking
     * screenshots always of the same crop. Either way we print where it ended
     * up, which is what screencapture -R needs. */
    SDL_Window *window = SDL_GetWindowFromID(sdl_window_id());
    if (window) {
        const char *pos = getenv("AOS_SIM_POS");
        int x = 0, y = 0;
        if (pos && sscanf(pos, "%d,%d", &x, &y) == 2) {
            SDL_SetWindowPosition(window, x, y);
        }
        SDL_GetWindowPosition(window, &x, &y);
        printf("[sim] window at %d,%d  (screencapture -R %d,%d,%d,%d)\n",
               x, y, x, y, AOS_SCREEN_W * ZOOM, AOS_SCREEN_H * ZOOM);
    }

    print_help();

    /* AOS_SIM_LAYOUT=1 reports at 1500 ms; with a larger number it can be
     * looked at after the script has done its work. */
    const char *layout_env = getenv("AOS_SIM_LAYOUT");
    uint64_t layout_at = 0;
    if (layout_env) {
        long value = atol(layout_env);
        layout_at = (value > 100) ? (uint64_t)value : 1500;
    }

    /* AOS_SIM_AUDIT=<label> audits the layout at 2 s and exits. The label goes
     * on every line so the output of many runs can be collected together.
     * AOS_SIM_AUDIT_MS changes the wait if the screen takes a while to
     * assemble. */
    const char *audit_env = getenv("AOS_SIM_AUDIT");
    uint64_t audit_at = 0;
    if (audit_env && *audit_env) {
        const char *ms = getenv("AOS_SIM_AUDIT_MS");
        long value = ms ? atol(ms) : 0;
        audit_at = (value > 100) ? (uint64_t)value : 2000;
    }

    uint64_t last_tick = 0;
    while (1) {
        uint32_t idle = lv_timer_handler();
        if (idle > 10) {
            idle = 10;
        }
        usleep(idle * 1000);

        btn_dispatch();

        uint64_t now = aos_hal_uptime_ms();
        script_tick(now);

        if (layout_at && now >= layout_at) {
            layout_at = 0;
            printf("[layout] on-screen texts:\n");
            layout_report(lv_screen_active(), 0);
        }

        if (audit_at && now >= audit_at) {
            /* With an animation in flight, the WHOLE screen is shifted: on
             * opening an app its root starts at x=368 and slides to 0. If the
             * audit looks right then, every text of that app is reported as
             * AFUERA by exactly one screen width, and the report accuses a
             * regression that does not exist -and a different app on each run,
             * which is what made it hard to believe-.
             *
             * It waits until no animation is left alive, with a cap in case
             * some screen animates forever. */
            if (lv_anim_count_running() > 0 && now < audit_at + 3000) {
                continue;
            }
            audit_run(audit_env);
            fflush(stdout);
            return 0;
        }

        if (now - last_tick >= 200) {
            last_tick = now;
            aos_hal_sim_idle_tick();
            aos_ui_tick();
            aos_alarm_service_tick();
            dim_veil_update();
        }
    }
    return 0;
}
