/*
 * AmoledOS - Orbits: the time as a small solar system, in depth.
 *
 * A sun in the middle; the hour is a ringed planet on the inner orbit, the
 * minute a blue one on the middle orbit, the second a comet on the outer one
 * with its tail behind it. The stars behind are three layers at different
 * depths: tilt the watch and they slide against each other (parallax), so
 * the glass looks like a window onto space. The time in digits sits small
 * under it, for reading at a glance.
 *
 * The sun, its glow and the orbits are drawn once into 'base'; a frame is a
 * copy of that, the stars, the planets, and a row-by-row comparison with what
 * is on the screen, so only what moved is presented: with the wrist still,
 * that is the comet and nothing else.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CX          (FC_SW / 2.0f)
#define CY          (FC_SH / 2.0f - 14.0f)
#define R_HOUR      40.0f
#define R_MIN       62.0f
#define R_SEC       84.0f
#define STARS       150
#define TAIL        14

typedef struct {
    float   x, y;
    uint8_t layer;      /* 0 far .. 2 near */
    uint8_t bright;
} star_t;

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    bool       aod;

    uint16_t  *base;            /* sun, glow, orbits */
    uint16_t  *work;            /* the frame being built */
    star_t     star[STARS];
    float      par_x, par_y;    /* parallax offset, eased */
    fc_grav_t  grav;

    int        sec;             /* the second last seen, and when it began */
    uint32_t   sec_ms;
    struct tm  now;
    char       last_hhmm[8];
    char       last_date[24];
} orbitas_t;

/* -------------------------------------------------------------------------- */

static inline void plot(uint16_t *buf, int x, int y, uint16_t c, uint32_t a)
{
    if (x < 0 || y < 0 || x >= FC_SW || y >= FC_SH) {
        return;
    }
    uint16_t *p = &buf[y * FC_SW + x];
    *p = fc_mix(*p, c, a);
}

static void ring(uint16_t *buf, float r, uint16_t c, uint32_t a)
{
    int n = (int)(2 * FC_PI * r * 1.5f);
    for (int i = 0; i < n; i++) {
        float t = 2 * FC_PI * i / n;
        plot(buf, (int)(CX + r * cosf(t)), (int)(CY + r * sinf(t)), c, a);
    }
}

static void build_base(orbitas_t *o)
{
    uint16_t *b = o->base;
    memset(b, 0, FC_SW * FC_SH * sizeof(uint16_t));
    uint16_t glow = fc_hex(0xFF8A1E), core = fc_hex(0xFFE7A0);
    float dim = o->aod ? 0.35f : 1.0f;
    for (int y = 0; y < FC_SH; y++) {
        for (int x = 0; x < FC_SW; x++) {
            float dx = x + 0.5f - CX, dy = y + 0.5f - CY;
            float r = sqrtf(dx * dx + dy * dy);
            uint32_t a;
            uint16_t c;
            if (r < 11) {
                c = core;
                a = 255;
            } else {
                float g = 11.0f / r;
                a = (uint32_t)(g * g * 220);
                c = glow;
            }
            b[y * FC_SW + x] = fc_scale(c, (uint32_t)(a * dim));
        }
    }
    uint16_t orbit = fc_hex(0x3A4A6A);
    uint32_t oa = o->aod ? 60 : 120;
    ring(b, R_HOUR, orbit, oa);
    ring(b, R_MIN, orbit, oa);
    ring(b, R_SEC, orbit, oa * 2 / 3);
}

/* A lit sphere: the side towards the sun bright, the far side dark. */
static void sphere(uint16_t *buf, float px, float py, float r, uint16_t col, float dim)
{
    float lx = CX - px, ly = CY - py;
    float ll = sqrtf(lx * lx + ly * ly) + 1e-3f;
    lx /= ll;
    ly /= ll;
    int x0 = (int)(px - r - 1), x1 = (int)(px + r + 1);
    int y0 = (int)(py - r - 1), y1 = (int)(py + r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - px) / r, dy = (y + 0.5f - py) / r;
            float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) {
                continue;
            }
            float dz = sqrtf(1.0f - d2);
            float lit = dx * lx + dy * ly + dz * 0.35f;
            lit = lit < 0 ? 0.08f : 0.08f + lit * 0.92f;
            if (lit > 1) lit = 1;
            uint32_t edge = d2 > 0.85f ? (uint32_t)((1.0f - d2) / 0.15f * 255) : 255;
            plot(buf, x, y, fc_scale(col, (uint32_t)(lit * dim * 255)), edge);
        }
    }
}

static void saturn_ring(uint16_t *buf, float px, float py, float r, float dim, bool front)
{
    /* a tilted ellipse, two passes: behind the planet and in front of it */
    uint16_t c = fc_hex(0xD8C090);
    int n = 80;
    for (int i = 0; i < n; i++) {
        float t = 2 * FC_PI * i / n;
        float s = sinf(t);
        if ((s > 0) != front) {
            continue;
        }
        for (float k = 1.5f; k <= 2.1f; k += 0.3f) {
            float ex = px + cosf(t) * r * k;
            float ey = py + s * r * k * 0.32f;
            plot(buf, (int)ex, (int)ey, c, (uint32_t)(200 * dim));
        }
    }
}

static void draw(orbitas_t *o)
{
    memcpy(o->work, o->base, FC_SW * FC_SH * sizeof(uint16_t));
    float dim = o->aod ? 0.4f : 1.0f;

    if (!o->aod) {
        /* the stars, by layer: the near ones slide the most */
        static const float depth[3] = { 0.25f, 0.55f, 1.0f };
        for (int i = 0; i < STARS; i++) {
            const star_t *s = &o->star[i];
            float x = s->x + o->par_x * depth[s->layer];
            float y = s->y + o->par_y * depth[s->layer];
            /* wrap, so the sky never runs out at the edges */
            x -= floorf(x / FC_SW) * FC_SW;
            y -= floorf(y / FC_SH) * FC_SH;
            uint16_t c = fc_rgb(s->bright, s->bright, (uint8_t)LV_MIN(255, s->bright + 30));
            uint16_t *p = &o->work[(int)y * FC_SW + (int)x];
            /* the brighter of the two, channel by channel: a star shows
             * through the faint edge of the glow and not through the sun */
            uint16_t r = LV_MAX(*p & 0xF800, c & 0xF800);
            uint16_t g = LV_MAX(*p & 0x07E0, c & 0x07E0);
            uint16_t b = LV_MAX(*p & 0x001F, c & 0x001F);
            *p = r | g | b;
        }
    }

    /* angles from 12 o'clock, clockwise */
    float sec = o->now.tm_sec + LV_MIN(999u, (uint32_t)aos_hal_uptime_ms() - o->sec_ms) / 1000.0f;
    if (o->aod) {
        sec = 0;
    }
    float a_min = 2 * FC_PI * (o->now.tm_min + sec / 60.0f) / 60.0f;
    float a_hour = 2 * FC_PI * ((o->now.tm_hour % 12) + o->now.tm_min / 60.0f) / 12.0f;
    float a_sec = 2 * FC_PI * sec / 60.0f;

    float hx = CX + R_HOUR * sinf(a_hour), hy = CY - R_HOUR * cosf(a_hour);
    float mx = CX + R_MIN * sinf(a_min), my = CY - R_MIN * cosf(a_min);

    saturn_ring(o->work, hx, hy, 7.0f, dim, false);
    sphere(o->work, hx, hy, 7.0f, fc_hex(0xE8B860), dim);
    saturn_ring(o->work, hx, hy, 7.0f, dim, true);
    sphere(o->work, mx, my, 6.0f, fc_hex(0x4AA0FF), dim);

    if (!o->aod) {
        /* the comet: a head and a tail that fades along the orbit behind it */
        for (int k = TAIL; k >= 0; k--) {
            float t = a_sec - k * 0.022f;
            float cx = CX + R_SEC * sinf(t), cy = CY - R_SEC * cosf(t);
            uint32_t a = (uint32_t)(255 * (1.0f - (float)k / (TAIL + 1)));
            uint16_t c = k == 0 ? fc_hex(0xFFFFFF) : fc_hex(0x9FE8FF);
            plot(o->work, (int)cx, (int)cy, c, a);
            if (k < 4) {
                plot(o->work, (int)cx + 1, (int)cy, c, a / 2);
                plot(o->work, (int)cx, (int)cy + 1, c, a / 2);
                plot(o->work, (int)cx - 1, (int)cy, c, a / 3);
                plot(o->work, (int)cx, (int)cy - 1, c, a / 3);
            }
        }
    }

    /* only what changed goes to the screen */
    int dx0 = FC_SW, dy0 = FC_SH, dx1 = -1, dy1 = -1;
    for (int y = 0; y < FC_SH; y++) {
        uint16_t *dst = o->fc.small + y * FC_SW, *src = o->work + y * FC_SW;
        if (memcmp(dst, src, FC_SW * sizeof(uint16_t)) == 0) {
            continue;
        }
        int a = 0, b = FC_SW - 1;
        while (dst[a] == src[a]) a++;
        while (dst[b] == src[b]) b--;
        memcpy(dst + a, src + a, (size_t)(b - a + 1) * sizeof(uint16_t));
        if (a < dx0) dx0 = a;
        if (b > dx1) dx1 = b;
        if (y < dy0) dy0 = y;
        dy1 = y;
    }
    if (dx1 >= 0) {
        fc_present(&o->fc, dx0, dy0, dx1 + 1, dy1 + 1);
    }
}

static void frame(void *ctx, uint32_t dt_ms)
{
    orbitas_t *o = ctx;
    if (!o || o->aod) {
        return;
    }
    float dt = dt_ms / 1000.0f;
    fc_grav_update(&o->grav, 0.25f);
    /* tilting the watch right moves the near stars left: they are behind */
    float want_x = -o->grav.x * 26.0f, want_y = -(o->grav.y - 0.5f) * 26.0f;
    o->par_x += (want_x - o->par_x) * LV_MIN(1.0f, dt * 6.0f);
    o->par_y += (want_y - o->par_y) * LV_MIN(1.0f, dt * 6.0f);
    draw(o);
}

/* -------------------------------------------------------------------------- */

static void text_update(orbitas_t *o, bool force)
{
    char hhmm[8], date[24];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", o->now.tm_hour, o->now.tm_min);
    snprintf(date, sizeof(date), "%s %d %s", aos_day_name(o->now.tm_wday), o->now.tm_mday,
             aos_month_name(o->now.tm_mon));
    if (force || strcmp(hhmm, o->last_hhmm) != 0) {
        fc_mask_t old = o->fc.mask[0];
        o->fc.mask[0].a = NULL;
        snprintf(o->last_hhmm, sizeof(o->last_hhmm), "%s", hhmm);
        fc_mask_text(&o->fc.mask[0], o->root, hhmm, aos_font_huge, 1.15f,
                     FC_W / 2, 370, o->aod ? fc_hex(0x8A8A8A) : fc_hex(0xFFFFFF), false);
        fc_present_mask(&o->fc, &old);
        free(old.a);
        fc_present_mask(&o->fc, &o->fc.mask[0]);
    }
    if (force || strcmp(date, o->last_date) != 0) {
        fc_mask_t old = o->fc.mask[1];
        o->fc.mask[1].a = NULL;
        snprintf(o->last_date, sizeof(o->last_date), "%s", date);
        fc_mask_text(&o->fc.mask[1], o->root, date, aos_font_small, 1.0f,
                     FC_W / 2, 414, o->aod ? fc_hex(0x4A4A4A) : fc_hex(0x8FA8D0), false);
        fc_present_mask(&o->fc, &old);
        free(old.a);
        fc_present_mask(&o->fc, &o->fc.mask[1]);
    }
}

static void *create(lv_obj_t *root)
{
    orbitas_t *o = lv_malloc_zeroed(sizeof(orbitas_t));
    if (!o) {
        return NULL;
    }
    o->base = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    o->work = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    if (!o->base || !o->work || !fc_init(&o->fc, root)) {
        free(o->base);
        free(o->work);
        lv_free(o);
        return NULL;
    }
    o->root = root;
    uint32_t seed = 0x5EED1234u;
    for (int i = 0; i < STARS; i++) {
        star_t *s = &o->star[i];
        s->x = fc_randf(&seed) * FC_SW;
        s->y = fc_randf(&seed) * FC_SH;
        s->layer = (uint8_t)(i % 3);
        s->bright = (uint8_t)(70 + s->layer * 50 + fc_randf(&seed) * 60);
    }
    build_base(o);
    aos_hal_time_now(&o->now);
    o->sec = o->now.tm_sec;
    o->sec_ms = (uint32_t)aos_hal_uptime_ms();
    fc_grav_update(&o->grav, 1.0f);
    text_update(o, true);
    draw(o);
    return o;
}

static void refresh(void *ctx, const struct tm *now)
{
    orbitas_t *o = ctx;
    if (!o) {
        return;
    }
    /* where inside the second we are, for a comet that does not tick */
    if (now->tm_sec != o->sec) {
        o->sec = now->tm_sec;
        o->sec_ms = (uint32_t)aos_hal_uptime_ms();
    }
    o->now = *now;
    text_update(o, false);
    if (o->aod) {
        draw(o);            /* once a minute: the planets move on */
    }
}

static void set_aod(void *ctx, bool aod)
{
    orbitas_t *o = ctx;
    if (!o || o->aod == aod) {
        return;
    }
    o->aod = aod;
    build_base(o);
    text_update(o, true);
    draw(o);
}

static void destroy(void *ctx)
{
    orbitas_t *o = ctx;
    if (!o) {
        return;
    }
    fc_deinit(&o->fc);
    free(o->base);
    free(o->work);
    lv_free(o);
}

static bool orbitas_get(aos_watchface_t *face)
{
    face->id       = "face.orbitas";
    face->name     = N_("Órbitas");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->frame    = frame;
    face->frame_ms = 40;
    return true;
}

AOS_FACE_ENTRY(orbitas_get);
