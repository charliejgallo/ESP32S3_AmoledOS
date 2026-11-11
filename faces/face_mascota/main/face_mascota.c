/*
 * AmoledOS - Pet: a small animal that lives under the time.
 *
 * Six animals (cat, dog, rabbit, bear, frog, penguin) in nine colours. It
 * breathes, blinks and looks around; it leans, and its ears and tail swing,
 * with the tilt of the watch. Stroke it (a tap on it) and it is happy, with
 * hearts; tap beside it and it looks there; shake the watch and it is dizzy.
 * It sleeps at night and when the battery is nearly flat, and it eats while
 * the watch charges: its energy is the watch's.
 *
 * A tap on the time changes it: the left half the animal, the right half the
 * colour. Its name is set in the portal (/face_mascota), which writes the
 * same file the taps do, face_mascota.cfg: "animal|colour|name".
 *
 * Like Claudito, it is not a sprite but a figure made of parts drawn every
 * frame from a pose: that is what lets it squash, lean and blink. The parts
 * are the soft shapes of face_draw.h at half resolution.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FACE_ID     "face.mascota"
#define CFG_FILE    "face_mascota.cfg"
#define GROUND      194.0f          /* where its feet are, half-res pixels */
#define HEARTS      8

enum { CAT = 0, DOG, RABBIT, BEAR, FROG, PENGUIN, ANIMALS };
enum { EYE_OPEN = 0, EYE_BLINK, EYE_HAPPY, EYE_SLEEP, EYE_DIZZY };

static const uint32_t COLOUR[] = {
    0xF29A4A, 0x9AA3AD, 0xF2EEE6, 0x4A4A58, 0x9A6A42, 0xE8CFA0, 0xF2A0B8, 0x8FC8F0, 0x7CC860,
};
#define COLOURS     (int)(sizeof(COLOUR) / sizeof(COLOUR[0]))
static const uint8_t DEFAULT_COLOUR[ANIMALS] = { 0, 4, 2, 5, 8, 3 };
/* Spanish is the source language; the names are the user's to change */
static const char *const DEFAULT_NAME[ANIMALS] = {
    N_("Michi"), N_("Firulais"), N_("Copito"), N_("Osito"), N_("Sapo"), N_("Pingu"),
};

typedef struct { float x, y, vy, life; } heart_t;

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    uint16_t  *base, *work;
    bool       aod;

    int        animal, colour;
    char       name[20];            /* empty: the animal's own */

    float      t;                   /* seconds, for every sway */
    float      blink_in;            /* seconds until the next blink */
    float      blink;               /* > 0 while the eyes are shut */
    float      happy, dizzy, look;  /* seconds left of each */
    float      look_x, look_y;      /* -1..1, where it looks */
    float      lean;                /* eased tilt, -1..1 */
    float      since;               /* seconds since the last frame drawn */
    bool       was_blinking;
    heart_t    heart[HEARTS];
    fc_grav_t  grav;
    float      last_mag;
    uint32_t   seed;

    bool       night, charging, tired;
    char       last_hhmm[8], last_date[24], last_name[40];
} mascota_t;

/* -------------------------------------------------------------------------- */
/* Its file                                                                   */
/* -------------------------------------------------------------------------- */

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

static void cfg_load(mascota_t *m)
{
    char path[96], txt[64] = "";
    cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        size_t got = fread(txt, 1, sizeof(txt) - 1, f);
        txt[got] = '\0';
        fclose(f);
    }
    m->animal = CAT;
    m->colour = -1;
    m->name[0] = '\0';
    int a = 0, c = -1;
    char *bar = strchr(txt, '|');
    if (bar) {
        a = atoi(txt);
        c = atoi(bar + 1);
        char *bar2 = strchr(bar + 1, '|');
        if (bar2) {
            size_t w = 0;
            for (const char *s = bar2 + 1; *s && *s != '\n' && *s != '\r' && w + 1 < sizeof(m->name); s++) {
                m->name[w++] = *s;
            }
            m->name[w] = '\0';
        }
    }
    m->animal = (a >= 0 && a < ANIMALS) ? a : CAT;
    m->colour = (c >= 0 && c < COLOURS) ? c : DEFAULT_COLOUR[m->animal];
}

static void cfg_save(const mascota_t *m)
{
    char path[96];
    cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d|%d|%s\n", m->animal, m->colour, m->name);
        fclose(f);
    }
}

/* -------------------------------------------------------------------------- */
/* Drawing it                                                                 */
/* -------------------------------------------------------------------------- */

static void eye(uint16_t *b, float x, float y, int state, float lx, float ly, float r)
{
    const uint16_t ink = fc_hex(0x20202A);
    switch (state) {
    case EYE_OPEN:
        fd_ellipse(b, x + lx * 2, y + ly * 1.5f, r, r * 1.15f, 0, ink, 255);
        fd_circle(b, x + lx * 2 - r * 0.3f, y + ly * 1.5f - r * 0.4f, r * 0.35f, 0xFFFF, 255);
        break;
    case EYE_BLINK:
        fd_line(b, x - r, y, x + r, y, 1.6f, ink, 255);
        break;
    case EYE_HAPPY:
        fd_line(b, x - r, y + 1, x, y - r * 0.6f, 1.8f, ink, 255);
        fd_line(b, x, y - r * 0.6f, x + r, y + 1, 1.8f, ink, 255);
        break;
    case EYE_SLEEP:
        fd_line(b, x - r, y - 1, x, y + r * 0.5f, 1.6f, ink, 255);
        fd_line(b, x, y + r * 0.5f, x + r, y - 1, 1.6f, ink, 255);
        break;
    default:    /* dizzy */
        fd_line(b, x - r, y - r, x + r, y + r, 1.6f, ink, 255);
        fd_line(b, x - r, y + r, x + r, y - r, 1.6f, ink, 255);
        break;
    }
}

static void heart_shape(uint16_t *b, float x, float y, float s, uint32_t a)
{
    const uint16_t red = fc_hex(0xFF4D6D);
    fd_circle(b, x - s * 0.5f, y, s * 0.62f, red, a);
    fd_circle(b, x + s * 0.5f, y, s * 0.62f, red, a);
    fd_tri(b, x - s * 1.05f, y + s * 0.25f, x + s * 1.05f, y + s * 0.25f, x, y + s * 1.5f, red, a);
}

static void draw_pet(mascota_t *m, uint16_t *b)
{
    const uint16_t fur = fc_hex(COLOUR[m->colour]);
    const uint16_t dark = fd_dark(fur, 0.28f), pale = fd_light(fur, 0.55f);
    const uint16_t pink = fc_hex(0xFF9DB0), ink = fc_hex(0x20202A), white = fc_hex(0xFFFFFF);
    const int an = m->animal;
    bool sleeping = m->aod || ((m->night || m->tired) && m->happy <= 0 && m->dizzy <= 0);

    float breath = sinf(m->t * (sleeping ? 1.3f : 2.2f));
    float hop = m->happy > 0 ? -fabsf(sinf(m->t * 9.0f)) * 6.0f : 0;
    float sway = sinf(m->t * 3.1f);
    float cx = FC_SW / 2.0f + m->lean * 5.0f;
    float by = GROUND - 24 + hop;                   /* the body's centre */
    float hx = cx + m->lean * 5.0f, hy = GROUND - 62 + hop - breath * 0.8f + (sleeping ? 4 : 0);
    float ear = m->lean * 0.35f + (m->happy > 0 ? sway * 0.18f : 0);

    /* its shadow on the floor, smaller while it is in the air */
    fd_ellipse(b, cx, GROUND + 3, 34 + hop, 5, 0, fc_hex(0x000000), 150);

    /* the tail, behind everything */
    float wag = (m->happy > 0 ? sinf(m->t * 16.0f) : sway) * (sleeping ? 0.1f : 1.0f);
    if (an == CAT) {
        float tx = cx + 22, ty = by + 12;
        fd_line(b, tx, ty, tx + 16, ty - 8 + wag * 2, 6, dark, 255);
        fd_line(b, tx + 16, ty - 8 + wag * 2, tx + 18 + wag * 5, ty - 28, 6, dark, 255);
    } else if (an == DOG) {
        fd_line(b, cx + 20, by + 4, cx + 30 + wag * 5, by - 12, 6, dark, 255);
    } else if (an == RABBIT) {
        fd_circle(b, cx + 24, by + 10, 7, white, 255);
    } else if (an == BEAR) {
        fd_circle(b, cx + 24, by + 8, 5, dark, 255);
    }

    /* the ears that sit behind the head */
    if (an == RABBIT) {
        for (int s = -1; s <= 1; s += 2) {
            fd_ellipse(b, hx + s * 11 + ear * 14, hy - 40, 7, 21, s * 0.14f + ear, fur, 255);
            fd_ellipse(b, hx + s * 11 + ear * 14, hy - 39, 3.5f, 15, s * 0.14f + ear, pink, 255);
        }
    } else if (an == BEAR) {
        for (int s = -1; s <= 1; s += 2) {
            fd_circle(b, hx + s * 22, hy - 22, 10, fur, 255);
            fd_circle(b, hx + s * 22, hy - 22, 5.5f, pale, 255);
        }
    } else if (an == CAT) {
        for (int s = -1; s <= 1; s += 2) {
            float tipx = hx + s * 27 + ear * 10, tipy = hy - 38;
            fd_tri(b, hx + s * 26, hy - 10, tipx, tipy, hx + s * 6, hy - 26, fur, 255);
            fd_tri(b, hx + s * 23, hy - 16, tipx - s * 2, tipy + 7, hx + s * 11, hy - 25, pink, 255);
        }
    }

    /* feet, body, belly */
    uint16_t foot = an == PENGUIN ? fc_hex(0xFFA030) : (an == FROG ? dark : pale);
    fd_ellipse(b, cx - 13, GROUND - 1 + hop * 0.3f, 10, 5, 0, foot, 255);
    fd_ellipse(b, cx + 13, GROUND - 1 + hop * 0.3f, 10, 5, 0, foot, 255);
    fd_ellipse(b, cx, by, 27 + breath * 0.6f, 23 - breath * 0.6f, 0, fur, 255);
    if (an == PENGUIN) {
        float flap = m->happy > 0 ? sinf(m->t * 14.0f) * 0.5f : 0.0f;
        fd_ellipse(b, cx - 27, by - 2, 6, 17, 0.35f + flap, dark, 255);
        fd_ellipse(b, cx + 27, by - 2, 6, 17, -0.35f - flap, dark, 255);
    }
    fd_ellipse(b, cx, by + 4, 17, 15, 0, an == PENGUIN ? white : pale, 255);

    /* the head */
    if (an == FROG) {
        fd_ellipse(b, hx, hy + 4, 34, 24, 0, fur, 255);
        fd_circle(b, hx - 17, hy - 16, 12, fur, 255);
        fd_circle(b, hx + 17, hy - 16, 12, fur, 255);
    } else {
        fd_ellipse(b, hx, hy, 31, 28, 0, fur, 255);
    }
    if (an == PENGUIN) {
        fd_ellipse(b, hx - 10, hy + 3, 13, 15, 0, white, 255);
        fd_ellipse(b, hx + 10, hy + 3, 13, 15, 0, white, 255);
    }
    if (an == DOG) {    /* its ears hang in front */
        for (int s = -1; s <= 1; s += 2) {
            fd_ellipse(b, hx + s * 28 + ear * 6, hy - 4, 9, 18, s * -0.3f + ear, dark, 255);
        }
    }
    if (an == DOG || an == BEAR) {
        fd_ellipse(b, hx, hy + 11, 13, 9.5f, 0, pale, 255);
    }

    /* the face */
    int state = EYE_OPEN;
    if (m->dizzy > 0)           state = EYE_DIZZY;
    else if (m->happy > 0)      state = EYE_HAPPY;
    else if (sleeping)          state = EYE_SLEEP;
    else if (m->blink > 0)      state = EYE_BLINK;
    float lx = m->look > 0 ? m->look_x : m->lean * 0.6f, ly = m->look > 0 ? m->look_y : 0;
    if (an == FROG) {
        for (int s = -1; s <= 1; s += 2) {
            if (state == EYE_OPEN) {
                fd_circle(b, hx + s * 17, hy - 17, 8, white, 255);
            }
            eye(b, hx + s * 17, hy - 17, state, lx, ly, 4.2f);
        }
    } else {
        eye(b, hx - 12, hy - 2, state, lx, ly, 4.2f);
        eye(b, hx + 12, hy - 2, state, lx, ly, 4.2f);
    }
    /* cheeks: a little always, a lot when it is happy */
    uint32_t blush = m->happy > 0 ? 170 : 70;
    fd_ellipse(b, hx - 21, hy + 9, 6, 3.5f, 0, pink, blush);
    fd_ellipse(b, hx + 21, hy + 9, 6, 3.5f, 0, pink, blush);

    float my = hy + (an == FROG ? 12 : 13);
    bool chewing = m->charging && !sleeping && m->happy <= 0 && sinf(m->t * 9.0f) > 0;
    if (an == PENGUIN) {
        fd_tri(b, hx - 7, hy + 6, hx + 7, hy + 6, hx, hy + 15 + (chewing ? 2 : 0), fc_hex(0xFFA030), 255);
    } else if (an == FROG) {
        fd_line(b, hx - 14, my, hx, my + 3 + (m->happy > 0 ? 3 : 0), 1.8f, ink, 255);
        fd_line(b, hx, my + 3 + (m->happy > 0 ? 3 : 0), hx + 14, my, 1.8f, ink, 255);
    } else {
        /* a nose, and a mouth that opens when it is happy or eating */
        uint16_t nose = (an == CAT || an == RABBIT) ? pink : ink;
        fd_tri(b, hx - 3.5f, hy + 6, hx + 3.5f, hy + 6, hx, hy + 10, nose, 255);
        if (m->happy > 0 || chewing) {
            fd_ellipse(b, hx, my + 2, 5, 4, 0, ink, 255);
            fd_ellipse(b, hx, my + 4, 3, 2, 0, pink, 255);
        } else {
            fd_line(b, hx - 5, my, hx - 2.5f, my + 2, 1.5f, ink, 255);
            fd_line(b, hx - 2.5f, my + 2, hx, my, 1.5f, ink, 255);
            fd_line(b, hx, my, hx + 2.5f, my + 2, 1.5f, ink, 255);
            fd_line(b, hx + 2.5f, my + 2, hx + 5, my, 1.5f, ink, 255);
        }
        if (an == RABBIT && m->happy <= 0 && !chewing) {
            fd_rect(b, (int)hx - 2, (int)my + 2, (int)hx + 2, (int)my + 6, white, 255);
        }
        if (an == CAT) {
            for (int s = -1; s <= 1; s += 2) {
                fd_line(b, hx + s * 17, hy + 8, hx + s * 31, hy + 5, 1, white, 150);
                fd_line(b, hx + s * 17, hy + 11, hx + s * 31, hy + 13, 1, white, 150);
            }
        }
    }

    /* what goes with its state */
    if (m->charging && !sleeping) {             /* a bowl */
        fd_ellipse(b, cx + 52, GROUND - 2, 15, 6, 0, fc_hex(0x4A7FD0), 255);
        fd_ellipse(b, cx + 52, GROUND - 5, 12, 3.5f, 0, fc_hex(0xB07840), 255);
    }
    if (sleeping) {                             /* z z z, drifting up */
        for (int i = 0; i < 3; i++) {
            float ph = fmodf(m->t * 0.5f + i * 0.33f, 1.0f);
            float zx = hx + 30 + ph * 16, zy = hy - 26 - ph * 30, s = 3 + ph * 4;
            uint32_t a = (uint32_t)(220 * (1 - ph));
            fd_line(b, zx - s, zy - s, zx + s, zy - s, 1.5f, white, a);
            fd_line(b, zx + s, zy - s, zx - s, zy + s, 1.5f, white, a);
            fd_line(b, zx - s, zy + s, zx + s, zy + s, 1.5f, white, a);
        }
    }
    if (m->dizzy > 0) {                         /* stars going round its head */
        for (int i = 0; i < 3; i++) {
            float a = m->t * 6.0f + i * 2.094f;
            fd_circle(b, hx + cosf(a) * 26, hy - 34 + sinf(a) * 5, 2.2f, fc_hex(0xFFD60A), 255);
        }
    }
    for (int i = 0; i < HEARTS; i++) {
        if (m->heart[i].life > 0) {
            heart_shape(b, m->heart[i].x, m->heart[i].y, 4.5f,
                        (uint32_t)(255 * LV_MIN(1.0f, m->heart[i].life * 2)));
        }
    }
}

static void render(mascota_t *m)
{
    memcpy(m->work, m->base, FC_SW * FC_SH * sizeof(uint16_t));
    draw_pet(m, m->work);
    if (m->aod) {
        /* dimmed: the same pet, asleep, at a third of the light */
        for (int i = 0; i < FC_SW * FC_SH; i++) {
            if (m->work[i]) {
                m->work[i] = fc_scale(m->work[i], 80);
            }
        }
    }
    fd_show(&m->fc, m->work);
}

/* -------------------------------------------------------------------------- */

static void text_update(mascota_t *m, const struct tm *now, bool force)
{
    char hhmm[8], date[24], name[40];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    snprintf(date, sizeof(date), "%s %d %s", aos_day_name(now->tm_wday), now->tm_mday,
             aos_month_name(now->tm_mon));
    snprintf(name, sizeof(name), "%s", m->name[0] ? m->name : _(DEFAULT_NAME[m->animal]));
    struct { const char *txt; char *last; size_t n; const lv_font_t *font; float scale; int y; uint32_t col, dim; } L[3] = {
        { hhmm, m->last_hhmm, sizeof(m->last_hhmm), aos_font_huge, 1.7f, 64, 0xFFFFFF, 0x8A8A8A },
        { date, m->last_date, sizeof(m->last_date), aos_font_body, 1.0f, 122, 0x0A84FF, 0x5A5A5A },
        { name, m->last_name, sizeof(m->last_name), aos_font_small, 1.0f, 420, 0x8E8E93, 0x4A4A4A },
    };
    for (int i = 0; i < 3; i++) {
        if (!force && strcmp(L[i].txt, L[i].last) == 0) {
            continue;
        }
        fc_mask_t old = m->fc.mask[i];
        m->fc.mask[i].a = NULL;
        snprintf(L[i].last, L[i].n, "%s", L[i].txt);
        fc_mask_text(&m->fc.mask[i], m->root, L[i].txt, L[i].font, L[i].scale, FC_W / 2, L[i].y,
                     fc_hex(m->aod ? L[i].dim : L[i].col), false);
        fc_present_mask(&m->fc, &old);
        free(old.a);
        fc_present_mask(&m->fc, &m->fc.mask[i]);
    }
}

static void world_update(mascota_t *m, const struct tm *now)
{
    m->night = now->tm_hour >= 23 || now->tm_hour < 7;
    aos_battery_t batt;
    if (aos_hal_battery_read(&batt)) {
        m->charging = batt.charging;
        m->tired = batt.percent >= 0 && batt.percent < 15 && !batt.charging;
    }
}

static void frame(void *ctx, uint32_t dt_ms)
{
    mascota_t *m = ctx;
    if (!m || m->aod) {
        return;
    }
    float dt = dt_ms / 1000.0f;
    m->t += dt;
    bool tilting = false, hearts = false;

    float gx, gy, gz;
    if (aos_watchface_gravity(&gx, &gy, &gz)) {
        float mag = sqrtf(gx * gx + gy * gy + gz * gz);
        if (m->last_mag > 0 && fabsf(mag - m->last_mag) > 0.6f) {
            m->dizzy = 2.5f;                    /* a shake */
        }
        m->last_mag = mag;
        float want = LV_CLAMP(-1.0f, gx * 1.6f, 1.0f);
        tilting = fabsf(want - m->lean) > 0.04f;
        m->lean += (want - m->lean) * LV_MIN(1.0f, dt * 5.0f);
    }
    m->blink_in -= dt;
    if (m->blink_in <= 0) {
        m->blink = 0.13f;
        m->blink_in = 2.5f + fc_randf(&m->seed) * 3.5f;
    }
    if (m->blink > 0) m->blink -= dt;
    if (m->happy > 0) m->happy -= dt;
    if (m->dizzy > 0) m->dizzy -= dt;
    if (m->look > 0)  m->look -= dt;
    for (int i = 0; i < HEARTS; i++) {
        heart_t *h = &m->heart[i];
        if (h->life > 0) {
            h->life -= dt;
            h->y -= h->vy * dt;
            h->x += sinf(m->t * 4.0f + i) * 0.3f;
            hearts = true;
        }
    }
    /* Breathing does not need twenty frames a second: while nothing else is
     * happening it is drawn seven times a second, and at once when something
     * does (a blink starting or ending, a stroke, a tilt). On the board the
     * face spent 74 % of the time drawing a pet that was sitting still. */
    bool blinking = m->blink > 0;
    bool lively = m->happy > 0 || m->dizzy > 0 || m->look > 0 || hearts || tilting ||
                  blinking != m->was_blinking;
    m->was_blinking = blinking;
    m->since += dt;
    if (lively || m->since >= 0.14f) {
        m->since = 0;
        render(m);
    }
}

static void *create(lv_obj_t *root)
{
    mascota_t *m = lv_malloc_zeroed(sizeof(mascota_t));
    if (!m) {
        return NULL;
    }
    m->base = calloc(FC_SW * FC_SH, sizeof(uint16_t));
    m->work = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    if (!m->base || !m->work || !fc_init(&m->fc, root)) {
        free(m->base);
        free(m->work);
        lv_free(m);
        return NULL;
    }
    m->root = root;
    m->seed = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    m->blink_in = 2.0f;
    /* the floor: a rug of dim light under it, the rest black */
    fd_ellipse(m->base, FC_SW / 2.0f, GROUND + 4, 74, 13, 0, fc_hex(0x16161C), 255);
    cfg_load(m);
    struct tm now;
    aos_hal_time_now(&now);
    world_update(m, &now);
    text_update(m, &now, true);
    render(m);
    return m;
}

static void refresh(void *ctx, const struct tm *now)
{
    mascota_t *m = ctx;
    if (!m) {
        return;
    }
    world_update(m, now);
    text_update(m, now, false);
}

static void set_aod(void *ctx, bool aod)
{
    mascota_t *m = ctx;
    if (!m || m->aod == aod) {
        return;
    }
    m->aod = aod;
    if (!aod) {
        cfg_load(m);            /* its page may have renamed it meanwhile */
    }
    m->happy = m->dizzy = m->look = 0;
    memset(m->heart, 0, sizeof(m->heart));
    struct tm now;
    aos_hal_time_now(&now);
    world_update(m, &now);
    text_update(m, &now, true);
    render(m);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    mascota_t *m = ctx;
    if (!m || what != AOS_FACE_TOUCH_TAP || m->aod) {
        return false;
    }
    float sx = x / 2.0f, sy = y / 2.0f;
    if (y < 150) {
        /* on the time: another animal (left) or another colour (right) */
        if (x < FC_W / 2) {
            m->animal = (m->animal + 1) % ANIMALS;
            m->colour = DEFAULT_COLOUR[m->animal];
        } else {
            m->colour = (m->colour + 1) % COLOURS;
        }
        cfg_save(m);
        struct tm now;
        aos_hal_time_now(&now);
        text_update(m, &now, false);    /* its name, if it has none of its own */
        m->happy = 1.2f;
        return true;
    }
    if (fabsf(sx - FC_SW / 2.0f) < 40 && sy > GROUND - 100 && sy < GROUND + 8) {
        /* stroked */
        m->happy = 2.6f;
        m->dizzy = 0;
        for (int i = 0, made = 0; i < HEARTS && made < 4; i++) {
            if (m->heart[i].life <= 0) {
                m->heart[i] = (heart_t){ sx + (fc_randf(&m->seed) - 0.5f) * 44,
                                         GROUND - 70 - fc_randf(&m->seed) * 16,
                                         18 + fc_randf(&m->seed) * 14, 1.6f };
                made++;
            }
        }
        return true;
    }
    /* beside it: it looks there */
    m->look = 2.2f;
    m->look_x = LV_CLAMP(-1.0f, (sx - FC_SW / 2.0f) / 50.0f, 1.0f);
    m->look_y = LV_CLAMP(-1.0f, (sy - (GROUND - 62)) / 50.0f, 1.0f);
    return true;
}

static void destroy(void *ctx)
{
    mascota_t *m = ctx;
    if (!m) {
        return;
    }
    fc_deinit(&m->fc);
    free(m->base);
    free(m->work);
    lv_free(m);
}

static bool mascota_get(aos_watchface_t *face)
{
    face->id       = FACE_ID;
    face->name     = N_("Mascota");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 50;
    return true;
}

AOS_FACE_ENTRY(mascota_get);
