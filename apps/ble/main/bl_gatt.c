/*
 * BLE - the GATT explorer: one device, connected.
 *
 * The firmware connects, exchanges the MTU and discovers every service,
 * characteristic and descriptor (aos_hal_ble_gatt_*). Here they are shown as
 * cards, a service each, with what every characteristic allows (read,
 * write, notify, indicate) and its value in plain words when its UUID is a
 * known one (bl_value_format), as text when it is text, and in hex always.
 * "Leer todo" reads every readable attribute one after another, which is
 * how a Device Information service tells who made the thing and what
 * firmware it runs. Notifications update their value as they come and are
 * counted; the last events are listed at the bottom.
 *
 * No pairing: a characteristic that wants an encrypted link answers with
 * its ATT error, said in words.
 */
#include "bl.h"

#include <stdio.h>
#include <string.h>

#define ATTRS_MAX 160
#define LOG_MAX   12

typedef struct {
    uint8_t val[64];
    uint16_t len, full;
    int16_t status;
    uint32_t t, count;
    bool read_once, sub;
} attr_val_t;

static struct {
    bool open;                      /* a connection was asked for */
    aos_ble_attr_t at[ATTRS_MAX];
    attr_val_t v[ATTRS_MAX];
    int n;
    int state, reason;
    bool built_ready;
    /* "read everything": the queue, one at a time */
    uint16_t rq[ATTRS_MAX];
    int rq_n, rq_i;
    bool rq_wait;
    uint32_t rq_t;
    /* the screen */
    lv_obj_t *col, *st, *st_sub, *btns, *list, *log;
    lv_obj_t *val_l[ATTRS_MAX], *sub_b[ATTRS_MAX];
    char logs[LOG_MAX][96];
    int nlog;
    bool log_dirty;
    uint16_t write_h;
    bool write_rsp;
} G;

static const char *att_error(int st)
{
    if (st == 0) return "";
    if (st == 0x105) return N_("pide un enlace autenticado (emparejar)");
    if (st == 0x10F) return N_("pide un enlace cifrado (emparejar)");
    if (st == 0x108) return N_("pide autorización");
    if (st == 0x102) return N_("no se puede leer");
    if (st == 0x103) return N_("no se puede escribir");
    if (st == 0x101) return N_("atributo inexistente");
    if (st == 0x10D) return N_("largo no válido");
    if (st == 0x106) return N_("pedido no soportado");
    if (st == 13) return N_("no contestó a tiempo");
    if (st == 7) return N_("desconectado");
    if (st > 0x100 && st < 0x200) return N_("error de ATT");
    return N_("error");
}

static const char *reason_text(int r)
{
    switch (r) {
    case 0: return "";
    case 13: return N_("no contestó: puede que no acepte conexiones o esté lejos");
    case 0x208: return N_("se perdió la señal");
    case 0x213: return N_("el equipo cortó");
    case 0x216: return N_("cortado desde acá");
    case 0x23E: return N_("la conexión no llegó a armarse");
    case 0x207: return N_("no hay lugar para otra conexión");
    case 14: return N_("no hay lugar para otra conexión");
    default: return N_("falló");
    }
}

static void log_add(const char *fmt, const char *a, const char *b)
{
    if (G.nlog == LOG_MAX) {
        memmove(G.logs[0], G.logs[1], sizeof G.logs[0] * (LOG_MAX - 1));
        G.nlog--;
    }
    char tm[16];
    uint32_t s = (uint32_t)(aos_hal_uptime_ms() / 1000);
    snprintf(tm, sizeof tm, "%02u:%02u", (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    char line[96];
    snprintf(line, sizeof line, fmt, a ? a : "", b ? b : "");
    snprintf(G.logs[G.nlog++], sizeof G.logs[0], "%s  %.80s", tm, line);
    G.log_dirty = true;
}

static int attr_index(uint16_t h)
{
    for (int i = 0; i < G.n; i++)
        if (G.at[i].handle == h && G.at[i].kind != AOS_BLE_ATTR_SERVICE) return i;
    return -1;
}

static uint16_t uuid16_of(const aos_ble_attr_t *a)
{
    return a->uuid_len == 2 ? (uint16_t)(a->uuid[0] | (a->uuid[1] << 8)) : 0;
}

static void attr_name(const aos_ble_attr_t *a, char *out, size_t n)
{
    const char *nm = a->uuid_len == 2 ? bl_uuid16_name(uuid16_of(a)) : a->uuid_len == 16 ? bl_uuid128_name(a->uuid) : NULL;
    char u[48];
    bl_uuid_str(a->uuid, a->uuid_len, u, sizeof u);
    if (nm) snprintf(out, n, "%.60s", _(nm));
    else snprintf(out, n, "%s", u);
}

/* What a value says: in words if its UUID is known, as text if it is text,
 * and the hex after. */
static void value_text(int i, char *out, size_t n)
{
    const attr_val_t *v = &G.v[i];
    const aos_ble_attr_t *a = &G.at[i];
    if (v->status) {
        snprintf(out, n, "%s (0x%X)", _(att_error(v->status)), v->status);
        return;
    }
    if (!v->read_once) {
        out[0] = 0;
        return;
    }
    char words[120] = "", hex[64 * 3 + 8];
    if (!(a->uuid_len == 2 && bl_value_format(uuid16_of(a), v->val, v->len, words, sizeof words, aos_tr)))
        bl_value_text(v->val, v->len, words, sizeof words);
    bl_hex(v->val, v->len > 24 ? 24 : v->len, hex, sizeof hex);
    const char *more = v->full > 24 ? " ..." : "";
    char cnt[24] = "";
    if (v->count > 1) snprintf(cnt, sizeof cnt, "  ×%u", (unsigned)v->count);
    if (words[0]) snprintf(out, n, "%s\n%s%s%s", words, hex, more, cnt);
    else if (v->len) snprintf(out, n, "%s%s%s", hex, more, cnt);
    else snprintf(out, n, "%s%s", _("(vacío)"), cnt);
}

static void val_show(int i)
{
    if (i < 0 || i >= G.n || !G.val_l[i]) return;
    char t[300];
    value_text(i, t, sizeof t);
    lv_label_set_text(G.val_l[i], t);
    lv_obj_set_style_text_color(G.val_l[i], G.v[i].status ? AOS_C_ORANGE : AOS_C_TEXT, 0);
    if (t[0]) lv_obj_remove_flag(G.val_l[i], LV_OBJ_FLAG_HIDDEN);
}

/* ---- actions ---- */

static void read_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < G.n) aos_hal_ble_gatt_read(G.at[i].handle);
}

static void sub_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= G.n) return;
    int mode = G.v[i].sub ? 0 : (G.at[i].props & 0x10) ? 1 : 2;
    aos_hal_ble_gatt_subscribe(G.at[i].handle, mode);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "0x01 02 a0" or "01:02:A0" is bytes; anything else, its text */
static int parse_value(const char *s, uint8_t *out, int max)
{
    const char *p = s;
    while (*p == ' ') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        int n = 0, hi = -1;
        for (; *p && n < max; p++) {
            int v = hexval(*p);
            if (v < 0) { if (*p == ' ' || *p == ':' || *p == ',' || *p == '-') continue; return -1; }
            if (hi < 0) hi = v;
            else { out[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
        }
        return hi < 0 ? n : -1;
    }
    int n = (int)strlen(s);
    if (n > max) n = max;
    memcpy(out, s, n);
    return n;
}

static void write_done(const char *s)
{
    static uint8_t buf[512];
    int n = parse_value(s, buf, sizeof buf);
    if (n < 0) {
        aos_ui_toast(_("Hex incompleto: van de a dos cifras"), 1800);
        return;
    }
    if (!aos_hal_ble_gatt_write(G.write_h, buf, n, G.write_rsp)) aos_ui_toast(_("No se pudo escribir"), 1500);
    else {
        char b[16];
        snprintf(b, sizeof b, "%d", n);
        int i = attr_index(G.write_h);
        char nm[64] = "";
        if (i >= 0) attr_name(&G.at[i], nm, sizeof nm);
        log_add(_("escrito en %s: %s bytes"), nm, b);
    }
}

static void write_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= G.n) return;
    G.write_h = G.at[i].handle;
    G.write_rsp = (G.at[i].props & 0x08) != 0;
    bl_text_entry(_("Escribir: texto, o bytes con 0x (0x01 A0)"), "", false, write_done);
}

static void read_all_cb(lv_event_t *e)
{
    (void)e;
    G.rq_n = G.rq_i = 0;
    G.rq_wait = false;
    for (int i = 0; i < G.n && G.rq_n < ATTRS_MAX; i++) {
        const aos_ble_attr_t *a = &G.at[i];
        if (a->kind == AOS_BLE_ATTR_CHAR && (a->props & 0x02)) G.rq[G.rq_n++] = a->handle;
        else if (a->kind == AOS_BLE_ATTR_DESC) G.rq[G.rq_n++] = a->handle;
    }
    char n[12];
    snprintf(n, sizeof n, "%d", G.rq_n);
    log_add(_("leyendo %s atributos"), n, NULL);
}

static void disc_cb(lv_event_t *e)
{
    (void)e;
    if (G.state == AOS_BLE_GATT_READY || G.state == AOS_BLE_GATT_CONNECTING || G.state == AOS_BLE_GATT_DISCOVERING) {
        aos_hal_ble_gatt_disconnect();
        G.open = false;
        G.state = AOS_BLE_GATT_IDLE;
        log_add("%s", _("desconectado"), NULL);
    } else if (BL.sel >= 0) {
        memset(G.v, 0, sizeof G.v);
        G.n = 0;
        G.open = aos_hal_ble_gatt_connect(BL.dev[BL.sel].addr, BL.dev[BL.sel].addr_type);
        if (!G.open) aos_ui_toast(_("Bluetooth apagado"), 1500);
        else log_add("%s", _("conectando..."), NULL);
    }
    G.built_ready = false;
    bl_rebuild();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    bl_gatt_close();
    BL.page = BL_PAGE_DETAIL;
    bl_rebuild();
}

/* ---- the screen ---- */

static lv_obj_t *small_btn(lv_obj_t *parent, const char *text, lv_color_t bg, lv_event_cb_t cb, int i)
{
    lv_obj_t *b = bl_box(parent, LV_SIZE_CONTENT, 38);
    lv_obj_set_style_radius(b, 19, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = aos_label(b, text, aos_font_small, AOS_C_TEXT);
    lv_obj_center(l);
    aos_make_decorative(l);
    return b;
}

static void props_text(uint8_t p, char *out, size_t n)
{
    size_t k = 0;
    out[0] = 0;
    static const struct { uint8_t bit; const char *name; } P[] = {
        { 0x02, N_("lee") }, { 0x08, N_("escribe") }, { 0x04, N_("escribe sin respuesta") }, { 0x10, N_("notifica") },
        { 0x20, N_("indica") }, { 0x01, N_("difunde") }, { 0x40, N_("firma") },
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0] && k < n; i++)
        if (p & P[i].bit) k += snprintf(out + k, n - k, "%s%s", k ? " · " : "", _(P[i].name));
}

static void list_build(void)
{
    int32_t w = BL.cw;
    lv_obj_t *svc = NULL;
    int32_t iw = w - 28;
    for (int i = 0; i < G.n && i < ATTRS_MAX; i++) {
        const aos_ble_attr_t *a = &G.at[i];
        char nm[80], u[48], t[160];
        attr_name(a, nm, sizeof nm);
        bl_uuid_str(a->uuid, a->uuid_len, u, sizeof u);
        if (a->kind == AOS_BLE_ATTR_SERVICE) {
            svc = bl_vcard(G.list, w, 14, 6);
            aos_label(svc, nm, aos_font_body, BL_C);
            snprintf(t, sizeof t, _("Servicio %s · handles %u a %u"), u, a->handle, a->end);
            bl_caption(svc, t, iw);
            continue;
        }
        if (!svc) svc = bl_vcard(G.list, w, 14, 6);
        if (a->kind == AOS_BLE_ATTR_CHAR) {
            lv_obj_t *box = bl_box(svc, iw, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(box, 4, 0);
            lv_obj_set_style_pad_top(box, 6, 0);
            lv_obj_set_style_border_side(box, LV_BORDER_SIDE_TOP, 0);
            lv_obj_set_style_border_width(box, 1, 0);
            lv_obj_set_style_border_color(box, AOS_C_CARD2, 0);
            lv_obj_t *l = aos_label(box, nm, aos_font_small, AOS_C_TEXT);
            lv_obj_set_width(l, iw);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            char pr[96];
            props_text(a->props, pr, sizeof pr);
            snprintf(t, sizeof t, "%s · %u · %s", u, a->handle, pr);
            bl_caption(box, t, iw);
            G.val_l[i] = aos_label(box, "", &aos_montserrat_14, AOS_C_TEXT);
            lv_obj_set_width(G.val_l[i], iw);
            lv_label_set_long_mode(G.val_l[i], LV_LABEL_LONG_MODE_WRAP);
            lv_obj_add_flag(G.val_l[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *br = bl_wrap(box, iw, 6);
            if (a->props & 0x02) small_btn(br, _("Leer"), AOS_C_CARD2, read_cb, i);
            if (a->props & 0x0C) small_btn(br, _("Escribir"), AOS_C_CARD2, write_cb, i);
            if (a->props & 0x30)
                G.sub_b[i] = small_btn(br, G.v[i].sub ? _("Dejar de escuchar") : (a->props & 0x10) ? _("Notificaciones") : _("Indicaciones"),
                                       G.v[i].sub ? BL_C : AOS_C_CARD2, sub_cb, i);
            if (!(a->props & 0x3E)) lv_obj_add_flag(br, LV_OBJ_FLAG_HIDDEN);
            val_show(i);
        } else {
            /* a descriptor: one line, its value beside when read */
            lv_obj_t *box = bl_box(svc, iw, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_left(box, 14, 0);
            lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
            snprintf(t, sizeof t, "%s · %u", nm, a->handle);
            lv_obj_t *l = aos_label(box, t, aos_font_caption, AOS_C_DIM);
            lv_obj_set_width(l, iw - 14);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            G.val_l[i] = aos_label(box, "", &aos_montserrat_14, AOS_C_TEXT);
            lv_obj_set_width(G.val_l[i], iw - 14);
            lv_label_set_long_mode(G.val_l[i], LV_LABEL_LONG_MODE_WRAP);
            lv_obj_add_flag(G.val_l[i], LV_OBJ_FLAG_HIDDEN);
            val_show(i);
        }
    }
}

static void log_show(void)
{
    if (!G.log || !G.log_dirty) return;
    G.log_dirty = false;
    char t[LOG_MAX * 100];
    size_t k = 0;
    t[0] = 0;
    for (int i = G.nlog - 1; i >= 0 && k < sizeof t; i--) k += snprintf(t + k, sizeof t - k, "%s%s", k ? "\n" : "", G.logs[i]);
    lv_label_set_text(G.log, k ? t : _("Nada todavía."));
}

static void status_show(void)
{
    if (!G.st) return;
    char t[160], s[160] = "";
    switch (G.state) {
    case AOS_BLE_GATT_CONNECTING: snprintf(t, sizeof t, "%s", _("Conectando...")); break;
    case AOS_BLE_GATT_DISCOVERING: snprintf(t, sizeof t, "%s", _("Conectado, listando servicios...")); break;
    case AOS_BLE_GATT_READY: {
        int8_t r;
        int ns = 0, nc = 0;
        for (int i = 0; i < G.n; i++) { ns += G.at[i].kind == AOS_BLE_ATTR_SERVICE; nc += G.at[i].kind == AOS_BLE_ATTR_CHAR; }
        snprintf(t, sizeof t, _("Conectado · %d servicios, %d características"), ns, nc);
        if (aos_hal_ble_gatt_rssi(&r)) snprintf(s, sizeof s, _("Señal %d dBm · MTU %u"), r, aos_hal_ble_gatt_mtu());
        else snprintf(s, sizeof s, "MTU %u", aos_hal_ble_gatt_mtu());
        if (G.rq_i < G.rq_n) {
            size_t k = strlen(s);
            snprintf(s + k, sizeof s - k, _(" · leyendo %d de %d"), G.rq_i + 1, G.rq_n);
        }
        break;
    }
    case AOS_BLE_GATT_FAILED:
        snprintf(t, sizeof t, "%s", _("Sin conexión"));
        snprintf(s, sizeof s, "%s (%d)", _(reason_text(G.reason)), G.reason);
        break;
    default: snprintf(t, sizeof t, "%s", _("Desconectado")); break;
    }
    lv_label_set_text(G.st, t);
    lv_label_set_text(G.st_sub, s);
}

void bl_gatt_refresh(void)
{
    if (!G.open) return;
    int reason = 0;
    int st = aos_hal_ble_gatt_state(&reason);
    if (st != G.state || reason != G.reason) {
        int was = G.state;
        G.state = st;
        G.reason = reason;
        if (st == AOS_BLE_GATT_READY && was != AOS_BLE_GATT_READY) {
            G.n = aos_hal_ble_gatt_attrs(G.at, ATTRS_MAX);
            log_add("%s", _("servicios listados"), NULL);
        }
        if (st == AOS_BLE_GATT_FAILED) {
            log_add("%s %s", _("sin conexión:"), _(reason_text(reason)));
            for (int i = 0; i < G.n; i++) G.v[i].sub = false;
            G.rq_n = G.rq_i = 0;
        }
        if (G.col && ((st == AOS_BLE_GATT_READY) != G.built_ready)) {
            bl_rebuild();
            return;
        }
    }
    static aos_ble_gatt_ev_t ev[8];
    int n;
    while ((n = aos_hal_ble_gatt_events(ev, 8)) > 0) {
        for (int k = 0; k < n; k++) {
            const aos_ble_gatt_ev_t *e = &ev[k];
            int i = attr_index(e->handle);
            char nm[64] = "?";
            if (i >= 0) attr_name(&G.at[i], nm, sizeof nm);
            if (e->type == AOS_BLE_EV_READ || e->type == AOS_BLE_EV_NOTIFY || e->type == AOS_BLE_EV_INDICATE) {
                if (i >= 0) {
                    attr_val_t *v = &G.v[i];
                    v->status = e->status;
                    v->full = e->len;
                    v->len = e->len > sizeof v->val ? sizeof v->val : e->len;
                    memcpy(v->val, e->data, v->len);
                    v->t = e->t_ms;
                    v->read_once = true;
                    if (e->type != AOS_BLE_EV_READ) v->count++;
                    val_show(i);
                }
                if (e->type == AOS_BLE_EV_READ) {
                    if (e->status) log_add(_("leer %s: %s"), nm, _(att_error(e->status)));
                    if (G.rq_wait && G.rq_i < G.rq_n && G.rq[G.rq_i] == e->handle) {
                        G.rq_wait = false;
                        G.rq_i++;
                    }
                }
            } else if (e->type == AOS_BLE_EV_SUBSCRIBE) {
                bool on = e->status == 0 && e->len && e->data[0];
                if (i >= 0) {
                    G.v[i].sub = on;
                    if (G.sub_b[i]) {
                        lv_obj_set_style_bg_color(G.sub_b[i], on ? BL_C : AOS_C_CARD2, 0);
                        lv_label_set_text(lv_obj_get_child(G.sub_b[i], 0),
                                          on ? _("Dejar de escuchar") : (G.at[i].props & 0x10) ? _("Notificaciones") : _("Indicaciones"));
                    }
                }
                if (e->status) log_add(_("suscribir %s: %s"), nm, _(att_error(e->status)));
                else log_add(on ? _("escuchando %s%s") : _("ya no escucha %s%s"), nm, "");
            } else if (e->type == AOS_BLE_EV_WRITE) {
                if (e->status) log_add(_("escribir %s: %s"), nm, _(att_error(e->status)));
                else log_add(_("escritura aceptada en %s%s"), nm, "");
            }
        }
        if (n < 8) break;
    }
    /* the next of "read everything"; a read that never answers is skipped */
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (G.state == AOS_BLE_GATT_READY && G.rq_i < G.rq_n) {
        if (G.rq_wait && now - G.rq_t > 4000) { G.rq_wait = false; G.rq_i++; }
        if (!G.rq_wait && G.rq_i < G.rq_n && aos_hal_ble_gatt_read(G.rq[G.rq_i])) {
            G.rq_wait = true;
            G.rq_t = now;
        }
    }
    if (BL.ticks % 5 == 0) status_show();
    log_show();
}

void bl_gatt_build(lv_obj_t *page)
{
    if (BL.sel < 0) return;
    bl_dev_t *d = &BL.dev[BL.sel];
    G.col = NULL;
    memset(G.val_l, 0, sizeof G.val_l);
    memset(G.sub_b, 0, sizeof G.sub_b);
    if (!G.open) {
        /* first time on this page: connect */
        memset(&G, 0, sizeof G);
        G.open = aos_hal_ble_gatt_connect(d->addr, d->addr_type);
        G.state = G.open ? AOS_BLE_GATT_CONNECTING : AOS_BLE_GATT_IDLE;
        if (G.open) log_add("%s", _("conectando..."), NULL);
    }
    int32_t w = BL.cw, h = lv_obj_get_height(page);
    G.col = bl_column(page, w, h);
    bl_back_bar(G.col, w, bl_dev_name(d), back_cb);
    lv_obj_t *c = bl_vcard(G.col, w, 14, 6);
    G.st = aos_label(c, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(G.st, w - 28);
    lv_label_set_long_mode(G.st, LV_LABEL_LONG_MODE_WRAP);
    G.st_sub = aos_label(c, "", aos_font_caption, AOS_C_DIM);
    lv_obj_set_width(G.st_sub, w - 28);
    lv_label_set_long_mode(G.st_sub, LV_LABEL_LONG_MODE_WRAP);
    G.btns = bl_wrap(c, w - 28, 8);
    bool live = G.state == AOS_BLE_GATT_READY || G.state == AOS_BLE_GATT_CONNECTING || G.state == AOS_BLE_GATT_DISCOVERING;
    if (G.state == AOS_BLE_GATT_READY) bl_pill(G.btns, AOS_SYM_DOWNLOAD, _("Leer todo"), BL_C, read_all_cb, NULL);
    bl_pill(G.btns, live ? AOS_SYM_LAN_DISCONNECT : AOS_SYM_LAN_CONNECT, live ? _("Desconectar") : _("Conectar de nuevo"),
            AOS_C_CARD2, disc_cb, NULL);
    if (!aos_hal_bt_enabled()) bl_bt_off_card(G.col, w);
    G.built_ready = G.state == AOS_BLE_GATT_READY;
    G.list = bl_box(G.col, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(G.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(G.list, 10, 0);
    if (G.built_ready) list_build();
    else if (G.state == AOS_BLE_GATT_CONNECTING || G.state == AOS_BLE_GATT_DISCOVERING)
        bl_caption(G.list, _("Si tarda, puede que el equipo no acepte conexiones o que ya esté conectado a otro (muchos aceptan uno solo)."), w);
    lv_obj_t *lc = bl_vcard(G.col, w, 14, 6);
    aos_label(lc, _("Lo último"), aos_font_body, AOS_C_TEXT);
    G.log = aos_label(lc, "", aos_font_caption, AOS_C_DIM);
    lv_obj_set_width(G.log, w - 28);
    lv_label_set_long_mode(G.log, LV_LABEL_LONG_MODE_WRAP);
    G.log_dirty = true;
    log_show();
    status_show();
}

void bl_gatt_close(void)
{
    if (G.open) aos_hal_ble_gatt_disconnect();
    G.open = false;
    G.state = AOS_BLE_GATT_IDLE;
    G.col = G.st = G.st_sub = G.btns = G.list = G.log = NULL;
    memset(G.val_l, 0, sizeof G.val_l);
    memset(G.sub_b, 0, sizeof G.sub_b);
}

void bl_gatt_gone(void);
void bl_gatt_gone(void)
{
    G.col = G.st = G.st_sub = G.btns = G.list = G.log = NULL;
    memset(G.val_l, 0, sizeof G.val_l);
    memset(G.sub_b, 0, sizeof G.sub_b);
}
