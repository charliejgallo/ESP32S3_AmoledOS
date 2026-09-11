/*
 * AmoledOS - Life
 *
 * Two cellular automata on the same grid: Conway's Game of Life and Langton's
 * ant. They are together because they share everything expensive -the grid,
 * the upscaling and the drawing- and differ in twenty lines of rules.
 *
 * How it is drawn, which is the only decision that matters here:
 *
 *   - The grid is 92x92 cells and the canvas measures 368x368, that is, an
 *     EXACT scale of 4, and LVGL is given a 1:1 canvas. Stretching the canvas
 *     with LV_IMAGE_ALIGN_STRETCH costs 129 ms per frame measured on the board
 *     (7 fps), because LVGL allocates an alpha plane and composites with
 *     blending instead of copying. Upscaling by hand, the upscale is one write
 *     per cell and the rest is memcpy of rows.
 *   - There are no dirty rectangles, and it is right that there are none: in a
 *     cellular automaton half the screen changes per frame, so keeping track
 *     of what changed would cost more than redrawing.
 *   - The control bar is NOT on top of the canvas: it occupies the 80 px left
 *     over at the bottom. That way the canvas never invalidates it and its
 *     buttons are not redrawn on every frame.
 *
 * The upscaled buffer is 270 KB and goes through malloc(), that is, to PSRAM,
 * of which there is plenty. What is scarce is internal RAM, and only the two
 * 8.5 KB grids live there.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Geometry -------------------------------------------------------------
 * 92 x 4 = 368, the exact width of the screen. 92 rows x 4 = 368 of height
 * leave 80 px for the bar, which is exactly what one line of text plus a row
 * of buttons needs. */
#define W           92
#define H           92
#define SCALE       3           /* x4 was 368 px: with the bar below the touch floor it no longer fits */
#define DW          (W * SCALE)
#define DH          (H * SCALE)
/* The bar goes ON TOP and the counter at the bottom.
 *
 * It was the other way round and the five buttons ended up at y 390..433,
 * against a touch panel that reports nothing below 395 (see AOS_TOUCH_Y_MAX in
 * aos_hal.h): one useful pixel was left and in practice they did not respond.
 * Now the bar is 56 px at the top, the canvas goes below and the "gen / cells"
 * line -which is only read- is sent to the strip that cannot be touched, which
 * is exactly what it is good for.
 *
 * The canvas still measures 368x368 and now occupies y 56..424, so the touch
 * for seeding cells does not reach the grid's last 34 px strip. It is a
 * shortcut, not the main control, and that was preferred over shrinking the
 * 92x92 grid, which would change the simulation. */
/* The panel reports nothing above y = 55 (AOS_TOUCH_Y_MIN): the buttons sit at
 * 56..100 and the grid, now 276 px, below them; the status line goes into the
 * bottom dead strip. */
#define BAR_H       100
#define CANVAS_Y    BAR_H
#define CANVAS_X    ((AOS_SCREEN_W - DW) / 2)
#define INFO_Y      400

#define AGE_MAX     7           /* different ages Life colours by */

typedef enum { MODE_LIFE = 0, MODE_ANT } mode_t_;

/* Speeds: the frame period, not the number of steps. At 60 ms we are already
 * grazing the software renderer's drawing floor. */
static const uint16_t PERIOD_MS[3] = { 240, 120, 60 };
static const char *const SPEED_NAME[3] = { "x1", "x2", "x4" };

/* Ant steps per frame: one at a time it would never be seen to advance. */
#define ANT_STEPS   120

typedef struct {
    lv_obj_t   *canvas;
    lv_obj_t   *lbl_info;
    lv_obj_t   *lbl_play;
    lv_obj_t   *lbl_speed;
    lv_obj_t   *lbl_mode;
    lv_timer_t *timer;

    uint16_t *big;              /* 368x368 RGB565, PSRAM */
    uint8_t  *cell;             /* current state */
    uint8_t  *next;             /* double buffer of the step */

    mode_t_ mode;
    bool    running;
    int     speed;              /* index into PERIOD_MS */
    int     seed_kind;          /* what it seeds next time */

    uint32_t gen;
    uint32_t pop;
    uint32_t last_info_ms;      /* the counter is not rewritten on every frame */
    uint32_t last_pop;
    uint32_t same_pop;          /* consecutive generations with the same population */

    /* ant */
    int  ant_x, ant_y, ant_dir; /* dir: 0 up, 1 right, 2 down, 3 left */

    uint32_t rng;
    aos_app_t *self;            /* the runtime's copy: for KEEP_AWAKE */
} life_t;

static life_t s_life;

/* Palettes in RGB565. Life colours by age -the newborn in yellow, the old in
 * dark green-, which is what lets you see at a glance where something is
 * happening. */
static uint16_t s_pal_life[AGE_MAX + 1];
static uint16_t s_pal_ant[2];
static uint16_t s_ant_color;

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 |
                      ((rgb >> 10) & 0x3F) << 5  |
                      ((rgb >> 3)  & 0x1F));
}

static void palettes_init(void)
{
    static const uint32_t LIFE[AGE_MAX + 1] = {
        0x000000,   /* dead */
        0xFFF0A0, 0xFFD60A, 0x8CE07A, 0x30D158,
        0x24A048, 0x186C34, 0x0E4622,
    };
    for (int i = 0; i <= AGE_MAX; i++) {
        s_pal_life[i] = rgb565(LIFE[i]);
    }
    s_pal_ant[0] = rgb565(0x000000);
    s_pal_ant[1] = rgb565(0xFF9F0A);
    s_ant_color  = rgb565(0xFF453A);
}

/* -------------------------------------------------------------------------- */
/* Randomness                                                                  */

static uint32_t rnd(void)
{
    /* xorshift32: more than enough for a soup and it does not drag in rand(). */
    uint32_t x = s_life.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_life.rng = x;
    return x;
}

/* -------------------------------------------------------------------------- */
/* Seeding                                                                     */

static inline void put(int x, int y, uint8_t v)
{
    if (x >= 0 && x < W && y >= 0 && y < H) {
        s_life.cell[(size_t)y * W + x] = v;
    }
}

/* A pattern described as rows of text: '#' is alive. Far more readable than a
 * list of coordinates and it takes no more room. */
static void stamp(const char *const *rows, int count, int x0, int y0)
{
    for (int r = 0; r < count; r++) {
        for (int c = 0; rows[r][c]; c++) {
            if (rows[r][c] == '#') {
                put(x0 + c, y0 + r, 1);
            }
        }
    }
}

static void seed_soup(void)
{
    for (int i = 0; i < W * H; i++) {
        s_life.cell[i] = (rnd() % 100) < 32 ? 1 : 0;
    }
}

static void seed_gun(void)
{
    /* Gosper's glider gun (1970): the first proof that a pattern can grow
     * without limit. It measures 36x9 and fits with room to spare. */
    static const char *const GUN[9] = {
        "........................#...........",
        "......................#.#...........",
        "............##......##............##",
        "...........#...#....##............##",
        "##........#.....#...##..............",
        "##........#...#.##....#.#...........",
        "..........#.....#.......#...........",
        "...........#...#....................",
        "............##......................",
    };
    memset(s_life.cell, 0, W * H);
    stamp(GUN, 9, 6, 10);
}

static void seed_acorn(void)
{
    /* The acorn: seven cells that take 5206 generations to settle. On a grid
     * with wrapped edges it does not end the same as on the infinite plane,
     * but it makes a nice mess for a good while. */
    static const char *const ACORN[3] = {
        ".#.....",
        "...#...",
        "##..###",
    };
    memset(s_life.cell, 0, W * H);
    stamp(ACORN, 3, W / 2 - 3, H / 2 - 1);
}

static void seed_ant(void)
{
    memset(s_life.cell, 0, W * H);
    s_life.ant_x   = W / 2;
    s_life.ant_y   = H / 2;
    s_life.ant_dir = 0;
}

static void reseed(void)
{
    s_life.gen      = 0;
    s_life.same_pop = 0;
    s_life.last_pop = 0xFFFFFFFFu;

    if (s_life.mode == MODE_ANT) {
        seed_ant();
        return;
    }
    switch (s_life.seed_kind % 3) {
    case 0:  seed_soup();  break;
    case 1:  seed_gun();   break;
    default: seed_acorn(); break;
    }
}

/* -------------------------------------------------------------------------- */
/* Rules                                                                       */

static void step_life(void)
{
    const uint8_t *cell = s_life.cell;
    uint8_t *next = s_life.next;
    uint32_t pop = 0;

    for (int y = 0; y < H; y++) {
        /* The edges wrap (torus): a glider leaving through the top comes back
         * through the bottom, which on a small screen is the only thing that
         * keeps the picture alive. The modulo is avoided with two
         * conditions. */
        const uint8_t *r0 = cell + (size_t)(y ? y - 1 : H - 1) * W;
        const uint8_t *r1 = cell + (size_t)y * W;
        const uint8_t *r2 = cell + (size_t)(y + 1 < H ? y + 1 : 0) * W;
        uint8_t *out = next + (size_t)y * W;

        for (int x = 0; x < W; x++) {
            int xm = x ? x - 1 : W - 1;
            int xp = x + 1 < W ? x + 1 : 0;

            int n = (r0[xm] != 0) + (r0[x] != 0) + (r0[xp] != 0) +
                    (r1[xm] != 0) +                (r1[xp] != 0) +
                    (r2[xm] != 0) + (r2[x] != 0) + (r2[xp] != 0);

            uint8_t cur = r1[x];
            uint8_t v;
            if (cur) {
                v = (n == 2 || n == 3) ? (uint8_t)(cur < AGE_MAX ? cur + 1 : AGE_MAX) : 0;
            } else {
                v = (n == 3) ? 1 : 0;
            }
            out[x] = v;
            pop += (v != 0);
        }
    }

    memcpy(s_life.cell, next, (size_t)W * H);
    s_life.gen++;
    s_life.pop = pop;

    /* A population that does not move for 150 generations is a dead board or
     * one full of still lifes: it reseeds itself, since this is meant to be
     * watched and not played. */
    if (pop == s_life.last_pop) {
        s_life.same_pop++;
    } else {
        s_life.same_pop = 0;
        s_life.last_pop = pop;
    }
    if (pop == 0 || s_life.same_pop > 150) {
        s_life.seed_kind++;
        reseed();
    }
}

static void step_ant(void)
{
    static const int DX[4] = { 0, 1, 0, -1 };
    static const int DY[4] = { -1, 0, 1, 0 };

    for (int i = 0; i < ANT_STEPS; i++) {
        size_t idx = (size_t)s_life.ant_y * W + s_life.ant_x;
        if (s_life.cell[idx]) {
            s_life.ant_dir = (s_life.ant_dir + 3) & 3;      /* black: left */
            s_life.cell[idx] = 0;
        } else {
            s_life.ant_dir = (s_life.ant_dir + 1) & 3;      /* white: right */
            s_life.cell[idx] = 1;
        }
        s_life.ant_x += DX[s_life.ant_dir];
        s_life.ant_y += DY[s_life.ant_dir];
        if (s_life.ant_x < 0)  s_life.ant_x = W - 1;
        if (s_life.ant_x >= W) s_life.ant_x = 0;
        if (s_life.ant_y < 0)  s_life.ant_y = H - 1;
        if (s_life.ant_y >= H) s_life.ant_y = 0;
    }
    s_life.gen += ANT_STEPS;

    uint32_t pop = 0;
    for (int i = 0; i < W * H; i++) {
        pop += (s_life.cell[i] != 0);
    }
    s_life.pop = pop;
}

/* -------------------------------------------------------------------------- */
/* Drawing                                                                     */

/* One write per cell; the three repeated rows are a memcpy. */
static void expand(void)
{
    const uint16_t *pal = (s_life.mode == MODE_ANT) ? s_pal_ant : s_pal_life;

    for (int y = 0; y < H; y++) {
        uint16_t      *row = s_life.big + (size_t)y * SCALE * DW;
        const uint8_t *src = s_life.cell + (size_t)y * W;

        for (int x = 0; x < W; x++) {
            uint16_t c = pal[src[x]];
            uint16_t *p = row + x * SCALE;
            p[0] = c; p[1] = c; p[2] = c; p[3] = c;
        }
        for (int k = 1; k < SCALE; k++) {
            memcpy(row + (size_t)k * DW, row, (size_t)DW * 2);
        }
    }

    if (s_life.mode == MODE_ANT) {
        uint16_t *p = s_life.big + (size_t)s_life.ant_y * SCALE * DW +
                      (size_t)s_life.ant_x * SCALE;
        for (int k = 0; k < SCALE; k++) {
            for (int j = 0; j < SCALE; j++) {
                p[(size_t)k * DW + j] = s_ant_color;
            }
        }
    }
}

/* Rewriting the label invalidates its box: 368x20 of background plus a score
 * of glyphs, in an area the canvas does NOT cover, that is, a separate invalid
 * area and a whole text draw. Measured on the board, doing it on every frame
 * is ~13 ms of the 47 the frame used to cost. Three times a second it reads
 * just as well and stops showing up in the budget. */
static void refresh_info(bool force)
{
    uint32_t now = lv_tick_get();
    if (!force && (uint32_t)(now - s_life.last_info_ms) < 350u) {
        return;
    }
    s_life.last_info_ms = now;

    char buf[48];
    snprintf(buf, sizeof(buf), _("gen %u   celdas %u"),
             (unsigned)s_life.gen, (unsigned)s_life.pop);
    lv_label_set_text(s_life.lbl_info, buf);
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_life.running) {
        if (s_life.mode == MODE_ANT) {
            step_ant();
        } else {
            step_life();
        }
    }
    expand();
    lv_obj_invalidate(s_life.canvas);
    refresh_info(false);
}

/* -------------------------------------------------------------------------- */
/* Controls                                                                    */

/* The screen stays lit only while the simulation is running: with it stopped
 * there is nothing to look at and the battery is grateful. 'self' is the copy
 * the runtime keeps in its table, so touching its flags here is valid. */
static void apply_keep_awake(void)
{
    if (!s_life.self) {
        return;
    }
    if (s_life.running) {
        s_life.self->desc.flags |= AOS_APP_FLAG_KEEP_AWAKE;
    } else {
        s_life.self->desc.flags &= ~(uint32_t)AOS_APP_FLAG_KEEP_AWAKE;
    }
}

static void set_running(bool on)
{
    s_life.running = on;
    lv_label_set_text(s_life.lbl_play, on ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    apply_keep_awake();
}

static void play_cb(lv_event_t *event)
{
    (void)event;
    set_running(!s_life.running);
}

static void step_cb(lv_event_t *event)
{
    (void)event;
    set_running(false);
    if (s_life.mode == MODE_ANT) {
        step_ant();
    } else {
        step_life();
    }
    expand();
    lv_obj_invalidate(s_life.canvas);
    refresh_info(true);         /* one step at a time, the number is what you watch */
}

static void speed_cb(lv_event_t *event)
{
    (void)event;
    s_life.speed = (s_life.speed + 1) % 3;
    lv_label_set_text(s_life.lbl_speed, SPEED_NAME[s_life.speed]);
    lv_timer_set_period(s_life.timer, PERIOD_MS[s_life.speed]);
    aos_hal_pref_set_i32("life_speed", s_life.speed);
}

static void mode_cb(lv_event_t *event)
{
    (void)event;
    s_life.mode = (s_life.mode == MODE_LIFE) ? MODE_ANT : MODE_LIFE;
    lv_label_set_text(s_life.lbl_mode,
                      s_life.mode == MODE_ANT ? _("ANT") : _("VIDA"));
    aos_hal_pref_set_i32("life_mode", (int32_t)s_life.mode);
    reseed();
    set_running(true);
}

static void seed_cb(lv_event_t *event)
{
    (void)event;
    s_life.seed_kind++;
    reseed();
    set_running(true);
    aos_hal_beep(1300, 20);
}

/* A touch on the grid: in Life it drops a handful of live cells around the
 * finger, in the ant it moves it there. It is a tap and not a drag on purpose:
 * painting by dragging would force keeping the gestures (NO_SWIPE) and then
 * another way out would have to be invented. */
static void canvas_cb(lv_event_t *event)
{
    (void)event;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t area;
    lv_obj_get_coords(s_life.canvas, &area);
    int gx = (p.x - area.x1) / SCALE;
    int gy = (p.y - area.y1) / SCALE;
    if (gx < 0 || gx >= W || gy < 0 || gy >= H) {
        return;
    }

    if (s_life.mode == MODE_ANT) {
        s_life.ant_x = gx;
        s_life.ant_y = gy;
    } else {
        for (int i = 0; i < 14; i++) {
            put(gx - 2 + (int)(rnd() % 5), gy - 2 + (int)(rnd() % 5), 1);
        }
    }
    expand();
    lv_obj_invalidate(s_life.canvas);
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *bar_button(lv_obj_t *parent, const char *text, int slot,
                            lv_color_t color, lv_event_cb_t cb, lv_obj_t **out_label)
{
    /* Five buttons of 58 with 11 of spacing is 334, centred that leaves 17 of
     * margin. That margin is needed: the watch's glass has rounded corners
     * with a 38 px radius and at this height they are already eating the first
     * few pixels on each side. */
    const int32_t w = 58, gap = 11;
    const int32_t x0 = (AOS_SCREEN_W - (5 * w + 4 * gap)) / 2;

    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, 44);
    lv_obj_set_pos(btn, x0 + slot * (w + gap), AOS_TOUCH_Y_MIN);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = aos_label(btn, text, aos_font_small, AOS_C_TEXT);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label);
    if (out_label) {
        *out_label = label;
    }
    return btn;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    memset(&s_life, 0, sizeof(s_life));
    s_life.self = self;
    s_life.rng  = (uint32_t)aos_hal_uptime_ms() | 1u;
    s_life.last_info_ms = lv_tick_get();
    palettes_init();

    int32_t v = 0;
    if (aos_hal_pref_get_i32("life_speed", &v) && v >= 0 && v < 3) {
        s_life.speed = (int)v;
    } else {
        s_life.speed = 1;
    }
    if (aos_hal_pref_get_i32("life_mode", &v) && (v == MODE_LIFE || v == MODE_ANT)) {
        s_life.mode = (mode_t_)v;
    }

    /* The large ones through malloc, which with CONFIG_SPIRAM_USE_MALLOC go to
     * PSRAM. */
    s_life.big  = malloc((size_t)DW * DH * 2);
    s_life.cell = malloc((size_t)W * H);
    s_life.next = malloc((size_t)W * H);
    if (!s_life.big || !s_life.cell || !s_life.next) {
        free(s_life.big);
        free(s_life.cell);
        free(s_life.next);
        memset(&s_life, 0, sizeof(s_life));
        aos_ui_toast(_("Sin memoria"), 2000);
        return NULL;
    }
    memset(s_life.cell, 0, (size_t)W * H);

    lv_obj_t *page = aos_page(root);

    s_life.canvas = lv_canvas_create(page);
    lv_canvas_set_buffer(s_life.canvas, s_life.big, DW, DH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_life.canvas, DW, DH);
    lv_obj_set_pos(s_life.canvas, CANVAS_X, CANVAS_Y);
    /* The theme gives everything a radius: on a canvas that forces clipping
     * with a mask and drawing in layers. */
    lv_obj_set_style_radius(s_life.canvas, 0, 0);
    lv_image_set_antialias(s_life.canvas, false);
    lv_obj_add_flag(s_life.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_life.canvas, canvas_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, AOS_SCREEN_W, BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    /* Hangs off 'page' and not off the bar: it goes right at the bottom, in
     * the strip the touch panel cannot reach. */
    s_life.lbl_info = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                      AOS_SCREEN_W, 20);
    lv_obj_set_pos(s_life.lbl_info, 0, INFO_Y + 2);

    bar_button(bar, LV_SYMBOL_PAUSE, 0, AOS_C_GREEN, play_cb, &s_life.lbl_play);
    bar_button(bar, LV_SYMBOL_NEXT,  1, AOS_C_CARD2, step_cb, NULL);
    bar_button(bar, SPEED_NAME[s_life.speed], 2, AOS_C_CARD2, speed_cb,
               &s_life.lbl_speed);
    bar_button(bar, s_life.mode == MODE_ANT ? _("ANT") : _("VIDA"), 3, AOS_C_CARD2,
               mode_cb, &s_life.lbl_mode);
    bar_button(bar, LV_SYMBOL_REFRESH, 4, AOS_C_ORANGE, seed_cb, NULL);

    reseed();
    set_running(true);
    expand();
    refresh_info(true);

    s_life.timer = lv_timer_create(frame_cb, PERIOD_MS[s_life.speed], NULL);
    return &s_life;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_life.timer) {
        lv_timer_delete(s_life.timer);
        s_life.timer = NULL;
    }
    /* The objects first, with the context still standing: the canvas points at
     * a buffer we are about to free, and deleting it after the free() would be
     * asking LVGL to draw returned memory. */
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    free(s_life.big);
    free(s_life.cell);
    free(s_life.next);
    memset(&s_life, 0, sizeof(s_life));
}

void aos_app_life_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.life",
            .name     = "Vida",
            .icon_vec = AOS_ICON_LIFE,
            .color_a  = 0x30D158,
            .color_b  = 0x0E4622,
            .flags    = AOS_APP_FLAG_FULLSCREEN,
            .order    = 152,
        },
        .create  = create,
        .destroy = destroy,
    };
}
