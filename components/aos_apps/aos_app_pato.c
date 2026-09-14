/*
 * AmoledOS - Pato goma: the watch runs little keyboard-and-mouse scripts on
 * the computer it is plugged into (USB, docs/USB.md). A "rubber ducky" whose
 * payloads the owner writes, picks and confirms on the wrist -nothing runs
 * without a tap on the confirmation screen- so it is automation, not an
 * attack: the script is only ever what the user typed into the portal.
 *
 * DISCLAIMER. This app exists only to educate and to demonstrate the HID
 * capabilities of the ESP32-S3 in AmoledOS. We take no responsibility for the
 * scripts third parties run with it, nor for any misuse they may put it to.
 *
 * The scripts live on the card, one text file per script in /sdcard/pato,
 * written from the /pato page of the portal (the firmware only stores bytes,
 * like Pixel Art). This app lists them, shows a confirmation with a preview,
 * and on "Ejecutar" plays the steps out over USB HID.
 *
 * The script language (same set the portal builds and this parser reads):
 *
 *     # a comment, ignored
 *     STRING texto        types the rest of the line
 *     KEY nombre          a key or a combo: enter, esc, cmd+space, f5, up...
 *     DELAY ms            waits
 *     MOUSE dx dy         moves the pointer (relative)
 *     SCROLL n            the wheel (+ up, - down)
 *     CLICK [1|2]         click, 1 left (default) / 2 right
 *     REPEAT n            repeats the previous line n more times
 *
 * Why it plays out from an lv_timer one action at a time: every key and every
 * typed character blocks the HAL ~50 ms (docs/HANDOFF-USB.md), so running a
 * whole script inside one callback would freeze the screen for seconds and
 * the "Parar" button with it. One character / one key per tick keeps each
 * callback short, the progress bar moving and Parar alive. A big MOUSE move
 * is split at parse time into chunks of <=120 (a report clamps at +-127).
 *
 * KEYS mode is required (the port has to be the keyboard). If it is not, the
 * list shows a button to switch it, the same one Control PC offers.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define PATO_MAX_SCRIPTS   64      /* how many files the list shows            */
#define PATO_NAME_MAX      48      /* a script's file name, without .pato      */
#define PATO_MAX_STEPS     800     /* after REPEAT expansion                   */
#define PATO_MAX_FILE      16384   /* a script file we will read               */
#define PATO_MOUSE_CHUNK   120     /* a report clamps at +-127                 */
#define PATO_TICK_MS       10      /* the runner's period                      */

typedef enum {
    ST_STRING, ST_KEY, ST_DELAY, ST_MOUSE, ST_SCROLL, ST_CLICK,
} step_kind_t;

typedef struct {
    step_kind_t kind;
    char       *text;   /* STRING text or KEY name, malloc'd; NULL otherwise */
    int         a, b;   /* DELAY ms=a; MOUSE dx=a,dy=b; SCROLL n=a; CLICK btn=a */
} pato_step_t;

typedef struct {
    /* the list face */
    lv_obj_t   *list_box;
    lv_obj_t   *list;
    lv_obj_t   *status;         /* the USB line at the top of the list        */
    lv_obj_t   *switch_btn;     /* "Pasar a Teclado", shown when not ready     */
    lv_timer_t *poll;           /* refreshes status / switch button           */
    /* the confirm face */
    lv_obj_t   *confirm_box;
    lv_obj_t   *cf_title;
    lv_obj_t   *cf_meta;
    lv_obj_t   *cf_preview;
    lv_obj_t   *cf_run_btn;
    /* the run face */
    lv_obj_t   *run_box;
    lv_obj_t   *run_title;
    lv_obj_t   *run_bar;
    lv_obj_t   *run_step;
    lv_obj_t   *run_btn;        /* "Parar" during a run, "Volver" when done    */
    lv_timer_t *run_timer;
    /* the loaded program. The step array is malloc'd (PSRAM) rather than a
     * static, so this app costs the firmware's scarce internal RAM nothing
     * while it is closed -the same rule the big buffers of every other app
     * follow. */
    pato_step_t *steps;
    int          nsteps;
    char        loaded[PATO_NAME_MAX];
    /* the runner's cursor */
    int         cur;
    int         char_pos;       /* into a STRING step                          */
    bool        waiting;
    uint32_t    wait_until;
    bool        done;
    /* auto-refresh of the list: the poll timer re-reads the folder now and
     * then and rebuilds the list only when it changed (a script saved from the
     * portal, say), so it appears without leaving and re-entering the app. */
    uint32_t    list_sig;
    int         list_tick;
} pato_t;

static pato_t s_pato;

static char s_dir[128];         /* /sdcard/pato, built in create()             */

/* -------------------------------------------------------------------------- */
/* The program                                                                 */
/* -------------------------------------------------------------------------- */

static void prog_free(void)
{
    for (int i = 0; i < s_pato.nsteps; i++) {
        free(s_pato.steps[i].text);
        s_pato.steps[i].text = NULL;
    }
    s_pato.nsteps = 0;
}

static bool step_add(step_kind_t kind, const char *text, int a, int b)
{
    if (!s_pato.steps || s_pato.nsteps >= PATO_MAX_STEPS) {
        return false;
    }
    pato_step_t *st = &s_pato.steps[s_pato.nsteps];
    st->kind = kind;
    st->a = a;
    st->b = b;
    st->text = NULL;
    if (text) {
        st->text = malloc(strlen(text) + 1);
        if (!st->text) {
            return false;
        }
        strcpy(st->text, text);
    }
    s_pato.nsteps++;
    return true;
}

/* A MOUSE move larger than a report can carry becomes several steps, so the
 * runner never has to loop inside one tick. */
static bool step_add_mouse(int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return step_add(ST_MOUSE, NULL, 0, 0);
    }
    while (dx != 0 || dy != 0) {
        int cx = dx >  PATO_MOUSE_CHUNK ?  PATO_MOUSE_CHUNK
               : dx < -PATO_MOUSE_CHUNK ? -PATO_MOUSE_CHUNK : dx;
        int cy = dy >  PATO_MOUSE_CHUNK ?  PATO_MOUSE_CHUNK
               : dy < -PATO_MOUSE_CHUNK ? -PATO_MOUSE_CHUNK : dy;
        if (!step_add(ST_MOUSE, NULL, cx, cy)) {
            return false;
        }
        dx -= cx;
        dy -= cy;
    }
    return true;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

/* Duplicate the previous parsed step n times (DuckyScript's REPEAT). A STRING
 * is duplicated whole; the runner types it out char by char each time. */
static bool prog_repeat(int n)
{
    if (s_pato.nsteps == 0 || n <= 0) {
        return true;
    }
    pato_step_t src = s_pato.steps[s_pato.nsteps - 1];
    for (int i = 0; i < n; i++) {
        if (!step_add(src.kind, src.text, src.a, src.b)) {
            return false;
        }
    }
    return true;
}

/* Parse the whole text (modified in place) into s_pato.steps. Unknown lines
 * are skipped, so a typo loses a line, never the script. */
static void prog_parse(char *text)
{
    prog_free();
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = trim(line);
        if (s[0] == '\0' || s[0] == '#') {
            continue;
        }
        /* the verb is the first word */
        char *arg = s;
        while (*arg && *arg != ' ' && *arg != '\t') {
            arg++;
        }
        char verb[12];
        size_t vl = (size_t)(arg - s);
        if (vl >= sizeof(verb)) {
            continue;
        }
        for (size_t i = 0; i < vl; i++) {
            verb[i] = (char)toupper((unsigned char)s[i]);
        }
        verb[vl] = '\0';
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }

        if (!strcmp(verb, "STRING") || !strcmp(verb, "TYPE")) {
            if (*arg) {
                step_add(ST_STRING, arg, 0, 0);
            }
        } else if (!strcmp(verb, "KEY") || !strcmp(verb, "PRESS")) {
            if (*arg) {
                step_add(ST_KEY, arg, 0, 0);
            }
        } else if (!strcmp(verb, "DELAY") || !strcmp(verb, "WAIT")) {
            int ms = atoi(arg);
            if (ms < 0) ms = 0;
            if (ms > 60000) ms = 60000;
            step_add(ST_DELAY, NULL, ms, 0);
        } else if (!strcmp(verb, "MOUSE") || !strcmp(verb, "MOVE")) {
            int dx = 0, dy = 0;
            sscanf(arg, "%d %d", &dx, &dy);
            step_add_mouse(dx, dy);
        } else if (!strcmp(verb, "SCROLL")) {
            int n = atoi(arg);
            if (n >  127) n =  127;
            if (n < -127) n = -127;
            step_add(ST_SCROLL, NULL, n, 0);
        } else if (!strcmp(verb, "CLICK")) {
            int b = atoi(arg);
            step_add(ST_CLICK, NULL, (b == 2) ? 2 : 1, 0);
        } else if (!strcmp(verb, "REPEAT")) {
            prog_repeat(atoi(arg));
        }
        if (s_pato.nsteps >= PATO_MAX_STEPS) {
            break;
        }
    }
}

/* Read a script file whole, into a caller buffer, NUL-terminated. */
static bool read_file(const char *name, char *buf, size_t cap)
{
    char path[192];
    snprintf(path, sizeof(path), "%s/%s.pato", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return true;
}

/* -------------------------------------------------------------------------- */
/* Faces                                                                       */
/* -------------------------------------------------------------------------- */

static void show_face(lv_obj_t *which)
{
    lv_obj_add_flag(s_pato.list_box,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pato.confirm_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pato.run_box,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(which, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* The runner                                                                  */
/* -------------------------------------------------------------------------- */

static const char *step_caption(const pato_step_t *st)
{
    static char buf[48];
    switch (st->kind) {
    case ST_STRING: snprintf(buf, sizeof(buf), "%s", _("Escribiendo")); break;
    case ST_KEY:    snprintf(buf, sizeof(buf), "%s %s", _("Tecla"), st->text); break;
    case ST_DELAY:  snprintf(buf, sizeof(buf), "%s %d ms", _("Espera"), st->a); break;
    case ST_MOUSE:  snprintf(buf, sizeof(buf), "%s", _("Mouse")); break;
    case ST_SCROLL: snprintf(buf, sizeof(buf), "%s", _("Rueda")); break;
    case ST_CLICK:  snprintf(buf, sizeof(buf), "%s", _("Clic")); break;
    default:        buf[0] = '\0'; break;
    }
    return buf;
}

static void run_stop(bool finished)
{
    if (s_pato.run_timer) {
        lv_timer_delete(s_pato.run_timer);
        s_pato.run_timer = NULL;
    }
    s_pato.done = finished;
    if (finished) {
        lv_bar_set_value(s_pato.run_bar, 100, LV_ANIM_OFF);
        lv_label_set_text(s_pato.run_step, _("Listo"));
        lv_label_set_text(lv_obj_get_child(s_pato.run_btn, 0), _("Volver"));
        lv_obj_set_style_bg_color(s_pato.run_btn, AOS_C_CARD2, 0);
        aos_hal_beep(1600, 20);
    }
}

static void run_tick(lv_timer_t *timer)
{
    (void)timer;

    if (!aos_hal_usb_keys_ready()) {
        /* the computer let go of the keyboard mid-run: stop and say so */
        run_stop(false);
        show_face(s_pato.list_box);
        aos_ui_toast(_("Se perdio el teclado USB"), 1600);
        return;
    }
    if (s_pato.cur >= s_pato.nsteps) {
        run_stop(true);
        return;
    }

    pato_step_t *st = &s_pato.steps[s_pato.cur];
    bool advance = true;

    switch (st->kind) {
    case ST_STRING: {
        int len = (int)strlen(st->text);
        if (s_pato.char_pos < len) {
            char one[2] = { st->text[s_pato.char_pos], '\0' };
            aos_hal_usb_type(one);
            s_pato.char_pos++;
        }
        advance = (s_pato.char_pos >= len);
        break;
    }
    case ST_KEY:
        aos_hal_usb_key(st->text);
        break;
    case ST_DELAY:
        if (!s_pato.waiting) {
            s_pato.waiting = true;
            s_pato.wait_until = (uint32_t)aos_hal_uptime_ms() + (uint32_t)st->a;
            advance = false;
        } else if ((int32_t)((uint32_t)aos_hal_uptime_ms() - s_pato.wait_until) >= 0) {
            s_pato.waiting = false;
        } else {
            advance = false;
        }
        break;
    case ST_MOUSE:
        aos_hal_usb_mouse(st->a, st->b, 0);
        break;
    case ST_SCROLL:
        aos_hal_usb_mouse(0, 0, st->a);
        break;
    case ST_CLICK:
        aos_hal_usb_click(st->a);
        break;
    }

    if (advance) {
        s_pato.cur++;
        s_pato.char_pos = 0;
        lv_label_set_text(s_pato.run_step,
                          s_pato.cur < s_pato.nsteps
                              ? step_caption(&s_pato.steps[s_pato.cur]) : "");
    }
    int pct = s_pato.nsteps ? s_pato.cur * 100 / s_pato.nsteps : 100;
    lv_bar_set_value(s_pato.run_bar, pct, LV_ANIM_OFF);
}

static void run_start(void)
{
    s_pato.cur = 0;
    s_pato.char_pos = 0;
    s_pato.waiting = false;
    s_pato.done = false;

    lv_label_set_text(s_pato.run_title, s_pato.loaded);
    lv_bar_set_value(s_pato.run_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_pato.run_step,
                      s_pato.nsteps ? step_caption(&s_pato.steps[0]) : "");
    lv_label_set_text(lv_obj_get_child(s_pato.run_btn, 0), _("Parar"));
    lv_obj_set_style_bg_color(s_pato.run_btn, AOS_C_RED, 0);
    show_face(s_pato.run_box);

    if (!s_pato.run_timer) {
        s_pato.run_timer = lv_timer_create(run_tick, PATO_TICK_MS, NULL);
    }
}

/* -------------------------------------------------------------------------- */
/* Callbacks                                                                   */
/* -------------------------------------------------------------------------- */

static void run_btn_cb(lv_event_t *event)
{
    (void)event;
    if (s_pato.done) {
        show_face(s_pato.list_box);      /* "Volver" */
        return;
    }
    run_stop(false);                     /* "Parar" */
    show_face(s_pato.list_box);
    aos_ui_toast(_("Detenido"), 1200);
}

static void confirm_run_cb(lv_event_t *event)
{
    (void)event;
    if (!aos_hal_usb_keys_ready()) {
        aos_ui_toast(_("Pasa el USB a Teclado"), 1600);
        return;
    }
    if (s_pato.nsteps == 0) {
        aos_ui_toast(_("El script esta vacio"), 1400);
        return;
    }
    run_start();
}

static void confirm_cancel_cb(lv_event_t *event)
{
    (void)event;
    show_face(s_pato.list_box);
}

/* A script in the list was tapped: load it, parse it, fill the confirmation. */
static void pick_cb(lv_event_t *event)
{
    const char *name = lv_event_get_user_data(event);

    char *raw = malloc(PATO_MAX_FILE);      /* PSRAM; freed before returning */
    if (!raw) {
        return;
    }
    if (!read_file(name, raw, PATO_MAX_FILE)) {
        free(raw);
        aos_ui_toast(_("No se pudo leer el script"), 1600);
        return;
    }
    snprintf(s_pato.loaded, sizeof(s_pato.loaded), "%s", name);

    /* the preview shows the file as written; keep it before prog_parse chews
     * the buffer up with strtok. A bounded copy, not snprintf("%s"): the
     * source can be far bigger than the preview and GCC rejects that. */
    static char preview[420];
    size_t plen = strlen(raw);
    if (plen >= sizeof(preview)) {
        plen = sizeof(preview) - 1;
    }
    memcpy(preview, raw, plen);
    preview[plen] = '\0';

    prog_parse(raw);

    lv_label_set_text(s_pato.cf_title, s_pato.loaded);
    char meta[48];
    snprintf(meta, sizeof(meta), "%d %s", s_pato.nsteps, _("pasos"));
    lv_label_set_text(s_pato.cf_meta, meta);
    lv_label_set_text(s_pato.cf_preview, preview);
    free(raw);
    show_face(s_pato.confirm_box);
}

static void switch_cb(lv_event_t *event)
{
    (void)event;
    if (!aos_hal_usb_mode_set(AOS_HAL_USB_KEYS)) {
        aos_ui_toast(_("El USB esta cambiando de modo"), 1400);
    }
}

/* -------------------------------------------------------------------------- */
/* The list                                                                    */
/* -------------------------------------------------------------------------- */

/* Kept so the button user_data (a const char *) stays valid for the app's
 * life; refilled on every refresh. */
static char s_names[PATO_MAX_SCRIPTS][PATO_NAME_MAX];
static int  s_nnames;

static void refresh_list(void)
{
    lv_obj_clean(s_pato.list);
    s_nnames = 0;

    DIR *d = opendir(s_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && s_nnames < PATO_MAX_SCRIPTS) {
            size_t len = strlen(e->d_name);
            if (len < 6 || strcmp(e->d_name + len - 5, ".pato") != 0) {
                continue;       /* only *.pato */
            }
            size_t base = len - 5;
            if (base >= PATO_NAME_MAX) {
                base = PATO_NAME_MAX - 1;
            }
            memcpy(s_names[s_nnames], e->d_name, base);
            s_names[s_nnames][base] = '\0';
            s_nnames++;
        }
        closedir(d);
    }

    if (s_nnames == 0) {
        lv_obj_t *empty = lv_label_create(s_pato.list);
        lv_label_set_text(empty,
            _("No hay scripts.\nCrealos en el portal,\npagina Pato goma."));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, AOS_C_DIM, 0);
        lv_obj_set_style_text_font(empty, aos_font_small, 0);
        lv_obj_set_width(empty, lv_pct(100));
        return;
    }

    for (int i = 0; i < s_nnames; i++) {
        lv_obj_t *b = aos_button(s_pato.list, s_names[i], AOS_C_CARD2,
                                 pick_cb, s_names[i]);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 54);
    }
}

/* A cheap fingerprint of the folder: an FNV-1a hash over each .pato's name,
 * size and mtime. It changes when a script is added, removed or edited, which
 * is what lets the list refresh itself without a full rebuild every tick. */
static uint32_t list_signature(void)
{
    uint32_t h = 2166136261u;
    DIR *d = opendir(s_dir);
    if (!d) {
        return 0;
    }
    struct dirent *e;
    char path[192];
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 6 || strcmp(e->d_name + len - 5, ".pato") != 0) {
            continue;
        }
        for (const char *p = e->d_name; *p; p++) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", s_dir, e->d_name);
        if (stat(path, &st) == 0) {
            h = (h ^ (uint32_t)st.st_size) * 16777619u;
            h = (h ^ (uint32_t)st.st_mtime) * 16777619u;
        }
    }
    closedir(d);
    return h;
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    bool ready = aos_hal_usb_keys_ready();
    lv_label_set_text(s_pato.status,
                      ready ? _("USB: teclado listo")
                            : _("USB: pasalo a \"Teclado y red\""));
    lv_obj_set_style_text_color(s_pato.status, ready ? AOS_C_GREEN : AOS_C_ORANGE, 0);
    if (ready) {
        lv_obj_add_flag(s_pato.switch_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_pato.list, LV_ALIGN_TOP_MID, 0, 64);
    } else {
        lv_obj_remove_flag(s_pato.switch_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_pato.list, LV_ALIGN_TOP_MID, 0, 110);
    }

    /* Re-read the folder about every two seconds, but only while the list is
     * up (not mid-run) and rebuild only when it actually changed. */
    if (!lv_obj_has_flag(s_pato.list_box, LV_OBJ_FLAG_HIDDEN) &&
        ++s_pato.list_tick >= 4) {
        s_pato.list_tick = 0;
        uint32_t sig = list_signature();
        if (sig != s_pato.list_sig) {
            s_pato.list_sig = sig;
            refresh_list();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                   */
/* -------------------------------------------------------------------------- */

static lv_obj_t *make_box(lv_obj_t *root)
{
    lv_obj_t *box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_pato, 0, sizeof(s_pato));

    s_pato.steps = malloc(sizeof(pato_step_t) * PATO_MAX_STEPS);   /* PSRAM */
    if (!s_pato.steps) {
        return NULL;
    }

    const char *sd = aos_hal_path_sd_root();
    snprintf(s_dir, sizeof(s_dir), "%s/pato", sd ? sd : "/sdcard");

    /* --- the list face --- */
    s_pato.list_box = make_box(root);

    lv_obj_t *title = lv_label_create(s_pato.list_box);
    lv_label_set_text(title, "Pato goma");
    lv_obj_set_style_text_font(title, aos_font_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pato.status = lv_label_create(s_pato.list_box);
    lv_obj_set_style_text_font(s_pato.status, aos_font_small, 0);
    lv_label_set_text(s_pato.status, "");
    lv_obj_align(s_pato.status, LV_ALIGN_TOP_LEFT, 0, 34);

    s_pato.switch_btn = aos_button(s_pato.list_box, _("Pasar a Teclado"),
                                   AOS_C_ACCENT, switch_cb, NULL);
    lv_obj_set_size(s_pato.switch_btn, lv_pct(100), 42);
    lv_obj_align(s_pato.switch_btn, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_add_flag(s_pato.switch_btn, LV_OBJ_FLAG_HIDDEN);

    s_pato.list = lv_obj_create(s_pato.list_box);
    lv_obj_remove_style_all(s_pato.list);
    lv_obj_set_style_bg_opa(s_pato.list, LV_OPA_TRANSP, 0);
    lv_obj_set_size(s_pato.list, lv_pct(100), 300);
    lv_obj_align(s_pato.list, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_flex_flow(s_pato.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pato.list, 8, 0);
    lv_obj_set_style_pad_right(s_pato.list, 6, 0);

    /* --- the confirm face --- */
    s_pato.confirm_box = make_box(root);

    s_pato.cf_title = lv_label_create(s_pato.confirm_box);
    lv_obj_set_style_text_font(s_pato.cf_title, aos_font_title, 0);
    lv_obj_set_width(s_pato.cf_title, lv_pct(100));
    lv_label_set_long_mode(s_pato.cf_title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_pato.cf_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pato.cf_meta = lv_label_create(s_pato.confirm_box);
    lv_obj_set_style_text_font(s_pato.cf_meta, aos_font_small, 0);
    lv_obj_set_style_text_color(s_pato.cf_meta, AOS_C_DIM, 0);
    lv_obj_align(s_pato.cf_meta, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t *pv = lv_obj_create(s_pato.confirm_box);
    lv_obj_set_style_bg_color(pv, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(pv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pv, 0, 0);
    lv_obj_set_style_radius(pv, 10, 0);
    lv_obj_set_style_pad_all(pv, 10, 0);
    lv_obj_set_size(pv, lv_pct(100), 150);
    lv_obj_align(pv, LV_ALIGN_TOP_MID, 0, 58);
    s_pato.cf_preview = lv_label_create(pv);
    lv_obj_set_style_text_font(s_pato.cf_preview, aos_font_small, 0);
    lv_obj_set_style_text_color(s_pato.cf_preview, AOS_C_TEXT, 0);
    lv_obj_set_width(s_pato.cf_preview, lv_pct(100));
    lv_label_set_text(s_pato.cf_preview, "");

    lv_obj_t *warn = lv_label_create(s_pato.confirm_box);
    lv_label_set_text(warn, _("Se enviara al teclado y mouse de la computadora."));
    lv_obj_set_style_text_font(warn, aos_font_small, 0);
    lv_obj_set_style_text_color(warn, AOS_C_ORANGE, 0);
    lv_obj_set_width(warn, lv_pct(100));
    lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 216);

    lv_obj_t *cancel = aos_button(s_pato.confirm_box, _("Cancelar"), AOS_C_CARD2,
                                  confirm_cancel_cb, NULL);
    lv_obj_set_size(cancel, 150, 52);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 0, 268);

    s_pato.cf_run_btn = aos_button(s_pato.confirm_box, _("Ejecutar"), AOS_C_GREEN,
                                   confirm_run_cb, NULL);
    lv_obj_set_size(s_pato.cf_run_btn, 150, 52);
    lv_obj_align(s_pato.cf_run_btn, LV_ALIGN_TOP_RIGHT, 0, 268);

    /* --- the run face --- */
    s_pato.run_box = make_box(root);

    s_pato.run_title = lv_label_create(s_pato.run_box);
    lv_obj_set_style_text_font(s_pato.run_title, aos_font_title, 0);
    lv_obj_set_width(s_pato.run_title, lv_pct(100));
    lv_label_set_long_mode(s_pato.run_title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_pato.run_title, LV_ALIGN_TOP_LEFT, 0, 8);

    s_pato.run_bar = lv_bar_create(s_pato.run_box);
    lv_obj_set_size(s_pato.run_bar, lv_pct(100), 16);
    lv_obj_align(s_pato.run_bar, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(s_pato.run_bar, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_color(s_pato.run_bar, AOS_C_GREEN, LV_PART_INDICATOR);
    lv_bar_set_value(s_pato.run_bar, 0, LV_ANIM_OFF);

    s_pato.run_step = lv_label_create(s_pato.run_box);
    lv_obj_set_style_text_font(s_pato.run_step, aos_font_body, 0);
    lv_obj_set_style_text_color(s_pato.run_step, AOS_C_DIM, 0);
    lv_obj_align(s_pato.run_step, LV_ALIGN_TOP_MID, 0, 104);

    s_pato.run_btn = aos_button(s_pato.run_box, _("Parar"), AOS_C_RED,
                                run_btn_cb, NULL);
    lv_obj_set_size(s_pato.run_btn, 200, 56);
    lv_obj_align(s_pato.run_btn, LV_ALIGN_TOP_MID, 0, 240);

    refresh_list();
    s_pato.list_sig = list_signature();
    poll_cb(NULL);
    s_pato.poll = lv_timer_create(poll_cb, 500, NULL);
    show_face(s_pato.list_box);
    return &s_pato;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_pato.run_timer) {
        lv_timer_delete(s_pato.run_timer);
        s_pato.run_timer = NULL;
    }
    if (s_pato.poll) {
        lv_timer_delete(s_pato.poll);
        s_pato.poll = NULL;
    }
    prog_free();
    free(s_pato.steps);
    s_pato.steps = NULL;
    /* the LVGL objects under root are freed by the runtime */
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    /* From a sub-face, "back" returns to the list instead of leaving. */
    if (!lv_obj_has_flag(s_pato.run_box, LV_OBJ_FLAG_HIDDEN)) {
        if (!s_pato.done) {
            run_stop(false);
        }
        show_face(s_pato.list_box);
        return true;
    }
    if (!lv_obj_has_flag(s_pato.confirm_box, LV_OBJ_FLAG_HIDDEN)) {
        show_face(s_pato.list_box);
        return true;
    }
    return false;
}

void aos_app_pato_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.pato",
            .name     = "Pato goma",
            .icon_vec = AOS_ICON_DUCK,
            .icon     = LV_SYMBOL_USB,      /* fallback if the firmware is old */
            .color_a  = 0x0A84FF,           /* water blue, so the yellow pops  */
            .color_b  = 0x0050A0,
            .order    = 48,                 /* right after Control PC (47)      */
            .flags    = AOS_APP_FLAG_KEEP_AWAKE,  /* a run must not dim out     */
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
