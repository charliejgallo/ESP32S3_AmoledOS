/*
 * BLE - one device, in detail.
 *
 * Who it is (name, what it seems to be, company, address and its kind), its
 * signal (now, the two last minutes, min, mean, max, the estimated
 * distance) and its rhythm (the interval between advertisements, how many
 * were heard, how often the bytes changed), what a sensor or a beacon says,
 * and every AD structure of its advertisement and of its scan response
 * explained line by line, with the raw bytes under them. From here: the
 * finder, the GATT explorer when it takes connections, a star and a name of
 * our own (kept in ble/nombres.txt).
 */
#include "bl.h"
#include "aos_text_safe.h"
#include "bl_crypt.h"

#include <stdio.h>
#include <string.h>

static struct {
    lv_obj_t *col, *rssi, *stats, *graph, *sens, *adv;
    uint32_t changes;
    uint32_t adv_ms;
    lv_obj_t *fav;
} D;

static bl_dev_t *cur(void)
{
    return BL.sel >= 0 && BL.sel < BL.ndev ? &BL.dev[BL.sel] : NULL;
}

static void back_cb(lv_event_t *e) { (void)e; bl_go_tab(); }
static void find_cb(lv_event_t *e) { (void)e; bl_open_finder(BL.sel); }
static void gatt_cb(lv_event_t *e) { (void)e; bl_open_gatt(BL.sel); }

static void fav_cb(lv_event_t *e)
{
    (void)e;
    bl_dev_t *d = cur();
    if (!d) return;
    d->fav = !d->fav;
    bl_names_save();
    bl_pill_set(D.fav, d->fav ? AOS_SYM_STAR : AOS_SYM_STAR_OUTLINE, d->fav ? _("Favorito") : _("Marcar"),
                d->fav ? lv_color_hex(0x8A6D00) : AOS_C_CARD2);
}

static void alias_done(const char *v)
{
    bl_dev_t *d = cur();
    if (!d) return;
    snprintf(d->alias, sizeof d->alias, "%.27s", v);
    bl_utf8_trim(d->alias);
    for (char *p = d->alias; *p; p++) if (*p == '|' || *p == '\n') *p = ' ';
    bl_names_save();
    bl_rebuild();
}

static void alias_cb(lv_event_t *e)
{
    (void)e;
    bl_dev_t *d = cur();
    if (d) bl_text_entry(_("Un nombre para este equipo"), d->alias[0] ? d->alias : d->ad.name, false, alias_done);
}

static void graph_draw(lv_event_t *e)
{
    bl_dev_t *d = cur();
    if (!d) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_layer_t *layer = lv_event_get_layer(e);
    static const int G[] = { -50, -70, -90 };
    for (int i = 0; i < 3; i++) {
        int32_t y = a.y2 - (int32_t)((G[i] + 100) / 70.0f * lv_area_get_height(&a));
        bl_draw_line(layer, a.x1, y, a.x2, y, AOS_C_DIM, 1, LV_OPA_30);
        char t[8];
        snprintf(t, sizeof t, "%d", G[i]);
        bl_draw_text(layer, t, aos_font_tiny, AOS_C_DIM, a.x1 + 2, y - 15, 40, LV_TEXT_ALIGN_LEFT);
    }
    uint32_t now_s = (uint32_t)(aos_hal_uptime_ms() / 1000);
    bl_spark(layer, &a, d->hist, BL_HIST, (int)now_s, BL_C);
}

/* one packet: its lines explained, and its bytes */
static void packet_card(lv_obj_t *col, int32_t w, const char *title, const uint8_t *data, int len)
{
    lv_obj_t *c = bl_vcard(col, w, 14, 6);
    char t[64];
    snprintf(t, sizeof t, "%s · %d %s", title, len, _("bytes"));
    aos_label(c, t, aos_font_body, AOS_C_TEXT);
    static bl_line_t lines[24];
    int n = bl_explain(data, len, lines, 24, aos_tr);
    for (int i = 0; i < n; i++) {
        /* a name or a URL from the air: whatever it carries, drawable */
        char v[sizeof lines[0].val + 16];
        aos_text_safe(v, sizeof v, lines[i].val);
        bl_kv(c, w - 28, lines[i].key, v);
    }
    /* its key opens what the lines above call encrypted */
    bl_dev_t *d = cur();
    if (d && d->has_key) {
        static bl_ad_t one;
        bl_ad_clear(&one);
        bl_ad_merge(&one, data, len);
        bl_sensor_t s;
        int r = bl_sensor_decode_key(&one, d->addr, d->key, &s);
        if (r == BL_KEY_OK) {
            char l[120];
            bl_sensor_line(&s, l, sizeof l);
            bl_kv(c, w - 28, _("Con la clave"), l[0] ? l : _("sin lecturas en este paquete"));
        } else if (r == BL_KEY_WRONG) {
            bl_kv(c, w - 28, _("Con la clave"), _("no coincide"));
        }
    }
    char hex[31 * 3 + 4];
    bl_hex(data, len, hex, sizeof hex);
    lv_obj_t *h = aos_label(c, hex, &aos_montserrat_14, AOS_C_DIM);
    lv_obj_set_width(h, w - 28);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
}

static void adv_fill(void)
{
    bl_dev_t *d = cur();
    if (!D.adv || !d) return;
    lv_obj_clean(D.adv);
    int32_t w = BL.cw;
    if (d->adv_len) packet_card(D.adv, w, _("Anuncio"), d->adv, d->adv_len);
    if (d->rsp_len) packet_card(D.adv, w, _("Respuesta al escaneo"), d->rsp, d->rsp_len);
    if (!d->adv_len && !d->rsp_len) bl_caption(D.adv, _("Todavía no se lo oyó esta vez."), w);
    D.changes = d->n_changes;
    D.adv_ms = (uint32_t)aos_hal_uptime_ms();
}

/* ---- the key of a device that encrypts ---- */

static void sens_fill(void);

static void key_done(const char *v)
{
    uint8_t k[16];
    if (!bl_parse_key(v, k)) {
        aos_ui_toast(_("Tienen que ser 32 cifras hexadecimales"), 2000);
        return;
    }
    bl_key_set(BL.sel, k);
    sens_fill();
}

static void key_cb(lv_event_t *e)
{
    (void)e;
    bl_text_entry(_("La clave del equipo (32 cifras)"), "", false, key_done);
}

static void key_del_cb(lv_event_t *e)
{
    (void)e;
    bl_key_set(BL.sel, NULL);
    sens_fill();
}

static void key_card(lv_obj_t *parent, int32_t w, const bl_dev_t *d)
{
    const char *st;
    lv_color_t c = AOS_C_DIM;
    if (!d->has_key) st = _("Cifra sus lecturas. Con su clave (en Xiaomi, la «bindkey»: 32 cifras hexadecimales) la app las descifra.");
    else if (d->key_state == BL_KEY_OK) { st = _("Clave correcta: las lecturas se descifran."); c = AOS_C_GREEN; }
    else if (d->key_state == BL_KEY_WRONG) { st = _("La clave no coincide con lo que anuncia."); c = AOS_C_ORANGE; }
    else if (d->key_state == BL_KEY_UNSUPPORTED) { st = _("Usa un cifrado viejo (MiBeacon v2/v3) que la app no descifra."); c = AOS_C_ORANGE; }
    else st = _("Clave cargada: esperando un paquete cifrado para probarla.");
    lv_obj_t *l = bl_caption(parent, st, w);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_t *b = bl_wrap(parent, w, 8);
    bl_pill(b, AOS_SYM_LOCK_OPEN_VARIANT, d->has_key ? _("Cambiar la clave") : _("Cargar la clave"), BL_C, key_cb, NULL);
    if (d->has_key) bl_pill(b, AOS_SYM_DELETE, _("Borrar la clave"), AOS_C_CARD2, key_del_cb, NULL);
    if (!d->has_key)
        bl_caption(parent, _("La bindkey de un sensor Xiaomi sale de su cuenta de Mi Home (por ejemplo con «Xiaomi Cloud Tokens Extractor»), o de Home Assistant si ya lo lee. También se puede escribir en ble/claves.txt de la tarjeta, desde el portal."), w);
}

static void sens_fill(void)
{
    bl_dev_t *d = cur();
    if (!D.sens || !d) return;
    lv_obj_clean(D.sens);
    int32_t w = BL.cw - 28;
    char v[64], t[64];
    if (d->has_sen) {
        const bl_sensor_t *s = &d->sen;
        if (d->has_key && d->key_state == BL_KEY_OK)
            snprintf(t, sizeof t, "%s · %s · %s", _("Sensor"), s->format ? s->format : "", _("descifrado"));
        else snprintf(t, sizeof t, "%s · %s", _("Sensor"), s->format ? s->format : "");
        aos_label(D.sens, t, aos_font_body, AOS_C_TEXT);
        if (s->encrypted) bl_kv(D.sens, w, _("Datos"), _("cifrados: hace falta la clave del equipo"));
        if (s->encrypted || d->has_key) key_card(D.sens, w, d);
#define KV(bit, key, fmtv) if (s->mask & (bit)) { fmtv; bl_kv(D.sens, w, key, v); }
        char n[24];
        KV(BL_V_TEMP, _("Temperatura"), (bl_fmt_num(n, sizeof n, s->temp, 2), snprintf(v, sizeof v, "%s °C", n)));
        KV(BL_V_HUM, _("Humedad"), (bl_fmt_num(n, sizeof n, s->hum, 1), snprintf(v, sizeof v, "%s %%", n)));
        KV(BL_V_PRESS, _("Presión"), (bl_fmt_num(n, sizeof n, s->press, 1), snprintf(v, sizeof v, "%s hPa", n)));
        KV(BL_V_DEW, _("Punto de rocío"), (bl_fmt_num(n, sizeof n, s->dew, 1), snprintf(v, sizeof v, "%s °C", n)));
        KV(BL_V_CO2, "CO2", snprintf(v, sizeof v, "%d ppm", (int)s->co2));
        KV(BL_V_PM25, "PM2,5", snprintf(v, sizeof v, "%d µg/m³", (int)s->pm25));
        KV(BL_V_TVOC, "TVOC", snprintf(v, sizeof v, "%d ppb", (int)s->tvoc));
        KV(BL_V_LUX, _("Luz"), snprintf(v, sizeof v, "%d lx", (int)s->lux));
        KV(BL_V_MOIST, _("Humedad del suelo"), snprintf(v, sizeof v, "%d %%", (int)s->moist));
        KV(BL_V_COND, _("Conductividad"), snprintf(v, sizeof v, "%d µS/cm", (int)s->cond));
        KV(BL_V_WEIGHT, _("Peso"), (bl_fmt_num(n, sizeof n, s->weight, 2), snprintf(v, sizeof v, "%s kg", n)));
        KV(BL_V_POWER, _("Potencia"), (bl_fmt_num(n, sizeof n, s->power, 1), snprintf(v, sizeof v, "%s W", n)));
        KV(BL_V_ENERGY, _("Energía"), (bl_fmt_num(n, sizeof n, s->energy, 3), snprintf(v, sizeof v, "%s kWh", n)));
        KV(BL_V_DIST, _("Distancia"), snprintf(v, sizeof v, "%d mm", (int)s->dist));
        KV(BL_V_ROTATION, _("Giro"), (bl_fmt_num(n, sizeof n, s->rotation, 1), snprintf(v, sizeof v, "%s°", n)));
        KV(BL_V_OPEN, _("Abertura"), snprintf(v, sizeof v, "%s", s->open ? _("abierta") : _("cerrada")));
        KV(BL_V_MOTION, _("Movimiento"), snprintf(v, sizeof v, "%s", s->motion ? _("sí") : _("no")));
        KV(BL_V_BUTTON, _("Botón"), snprintf(v, sizeof v, _("evento %d"), s->button));
        KV(BL_V_HR, _("Pulso"), snprintf(v, sizeof v, "%d lpm", s->hr));
        if (s->mask & BL_V_ACC) {
            char ny[16], nz[16];
            bl_fmt_num(n, sizeof n, s->acc_x, 2);
            bl_fmt_num(ny, sizeof ny, s->acc_y, 2);
            bl_fmt_num(nz, sizeof nz, s->acc_z, 2);
            snprintf(v, sizeof v, "x %s · y %s · z %s g", n, ny, nz);
            bl_kv(D.sens, w, _("Aceleración"), v);
        }
        KV(BL_V_BATT, _("Batería"), snprintf(v, sizeof v, "%d %%", s->batt));
        KV(BL_V_VOLT, _("Tensión"), (bl_fmt_num(n, sizeof n, s->volt, 3), snprintf(v, sizeof v, "%s V", n)));
        KV(BL_V_COUNT, _("Contador"), snprintf(v, sizeof v, "%d", s->count));
#undef KV
    }
    if (d->has_bc) {
        const bl_beacon_t *b = &d->bc;
        static const char *const K[] = { "", "iBeacon", "AltBeacon", "Eddystone UID", "Eddystone URL", "Eddystone TLM", "Eddystone EID" };
        snprintf(t, sizeof t, "%s · %s", _("Baliza"), b->kind < 7 ? K[b->kind] : "?");
        aos_label(D.sens, t, aos_font_body, AOS_C_TEXT);
        char u[48];
        if (b->kind == BL_BEACON_IBEACON || b->kind == BL_BEACON_ALT) {
            bl_uuid_str(b->uuid, 16, u, sizeof u);
            bl_kv(D.sens, w, "UUID", u);
            snprintf(v, sizeof v, "%u / %u", b->major, b->minor);
            bl_kv(D.sens, w, _("Mayor / menor"), v);
        } else if (b->kind == BL_BEACON_EDDY_UID) {
            bl_hex(b->uuid, 10, u, sizeof u);
            bl_kv(D.sens, w, _("Espacio"), u);
            bl_hex(b->uuid + 10, 6, u, sizeof u);
            bl_kv(D.sens, w, _("Instancia"), u);
        }
        if (b->url[0]) bl_kv(D.sens, w, "URL", b->url);
        snprintf(v, sizeof v, "%d dBm", b->tx1m);
        bl_kv(D.sens, w, b->kind == BL_BEACON_IBEACON || b->kind == BL_BEACON_ALT ? _("Potencia a 1 m") : _("Potencia a 0 m"), v);
    }
    if (!d->has_sen && !d->has_bc) lv_obj_add_flag(D.sens, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(D.sens, LV_OBJ_FLAG_HIDDEN);
}

void bl_detail_refresh(void)
{
    bl_dev_t *d = cur();
    if (!D.col || !d) return;
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    bool alive = bl_alive(d);
    char t[48];
    if (now - d->last_ms < 10000) snprintf(t, sizeof t, "%d dBm", d->rssi);
    else snprintf(t, sizeof t, "--");
    lv_label_set_text(D.rssi, t);
    lv_obj_set_style_text_color(D.rssi, alive ? bl_rssi_color(d->rssi) : AOS_C_DIM, 0);
    char s[400], dist[16], avg[16], itv[24], age[24], seen[24];
    int p1m = bl_p1m_guess(&d->ad, d->has_bc ? &d->bc : NULL);
    float m = bl_distance_m(lroundf_safe(d->rssi_avg), p1m, bl_env_n());
    bl_fmt_num(dist, sizeof dist, m, m < 10 ? 1 : 0);
    bl_fmt_num(avg, sizeof avg, d->rssi_avg, 0);
    if (d->itvl_ms > 0) {
        if (d->itvl_ms < 1000) snprintf(itv, sizeof itv, "%d ms", (int)d->itvl_ms);
        else { char n[16]; bl_fmt_num(n, sizeof n, d->itvl_ms / 1000, 1); snprintf(itv, sizeof itv, "%s s", n); }
    } else {
        snprintf(itv, sizeof itv, "?");
    }
    bl_fmt_age(age, sizeof age, now - d->last_ms);
    bl_fmt_age(seen, sizeof seen, now - d->first_ms);
    snprintf(s, sizeof s,
             _("Mín. %d · prom. %s · máx. %d dBm · ~%s m\nCada %s · %u anuncios, %u respuestas · cambió %u veces\nOído %s · desde hace %s"),
             d->rssi_min > 0 ? 0 : d->rssi_min, avg, d->rssi_max < -126 ? 0 : d->rssi_max, dist, itv, (unsigned)d->n_adv,
             (unsigned)d->n_rsp, (unsigned)d->n_changes, age, seen);
    lv_label_set_text(D.stats, s);
    lv_obj_invalidate(D.graph);
    /* the explanations again when the bytes changed, at most every 3 s: some
     * devices change them in every packet */
    if (d->n_changes != D.changes && now - D.adv_ms > 3000) {
        int32_t sy = lv_obj_get_scroll_y(D.col);
        adv_fill();
        sens_fill();
        lv_obj_update_layout(D.col);
        lv_obj_scroll_to_y(D.col, sy, LV_ANIM_OFF);
    }
}

void bl_detail_build(lv_obj_t *page)
{
    memset(&D, 0, sizeof D);
    bl_dev_t *d = cur();
    if (!d) return;
    int32_t w = BL.cw, h = lv_obj_get_height(page);
    D.col = bl_column(page, w, h);
    bl_back_bar(D.col, w, bl_dev_name(d), back_cb);

    /* who */
    lv_obj_t *c = bl_vcard(D.col, w, 14, 8);
    lv_obj_t *r = bl_row(c, w - 28, 56, 10);
    bl_round_icon(r, bl_class_glyph(d->cls), bl_class_color(d->cls), 56);
    lv_obj_t *who = bl_box(r, w - 28 - 56 - 10, 56);
    char t[160], a[20];
    const char *what = d->label ? _(d->label) : _(bl_class_name(d->cls));
    lv_obj_t *l = aos_label(who, what, aos_font_body, AOS_C_TEXT);
    lv_obj_set_size(l, w - 28 - 56 - 10, lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    bl_fmt_addr(d->addr, a, sizeof a);
    snprintf(t, sizeof t, "%s · %s", a, _(bl_addr_kind_name(bl_addr_kind(d->addr, d->addr_type))));
    l = aos_label(who, t, aos_font_caption, AOS_C_DIM);
    lv_obj_set_size(l, w - 28 - 56 - 10, lv_font_get_line_height(aos_font_caption));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_y(l, 30);
    if (d->ad.nmfg) {
        const char *co = bl_company_name(d->ad.mfg[0].company);
        char cs[64];
        if (co) snprintf(cs, sizeof cs, "%.48s (0x%04X)", co, d->ad.mfg[0].company);
        else snprintf(cs, sizeof cs, "%s 0x%04X", _("Compañía"), d->ad.mfg[0].company);
        bl_kv(c, w - 28, _("Fabricante"), cs);
    }
    if (d->ad.name[0] && d->alias[0]) {
        char nm[64];
        aos_text_safe(nm, sizeof nm, d->ad.name);
        bl_kv(c, w - 28, _("Nombre propio"), nm);
    }
    if (bl_addr_kind(d->addr, d->addr_type) == BL_ADDR_RPA)
        bl_caption(c, _("Su dirección es privada y cambia cada pocos minutos: cuando cambie, va a aparecer como otro equipo."), w - 28);
    lv_obj_t *b = bl_wrap(c, w - 28, 8);
    bl_pill(b, AOS_SYM_MAGNIFY, _("Buscar"), BL_C, find_cb, NULL);
    bool conn = (d->kinds & ((1u << AOS_BLE_ADV_IND) | (1u << AOS_BLE_ADV_DIRECT_IND))) != 0;
    if (conn) bl_pill(b, AOS_SYM_LAN_CONNECT, _("Conectar"), lv_color_hex(0x5E5CE6), gatt_cb, NULL);
    D.fav = bl_pill(b, d->fav ? AOS_SYM_STAR : AOS_SYM_STAR_OUTLINE, d->fav ? _("Favorito") : _("Marcar"),
                    d->fav ? lv_color_hex(0x8A6D00) : AOS_C_CARD2, fav_cb, NULL);
    bl_pill(b, AOS_SYM_PENCIL, _("Nombre"), AOS_C_CARD2, alias_cb, NULL);

    /* the signal */
    c = bl_vcard(D.col, w, 14, 6);
    r = bl_row(c, w - 28, 36, 8);
    aos_label(r, _("Señal"), aos_font_body, AOS_C_TEXT);
    lv_obj_t *sp = bl_box(r, 10, 10);
    lv_obj_set_flex_grow(sp, 1);
    D.rssi = aos_label(r, "", aos_font_title, AOS_C_TEXT);
    D.graph = bl_box(c, w - 28, 90);
    lv_obj_add_event_cb(D.graph, graph_draw, LV_EVENT_DRAW_MAIN, NULL);
    D.stats = aos_label(c, "", aos_font_caption, AOS_C_DIM);
    lv_obj_set_width(D.stats, w - 28);
    lv_label_set_long_mode(D.stats, LV_LABEL_LONG_MODE_WRAP);

    /* a sensor's or a beacon's */
    D.sens = bl_vcard(D.col, w, 14, 6);
    sens_fill();

    /* the packets */
    D.adv = bl_box(D.col, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(D.adv, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(D.adv, 10, 0);
    adv_fill();
    bl_detail_refresh();
}

void bl_detail_gone(void);
void bl_detail_gone(void)
{
    memset(&D, 0, sizeof D);
}
