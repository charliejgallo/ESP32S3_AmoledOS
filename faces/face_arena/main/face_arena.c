/*
 * AmoledOS - Sand: an hourglass with no glass.
 *
 * Every minute a handful of sand falls in from wherever "up" is; every ten
 * minutes the sand changes colour, so the bands in the heap read the hour
 * like the rings of a tree. The time is drawn in the middle and the sand
 * piles up on its digits and pours around them; tilt the watch and the heap
 * slides. On the hour the sand drains away and the hour starts empty.
 *
 * It is a cellular automaton on a 92x112 grid (cells of 4x4 screen pixels):
 * a grain falls along gravity, quantised to eight directions, or slides to
 * one of the two cells beside that. Only the cells that changed are drawn,
 * and when no grain moved the face sleeps until gravity turns.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GW          (FC_SW / 2)     /* 92: a cell is 2x2 of the half-res scene */
#define GH          (FC_SH / 2)     /* 112 */
#define PER_MINUTE  52
#define BASE_GRAINS 30
#define SOLID       0xFF
#define COLOURS     6

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    bool       aod;
    bool       asleep;

    uint8_t   *cell;        /* 0 empty, SOLID a digit, else a colour 1..COLOURS */
    uint8_t   *stamp;       /* the pass a grain last moved in */
    uint8_t    pass;
    uint16_t   pal[COLOURS + 1][4];   /* each colour in four grains of shade */
    uint8_t   *grain;       /* per cell: which of the four shades */

    int        grains;      /* how many there are */
    int        owed;        /* to drop in, a few per frame */
    int        draining;    /* to take out, on the hour */
    int        minute_colour;
    int        dir;         /* 0..7, the quantised gravity; -1 flat */
    fc_grav_t  grav;
    uint32_t   seed;

    int        dx0, dy0, dx1, dy1;    /* dirty cells this frame */
    char       last_hhmm[8];
    char       last_date[24];
    int        last_min, last_hour;
} arena_t;

/* the eight directions, clockwise from "down" */
static const int8_t DIRS[8][2] = {
    { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
};

/* -------------------------------------------------------------------------- */

static void build_palette(arena_t *a, bool aod)
{
    /* sand, terracotta, rose, ochre, slate, cream: one per ten minutes */
    static const uint32_t base[COLOURS] = {
        0xE8C27A, 0xD9774A, 0xE59AA5, 0xC99A2E, 0x8FA3B8, 0xF2E6C9,
    };
    for (int c = 0; c < COLOURS; c++) {
        uint16_t col = fc_hex(base[c]);
        for (int s = 0; s < 4; s++) {
            uint16_t v = fc_scale(col, 175 + s * 26);
            a->pal[c + 1][s] = aod ? fc_scale(v, 70) : v;
        }
    }
}

static inline void mark(arena_t *a, int x, int y)
{
    if (x < a->dx0) a->dx0 = x;
    if (x > a->dx1) a->dx1 = x;
    if (y < a->dy0) a->dy0 = y;
    if (y > a->dy1) a->dy1 = y;
}

static void draw_cell(arena_t *a, int x, int y)
{
    uint8_t v = a->cell[y * GW + x];
    uint16_t c = (v == 0 || v == SOLID) ? 0 : a->pal[v][a->grain[y * GW + x] & 3];
    uint16_t *p = a->fc.small + (2 * y) * FC_SW + 2 * x;
    p[0] = p[1] = c;
    p[FC_SW] = p[FC_SW + 1] = c;
}

static void present_dirty(arena_t *a)
{
    if (a->dx1 < 0) {
        return;
    }
    for (int y = a->dy0; y <= a->dy1; y++) {
        for (int x = a->dx0; x <= a->dx1; x++) {
            draw_cell(a, x, y);
        }
    }
    fc_present(&a->fc, 2 * a->dx0, 2 * a->dy0, 2 * a->dx1 + 2, 2 * a->dy1 + 2);
    a->dx0 = GW; a->dy0 = GH; a->dx1 = a->dy1 = -1;
}

/* The digits are solid: a cell is solid where the time's glyph (not its
 * halo, the mask's upper half) covers most of it. A grain caught inside a
 * digit that just changed shape goes back to the pile owed. */
static void solid_from_mask(arena_t *a)
{
    const fc_mask_t *m = &a->fc.mask[0];
    for (int i = 0; i < GW * GH; i++) {
        if (a->cell[i] == SOLID) {
            a->cell[i] = 0;
            mark(a, i % GW, i / GW);
        }
    }
    if (!m->a) {
        return;
    }
    for (int cy = 0; cy < GH; cy++) {
        for (int cx = 0; cx < GW; cx++) {
            int sx = cx * 4 - m->x, sy = cy * 4 - m->y;
            if (sx + 4 <= 0 || sy + 4 <= 0 || sx >= m->w || sy >= m->h) {
                continue;
            }
            int on = 0;
            for (int j = 1; j < 4; j += 2) {
                for (int i = 1; i < 4; i += 2) {
                    int xx = sx + i, yy = sy + j;
                    if (xx >= 0 && yy >= 0 && xx < m->w && yy < m->h &&
                        m->a[yy * m->w + xx] >= 190) {
                        on++;
                    }
                }
            }
            if (on >= 2) {
                uint8_t *c = &a->cell[cy * GW + cx];
                if (*c && *c != SOLID) {
                    a->grains--;
                    a->owed++;
                }
                *c = SOLID;
                mark(a, cx, cy);
            }
        }
    }
}

/* Drops a grain in at the edge gravity points away from. */
static bool drop_grain(arena_t *a)
{
    int d = a->dir < 0 ? 0 : a->dir;
    int gx = DIRS[d][0], gy = DIRS[d][1];
    for (int tries = 0; tries < 12; tries++) {
        int x, y;
        /* a point on the "top" edge, spread around its middle */
        float t = (fc_randf(&a->seed) + fc_randf(&a->seed)) * 0.5f;
        if (gy != 0 && (gx == 0 || fc_rand(&a->seed) & 1)) {
            y = gy > 0 ? 0 : GH - 1;
            x = (int)(t * GW);
        } else {
            x = gx > 0 ? 0 : GW - 1;
            y = (int)(t * GH);
        }
        if (x < 0 || x >= GW || y < 0 || y >= GH) continue;
        uint8_t *c = &a->cell[y * GW + x];
        if (*c == 0) {
            *c = (uint8_t)(1 + a->minute_colour % COLOURS);
            a->grain[y * GW + x] = (uint8_t)(fc_rand(&a->seed) & 3);
            a->grains++;
            mark(a, x, y);
            return true;
        }
    }
    return false;
}

/* Takes a grain out at the edge gravity points to, on the hour. */
static bool take_grain(arena_t *a)
{
    int d = a->dir < 0 ? 0 : a->dir;
    int gx = DIRS[d][0], gy = DIRS[d][1];
    /* the cells are scanned from the "bottom" edge inwards */
    for (int depth = 0; depth < LV_MAX(GW, GH); depth++) {
        int start = (int)(fc_rand(&a->seed) % (gy != 0 ? GW : GH));
        int span = gy != 0 ? GW : GH;
        for (int k = 0; k < span; k++) {
            int i = (start + k) % span;
            int x = gy != 0 ? i : (gx > 0 ? GW - 1 - depth : depth);
            int y = gy != 0 ? (gy > 0 ? GH - 1 - depth : depth) : i;
            if (x < 0 || x >= GW || y < 0 || y >= GH) continue;
            uint8_t *c = &a->cell[y * GW + x];
            if (*c && *c != SOLID) {
                *c = 0;
                a->grains--;
                mark(a, x, y);
                return true;
            }
        }
    }
    return false;
}

/* One step of the automaton. Returns how many grains moved. */
static int step(arena_t *a)
{
    if (a->dir < 0) {
        return 0;
    }
    int gx = DIRS[a->dir][0], gy = DIRS[a->dir][1];
    int lx = DIRS[(a->dir + 1) & 7][0], ly = DIRS[(a->dir + 1) & 7][1];
    int rx = DIRS[(a->dir + 7) & 7][0], ry = DIRS[(a->dir + 7) & 7][1];
    a->pass++;
    if (a->pass == 0) {
        a->pass = 1;
        memset(a->stamp, 0, GW * GH);
    }
    int moved = 0;
    /* from the "bottom" up, so a column falls in one pass */
    int y0 = gy > 0 ? GH - 1 : 0, y1 = gy > 0 ? -1 : GH, ys = gy > 0 ? -1 : 1;
    int x0 = gx > 0 ? GW - 1 : 0, x1 = gx > 0 ? -1 : GW, xs = gx > 0 ? -1 : 1;
    bool flip = fc_rand(&a->seed) & 1;
    for (int y = y0; y != y1; y += ys) {
        for (int x = x0; x != x1; x += xs) {
            int i = y * GW + x;
            uint8_t v = a->cell[i];
            if (v == 0 || v == SOLID || a->stamp[i] == a->pass) {
                continue;
            }
            int cand[3][2] = { { gx, gy }, { lx, ly }, { rx, ry } };
            if (flip ^ (bool)((x + y) & 1)) {
                cand[1][0] = rx; cand[1][1] = ry;
                cand[2][0] = lx; cand[2][1] = ly;
            }
            for (int k = 0; k < 3; k++) {
                int nx = x + cand[k][0], ny = y + cand[k][1];
                if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) {
                    continue;
                }
                int j = ny * GW + nx;
                if (a->cell[j] == 0) {
                    a->cell[j] = v;
                    a->grain[j] = a->grain[i];
                    a->stamp[j] = a->pass;
                    a->cell[i] = 0;
                    mark(a, x, y);
                    mark(a, nx, ny);
                    moved++;
                    break;
                }
            }
        }
    }
    return moved;
}

static int quantise(arena_t *a)
{
    float px = a->grav.x, py = a->grav.y;
    if (px * px + py * py < 0.30f * 0.30f) {
        return -1;          /* flat: the sand stays where it is */
    }
    /* DIRS[0] is down (0, 1), clockwise: angle from +y towards -x */
    float ang = atan2f(-px, py);
    int d = (int)floorf(ang / (FC_PI / 4) + 0.5f);
    return (d % 8 + 8) % 8;
}

static void frame(void *ctx, uint32_t dt_ms)
{
    (void)dt_ms;
    arena_t *a = ctx;
    if (!a || a->aod) {
        return;
    }
    fc_grav_update(&a->grav, 0.3f);
    int d = quantise(a);
    if (d < 0) {
        /* flat on the table there is no "down": the sand keeps falling the
         * way it last did, or the minute's handful would stay stuck along
         * the top edge (the first afternoon on the board) */
        d = a->dir;
    }
    if (d != a->dir) {
        a->dir = d;
        a->asleep = false;
    }
    if (a->asleep && !a->owed && !a->draining) {
        return;
    }
    for (int k = 0; k < 4 && a->owed > 0; k++) {
        if (drop_grain(a)) {
            a->owed--;
        } else {
            break;
        }
    }
    for (int k = 0; k < 40 && a->draining > 0; k++) {
        if (take_grain(a)) {
            a->draining--;
        } else {
            a->draining = 0;
        }
    }
    int moved = step(a);
    moved += step(a);
    present_dirty(a);
    a->asleep = moved == 0 && !a->owed && !a->draining;
}

/* -------------------------------------------------------------------------- */

static void text_update(arena_t *a, const struct tm *now, bool force)
{
    char hhmm[8], date[24];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    snprintf(date, sizeof(date), "%s %d %s", aos_day_name(now->tm_wday), now->tm_mday,
             aos_month_name(now->tm_mon));
    if (force || strcmp(hhmm, a->last_hhmm) != 0) {
        fc_mask_t old = a->fc.mask[0];
        a->fc.mask[0].a = NULL;
        snprintf(a->last_hhmm, sizeof(a->last_hhmm), "%s", hhmm);
        fc_mask_text(&a->fc.mask[0], a->root, hhmm, aos_font_huge, 2.2f,
                     FC_W / 2, 196, a->aod ? fc_hex(0x8A8A8A) : fc_hex(0xFFFFFF), true);
        solid_from_mask(a);
        present_dirty(a);
        fc_present_mask(&a->fc, &old);
        free(old.a);
        fc_present_mask(&a->fc, &a->fc.mask[0]);
        a->asleep = false;
    }
    if (force || strcmp(date, a->last_date) != 0) {
        fc_mask_t old = a->fc.mask[1];
        a->fc.mask[1].a = NULL;
        snprintf(a->last_date, sizeof(a->last_date), "%s", date);
        fc_mask_text(&a->fc.mask[1], a->root, date, aos_font_body, 1.0f,
                     FC_W / 2, 40, a->aod ? fc_hex(0x5A5A5A) : fc_hex(0xF2E6C9), true);
        fc_present_mask(&a->fc, &old);
        free(old.a);
        fc_present_mask(&a->fc, &a->fc.mask[1]);
    }
}

/* What the minute and the hour owe the heap. */
static void clock_update(arena_t *a, const struct tm *now)
{
    if (a->last_min < 0) {
        a->last_min = now->tm_min;
        a->last_hour = now->tm_hour;
        return;
    }
    if (now->tm_hour != a->last_hour) {
        a->draining = a->grains + a->owed;          /* the hour drains away */
        a->owed = BASE_GRAINS;
        a->minute_colour = 0;
        a->last_hour = now->tm_hour;
        a->last_min = now->tm_min;
        a->asleep = false;
    } else if (now->tm_min != a->last_min) {
        int n = now->tm_min - a->last_min;
        a->minute_colour = now->tm_min / 10;
        a->owed += (n > 0 ? n : 1) * PER_MINUTE;
        a->last_min = now->tm_min;
        a->asleep = false;
    }
}

static void *create(lv_obj_t *root)
{
    arena_t *a = lv_malloc_zeroed(sizeof(arena_t));
    if (!a) {
        return NULL;
    }
    a->cell = calloc(GW * GH, 1);
    a->stamp = calloc(GW * GH, 1);
    a->grain = calloc(GW * GH, 1);
    if (!a->cell || !a->stamp || !a->grain || !fc_init(&a->fc, root)) {
        free(a->cell);
        free(a->stamp);
        free(a->grain);
        lv_free(a);
        return NULL;
    }
    a->root = root;
    a->seed = (uint32_t)aos_hal_uptime_ms() * 2246822519u | 1u;
    a->dx0 = GW; a->dy0 = GH; a->dx1 = a->dy1 = -1;
    a->last_min = -1;
    build_palette(a, false);
    fc_grav_update(&a->grav, 1.0f);
    a->dir = quantise(a);
    if (a->dir < 0) {
        a->dir = 0;
    }

    struct tm now;
    aos_hal_time_now(&now);
    text_update(a, &now, true);
    clock_update(a, &now);

    /* the minutes gone already, laid down at once: bottom-up, settled, each
     * ten minutes in its colour */
    int total = BASE_GRAINS + now.tm_min * PER_MINUTE;
    for (int m = 0; m <= now.tm_min / 10; m++) {
        a->minute_colour = m;
        int upto = (m == now.tm_min / 10) ? total : BASE_GRAINS + (m + 1) * 10 * PER_MINUTE;
        while (a->grains < upto && a->grains < total) {
            if (!drop_grain(a)) {
                break;
            }
            /* let them fall: one step every row's worth of grains */
            if ((a->grains & 31) == 0) {
                for (int k = 0; k < 2; k++) step(a);
            }
        }
        for (int k = 0; k < GH * 2; k++) {
            if (!step(a)) break;
        }
    }
    a->minute_colour = now.tm_min / 10;
    a->dx0 = 0; a->dy0 = 0; a->dx1 = GW - 1; a->dy1 = GH - 1;
    present_dirty(a);
    return a;
}

static void refresh(void *ctx, const struct tm *now)
{
    arena_t *a = ctx;
    if (!a) {
        return;
    }
    clock_update(a, now);
    text_update(a, now, false);
    if (a->aod && (a->owed || a->draining)) {
        /* dimmed there are no frames: settle the minute's sand here, once */
        while (a->owed > 0 && drop_grain(a)) a->owed--;
        while (a->draining > 0 && take_grain(a)) a->draining--;
        a->owed = a->draining = 0;
        for (int k = 0; k < GH * 2 && step(a); k++) {
        }
        present_dirty(a);
    }
}

static void set_aod(void *ctx, bool aod)
{
    arena_t *a = ctx;
    if (!a || a->aod == aod) {
        return;
    }
    a->aod = aod;
    build_palette(a, aod);
    struct tm now;
    aos_hal_time_now(&now);
    text_update(a, &now, true);
    a->dx0 = 0; a->dy0 = 0; a->dx1 = GW - 1; a->dy1 = GH - 1;
    present_dirty(a);
    a->asleep = false;
}

static void destroy(void *ctx)
{
    arena_t *a = ctx;
    if (!a) {
        return;
    }
    fc_deinit(&a->fc);
    free(a->cell);
    free(a->stamp);
    free(a->grain);
    lv_free(a);
}

static bool arena_get(aos_watchface_t *face)
{
    face->id       = "face.arena";
    face->name     = N_("Arena");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->frame    = frame;
    face->frame_ms = 33;
    return true;
}

AOS_FACE_ENTRY(arena_get);
