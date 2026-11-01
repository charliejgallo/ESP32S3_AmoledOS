/*
 * AmoledOS - Home: the time and four things of the house, from Home Assistant.
 *
 * Up to four entities in tiles under the time: a sensor shows its value and
 * unit, a light or a switch whether it is on, and a tap on one of those
 * toggles it. Which four is the face's own list, written by its page in the
 * portal (/face_casa) into face_casa.cfg beside the module; with none, the
 * four the Sensores app shows. The address and the token are Remoto's, the
 * same that every Home Assistant thing on the watch uses (rc_url, rc_token).
 *
 * One request for all of them (a template, Remoto's lesson), every 30 s
 * while the screen is lit and the face on it; none with the screen dimmed or
 * off, so the house is not asked about while nobody looks. The values seen
 * last stay, with how old they are.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FACE_ID     "face.casa"
#define CFG_FILE    "face_casa.cfg"
#define TILES       4
#define POLL_MS     30000
#define RESP_MAX    2048

#define TILE_W      164
#define TILE_H      100
#define TILE_GAP    12
#define TILE_Y      164

typedef struct {
    char entity[64];
    char name[28];          /* the list's; else Home Assistant's friendly name */
    char value[24];
    char unit[12];
    bool toggle;            /* light, switch, input_boolean, fan */
    bool on;
    bool known;
} ent_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *status;
    lv_obj_t *hint;
    lv_obj_t *tile[TILES];
    lv_obj_t *t_name[TILES];
    lv_obj_t *t_value[TILES];
    lv_obj_t *t_dot[TILES];

    ent_t     ent[TILES];
    int       n;
    bool      aod;

    char      base[96];
    char      hdr[320];
    bool      configured;

    int       req_poll;
    int       req_cmd;
    uint32_t  last_poll_ms;     /* when the last poll went out */
    uint32_t  last_ok_ms;       /* when one last answered */
    uint32_t  next_poll_ms;     /* 0: as soon as possible */
    char      error[48];
    char      last_hhmm[8];
} casa_t;

/* -------------------------------------------------------------------------- */

/* Keeps ASCII and two-byte UTF-8 (Latin-1, which the fonts have): an emoji
 * in a friendly name would be drawn as nothing at all. */
static void clean_copy(char *out, size_t n, const char *in, size_t len)
{
    size_t w = 0;
    for (size_t r = 0; r < len && in[r] && w + 1 < n; r++) {
        unsigned char c = (unsigned char)in[r];
        if (c >= 0x20 && c < 0x7F) {
            out[w++] = (char)c;
        } else if ((c == 0xC2 || c == 0xC3) && r + 1 < len && w + 2 < n) {
            out[w++] = (char)c;
            out[w++] = in[++r];
        } else if ((c & 0xE0) == 0xC0) {
            r += 1;
        } else if ((c & 0xF0) == 0xE0) {
            r += 2;
        } else if ((c & 0xF8) == 0xF0) {
            r += 3;
        }
    }
    out[w] = '\0';
}

/* strcspn() is not in the firmware's symbol table */
static size_t span_to(const char *s, const char *stops)
{
    size_t n = 0;
    while (s[n] && !strchr(stops, s[n])) {
        n++;
    }
    return n;
}

static bool entity_ok(const char *e)
{
    if (!strchr(e, '.')) {
        return false;
    }
    for (; *e; e++) {
        if (!((*e >= 'a' && *e <= 'z') || (*e >= '0' && *e <= '9') || *e == '_' || *e == '.')) {
            return false;
        }
    }
    return true;
}

static bool domain_toggles(const char *e)
{
    static const char *const T[] = { "light.", "switch.", "input_boolean.", "fan.",
                                     "automation." };
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        if (strncmp(e, T[i], strlen(T[i])) == 0) {
            return true;
        }
    }
    return false;
}

/* "entity|name;entity|name": the Sensores app's format, so its list reads
 * the same (its third field, the unit, is ignored: Home Assistant says it). */
static int parse_list(casa_t *c, const char *txt)
{
    c->n = 0;
    const char *p = txt;
    while (*p && c->n < TILES) {
        char rec[128];
        size_t len = span_to(p, ";\n");
        snprintf(rec, sizeof(rec), "%.*s", (int)len, p);
        p += len;
        if (*p) {
            p++;
        }
        char *bar = strchr(rec, '|');
        char *name = NULL;
        if (bar) {
            *bar = '\0';
            name = bar + 1;
            char *bar2 = strchr(name, '|');
            if (bar2) {
                *bar2 = '\0';
            }
        }
        /* trim */
        char *e = rec;
        while (*e == ' ') e++;
        e[span_to(e, " \r")] = '\0';
        if (!entity_ok(e)) {
            continue;
        }
        ent_t *t = &c->ent[c->n++];
        memset(t, 0, sizeof(*t));
        snprintf(t->entity, sizeof(t->entity), "%.63s", e);
        if (name && *name) {
            clean_copy(t->name, sizeof(t->name), name, strlen(name));
        }
        t->toggle = domain_toggles(t->entity);
    }
    return c->n;
}

/* The face's own list lives beside its module (on the board,
 * /spiffs/faces/face_casa.cfg, which the portal writes as dir=faces); a face
 * without a file of its own (the simulator) looks in the data folder. */
static void cfg_path(char *out, size_t n)
{
    for (int i = 0; i < aos_watchface_count(); i++) {
        const aos_watchface_t *f = aos_watchface_at(i);
        const char *path = aos_watchface_path(i);
        if (f && path && strcmp(f->id, FACE_ID) == 0) {
            const char *slash = strrchr(path, '/');
            snprintf(out, n, "%.*s/" CFG_FILE, (int)(slash - path), path);
            return;
        }
    }
    snprintf(out, n, "%s/" CFG_FILE, aos_hal_path_data());
}

static void load_config(casa_t *c)
{
    char url[96] = "", token[280] = "";
    aos_hal_pref_get_str("rc_url", url, sizeof(url));
    aos_hal_pref_get_str("rc_token", token, sizeof(token));
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        url[--n] = '\0';
    }
    c->configured = n > 8 && token[0] &&
                    (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
    snprintf(c->base, sizeof(c->base), "%s", url);
    snprintf(c->hdr, sizeof(c->hdr), "Authorization: Bearer %s\r\n", token);

    /* keep what was shown if the list is the same */
    ent_t old[TILES];
    int old_n = c->n;
    memcpy(old, c->ent, sizeof(old));

    char path[96], txt[400] = "";
    cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        size_t got = fread(txt, 1, sizeof(txt) - 1, f);
        txt[got] = '\0';
        fclose(f);
    }
    if (!parse_list(c, txt)) {
        txt[0] = '\0';
        aos_hal_pref_get_str("sn_list", txt, sizeof(txt));
        parse_list(c, txt);
    }
    for (int i = 0; i < c->n; i++) {
        for (int j = 0; j < old_n; j++) {
            if (strcmp(c->ent[i].entity, old[j].entity) == 0) {
                char name[28];
                snprintf(name, sizeof(name), "%s", c->ent[i].name);
                c->ent[i] = old[j];
                if (name[0]) {
                    snprintf(c->ent[i].name, sizeof(c->ent[i].name), "%s", name);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------- */

static const char *state_text(const char *s)
{
    static const struct { const char *ha, *es; } MAP[] = {
        { "on", N_("Encendido") }, { "off", N_("Apagado") },
        { "open", N_("Abierto") }, { "closed", N_("Cerrado") },
        { "home", N_("En casa") }, { "not_home", N_("Fuera") },
        { "locked", N_("Cerrado") }, { "unlocked", N_("Abierto") },
        { "playing", N_("Sonando") }, { "paused", N_("Pausa") },
        { "idle", N_("Inactivo") },
    };
    if (!strcmp(s, "unavailable") || !strcmp(s, "unknown") || !*s) {
        return "--";
    }
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        if (!strcmp(s, MAP[i].ha)) {
            return _(MAP[i].es);
        }
    }
    return s;
}

static void layout_tiles(casa_t *c)
{
    for (int i = 0; i < TILES; i++) {
        if (i >= c->n || c->aod) {
            lv_obj_add_flag(c->tile[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(c->tile[i], LV_OBJ_FLAG_HIDDEN);
        int row = i / 2, col = i % 2;
        /* an odd last tile spans the row */
        bool wide = (i == c->n - 1) && (c->n % 2 == 1);
        int w = wide ? 2 * TILE_W + TILE_GAP : TILE_W;
        int x = (AOS_SCREEN_W - (2 * TILE_W + TILE_GAP)) / 2 + (wide ? 0 : col * (TILE_W + TILE_GAP));
        int y = TILE_Y + (c->n <= 2 ? 50 : 0) + row * (TILE_H + TILE_GAP);
        lv_obj_set_size(c->tile[i], w, TILE_H);
        lv_obj_set_pos(c->tile[i], x, y);
        lv_obj_set_width(c->t_name[i], w - 44);
    }
}

static void paint_tiles(casa_t *c)
{
    for (int i = 0; i < c->n && i < TILES; i++) {
        const ent_t *t = &c->ent[i];
        lv_label_set_text(c->t_name[i], t->name[0] ? t->name : t->entity);
        char val[48];
        if (!t->known) {
            snprintf(val, sizeof(val), "...");
        } else if (t->unit[0]) {
            snprintf(val, sizeof(val), "%s %s", t->value, t->unit);
        } else {
            snprintf(val, sizeof(val), "%s", state_text(t->value));
        }
        lv_label_set_text(c->t_value[i], val);
        /* a number reads big; a word ("Encendido") in the body size fits */
        bool word = t->known && !t->unit[0] && strcmp(state_text(t->value), t->value) != 0;
        lv_obj_set_style_text_font(c->t_value[i], word ? aos_font_body : aos_font_title, 0);

        lv_color_t bg = lv_color_hex(0x1C1C1E), dot = lv_color_hex(0x3A3A3C);
        lv_color_t fg = lv_color_hex(0xFFFFFF);
        if (t->known && (t->toggle || !t->unit[0]) && (t->on || !strcmp(t->value, "on"))) {
            bg  = lv_color_hex(0x3A2C08);
            dot = lv_color_hex(0xFFC83D);
            fg  = lv_color_hex(0xFFD86A);
        } else if (t->known && t->unit[0]) {
            dot = lv_color_hex(0x40C8E0);
        } else if (t->known && t->toggle) {
            fg = lv_color_hex(0x9A9AA0);
        }
        lv_obj_set_style_bg_color(c->tile[i], bg, 0);
        lv_obj_set_style_bg_color(c->t_dot[i], dot, 0);
        lv_obj_set_style_text_color(c->t_value[i], fg, 0);
    }
}

static void set_status(casa_t *c)
{
    char txt[96];
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (!c->configured) {
        snprintf(txt, sizeof(txt), "%s", _("Home Assistant sin configurar"));
    } else if (c->n == 0) {
        snprintf(txt, sizeof(txt), "%s", "");
    } else if (c->error[0]) {
        snprintf(txt, sizeof(txt), "%s", c->error);
    } else if (c->last_ok_ms) {
        uint32_t min = (now - c->last_ok_ms) / 60000;
        char when[32];
        if (min == 0) {
            snprintf(when, sizeof(when), "%s", _("ahora"));
        } else {
            snprintf(when, sizeof(when), _("hace %u min"), (unsigned)min);
        }
        snprintf(txt, sizeof(txt), "Home Assistant  \xC2\xB7  %s", when);
    } else {
        snprintf(txt, sizeof(txt), "Home Assistant...");
    }
    lv_label_set_text(c->status, txt);

    bool hint = !c->configured || c->n == 0;
    if (hint && !c->aod) {
        char h[96];
        snprintf(h, sizeof(h), _("Elegí qué mostrar en\n%s.local/face_casa"),
                 aos_hal_device_name());
        lv_label_set_text(c->hint, h);
        lv_obj_remove_flag(c->hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(c->hint, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* The conversation                                                           */
/* -------------------------------------------------------------------------- */

static void poll_send(casa_t *c)
{
    if (!c->configured || c->n == 0 || c->req_poll || c->req_cmd) {
        return;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        snprintf(c->error, sizeof(c->error), "%s", _("sin wifi"));
        return;
    }
    /* state^unit^friendly name, per entity, joined by '|' */
    char tpl[1400];
    int w = snprintf(tpl, sizeof(tpl), "{\"template\":\"");
    for (int i = 0; i < c->n && w < (int)sizeof(tpl) - 200; i++) {
        const char *e = c->ent[i].entity;
        w += snprintf(tpl + w, sizeof(tpl) - w,
                      "%s{{states('%s')}}^{{state_attr('%s','unit_of_measurement')|default('',true)}}"
                      "^{{state_attr('%s','friendly_name')|default('',true)}}",
                      i ? "|" : "", e, e, e);
    }
    snprintf(tpl + w, sizeof(tpl) - w, "\"}");
    char url[160];
    snprintf(url, sizeof(url), "%s/api/template", c->base);
    int id = aos_hal_http_request("POST", url, c->hdr, tpl, "application/json", RESP_MAX);
    if (id > 0) {
        c->req_poll = id;
        c->last_poll_ms = (uint32_t)aos_hal_uptime_ms();
    }
}

static void apply(casa_t *c, const char *body, int len)
{
    const char *p = body, *end = body + len;
    for (int i = 0; i < c->n && p <= end; i++) {
        const char *rec_end = memchr(p, '|', (size_t)(end - p));
        if (!rec_end) {
            rec_end = end;
        }
        /* state ^ unit ^ name */
        const char *f[3] = { p, NULL, NULL };
        size_t fl[3] = { (size_t)(rec_end - p), 0, 0 };
        const char *c1 = memchr(p, '^', (size_t)(rec_end - p));
        if (c1) {
            fl[0] = (size_t)(c1 - p);
            f[1] = c1 + 1;
            const char *c2 = memchr(f[1], '^', (size_t)(rec_end - f[1]));
            fl[1] = (size_t)((c2 ? c2 : rec_end) - f[1]);
            if (c2) {
                f[2] = c2 + 1;
                fl[2] = (size_t)(rec_end - f[2]);
            }
        }
        ent_t *t = &c->ent[i];
        clean_copy(t->value, sizeof(t->value), f[0], fl[0]);
        t->unit[0] = '\0';
        if (f[1]) {
            clean_copy(t->unit, sizeof(t->unit), f[1], fl[1]);
        }
        if (f[2] && !t->name[0]) {
            clean_copy(t->name, sizeof(t->name), f[2], fl[2]);
        }
        /* a number with many decimals is cut to one */
        char *dot = strchr(t->value, '.');
        if (dot && t->unit[0] && strlen(dot) > 2) {
            dot[2] = '\0';
        }
        t->on = !strcmp(t->value, "on");
        t->known = true;
        p = rec_end + 1;
    }
}

static void pump(casa_t *c)
{
    if (c->req_poll && aos_hal_http_state(c->req_poll) != AOS_HTTP_BUSY) {
        int code = aos_hal_http_status(c->req_poll);
        if (aos_hal_http_state(c->req_poll) == AOS_HTTP_DONE && code == 200) {
            const char *body = aos_hal_http_body(c->req_poll);
            int len = aos_hal_http_len(c->req_poll);
            if (body && len > 0) {
                apply(c, body, len);
            }
            c->error[0] = '\0';
            c->last_ok_ms = (uint32_t)aos_hal_uptime_ms();
        } else if (code == 401 || code == 403) {
            snprintf(c->error, sizeof(c->error), _("token rechazado (%d)"), code);
        } else {
            snprintf(c->error, sizeof(c->error), _("Home Assistant no responde (%d)"), code);
        }
        aos_hal_http_release(c->req_poll);
        c->req_poll = 0;
        c->next_poll_ms = (uint32_t)aos_hal_uptime_ms() + POLL_MS;
        paint_tiles(c);
        set_status(c);
    }
    if (c->req_cmd && aos_hal_http_state(c->req_cmd) != AOS_HTTP_BUSY) {
        int code = aos_hal_http_status(c->req_cmd);
        if (code != 200) {
            snprintf(c->error, sizeof(c->error), _("no se pudo cambiar (%d)"), code);
        }
        aos_hal_http_release(c->req_cmd);
        c->req_cmd = 0;
        /* the light answers a moment later: ask after a breath */
        c->next_poll_ms = (uint32_t)aos_hal_uptime_ms() + 900;
        set_status(c);
    }
}

static void toggle(casa_t *c, int i)
{
    ent_t *t = &c->ent[i];
    if (!c->configured || aos_hal_net_state() != AOS_NET_CONNECTED) {
        return;
    }
    if (c->req_poll) {
        aos_hal_http_release(c->req_poll);
        c->req_poll = 0;
    }
    if (c->req_cmd) {
        return;
    }
    char body[96], url[160];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", t->entity);
    snprintf(url, sizeof(url), "%s/api/services/homeassistant/toggle", c->base);
    int id = aos_hal_http_request("POST", url, c->hdr, body, "application/json", 512);
    if (id > 0) {
        c->req_cmd = id;
        /* show it changed at once; the next poll says how it really is */
        t->on = !t->on;
        snprintf(t->value, sizeof(t->value), "%s", t->on ? "on" : "off");
        paint_tiles(c);
        aos_hal_activity();
    }
}

/* -------------------------------------------------------------------------- */

static void *create(lv_obj_t *root)
{
    casa_t *c = lv_malloc_zeroed(sizeof(casa_t));
    if (!c) {
        return NULL;
    }
    c->root = root;

    c->time = aos_label_scaled(root, "--:--", aos_font_huge, AOS_C_TEXT, 330);
    lv_obj_align(c->time, LV_ALIGN_TOP_MID, 0, 46);

    c->date = aos_label(root, "", aos_font_body, AOS_C_ACCENT);
    lv_obj_align(c->date, LV_ALIGN_TOP_MID, 0, 124);

    for (int i = 0; i < TILES; i++) {
        lv_obj_t *t = lv_obj_create(root);
        lv_obj_remove_style_all(t);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(t, AOS_C_CARD, 0);
        lv_obj_set_style_radius(t, 22, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        c->tile[i] = t;

        c->t_name[i] = aos_label(t, "", aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(c->t_name[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(c->t_name[i], 14, 12);

        c->t_dot[i] = lv_obj_create(t);
        lv_obj_remove_style_all(c->t_dot[i]);
        lv_obj_set_size(c->t_dot[i], 12, 12);
        lv_obj_set_style_radius(c->t_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c->t_dot[i], LV_OPA_COVER, 0);
        lv_obj_align(c->t_dot[i], LV_ALIGN_TOP_RIGHT, -14, 16);

        c->t_value[i] = aos_label(t, "", aos_font_title, AOS_C_TEXT);
        lv_obj_align(c->t_value[i], LV_ALIGN_BOTTOM_LEFT, 14, -12);
    }

    c->hint = aos_label(root, "", aos_font_body, AOS_C_DIM);
    lv_obj_set_style_text_align(c->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c->hint, LV_ALIGN_CENTER, 0, 70);

    c->status = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(c->status, LV_ALIGN_BOTTOM_MID, 0, -26);

    load_config(c);
    layout_tiles(c);
    paint_tiles(c);
    set_status(c);
    c->next_poll_ms = 0;
    return c;
}

static void refresh(void *ctx, const struct tm *now)
{
    casa_t *c = ctx;
    if (!c) {
        return;
    }
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(hhmm, c->last_hhmm) != 0) {
        snprintf(c->last_hhmm, sizeof(c->last_hhmm), "%s", hhmm);
        lv_label_set_text(c->time, hhmm);
        char date[40];
        snprintf(date, sizeof(date), "%s %d %s", aos_day_name(now->tm_wday),
                 now->tm_mday, aos_month_name(now->tm_mon));
        lv_label_set_text(c->date, date);
        set_status(c);                      /* "hace N min" moves on */
    }
    pump(c);
    if (c->aod || !aos_watchface_live()) {
        return;
    }
    uint32_t t = (uint32_t)aos_hal_uptime_ms();
    if (!c->req_poll && !c->req_cmd && (c->next_poll_ms == 0 || (int32_t)(t - c->next_poll_ms) >= 0)) {
        load_config(c);                     /* the page may have changed the list */
        layout_tiles(c);
        poll_send(c);
        if (!c->req_poll) {
            c->next_poll_ms = t + POLL_MS;  /* no wifi, no list: try again later */
            paint_tiles(c);
        }
        set_status(c);
    }
}

static void set_aod(void *ctx, bool aod)
{
    casa_t *c = ctx;
    if (!c) {
        return;
    }
    c->aod = aod;
    lv_obj_set_style_text_color(c->time, aod ? lv_color_hex(0x9A9A9A) : AOS_C_TEXT, 0);
    lv_obj_set_style_text_color(c->date, aod ? lv_color_hex(0x5A5A5A) : AOS_C_ACCENT, 0);
    if (aod) {
        lv_obj_add_flag(c->status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(c->status, LV_OBJ_FLAG_HIDDEN);
        /* back from dimmed: fresh values if the ones shown are old */
        if (!c->last_ok_ms || (uint32_t)aos_hal_uptime_ms() - c->last_ok_ms > POLL_MS) {
            c->next_poll_ms = 0;
        }
    }
    layout_tiles(c);
    set_status(c);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    casa_t *c = ctx;
    if (!c || what != AOS_FACE_TOUCH_TAP || c->aod) {
        return false;
    }
    for (int i = 0; i < c->n && i < TILES; i++) {
        lv_area_t a;
        lv_obj_get_coords(c->tile[i], &a);
        if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
            if (c->ent[i].toggle) {
                toggle(c, i);
            } else {
                c->next_poll_ms = 0;        /* a sensor: ask now */
            }
            return true;
        }
    }
    return false;
}

static void destroy(void *ctx)
{
    casa_t *c = ctx;
    if (!c) {
        return;
    }
    /* the slots are the whole system's: three */
    if (c->req_poll) {
        aos_hal_http_release(c->req_poll);
    }
    if (c->req_cmd) {
        aos_hal_http_release(c->req_cmd);
    }
    lv_free(c);
}

static bool casa_get(aos_watchface_t *face)
{
    face->id      = FACE_ID;
    face->name    = N_("Casa");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
    face->flags   = AOS_FACE_FLAG_TOUCH;
    face->touch   = touch;
    return true;
}

AOS_FACE_ENTRY(casa_get);
