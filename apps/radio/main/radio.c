/*
 * AmoledOS - Radio. See radio.h for the arrangement, radio_ui.c for the
 * panel and radio_art.c for the covers; this file is what the panel does.
 *
 * The app owns no sound and no connection: it hands its nine keys to
 * aos_hal_radio_play() and reads back aos_hal_player_info() (what is heard)
 * and aos_hal_radio_status() (how the stream is doing) five times a second.
 * So opening the app while a station plays just shows it, closing it leaves
 * it playing, and the control centre's next/previous walk the same keys.
 */
#include "radio.h"

#include "aos_app.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* A sixties table radio: a handle, the cabinet, a round grille on the left,
 * a lit dial and two knobs on the right. */
static const uint8_t RADIO_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, 0, -17, 34, 16, 5, AIC_C_TEXT, 0),               /* handle  */
    AIC_BORDER(AIC_DIV(24), AIC_C_TEXT, 255),
    AIC_RECT(AIC_CENTER, 0, 7, 66, 44, 9, AIC_C_TEXT, 255),               /* cabinet */
    AIC_INTO,
    AIC_RECT(AIC_LEFT_MID, 6, 0, 26, 26, AIC_CIRCLE, AIC_C_LIT(0x3A2410), 255),
    AIC_RECT(AIC_TOP_RIGHT, -6, 7, 24, 9, 2, AIC_C_ORANGE, 255),          /* dial    */
    AIC_RECT(AIC_BOTTOM_RIGHT, -21, -6, 8, 8, AIC_CIRCLE, AIC_C_LIT(0x3A2410), 255),
    AIC_RECT(AIC_BOTTOM_RIGHT, -8, -6, 8, 8, AIC_CIRCLE, AIC_C_LIT(0x3A2410), 255),
    AIC_OUT,
    AIC_END
};


/* A bounded copy that does not cut a UTF-8 character in half, and that the
 * board's GCC does not take for a truncated snprintf (an error there). */
static void txt_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap - 1);
    if (n == cap - 1) {
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---- the keys ----------------------------------------------------------------- */

static int32_t keys_gen(void)
{
    int32_t g = 0;
    aos_hal_pref_get_i32("rad_gen", &g);
    return g;
}

static void keys_load(radio_t *r)
{
    memset(r->st, 0, sizeof(r->st));
    for (int i = 0; i < RADIO_KEYS; i++) {
        char key[8], raw[sizeof(r->st[0].name) + sizeof(r->st[0].url) + 4];
        snprintf(key, sizeof(key), "rad%d", i);
        if (!aos_hal_pref_get_str(key, raw, sizeof(raw)) || !raw[0]) {
            continue;
        }
        char *sep = strchr(raw, '\x1f');
        if (!sep) {
            continue;
        }
        *sep = '\0';
        txt_copy(r->st[i].name, sizeof(r->st[i].name), raw);
        txt_copy(r->st[i].url, sizeof(r->st[i].url), sep + 1);
    }
    r->gen = keys_gen();
}

static bool any_key(radio_t *r)
{
    for (int i = 0; i < RADIO_KEYS; i++) {
        if (r->st[i].url[0]) {
            return true;
        }
    }
    return false;
}

/* Which of our keys is the station the player has, -1 if none (a URL the
 * portal was testing, or the keys changed under it). */
static int active_key(radio_t *r, const aos_radio_status_t *rs)
{
    if (rs->index >= 0 && rs->index < RADIO_KEYS && rs->url[0] &&
        strcmp(rs->url, r->st[rs->index].url) == 0) {
        return rs->index;
    }
    for (int i = 0; i < RADIO_KEYS; i++) {
        if (rs->url[0] && strcmp(rs->url, r->st[i].url) == 0) {
            return i;
        }
    }
    return -1;
}

static int last_key(radio_t *r)
{
    int32_t last = 0;
    aos_hal_pref_get_i32("rad_last", &last);
    if (last >= 0 && last < RADIO_KEYS && r->st[last].url[0]) {
        return (int)last;
    }
    for (int i = 0; i < RADIO_KEYS; i++) {
        if (r->st[i].url[0]) {
            return i;
        }
    }
    return -1;
}

static void hint_portal(radio_t *r, int key)
{
    (void)r;
    char msg[160];
    if (key >= 0) {
        snprintf(msg, sizeof(msg), _("La tecla %d está libre: elegí una radio en %s.local/radio"),
                 key + 1, aos_hal_device_name());
    } else {
        snprintf(msg, sizeof(msg), _("Elegí tus radios en %s.local/radio"), aos_hal_device_name());
    }
    aos_ui_toast(msg, 3500);
}

static void play_key(radio_t *r, int i)
{
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        aos_ui_toast(_("Sin WiFi"), 2000);
        return;
    }
    if (aos_hal_radio_play(r->st, RADIO_KEYS, i)) {
        aos_hal_pref_set_i32("rad_last", i);
        radio_ui_needle_to(r, i);
    }
}

/* ---- what the panel does ---------------------------------------------------------- */

void radio_on_key(radio_t *r, int i)
{
    if (!r->st[i].url[0]) {
        hint_portal(r, i);
        return;
    }
    aos_player_info_t pi;
    aos_radio_status_t rs;
    aos_hal_player_info(&pi);
    aos_hal_radio_status(&rs);
    if (pi.live && pi.state != AOS_PLAYER_STOPPED && active_key(r, &rs) == i) {
        if (pi.state == AOS_PLAYER_PAUSED) {
            aos_hal_player_resume();
        }
        return;                         /* already on it, like a real key */
    }
    play_key(r, i);
}

void radio_on_play(radio_t *r)
{
    aos_player_info_t pi;
    aos_hal_player_info(&pi);
    if (pi.live && pi.state == AOS_PLAYER_PLAYING) {
        aos_hal_player_pause();
    } else if (pi.live && pi.state == AOS_PLAYER_PAUSED) {
        aos_hal_player_resume();
    } else {
        int k = last_key(r);
        if (k < 0) {
            hint_portal(r, -1);
        } else {
            play_key(r, k);
        }
    }
}

void radio_on_step(radio_t *r, int step)
{
    aos_player_info_t pi;
    aos_hal_player_info(&pi);
    if (pi.live) {
        if (step > 0) {
            aos_hal_player_next();
        } else {
            aos_hal_player_prev();
        }
        return;
    }
    int k = last_key(r);
    if (k < 0) {
        hint_portal(r, -1);
        return;
    }
    for (int n = 1; n <= RADIO_KEYS; n++) {
        int i = ((k + step * n) % RADIO_KEYS + RADIO_KEYS) % RADIO_KEYS;
        if (r->st[i].url[0]) {
            play_key(r, i);
            return;
        }
    }
}

void radio_on_mute(radio_t *r)
{
    (void)r;
    int v = aos_hal_volume_get();
    if (v > 0) {
        aos_hal_pref_set_i32("rad_unmute", v);
        aos_hal_volume_set(0);
    } else {
        int32_t back = 60;
        aos_hal_pref_get_i32("rad_unmute", &back);
        aos_hal_volume_set(back > 0 ? (int)back : 60);
    }
}

void radio_on_volume(radio_t *r, int v)
{
    (void)r;
    aos_hal_volume_set(v);
}

/* ---- words ------------------------------------------------------------------------ */

/* The HAL says why in English, for the logs; the panel says it in the
 * watch's language. The raw reason is in the info card. */
static const char *why(const char *error)
{
    if (strstr(error, "only MP3") || strstr(error, "HLS")) {
        return _("Formato no soportado: sólo MP3");
    }
    if (strstr(error, "404") || strstr(error, "410") || strstr(error, "403")) {
        return _("La radio no está en esa dirección");
    }
    if (strstr(error, "no clock")) {
        return _("Falta la hora para https");
    }
    if (strstr(error, "resolve")) {
        return _("No se encontró el servidor");
    }
    if (strstr(error, "connect")) {
        return _("No se pudo conectar");
    }
    if (strstr(error, "memory")) {
        return _("Sin memoria");
    }
    return _("No se pudo sintonizar");
}

static void set_text(lv_obj_t *label, char *shown, size_t cap, const char *now)
{
    if (strncmp(shown, now, cap - 1) != 0) {
        snprintf(shown, cap, "%s", now);
        lv_label_set_text(label, now);
    }
}

static bool real_genre(const char *g)
{
    return g[0] && strcasecmp(g, "genre") != 0 && strcasecmp(g, "various") != 0 &&
           strcasecmp(g, "misc") != 0;
}

void radio_info_text(radio_t *r, char *out, int cap)
{
    aos_player_info_t pi;
    aos_radio_status_t rs;
    aos_hal_player_info(&pi);
    aos_hal_radio_status(&rs);
    int n = 0;
#define ADD(...) do { if (n < cap) n += snprintf(out + n, (size_t)(cap - n), __VA_ARGS__); } while (0)
    if (!pi.live) {
        ADD("%s", _("No suena ninguna radio."));
        return;
    }
    ADD("%s: %s\n", _("Tema"), rs.title[0] ? rs.title : "-");
    if (r->art.have && r->art.album[0]) {
        ADD("%s: %s\n", _("Álbum"), r->art.album);
    }
    if (rs.icy_name[0] || real_genre(rs.icy_genre)) {
        ADD("%s: %s%s%s\n", _("La emisora dice"), rs.icy_name,
            rs.icy_name[0] && real_genre(rs.icy_genre) ? " - " : "",
            real_genre(rs.icy_genre) ? rs.icy_genre : "");
    }
    if (rs.sample_rate) {
        ADD("%s: MP3 %u kbps, %u.%u kHz, %s\n", _("Formato"), (unsigned)rs.kbps,
            (unsigned)(rs.sample_rate / 1000), (unsigned)(rs.sample_rate % 1000 / 100),
            rs.channels == 2 ? _("estéreo") : _("mono"));
    }
    ADD("%s: %s%s\n", _("Servidor"), rs.host, rs.tls ? " (https)" : "");
    if (rs.icy_url[0]) {
        ADD("%s: %s\n", _("Web"), rs.icy_url);
    }
    ADD("%s: %u.%u s   %s: %u\n", _("Buffer"), (unsigned)(rs.buffer_ms / 1000),
        (unsigned)(rs.buffer_ms % 1000 / 100), _("Reconexiones"), (unsigned)rs.reconnects);
    ADD("%s: %u:%02u   %s: %u.%u MB\n", _("Escuchando"), (unsigned)(rs.listening_s / 60),
        (unsigned)(rs.listening_s % 60), _("Recibido"), (unsigned)(rs.bytes / 1000000),
        (unsigned)(rs.bytes % 1000000 / 100000));
    if (r->art.have) {
        ADD("%s: iTunes\n", _("Tapa"));
    }
    if (rs.error[0] && (rs.state == AOS_RADIO_FAILED || rs.state == AOS_RADIO_RETRYING)) {
        ADD("%s: %s\n", _("Motivo"), rs.error);
    }
#undef ADD
}

/* ---- the tick ------------------------------------------------------------------------ */

static void cover_for(radio_t *r, int active)
{
    int kind = 0;
    if (r->art.have) {
        kind = 2;
    } else if (active >= 0) {
        kind = 1;
    }
    if (kind == r->s_cover_kind && (kind != 1 || active == r->s_cover_slot)) {
        return;
    }
    if (kind == 1) {
        uint16_t *tmp = art_big_alloc(ART_PX * ART_PX * 2);
        bool ok = tmp && art_logo(active, tmp);
        radio_ui_cover(r, ok ? tmp : NULL);
        art_big_free(tmp);
        if (!ok) {
            kind = 0;
        }
    } else if (kind == 2) {
        radio_ui_cover(r, r->art.px);
    } else {
        radio_ui_cover(r, NULL);
    }
    r->s_cover_kind = kind;
    r->s_cover_slot = active;
}

static void radio_tick(aos_app_t *self, void *inst)
{
    (void)self;
    radio_t *r = inst;
    if (!r || r->closing) {
        return;
    }
    if (keys_gen() != r->gen) {
        keys_load(r);
        r->s_active = -2;
        r->s_cover_kind = -1;
    }

    aos_player_info_t pi;
    aos_radio_status_t rs;
    aos_hal_player_info(&pi);
    aos_hal_radio_status(&rs);
    bool live = pi.live;
    bool playing = live && pi.state == AOS_PLAYER_PLAYING;
    bool paused = live && pi.state == AOS_PLAYER_PAUSED;
    int active = live && pi.state != AOS_PLAYER_STOPPED ? active_key(r, &rs) : -1;
    bool net = aos_hal_net_state() == AOS_NET_CONNECTED;

    if (active != r->s_active) {
        r->s_active = active;
        radio_ui_keys(r);
        radio_ui_needle_to(r, active >= 0 ? active : (live ? -1 : last_key(r)));
        if (active >= 0) {
            aos_hal_pref_set_i32("rad_last", active);
        }
    }

    /* the dial */
    char station[48], title[96], artist[96] = "", meta[96] = "";
    int lk = last_key(r);
    if (live && pi.state != AOS_PLAYER_STOPPED) {
        txt_copy(station, sizeof(station), rs.station[0] ? rs.station : pi.album);
    } else if (lk >= 0) {
        txt_copy(station, sizeof(station), r->st[lk].name);
    } else {
        txt_copy(station, sizeof(station), _("Radio"));
    }
    if (!any_key(r)) {
        txt_copy(title, sizeof(title), _("Elegí tus radios en"));
        snprintf(artist, sizeof(artist), "%s.local/radio", aos_hal_device_name());
    } else if (!live || pi.state == AOS_PLAYER_STOPPED) {
        snprintf(title, sizeof(title), "%s",
                 rs.state == AOS_RADIO_FAILED ? why(rs.error) : _("Tocá una tecla"));
    } else if (pi.title[0]) {
        txt_copy(title, sizeof(title), pi.title);
        txt_copy(artist, sizeof(artist), pi.artist);
    } else if (rs.state == AOS_RADIO_CONNECTING || rs.state == AOS_RADIO_RETRYING) {
        txt_copy(title, sizeof(title), _("Sintonizando..."));
    } else if (rs.state == AOS_RADIO_BUFFERING) {
        txt_copy(title, sizeof(title), _("Cargando..."));
    } else {
        txt_copy(title, sizeof(title), rs.icy_name[0] ? rs.icy_name : station);
    }
    if (live && pi.state != AOS_PLAYER_STOPPED && rs.kbps) {
        int n = snprintf(meta, sizeof(meta), "MP3  %u kbps", (unsigned)rs.kbps);
        if (real_genre(rs.icy_genre) && n > 0 && n < (int)sizeof(meta)) {
            snprintf(meta + n, sizeof(meta) - (size_t)n, "  %.40s", rs.icy_genre);
        }
    }
    set_text(r->station, r->s_station, sizeof(r->s_station), station);
    set_text(r->title, r->s_title, sizeof(r->s_title), title);
    set_text(r->artist, r->s_artist, sizeof(r->s_artist), artist);
    set_text(r->meta, r->s_meta, sizeof(r->s_meta), meta);

    int onair = playing && rs.state == AOS_RADIO_PLAYING;
    if (onair != r->s_onair) {
        r->s_onair = onair;
        lv_obj_set_style_bg_color(r->onair, lv_color_hex(onair ? 0xFF3B30 : 0x401010), 0);
    }

    /* the transport and the volume */
    int state = playing ? 1 : (paused ? 2 : 0);
    if (state != r->s_state) {
        r->s_state = state;
        lv_label_set_text(r->i_play, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    int vol = aos_hal_volume_get();
    if (vol != r->s_vol && !lv_obj_has_state(r->vol, LV_STATE_PRESSED)) {
        r->s_vol = vol;
        lv_slider_set_value(r->vol, vol, LV_ANIM_OFF);
        char v[8];
        snprintf(v, sizeof(v), "%d", vol);
        lv_label_set_text(r->vol_lbl, v);
    }
    int muted = vol == 0;
    if (muted != r->s_muted) {
        r->s_muted = muted;
        lv_label_set_text(r->i_mute, muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    }

    /* the line under it all */
    char status[96] = "";
    if (!net) {
        txt_copy(status, sizeof(status), _("Sin WiFi"));
    } else if (live && pi.state != AOS_PLAYER_STOPPED) {
        switch (rs.state) {
        case AOS_RADIO_CONNECTING:
            snprintf(status, sizeof(status), _("Conectando con %.60s"), rs.host);
            break;
        case AOS_RADIO_BUFFERING:
            txt_copy(status, sizeof(status), _("Llenando el buffer"));
            break;
        case AOS_RADIO_RETRYING:
            snprintf(status, sizeof(status), _("Reconectando (%u)"), (unsigned)rs.reconnects);
            break;
        case AOS_RADIO_FAILED:
            txt_copy(status, sizeof(status), why(rs.error));
            break;
        default:
            if (paused) {
                txt_copy(status, sizeof(status), _("En pausa"));
            } else {
                snprintf(status, sizeof(status), _("En vivo  -  buffer %u s"),
                         (unsigned)(rs.buffer_ms / 1000));
            }
            break;
        }
    } else if (!live && pi.state == AOS_PLAYER_PLAYING) {
        txt_copy(status, sizeof(status), _("Suena la música de la tarjeta"));
    }
    set_text(r->status, r->s_status, sizeof(r->s_status), status);

    /* the cover: the song's when it can be found, else the key's logo */
    if (rs.title_gen != r->s_title_gen) {
        r->s_title_gen = rs.title_gen;
        art_request(&r->art, live ? rs.title : "", station);
        r->s_cover_kind = -1;
    }
    art_tick(&r->art);
    if (r->art.fresh) {
        r->art.fresh = false;
        r->s_cover_kind = -1;
    }
    cover_for(r, active >= 0 ? active : (live ? -1 : lk));

    /* the info card, once a second */
    if (r->info && r->info_refresh++ % 5 == 0) {
        static char buf[900];
        radio_info_text(r, buf, sizeof(buf));
        lv_label_set_text(r->info_title, station);
        lv_label_set_text(r->info_text, buf);
    }
}

/* ---- the life cycle ---------------------------------------------------------------------- */

static void *radio_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    radio_t *r = lv_malloc_zeroed(sizeof(radio_t));
    if (!r) {
        return NULL;
    }
    art_init(&r->art);
    keys_load(r);
    radio_ui_build(r, root);
    r->s_title_gen = UINT32_MAX;
    radio_tick(self, r);
    /* the needle starts where it should be, not travelling to it */
    r->needle_x = r->needle_to;
    lv_obj_set_x(r->needle, r->needle_x / 16);
    return r;
}

static void radio_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    radio_t *r = inst;
    if (!r) {
        return;
    }
    r->closing = true;
    if (r->anim) {
        lv_timer_delete(r->anim);
    }
    lv_obj_clean(r->root);          /* the canvases go before their buffers */
    art_free(&r->art);
    art_big_free(r->cover_px);
    art_big_free(r->scale_px);
    art_big_free(r->info_px);
    lv_free(r);
}

static bool radio_init(aos_app_t *app)
{
    app->desc.id       = "aos.radio";
    app->desc.name     = "Radio";
    app->desc.icon     = LV_SYMBOL_AUDIO;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0xFF9F0A;
    app->desc.color_b  = 0x8A4B00;
    app->desc.order    = 64;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN;
    aos_icon_set_ops(app, RADIO_ICON, sizeof RADIO_ICON);

    app->create  = radio_create;
    app->destroy = radio_destroy;
    app->tick    = radio_tick;
    return true;
}

AOS_APP_ENTRY(radio_init);
