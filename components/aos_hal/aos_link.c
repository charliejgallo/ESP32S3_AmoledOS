/*
 * AmoledOS - Link: the common layer (docs/LINK.md).
 *
 * The same file on the board and in the simulator, over the raw layer of
 * aos_link_internal.h. What lives here:
 *
 *   - beacons once a second (name, what the watch offers), the neighbours
 *     of the last five seconds with their RSSI;
 *   - pairing by the bump: a knock here and a bump frame from a neighbour
 *     within 400 ms of each other, close by, make partners; the key is
 *     SHA-256 over both MACs and both nonces, the partner lives in NVS;
 *   - the reliable channel to the partner: go-back-N with a window of four,
 *     cumulative acknowledgements, retransmit after 80 ms, in order, and
 *     a "lost" state after too many retries;
 *   - the fast channel: send and forget, the last one wins;
 *   - the test protocol /api/link drives: numbered frames, an echo, a bulk
 *     transfer over the reliable channel with losses forced on purpose.
 *
 * Threads: on the board the raw layer delivers frames and runs the poll
 * from its own task; the apps call the public functions from LVGL's task.
 * Everything shared sits behind aos_link_lock(), which the raw layer
 * provides (nothing at all in the simulator, which is one thread).
 *
 * Frames (all start with "AOSL", the protocol version, a type):
 *   'B' beacon          name[24] offer[24]                       broadcast
 *   'P' bump            nonce u32, name[24]                      broadcast
 *   'C' / 'K' confirm   —                                        partner, encrypted
 *   'D' reliable data   seq u16, payload                         partner
 *   'A' reliable ack    seq u16 (the next one expected)          partner
 *   'S' reliable sync   seq u16 (the sender's next; the receiver takes it) partner
 *   'F' fast data       payload                                  partner
 *   'T' 'E' 'R' test    seq u32 (+ echo)                         as asked
 */
#include "aos_hal.h"
#include "aos_link_internal.h"

#include <stdio.h>
#include <string.h>

#include "mbedtls/sha256.h"

#define LINK_MAGIC          "AOSL"
#define LINK_PROTO          1
#define LINK_HDR            6

#define LINK_T_BEACON       'B'
#define LINK_T_BUMP         'P'
#define LINK_T_CONFIRM      'C'
#define LINK_T_CONFIRM_ACK  'K'
#define LINK_T_DATA         'D'
#define LINK_T_ACK          'A'
#define LINK_T_SYNC         'S'      /* sender -> receiver: my next sequence is N */
#define LINK_T_FAST         'F'
#define LINK_T_TEST         'T'
#define LINK_T_TEST_ECHO_REQ 'E'
#define LINK_T_TEST_ECHO    'R'

#define LINK_BEACON_MS      1000
#define LINK_NEIGHBOUR_TTL  5000
#define LINK_BUMP_WINDOW_MS 400
#define LINK_PAIR_QUIET_MS  3000
#define LINK_RSSI_NEAR      (-50)
#define LINK_PMK            "AmoledOS-link-01"

#define LINK_RING_LEN       16          /* fast frames waiting for the app */
#define LINK_REL_WINDOW     4
#define LINK_REL_QUEUE      16          /* reliable frames waiting to go */
#define LINK_REL_RX_RING    16
#define LINK_REL_RTO_MS     80
#define LINK_REL_MAX_RETRY  40          /* 40 x 80 ms = 3.2 s without an ack: lost */
#define LINK_REL_PAYLOAD    (AOS_LINK_MAX_FRAME - LINK_HDR - 2)
#define LINK_RTT_SLOTS      64

typedef struct {
    uint8_t  mac[6];
    int8_t   rssi;
    uint16_t len;
    uint8_t  data[AOS_LINK_MAX_FRAME];
} frame_t;

typedef struct {
    bool     used;
    uint8_t  mac[6];
    char     name[AOS_LINK_NAME_MAX + 1];
    char     app[AOS_LINK_NAME_MAX + 1];
    int8_t   rssi;
    uint32_t seen_ms;
} neighbour_t;

typedef struct {
    uint16_t seq;
    uint16_t len;
    uint32_t sent_ms;
    uint8_t  data[LINK_REL_PAYLOAD];
} rel_frame_t;

static bool             s_started;
static aos_link_stats_t s_stats;
static char             s_offer[AOS_LINK_NAME_MAX + 1];
static neighbour_t      s_neighbours[AOS_LINK_NEIGHBOURS];
static uint32_t         s_last_beacon_ms;

static frame_t          s_fast[LINK_RING_LEN];
static volatile uint32_t s_fast_head, s_fast_tail;

static bool             s_pairing;
static uint32_t         s_pair_events;
static uint32_t         s_my_bump_ms, s_my_nonce;
static uint32_t         s_pair_quiet_until;
static uint32_t         s_last_confirm_ms;
static struct {
    bool valid; uint8_t mac[6]; char name[AOS_LINK_NAME_MAX + 1];
    uint32_t nonce, at_ms; int8_t rssi;
} s_their_bump;
static struct {
    bool valid, confirmed; uint8_t mac[6]; char name[AOS_LINK_NAME_MAX + 1]; uint8_t lmk[16];
} s_partner;

/* reliable: sender */
static rel_frame_t      s_rel_q[LINK_REL_QUEUE];
static uint32_t         s_rel_head, s_rel_tail;   /* app pushes at head, poll sends from tail */
static uint32_t         s_rel_inflight;
static uint16_t         s_rel_next_seq;
static uint32_t         s_rel_retries;
static bool             s_rel_lost;
static bool             s_rel_synced;      /* the receiver knows where my numbering starts */
static uint32_t         s_rel_sync_sent_ms;
/* reliable: receiver */
static uint16_t         s_rel_expect;
static frame_t          s_rel_rx[LINK_REL_RX_RING];
static volatile uint32_t s_rel_rx_head, s_rel_rx_tail;

static volatile bool    s_tx_busy;
static uint32_t         s_tx_busy_since;

static struct {
    bool     running, broadcast, echo;
    uint8_t  mac[6];
    uint32_t n, done, gap_ms, next_at_ms, started_ms;
    uint16_t len;
} s_test;
static uint32_t         s_test_seq;
static uint32_t         s_rtt_sent_ms[LINK_RTT_SLOTS], s_rtt_seq[LINK_RTT_SLOTS];
static uint32_t         s_test_last_seq;
static bool             s_test_have_seq;
static struct {
    bool     sending;
    uint32_t bytes, sent, seq, started_ms;
} s_bulk;
static uint32_t         s_bulk_rx_bytes, s_bulk_rx_bad, s_bulk_rx_seq;
static uint32_t         s_drop_pct, s_drop_count, s_drop_lcg;
static bool             s_bulk_receiver;   /* the poll drains the reliable ring itself, for the test */

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ---- small helpers ------------------------------------------------------- */

static void hex_of(const uint8_t *b, size_t n, char *out)
{
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = '\0';
}

static bool unhex(const char *in, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(in + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static void hdr(uint8_t *f, uint8_t type)
{
    memcpy(f, LINK_MAGIC, 4);
    f[4] = LINK_PROTO;
    f[5] = type;
}

static bool raw_send(const uint8_t *mac, const uint8_t *f, size_t len)
{
    bool ok = aos_link_raw_send(mac, f, len);
    if (ok) {
        s_stats.sent++;
        s_tx_busy = true;
        s_tx_busy_since = aos_link_now_ms();
    } else {
        s_stats.send_err++;
    }
    return ok;
}

static bool tx_free(void)
{
    if (s_tx_busy && aos_link_now_ms() - s_tx_busy_since > 50) {
        s_tx_busy = false;              /* the callback never came: do not hang */
    }
    return !s_tx_busy;
}

/* ---- neighbours and beacons --------------------------------------------- */

static neighbour_t *neighbour_slot(const uint8_t mac[6])
{
    neighbour_t *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
        neighbour_t *n = &s_neighbours[i];
        if (n->used && memcmp(n->mac, mac, 6) == 0) {
            return n;
        }
        if (!n->used && !free_slot) {
            free_slot = n;
        }
        if (n->used && (!oldest || n->seen_ms < oldest->seen_ms)) {
            oldest = n;
        }
    }
    neighbour_t *n = free_slot ? free_slot : oldest;
    memset(n, 0, sizeof *n);
    n->used = true;
    memcpy(n->mac, mac, 6);
    return n;
}

static void send_beacon(void)
{
    uint8_t f[LINK_HDR + 2 * AOS_LINK_NAME_MAX];
    hdr(f, LINK_T_BEACON);
    memset(f + LINK_HDR, 0, sizeof f - LINK_HDR);
    const char *me = aos_hal_device_name();
    memcpy(f + LINK_HDR, me, strnlen(me, AOS_LINK_NAME_MAX));
    memcpy(f + LINK_HDR + AOS_LINK_NAME_MAX, s_offer, strnlen(s_offer, AOS_LINK_NAME_MAX));
    raw_send(BROADCAST, f, sizeof f);
}

/* ---- the partner ---------------------------------------------------------- */

static void derive_lmk(const uint8_t a[6], const uint8_t b[6], uint32_t na, uint32_t nb, uint8_t out[16])
{
    uint8_t material[6 + 6 + 4 + 8];
    const uint8_t *lo = memcmp(a, b, 6) < 0 ? a : b;
    const uint8_t *hi = lo == a ? b : a;
    memcpy(material, lo, 6);
    memcpy(material + 6, hi, 6);
    uint32_t mix = na ^ nb;
    memcpy(material + 12, &mix, 4);
    memcpy(material + 16, LINK_PMK, 8);
    uint8_t digest[32];
    mbedtls_sha256(material, sizeof material, digest, 0);
    memcpy(out, digest, 16);
}

static void partner_save(void)
{
    char hex[33];
    hex_of(s_partner.mac, 6, hex);
    aos_hal_pref_set_str("lk_peer", s_partner.valid ? hex : "");
    hex_of(s_partner.lmk, 16, hex);
    aos_hal_pref_set_str("lk_lmk", s_partner.valid ? hex : "");
    aos_hal_pref_set_str("lk_pname", s_partner.valid ? s_partner.name : "");
}

static void partner_load(void)
{
    char hex[40], name[AOS_LINK_NAME_MAX + 1];
    memset(&s_partner, 0, sizeof s_partner);
    if (aos_hal_pref_get_str("lk_peer", hex, sizeof hex) && strlen(hex) == 12 &&
        unhex(hex, s_partner.mac, 6) &&
        aos_hal_pref_get_str("lk_lmk", hex, sizeof hex) && strlen(hex) == 32 &&
        unhex(hex, s_partner.lmk, 16)) {
        s_partner.valid = true;
        if (aos_hal_pref_get_str("lk_pname", name, sizeof name)) {
            snprintf(s_partner.name, sizeof s_partner.name, "%s", name);
        }
    }
}

static void reliable_reset(void)
{
    s_rel_head = s_rel_tail = 0;
    s_rel_inflight = 0;
    /* A fresh start, on a number the other side cannot mistake for the
     * old one's neighbour: the second bulk test on the boards (2026-09-19)
     * restarted at 1 while the receiver still expected 415, whose acks the
     * sender read as "everything before 415 is done" and declared 100 KB
     * delivered that never arrived. The receiver learns the start from 'S'. */
    s_rel_next_seq = (uint16_t)((aos_link_now_ms() * 2654435761u) >> 16) | 1u;
    s_rel_retries = 0;
    s_rel_lost = false;
    s_rel_synced = false;
    s_rel_sync_sent_ms = 0;
    s_rel_rx_head = s_rel_rx_tail = 0;
}

static void send_confirm(uint8_t type)
{
    uint8_t f[LINK_HDR];
    hdr(f, type);
    raw_send(s_partner.mac, f, sizeof f);
}

static void pair_with(const uint8_t mac[6], const char *name, uint32_t their_nonce)
{
    if (s_partner.valid && memcmp(s_partner.mac, mac, 6) != 0) {
        aos_link_raw_set_partner(s_partner.mac, NULL);
    }
    memset(&s_partner, 0, sizeof s_partner);
    s_partner.valid = true;
    memcpy(s_partner.mac, mac, 6);
    snprintf(s_partner.name, sizeof s_partner.name, "%s", name);
    derive_lmk(s_stats.own_mac, mac, s_my_nonce, their_nonce, s_partner.lmk);
    aos_link_raw_set_partner(s_partner.mac, s_partner.lmk);
    partner_save();
    reliable_reset();
    s_pair_events++;
    s_their_bump.valid = false;
    /* One knock was 17 pairings on 2026-09-19: the case keeps ringing above
     * the bump threshold, and every ring re-paired with a fresh key. */
    s_pair_quiet_until = aos_link_now_ms() + LINK_PAIR_QUIET_MS;
    send_confirm(LINK_T_CONFIRM);
}

static bool bumps_match(uint32_t mine, uint32_t theirs)
{
    uint32_t d = mine > theirs ? mine - theirs : theirs - mine;
    return mine && theirs && d <= LINK_BUMP_WINDOW_MS;
}

static bool quiet(void)
{
    return (int32_t)(aos_link_now_ms() - s_pair_quiet_until) < 0;
}

static bool from_partner(const uint8_t mac[6])
{
    return s_partner.valid && memcmp(mac, s_partner.mac, 6) == 0;
}

/* ---- the reliable channel ------------------------------------------------- */

static void rel_send_frame(rel_frame_t *r)
{
    uint8_t f[AOS_LINK_MAX_FRAME];
    hdr(f, LINK_T_DATA);
    memcpy(f + LINK_HDR, &r->seq, 2);
    memcpy(f + LINK_HDR + 2, r->data, r->len);
    r->sent_ms = aos_link_now_ms();
    raw_send(s_partner.mac, f, LINK_HDR + 2 + r->len);
    s_stats.rel_tx++;
}

static void rel_send_ack(void)
{
    uint8_t f[LINK_HDR + 2];
    hdr(f, LINK_T_ACK);
    memcpy(f + LINK_HDR, &s_rel_expect, 2);
    raw_send(s_partner.mac, f, sizeof f);
}

static bool partner_seen_recently(uint32_t now)
{
    for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
        const neighbour_t *nb = &s_neighbours[i];
        if (nb->used && memcmp(nb->mac, s_partner.mac, 6) == 0 && now - nb->seen_ms <= LINK_NEIGHBOUR_TTL) {
            return true;
        }
    }
    return false;
}

static void rel_poll(void)
{
    if (!s_partner.valid || s_rel_lost) {
        return;
    }
    uint32_t now = aos_link_now_ms();
    uint32_t pending = s_rel_head - s_rel_tail;
    if (!s_rel_synced) {
        /* tell the receiver where my numbering starts, while there is
         * something to send or the partner is around, twice a second */
        if ((pending || partner_seen_recently(now)) && now - s_rel_sync_sent_ms >= 500 && tx_free()) {
            s_rel_sync_sent_ms = now;
            uint16_t start = pending ? s_rel_q[s_rel_tail % LINK_REL_QUEUE].seq : s_rel_next_seq;
            uint8_t f[LINK_HDR + 2];
            hdr(f, LINK_T_SYNC);
            memcpy(f + LINK_HDR, &start, 2);
            raw_send(s_partner.mac, f, sizeof f);
        }
        return;
    }
    if (s_rel_inflight) {
        rel_frame_t *oldest = &s_rel_q[s_rel_tail % LINK_REL_QUEUE];
        if (now - oldest->sent_ms > LINK_REL_RTO_MS) {
            if (++s_rel_retries > LINK_REL_MAX_RETRY) {
                s_rel_lost = true;
                s_stats.rel_lost++;
                return;
            }
            s_stats.rel_retx++;
            s_rel_inflight = 0;         /* go back: resend from the oldest */
        }
    }
    if (s_rel_inflight < LINK_REL_WINDOW && s_rel_inflight < pending && tx_free()) {
        rel_send_frame(&s_rel_q[(s_rel_tail + s_rel_inflight) % LINK_REL_QUEUE]);
        s_rel_inflight++;
    }
}

static void rel_on_ack(uint16_t next_expected)
{
    if ((int16_t)(next_expected - s_rel_next_seq) > 0) {
        return;                         /* beyond anything I sent: stale numbering */
    }
    if (!s_rel_synced) {
        uint16_t start = s_rel_head != s_rel_tail ? s_rel_q[s_rel_tail % LINK_REL_QUEUE].seq : s_rel_next_seq;
        if (next_expected == start) {
            s_rel_synced = true;        /* the receiver took my start */
        }
        return;
    }
    while (s_rel_head != s_rel_tail) {
        rel_frame_t *oldest = &s_rel_q[s_rel_tail % LINK_REL_QUEUE];
        int16_t diff = (int16_t)(next_expected - oldest->seq);
        if (diff <= 0) {
            break;
        }
        s_rel_tail++;
        if (s_rel_inflight) {
            s_rel_inflight--;
        }
        s_rel_retries = 0;
        s_stats.rel_acked++;
    }
}

static void rel_on_data(const frame_t *f)
{
    if (f->len < LINK_HDR + 2) {
        return;
    }
    uint16_t seq;
    memcpy(&seq, f->data + LINK_HDR, 2);
    if (s_drop_pct) {
        s_drop_lcg = s_drop_lcg * 1103515245u + 12345u;
        if ((s_drop_lcg >> 16) % 100 < s_drop_pct) {
            s_drop_count++;
            return;                     /* dropped on purpose, for the test */
        }
    }
    if (seq == s_rel_expect) {
        uint32_t next = (s_rel_rx_head + 1) % LINK_REL_RX_RING;
        if (next == s_rel_rx_tail) {
            return;                     /* the app is not draining: no ack, they resend */
        }
        frame_t *slot = &s_rel_rx[s_rel_rx_head];
        memcpy(slot->mac, f->mac, 6);
        slot->rssi = f->rssi;
        slot->len  = f->len - LINK_HDR - 2;
        memcpy(slot->data, f->data + LINK_HDR + 2, slot->len);
        __sync_synchronize();
        s_rel_rx_head = next;
        s_rel_expect++;
        s_stats.rel_rx++;
    }
    rel_send_ack();
}

/* ---- the test protocol ---------------------------------------------------- */

static void handle_test(const frame_t *f)
{
    if (f->len < LINK_HDR + 4) {
        return;
    }
    uint32_t seq;
    memcpy(&seq, f->data + LINK_HDR, 4);
    uint32_t now = aos_link_now_ms();
    switch (f->data[5]) {
    case LINK_T_TEST:
    case LINK_T_TEST_ECHO_REQ:
        s_stats.test_rx++;
        if (s_test_have_seq && seq > s_test_last_seq + 1) {
            s_stats.test_lost += seq - s_test_last_seq - 1;
        }
        if (!s_test_have_seq || seq > s_test_last_seq) {
            s_test_last_seq = seq;
            s_test_have_seq = true;
        }
        if (f->data[5] == LINK_T_TEST_ECHO_REQ) {
            uint8_t reply[LINK_HDR + 4];
            hdr(reply, LINK_T_TEST_ECHO);
            memcpy(reply + LINK_HDR, &seq, 4);
            raw_send(f->mac, reply, sizeof reply);
        }
        break;
    case LINK_T_TEST_ECHO: {
        uint32_t slot = seq % LINK_RTT_SLOTS;
        if (s_rtt_seq[slot] == seq) {
            uint32_t rtt = (now - s_rtt_sent_ms[slot]) * 1000;
            s_stats.echo_rx++;
            s_stats.rtt_sum_us += rtt;
            if (s_stats.rtt_min_us == 0 || rtt < s_stats.rtt_min_us) s_stats.rtt_min_us = rtt;
            if (rtt > s_stats.rtt_max_us) s_stats.rtt_max_us = rtt;
        }
        break;
    }
    default:
        break;
    }
}

static void test_poll(void)
{
    if (!s_test.running) {
        return;
    }
    uint32_t now = aos_link_now_ms();
    if (s_test.done >= s_test.n) {
        s_test.running = false;
        s_stats.test_ms = now - s_test.started_ms;
        return;
    }
    if (s_test.gap_ms ? (int32_t)(now - s_test.next_at_ms) < 0 : !tx_free()) {
        return;
    }
    uint8_t f[AOS_LINK_MAX_FRAME];
    memset(f, 0xA5, sizeof f);
    hdr(f, s_test.echo ? LINK_T_TEST_ECHO_REQ : LINK_T_TEST);
    uint32_t seq = ++s_test_seq;
    memcpy(f + LINK_HDR, &seq, 4);
    s_rtt_seq[seq % LINK_RTT_SLOTS]     = seq;
    s_rtt_sent_ms[seq % LINK_RTT_SLOTS] = now;
    raw_send(s_test.broadcast ? BROADCAST : s_test.mac, f, s_test.len);
    s_stats.test_tx++;
    s_test.done++;
    s_test.next_at_ms = now + s_test.gap_ms;
}

static void bulk_poll(void)
{
    if (!s_bulk.sending) {
        return;
    }
    if (s_bulk.sent >= s_bulk.bytes || s_rel_lost) {
        if (s_rel_head == s_rel_tail || s_rel_lost) {
            s_bulk.sending = false;
            s_stats.bulk_ms = aos_link_now_ms() - s_bulk.started_ms;
        }
        return;
    }
    if (s_rel_head - s_rel_tail >= LINK_REL_QUEUE) {
        return;
    }
    uint8_t payload[LINK_REL_PAYLOAD];
    uint32_t left = s_bulk.bytes - s_bulk.sent;
    uint16_t len = left < sizeof payload ? (uint16_t)left : (uint16_t)sizeof payload;
    if (len < 5) {
        len = 5;
    }
    payload[0] = 'X';
    memcpy(payload + 1, &s_bulk.seq, 4);
    for (int i = 5; i < len; i++) {
        payload[i] = (uint8_t)(s_bulk.seq * 7 + i);
    }
    /* straight into the queue: we hold the lock already */
    rel_frame_t *r = &s_rel_q[s_rel_head % LINK_REL_QUEUE];
    r->seq = s_rel_next_seq++;
    r->len = len;
    r->sent_ms = 0;
    memcpy(r->data, payload, len);
    s_rel_head++;
    s_bulk.seq++;
    s_bulk.sent += len;
}

/* ---- what the raw layer calls --------------------------------------------- */

/* From the WiFi task on the board: no lock here, it must not wait behind a
 * poll that is writing NVS. Two counters and a flag, nothing else. */
void aos_link_on_sent(bool ok)
{
    if (ok) {
        s_stats.ack_ok++;
    } else {
        s_stats.ack_fail++;
    }
    s_tx_busy = false;
}

void aos_link_on_frame(const uint8_t mac[6], int8_t rssi, const uint8_t *data, size_t len)
{
    if (!s_started || len < LINK_HDR || len > AOS_LINK_MAX_FRAME) {
        return;
    }
    frame_t f;
    memcpy(f.mac, mac, 6);
    f.rssi = rssi;
    f.len  = (uint16_t)len;
    memcpy(f.data, data, len);

    aos_link_lock();
    s_stats.received++;
    s_stats.last_rssi = rssi;
    memcpy(s_stats.last_mac, mac, 6);
    if (memcmp(f.data, LINK_MAGIC, 4) != 0 || f.data[4] != LINK_PROTO) {
        aos_link_unlock();
        return;
    }
    uint32_t now = aos_link_now_ms();
    switch (f.data[5]) {
    case LINK_T_BEACON: {
        if (f.len < LINK_HDR + 2 * AOS_LINK_NAME_MAX) {
            break;
        }
        neighbour_t *n = neighbour_slot(f.mac);
        memcpy(n->name, f.data + LINK_HDR, AOS_LINK_NAME_MAX);
        n->name[AOS_LINK_NAME_MAX] = '\0';
        memcpy(n->app, f.data + LINK_HDR + AOS_LINK_NAME_MAX, AOS_LINK_NAME_MAX);
        n->app[AOS_LINK_NAME_MAX] = '\0';
        n->rssi = rssi;
        n->seen_ms = now;
        if (from_partner(f.mac)) {
            if (n->name[0]) {
                snprintf(s_partner.name, sizeof s_partner.name, "%s", n->name);
            }
            if (!s_partner.confirmed && now - s_last_confirm_ms > 2000) {
                s_last_confirm_ms = now;
                send_confirm(LINK_T_CONFIRM);
            }
        }
        break;
    }
    case LINK_T_BUMP:
        if (f.len < LINK_HDR + 4 + AOS_LINK_NAME_MAX || !s_pairing || quiet()) {
            break;
        }
        s_their_bump.valid = true;
        memcpy(s_their_bump.mac, f.mac, 6);
        memcpy(&s_their_bump.nonce, f.data + LINK_HDR, 4);
        memcpy(s_their_bump.name, f.data + LINK_HDR + 4, AOS_LINK_NAME_MAX);
        s_their_bump.name[AOS_LINK_NAME_MAX] = '\0';
        s_their_bump.at_ms = now;
        s_their_bump.rssi  = rssi;
        if (rssi >= LINK_RSSI_NEAR && bumps_match(s_my_bump_ms, now)) {
            pair_with(f.mac, s_their_bump.name, s_their_bump.nonce);
        }
        break;
    case LINK_T_CONFIRM:
        if (from_partner(f.mac)) {
            s_partner.confirmed = true;
            send_confirm(LINK_T_CONFIRM_ACK);
        }
        break;
    case LINK_T_CONFIRM_ACK:
        if (from_partner(f.mac)) {
            s_partner.confirmed = true;
        }
        break;
    case LINK_T_DATA:
        if (from_partner(f.mac)) {
            rel_on_data(&f);
        }
        break;
    case LINK_T_SYNC:
        if (from_partner(f.mac) && f.len >= LINK_HDR + 2) {
            memcpy(&s_rel_expect, f.data + LINK_HDR, 2);
            rel_send_ack();
        }
        break;
    case LINK_T_ACK:
        if (from_partner(f.mac) && f.len >= LINK_HDR + 2) {
            uint16_t next;
            memcpy(&next, f.data + LINK_HDR, 2);
            rel_on_ack(next);
        }
        break;
    case LINK_T_FAST:
        if (from_partner(f.mac)) {
            uint32_t next = (s_fast_head + 1) % LINK_RING_LEN;
            if (next == s_fast_tail) {
                s_stats.dropped++;
            } else {
                frame_t *slot = &s_fast[s_fast_head];
                memcpy(slot->mac, f.mac, 6);
                slot->rssi = rssi;
                slot->len  = f.len - LINK_HDR;
                memcpy(slot->data, f.data + LINK_HDR, slot->len);
                __sync_synchronize();
                s_fast_head = next;
            }
        }
        break;
    case LINK_T_TEST:
    case LINK_T_TEST_ECHO_REQ:
    case LINK_T_TEST_ECHO:
        handle_test(&f);
        break;
    default:
        break;
    }
    aos_link_unlock();
}

void aos_link_poll(void)
{
    if (!s_started) {
        return;
    }
    aos_link_lock();
    uint32_t now = aos_link_now_ms();
    if (now - s_last_beacon_ms >= LINK_BEACON_MS && tx_free()) {
        s_last_beacon_ms = now;
        send_beacon();
        for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
            if (s_neighbours[i].used && now - s_neighbours[i].seen_ms > LINK_NEIGHBOUR_TTL) {
                s_neighbours[i].used = false;
            }
        }
    }
    test_poll();
    bulk_poll();
    rel_poll();
    if (s_bulk_receiver) {
        aos_hal_link_bulk_drain();
    }
    aos_link_unlock();
}

void aos_link_bump_hint(void)
{
    aos_hal_link_bump();
}

/* ---- public: life cycle --------------------------------------------------- */

bool aos_hal_link_start(void)
{
    if (s_started) {
        return true;
    }
    memset(&s_stats, 0, sizeof s_stats);
    if (!aos_link_raw_start(s_stats.own_mac, &s_stats.version)) {
        return false;
    }
    memset(s_neighbours, 0, sizeof s_neighbours);
    s_fast_head = s_fast_tail = 0;
    s_their_bump.valid = false;
    s_my_bump_ms = 0;
    s_last_beacon_ms = 0;
    s_tx_busy = false;
    s_test.running = false;
    s_bulk.sending = false;
    s_test_have_seq = false;
    partner_load();
    if (s_partner.valid) {
        aos_link_raw_set_partner(s_partner.mac, s_partner.lmk);
    }
    reliable_reset();
    s_started = true;
    return true;
}

void aos_hal_link_stop(void)
{
    if (!s_started) {
        return;
    }
    s_pairing = false;
    s_started = false;
    aos_link_raw_stop();
}

bool aos_hal_link_running(void)
{
    return s_started;
}

void aos_hal_link_set_channel_info(uint8_t channel)
{
    s_stats.channel = channel;
}

/* ---- public: raw, fast, reliable ------------------------------------------ */

bool aos_hal_link_send(const uint8_t mac[6], const void *data, size_t len)
{
    if (!s_started || !data || len == 0 || len > AOS_LINK_MAX_FRAME) {
        return false;
    }
    aos_link_lock();
    bool ok = raw_send(mac ? mac : BROADCAST, data, len);
    aos_link_unlock();
    return ok;
}

bool aos_hal_link_send_partner(const void *data, size_t len)
{
    if (!s_started || !s_partner.valid || !data || len == 0 || len > AOS_LINK_MAX_FRAME - LINK_HDR) {
        return false;
    }
    uint8_t f[AOS_LINK_MAX_FRAME];
    hdr(f, LINK_T_FAST);
    memcpy(f + LINK_HDR, data, len);
    aos_link_lock();
    bool ok = raw_send(s_partner.mac, f, LINK_HDR + len);
    aos_link_unlock();
    return ok;
}

int aos_hal_link_recv(aos_link_frame_t *out)
{
    if (!out || s_fast_tail == s_fast_head) {
        return 0;
    }
    __sync_synchronize();
    const frame_t *f = &s_fast[s_fast_tail];
    memcpy(out->mac, f->mac, 6);
    out->rssi = f->rssi;
    out->len  = f->len;
    memcpy(out->data, f->data, f->len);
    s_fast_tail = (s_fast_tail + 1) % LINK_RING_LEN;
    return (int)out->len;
}

bool aos_hal_link_send_reliable(const void *data, size_t len)
{
    if (!s_started || !s_partner.valid || s_rel_lost || !data || len == 0 || len > LINK_REL_PAYLOAD) {
        return false;
    }
    aos_link_lock();
    if (s_rel_head - s_rel_tail >= LINK_REL_QUEUE) {
        aos_link_unlock();
        return false;                   /* full: the app tries again next tick */
    }
    rel_frame_t *r = &s_rel_q[s_rel_head % LINK_REL_QUEUE];
    r->seq = s_rel_next_seq++;
    r->len = (uint16_t)len;
    r->sent_ms = 0;
    memcpy(r->data, data, len);
    s_rel_head++;
    aos_link_unlock();
    return true;
}

int aos_hal_link_recv_reliable(aos_link_frame_t *out)
{
    if (!out || s_rel_rx_tail == s_rel_rx_head) {
        return 0;
    }
    __sync_synchronize();
    const frame_t *f = &s_rel_rx[s_rel_rx_tail];
    memcpy(out->mac, f->mac, 6);
    out->rssi = f->rssi;
    out->len  = f->len;
    memcpy(out->data, f->data, f->len);
    s_rel_rx_tail = (s_rel_rx_tail + 1) % LINK_REL_RX_RING;
    return (int)out->len;
}

int aos_hal_link_reliable_pending(void)
{
    return (int)(s_rel_head - s_rel_tail);
}

bool aos_hal_link_reliable_lost(void)
{
    return s_rel_lost;
}

void aos_hal_link_reliable_reset(void)
{
    aos_link_lock();
    reliable_reset();
    aos_link_unlock();
}

/* ---- public: discovery and pairing ---------------------------------------- */

void aos_hal_link_offer(const char *app)
{
    snprintf(s_offer, sizeof s_offer, "%s", app ? app : "");
}

int aos_hal_link_neighbours(aos_link_neighbour_t *out, int max)
{
    int n = 0;
    aos_link_lock();
    uint32_t now = aos_link_now_ms();
    for (int i = 0; i < AOS_LINK_NEIGHBOURS && n < max; i++) {
        const neighbour_t *nb = &s_neighbours[i];
        if (!nb->used || now - nb->seen_ms > LINK_NEIGHBOUR_TTL) {
            continue;
        }
        memcpy(out[n].mac, nb->mac, 6);
        snprintf(out[n].name, sizeof out[n].name, "%s", nb->name);
        snprintf(out[n].app, sizeof out[n].app, "%s", nb->app);
        out[n].rssi   = nb->rssi;
        out[n].age_ms = now - nb->seen_ms;
        n++;
    }
    aos_link_unlock();
    return n;
}

void aos_hal_link_pair_enable(bool on)
{
    s_pairing = on && s_started;
    if (!on) {
        s_their_bump.valid = false;
        s_my_bump_ms = 0;
    }
}

bool aos_hal_link_pairing(void)
{
    return s_pairing;
}

void aos_hal_link_bump(void)
{
    if (!s_started || !s_pairing) {
        return;
    }
    aos_link_lock();
    uint32_t now = aos_link_now_ms();
    if (quiet() || now - s_my_bump_ms < LINK_BUMP_WINDOW_MS) {
        aos_link_unlock();
        return;                         /* just paired, or the same knock ringing on */
    }
    s_my_bump_ms = now;
    s_my_nonce   = (now * 2654435761u) ^ (s_stats.received * 40503u) ^ ((uint32_t)s_stats.own_mac[5] << 24);
    uint8_t f[LINK_HDR + 4 + AOS_LINK_NAME_MAX];
    hdr(f, LINK_T_BUMP);
    memcpy(f + LINK_HDR, &s_my_nonce, 4);
    memset(f + LINK_HDR + 4, 0, AOS_LINK_NAME_MAX);
    const char *me = aos_hal_device_name();
    memcpy(f + LINK_HDR + 4, me, strnlen(me, AOS_LINK_NAME_MAX));
    raw_send(BROADCAST, f, sizeof f);
    if (s_their_bump.valid && s_their_bump.rssi >= LINK_RSSI_NEAR &&
        bumps_match(now, s_their_bump.at_ms)) {
        pair_with(s_their_bump.mac, s_their_bump.name, s_their_bump.nonce);
    }
    aos_link_unlock();
}

bool aos_hal_link_partner(aos_link_partner_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (!s_started && !s_partner.valid) {
        partner_load();
    }
    aos_link_lock();
    out->valid     = s_partner.valid;
    out->confirmed = s_partner.confirmed;
    memcpy(out->mac, s_partner.mac, 6);
    snprintf(out->name, sizeof out->name, "%s", s_partner.name);
    uint32_t now = aos_link_now_ms();
    for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
        const neighbour_t *nb = &s_neighbours[i];
        if (nb->used && memcmp(nb->mac, s_partner.mac, 6) == 0 && now - nb->seen_ms <= LINK_NEIGHBOUR_TTL) {
            out->seen   = true;
            out->rssi   = nb->rssi;
            out->age_ms = now - nb->seen_ms;
        }
    }
    aos_link_unlock();
    return out->valid;
}

void aos_hal_link_unpair(void)
{
    aos_link_lock();
    if (s_partner.valid) {
        aos_link_raw_set_partner(s_partner.mac, NULL);
    }
    memset(&s_partner, 0, sizeof s_partner);
    partner_save();
    reliable_reset();
    aos_link_unlock();
}

uint32_t aos_hal_link_pair_events(void)
{
    return s_pair_events;
}

/* ---- public: stats and tests ---------------------------------------------- */

bool aos_hal_link_stats(aos_link_stats_t *out)
{
    if (!out) {
        return false;
    }
    aos_link_lock();
    *out = s_stats;
    out->running = s_started;
    out->rel_pending   = s_rel_head - s_rel_tail;
    out->rel_synced    = s_rel_synced;
    out->bulk_rx_bytes = s_bulk_rx_bytes;
    out->bulk_rx_bad   = s_bulk_rx_bad;
    out->drop_count    = s_drop_count;
    aos_link_unlock();
    return true;
}

void aos_hal_link_stats_reset(void)
{
    aos_link_lock();
    uint32_t v = s_stats.version;
    uint8_t mac[6], ch = s_stats.channel;
    memcpy(mac, s_stats.own_mac, 6);
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.version = v;
    s_stats.channel = ch;
    memcpy(s_stats.own_mac, mac, 6);
    s_test_have_seq = false;
    s_bulk_rx_bytes = s_bulk_rx_bad = s_bulk_rx_seq = 0;
    s_drop_count = 0;
    aos_link_unlock();
}

bool aos_hal_link_test(const uint8_t mac[6], uint32_t n, uint32_t gap_ms, bool echo, uint16_t len)
{
    if (!s_started || s_test.running || n == 0) {
        return false;
    }
    aos_link_lock();
    memset(&s_test, 0, sizeof s_test);
    if (mac) {
        memcpy(s_test.mac, mac, 6);
    } else {
        s_test.broadcast = true;
    }
    s_test.n = n;
    s_test.gap_ms = gap_ms;
    s_test.echo = echo;
    s_test.len = len < LINK_HDR + 4 ? LINK_HDR + 4 : (len > AOS_LINK_MAX_FRAME ? AOS_LINK_MAX_FRAME : len);
    s_test.started_ms = aos_link_now_ms();
    s_test.next_at_ms = s_test.started_ms;
    s_test.running = true;
    aos_link_unlock();
    return true;
}

bool aos_hal_link_test_running(void)
{
    return s_test.running || s_bulk.sending;
}

bool aos_hal_link_bulk_test(uint32_t bytes)
{
    if (!s_started || !s_partner.valid || s_bulk.sending || bytes == 0) {
        return false;
    }
    aos_link_lock();
    reliable_reset();
    memset(&s_bulk, 0, sizeof s_bulk);
    s_bulk.bytes = bytes;
    s_bulk.started_ms = aos_link_now_ms();
    s_bulk.sending = true;
    aos_link_unlock();
    return true;
}

void aos_hal_link_drop_percent(uint32_t pct)
{
    s_drop_pct = pct > 100 ? 100 : pct;
    s_drop_lcg = aos_link_now_ms();
}

void aos_hal_link_bulk_receiver(bool on)
{
    s_bulk_receiver = on;
    if (on) {
        s_bulk_rx_bytes = s_bulk_rx_bad = s_bulk_rx_seq = 0;
        s_drop_count = 0;
    }
}

/* For tests without a bump (the simulator's instances): a partner by MAC,
 * with a key both sides derive from nothing but the MACs. */
void aos_hal_link_set_partner_test(const uint8_t mac[6])
{
    aos_link_lock();
    memset(&s_partner, 0, sizeof s_partner);
    s_partner.valid = true;
    memcpy(s_partner.mac, mac, 6);
    snprintf(s_partner.name, sizeof s_partner.name, "test");
    derive_lmk(s_stats.own_mac, mac, 0, 0, s_partner.lmk);
    aos_link_raw_set_partner(s_partner.mac, s_partner.lmk);
    reliable_reset();
    aos_link_unlock();
}

void aos_hal_link_bulk_drain(void)
{
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) {
        if (f.len >= 5 && f.data[0] == 'X') {
            uint32_t seq;
            memcpy(&seq, f.data + 1, 4);
            bool ok = seq == s_bulk_rx_seq;
            for (int i = 5; ok && i < f.len; i++) {
                ok = f.data[i] == (uint8_t)(seq * 7 + i);
            }
            if (ok) {
                s_bulk_rx_bytes += f.len;
                s_bulk_rx_seq = seq + 1;
            } else {
                s_bulk_rx_bad++;
            }
        }
    }
}
