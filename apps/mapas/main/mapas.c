/*
 * MAPAS - a street map on the watch.
 *
 * The data are OpenFreeMap's vector tiles (OpenMapTiles schema, from
 * OpenStreetMap), drawn on the watch in a dark style for the AMOLED: no API
 * key, no quota, and the same tiles online, in the card's cache and in the
 * offline packs a zone is downloaded into.
 *
 *   one finger ....... move the map (and a flick keeps it going)
 *   two fingers ...... zoom about the point between them
 *   double tap ....... zoom in there
 *   + / − ............ a level in or out
 *   ≡ ................ zones, offline maps, settings
 *   long press ....... the coordinates of that point
 *
 * How it is built, and why:
 *
 *   - The worker (core 0) renders the map into a buffer BIGGER than the
 *     screen (560 x 640, 96 px of margin all round), two of them in turns.
 *     LVGL's side never waits for a render: every frame it cuts the screen
 *     out of the newest buffer (moved, or scaled while a pinch or a zoom
 *     animation is on) and blits it. A pan costs a copy; the worker renders
 *     a new buffer when the screen nears its edge or the zoom settles.
 *   - Tiles come from RAM, the offline packs, the card's cache or the
 *     network, in that order (mp_store.c). The requests are made from LVGL's
 *     side (the HAL's HTTP wants that); the worker decodes the answers and
 *     writes them to the cache.
 *   - Everything on top of the map (buttons, scale, attribution) is drawn
 *     into the frame: the blit covers the whole screen.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_gesture.h"
#include "aos_fonts.h"
#include "lvgl.h"

#include "mp_draw.h"
#include "mp_render.h"
#include "mp_store.h"
#include "mp_mem.h"
#include "mp_search.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW          AOS_SCREEN_W        /* 368 */
#define SH          AOS_SCREEN_H        /* 448 */
#define MARGIN      96
#define BW          (SW + 2 * MARGIN)   /* 560 */
#define BH          (SH + 2 * MARGIN)   /* 640 */
#define EDGE        12                  /* re-render when the screen is this close to the edge */
#define TICK_MS     16
#define STILL_MS    140
#define WORKER_STACK (16 * 1024)
#define STRIP       16                  /* rows per strip: 28 strips, 11.5 KB each */

#define RAM_BUDGET  (1400u * 1024u)     /* decoded tiles */
#define HTTP_MAX    (1100 * 1024)       /* a tile's ceiling; the densest z14 seen was 0.5 MB */
/* One download at a time. Measured on the board (2026-09-26): a 132 KB tile
 * alone came in at 333 KB/s; two TLS downloads at once crawled at 13 KB/s,
 * one was cut after 20 s, and the WiFi logged bcn_timeout - two 16 KB TCP
 * windows fill the 16 receive buffers faster than TLS empties them, and the
 * beacons are what gets dropped. */
#define MAX_INFLIGHT 1
#define NQ          4
#define MAX_FAILED  16
#define MAX_ZONES   24
#define MAX_HITS    30
#define MAX_OFFLINE 22                  /* of MAX_HITS: room left for Photon's */
#define PEND_MS     1000                /* multi-tap: a second tap within this cycles the letter */

enum { SCR_MAP = 0, SCR_LIST, SCR_KEYS, SCR_RESULTS };
#define TILEJSON    "https://tiles.openfreemap.org/planet"

/* the buttons drawn into the frame */
#define BTN_R       23
#define ZIN_X       (SW - 40)
#define ZIN_Y       186
#define ZOUT_X      (SW - 40)
#define ZOUT_Y      244
#define MENU_X      44
#define MENU_Y      44
#define BTN_HIT     32

enum { Q_FREE = 0, Q_FLIGHT, Q_READY, Q_DONE };

typedef struct {
    volatile int state;
    int          id;
    mp_key_t     k;
    bool         empty;         /* 204/404: a tile with nothing in it */
    uint32_t     t0;            /* when it was asked for */
} net_t;

typedef struct {
    uint16_t *px;
    mp_view_t v;
} back_t;

typedef struct {
    char     name[32];
    uint32_t cx, cy;
    float    z;
} zone_t;

typedef struct {
    aos_app_t  *self;
    lv_obj_t   *root;
    lv_obj_t   *map;            /* the touch surface */
    lv_obj_t   *canvas;         /* the simulator's way to see a frame */
    uint16_t   *cv;
    lv_obj_t   *list;
    lv_timer_t *timer;

    mp_font_t   fonts[MP_FONTS];
    uint16_t   *strip[2];           /* internal, DMA-capable: SW x STRIP */
    int         strip_i;

    /* the worker's buffers */
    back_t          back[2];
    volatile int    front;          /* -1 until the first render */
    volatile int    composing;      /* the buffer LVGL is reading, -1 none */
    volatile uint32_t front_gen;
    volatile bool   dirty_tiles;
    volatile bool   want_rescan, want_clear;
    volatile int    cleared;        /* files the last clear removed, -1 none */
    volatile bool   packs_ready;
    mp_pack_info_t  packs[MP_MAX_PACKS];
    volatile int    npacks;
    uint32_t        renders, t_log;
    float           z_log;

    /* what the last render lacked, for the network (worker writes) */
    mp_key_t        want[MP_MAX_MISSING];
    volatile int    nwant;
    volatile uint32_t want_seq;
    volatile int    missing, stand_in;

    /* the network (LVGL's side) */
    net_t       q[NQ];
    struct { mp_key_t k; uint32_t until; } failed[MAX_FAILED];
    int         nfailed;
    struct { mp_key_t k; uint32_t t; } recent[8];
    int         recent_i;
    char        tpl[192];
    int         tpl_id;
    uint32_t    tpl_retry;
    bool        online;
    int         last_err;
    uint32_t    downloaded;
    uint32_t    busy_ms;
    bool        low_latency;

    /* the view (LVGL's side) */
    mp_view_t   view;
    volatile uint32_t view_seq;
    volatile uint32_t moved_ms;
    float       zt, ax, ay;     /* zoom animation: target and anchor */
    bool        zooming;
    float       vx, vy;         /* fling, px/s */
    bool        touching;
    uint32_t    shown_seq, shown_gen;
    bool        overlay_dirty;
    bool        viewing;

    zone_t      zones[MAX_ZONES];
    int         nzones;

    /* search: the keypad's text, the worker's offline pass, Photon online */
    char        query[40];
    int         pend_key;           /* the key whose letters a new tap cycles, -1 none */
    int         pend_idx;
    uint32_t    pend_t;
    lv_obj_t   *q_label, *q_hint;
    char        skey[40];
    volatile bool want_search, search_cancel, searching;
    volatile uint32_t search_seq;
    uint32_t    search_shown;
    mp_hit_t    hits[MAX_HITS];
    volatile int nhits;
    int         photon_id;
    bool        photon_done;
    int         screen;             /* SCR_* */
    lv_obj_t   *res_list;

    /* the pin of the last place found */
    bool        pin_on;
    uint32_t    pin_cx, pin_cy;
    char        pin_name[MP_HIT_NAME];

    /* goto.txt, the portal's "show it on the watch" (worker reads, LVGL goes) */
    volatile uint32_t goto_seq;
    uint32_t    goto_seen;
    uint32_t    goto_cx, goto_cy;
    float       goto_z;

    /* <card>/maps/bench.txt: a scripted pan and zoom, timed (the portal's
     * injected taps reach LVGL, not the gesture recogniser) */
    bool        bench;
    int         bench_phase;
    bool        bench_zoomed;
    uint32_t    bench_t;
    uint32_t    b_frames, b_compose, b_blit;
    volatile uint32_t b_renders, b_render_us;
} app_t;

static app_t *s_app;

static void *alloc_dma(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#else
    return malloc(n);
#endif
}

static void free_dma(void *p)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
    heap_caps_free(p);
#else
    free(p);
#endif
}

/* ---------------------------------------------------------------------------
 * Places on the card
 * ------------------------------------------------------------------------- */

static void zones_path(char *out, size_t n)
{
    const char *d = mp_maps_dir();
    snprintf(out, n, "%s/zones.txt", d ? d : aos_hal_path_data());
}

/* zones.txt: one per line, tab-separated: name, latitude and longitude in
 * millionths of a degree, zoom in tenths. Lines starting with # are notes.
 * The portal's /mapas page writes it; "Save this view" appends to it. */
static void zones_load(app_t *a)
{
    a->nzones = 0;
    char path[128];
    zones_path(path, sizeof path);
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char *buf = (char *)malloc(8192);
    int n = buf ? (int)fread(buf, 1, 8191, fp) : 0;
    fclose(fp);
    if (!buf) return;
    buf[n] = 0;
    char *line = buf;
    while (*line && a->nzones < MAX_ZONES) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *f[4] = { line, NULL, NULL, NULL };
        for (int i = 1; i < 4; i++) {
            char *t = f[i - 1] ? strchr(f[i - 1], '\t') : NULL;
            if (t) {
                *t = 0;
                f[i] = t + 1;
            }
        }
        if (line[0] != '#' && f[1] && f[2]) {
            zone_t *z = &a->zones[a->nzones];
            snprintf(z->name, sizeof z->name, "%.31s", f[0]);
            char *cr = strchr(z->name, '\r');
            if (cr) *cr = 0;
            long la = strtol(f[1], NULL, 10), lo = strtol(f[2], NULL, 10);
            long zz = f[3] ? strtol(f[3], NULL, 10) : 150;
            if (z->name[0] && la >= -85000000 && la <= 85000000 && lo >= -180000000 && lo <= 180000000) {
                mp_lonlat_to_world((float)lo / 1e6f, (float)la / 1e6f, &z->cx, &z->cy);
                z->z = zz >= 20 && zz <= 185 ? (float)zz / 10.0f : 15.0f;
                a->nzones++;
            }
        }
        if (!end) break;
        line = end + 1;
    }
    free(buf);
}

static bool zone_append(app_t *a, const char *name)
{
    char path[128], line[96];
    zones_path(path, sizeof path);
    float lon, lat;
    mp_world_to_lonlat(a->view.cx, a->view.cy, &lon, &lat);
    FILE *fp = fopen(path, "ab");
    if (!fp) return false;
    int n = snprintf(line, sizeof line, "%s\t%ld\t%ld\t%d\n", name, (long)(lat * 1e6f),
                     (long)(lon * 1e6f), (int)(a->view.z * 10 + 0.5f));
    bool ok = fwrite(line, 1, (size_t)n, fp) == (size_t)n;
    ok = fclose(fp) == 0 && ok;
    return ok;
}

/* goto.txt: latitude, longitude (millionths) and zoom (tenths), tab-separated,
 * written by the /mapas page. Read once and deleted. */
static void goto_check(app_t *a)
{
    const char *d = mp_maps_dir();
    if (!d) return;
    char path[128], buf[64];
    snprintf(path, sizeof path, "%s/goto.txt", d);
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    int n = (int)fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    remove(path);
    if (n <= 0) return;
    buf[n] = 0;
    char *p = buf, *e;
    long la = strtol(p, &e, 10);
    if (e == p) return;
    p = e;
    long lo = strtol(p, &e, 10);
    if (e == p) return;
    p = e;
    long zz = strtol(p, &e, 10);
    if (e == p) zz = 150;
    if (la < -85000000 || la > 85000000 || lo < -180000000 || lo > 180000000) return;
    mp_lonlat_to_world((float)lo / 1e6f, (float)la / 1e6f, &a->goto_cx, &a->goto_cy);
    a->goto_z = zz >= 20 && zz <= 185 ? (float)zz / 10.0f : 15.0f;
    a->goto_seq++;
    aos_hal_log("mapas", "goto %ld, %ld z%ld from the portal", la, lo, zz);
}

/* ---------------------------------------------------------------------------
 * The worker
 * ------------------------------------------------------------------------- */

static void worker(void *arg)
{
    app_t *a = (app_t *)arg;
    mp_store_init(RAM_BUDGET);
    mp_store_scan_packs();
    a->npacks = mp_store_packs(a->packs, MP_MAX_PACKS);
    a->packs_ready = true;
    goto_check(a);
    uint32_t goto_t = (uint32_t)aos_hal_uptime_ms();

    while (!aos_hal_worker_should_stop()) {
        uint32_t tn = (uint32_t)aos_hal_uptime_ms();
        if (tn - goto_t > 1500) {
            goto_t = tn;
            goto_check(a);
        }
        if (a->want_rescan) {
            a->want_rescan = false;
            mp_store_scan_packs();
            a->npacks = mp_store_packs(a->packs, MP_MAX_PACKS);
            a->packs_ready = true;
            a->dirty_tiles = true;
        }
        if (a->want_clear) {
            a->want_clear = false;
            a->cleared = mp_store_clear_cache();
        }
        if (a->want_search) {
            a->want_search = false;
            a->search_cancel = false;
            uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
            int n = mp_search_files(a->skey, a->hits, MAX_OFFLINE, &a->search_cancel);
            a->nhits = n;
            aos_hal_log("mapas", "search \"%s\": %d offline in %u ms", a->skey, n,
                        (unsigned)((uint32_t)aos_hal_uptime_ms() - t0));
            a->searching = false;
            a->search_seq++;
        }

        /* the answers from the network */
        for (int i = 0; i < NQ; i++) {
            net_t *q = &a->q[i];
            if (q->state != Q_READY) continue;
            const char *body = q->empty ? NULL : aos_hal_http_body(q->id);
            int len = q->empty ? 0 : aos_hal_http_len(q->id);
            if (q->empty || body) {
                mp_store_put(q->k.z, q->k.x, q->k.y, (uint8_t *)body, len, true);
                a->dirty_tiles = true;
            }
            q->state = Q_DONE;
        }

        /* does the newest buffer still cover the screen? */
        mp_view_t v = a->view;
        int f = a->front;
        bool need = f < 0 || a->dirty_tiles;
        if (!need) {
            const back_t *b = &a->back[f];
            float k = exp2f(v.z - b->v.z);
            float s = mp_px_per_unit(b->v.z);
            float bx = BW * 0.5f + (float)(int32_t)(v.cx - b->v.cx) * s;
            float by = BH * 0.5f + (float)(int32_t)(v.cy - b->v.cy) * s;
            float hw = SW * 0.5f / k, hh = SH * 0.5f / k;
            bool still = (uint32_t)aos_hal_uptime_ms() - a->moved_ms > STILL_MS;
            if (bx - hw < EDGE || by - hh < EDGE || bx + hw > BW - EDGE || by + hh > BH - EDGE) need = true;
            if (fabsf(v.z - b->v.z) > 0.001f && still) need = true;
            if (fabsf(v.z - b->v.z) > 0.55f) need = true;
        }
        if (!need) {
            aos_hal_worker_sleep(8);
            continue;
        }
        int bi = f < 0 ? 0 : 1 - f;
        while (a->composing == bi && !aos_hal_worker_should_stop()) aos_hal_worker_sleep(1);
        a->dirty_tiles = false;
        mp_fb_t fb = { a->back[bi].px, BW, BH, 0, 0, BW, BH };
        mp_render_stats_t st;
        uint32_t tr = (uint32_t)aos_hal_uptime_ms();
        mp_render(&fb, &v, a->fonts, &st);
        a->b_render_us += ((uint32_t)aos_hal_uptime_ms() - tr) * 1000;
        a->b_renders++;
        a->back[bi].v = v;
        a->front = bi;
        a->front_gen++;

        int n = st.missing < MP_MAX_MISSING ? st.missing : MP_MAX_MISSING;
        for (int i = 0; i < n; i++) a->want[i] = st.miss[i];
        a->nwant = n;
        a->want_seq++;
        a->missing = st.missing;
        a->stand_in = st.stand_in;
        a->renders++;

        uint32_t now = (uint32_t)aos_hal_uptime_ms();
        if (now - a->t_log > 1000 || fabsf(v.z - a->z_log) > 0.5f) {
            a->z_log = v.z;
            uint32_t rb, hp, hc;
            int rt;
            mp_store_stats(&rb, &rt, &hp, &hc);
            /* the three dearest classes */
            int top[3] = { -1, -1, -1 };
            for (int c = 0; c < MC_GEOM_END; c++) {
                for (int k = 0; k < 3; k++) {
                    if (top[k] < 0 || st.us_cls[c] > st.us_cls[top[k]]) {
                        for (int m = 2; m > k; m--) top[m] = top[m - 1];
                        top[k] = c;
                        break;
                    }
                }
            }
            aos_hal_log("mapas", "z%.2f: tiles %u + geometry %u + labels %u ms; %d tiles, %d stand-ins, "
                        "%d missing, %d labels, points %u -> %u; dearest classes %d:%u %d:%u %d:%u ms "
                        "| ram %d tiles %u KB, pack %u, cache %u",
                        (double)v.z, (unsigned)(st.us_tiles / 1000), (unsigned)(st.us_geom / 1000),
                        (unsigned)(st.us_labels / 1000), st.shown, st.stand_in, st.missing, st.labels,
                        (unsigned)st.pts_in, (unsigned)st.pts_out,
                        top[0], (unsigned)(st.us_cls[top[0]] / 1000), top[1],
                        (unsigned)(st.us_cls[top[1]] / 1000), top[2], (unsigned)(st.us_cls[top[2]] / 1000),
                        rt, (unsigned)(rb / 1024), (unsigned)hp, (unsigned)hc);
            a->t_log = now;
        }
        aos_hal_worker_sleep(1);
    }
    mp_store_deinit();
}

/* ---------------------------------------------------------------------------
 * The network (LVGL's side)
 * ------------------------------------------------------------------------- */

static bool key_eq(const mp_key_t *a, const mp_key_t *b)
{
    return a->z == b->z && a->x == b->x && a->y == b->y;
}

/* "tiles":["https://.../{z}/{x}/{y}.pbf"] out of the TileJSON */
static bool tilejson_parse(app_t *a, const char *js)
{
    const char *p = strstr(js, "\"tiles\"");
    if (!p) return false;
    p = strchr(p, '[');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;
    const char *e = strchr(p, '"');
    if (!e || e - p >= (long)sizeof a->tpl || e - p < 20) return false;
    if (strncmp(p, "https://", 8) != 0 || !strstr(p, "{z}")) return false;
    memcpy(a->tpl, p, (size_t)(e - p));
    a->tpl[e - p] = 0;
    return true;
}

static bool tile_url(const app_t *a, const mp_key_t *k, char *out, size_t n)
{
    size_t o = 0;
    for (const char *p = a->tpl; *p && o + 12 < n; p++) {
        if (p[0] == '{' && p[2] == '}' && (p[1] == 'z' || p[1] == 'x' || p[1] == 'y')) {
            unsigned v = p[1] == 'z' ? k->z : p[1] == 'x' ? (unsigned)k->x : (unsigned)k->y;
            o += (size_t)snprintf(out + o, n - o, "%u", v);
            p += 2;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
    return o + 12 < n;
}

static bool failed_recently(app_t *a, const mp_key_t *k, uint32_t now)
{
    for (int i = 0; i < a->nfailed; i++)
        if (key_eq(&a->failed[i].k, k) && (int32_t)(a->failed[i].until - now) > 0) return true;
    for (int i = 0; i < 8; i++)
        if (key_eq(&a->recent[i].k, k) && now - a->recent[i].t < 3000) return true;
    return false;
}

static void failed_add(app_t *a, const mp_key_t *k, uint32_t until)
{
    int i = a->nfailed < MAX_FAILED ? a->nfailed++ : (int)(until % MAX_FAILED);
    a->failed[i].k = *k;
    a->failed[i].until = until;
}

static int inflight(const app_t *a)
{
    int n = 0;
    for (int i = 0; i < NQ; i++) n += a->q[i].state != Q_FREE;
    return n;
}

static void net_pump(app_t *a)
{
    uint32_t now = (uint32_t)aos_hal_uptime_ms();

    /* give back what the worker has used */
    for (int i = 0; i < NQ; i++) {
        net_t *q = &a->q[i];
        if (q->state != Q_DONE) continue;
        if (q->id > 0) aos_hal_http_release(q->id);
        a->recent[a->recent_i].k = q->k;
        a->recent[a->recent_i].t = now;
        a->recent_i = (a->recent_i + 1) % 8;
        q->id = 0;
        q->state = Q_FREE;
        a->overlay_dirty = true;
    }
    /* what came in */
    for (int i = 0; i < NQ; i++) {
        net_t *q = &a->q[i];
        if (q->state != Q_FLIGHT) continue;
        aos_http_state_t hs = aos_hal_http_state(q->id);
        if (hs == AOS_HTTP_BUSY) continue;
        int code = aos_hal_http_status(q->id);
        int len = aos_hal_http_len(q->id);
        bool whole = hs == AOS_HTTP_DONE && len < HTTP_MAX - 1 &&
                     (len == 0 || mp_mvt_complete((const uint8_t *)aos_hal_http_body(q->id), len));
        if (whole) {
            uint32_t ms = now - q->t0;
            aos_hal_log("mapas", "tile %d/%u/%u: %d KB in %u ms (%u KB/s), %d in flight",
                        q->k.z, (unsigned)q->k.x, (unsigned)q->k.y, len / 1024, (unsigned)ms,
                        (unsigned)(ms ? (uint32_t)len / ms : 0), inflight(a));
            q->empty = code == 204 || len == 0;
            a->downloaded += (uint32_t)len;
            a->last_err = 0;
            q->state = Q_READY;
        } else if (hs == AOS_HTTP_FAILED && (code == 404 || code == 204)) {
            q->empty = true;
            q->state = Q_READY;
        } else {
            bool big = hs == AOS_HTTP_DONE && len >= HTTP_MAX - 1;
            aos_hal_log("mapas", "tile %d/%u/%u: %s %d, %d bytes", q->k.z, (unsigned)q->k.x,
                        (unsigned)q->k.y, big ? "too big" : hs == AOS_HTTP_DONE ? "cut short" : "failed",
                        code, len);
            a->last_err = hs == AOS_HTTP_DONE ? -100 : code;
            failed_add(a, &q->k, now + (big ? 600000 : code == AOS_HTTP_ERR_SIN_HORA ? 3000 : 6000));
            aos_hal_http_release(q->id);
            q->id = 0;
            q->state = Q_FREE;
            a->overlay_dirty = true;
        }
    }

    /* With the screen on the WiFi naps between beacons (modem sleep): every
     * round trip costs 200-300 ms, and two TLS downloads of half a megabyte
     * made the access point give up on the watch (bcn_timeout, 2026-09-26,
     * as the OTA did before v0.8.1). Awake while something is in flight, and
     * a few seconds after, for the next tile. */
    bool busy = inflight(a) > 0 || a->tpl_id > 0;
    if (busy) a->busy_ms = now;
    bool want_ll = busy || now - a->busy_ms < 4000;
    if (want_ll != a->low_latency) {
        a->low_latency = want_ll;
        aos_hal_net_low_latency(want_ll);
    }

    if (!a->online || aos_hal_net_state() != AOS_NET_CONNECTED) return;

    /* the template, once (and kept in a preference for next time) */
    if (!a->tpl[0]) {
        if (a->tpl_id > 0) {
            aos_http_state_t hs = aos_hal_http_state(a->tpl_id);
            if (hs == AOS_HTTP_BUSY) return;
            if (hs == AOS_HTTP_DONE && tilejson_parse(a, aos_hal_http_body(a->tpl_id))) {
                aos_hal_pref_set_str("map_tpl", a->tpl);
                aos_hal_log("mapas", "tiles: %s", a->tpl);
            } else {
                a->last_err = aos_hal_http_status(a->tpl_id);
                a->tpl_retry = now + (a->last_err == AOS_HTTP_ERR_SIN_HORA ? 3000 : 15000);
            }
            aos_hal_http_release(a->tpl_id);
            a->tpl_id = 0;
            a->overlay_dirty = true;
        } else if ((int32_t)(now - a->tpl_retry) >= 0) {
            a->tpl_id = aos_hal_http_get(TILEJSON, 64 * 1024);
            if (a->tpl_id <= 0) {
                a->tpl_id = 0;
                a->tpl_retry = now + 2000;
            }
        }
        if (!a->tpl[0]) return;
    }

    /* ask for what the last render lacked, nearest first */
    int n = a->nwant;
    for (int w = 0; w < n && inflight(a) < MAX_INFLIGHT; w++) {
        mp_key_t k = a->want[w];
        bool queued = false;
        for (int i = 0; i < NQ; i++)
            if (a->q[i].state != Q_FREE && key_eq(&a->q[i].k, &k)) queued = true;
        if (queued || failed_recently(a, &k, now)) continue;
        int slot = -1;
        for (int i = 0; i < NQ; i++)
            if (a->q[i].state == Q_FREE) { slot = i; break; }
        if (slot < 0) break;
        char url[256];
        if (!tile_url(a, &k, url, sizeof url)) continue;
        int id = aos_hal_http_get(url, HTTP_MAX);
        if (id <= 0) break;             /* no slot in the HAL right now */
        a->q[slot].id = id;
        a->q[slot].k = k;
        a->q[slot].empty = false;
        a->q[slot].t0 = now;
        a->q[slot].state = Q_FLIGHT;
        a->overlay_dirty = true;
    }
}

static void show_list(app_t *a);
static void show_map(app_t *a);
static void search_pump(app_t *a);
static void search_start(app_t *a);

/* ---------------------------------------------------------------------------
 * The view
 * ------------------------------------------------------------------------- */

static void view_moved(app_t *a)
{
    a->moved_ms = (uint32_t)aos_hal_uptime_ms();
    a->view_seq++;
}

static uint32_t clamp_y(int64_t y)
{
    if (y < 0) return 0;
    if (y > 0xFFFFFFFFll) return 0xFFFFFFFFu;
    return (uint32_t)y;
}

static void view_pan(app_t *a, float dx, float dy)
{
    float s = mp_px_per_unit(a->view.z);
    float ux = -dx / s, uy = -dy / s;
    if (ux > 2.0e9f) ux = 2.0e9f;
    if (ux < -2.0e9f) ux = -2.0e9f;
    if (uy > 2.0e9f) uy = 2.0e9f;
    if (uy < -2.0e9f) uy = -2.0e9f;
    a->view.cx += (uint32_t)(int32_t)ux;
    a->view.cy = clamp_y((int64_t)a->view.cy + (int32_t)uy);
    view_moved(a);
}

/* zoom to z keeping the world point under screen (ax, ay) where it is */
static void view_zoom_at(app_t *a, float z, float ax, float ay)
{
    if (z < MP_ZMIN) z = MP_ZMIN;
    if (z > MP_ZMAX) z = MP_ZMAX;
    float s0 = mp_px_per_unit(a->view.z), s1 = mp_px_per_unit(z);
    float ox = ax - SW * 0.5f, oy = ay - SH * 0.5f;
    float ux = ox / s0 - ox / s1, uy = oy / s0 - oy / s1;
    if (ux > 2.0e9f) ux = 2.0e9f;
    if (ux < -2.0e9f) ux = -2.0e9f;
    if (uy > 2.0e9f) uy = 2.0e9f;
    if (uy < -2.0e9f) uy = -2.0e9f;
    a->view.cx += (uint32_t)(int32_t)ux;
    a->view.cy = clamp_y((int64_t)a->view.cy + (int32_t)uy);
    a->view.z = z;
    view_moved(a);
}

static void zoom_to(app_t *a, float z, float ax, float ay)
{
    if (z < MP_ZMIN) z = MP_ZMIN;
    if (z > MP_ZMAX) z = MP_ZMAX;
    a->zt = z;
    a->ax = ax;
    a->ay = ay;
    a->zooming = true;
    a->vx = a->vy = 0;
}

static void go_to(app_t *a, uint32_t cx, uint32_t cy, float z)
{
    a->view.cx = cx;
    a->view.cy = cy;
    a->view.z = z < MP_ZMIN ? MP_ZMIN : z > MP_ZMAX ? MP_ZMAX : z;
    a->zooming = false;
    a->vx = a->vy = 0;
    view_moved(a);
}

static void view_save(app_t *a)
{
    aos_hal_pref_set_i32("map_cx", (int32_t)a->view.cx);
    aos_hal_pref_set_i32("map_cy", (int32_t)a->view.cy);
    aos_hal_pref_set_i32("map_z", (int32_t)(a->view.z * 100));
}

static void view_load(app_t *a)
{
    int32_t cx, cy, z;
    if (aos_hal_pref_get_i32("map_cx", &cx) && aos_hal_pref_get_i32("map_cy", &cy) &&
        aos_hal_pref_get_i32("map_z", &z)) {
        go_to(a, (uint32_t)cx, (uint32_t)cy, (float)z / 100.0f);
        return;
    }
    /* first time: where Clima is, else the Obelisco */
    int32_t la, lo;
    float lat = -34.6037f, lon = -58.3816f, zz = 12.0f;
    if (aos_hal_pref_get_i32("clima_lat", &la) && aos_hal_pref_get_i32("clima_lon", &lo)) {
        lat = (float)la / 1e4f;
        lon = (float)lo / 1e4f;
        zz = 13.0f;
    }
    uint32_t x, y;
    mp_lonlat_to_world(lon, lat, &x, &y);
    go_to(a, x, y, zz);
}

/* ---------------------------------------------------------------------------
 * The frame: the newest buffer, moved or scaled, and what goes on top
 * ------------------------------------------------------------------------- */

/* Where the screen falls in the newest buffer; set once per frame. */
typedef struct {
    const back_t *b;
    bool          scaled;
    int           ox, oy;               /* unscaled: the screen's corner in the buffer */
    float         bxc, byc, inv;        /* scaled */
} cut_t;

static int16_t s_col[SW];

static bool cut_begin(app_t *a, cut_t *c)
{
    int f = a->front;
    if (f < 0) return false;
    a->composing = f;
    if (a->front != f) {                /* swapped under us: take the new one */
        f = a->front;
        a->composing = f;
    }
    c->b = &a->back[f];
    const mp_view_t *v = &a->view;
    float k = exp2f(v->z - c->b->v.z);
    float s = mp_px_per_unit(c->b->v.z);
    c->bxc = BW * 0.5f + (float)(int32_t)(v->cx - c->b->v.cx) * s;
    c->byc = BH * 0.5f + (float)(int32_t)(v->cy - c->b->v.cy) * s;
    c->scaled = fabsf(k - 1.0f) >= 0.002f;
    if (!c->scaled) {
        c->ox = (int)floorf(c->bxc - SW * 0.5f + 0.5f);
        c->oy = (int)floorf(c->byc - SH * 0.5f + 0.5f);
    } else {
        /* nearest pixel: a pinch or a zoom on its way */
        c->inv = 1.0f / k;
        for (int x = 0; x < SW; x++) {
            int bx = (int)floorf(c->bxc + ((float)x + 0.5f - SW * 0.5f) * c->inv);
            s_col[x] = (int16_t)(bx < 0 || bx >= BW ? -1 : bx);
        }
    }
    return true;
}

/* rows [y0, y0 + n) of the screen into dst (SW x n) */
static void cut_rows(const cut_t *c, uint16_t *dst, int y0, int n)
{
    const uint16_t bg = mp_be565(MP_BG);
    for (int r = 0; r < n; r++) {
        int y = y0 + r;
        uint16_t *d = dst + (size_t)r * SW;
        if (!c) {
            for (int x = 0; x < SW; x++) d[x] = bg;
            continue;
        }
        int by = c->scaled ? (int)floorf(c->byc + ((float)y + 0.5f - SH * 0.5f) * c->inv) : c->oy + y;
        if (by < 0 || by >= BH) {
            for (int x = 0; x < SW; x++) d[x] = bg;
            continue;
        }
        const uint16_t *srow = c->b->px + (size_t)by * BW;
        if (c->scaled) {
            for (int x = 0; x < SW; x++) d[x] = s_col[x] < 0 ? bg : srow[s_col[x]];
            continue;
        }
        int ox = c->ox;
        int x0 = ox < 0 ? -ox : 0, x1 = ox + SW > BW ? BW - ox : SW;
        if (x1 < x0) x1 = x0;
        for (int x = 0; x < x0; x++) d[x] = bg;
        if (x1 > x0) memcpy(d + x0, srow + ox + x0, (size_t)(x1 - x0) * 2);
        for (int x = x1; x < SW; x++) d[x] = bg;
    }
}

/* What goes on top, worked out once per frame and drawn strip by strip. */
typedef struct {
    char  scale_txt[16];
    int   scale_px;
    char  status[64];
} over_t;

static void over_begin(app_t *a, over_t *o)
{
    memset(o, 0, sizeof *o);
    /* the scale: a round length of at least 40 px */
    float mpp = mp_metres_per_px(&a->view);
    static const float NICE[] = { 1, 2, 5 };
    float best = 0;
    for (float dec = 1; dec < 1e7f && !best; dec *= 10) {
        for (int i = 0; i < 3; i++) {
            if (NICE[i] * dec / mpp >= 40.0f) {
                best = NICE[i] * dec;
                break;
            }
        }
    }
    if (best > 0) {
        o->scale_px = (int)(best / mpp);
        if (best >= 1000) snprintf(o->scale_txt, sizeof o->scale_txt, "%d km", (int)(best / 1000));
        else snprintf(o->scale_txt, sizeof o->scale_txt, "%d m", (int)best);
    }
    /* what is going on with the data, when something is */
    bool net = aos_hal_net_state() == AOS_NET_CONNECTED;
    if (inflight(a) || (a->missing && a->online && a->tpl[0] && net)) {
        snprintf(o->status, sizeof o->status, "%s %d", _("Descargando"), a->missing ? a->missing : 1);
    } else if (a->missing && !a->online) {
        snprintf(o->status, sizeof o->status, "%s", _("Sin datos aquí"));
    } else if (a->missing && !net) {
        snprintf(o->status, sizeof o->status, "%s", _("Sin conexión"));
    } else if (a->missing && a->last_err == AOS_HTTP_ERR_SIN_HORA) {
        snprintf(o->status, sizeof o->status, "%s", _("Esperando la hora"));
    } else if (a->missing && a->last_err) {
        snprintf(o->status, sizeof o->status, "%s", _("Error de descarga"));
    }
}

static void button_disc(mp_fb_t *fb, int cx, int cy)
{
    if (cy + BTN_R + 2 < fb->cy0 || cy - BTN_R - 2 >= fb->cy1) return;
    mp_disc(fb, (float)cx, (float)cy, BTN_R + 1.5f, 0x000000, 150);
    mp_disc(fb, (float)cx, (float)cy, BTN_R, 0x2A2F3A, 235);
}

static bool rows_touch(const mp_fb_t *fb, int y0, int y1)
{
    return y1 > fb->cy0 && y0 < fb->cy1;
}

/* the overlay over rows [y0, y0 + n) of the screen, dst being those rows */
static void over_rows(app_t *a, const over_t *o, uint16_t *dst, int y0, int n)
{
    /* a frame-sized view whose only valid rows are the strip's */
    mp_fb_t fb = { dst - (ptrdiff_t)y0 * SW, SW, SH, 0, y0, SW, y0 + n };
    const uint32_t ink = 0xE8EAEE;

    if (rows_touch(&fb, ZIN_Y - BTN_R - 2, ZOUT_Y + BTN_R + 2)) {
        button_disc(&fb, ZIN_X, ZIN_Y);
        float h[4] = { ZIN_X - 9.0f, ZIN_Y, ZIN_X + 9.0f, ZIN_Y };
        float vv[4] = { ZIN_X, ZIN_Y - 9.0f, ZIN_X, ZIN_Y + 9.0f };
        mp_polyline(&fb, h, 2, 3.0f, ink, 255);
        mp_polyline(&fb, vv, 2, 3.0f, ink, 255);
        button_disc(&fb, ZOUT_X, ZOUT_Y);
        float m[4] = { ZOUT_X - 9.0f, ZOUT_Y, ZOUT_X + 9.0f, ZOUT_Y };
        mp_polyline(&fb, m, 2, 3.0f, ink, 255);
    }
    if (rows_touch(&fb, MENU_Y - BTN_R - 2, MENU_Y + BTN_R + 2)) {
        button_disc(&fb, MENU_X, MENU_Y);
        for (int i = -1; i <= 1; i++) {
            float l[4] = { MENU_X - 9.0f, MENU_Y + i * 6.0f, MENU_X + 9.0f, MENU_Y + i * 6.0f };
            mp_polyline(&fb, l, 2, 2.4f, ink, 255);
        }
    }
    const mp_font_t *fs = &a->fonts[MP_FONT_S];
    if (!fs->ok) return;
    const int lh = fs->line_h;

    /* the pin of the last place found, and its name over it */
    if (a->pin_on) {
        float s = mp_px_per_unit(a->view.z);
        float px = SW * 0.5f + (float)(int32_t)(a->pin_cx - a->view.cx) * s;
        float py = SH * 0.5f + (float)(int32_t)(a->pin_cy - a->view.cy) * s;
        if (px > -80 && px < SW + 80 && py > -20 && py < SH + 40 && rows_touch(&fb, (int)py - 50, (int)py + 3)) {
            float stem[4] = { px, py - 12, px, py };
            mp_polyline(&fb, stem, 2, 4.0f, 0x000000, 160);
            mp_polyline(&fb, stem, 2, 2.4f, 0xFF5A4E, 255);
            mp_disc(&fb, px, py - 20, 11.5f, 0x000000, 150);
            mp_disc(&fb, px, py - 20, 10, 0xFF5A4E, 255);
            mp_disc(&fb, px, py - 20, 4, 0xFFFFFF, 255);
            const mp_font_t *fm = &a->fonts[MP_FONT_M];
            int w = mp_text_width(fm, a->pin_name);
            mp_text(&fb, fm, (int)px - w / 2, (int)py - 34 - fm->line_h, a->pin_name, 0xFFFFFF, MP_BG, true);
        }
    }

    if (o->scale_px && rows_touch(&fb, 392 - lh, 396)) {
        int x0 = 34, yb = 392, px = o->scale_px;
        float bar[6] = { (float)x0, yb - 5.0f, (float)x0, (float)yb, (float)(x0 + px), (float)yb };
        float end[4] = { (float)(x0 + px), (float)yb, (float)(x0 + px), yb - 5.0f };
        mp_polyline(&fb, bar, 3, 4.0f, MP_BG, 200);
        mp_polyline(&fb, end, 2, 4.0f, MP_BG, 200);
        mp_polyline(&fb, bar, 3, 1.6f, 0xD8DCE4, 255);
        mp_polyline(&fb, end, 2, 1.6f, 0xD8DCE4, 255);
        mp_text(&fb, fs, x0 + px + 6, yb - lh + 3, o->scale_txt, 0xD8DCE4, MP_BG, true);
    }
    /* the attribution the licence asks for */
    if (rows_touch(&fb, 406, 408 + lh + 2)) {
        const char *attr = "© OpenFreeMap © OpenMapTiles © OSM";
        mp_text(&fb, fs, (SW - mp_text_width(fs, attr)) / 2, 408, attr, 0x8A919E, MP_BG, true);
    }
    if (o->status[0] && rows_touch(&fb, 32, 36 + lh)) {
        mp_text(&fb, fs, (SW - mp_text_width(fs, o->status)) / 2, 34, o->status, 0xFFD27A, MP_BG, true);
    }
}

static uint32_t ccount(void)
{
#if defined(__XTENSA__)
    uint32_t c;
    __asm__ volatile("rsr %0, ccount" : "=a"(c));
    return c;
#else
    return (uint32_t)(aos_hal_uptime_ms() * 240000u);
#endif
}

/* The frame goes out in strips of internal RAM, two in turns: the panel's
 * DMA sends one while the next is cut out of the buffer. A whole frame in
 * PSRAM cost 23 ms to compose plus 28 to push (18 fps, measured). */
static void push_frame(app_t *a)
{
    uint32_t c0 = ccount(), tb = 0;
    cut_t c;
    bool have = cut_begin(a, &c);
    over_t o;
    over_begin(a, &o);
    bool canvas = false;
    for (int y0 = 0; y0 < SH; y0 += STRIP) {
        int n = SH - y0 < STRIP ? SH - y0 : STRIP;
        uint16_t *st = a->strip[a->strip_i];
        a->strip_i ^= 1;
        cut_rows(have ? &c : NULL, st, y0, n);
        over_rows(a, &o, st, y0, n);
        uint32_t t = ccount();
        if (!aos_hal_display_blit(0, y0, SW, n, st) && a->cv) {
            /* the simulator (or the panel asleep): through the canvas */
            canvas = true;
            for (int k = 0; k < SW * n; k++)
                a->cv[(size_t)y0 * SW + k] = (uint16_t)((st[k] >> 8) | (st[k] << 8));
        }
        tb += ccount() - t;
    }
    a->composing = -1;
    if (canvas && a->canvas) lv_obj_invalidate(a->canvas);
    uint32_t c2 = ccount();
    a->b_frames++;
    a->b_compose += (c2 - c0 - tb) / 240;
    a->b_blit += tb / 240;
}

/* The bench: wait for the data, then pan, zoom in, pan, zoom out and a slow
 * continuous zoom like a pinch, logging each phase. */
static void bench_step(app_t *a)
{
    static const char *const NAME[] = { "wait", "pan", "zoom in", "pan z+2", "zoom out", "pinch",
                                        "search", "end" };
    uint32_t now = (uint32_t)aos_hal_uptime_ms(), el = now - a->bench_t;
    float dt = TICK_MS / 1000.0f;
    bool next = false;
    switch (a->bench_phase) {
    case 0: next = (el > 2000 && a->missing == 0 && !inflight(a)) || el > 30000; break;
    case 1: view_pan(a, -260 * dt, 0); next = el > 3000; break;
    case 2: if (!a->bench_zoomed) zoom_to(a, a->view.z + 2, SW / 2, SH / 2); a->bench_zoomed = true; next = el > 4000; break;
    case 3: view_pan(a, -180 * dt, -120 * dt); next = el > 3000; break;
    case 4: if (!a->bench_zoomed) zoom_to(a, a->view.z - 3, SW / 2, SH / 2); a->bench_zoomed = true; next = el > 4000; break;
    case 5: view_zoom_at(a, a->view.z + 0.8f * dt, SW / 2, SH / 2); next = el > 2500; break;
    case 6:
        if (!a->bench_zoomed) {
            snprintf(a->query, sizeof a->query, "serrano");
            search_start(a);
            a->bench_zoomed = true;
        }
        next = (a->search_seq != 0 && !a->searching && a->photon_id == 0 && a->photon_done) || el > 20000;
        if (next) aos_hal_log("mapas", "bench search: %d hits, online %s", a->nhits,
                              a->photon_done ? "answered" : "no");
        break;
    default: return;
    }
    if (!next) return;
    if (a->bench_phase > 0) {
        uint32_t f = a->b_frames ? a->b_frames : 1, r = a->b_renders ? a->b_renders : 1;
        aos_hal_log("mapas", "bench %s: %u frames in %u ms (%u fps), compose %u us + blit %u us; "
                    "%u renders, %u ms each; z%.2f, %d missing",
                    NAME[a->bench_phase], (unsigned)a->b_frames, (unsigned)el,
                    (unsigned)(a->b_frames * 1000 / (el ? el : 1)), (unsigned)(a->b_compose / f),
                    (unsigned)(a->b_blit / f), (unsigned)a->b_renders,
                    (unsigned)(a->b_render_us / r / 1000), (double)a->view.z, a->missing);
    }
    a->bench_phase++;
    a->bench_zoomed = false;
    a->bench_t = now;
    a->b_frames = a->b_compose = a->b_blit = 0;
    a->b_renders = a->b_render_us = 0;
    if (a->bench_phase == 7) {
        char path[128];
        snprintf(path, sizeof path, "%s/bench.txt", mp_maps_dir() ? mp_maps_dir() : ".");
        remove(path);
        a->bench = false;
    }
}

static void tick(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    net_pump(a);
    search_pump(a);
    if (a->goto_seq != a->goto_seen) {
        a->goto_seen = a->goto_seq;
        go_to(a, a->goto_cx, a->goto_cy, a->goto_z);
        if (!a->viewing) show_map(a);
    }
    if (a->bench) bench_step(a);
    if (!a->viewing) return;
    float dt = TICK_MS / 1000.0f;

    if (a->zooming) {
        float d = a->zt - a->view.z;
        if (fabsf(d) < 0.01f) {
            view_zoom_at(a, a->zt, a->ax, a->ay);
            a->zooming = false;
        } else {
            view_zoom_at(a, a->view.z + d * 0.28f, a->ax, a->ay);
        }
    } else if (!a->touching && (fabsf(a->vx) > 20 || fabsf(a->vy) > 20)) {
        view_pan(a, a->vx * dt, a->vy * dt);
        a->vx *= 0.92f;
        a->vy *= 0.92f;
    }

    if (a->view_seq != a->shown_seq || a->front_gen != a->shown_gen || a->overlay_dirty) {
        a->shown_seq = a->view_seq;
        a->shown_gen = a->front_gen;
        a->overlay_dirty = false;
        push_frame(a);
    }
}

/* ---------------------------------------------------------------------------
 * Touch
 * ------------------------------------------------------------------------- */

static bool hit(float x, float y, int cx, int cy)
{
    return fabsf(x - cx) < BTN_HIT && fabsf(y - cy) < BTN_HIT;
}

static bool buttons(app_t *a, float x, float y)
{
    if (hit(x, y, ZIN_X, ZIN_Y)) {
        zoom_to(a, floorf((a->zooming ? a->zt : a->view.z) + 1.0f + 0.01f), SW * 0.5f, SH * 0.5f);
        return true;
    }
    if (hit(x, y, ZOUT_X, ZOUT_Y)) {
        zoom_to(a, ceilf((a->zooming ? a->zt : a->view.z) - 1.0f - 0.01f), SW * 0.5f, SH * 0.5f);
        return true;
    }
    if (hit(x, y, MENU_X, MENU_Y)) {
        show_list(a);
        return true;
    }
    return false;
}

static void gesture_cb(const aos_gesture_event_t *ev, void *user)
{
    app_t *a = (app_t *)user;
    if (!a->viewing) return;
    switch (ev->type) {
    case AOS_GESTURE_DRAG_BEGIN:
    case AOS_GESTURE_PINCH_BEGIN:
        a->touching = true;
        a->zooming = false;
        a->vx = a->vy = 0;
        break;
    case AOS_GESTURE_DRAG:
        view_pan(a, ev->dx, ev->dy);
        break;
    case AOS_GESTURE_DRAG_END:
        a->touching = false;
        a->vx = ev->vx;
        a->vy = ev->vy;
        break;
    case AOS_GESTURE_PINCH:
        view_zoom_at(a, a->view.z + log2f(ev->scale), ev->x, ev->y);
        view_pan(a, ev->dx, ev->dy);
        break;
    case AOS_GESTURE_PINCH_END:
        a->touching = false;
        break;
    case AOS_GESTURE_TAP:
        buttons(a, ev->x, ev->y);
        break;
    case AOS_GESTURE_DOUBLE_TAP:
        /* FAST_TAP: a double tap on a button is its second press */
        if (!buttons(a, ev->x, ev->y)) zoom_to(a, floorf(a->view.z + 1.0f + 0.01f), ev->x, ev->y);
        break;
    case AOS_GESTURE_LONG_PRESS: {
        float s = mp_px_per_unit(a->view.z);
        uint32_t x = a->view.cx + (uint32_t)(int32_t)((ev->x - SW * 0.5f) / s);
        uint32_t y = clamp_y((int64_t)a->view.cy + (int32_t)((ev->y - SH * 0.5f) / s));
        float lon, lat;
        mp_world_to_lonlat(x, y, &lon, &lat);
        char txt[48];
        snprintf(txt, sizeof txt, "%.5f, %.5f", (double)lat, (double)lon);
        aos_ui_toast(txt, 3000);
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * The list: zones, offline maps, settings
 * ------------------------------------------------------------------------- */

static void show_map(app_t *a)
{
    if (a->list) {
        lv_obj_delete(a->list);
        a->list = NULL;
    }
    a->viewing = true;
    a->screen = SCR_MAP;
    a->q_label = a->q_hint = a->res_list = NULL;
    a->self->desc.flags |= AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    lv_obj_remove_flag(a->map, LV_OBJ_FLAG_HIDDEN);
    a->overlay_dirty = true;
}

/* A black full-screen panel over the map, in place of whatever panel was up. */
static lv_obj_t *panel_new(app_t *a, int screen)
{
    if (a->list) lv_obj_delete(a->list);
    a->q_label = a->q_hint = a->res_list = NULL;
    a->viewing = false;
    a->screen = screen;
    a->vx = a->vy = 0;
    a->zooming = false;
    a->self->desc.flags &= ~(uint32_t)(AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG);
    lv_obj_add_flag(a->map, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *l = lv_obj_create(a->root);
    a->list = l;
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, SW, SH);
    lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

static void zone_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (i >= 0 && i < a->nzones) go_to(a, a->zones[i].cx, a->zones[i].cy, a->zones[i].z);
    show_map(a);
}

static void pack_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (i >= 0 && i < a->npacks) {
        const mp_pack_info_t *p = &a->packs[i];
        uint32_t x0, y0, x1, y1;
        mp_lonlat_to_world((float)p->w / 1e6f, (float)p->n / 1e6f, &x0, &y0);
        mp_lonlat_to_world((float)p->e / 1e6f, (float)p->s / 1e6f, &x1, &y1);
        /* the zoom that fits the zone on the screen, within what the pack has */
        float span = (float)(x1 - x0) > (float)(y1 - y0) ? (float)(x1 - x0) : (float)(y1 - y0);
        float z = span > 0 ? log2f(SW * 4294967296.0f / 256.0f / span) : 14.0f;
        if (z < p->minz + 1) z = (float)(p->minz + 1);
        if (z > p->maxz + 3) z = (float)(p->maxz + 3);
        go_to(a, x0 + (x1 - x0) / 2, y0 + (y1 - y0) / 2, z);
    }
    show_map(a);
}

static void clima_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int32_t la, lo;
    if (aos_hal_pref_get_i32("clima_lat", &la) && aos_hal_pref_get_i32("clima_lon", &lo)) {
        uint32_t x, y;
        mp_lonlat_to_world((float)lo / 1e4f, (float)la / 1e4f, &x, &y);
        go_to(a, x, y, 14.0f);
    }
    show_map(a);
}

static void save_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    char name[32];
    snprintf(name, sizeof name, "%s %d", _("Zona"), a->nzones + 1);
    if (zone_append(a, name)) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s: %s", _("Guardada"), name);
        aos_ui_toast(msg, 2000);
    } else {
        aos_ui_toast(_("No se pudo guardar"), 2000);
    }
    show_map(a);
}

static void online_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->online = !a->online;
    aos_hal_pref_set_i32("map_online", a->online);
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_label_set_text(lv_obj_get_child(btn, 0), a->online ? _("Descargar: sí") : _("Descargar: no"));
    a->dirty_tiles = true;
}

static void clear_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->cleared = -1;
    a->want_clear = true;
    aos_ui_toast(_("Caché borrada"), 1500);
    (void)a;
}

static lv_obj_t *row(app_t *a, lv_obj_t *parent, const char *text, const char *sub,
                     lv_event_cb_t cb, int idx)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, SW - 56, sub ? 64 : 52);
    lv_obj_set_style_bg_color(r, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(r, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, a);
    lv_obj_t *l = aos_label(r, text, aos_font_body, AOS_C_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, SW - 90);
    lv_obj_align(l, sub ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 16, sub ? 8 : 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (sub) {
        lv_obj_t *s = aos_label(r, sub, aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s, SW - 90);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 16, -8);
        lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    }
    return r;
}

static void heading(lv_obj_t *parent, const char *text)
{
    lv_obj_t *h = aos_label(parent, text, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(h, SW - 72);
    lv_obj_set_style_pad_top(h, 8, 0);
}

static void show_keys(app_t *a);

static void search_open_cb(lv_event_t *e)
{
    show_keys((app_t *)lv_event_get_user_data(e));
}

static void show_list(app_t *a)
{
    zones_load(a);
    a->want_rescan = true;

    lv_obj_t *l = panel_new(a, SCR_LIST);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(l, 8, 0);
    lv_obj_set_style_pad_top(l, 22, 0);
    lv_obj_set_style_pad_bottom(l, 70, 0);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);

    aos_label(l, _("Mapas"), aos_font_title, AOS_C_TEXT);
    char st[48];
    snprintf(st, sizeof st, "%s  %s", LV_SYMBOL_KEYBOARD, _("Buscar"));
    row(a, l, st, NULL, search_open_cb, 0);
    row(a, l, _("Guardar esta vista"), NULL, save_cb, 0);

    if (a->nzones) heading(l, _("Zonas"));
    for (int i = 0; i < a->nzones; i++) {
        char sub[32];
        snprintf(sub, sizeof sub, "zoom %.1f", (double)a->zones[i].z);
        row(a, l, a->zones[i].name, sub, zone_cb, i);
    }
    int32_t la, lo;
    if (aos_hal_pref_get_i32("clima_lat", &la) && aos_hal_pref_get_i32("clima_lon", &lo)) {
        char city[48] = "";
        aos_hal_pref_get_str("clima_city", city, sizeof city);
        row(a, l, _("Ciudad de Clima"), city[0] ? city : NULL, clima_cb, 0);
    }

    if (a->packs_ready && a->npacks) {
        heading(l, _("Sin conexión"));
        /* a zone bigger than one upload is several packs with the same name:
         * one row, the tiles added up */
        for (int i = 0; i < a->npacks; i++) {
            bool seen = false;
            for (int j = 0; j < i; j++)
                if (!strcmp(a->packs[j].name, a->packs[i].name)) seen = true;
            if (seen) continue;
            int n = 0, zmin = 99, zmax = 0;
            for (int j = i; j < a->npacks; j++) {
                if (strcmp(a->packs[j].name, a->packs[i].name)) continue;
                n += a->packs[j].ntiles;
                if (a->packs[j].minz < zmin) zmin = a->packs[j].minz;
                if (a->packs[j].maxz > zmax) zmax = a->packs[j].maxz;
            }
            char sub[48];
            snprintf(sub, sizeof sub, "%d %s · z%d-%d", n, _("teselas"), zmin, zmax);
            row(a, l, a->packs[i].name, sub, pack_cb, i);
        }
    }

    heading(l, _("Ajustes"));
    lv_obj_t *b = aos_button(l, a->online ? _("Descargar: sí") : _("Descargar: no"), AOS_C_CARD2, online_cb, a);
    lv_obj_set_size(b, SW - 56, 48);
    b = aos_button(l, _("Borrar caché"), AOS_C_CARD2, clear_cb, a);
    lv_obj_set_size(b, SW - 56, 48);

    char info[160];
    snprintf(info, sizeof info, "%s\n%s %u KB",
             _("Datos: OpenFreeMap, OpenMapTiles, OpenStreetMap"),
             _("Descargado:"), (unsigned)(a->downloaded / 1024));
    lv_obj_t *t = aos_label(l, info, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(t, SW - 60);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---------------------------------------------------------------------------
 * Search: a keypad like an old phone's, and the results
 * ------------------------------------------------------------------------- */

/* the letters behind each key, in the order taps walk them; the digit last */
static const char *const KEYS[12] = {
    ".-'1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9",
    "", " 0", "",
};
enum { K_BACK = 9, K_ZERO = 10, K_OK = 11 };

static void keys_refresh(app_t *a)
{
    if (!a->q_label) return;
    char buf[64];
    snprintf(buf, sizeof buf, "%s%s", a->query, a->pend_key >= 0 ? "" : "_");
    lv_label_set_text(a->q_label, a->query[0] || a->pend_key >= 0 ? buf : _("Escribí un nombre"));
    lv_obj_set_style_text_color(a->q_label, a->query[0] ? AOS_C_TEXT : AOS_C_DIM, 0);
    /* while a key is live: its letters, the one that would go in marked */
    buf[0] = 0;
    if (a->pend_key >= 0) {
        const char *k = KEYS[a->pend_key];
        int o = 0;
        for (int i = 0; k[i] && o < (int)sizeof buf - 6; i++) {
            char c = k[i] == ' ' ? '_' : k[i];
            if (i == a->pend_idx) o += snprintf(buf + o, sizeof buf - (size_t)o, "[%c] ", c);
            else o += snprintf(buf + o, sizeof buf - (size_t)o, " %c  ", c);
        }
    }
    lv_label_set_text(a->q_hint, buf);
}

static void search_start(app_t *a);

static void key_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int k = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    size_t len = strlen(a->query);
    if (k == K_BACK) {
        if (len) a->query[len - 1] = 0;
        a->pend_key = -1;
    } else if (k == K_OK) {
        a->pend_key = -1;
        if (a->query[0]) {
            search_start(a);
            return;
        }
    } else if (k == a->pend_key && now - a->pend_t < PEND_MS && len) {
        /* the same key again, soon: the next letter in its place */
        a->pend_idx = (a->pend_idx + 1) % (int)strlen(KEYS[k]);
        a->query[len - 1] = KEYS[k][a->pend_idx];
    } else if (len < sizeof a->query - 1) {
        a->query[len] = KEYS[k][0];
        a->query[len + 1] = 0;
        a->pend_key = k;
        a->pend_idx = 0;
    }
    a->pend_t = now;
    keys_refresh(a);
}

static void key_long_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->query[0] = 0;                    /* a long press on ⌫ clears it all */
    a->pend_key = -1;
    keys_refresh(a);
}

static void show_keys(app_t *a)
{
    lv_obj_t *p = panel_new(a, SCR_KEYS);
    a->pend_key = -1;

    a->q_label = aos_label(p, "", aos_font_title, AOS_C_TEXT);
    lv_obj_set_width(a->q_label, SW - 60);
    lv_label_set_long_mode(a->q_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(a->q_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a->q_label, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_t *line = lv_obj_create(p);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, SW - 80, 2);
    lv_obj_set_style_bg_color(line, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 72);
    a->q_hint = aos_label(p, "", aos_font_body, AOS_C_ACCENT);
    lv_obj_align(a->q_hint, LV_ALIGN_TOP_MID, 0, 80);

    /* 3 x 4 keys, 112 x 66, from y 112 to 398: inside where the glass reads */
    const int KW = 112, KH = 66, GX = 8, GY = 8, X0 = (SW - 3 * KW - 2 * GX) / 2, Y0 = 112;
    for (int i = 0; i < 12; i++) {
        lv_obj_t *b = lv_obj_create(p);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, KW, KH);
        lv_obj_set_pos(b, X0 + (i % 3) * (KW + GX), Y0 + (i / 3) * (KH + GY));
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_bg_color(b, i == K_OK ? AOS_C_ACCENT : AOS_C_CARD2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, key_cb, LV_EVENT_PRESSED, a);
        if (i == K_BACK) lv_obj_add_event_cb(b, key_long_cb, LV_EVENT_LONG_PRESSED, a);

        char big[8], small[16] = "";
        if (i == K_BACK) snprintf(big, sizeof big, "%s", LV_SYMBOL_BACKSPACE);
        else if (i == K_OK) snprintf(big, sizeof big, "%s", LV_SYMBOL_OK);
        else {
            const char *k = KEYS[i];
            int n = (int)strlen(k);
            snprintf(big, sizeof big, "%c", k[n - 1]);
            int o = 0;
            for (int j = 0; j < n - 1 && o < (int)sizeof small - 3; j++) {
                small[o++] = k[j] == ' ' ? '_' : (char)(k[j] >= 'a' && k[j] <= 'z' ? k[j] - 32 : k[j]);
                small[o++] = ' ';
            }
            if (o) small[o - 1] = 0;
        }
        lv_obj_t *l1 = aos_label(b, big, aos_font_title, AOS_C_TEXT);
        lv_obj_remove_flag(l1, LV_OBJ_FLAG_CLICKABLE);
        if (small[0]) {
            lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 4);
            lv_obj_t *l2 = aos_label(b, small, aos_font_small, AOS_C_DIM);
            lv_obj_align(l2, LV_ALIGN_BOTTOM_MID, 0, -6);
            lv_obj_remove_flag(l2, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_center(l1);
        }
    }
    keys_refresh(a);
}

static const char *kind_text(const mp_hit_t *h)
{
    switch (h->kind) {
    case MP_KIND_PLACE:   return _("lugar");
    case MP_KIND_STREET:  return _("calle");
    case MP_KIND_WATER:   return _("agua");
    case MP_KIND_ADDRESS: return h->sub;
    default:              return h->sub;     /* the OSM class: restaurant, school... */
    }
}

static void result_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    if (i < 0 || i >= a->nhits) return;
    const mp_hit_t *h = &a->hits[i];
    uint32_t x, y;
    mp_lonlat_to_world((float)h->lon / 1e6f, (float)h->lat / 1e6f, &x, &y);
    a->pin_on = true;
    a->pin_cx = x;
    a->pin_cy = y;
    snprintf(a->pin_name, sizeof a->pin_name, "%s", h->name);
    go_to(a, x, y, mp_search_zoom(h));
    show_map(a);
}

static void results_refresh(app_t *a)
{
    if (!a->res_list) return;
    lv_obj_clean(a->res_list);
    lv_obj_t *l = a->res_list;
    char title[64];
    snprintf(title, sizeof title, "\"%s\"", a->query);
    aos_label(l, title, aos_font_title, AOS_C_TEXT);
    bool online_busy = a->photon_id > 0;
    if (a->searching || (online_busy && !a->nhits)) {
        aos_label(l, _("Buscando..."), aos_font_body, AOS_C_DIM);
        return;
    }
    if (!a->nhits) {
        lv_obj_t *t = aos_label(l, a->online ? _("Nada con ese nombre.") :
                                _("Nada con ese nombre en las zonas descargadas."),
                                aos_font_body, AOS_C_DIM);
        lv_obj_set_width(t, SW - 60);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    for (int i = 0; i < a->nhits; i++) {
        const mp_hit_t *h = &a->hits[i];
        char sub[64];
        snprintf(sub, sizeof sub, "%s%s", h->score >= 1000 ? LV_SYMBOL_WIFI " " : "", kind_text(h));
        row(a, l, h->name, sub, result_cb, i);
    }
    if (online_busy) aos_label(l, _("Buscando en línea..."), aos_font_small, AOS_C_DIM);
}

static void search_start(app_t *a)
{
    lv_obj_t *p = panel_new(a, SCR_RESULTS);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_set_style_pad_top(p, 22, 0);
    lv_obj_set_style_pad_bottom(p, 70, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
    a->res_list = p;

    mp_search_key(a->query, a->skey, sizeof a->skey);
    a->nhits = 0;
    a->photon_done = false;
    if (a->photon_id > 0) aos_hal_http_release(a->photon_id);
    a->photon_id = 0;
    a->searching = true;
    a->search_shown = a->search_seq;
    a->want_search = true;
    results_refresh(a);
}

/* In the tick: the offline pass finished -> show it and ask Photon. */
static void search_pump(app_t *a)
{
    if (a->search_seq != a->search_shown) {
        a->search_shown = a->search_seq;
        if (a->screen == SCR_RESULTS && a->online && aos_hal_net_state() == AOS_NET_CONNECTED &&
            !a->photon_done) {
            float lon, lat;
            mp_world_to_lonlat(a->view.cx, a->view.cy, &lon, &lat);
            char url[200], q[96];
            /* the query, URL-encoded (letters, digits and a few signs) */
            int o = 0;
            for (const char *c = a->query; *c && o < (int)sizeof q - 4; c++) {
                if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) q[o++] = *c;
                else o += snprintf(q + o, sizeof q - (size_t)o, "%%%02X", (unsigned char)*c);
            }
            q[o] = 0;
            snprintf(url, sizeof url, "https://photon.komoot.io/api/?limit=8&lat=%.4f&lon=%.4f&q=%s",
                     (double)lat, (double)lon, q);
            a->photon_id = aos_hal_http_get(url, 48 * 1024);
            if (a->photon_id <= 0) a->photon_id = 0;
        }
        if (a->screen == SCR_RESULTS) results_refresh(a);
    }
    const char *pick = getenv("MAPAS_PICK");
    if (pick && pick[0] == '1' && a->screen == SCR_RESULTS && a->photon_done && a->nhits && a->res_list) {
        lv_obj_t *r = lv_obj_get_child(a->res_list, 1);
        if (r) lv_obj_send_event(r, LV_EVENT_CLICKED, NULL);
        return;
    }
    if (a->photon_id > 0) {
        aos_http_state_t hs = aos_hal_http_state(a->photon_id);
        if (hs == AOS_HTTP_BUSY) return;
        if (hs == AOS_HTTP_DONE) {
            int n = a->nhits;
            n += mp_search_photon(aos_hal_http_body(a->photon_id), a->hits, n, MAX_HITS);
            a->nhits = n;
        } else {
            aos_hal_log("mapas", "photon: %d", aos_hal_http_status(a->photon_id));
        }
        aos_hal_http_release(a->photon_id);
        a->photon_id = 0;
        a->photon_done = true;
        if (a->screen == SCR_RESULTS) results_refresh(a);
    }
}

/* ---------------------------------------------------------------------------
 * Life cycle
 * ------------------------------------------------------------------------- */

static void mp_destroy(aos_app_t *self, void *inst);

static void *mp_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    a->root = root;
    a->front = -1;
    a->composing = -1;
    a->cleared = -1;
    s_app = a;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    bool ok = true;
    for (int i = 0; i < 2; i++) {
        a->strip[i] = (uint16_t *)alloc_dma((size_t)SW * STRIP * 2);
        a->back[i].px = (uint16_t *)malloc((size_t)BW * BH * 2);
        ok &= a->strip[i] && a->back[i].px;
    }
    if (!ok) {
        mp_destroy(self, a);
        aos_ui_toast(_("Sin memoria"), 2000);
        return NULL;
    }
    uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
    mp_font_bake(&a->fonts[MP_FONT_S], &aos_montserrat_14);
    mp_font_bake(&a->fonts[MP_FONT_M], &aos_montserrat_16);
    mp_font_bake(&a->fonts[MP_FONT_L], &aos_montserrat_20);
    aos_hal_log("mapas", "fonts baked in %u ms", (unsigned)((uint32_t)aos_hal_uptime_ms() - t0));

    int32_t on = 1;
    aos_hal_pref_get_i32("map_online", &on);
    a->online = on != 0;
    aos_hal_pref_get_str("map_tpl", a->tpl, sizeof a->tpl);
    view_load(a);
    zones_load(a);
    if (mp_maps_dir()) {
        char path[128];
        snprintf(path, sizeof path, "%s/bench.txt", mp_maps_dir());
        FILE *fp = fopen(path, "rb");
        if (fp) {
            fclose(fp);
            a->bench = true;
            a->bench_t = (uint32_t)aos_hal_uptime_ms();
            /* always the same place: the centre of Buenos Aires at z13 */
            uint32_t bx, by;
            mp_lonlat_to_world(-58.3816f, -34.6037f, &bx, &by);
            go_to(a, bx, by, 13.0f);
            aos_hal_log("mapas", "bench.txt found: timing a scripted pan and zoom");
        }
    }

    a->map = lv_obj_create(root);
    lv_obj_remove_style_all(a->map);
    lv_obj_set_size(a->map, SW, SH);
    lv_obj_set_pos(a->map, 0, 0);
    a->cv = (uint16_t *)malloc((size_t)SW * SH * 2);
    if (a->cv) {
        memset(a->cv, 0, (size_t)SW * SH * 2);
        a->canvas = lv_canvas_create(a->map);
        lv_canvas_set_buffer(a->canvas, a->cv, SW, SH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(a->canvas, 0, 0);
        lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *touch = lv_obj_create(a->map);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, SW, SH);
    aos_gesture_attach(touch, AOS_GESTURE_FLAG_FAST_TAP, gesture_cb, a);

    a->viewing = true;
    a->self->desc.flags |= AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;

    /* no labels under the buttons, the scale and the attribution, where the
     * screen sits in a fresh buffer */
    static const int16_t R[][4] = {
        { MENU_X - BTN_R - 4, MENU_Y - BTN_R - 4, MENU_X + BTN_R + 4, MENU_Y + BTN_R + 4 },
        { ZIN_X - BTN_R - 4, ZIN_Y - BTN_R - 4, ZIN_X + BTN_R + 4, ZOUT_Y + BTN_R + 4 },
        { 30, 376, 180, 400 },
        { 20, 404, SW - 20, 426 },
    };
    int16_t rr[4][4];
    for (int i = 0; i < 4; i++) {
        rr[i][0] = (int16_t)(R[i][0] + MARGIN);
        rr[i][1] = (int16_t)(R[i][1] + MARGIN);
        rr[i][2] = (int16_t)(R[i][2] + MARGIN);
        rr[i][3] = (int16_t)(R[i][3] + MARGIN);
    }
    mp_render_reserve((const int16_t (*)[4])rr, 4);

    if (!aos_hal_worker_start_on("mapas", worker, a, WORKER_STACK, 0, 3)) {
        aos_hal_log("mapas", "no worker");
    }
    a->timer = lv_timer_create(tick, TICK_MS, a);

    /* development switch (the board's getenv() is always NULL): MAPAS_Q=text
     * opens the results of that search, MAPAS_PICK=1 then picks the first */
    const char *q = getenv("MAPAS_Q");
    if (q && q[0]) {
        snprintf(a->query, sizeof a->query, "%s", q);
        search_start(a);
    }
    return a;
}

static void mp_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    if (a->timer) lv_timer_delete(a->timer);
    a->search_cancel = true;
    aos_hal_worker_stop();
    for (int i = 0; i < NQ; i++)
        if (a->q[i].state != Q_FREE && a->q[i].id > 0) aos_hal_http_release(a->q[i].id);
    if (a->tpl_id > 0) aos_hal_http_release(a->tpl_id);
    if (a->photon_id > 0) aos_hal_http_release(a->photon_id);
    if (a->low_latency) aos_hal_net_low_latency(false);
    if (a->front >= 0) view_save(a);
    for (int i = 0; i < MP_FONTS; i++) mp_font_free(&a->fonts[i]);
    for (int i = 0; i < 2; i++) {
        free_dma(a->strip[i]);
        free(a->back[i].px);
    }
    if (self && self->root) lv_obj_clean(self->root);   /* the canvas uses cv */
    free(a->cv);
    s_app = NULL;
    lv_free(a);
}

static bool mp_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return false;
    if (a->screen == SCR_RESULTS) {
        a->search_cancel = true;
        show_keys(a);
        return true;
    }
    if (a->screen != SCR_MAP) {
        show_map(a);
        return true;
    }
    return false;
}

/* A folded map: three panels and a pin. */
static const uint8_t MP_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -22, 2, 24, 64, 4, AIC_C_LIT(0x3E8E5A), 255),
    AIC_RECT(AIC_CENTER, 0, -2, 24, 64, 4, AIC_C_LIT(0x2F6FB0), 255),
    AIC_RECT(AIC_CENTER, 22, 2, 24, 64, 4, AIC_C_LIT(0x3E8E5A), 255),
    AIC_RECT(AIC_CENTER, 0, -14, 26, 26, AIC_CIRCLE, AIC_C_LIT(0xFF5A4E), 255),
    AIC_RECT(AIC_CENTER, 0, -14, 10, 10, AIC_CIRCLE, AIC_C_LIT(0xFFFFFF), 255),
    AIC_END
};

static bool mp_init(aos_app_t *app)
{
    app->desc.id       = "demo.mapas";
    app->desc.name     = "Mapas";
    app->desc.icon     = LV_SYMBOL_GPS;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, MP_ICON, sizeof MP_ICON);
    app->desc.color_a  = 0x1F5C3A;
    app->desc.color_b  = 0x0B2418;
    app->desc.order    = 164;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN;

    app->create  = mp_create;
    app->destroy = mp_destroy;
    app->back    = mp_back;
    return true;
}

AOS_APP_ENTRY(mp_init);
