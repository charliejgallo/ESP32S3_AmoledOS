/*
 * Claudito - a virtual pet for AmoledOS
 *
 * A tamagotchi with the Claude Code critter inside. It is all pixel art of our
 * own: the art lives on a 92x112 grid and LVGL stretches it x4 with
 * nearest-neighbour, so what you see on the 368x448 screen is perfectly square
 * 4x4 blocks, without a single smoothed line.
 *
 * Three layers, three canvases:
 *
 *   HUD        92x17   name, day and the four status bars
 *   stage      92x78   background, character, objects and particles
 *   bar        92x17   the six action buttons
 *
 * Each scene's background is drawn once into a separate buffer and copied with
 * memcpy on every frame: redrawing the parquet plank by plank fourteen times a
 * second adds nothing.
 *
 * The same source builds two ways:
 *
 *   .so for the board       cd apps/claudito && idf.py -G 'Unix Makefiles' set-target esp32s3 && idf.py so
 *   inside the simulator    it builds itself (AOS_SIM_BUILTIN)
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "cl_pixel.h"
#include "cl_pet.h"
#include "cl_scene.h"
#include "cl_sprites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS        70          /* ~14 frames per second, nicely retro */
#define TRAY_ITEMS      4
#define ACT_COUNT       6
#define PARTS_MAX       24

/* How long the name has to be held down to reset the day counter: ~2 s, long
 * enough not to happen by accident and short enough not to leave you wondering
 * whether it is doing anything. The little bar growing under the name is what
 * says it is. */
#define HOLD_FRAMES     (2000 / FRAME_MS)

/* The stats are stored in hundredths so the wear is integral: with integers
 * there is no need to carry floating-point accumulators around or to depend on
 * libm in an app loaded with dlopen. */
#define STAT_MAX        10000
#define STAT_PCT(v)     ((v) / 100)

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

typedef enum {
    MODE_IDLE = 0,
    MODE_FOOD_TRAY,
    MODE_TOY_TRAY,
    MODE_EATING,
    MODE_PLAYING,
    MODE_WASH,
    MODE_TICKLE,
    MODE_SLEEP,
} cl_mode_t;

typedef enum {
    ACT_FEED = 0,
    ACT_PLAY,
    ACT_WASH,
    ACT_TICKLE,
    ACT_SLEEP,
    ACT_SCENE,
} action_t;

typedef enum {
    TOY_BALL = 0,
    TOY_BALLOON,
    TOY_BUBBLES,
    TOY_BLOCKS,
} toy_t;

typedef enum {
    P_HEART = 0,
    P_SPARK,
    P_NOTE,
    P_BUBBLE,
    P_CRUMB,
    P_SUDS,
    P_ZZZ,
    P_DROP,
} pkind_t;

/* The particles move in sixteenths of a pixel: at fourteen frames a second, a
 * heart rising a whole pixel per frame flies off. */
typedef struct {
    uint8_t kind;
    uint8_t life;
    uint8_t life0;
    int16_t x, y;
    int16_t vx, vy;
} part_t;

typedef struct {
    lv_obj_t   *canvas_hud;
    lv_obj_t   *canvas_stage;
    lv_obj_t   *canvas_bar;
    lv_obj_t   *stage_touch;
    lv_obj_t   *name_touch;
    lv_obj_t   *tray_btn[TRAY_ITEMS];
    lv_obj_t   *act_btn[ACT_COUNT];
    lv_timer_t *timer;

    uint16_t *mem_hud, *mem_stage, *mem_bar, *mem_bg;
    /* The same three, already upscaled x4: it is what the canvas sees. */
    uint16_t *big_hud, *big_stage, *big_bar;
    cl_buf_t  hud, stage, bar, bg;

    cl_pet_t       pet;
    cl_scene_id_t  scene;
    cl_mode_t         mode;

    int frame;
    int anim;               /* frames within the current mode */
    int hop, hop_v;         /* jump: the critter's floor goes up and down */
    int walk_target;        /* -1 = still */
    int idle_timer;
    int blink;
    int flip;               /* facing left */

    /* stats in hundredths */
    int32_t hunger, happy, clean, energy;
    int32_t last_min;       /* clock minute of the last save */
    int32_t born_day;
    int     acc_ms;
    int     sec;            /* seconds lived, for the slow rhythms */
    int     save_timer;

    /* food travelling to the mouth */
    int food_kind, fly_t, fly_x0, fly_y0;

    /* toys */
    toy_t toy;
    int   ball_x, ball_y, ball_vx, ball_vy;
    int   blocks, block_t, block_fall;
    int   balloon_t;

    /* sponge */
    int  sponge_x, sponge_y, sponge_px, sponge_py;
    bool sponge_down;
    int  scrub;

    /* speech bubble */
    char msg1[20];
    char msg2[20];
    int  msg_t;

    /* sustained press on the name */
    int  hold_t;

    part_t parts[PARTS_MAX];
    bool   hud_dirty;
    bool   bar_dirty;
    bool   want_exit;
} app_t;

static void expand4(const uint16_t *src, int w, int h, uint16_t *dst);

/* --------------------------------------------------------------------------
 * Catalogues
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *const *rows;
    int  nrows;
    const char *name;
    int  hunger, happy, clean;      /* what it adds or subtracts, in hundredths */
} food_t;

static const food_t FOODS[TRAY_ITEMS] = {
        /* The food names do NOT carry N_: today they are drawn nowhere
     * -unlike the toys, which appear in the bubble- and an N_ without its
     * matching _() leaves an orphan key in the catalogue, which translates
     * nothing and never fails. If they are ever shown, they get marked.
     *
     * What IS drawn goes through cl_text(), the 5x7 font of our own: ASCII
     * 32-126, no accents, and it silently discards anything outside that. And
     * the bubbles are char[20]: 19 characters per line. */
    { CL_SPRITE(cl_spr_apple),  "MANZANA", 2000,  400,    0 },
    { CL_SPRITE(cl_spr_pizza),  "PIZZA",   3000,  800, -400 },
    { CL_SPRITE(cl_spr_cookie), "GALLETA", 1200, 1000, -200 },
    { CL_SPRITE(cl_spr_cake),   "TORTA",   1800, 1600, -700 },
};

typedef struct {
    const char *const *rows;
    int  nrows;
    const char *name;
} toy_info_t;

static const toy_info_t TOYS[TRAY_ITEMS] = {
    { CL_SPRITE(cl_spr_ball),    N_("PELOTA")   },
    { CL_SPRITE(cl_spr_balloon), N_("GLOBO")    },
    { CL_SPRITE(cl_spr_bubbles), N_("BURBUJAS") },
    { CL_SPRITE(cl_spr_dice),    N_("CUBOS")    },
};

/* --------------------------------------------------------------------------
 * Random numbers of our own: rand() would force the firmware to export it and
 * this way the background comes out the same every time when captures have to
 * be compared.
 * -------------------------------------------------------------------------- */
static uint32_t s_rng = 0x1234567;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static int rnd_range(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Stats and persistence
 * -------------------------------------------------------------------------- */

/* Minutes since an arbitrary epoch. There is no time() in the loader's symbol
 * table, so it is built from the struct tm: tm_yday never reaches 366, so
 * year*366 + yday always grows. */
static int32_t wall_minutes(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    aos_hal_time_now(&t);
    return (((int32_t)t.tm_year * 366 + t.tm_yday) * 24 + t.tm_hour) * 60 + t.tm_min;
}

static int32_t wall_days(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    aos_hal_time_now(&t);
    return (int32_t)t.tm_year * 366 + t.tm_yday;
}

/* Modes that survive closing the app.
 *
 * Leaving does not cancel what it was doing: if you left it sleeping, it goes
 * on sleeping (and goes on recovering energy with the app closed). Eating is a
 * two-second animation and the trays are a half-chosen menu: those two are not
 * worth restoring. */
static cl_mode_t mode_saved(cl_mode_t mode)
{
    switch (mode) {
    case MODE_SLEEP:
    case MODE_PLAYING:
    case MODE_WASH:
    case MODE_TICKLE:
        return mode;
    default:
        return MODE_IDLE;
    }
}

static void stats_save(app_t *a)
{
    aos_hal_pref_set_i32("pet_hunger", a->hunger);
    aos_hal_pref_set_i32("pet_happy",  a->happy);
    aos_hal_pref_set_i32("pet_clean",  a->clean);
    aos_hal_pref_set_i32("pet_energy", a->energy);
    aos_hal_pref_set_i32("pet_min",    a->last_min);
    aos_hal_pref_set_i32("pet_born",   a->born_day);
    aos_hal_pref_set_i32("pet_scene",  (int32_t)a->scene);
    aos_hal_pref_set_i32("pet_mode",   (int32_t)mode_saved(a->mode));
    aos_hal_pref_set_i32("pet_toy",    (int32_t)a->toy);
}

static void stats_load(app_t *a)
{
    int32_t v;

    a->hunger = a->happy = a->clean = a->energy = 8000;

    if (aos_hal_pref_get_i32("pet_hunger", &v)) a->hunger = clampi(v, 0, STAT_MAX);
    if (aos_hal_pref_get_i32("pet_happy",  &v)) a->happy  = clampi(v, 0, STAT_MAX);
    if (aos_hal_pref_get_i32("pet_clean",  &v)) a->clean  = clampi(v, 0, STAT_MAX);
    if (aos_hal_pref_get_i32("pet_energy", &v)) a->energy = clampi(v, 0, STAT_MAX);
    if (aos_hal_pref_get_i32("pet_scene",  &v)) a->scene  = (v == CL_SCENE_PARK) ? CL_SCENE_PARK
                                                                                 : CL_SCENE_HOME;

    a->born_day = wall_days();
    if (aos_hal_pref_get_i32("pet_born", &v) && v > 0 && v <= a->born_day) {
        a->born_day = v;
    }

    /* How it was left last time. It is restored later, once the buttons exist,
     * but it is needed here because it changes how time runs. */
    if (aos_hal_pref_get_i32("pet_mode", &v)) {
        a->mode = mode_saved((cl_mode_t)v);
    }
    if (aos_hal_pref_get_i32("pet_toy", &v) && v >= 0 && v < TRAY_ITEMS) {
        a->toy = (toy_t)v;
    }

    /* What happened while the app was closed. It is clamped to twelve hours:
     * coming back after a week must not find a ruined critter, and if the
     * clock was never set the sum can come out as anything. */
    a->last_min = wall_minutes();
    if (aos_hal_pref_get_i32("pet_min", &v) && v > 0) {
        int32_t elapsed = a->last_min - v;
        if (elapsed > 0) {
            if (elapsed > 12 * 60) {
                elapsed = 12 * 60;
            }
            if (a->mode == MODE_SLEEP) {
                /* it slept the whole time, at the same rate as stats_second() */
                a->energy = clampi(a->energy + elapsed * 1500, 0, STAT_MAX);
                a->hunger = clampi(a->hunger - elapsed * 20,   0, STAT_MAX);
            } else {
                a->hunger = clampi(a->hunger - elapsed * 60, 0, STAT_MAX);
                a->happy  = clampi(a->happy  - elapsed * 60, 0, STAT_MAX);
                a->clean  = clampi(a->clean  - elapsed * 30, 0, STAT_MAX);
                a->energy = clampi(a->energy - elapsed * 30, 0, STAT_MAX);
            }
        }
    }
}

/* One second's wear. The different rhythms come out of the seconds counter
 * instead of fractional accumulators. */
static void stats_second(app_t *a)
{
    a->sec++;

    if (a->mode == MODE_SLEEP) {
        a->energy = clampi(a->energy + 25, 0, STAT_MAX);
        if ((a->sec % 3) == 0) {
            a->hunger = clampi(a->hunger - 1, 0, STAT_MAX);
        }
        return;
    }

    a->hunger = clampi(a->hunger - 1, 0, STAT_MAX);
    a->happy  = clampi(a->happy  - 1, 0, STAT_MAX);
    if ((a->sec & 1) == 0) {
        a->clean  = clampi(a->clean  - 1, 0, STAT_MAX);
        a->energy = clampi(a->energy - 1, 0, STAT_MAX);
    }
    /* hungry or covered in dirt, the mood falls faster */
    if (a->hunger < 2500 || a->clean < 2500) {
        a->happy = clampi(a->happy - 1, 0, STAT_MAX);
    }

    a->hud_dirty = true;
}

/* --------------------------------------------------------------------------
 * Particles
 * -------------------------------------------------------------------------- */

static void spawn(app_t *a, pkind_t kind, int x, int y, int vx, int vy, int life)
{
    for (int i = 0; i < PARTS_MAX; i++) {
        part_t *p = &a->parts[i];
        if (p->life) {
            continue;
        }
        p->kind  = (uint8_t)kind;
        p->life  = (uint8_t)life;
        p->life0 = (uint8_t)life;
        p->x     = (int16_t)(x * 16);
        p->y     = (int16_t)(y * 16);
        p->vx    = (int16_t)vx;
        p->vy    = (int16_t)vy;
        return;
    }
}

static void parts_clear(app_t *a)
{
    memset(a->parts, 0, sizeof(a->parts));
}

static void parts_step(app_t *a)
{
    for (int i = 0; i < PARTS_MAX; i++) {
        part_t *p = &a->parts[i];
        if (!p->life) {
            continue;
        }
        p->x += p->vx;
        p->y += p->vy;

        switch (p->kind) {
        case P_CRUMB:
        case P_DROP:
            p->vy += 3;                     /* gravity */
            break;
        case P_HEART:
        case P_ZZZ:
        case P_BUBBLE:
            /* they rise evenly, zigzagging a little */
            p->x += (int16_t)(((a->frame + i) % 8) < 4 ? 1 : -1);
            break;
        default:
            break;
        }
        p->life--;
    }
}

static void draw_heart(cl_buf_t *b, int x, int y, uint16_t c, int size)
{
    if (size <= 1) {
        cl_px(b, x + 1, y, c);
        cl_px(b, x, y + 1, c);
        cl_px(b, x + 2, y + 1, c);
        return;
    }
    cl_px(b, x + 1, y, c);
    cl_px(b, x + 3, y, c);
    cl_rect(b, x, y + 1, 5, 2, c);
    cl_rect(b, x + 1, y + 3, 3, 1, c);
    cl_px(b, x + 2, y + 4, c);
}

static void draw_spark(cl_buf_t *b, int x, int y, uint16_t c, int size)
{
    cl_px(b, x, y, c);
    if (size > 0) {
        cl_px(b, x - 1, y, c);
        cl_px(b, x + 1, y, c);
        cl_px(b, x, y - 1, c);
        cl_px(b, x, y + 1, c);
    }
    if (size > 1) {
        cl_px(b, x - 2, y, c);
        cl_px(b, x + 2, y, c);
        cl_px(b, x, y - 2, c);
        cl_px(b, x, y + 2, c);
    }
}

/* A circle of radius 2 or 3 taken from the equation comes out diamond-shaped:
 * at this scale the bubbles are drawn by hand. */
static void draw_bubble(cl_buf_t *b, int x, int y, int big)
{
    const uint16_t skin = cl_rgb(0xBFEFFA);

    if (big) {
        cl_hline(b, x - 1, y - 3, 3, skin);
        cl_hline(b, x - 1, y + 3, 3, skin);
        cl_px(b, x - 2, y - 2, skin);   cl_px(b, x + 2, y - 2, skin);
        cl_px(b, x - 2, y + 2, skin);   cl_px(b, x + 2, y + 2, skin);
        cl_vline(b, x - 3, y - 1, 3, skin);
        cl_vline(b, x + 3, y - 1, 3, skin);
        cl_px(b, x - 1, y - 1, cl_rgb(0xFFFFFF));
        return;
    }
    cl_hline(b, x - 1, y - 2, 3, skin);
    cl_hline(b, x - 1, y + 2, 3, skin);
    cl_vline(b, x - 2, y - 1, 3, skin);
    cl_vline(b, x + 2, y - 1, 3, skin);
    cl_px(b, x - 1, y - 1, cl_rgb(0xFFFFFF));
}

static void parts_draw(app_t *a)
{
    for (int i = 0; i < PARTS_MAX; i++) {
        const part_t *p = &a->parts[i];
        if (!p->life) {
            continue;
        }
        int x = p->x / 16;
        int y = p->y / 16;
        int fade = p->life * 3 / (p->life0 ? p->life0 : 1);   /* 0..2 */

        switch (p->kind) {
        case P_HEART:
            draw_heart(&a->stage, x, y, cl_rgb(0xFF4D6D), fade);
            break;

        case P_SPARK:
            draw_spark(&a->stage, x, y, cl_rgb(fade > 1 ? 0xFFFFFF : 0xFFE066), fade);
            break;

        case P_NOTE:
            cl_rect(&a->stage, x, y + 3, 3, 2, cl_rgb(0xFFFFFF));
            cl_vline(&a->stage, x + 2, y, 4, cl_rgb(0xFFFFFF));
            cl_hline(&a->stage, x + 2, y, 3, cl_rgb(0xFFFFFF));
            break;

        case P_BUBBLE:
            draw_bubble(&a->stage, x, y, i & 1);
            break;

        case P_CRUMB:
            cl_rect(&a->stage, x, y, 2, 2, cl_rgb(0x8A5A32));
            break;

        case P_SUDS:
            cl_disc(&a->stage, x, y, fade, cl_rgb(0xFFFFFF));
            cl_px(&a->stage, x - 1, y - 1, cl_rgb(0xDFF3FA));
            break;

        case P_DROP:
            cl_px(&a->stage, x, y, cl_rgb(0x8FD4F7));
            cl_rect(&a->stage, x, y + 1, 2, 2, cl_rgb(0x67B8EA));
            break;

        case P_ZZZ:
            cl_text(&a->stage, x, y, "Z", cl_rgb(fade > 1 ? 0xFFFFFF : 0xA8BCD0));
            break;

        default:
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * Speech bubble
 * -------------------------------------------------------------------------- */

static void say(app_t *a, const char *l1, const char *l2, int frames)
{
    snprintf(a->msg1, sizeof(a->msg1), "%s", l1 ? l1 : "");
    snprintf(a->msg2, sizeof(a->msg2), "%s", l2 ? l2 : "");
    a->msg_t = frames;
}

static void bubble_draw(cl_buf_t *b, int cx, int bottom, const char *l1, const char *l2)
{
    int w1 = cl_text_w(l1);
    int w2 = l2[0] ? cl_text_w(l2) : 0;
    int tw = w1 > w2 ? w1 : w2;
    int w  = tw + 8;
    int h  = l2[0] ? 22 : 13;

    int x = cx - w / 2;
    if (x < 1)              x = 1;
    if (x + w > CL_ART_W - 1) x = CL_ART_W - 1 - w;
    int y = bottom - h - 3;

    cl_round(b, x, y, w, h, 2, cl_rgb(0xFFFFFF));
    /* border: the same rounding one pixel outside, in grey */
    cl_hline(b, x + 2, y - 1, w - 4, cl_rgb(0x3A3A3C));
    cl_hline(b, x + 2, y + h, w - 4, cl_rgb(0x3A3A3C));
    cl_vline(b, x - 1, y + 2, h - 4, cl_rgb(0x3A3A3C));
    cl_vline(b, x + w, y + 2, h - 4, cl_rgb(0x3A3A3C));

    /* tail pointing at the critter */
    cl_rect(b, cx - 2, y + h, 4, 1, cl_rgb(0xFFFFFF));
    cl_rect(b, cx - 1, y + h + 1, 3, 1, cl_rgb(0xFFFFFF));
    cl_px(b, cx, y + h + 2, cl_rgb(0xFFFFFF));
    cl_px(b, cx - 2, y + h + 1, cl_rgb(0x3A3A3C));
    cl_px(b, cx + 2, y + h + 1, cl_rgb(0x3A3A3C));

    cl_text(b, x + (w - w1) / 2, y + 3, l1, cl_rgb(0x2C2C2E));
    if (l2[0]) {
        cl_text(b, x + (w - w2) / 2, y + 12, l2, cl_rgb(0x2C2C2E));
    }
}

/* --------------------------------------------------------------------------
 * HUD
 * -------------------------------------------------------------------------- */

static void gauge(cl_buf_t *b, int x, int y, int value, uint32_t color)
{
    int pct = STAT_PCT(value);
    cl_rect(b, x, y, 15, 5, cl_rgb(0x1C1C1E));
    cl_rect(b, x + 1, y + 1, 13, 3, cl_rgb(0x3A3A3C));

    int fill = pct * 13 / 100;
    if (fill > 0) {
        uint32_t c = color;
        if (pct < 25) {
            c = 0xFF453A;
        } else if (pct < 50) {
            c = 0xFF9F0A;
        }
        cl_rect(b, x + 1, y + 1, fill, 3, cl_rgb(c));
        cl_hline(b, x + 1, y + 1, fill, cl_rgb(0xFFFFFF));
        cl_shade(b, x + 1, y + 1, fill, 1, 6);
    }
}

static void icon_food(cl_buf_t *b, int x, int y)
{
    cl_rect(b, x, y + 1, 5, 4, cl_rgb(0xE5484D));
    cl_px(b, x, y + 1, cl_rgb(0x3A3A3C));
    cl_px(b, x + 4, y + 1, cl_rgb(0x3A3A3C));
    cl_px(b, x + 2, y, cl_rgb(0x45C463));
    cl_px(b, x + 1, y + 2, cl_rgb(0xFF9F8A));
}

static void icon_heart(cl_buf_t *b, int x, int y)
{
    draw_heart(b, x, y, cl_rgb(0xFF4D6D), 2);
}

static void icon_drop(cl_buf_t *b, int x, int y)
{
    cl_px(b, x + 2, y, cl_rgb(0x67DCEA));
    cl_rect(b, x + 1, y + 1, 3, 1, cl_rgb(0x67DCEA));
    cl_rect(b, x, y + 2, 5, 2, cl_rgb(0x4A9DF5));
    cl_rect(b, x + 1, y + 4, 3, 1, cl_rgb(0x4A9DF5));
    cl_px(b, x + 1, y + 2, cl_rgb(0xFFFFFF));
}

static void icon_bolt(cl_buf_t *b, int x, int y)
{
    cl_rect(b, x + 2, y, 2, 2, cl_rgb(0xFFD60A));
    cl_rect(b, x + 1, y + 2, 3, 1, cl_rgb(0xFFD60A));
    cl_rect(b, x, y + 2, 2, 2, cl_rgb(0xFFD60A));
    cl_rect(b, x + 2, y + 3, 2, 2, cl_rgb(0xFFD60A));
}

static void hud_draw(app_t *a)
{
    cl_buf_t *b = &a->hud;

    cl_fill(b, cl_rgb(0x101014));
    cl_rect(b, 0, 0, CL_ART_W, 1, cl_rgb(0x24242A));
    cl_hline(b, 0, CL_HUD_H - 1, CL_ART_W, cl_rgb(0x2C2C2E));

    cl_text(b, 3, 2, "CLAUDITO", cl_rgb(0xD97757));

    /* charging bar while the name is held down */
    if (a->hold_t > 0) {
        int full = cl_text_w("CLAUDITO");
        int done = full * a->hold_t / HOLD_FRAMES;
        cl_rect(b, 3, 9, full, 2, cl_rgb(0x3A3A3C));
        cl_rect(b, 3, 9, done, 2, cl_rgb(0xFFD60A));
    }

    char right[16];
    snprintf(right, sizeof(right), _("DIA %d"), (int)(wall_days() - a->born_day + 1));
    cl_text(b, CL_ART_W - 3 - cl_text_w(right), 2, right, cl_rgb(0x8E8E93));

    icon_food (b, 1,  11);  gauge(b, 7,  11, a->hunger, 0x30D158);
    icon_heart(b, 24, 11);  gauge(b, 30, 11, a->happy,  0x30D158);
    icon_drop (b, 47, 11);  gauge(b, 53, 11, a->clean,  0x30D158);
    icon_bolt (b, 70, 11);  gauge(b, 76, 11, a->energy, 0x30D158);

    expand4(a->mem_hud, CL_ART_W, CL_HUD_H, a->big_hud);
    lv_obj_invalidate(a->canvas_hud);
}

/* --------------------------------------------------------------------------
 * Action bar
 * -------------------------------------------------------------------------- */

static const uint32_t ACT_COLOR[ACT_COUNT] = {
    0xE5484D,   /* eat      */
    0x30D158,   /* play     */
    0x4A9DF5,   /* clean    */
    0xFFD60A,   /* tickle   */
    0xB072F0,   /* sleep    */
    0xFF9F0A,   /* scene    */
};

static bool act_is_on(const app_t *a, int act)
{
    switch (act) {
    case ACT_FEED:   return a->mode == MODE_FOOD_TRAY || a->mode == MODE_EATING;
    case ACT_PLAY:   return a->mode == MODE_TOY_TRAY  || a->mode == MODE_PLAYING;
    case ACT_WASH:   return a->mode == MODE_WASH;
    case ACT_TICKLE: return a->mode == MODE_TICKLE;
    case ACT_SLEEP:  return a->mode == MODE_SLEEP;
    default:         return false;
    }
}

static void bar_draw(app_t *a)
{
    cl_buf_t *b = &a->bar;

    cl_fill(b, cl_rgb(0x101014));
    cl_hline(b, 0, 0, CL_ART_W, cl_rgb(0x2C2C2E));

    for (int i = 0; i < ACT_COUNT; i++) {
        int x = 1 + i * 15;
        bool on = act_is_on(a, i);

        if (on) {
            cl_round(b, x, 2, 14, 14, 2, cl_rgb(ACT_COLOR[i]));
            cl_shade(b, x, 2, 14, 14, -3);
        } else {
            cl_round(b, x, 2, 14, 14, 2, cl_rgb(0x1C1C1E));
        }
        cl_hline(b, x + 2, 2, 10, cl_rgb(on ? 0xFFFFFF : 0x3A3A3C));

        switch (i) {
        case ACT_FEED:   cl_blit(b, x + 2, 4, CL_SPRITE(cl_spr_apple),  false); break;
        case ACT_PLAY:   cl_blit(b, x + 2, 4, CL_SPRITE(cl_spr_ball),   false); break;
        case ACT_WASH:   cl_blit(b, x + 2, 4, CL_SPRITE(cl_spr_sponge), false); break;
        case ACT_TICKLE: cl_blit(b, x + 3, 4, CL_SPRITE(cl_spr_hand),   false); break;
        case ACT_SLEEP:  cl_blit(b, x + 3, 4, CL_SPRITE(cl_spr_moon),   false); break;
        default:
            if (a->scene == CL_SCENE_HOME) {
                cl_blit(b, x + 2, 4, CL_SPRITE(cl_spr_tree_icon), false);
            } else {
                cl_blit(b, x + 2, 4, CL_SPRITE(cl_spr_door), false);
            }
            break;
        }
    }

    expand4(a->mem_bar, CL_ART_W, CL_BAR_H, a->big_bar);
    lv_obj_invalidate(a->canvas_bar);
}

/* --------------------------------------------------------------------------
 * Item tray
 * -------------------------------------------------------------------------- */

#define TRAY_X0     6
#define TRAY_Y      14
#define TRAY_STEP   21
#define TRAY_SIZE   18

static void tray_draw(app_t *a)
{
    cl_buf_t *b = &a->stage;
    bool food = (a->mode == MODE_FOOD_TRAY);

    cl_round(b, 2, 2, 88, 34, 3, cl_rgb(0x1C1C1E));
    cl_shade(b, 2, 2, 88, 34, 0);
    cl_round(b, 3, 3, 86, 32, 3, cl_rgb(0x2C2C2E));

    cl_text_center(b, CL_ART_W / 2, 5,
                   food ? _("QUE COMEMOS?") : _("A QUE JUGAMOS?"),
                   cl_rgb(0xFFFFFF), cl_rgb(0x101014));

    for (int i = 0; i < TRAY_ITEMS; i++) {
        int x = TRAY_X0 + i * TRAY_STEP;
        cl_round(b, x, TRAY_Y, TRAY_SIZE, TRAY_SIZE, 2, cl_rgb(0x3A3A3C));
        cl_round(b, x + 1, TRAY_Y + 1, TRAY_SIZE - 2, TRAY_SIZE - 2, 2, cl_rgb(0x4A4A50));

        if (food) {
            cl_blit(b, x + 4, TRAY_Y + 4, FOODS[i].rows, FOODS[i].nrows, false);
        } else {
            /* the balloon and the die are taller or narrower than the cell */
            int ox = (i == TOY_BALLOON) ? 4 : (i == TOY_BLOCKS ? 5 : 4);
            int oy = (i == TOY_BALLOON) ? 2 : (i == TOY_BLOCKS ? 5 : 4);
            cl_blit(b, x + ox, TRAY_Y + oy, TOYS[i].rows, TOYS[i].nrows, false);
        }
    }
}

static void tray_buttons(app_t *a, bool visible)
{
    for (int i = 0; i < TRAY_ITEMS; i++) {
        if (visible) {
            lv_obj_remove_flag(a->tray_btn[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(a->tray_btn[i]);
        } else {
            lv_obj_add_flag(a->tray_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* --------------------------------------------------------------------------
 * Modes
 * -------------------------------------------------------------------------- */

static void mode_set(app_t *a, cl_mode_t mode)
{
    a->mode = mode;
    a->anim = 0;
    a->sponge_down = false;
    a->scrub = 0;
    tray_buttons(a, mode == MODE_FOOD_TRAY || mode == MODE_TOY_TRAY);
    a->bar_dirty = true;
}

/* Only the toy's state: shared by starting to play and coming back to the app
 * with a half-used toy. */
static void toy_reset(app_t *a, toy_t toy)
{
    a->toy = toy;

    switch (toy) {
    case TOY_BALL:
        a->ball_x  = 20 * 16;
        a->ball_y  = 20 * 16;
        a->ball_vx = 22;
        a->ball_vy = 0;
        break;
    case TOY_BLOCKS:
        a->blocks = 0;
        a->block_t = 0;
        a->block_fall = 0;
        break;
    case TOY_BALLOON:
        a->balloon_t = 0;
        break;
    default:
        break;
    }
}

static void toy_start(app_t *a, toy_t toy)
{
    mode_set(a, MODE_PLAYING);
    parts_clear(a);
    toy_reset(a, toy);

    char line[20];
    snprintf(line, sizeof(line), "%s!", _(TOYS[toy].name));
    say(a, line, "", 24);
    aos_hal_beep(1400, 25);
}

static void feed_start(app_t *a, int kind)
{
    if (a->hunger > 9400) {
        say(a, _("NO ENTRA"), _("MAS!"), 26);
        /* it turns its face the other way; it comes back by itself on the
           boredom routine's next decision */
        a->pet.face_dx = -3;
        aos_hal_beep(300, 90);
        mode_set(a, MODE_IDLE);
        return;
    }

    a->food_kind = kind;
    a->fly_x0 = TRAY_X0 + kind * TRAY_STEP + 4;
    a->fly_y0 = TRAY_Y + 4;
    a->fly_t  = 0;
    mode_set(a, MODE_EATING);
    aos_hal_beep(900, 30);
}

static void feed_finish(app_t *a)
{
    const food_t *f = &FOODS[a->food_kind];

    a->hunger = clampi(a->hunger + f->hunger, 0, STAT_MAX);
    a->happy  = clampi(a->happy  + f->happy,  0, STAT_MAX);
    a->clean  = clampi(a->clean  + f->clean,  0, STAT_MAX);
    a->hud_dirty = true;

    /* the hearts come out above the head: over the mouth they look like a pink
       moustache */
    int px, py, pw, ph;
    cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);
    for (int i = 0; i < 5; i++) {
        spawn(a, P_HEART, px + 4 + i * (pw - 8) / 4, py - 3,
              rnd_range(-6, 6), -12, rnd_range(14, 22));
    }
    say(a, _("RICO!"), "", 22);
    aos_hal_beep(1600, 40);

    a->hop_v = 9;
    mode_set(a, MODE_IDLE);
}

static void action_do(app_t *a, int act)
{
    switch (act) {
    case ACT_FEED:
        mode_set(a, a->mode == MODE_FOOD_TRAY ? MODE_IDLE : MODE_FOOD_TRAY);
        break;

    case ACT_PLAY:
        mode_set(a, a->mode == MODE_TOY_TRAY ? MODE_IDLE : MODE_TOY_TRAY);
        break;

    case ACT_WASH:
        if (a->mode == MODE_WASH) {
            mode_set(a, MODE_IDLE);
        } else {
            mode_set(a, MODE_WASH);
            a->sponge_x = CL_ART_W / 2;
            a->sponge_y = 30;
            say(a, _("FROTAME!"), "", 30);
        }
        break;

    case ACT_TICKLE:
        if (a->mode == MODE_TICKLE) {
            mode_set(a, MODE_IDLE);
        } else {
            mode_set(a, MODE_TICKLE);
            /* the hand starts beside the critter: left at 0,0 the first frame
               draws it in the screen's corner */
            a->sponge_x = a->pet.x + 20;
            a->sponge_y = CL_FLOOR_Y - 14;
            say(a, _("TOCAME LA"), _("PANZA!"), 30);
        }
        break;

    case ACT_SLEEP:
        if (a->mode == MODE_SLEEP) {
            mode_set(a, MODE_IDLE);
            say(a, _("BUEN DIA!"), "", 24);
            aos_hal_beep(1200, 40);
        } else {
            mode_set(a, MODE_SLEEP);
            parts_clear(a);
            say(a, "A DORMIR...", "", 26);
            aos_hal_beep(500, 120);
        }
        break;

    case ACT_SCENE:
    default:
        a->scene = (a->scene == CL_SCENE_HOME) ? CL_SCENE_PARK : CL_SCENE_HOME;
        cl_scene_draw(&a->bg, a->scene);
        mode_set(a, MODE_IDLE);
        say(a, a->scene == CL_SCENE_PARK ? _("AL PATIO!") : _("A CASA!"), "", 24);
        aos_hal_beep(1100, 40);
        aos_hal_pref_set_i32("pet_scene", (int32_t)a->scene);
        break;
    }
    a->bar_dirty = true;
}

/* --------------------------------------------------------------------------
 * Behaviour
 * -------------------------------------------------------------------------- */

static void pet_idle_face(app_t *a)
{
    cl_pet_t *p = &a->pet;

    if (a->blink > 0) {
        p->eye = CL_EYE_BLINK;
        return;
    }
    if (STAT_PCT(a->energy) < 20) {
        p->eye = CL_EYE_SLEEP;
    } else if (STAT_PCT(a->happy) > 70) {
        p->eye = CL_EYE_HAPPY;
    } else {
        p->eye = CL_EYE_OPEN;
    }

    if (STAT_PCT(a->hunger) < 25 || STAT_PCT(a->happy) < 25) {
        p->mouth = CL_MOUTH_SAD;
    } else if (STAT_PCT(a->happy) > 70) {
        p->mouth = CL_MOUTH_TOOTH;
    } else {
        p->mouth = CL_MOUTH_SMILE;
    }
}

/* What it says by itself when bored, according to what it is short of. */
static void idle_talk(app_t *a)
{
    if (a->msg_t > 0) {
        return;
    }
    if (STAT_PCT(a->hunger) < 25) {
        say(a, _("TENGO"), _("HAMBRE"), 30);
    } else if (STAT_PCT(a->clean) < 25) {
        say(a, _("ESTOY"), _("SUCIO"), 30);
    } else if (STAT_PCT(a->energy) < 20) {
        say(a, _("QUE SUENO"), "", 30);
    } else if (STAT_PCT(a->happy) < 30) {
        say(a, _("JUGAMOS?"), "", 30);
    } else if ((rnd() & 3) == 0) {
        static const char *const hi[] = { N_("HOLA!"), N_("TODO BIEN"),
                                          N_("QUE LINDO"), N_("HOLA HOLA") };
        say(a, _(hi[rnd() % 4]), "", 26);
    }
}

static void behave_idle(app_t *a)
{
    cl_pet_t *p = &a->pet;

    if (--a->idle_timer <= 0) {
        a->idle_timer = rnd_range(20, 60);

        switch (rnd() % 5) {
        case 0:
            a->walk_target = rnd_range(24, 68);
            break;
        case 1:
            if (STAT_PCT(a->happy) > 40 && STAT_PCT(a->energy) > 25) {
                a->hop_v = 8;
                aos_hal_beep(1500, 15);
            }
            break;
        case 2:
            p->face_dx = rnd_range(-2, 2);
            break;
        case 3:
            idle_talk(a);
            break;
        default:
            break;
        }
    }

    if (a->walk_target >= 0) {
        int dx = a->walk_target - p->x;
        if (dx > 1) {
            p->x++;
            a->flip = 0;
        } else if (dx < -1) {
            p->x--;
            a->flip = 1;
        } else {
            a->walk_target = -1;
            p->step = 0;
        }
        if (a->walk_target >= 0) {
            p->step = (a->frame / 2) % 2 + 1;
            p->lean = a->flip ? -1 : 1;
        }
    } else {
        p->step = 0;
        p->lean = 0;
    }

    pet_idle_face(a);
    p->arm_l = ((a->frame / 7) % 8 == 0) ? 1 : 0;
    p->arm_r = 0;
}

static void behave_play(app_t *a)
{
    cl_pet_t *p = &a->pet;

    p->eye   = CL_EYE_HAPPY;
    p->mouth = CL_MOUTH_OPEN;

    switch (a->toy) {
    case TOY_BALL: {
        a->ball_x += a->ball_vx;
        a->ball_y += a->ball_vy;
        a->ball_vy += 5;                             /* gravity */

        if (a->ball_x < 6 * 16) {
            a->ball_x = 6 * 16;
            a->ball_vx = -a->ball_vx;
        }
        if (a->ball_x > (CL_ART_W - 17) * 16) {
            a->ball_x = (CL_ART_W - 17) * 16;
            a->ball_vx = -a->ball_vx;
        }
        if (a->ball_y > (CL_FLOOR_Y - 11) * 16) {
            a->ball_y = (CL_FLOOR_Y - 11) * 16;
            a->ball_vy = -(a->ball_vy * 3) / 4;
            if (a->ball_vy > -20) {
                a->ball_vy = -46;                    /* so it never sits completely still */
            }
            aos_hal_beep(700, 12);
        }

        int bx = a->ball_x / 16 + 5;
        if (bx > p->x + 4 && p->x < 70) {
            p->x++;
            a->flip = 0;
            p->step = (a->frame / 2) % 2 + 1;
        } else if (bx < p->x - 4 && p->x > 22) {
            p->x--;
            a->flip = 1;
            p->step = (a->frame / 2) % 2 + 1;
        } else {
            p->step = 0;
            if (a->hop == 0 && (a->frame % 12) == 0) {
                a->hop_v = 10;
                a->happy = clampi(a->happy + 60, 0, STAT_MAX);
                a->hud_dirty = true;
            }
        }
        p->arm_l = p->arm_r = (a->hop > 2) ? 2 : 1;
        break;
    }

    case TOY_BALLOON:
        a->balloon_t++;
        p->arm_r = 2;
        p->arm_l = (a->balloon_t / 6) % 2 ? 1 : 0;
        if ((a->frame % 10) == 0) {
            a->happy = clampi(a->happy + 40, 0, STAT_MAX);
            a->hud_dirty = true;
        }
        if ((a->frame % 26) == 0) {
            int hx, hy;
            cl_pet_hand_at(p, 1, &hx, &hy);
            spawn(a, P_NOTE, hx + 4, hy - 20, rnd_range(-4, 4), -10, 18);
        }
        break;

    case TOY_BUBBLES: {
        if ((a->frame % 5) == 0) {
            spawn(a, P_BUBBLE, rnd_range(8, CL_ART_W - 8), CL_FLOOR_Y - 2,
                  rnd_range(-6, 6), -rnd_range(8, 16), 40);
        }
        /* the ones passing near its head it pops */
        int px, py, pw, ph;
        cl_pet_bbox(p, &px, &py, &pw, &ph);
        for (int i = 0; i < PARTS_MAX; i++) {
            part_t *q = &a->parts[i];
            if (!q->life || q->kind != P_BUBBLE) {
                continue;
            }
            int qx = q->x / 16, qy = q->y / 16;
            if (qx > px - 2 && qx < px + pw + 2 && qy > py - 6 && qy < py + 10) {
                q->life = 0;
                for (int k = 0; k < 3; k++) {
                    spawn(a, P_SPARK, qx, qy, rnd_range(-14, 14), rnd_range(-14, 4), 8);
                }
                a->happy = clampi(a->happy + 90, 0, STAT_MAX);
                a->hud_dirty = true;
                a->hop_v = 7;
                aos_hal_beep(2000, 10);
            }
        }
        p->arm_l = p->arm_r = 2;
        break;
    }

    case TOY_BLOCKS:
    default:
        if (a->block_fall > 0) {
            a->block_fall--;
            p->mouth = CL_MOUTH_LAUGH;
            p->eye   = CL_EYE_SQUINT;
            if (a->block_fall == 0) {
                a->blocks = 0;
            }
        } else if (++a->block_t > 16) {
            a->block_t = 0;
            a->blocks++;
            a->happy = clampi(a->happy + 120, 0, STAT_MAX);
            a->hud_dirty = true;
            aos_hal_beep(900 + a->blocks * 200, 20);
            if (a->blocks >= 5) {
                a->block_fall = 22;
                int bx = p->x + 20;
                for (int i = 0; i < 6; i++) {
                    spawn(a, P_SPARK, bx, CL_FLOOR_Y - 10,
                          rnd_range(-16, 16), rnd_range(-20, -4), 12);
                }
                aos_hal_beep(300, 120);
            }
        }
        p->arm_r = 2;
        p->arm_l = 1;
        break;
    }
}

static void behave_wash(app_t *a)
{
    cl_pet_t *p = &a->pet;

    p->eye   = a->sponge_down ? CL_EYE_SQUINT : CL_EYE_OPEN;
    p->mouth = a->sponge_down ? CL_MOUTH_SMILE : CL_MOUTH_OH;
    p->step  = 0;
    p->lean  = a->sponge_down ? ((a->frame / 2) % 2 ? 1 : -1) : 0;

    if (a->scrub >= 20) {
        a->scrub = 0;
        a->clean = clampi(a->clean + 900, 0, STAT_MAX);
        a->happy = clampi(a->happy + 120, 0, STAT_MAX);
        a->hud_dirty = true;

        if (STAT_PCT(a->clean) >= 99) {
            int px, py, pw, ph;
            cl_pet_bbox(p, &px, &py, &pw, &ph);
            for (int i = 0; i < 6; i++) {
                spawn(a, P_SPARK, px + rnd_range(0, pw), py + rnd_range(0, ph / 2),
                      0, -4, 14);
            }
            say(a, _("LIMPIO!"), "", 26);
            aos_hal_beep(2200, 60);
        }
    }
}

static void behave_tickle(app_t *a)
{
    cl_pet_t *p = &a->pet;

    if (a->anim > 0) {
        a->anim--;
        p->eye   = CL_EYE_SQUINT;
        p->mouth = CL_MOUTH_LAUGH;
        p->lean  = (a->frame % 2) ? 2 : -2;
        p->blush = true;
        p->arm_l = p->arm_r = 2;
    } else {
        p->blush = false;
        p->lean  = 0;
        pet_idle_face(a);
        p->arm_l = p->arm_r = 0;
    }
}

static void behave_sleep(app_t *a)
{
    cl_pet_t *p = &a->pet;

    p->eye    = CL_EYE_SLEEP;
    p->mouth  = CL_MOUTH_FLAT;
    p->step   = 0;
    p->lean   = 0;
    p->arm_l  = p->arm_r = 0;
    /* it breathes: it squashes and stretches slowly */
    p->squash = ((a->frame / 10) % 2) ? 4 : 3;

    if ((a->frame % 20) == 0) {
        int px, py, pw, ph;
        cl_pet_bbox(p, &px, &py, &pw, &ph);
        spawn(a, P_ZZZ, px + pw - 4, py - 4, 2, -6, 26);
    }
}

/* --------------------------------------------------------------------------
 * Objects drawn on top of the stage
 * -------------------------------------------------------------------------- */

static void draw_flies(app_t *a)
{
    /* the flies only appear when it is filthy: they communicate the state
     * better than the cleanliness bar */
    if (STAT_PCT(a->clean) >= 30 || a->mode == MODE_SLEEP) {
        return;
    }
    int px, py, pw, ph;
    cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);

    for (int i = 0; i < 3; i++) {
        int t = a->frame * 2 + i * 40;
        int x = px + pw / 2 + ((t / 3) % 30) - 15;
        int y = py - 4 + ((t / 5 + i * 3) % 10);
        cl_px(&a->stage, x, y, cl_rgb(0x1B1210));
        cl_px(&a->stage, x + 1, y, cl_rgb(0x1B1210));
        cl_px(&a->stage, x + ((a->frame & 1) ? 1 : -1), y - 1, cl_rgb(0x8A8A90));
    }
}

static void draw_toy(app_t *a)
{
    cl_pet_t *p = &a->pet;

    switch (a->toy) {
    case TOY_BALL:
        cl_shade(&a->stage, a->ball_x / 16 + 1, CL_FLOOR_Y, 9, 1, -4);
        cl_blit(&a->stage, a->ball_x / 16, a->ball_y / 16,
                CL_SPRITE(cl_spr_ball), false);
        break;

    case TOY_BALLOON: {
        int hx, hy;
        cl_pet_hand_at(p, 1, &hx, &hy);
        int sway = ((a->balloon_t / 5) % 4) - 2;
        int bx = hx + sway - 3;
        int by = hy - 26;

        cl_blit(&a->stage, bx, by, CL_SPRITE(cl_spr_balloon), false);
        /* the string goes from the knot to the hand, with a sag that follows
         * the swing */
        for (int y = by + 11; y <= hy; y++) {
            int t = y - (by + 11);
            cl_px(&a->stage, bx + 4 - (sway * t) / 14, y, cl_rgb(0xFFFFFF));
        }
        break;
    }

    case TOY_BLOCKS: {
        static const uint32_t col[5] = { 0xE5484D, 0x4A9DF5, 0xFFD60A, 0x45C463, 0xB072F0 };
        int bx = p->x + 18;
        if (bx > CL_ART_W - 14) {
            bx = CL_ART_W - 14;
        }
        for (int i = 0; i < a->blocks && i < 5; i++) {
            int wob = 0;
            if (a->block_fall > 0) {
                wob = (i + 1) * ((a->block_fall / 2 % 2) ? 2 : -2);
            } else if (a->blocks >= 4) {
                wob = ((a->frame / 3) % 2) ? i : -i;
            }
            int y = CL_FLOOR_Y - 6 - i * 6;
            cl_rect(&a->stage, bx + wob, y, 12, 6, cl_rgb(col[i]));
            cl_shade(&a->stage, bx + wob + 1, y + 1, 10, 1, 6);
            cl_shade(&a->stage, bx + wob + 1, y + 4, 10, 1, -5);
            cl_frame(&a->stage, bx + wob, y, 12, 6, cl_rgb(0x1B1210));
        }
        break;
    }

    default:
        break;
    }
}

static void draw_food_flight(app_t *a)
{
    /* After the bite the food no longer exists: if it went on being drawn at
     * the destination a sprite would stay stuck on its face while it chews. */
    if (a->fly_t > 12) {
        return;
    }

    const food_t *f = &FOODS[a->food_kind];
    int mx, my;
    cl_pet_mouth_at(&a->pet, &mx, &my);

    int total = 12;
    int t = a->fly_t;
    int x = a->fly_x0 + (mx - 5 - a->fly_x0) * t / total;
    int y = a->fly_y0 + (my - 4 - a->fly_y0) * t / total;

    cl_blit(&a->stage, x, y, f->rows, f->nrows, false);
}

static void draw_sponge(app_t *a)
{
    int x = a->sponge_x - 5;
    int y = a->sponge_y - 7;      /* the sponge's body starts on row 4 */

    if (a->sponge_down) {
        cl_shade(&a->stage, x - 1, y + 3, 13, 9, 4);
    }
    cl_blit(&a->stage, x, y, CL_SPRITE(cl_spr_sponge), false);
}

static void draw_hand(app_t *a)
{
    cl_blit(&a->stage, a->sponge_x - 4, a->sponge_y + 1, CL_SPRITE(cl_spr_hand),
            a->sponge_x > a->pet.x);
}

static void day_counter_reset(app_t *a)
{
    a->born_day = wall_days();
    a->last_min = wall_minutes();
    stats_save(a);                  /* so it is not lost if the power goes */

    a->hud_dirty = true;
    a->hop_v = 9;
    say(a, _("DIA 1!"), _("DE NUEVO"), 28);
    aos_hal_beep(2200, 70);

    int px, py, pw, ph;
    cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);
    for (int i = 0; i < 6; i++) {
        spawn(a, P_SPARK, px + rnd_range(0, pw), py + rnd_range(0, ph / 2),
              rnd_range(-10, 10), -rnd_range(4, 12), 14);
    }
}

/* --------------------------------------------------------------------------
 * One frame
 * -------------------------------------------------------------------------- */

static void frame_draw(app_t *a)
{
    cl_buf_t *b = &a->stage;

    cl_copy(b, &a->bg);
    if (a->mode == MODE_SLEEP) {
        cl_scene_night(b, a->scene, a->frame);
    } else {
        cl_scene_anim(b, a->scene, a->frame);
    }

    /* the critter, with the jump applied to the floor */
    a->pet.y = CL_FLOOR_Y - a->hop;
    a->pet.dirt = 3 - clampi(STAT_PCT(a->clean) / 25, 0, 3);
    cl_pet_draw(b, &a->pet);

    draw_flies(a);

    switch (a->mode) {
    case MODE_EATING:
        draw_food_flight(a);
        break;
    case MODE_PLAYING:
        draw_toy(a);
        break;
    case MODE_WASH:
        draw_sponge(a);
        break;
    case MODE_TICKLE:
        draw_hand(a);
        break;
    default:
        break;
    }

    parts_draw(a);

    if (a->mode == MODE_FOOD_TRAY || a->mode == MODE_TOY_TRAY) {
        tray_draw(a);
    } else if (a->msg_t > 0) {
        int px, py, pw, ph;
        cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);
        bubble_draw(b, px + pw / 2, py - 2, a->msg1, a->msg2);
    }

    expand4(a->mem_stage, CL_ART_W, CL_STAGE_H, a->big_stage);
    lv_obj_invalidate(a->canvas_stage);
}

static void step(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this call 'a' no longer exists
         * and nothing else can be touched */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    a->frame++;

    /* the stats clock */
    a->acc_ms += FRAME_MS;
    while (a->acc_ms >= 1000) {
        a->acc_ms -= 1000;
        stats_second(a);
    }
    if (++a->save_timer >= (20000 / FRAME_MS)) {
        a->save_timer = 0;
        a->last_min = wall_minutes();
        stats_save(a);
    }

    /* sustained press on the name */
    if (a->hold_t > 0) {
        a->hold_t++;
        a->hud_dirty = true;
        if (a->hold_t >= HOLD_FRAMES) {
            a->hold_t = 0;          /* not repeated while the finger stays down */
            day_counter_reset(a);
        }
    }

    /* blinking */
    if (a->blink > 0) {
        a->blink--;
    } else if ((rnd() % 40) == 0) {
        a->blink = 2;
    }

    /* jump */
    if (a->hop > 0 || a->hop_v > 0) {
        a->hop += a->hop_v;
        a->hop_v -= 3;
        if (a->hop <= 0) {
            a->hop = 0;
            a->hop_v = 0;
            a->pet.squash = 3;
        }
    } else if (a->pet.squash > 0 && a->mode != MODE_SLEEP) {
        a->pet.squash--;
    }
    if (a->hop > 2 && a->mode != MODE_SLEEP) {
        a->pet.squash = -2;
    }

    if (a->msg_t > 0) {
        a->msg_t--;
    }

    switch (a->mode) {
    case MODE_EATING:
        a->fly_t++;
        if (a->fly_t == 12) {
            a->pet.mouth = CL_MOUTH_BIG;
            a->pet.eye   = CL_EYE_SQUINT;
            aos_hal_beep(600, 40);
            int mx, my;
            cl_pet_mouth_at(&a->pet, &mx, &my);
            for (int i = 0; i < 4; i++) {
                spawn(a, P_CRUMB, mx + rnd_range(-4, 4), my,
                      rnd_range(-10, 10), -8, 12);
            }
        } else if (a->fly_t > 12 && a->fly_t < 26) {
            a->pet.mouth = CL_MOUTH_CHEW;
            a->pet.step  = (a->fly_t / 2) % 2;
            a->pet.eye   = CL_EYE_HAPPY;
        } else if (a->fly_t >= 26) {
            feed_finish(a);
        }
        break;

    case MODE_PLAYING:
        behave_play(a);
        break;

    case MODE_WASH:
        behave_wash(a);
        break;

    case MODE_TICKLE:
        behave_tickle(a);
        break;

    case MODE_SLEEP:
        behave_sleep(a);
        break;

    case MODE_FOOD_TRAY:
    case MODE_TOY_TRAY:
        pet_idle_face(a);
        a->pet.step = 0;
        break;

    case MODE_IDLE:
    default:
        behave_idle(a);
        break;
    }

    parts_step(a);
    frame_draw(a);

    if (a->hud_dirty) {
        a->hud_dirty = false;
        hud_draw(a);
    }
    if (a->bar_dirty) {
        a->bar_dirty = false;
        bar_draw(a);
    }
}

/* --------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------- */

/* Converts the finger's point to stage art coordinates. It is measured against
 * the object and not against the screen because when the app opens the runtime
 * slides the root: during that animation the two do not coincide. */
static bool touch_point(app_t *a, int *ax, int *ay)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return false;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t area;
    lv_obj_get_coords(a->stage_touch, &area);
    *ax = (point.x - area.x1) / CL_SCALE;
    *ay = (point.y - area.y1) / CL_SCALE;
    return true;
}

static void tickle_hit(app_t *a)
{
    a->anim = 12;
    a->happy = clampi(a->happy + 250, 0, STAT_MAX);
    a->energy = clampi(a->energy - 30, 0, STAT_MAX);
    a->hud_dirty = true;

    int px, py, pw, ph;
    cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);
    for (int i = 0; i < 3; i++) {
        spawn(a, P_HEART, px + rnd_range(2, pw - 4), py, rnd_range(-8, 8), -11,
              rnd_range(12, 20));
    }
    static const char *const laugh[] = { N_("JI JI JI!"), N_("JA JA!"),
                                         N_("BASTA!"), N_("MAS!") };
    say(a, _(laugh[rnd() % 4]), "", 16);
    aos_hal_beep(1800 + (int)(rnd() % 400), 25);
}

static void stage_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    int x, y;
    if (!touch_point(a, &x, &y)) {
        return;
    }

    int px, py, pw, ph;
    cl_pet_bbox(&a->pet, &px, &py, &pw, &ph);
    bool on_pet = (x >= px - 2 && x <= px + pw + 2 && y >= py - 2 && y <= py + ph + 2);

    switch (a->mode) {
    case MODE_WASH:
        a->sponge_px = a->sponge_x;
        a->sponge_py = a->sponge_y;
        a->sponge_x  = x;
        a->sponge_y  = y;

        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            a->sponge_down = false;
            break;
        }
        a->sponge_down = true;

        if (on_pet) {
            int dx = a->sponge_x - a->sponge_px;
            int dy = a->sponge_y - a->sponge_py;
            int moved = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (moved > 0) {
                a->scrub += moved;
                if ((a->frame % 2) == 0) {
                    spawn(a, P_SUDS, x + rnd_range(-4, 4), y + rnd_range(-4, 2),
                          rnd_range(-6, 6), -rnd_range(2, 8), 12);
                }
                if ((a->frame % 7) == 0) {
                    aos_hal_beep(400 + (int)(rnd() % 300), 8);
                }
            }
        }
        break;

    case MODE_TICKLE:
        a->sponge_x = x;
        a->sponge_y = y;
        if (code == LV_EVENT_PRESSED && on_pet) {
            tickle_hit(a);
        }
        break;

    case MODE_SLEEP:
        if (code == LV_EVENT_PRESSED) {
            action_do(a, ACT_SLEEP);
        }
        break;

    case MODE_FOOD_TRAY:
    case MODE_TOY_TRAY:
        /* touching outside the tray closes it */
        if (code == LV_EVENT_PRESSED && y > 40) {
            mode_set(a, MODE_IDLE);
        }
        break;

    default:
        if (code == LV_EVENT_PRESSED && on_pet) {
            /* a stroke: it always adds something */
            a->happy = clampi(a->happy + 120, 0, STAT_MAX);
            a->hud_dirty = true;
            a->pet.eye = CL_EYE_LOVE;
            a->hop_v = 7;
            spawn(a, P_HEART, px + pw / 2, py - 2, rnd_range(-5, 5), -10, 18);
            aos_hal_beep(1700, 20);
            if ((rnd() & 1) == 0) {
                say(a, "MMM...", "", 14);
            }
        }
        break;
    }
}

/* The app asks for AOS_APP_FLAG_NO_SWIPE because cleaning the critter means
 * dragging your finger, and the runtime's back gesture fires at 50 px of drag:
 * rubbing its belly would take you out of the app. In exchange, it interprets
 * the gesture itself, and does nothing while the finger is at work. */
static void stage_gesture(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();

    if (!indev || a->mode == MODE_WASH || a->mode == MODE_TICKLE) {
        return;
    }
    if (lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }

    /* on release, LVGL sends a CLICKED to the object below anyway */
    lv_indev_wait_release(indev);

    /* same rule as claudito_back(): the tray closes, not the action */
    if (a->mode == MODE_FOOD_TRAY || a->mode == MODE_TOY_TRAY) {
        mode_set(a, MODE_IDLE);
    } else {
        a->want_exit = true;        /* it closes on the next frame, not here */
    }
}

/* Holding the name down resets the day counter. It is counted in frames and
 * not with LV_EVENT_LONG_PRESSED because LVGL's threshold belongs to the input
 * device (global, changing it would affect the whole system) and because this
 * way how much is left can be drawn. */
static void name_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);

    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        a->hold_t = 1;
        a->hud_dirty = true;
        return;
    }

    /* releasing early: count so it is of some use, since pressing the name
       does nothing else */
    if (a->hold_t > 0) {
        say(a, _("MANTENE PARA"), _("REINICIAR"), 30);
    }
    a->hold_t = 0;
    a->hud_dirty = true;
}

static void action_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    int act = (int)(lv_uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(event));
    action_do(a, act);
}

static void tray_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    int idx = (int)(lv_uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(event));

    if (a->mode == MODE_FOOD_TRAY) {
        feed_start(a, idx);
    } else if (a->mode == MODE_TOY_TRAY) {
        toy_start(a, (toy_t)idx);
    }
}

/* --------------------------------------------------------------------------
 * Building the UI
 * -------------------------------------------------------------------------- */

/* An art canvas. The small buffer is upscaled x4 by hand with expand4() and
 * the canvas receives the result already at its real size, so LVGL draws it
 * 1:1 and there is no scaling in between: the 4x4 blocks come out exact. */
static lv_obj_t *art_canvas(lv_obj_t *parent, uint16_t *big, int w, int h, int y)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    /* The canvas receives the ALREADY upscaled buffer and is drawn 1:1: no STRETCH. */
    lv_canvas_set_buffer(canvas, big, w * CL_SCALE, h * CL_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, w * CL_SCALE, h * CL_SCALE);
    lv_obj_set_pos(canvas, 0, y * CL_SCALE);
    lv_image_set_antialias(canvas, false);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    return canvas;
}

static lv_obj_t *hit_area(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_event_cb_t cb, uint32_t codes, void *user, int index)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(obj, (void *)(lv_uintptr_t)index);

    if (codes & 1) lv_obj_add_event_cb(obj, cb, LV_EVENT_PRESSED, user);
    if (codes & 2) lv_obj_add_event_cb(obj, cb, LV_EVENT_PRESSING, user);
    if (codes & 4) lv_obj_add_event_cb(obj, cb, LV_EVENT_RELEASED, user);
    if (codes & 8) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
    /* PRESS_LOST arrives when the finger moves off the object without
       releasing: for a sustained press that is as much a cancellation as
       releasing */
    if (codes & 16) lv_obj_add_event_cb(obj, cb, LV_EVENT_PRESS_LOST, user);
    return obj;
}

/* Integer-scale upscaling, done by hand.
 *
 * Measured on the board: letting LVGL stretch the canvas
 * (LV_IMAGE_ALIGN_STRETCH) costs 129 ms per frame, that is, 7 fps. The reason
 * is in lv_draw_sw_transform: with an RGB565 source it ALSO generates a
 * per-pixel alpha channel and composites with alpha blending instead of
 * copying, so each of the 164,864 pixels goes through transform + mask +
 * blending (~190 cycles per pixel).
 *
 * Here it is upscaled by hand and the canvas ends up 1:1, which for LVGL is a
 * flat copy. Each row is written once and the vertical repeats are memcpy. */
static void expand4(const uint16_t *src, int w, int h, uint16_t *dst)
{
    const int dw = w * CL_SCALE;
    for (int y = 0; y < h; y++) {
        uint16_t       *row = dst + (size_t)y * CL_SCALE * dw;
        const uint16_t *s   = src + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t  c = s[x];
            uint16_t *p = row + x * CL_SCALE;
            p[0] = p[1] = p[2] = p[3] = c;
        }
        for (int k = 1; k < CL_SCALE; k++) {
            memcpy(row + (size_t)k * dw, row, (size_t)dw * sizeof(uint16_t));
        }
    }
}

static void free_buffers(app_t *a)
{
    free(a->mem_hud);
    free(a->mem_stage);
    free(a->mem_bar);
    free(a->mem_bg);
    free(a->big_hud);
    free(a->big_stage);
    free(a->big_bar);
    a->mem_hud = a->mem_stage = a->mem_bar = a->mem_bg = NULL;
    a->big_hud = a->big_stage = a->big_bar = NULL;
}

/* The four buffers add up to some 35 KB. They go through malloc() and not
 * lv_malloc() on purpose: LVGL's pool on the board is 64 KB and it needs it
 * for the objects and the draw buffers, whereas malloc() with PSRAM has plenty
 * to spare for blocks of this size. */
static bool alloc_buffers(app_t *a)
{
    size_t stage_px = (size_t)CL_ART_W * CL_STAGE_H;
    size_t hud_px   = (size_t)CL_ART_W * CL_HUD_H;
    size_t bar_px   = (size_t)CL_ART_W * CL_BAR_H;

    a->mem_stage = (uint16_t *)malloc(stage_px * sizeof(uint16_t));
    a->mem_bg    = (uint16_t *)malloc(stage_px * sizeof(uint16_t));
    a->mem_hud   = (uint16_t *)malloc(hud_px   * sizeof(uint16_t));
    a->mem_bar   = (uint16_t *)malloc(bar_px   * sizeof(uint16_t));

    /* The upscaled ones: ~330 KB in total, they go to PSRAM and are
     * comfortable there. */
    const size_t big = (size_t)CL_SCALE * CL_SCALE * sizeof(uint16_t);
    a->big_stage = (uint16_t *)malloc(stage_px * big);
    a->big_hud   = (uint16_t *)malloc(hud_px   * big);
    a->big_bar   = (uint16_t *)malloc(bar_px   * big);

    if (!a->mem_stage || !a->mem_bg || !a->mem_hud || !a->mem_bar ||
        !a->big_stage || !a->big_hud || !a->big_bar) {
        free_buffers(a);
        return false;
    }

    a->stage.px = a->mem_stage; a->stage.w = CL_ART_W; a->stage.h = CL_STAGE_H;
    a->bg.px    = a->mem_bg;    a->bg.w    = CL_ART_W; a->bg.h    = CL_STAGE_H;
    a->hud.px   = a->mem_hud;   a->hud.w   = CL_ART_W; a->hud.h   = CL_HUD_H;
    a->bar.px   = a->mem_bar;   a->bar.w   = CL_ART_W; a->bar.h   = CL_BAR_H;
    return true;
}

static void *claudito_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }
    if (!alloc_buffers(a)) {
        lv_free(a);
        return NULL;
    }

    s_rng = (uint32_t)aos_hal_uptime_ms() | 1u;

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    cl_pet_init(&a->pet);
    a->pet.y = CL_FLOOR_Y;
    a->walk_target = -1;
    a->idle_timer  = 20;
    a->mode        = MODE_IDLE;

    stats_load(a);
    cl_scene_draw(&a->bg, a->scene);

    a->canvas_hud   = art_canvas(root, a->big_hud,   CL_ART_W, CL_HUD_H,   0);
    a->canvas_stage = art_canvas(root, a->big_stage, CL_ART_W, CL_STAGE_H, CL_STAGE_Y);
    a->canvas_bar   = art_canvas(root, a->big_bar,   CL_ART_W, CL_BAR_H,   CL_BAR_Y);

    /* the stage's touch layer: in LVGL 9 every lv_obj is born clickable, so
     * the canvases would eat the touch if the flag were not taken off them */
    a->stage_touch = hit_area(root, 0, CL_STAGE_Y * CL_SCALE,
                              CL_ART_W * CL_SCALE, CL_STAGE_H * CL_SCALE,
                              stage_event, 1 | 2 | 4 | 16, a, 0);
    lv_obj_add_event_cb(a->stage_touch, stage_gesture, LV_EVENT_GESTURE, a);

    /* the name and the day counter: holding it down resets the days. It
       reaches down to row 10 of the art so as not to eat the bars. */
    /* At y = 56 and not 0: the panel reports nothing above 55 (AOS_TOUCH_Y_MIN),
     * so the hold lives on the first rows of the stage, over the name. */
    a->name_touch = hit_area(root, 0, AOS_TOUCH_Y_MIN, CL_ART_W * CL_SCALE, 11 * CL_SCALE,
                             name_event, 1 | 4 | 16, a, 0);

    for (int i = 0; i < TRAY_ITEMS; i++) {
        int x = (TRAY_X0 + i * TRAY_STEP) * CL_SCALE;
        int y = (CL_STAGE_Y + TRAY_Y) * CL_SCALE;
        a->tray_btn[i] = hit_area(root, x, y, TRAY_SIZE * CL_SCALE, TRAY_SIZE * CL_SCALE,
                                  tray_event, 8, a, i);
        lv_obj_add_flag(a->tray_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < ACT_COUNT; i++) {
        /* 32 px taller than the bar, upwards: the panel reports nothing
         * below y = 395 and the bar alone left 11 px to touch. */
        a->act_btn[i] = hit_area(root, (1 + i * 15) * CL_SCALE, CL_BAR_Y * CL_SCALE - 32,
                                 14 * CL_SCALE, CL_BAR_H * CL_SCALE + 32,
                                 action_event, 8, a, i);
    }

    /* Only now can the mode it was left in be restored: mode_set() touches the
       tray's buttons, which did not exist until this line. */
    switch (a->mode) {
    case MODE_PLAYING:
        toy_reset(a, a->toy);
        break;
    case MODE_WASH:
    case MODE_TICKLE:
        a->sponge_x = a->pet.x + 20;
        a->sponge_y = CL_FLOOR_Y - 14;
        break;
    default:
        break;
    }
    mode_set(a, a->mode);

    hud_draw(a);
    bar_draw(a);
    frame_draw(a);

    /* one that is still sleeping is not greeted */
    if (a->mode != MODE_SLEEP) {
        say(a, _("HOLA!"), "", 30);
    }
    a->timer = lv_timer_create(step, FRAME_MS, a);
    return a;
}

static void claudito_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    a->last_min = wall_minutes();
    stats_save(a);
    free_buffers(a);
    lv_free(a);
}

/* Back -the side button, or the back key- does NOT cancel whatever the critter
 * is doing: leaving is leaving. If you left it sleeping the app closes and it
 * goes on sleeping, and when you open it again you find it asleep with the
 * energy it recovered meanwhile (see stats_load).
 *
 * The one exception is an open tray, which is not an action but a half-chosen
 * menu: there, yes, one step back closes it. */
static bool claudito_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;

    if (a && (a->mode == MODE_FOOD_TRAY || a->mode == MODE_TOY_TRAY)) {
        mode_set(a, MODE_IDLE);
        return true;
    }
    return false;                   /* let the runtime close the app */
}

static bool claudito_init(aos_app_t *app)
{
    app->desc.id      = "demo.claudito";
    app->desc.name    = "Claudito";
    app->desc.icon    = "C";
    app->desc.icon_vec = AOS_ICON_PET;
    app->desc.color_a = 0xD97757;
    app->desc.color_b = 0x9C4A32;
    app->desc.order   = 140;
    /* LONG_DRAG: rubbing the critter with the sponge easily passes the 50 px
     * that trigger the global gesture detection, and that calls
     * lv_indev_wait_release() even when the app has NO_SWIPE: without the
     * flag, the rubbing cuts itself short halfway. */
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                        AOS_APP_FLAG_NO_SWIPE  | AOS_APP_FLAG_LONG_DRAG;

    app->create  = claudito_create;
    app->destroy = claudito_destroy;
    app->back    = claudito_back;
    return true;
}

AOS_APP_ENTRY(claudito_init);
