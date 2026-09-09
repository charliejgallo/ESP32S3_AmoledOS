/*
 * GEMAS
 *
 * A match-three game for AmoledOS, of the Bejeweled family: two neighbouring
 * jewels are swapped and anything left in a line of three or more breaks, what
 * is above falls and sometimes that makes another line by itself. Lining up
 * four or more leaves a special jewel.
 *
 * It builds two ways from the same source:
 *
 *   .so for the board            cd apps/gemas && idf.py -G 'Unix Makefiles' set-target esp32s3 && idf.py so
 *   built-in app of the simulator   the simulator builds it (AOS_SIM_BUILTIN)
 *
 * How it is put together inside:
 *
 *   gm_art    draws the jewels in code, once, into RGB565A8 sprites
 *   gm_board  the rules: lines, specials, falling, shuffling
 *   gm_fx     sparks, ripples, beams and panels
 *   gm_snd    the melodies, which come out of the single tone there is
 *
 * And what is left here is what joins them: one view per cell (an LVGL image
 * that moves, scales and rotates) and a state machine carrying the swap, the
 * break, the fall and the cascade along.
 *
 * There is no canvas: each jewel is an LVGL object, so only what moves is
 * repainted. In a game where half the screen is still most of the time, that
 * is far cheaper than redrawing everything every frame.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"
#include "aos_theme.h"

#include "gm_art.h"
#include "gm_board.h"
#include "gm_fx.h"
#include "gm_snd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Measurements
 * -------------------------------------------------------------------------- */

#define FRAME_MS        33              /* 30 frames per second */
#define BOARD_PX        (GM_N * GM_CELL)        /* 352 */
#define BOARD_X         ((AOS_SCREEN_W - BOARD_PX) / 2)  /* 8 */
#define BOARD_Y         80
#define FX16            16              /* positions in 1/16 of a pixel */

#define TIME_MAX        10000           /* the time bar, in thousandths */
#define COMBO_MAX       9

#define KEY_HI_RELAX    "gem_hi_relax"
#define KEY_HI_TIME     "gem_hi_time"
#define KEY_MODE        "gem_mode"
#define KEY_DIFF        "gem_diff"
#define KEY_SFX         "gem_sfx"

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

typedef enum {
    ST_MENU = 0,
    ST_FALL,        /* something is falling (the initial deal too) */
    ST_IDLE,        /* the board is waiting for the player */
    ST_SWAP,        /* two jewels changing places */
    ST_UNSWAP,      /* ... and coming back, because they formed nothing */
    ST_POP,         /* the jewels breaking */
    ST_SHUFFLE,     /* no moves left: it shuffles */
    ST_OVER,
    ST_PAUSE,
} state_t;

/* How a jewel breaks. It changes the animation, the sound and the sparks. */
typedef enum {
    POP_NORMAL = 0, /* line of three: it grows and fades      */
    POP_SUCK,       /* line of four or more: it goes to the centre */
    POP_BURN,       /* a flame caught it                      */
    POP_BEAM,       /* a star caught it                       */
    POP_WIPE,       /* the hypercube took it                  */
} pop_kind_t;

typedef struct {
    lv_obj_t *img;              /* the jewel */
    lv_obj_t *badge;            /* flame or star, on top */
    int16_t   x, y;             /* current position in 1/16 px, inside the board */
    int16_t   vy;               /* fall, 1/16 px per frame */
    int16_t   sx, sy;           /* origin of the swap, in 1/16 px */
    uint8_t   pop, pop0;        /* frames of breaking: 0 = whole */
    uint8_t   delay;            /* wait before it starts breaking */
    uint8_t   kind;             /* pop_kind_t */
    uint8_t   squash;           /* frames of bounce on landing */
    uint8_t   birth;            /* frames of a special jewel appearing */
    int8_t    pr, pc;           /* where it goes if a special swallows it */
    int8_t    type;
    uint8_t   special;
    bool      alive;
} cell_view_t;

typedef struct {
    const char *name;
    uint8_t  colors;
    uint16_t drain;         /* thousandths of bar per frame, level 1 */
    uint16_t drain_step;    /* how much it goes up per level          */
    uint16_t gain;          /* thousandths each broken jewel gives back */
} diff_t;

/* The balance of the time-attack mode comes from a single sum: the bar is
 * 10,000 thousandths, the drain is per frame (30 a second) and each broken
 * jewel gives back 'gain'. In NORMAL level 1 that is 180 thousandths a second,
 * that is, 55 seconds of a full bar, and a line of three gives back some 4
 * seconds: time to think, but not to get distracted. */
static const diff_t s_diffs[3] = {
    { N_("FACIL"),    6,  4, 1, 300 },
    { N_("NORMAL"),   7,  6, 2, 240 },
    { N_("DIFICIL"),  7,  9, 3, 180 },
};

typedef struct {
    /* --- game --- */
    gm_board_t  b;
    cell_view_t v[GM_N][GM_N];
    state_t     state;
    state_t     resume;         /* where to go back to from the pause */
    int         phase;          /* frames the phase has left */
    int         phase0;
    int         combo;
    uint32_t    score;
    uint32_t    hiscore;
    int         level;
    int         level_pts;      /* points accumulated towards the level */
    int         level_need;
    int         timebar;        /* 0..TIME_MAX, time-attack mode only */
    int         mode;           /* 0 relaxed, 1 time attack */
    int         diff;
    int         idle_frames;    /* for the hint */
    int         hint[4];
    bool        hint_on;
    int         tick_warn;      /* so the time warning is not repeated */

    /* the cell the finger moved: that is where the special jewel appears */
    uint8_t     mark[GM_N][GM_N];   /* what is breaking right now */
    int8_t      swap_r1, swap_c1, swap_r2, swap_c2;
    int8_t      sel_r, sel_c;   /* current selection, -1 if none */

    /* --- view --- */
    gm_art_t    art;
    gm_fx_t     fx;
    lv_obj_t   *root;
    lv_obj_t   *board;          /* the board's container, it shakes */
    lv_obj_t   *bg;
    lv_obj_t   *sel_ring;
    lv_obj_t   *touch;
    lv_timer_t *timer;

    /* HUD */
    lv_obj_t   *hud;
    lv_obj_t   *lbl_score;
    lv_obj_t   *lbl_level;
    lv_obj_t   *bar_level;
    lv_obj_t   *bar_level_fill;
    lv_obj_t   *bar_time;
    lv_obj_t   *bar_time_fill;
    lv_obj_t   *btn_pause;

    /* panels */
    lv_obj_t   *menu;
    lv_obj_t   *chip_mode;
    lv_obj_t   *chip_diff;
    lv_obj_t   *chip_sfx;
    lv_obj_t   *lbl_best;
    lv_obj_t   *pause;
    lv_obj_t   *chip_sfx2;
    lv_obj_t   *over;
    lv_obj_t   *lbl_over_title;
    lv_obj_t   *lbl_over_score;
    lv_obj_t   *lbl_over_info;

    /* input */
    bool        pressing;
    int16_t     press_x, press_y;
    int8_t      press_r, press_c;
    bool        drag_done;
    bool        want_exit;
#ifdef AOS_SIM_BUILTIN
    int8_t      test_mv[4];     /* move served up by GEMAS_TEST, -1 = none */
#endif
} app_t;

/* --------------------------------------------------------------------------
 * Utilities
 * -------------------------------------------------------------------------- */

static inline int cell_home_x(int c) { return c * GM_CELL * FX16; }
static inline int cell_home_y(int r) { return r * GM_CELL * FX16; }

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Points with a thousands separator: "12.480". It reads far better at a
 * glance, which is all you look at while playing. */
static void format_score(char *out, size_t len, uint32_t value)
{
    if (value < 1000) {
        snprintf(out, len, "%u", (unsigned)value);
    } else if (value < 1000000) {
        snprintf(out, len, "%u.%03u", (unsigned)(value / 1000),
                 (unsigned)(value % 1000));
    } else {
        snprintf(out, len, "%u.%03u.%03u", (unsigned)(value / 1000000),
                 (unsigned)((value / 1000) % 1000), (unsigned)(value % 1000));
    }
}

/* --------------------------------------------------------------------------
 * A cell's view
 *
 * Each jewel is a 44x44 LVGL image with its sprite. The specials also carry a
 * badge (the flame or the star), which is another sibling image and not a
 * child: scaling an object with children forces LVGL to draw the whole subtree
 * in a separate layer, whereas scaling a lone image is a multiplication inside
 * the same blit. With twenty jewels breaking at once the difference shows.
 * -------------------------------------------------------------------------- */

static lv_obj_t *make_image(app_t *a)
{
    lv_obj_t *img = lv_image_create(a->board);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(img, GM_SPRITE, GM_SPRITE);
    lv_image_set_pivot(img, GM_SPRITE / 2, GM_SPRITE / 2);
    lv_image_set_antialias(img, true);
    return img;
}

static void view_badge(app_t *a, cell_view_t *v)
{
    const lv_image_dsc_t *src = NULL;
    if (v->special == GM_SP_FLAME) {
        src = &a->art.flame.dsc;
    } else if (v->special == GM_SP_STAR) {
        src = &a->art.star.dsc;
    }

    if (!src) {
        if (v->badge) {
            lv_obj_add_flag(v->badge, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (!v->badge) {
        v->badge = make_image(a);
    }
    lv_image_set_src(v->badge, src);
    lv_obj_remove_flag(v->badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(v->badge);
}

static void view_sync(app_t *a, int r, int c)
{
    cell_view_t *v = &a->v[r][c];
    v->type    = a->b.c[r][c].type;
    v->special = a->b.c[r][c].special;

    const lv_image_dsc_t *src = (v->special == GM_SP_HYPER)
                              ? &a->art.hyper.dsc
                              : &a->art.gem[clampi(v->type, 0, GM_TYPES - 1)].dsc;
    lv_image_set_src(v->img, src);
    lv_obj_remove_flag(v->img, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_scale(v->img, 256);
    lv_image_set_rotation(v->img, 0);
    lv_obj_set_style_image_opa(v->img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor_opa(v->img, LV_OPA_TRANSP, 0);
    v->alive = true;
    v->pop = 0;
    view_badge(a, v);
}

static void view_place(cell_view_t *v)
{
    int px = v->x / FX16;
    int py = v->y / FX16;
    lv_obj_set_pos(v->img, px, py);
    if (v->badge && !lv_obj_has_flag(v->badge, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_pos(v->badge, px, py);
    }
}

static void view_scale(cell_view_t *v, int sx, int sy)
{
    lv_image_set_scale_x(v->img, sx);
    lv_image_set_scale_y(v->img, sy);
    if (v->badge && !lv_obj_has_flag(v->badge, LV_OBJ_FLAG_HIDDEN)) {
        lv_image_set_scale_x(v->badge, sx);
        lv_image_set_scale_y(v->badge, sy);
    }
}

static void view_hide(cell_view_t *v)
{
    lv_obj_add_flag(v->img, LV_OBJ_FLAG_HIDDEN);
    if (v->badge) {
        lv_obj_add_flag(v->badge, LV_OBJ_FLAG_HIDDEN);
    }
    v->alive = false;
}

/* Centre of a cell in screen coordinates, which is what the sparks and the
 * panels want (they live outside the board's container). */
static void cell_center(int r, int c, int *x, int *y)
{
    *x = BOARD_X + c * GM_CELL + GM_CELL / 2;
    *y = BOARD_Y + r * GM_CELL + GM_CELL / 2;
}


/* --------------------------------------------------------------------------
 * Score, levels and time
 * -------------------------------------------------------------------------- */

static void hud_refresh(app_t *a)
{
    char buf[24];

    format_score(buf, sizeof(buf), a->score);
    lv_label_set_text(a->lbl_score, buf);

    snprintf(buf, sizeof(buf), "NIVEL %d", a->level);
    lv_label_set_text(a->lbl_level, buf);

    int w = a->level_need ? (BOARD_PX * a->level_pts / a->level_need) : 0;
    lv_obj_set_width(a->bar_level_fill, clampi(w, 0, BOARD_PX));

    if (a->mode == 1) {
        int tw = BOARD_PX * a->timebar / TIME_MAX;
        lv_obj_set_width(a->bar_time_fill, clampi(tw, 0, BOARD_PX));
        /* from green to red as it runs out */
        uint32_t col = (a->timebar > TIME_MAX / 2) ? 0x30D158
                     : (a->timebar > TIME_MAX / 5) ? 0xFFD60A : 0xFF453A;
        lv_obj_set_style_bg_color(a->bar_time_fill, lv_color_hex(col), 0);
    }
}

static void level_up(app_t *a)
{
    a->level++;
    a->level_pts -= a->level_need;
    a->level_need = 900 + a->level * 550;
    a->timebar = TIME_MAX;
    a->tick_warn = 0;

    gm_snd_play(GM_SFX_LEVELUP, 0);
    gm_fx_flash(&a->fx, 90, 8);

    char buf[24];
    snprintf(buf, sizeof(buf), "NIVEL %d", a->level);
    gm_fx_text(&a->fx, AOS_SCREEN_W / 2, BOARD_Y + BOARD_PX / 2 - 30, buf,
               0xFFD60A, true);
}

static void add_score(app_t *a, int pts)
{
    if (pts <= 0) {
        return;
    }
    a->score += (uint32_t)pts;
    if (a->score > a->hiscore) {
        a->hiscore = a->score;
    }
    a->level_pts += pts;
    while (a->level_pts >= a->level_need) {
        level_up(a);
    }
}

/* --------------------------------------------------------------------------
 * Breaking the jewels
 * -------------------------------------------------------------------------- */

static const uint8_t s_pop_frames[] = {
    [POP_NORMAL] = 9,
    [POP_SUCK]   = 13,
    [POP_BURN]   = 12,
    [POP_BEAM]   = 10,
    [POP_WIPE]   = 14,
};

static void set_pop(app_t *a, int r, int c, int kind, int delay, int pr, int pc)
{
    cell_view_t *v = &a->v[r][c];
    if (!v->alive) {
        return;
    }
    v->kind  = (uint8_t)kind;
    v->pop0  = s_pop_frames[kind];
    v->pop   = v->pop0;
    v->delay = (uint8_t)delay;
    v->pr    = (int8_t)pr;
    v->pc    = (int8_t)pc;
    v->vy    = 0;
}

static int pop_total(const app_t *a)
{
    int worst = 0;
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            const cell_view_t *v = &a->v[r][c];
            if (v->pop) {
                int total = v->delay + v->pop;
                if (total > worst) {
                    worst = total;
                }
            }
        }
    }
    return worst;
}

/* Effects of a special detonating. It is done here and not in gm_board because
 * it is pure decoration: the rules have already decided which cells go. */
static void special_effects(app_t *a, int r, int c, int special)
{
    int x, y;
    cell_center(r, c, &x, &y);

    if (special == GM_SP_FLAME) {
        gm_fx_ring(&a->fx, x, y, 0xFF8A14, 12, 74, 12, 6);
        gm_fx_burst(&a->fx, x, y, 0xFF6A00, 10, 46, false);
        gm_fx_burst(&a->fx, x, y, 0xFFD54A, 6, 30, false);
        gm_fx_shake(&a->fx, 8);
        gm_snd_play(GM_SFX_FLAME, 0);
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                int nr = r + dr, nc = c + dc;
                if (nr >= 0 && nr < GM_N && nc >= 0 && nc < GM_N &&
                    a->mark[nr][nc]) {
                    set_pop(a, nr, nc, POP_BURN, 0, r, c);
                }
            }
        }
    } else if (special == GM_SP_STAR) {
        gm_fx_beam(&a->fx, x, y, true, 0x5AD8FF, 9);
        gm_fx_beam(&a->fx, x, y, false, 0x5AD8FF, 9);
        gm_fx_burst(&a->fx, x, y, 0xFFFFFF, 8, 52, false);
        gm_fx_shake(&a->fx, 5);
        gm_snd_play(GM_SFX_STAR, 0);
        for (int i = 0; i < GM_N; i++) {
            if (a->mark[r][i]) {
                set_pop(a, r, i, POP_BEAM, (i > c ? i - c : c - i), r, c);
            }
            if (a->mark[i][c]) {
                set_pop(a, i, c, POP_BEAM, (i > r ? i - r : r - i), r, c);
            }
        }
    } else if (special == GM_SP_HYPER) {
        gm_fx_ring(&a->fx, x, y, 0xFFFFFF, 10, 200, 16, 8);
        gm_fx_flash(&a->fx, 70, 7);
        gm_fx_shake(&a->fx, 10);
        gm_snd_play(GM_SFX_HYPER, 0);
        for (int rr = 0; rr < GM_N; rr++) {
            for (int cc = 0; cc < GM_N; cc++) {
                if (!a->mark[rr][cc]) {
                    continue;
                }
                int dr = rr > r ? rr - r : r - rr;
                int dc = cc > c ? cc - c : c - cc;
                set_pop(a, rr, cc, POP_WIPE, (dr + dc), r, c);
            }
        }
    }
}

/* Starts the breaking of everything marked and hands out the points. */
static void begin_pop(app_t *a, int group_cells, int group_points,
                      int specials_made)
{
    int gems = gm_mark_count(a->mark);

    /* the ones the explosions took score too, more weakly */
    int pts = group_points + 40 * (gems - group_cells);
    int mult = clampi(a->combo, 1, COMBO_MAX);
    pts = pts * mult + specials_made;

    add_score(a, pts);

    if (a->mode == 1) {
        a->timebar = clampi(a->timebar + gems * s_diffs[a->diff].gain,
                            0, TIME_MAX);
    }

    a->state  = ST_POP;
    a->phase0 = pop_total(a) + 2;
    a->phase  = a->phase0;
}

/* Looks for completed lines and starts the breaking phase. If there are none,
 * the cascade is over. Returns true if something breaks. */
static bool resolve_step(app_t *a)
{
    gm_group_t g[GM_MAX_GROUPS];
    int ng = gm_find_groups(&a->b, g, GM_MAX_GROUPS, a->swap_r2, a->swap_c2);
    if (ng == 0) {
        return false;
    }

    a->combo++;
    memset(a->mark, 0, sizeof(a->mark));

    int group_cells = 0, group_points = 0, specials_made = 0;

    for (int i = 0; i < ng; i++) {
        for (int k = 0; k < g[i].n; k++) {
            a->mark[g[i].cell[k] / GM_N][g[i].cell[k] % GM_N] = 1;
        }
        group_cells += g[i].n;
        group_points += 40 * g[i].n + 30 * (g[i].n - 3);
    }

    /* The specials that were in the line detonate and take more cells with
     * them. Mind the order: expand first and place the new jewels afterwards,
     * or the newly created one would explode on the spot. */
    gm_expand_specials(&a->b, a->mark);

    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            if (a->mark[r][c]) {
                set_pop(a, r, c, POP_NORMAL, 0, r, c);
            }
        }
    }

    /* lines of four or more are pulled towards where the special will be born */
    for (int i = 0; i < ng; i++) {
        if (g[i].special == GM_SP_NONE) {
            continue;
        }
        for (int k = 0; k < g[i].n; k++) {
            int r = g[i].cell[k] / GM_N, c = g[i].cell[k] % GM_N;
            if (a->mark[r][c]) {
                set_pop(a, r, c, POP_SUCK, 0, g[i].pr, g[i].pc);
            }
        }
    }

    /* explosions: they override the break type of whatever they reach */
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            if (a->mark[r][c] && a->b.c[r][c].special != GM_SP_NONE) {
                special_effects(a, r, c, a->b.c[r][c].special);
            }
        }
    }

    /* now, at last, the new jewels stay in their cell */
    for (int i = 0; i < ng; i++) {
        int x, y;
        cell_center(g[i].pr, g[i].pc, &x, &y);

        if (g[i].special != GM_SP_NONE) {
            a->b.c[g[i].pr][g[i].pc].type    = g[i].type;
            a->b.c[g[i].pr][g[i].pc].special = g[i].special;
            a->mark[g[i].pr][g[i].pc] = 0;
            a->v[g[i].pr][g[i].pc].pop = 0;

            specials_made += (g[i].special == GM_SP_FLAME) ? 150
                           : (g[i].special == GM_SP_STAR)  ? 300 : 500;

            gm_fx_ring(&a->fx, x, y, gm_art_color_light(g[i].type), 8, 46, 10, 4);
            gm_snd_play(g[i].special == GM_SP_FLAME ? GM_SFX_MATCH4
                                                    : GM_SFX_MATCH5, 0);
        } else {
            gm_snd_play(GM_SFX_MATCH, a->combo);
        }

        char buf[16];
        int mult = clampi(a->combo, 1, COMBO_MAX);
        snprintf(buf, sizeof(buf), "+%d", (40 * g[i].n + 30 * (g[i].n - 3)) * mult);
        gm_fx_text(&a->fx, x, y - 10, buf, gm_art_color_light(g[i].type), false);

        for (int k = 0; k < g[i].n; k++) {
            int cx, cy;
            cell_center(g[i].cell[k] / GM_N, g[i].cell[k] % GM_N, &cx, &cy);
            gm_fx_burst(&a->fx, cx, cy, gm_art_color_light(g[i].type),
                        g[i].special != GM_SP_NONE ? 3 : 5, 34, true);
        }
    }

    if (a->combo >= 2) {
        char buf[20];
        snprintf(buf, sizeof(buf), "CADENA x%d", clampi(a->combo, 1, COMBO_MAX));
        gm_fx_text(&a->fx, AOS_SCREEN_W / 2, BOARD_Y + 40, buf, 0xFFD60A, true);
        gm_fx_shake(&a->fx, 4);
    }

    begin_pop(a, group_cells, group_points, specials_made);
    return true;
}

/* The hypercube forms no lines: it is activated by swapping it with a jewel,
 * and it takes every jewel of that colour. Two hypercubes together clear the
 * board. */
static void activate_hyper(app_t *a, int hr, int hc, int tr, int tc)
{
    a->combo++;
    memset(a->mark, 0, sizeof(a->mark));

    /* the hypercube itself is taken out of the equation before expanding:
     * otherwise gm_expand_specials would detonate it again and it would take
     * its own colour too */
    a->b.c[hr][hc].special = GM_SP_NONE;

    if (a->b.c[tr][tc].special == GM_SP_HYPER) {
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                a->mark[r][c] = 1;
            }
        }
        gm_fx_flash(&a->fx, 140, 10);
    } else {
        gm_mark_color(&a->b, a->mark, a->b.c[tr][tc].type);
    }
    a->mark[hr][hc] = 1;

    gm_expand_specials(&a->b, a->mark);
    int gems = gm_mark_count(a->mark);

    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            if (a->mark[r][c]) {
                int dr = r > hr ? r - hr : hr - r;
                int dc = c > hc ? c - hc : hc - c;
                set_pop(a, r, c, POP_WIPE, dr + dc, hr, hc);
            }
        }
    }

    int x, y;
    cell_center(hr, hc, &x, &y);
    gm_fx_ring(&a->fx, x, y, 0xFFFFFF, 10, 230, 18, 9);
    gm_fx_burst(&a->fx, x, y, 0xFFFFFF, 12, 60, false);
    gm_fx_shake(&a->fx, 10);
    gm_snd_play(GM_SFX_HYPER, 0);

    begin_pop(a, gems, 55 * gems, 0);
}

/* --------------------------------------------------------------------------
 * The fall
 *
 * gm_collapse leaves the board already resolved and says, for each cell, which
 * row whatever is now there came from. With that, the views travel with their
 * LVGL object (so the jewel coming down is the same one that was above, and
 * not a new object with the same drawing) and the new jewels recycle the
 * objects of those that broke.
 * -------------------------------------------------------------------------- */

static void do_collapse(app_t *a)
{
    gm_fall_t fall;
    gm_collapse(&a->b, a->mark, &fall);

    for (int c = 0; c < GM_N; c++) {
        cell_view_t old[GM_N];
        bool used[GM_N];
        for (int r = 0; r < GM_N; r++) {
            old[r] = a->v[r][c];
            used[r] = false;
        }

        for (int r = 0; r < GM_N; r++) {
            int from = fall.from[r][c];
            if (from >= 0) {
                a->v[r][c] = old[from];
                used[from] = true;
            }
        }

        int spare = 0;
        for (int r = 0; r < GM_N; r++) {
            if (fall.from[r][c] >= 0) {
                continue;
            }
            while (spare < GM_N && used[spare]) {
                spare++;
            }
            if (spare >= GM_N) {
                break;      /* cannot happen: as many broke as are born */
            }
            used[spare] = true;

            cell_view_t *v = &a->v[r][c];
            *v = old[spare];
            v->x  = (int16_t)cell_home_x(c);
            v->y  = (int16_t)(-(fall.born[r][c] + 1) * GM_CELL * FX16 / 2);
            v->vy = 0;
        }

        for (int r = 0; r < GM_N; r++) {
            cell_view_t *v = &a->v[r][c];
            v->pop = 0;
            v->delay = 0;
            v->squash = 0;
            v->x = (int16_t)cell_home_x(c);

            bool nueva = (v->type != a->b.c[r][c].type ||
                          v->special != a->b.c[r][c].special ||
                          !v->alive);
            if (nueva) {
                view_sync(a, r, c);
                if (a->b.c[r][c].special != GM_SP_NONE && v->y >= 0) {
                    v->birth = 10;      /* the freshly created special bounces */
                }
            }
            view_scale(v, 256, 256);
            lv_image_set_rotation(v->img, 0);
            lv_obj_set_style_image_recolor_opa(v->img, LV_OPA_TRANSP, 0);
            view_place(v);
        }
    }

#ifdef AOS_SIM_BUILTIN
    if (getenv("GEMAS_TRACE")) {
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                if (a->b.c[r][c].special != GM_SP_NONE) {
                    printf("[gemas] especial %d en %d,%d (vista badge=%p vis=%d)\n",
                           a->b.c[r][c].special, r, c, (void *)a->v[r][c].badge,
                           a->v[r][c].badge ? !lv_obj_has_flag(a->v[r][c].badge, LV_OBJ_FLAG_HIDDEN) : -1);
                }
            }
        }
    }
#endif
    a->state = ST_FALL;
}

/* The cascade is over: back to waiting for the player. */
static void cascade_end(app_t *a)
{
    a->combo = 0;
    a->swap_r2 = a->swap_c2 = -1;

    if (!gm_has_move(&a->b)) {
        a->state = ST_SHUFFLE;
        a->phase0 = 26;
        a->phase  = a->phase0;
        gm_snd_play(GM_SFX_SHUFFLE, 0);
        gm_fx_text(&a->fx, AOS_SCREEN_W / 2, BOARD_Y + BOARD_PX / 2 - 20,
                   _("SIN JUGADAS"), 0xFFD60A, true);
        return;
    }

    a->state = ST_IDLE;
    a->idle_frames = 0;
}

/* --------------------------------------------------------------------------
 * One frame of each phase
 * -------------------------------------------------------------------------- */

static void try_swap(app_t *a, int r1, int c1, int r2, int c2);

/* easing: 0..256 -> 0..256 with a start and a stop */
static int ease(int t)
{
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return (t * t * (768 - 2 * t)) / (256 * 256);
}

static void step_swap(app_t *a)
{
    cell_view_t *v1 = &a->v[a->swap_r1][a->swap_c1];
    cell_view_t *v2 = &a->v[a->swap_r2][a->swap_c2];

    a->phase--;
    int t = ease((a->phase0 - a->phase) * 256 / a->phase0);

    int h1x = cell_home_x(a->swap_c1), h1y = cell_home_y(a->swap_r1);
    int h2x = cell_home_x(a->swap_c2), h2y = cell_home_y(a->swap_r2);

    v1->x = (int16_t)(h1x + (h2x - h1x) * t / 256);
    v1->y = (int16_t)(h1y + (h2y - h1y) * t / 256);
    v2->x = (int16_t)(h2x + (h1x - h2x) * t / 256);
    v2->y = (int16_t)(h2y + (h1y - h2y) * t / 256);

    /* the one travelling forwards passes over the top and grows a little */
    int bump = 256 + (t < 128 ? t : 256 - t) / 4;
    view_scale(v1, bump, bump);
    view_place(v1);
    view_place(v2);

    if (a->phase > 0) {
        return;
    }

    view_scale(v1, 256, 256);

    /* the swap only takes effect now, both on the board and in the views: that
     * way each jewel goes on being the same LVGL object */
    gm_cell_t tmp = a->b.c[a->swap_r1][a->swap_c1];
    a->b.c[a->swap_r1][a->swap_c1] = a->b.c[a->swap_r2][a->swap_c2];
    a->b.c[a->swap_r2][a->swap_c2] = tmp;

    cell_view_t vtmp = *v1;
    *v1 = *v2;
    *v2 = vtmp;
    v1->x = (int16_t)h1x; v1->y = (int16_t)h1y;
    v2->x = (int16_t)h2x; v2->y = (int16_t)h2y;
    view_place(v1);
    view_place(v2);

    if (a->state == ST_UNSWAP) {
        a->state = ST_IDLE;
        a->idle_frames = 0;
        return;
    }

    /* the hypercube is activated by swapping it, not by lining it up */
    if (a->b.c[a->swap_r2][a->swap_c2].special == GM_SP_HYPER) {
        activate_hyper(a, a->swap_r2, a->swap_c2, a->swap_r1, a->swap_c1);
        return;
    }
    if (a->b.c[a->swap_r1][a->swap_c1].special == GM_SP_HYPER) {
        activate_hyper(a, a->swap_r1, a->swap_c1, a->swap_r2, a->swap_c2);
        return;
    }

    if (!resolve_step(a)) {
        /* it formed nothing: it comes back by itself and sounds like a no */
        int8_t r = a->swap_r1, c = a->swap_c1;
        a->swap_r1 = a->swap_r2; a->swap_c1 = a->swap_c2;
        a->swap_r2 = r;          a->swap_c2 = c;
        a->state  = ST_UNSWAP;
        a->phase0 = 7;
        a->phase  = a->phase0;
        gm_snd_play(GM_SFX_DENY, 0);
    }
}

static void step_pop(app_t *a)
{
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            cell_view_t *v = &a->v[r][c];
            if (v->pop == 0) {
                continue;
            }
            if (v->delay > 0) {
                v->delay--;
                continue;
            }

            v->pop--;
            int p = v->pop * 256 / v->pop0;      /* 256 at the start, 0 at the end */

            switch (v->kind) {
            case POP_SUCK: {
                int tx = cell_home_x(v->pc), ty = cell_home_y(v->pr);
                v->x = (int16_t)(v->x + (tx - v->x) * 5 / 16);
                v->y = (int16_t)(v->y + (ty - v->y) * 5 / 16);
                int s = 40 + p * 216 / 256;
                view_scale(v, s, s);
                break;
            }
            case POP_BURN:
                lv_obj_set_style_image_recolor(v->img, lv_color_hex(0xFFE8B0), 0);
                lv_obj_set_style_image_recolor_opa(v->img,
                    (lv_opa_t)(255 - p * 255 / 256), 0);
                {
                    int s = 256 + (256 - p) * 120 / 256;
                    view_scale(v, s, s);
                }
                break;

            case POP_BEAM: {
                int s = 60 + p * 196 / 256;
                view_scale(v, 256 + (256 - p) / 2, s);
                lv_obj_set_style_image_recolor(v->img, lv_color_hex(0x9AE8FF), 0);
                lv_obj_set_style_image_recolor_opa(v->img,
                    (lv_opa_t)(255 - p * 255 / 256), 0);
                break;
            }
            case POP_WIPE: {
                int s = 30 + p * 226 / 256;
                view_scale(v, s, s);
                lv_image_set_rotation(v->img, (v->pop0 - v->pop) * 320);
                break;
            }
            case POP_NORMAL:
            default: {
                int s = 256 + (256 - p) * 170 / 256;
                view_scale(v, s, s);
                break;
            }
            }

            lv_obj_set_style_image_opa(v->img, (lv_opa_t)(p > 255 ? 255 : p), 0);
            view_place(v);

            if (v->pop == 0) {
                view_hide(v);
            }
        }
    }

    a->phase--;
    if (a->phase <= 0) {
        do_collapse(a);
    }
}

static void step_fall(app_t *a)
{
    bool moving = false;

    for (int c = 0; c < GM_N; c++) {
        for (int r = 0; r < GM_N; r++) {
            cell_view_t *v = &a->v[r][c];
            int home = cell_home_y(r);

            if (v->y < home) {
                v->vy = (int16_t)(v->vy + 34);
                if (v->vy > 340) {
                    v->vy = 340;
                }
                v->y = (int16_t)(v->y + v->vy);
                if (v->y >= home) {
                    v->y = (int16_t)home;
                    v->squash = (uint8_t)(v->vy > 140 ? 4 : 2);
                    v->vy = 0;
                }
                moving = true;
                view_place(v);
            } else if (v->squash > 0) {
                /* squashing on landing, just barely: overdoing it makes the
                 * jewel spill out of its cell and overlap its neighbour */
                v->squash--;
                int k = v->squash * 9;
                view_scale(v, 256 + k, 256 - k);
                moving = true;
            } else if (v->birth > 0) {
                v->birth--;
                int k = v->birth * 10;
                view_scale(v, 256 + k, 256 + k);
                moving = true;
            }
        }
    }

    if (moving) {
        return;
    }

    if (!resolve_step(a)) {
        cascade_end(a);
    }
}

static void step_shuffle(app_t *a)
{
    a->phase--;

    int half = a->phase0 / 2;
    if (a->phase == half) {
        gm_board_shuffle(&a->b);
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                view_sync(a, r, c);
            }
        }
    }

    /* They go out and come back on. It could rotate them, but rotating or
     * scaling are the same expensive LVGL path (drawing into a separate layer
     * and transforming it) and here it would be all sixty-four at once;
     * opacity, by contrast, is one more blend in the same blit. */
    int t = (a->phase > half) ? (a->phase - half) * 255 / half
                              : (half - a->phase) * 255 / half;
    lv_opa_t opa = (lv_opa_t)clampi(255 - t, 25, 255);
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            lv_obj_set_style_image_opa(a->v[r][c].img, opa, 0);
            if (a->v[r][c].badge &&
                !lv_obj_has_flag(a->v[r][c].badge, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_set_style_image_opa(a->v[r][c].badge, opa, 0);
            }
        }
    }

    if (a->phase <= 0) {
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                lv_obj_set_style_image_opa(a->v[r][c].img, LV_OPA_COVER, 0);
                if (a->v[r][c].badge) {
                    lv_obj_set_style_image_opa(a->v[r][c].badge, LV_OPA_COVER, 0);
                }
            }
        }
        a->state = ST_IDLE;
        a->idle_frames = 0;
    }
}

/* If the player sits and stares, a move is pointed out to them. */
static void step_idle(app_t *a)
{
    a->idle_frames++;

#ifdef AOS_SIM_BUILTIN
    /* the move served up by GEMAS_TEST plays itself after a second, which is
     * plenty of time to start taking screenshots */
    if (a->test_mv[0] >= 0 && a->idle_frames > 30) {
        int8_t mv[4] = { a->test_mv[0], a->test_mv[1], a->test_mv[2], a->test_mv[3] };
        a->test_mv[0] = -1;
        try_swap(a, mv[0], mv[1], mv[2], mv[3]);
        return;
    }

    /* GEMAS_AUTO=1 makes the game play itself. It is not a game mode: it is
     * the only convenient way of watching a thousand cascades in a row while
     * tuning the animations, and of leaving it running for a good while to see
     * that it neither hangs nor runs out of moves. */
    if (getenv("GEMAS_AUTO") && a->idle_frames > 8) {
        uint8_t mv[4];
        if (gm_find_move(&a->b, mv)) {
            try_swap(a, mv[0], mv[1], mv[2], mv[3]);
            return;
        }
    }
#endif

    if (a->idle_frames == 200) {
        uint8_t mv[4];
        if (gm_find_move(&a->b, mv)) {
            a->hint[0] = mv[0]; a->hint[1] = mv[1];
            a->hint[2] = mv[2]; a->hint[3] = mv[3];
            a->hint_on = true;
        }
    }

    if (!a->hint_on) {
        return;
    }

    /* two jewels pulsing together */
    int t = a->idle_frames % 40;
    int k = (t < 20) ? t : 40 - t;
    int s = 256 + k * 4;
    for (int i = 0; i < 2; i++) {
        cell_view_t *v = &a->v[a->hint[i * 2]][a->hint[i * 2 + 1]];
        view_scale(v, s, s);
    }
}

static void hint_clear(app_t *a)
{
    if (!a->hint_on) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        view_scale(&a->v[a->hint[i * 2]][a->hint[i * 2 + 1]], 256, 256);
    }
    a->hint_on = false;
}

/* --------------------------------------------------------------------------
 * Time, game over and the loop
 * -------------------------------------------------------------------------- */

static void panel_show(app_t *a, lv_obj_t *panel);
static void panel_hide_all(app_t *a);
static void over_refresh(app_t *a, const char *title);

static void game_over(app_t *a, const char *why)
{
    a->state = ST_OVER;
    a->sel_r = a->sel_c = -1;
    lv_obj_add_flag(a->sel_ring, LV_OBJ_FLAG_HIDDEN);
    gm_snd_play(GM_SFX_GAMEOVER, 0);
    gm_fx_flash(&a->fx, 120, 10);
    over_refresh(a, why);
    panel_show(a, a->over);

    aos_hal_pref_set_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX,
                         (int32_t)a->hiscore);
}

static void step_time(app_t *a)
{
    if (a->mode != 1) {
        return;
    }

    const diff_t *d = &s_diffs[a->diff];
    a->timebar -= d->drain + d->drain_step * (a->level - 1);

    if (a->timebar <= 0) {
        a->timebar = 0;
        game_over(a, _("SE ACABO EL TIEMPO"));
        return;
    }

    /* warning: one tick a second when less than a fifth is left */
    if (a->timebar < TIME_MAX / 5) {
        if (--a->tick_warn <= 0) {
            a->tick_warn = 30;
            gm_snd_play(GM_SFX_TICK, 0);
        }
    } else {
        a->tick_warn = 0;
    }
}

static void apply_shake(app_t *a)
{
    int dx = 0, dy = 0;
    if (a->fx.shake > 0) {
        dx = (int)(gm_rnd(&a->b) % 7u) - 3;
        dy = (int)(gm_rnd(&a->b) % 7u) - 3;
    }
    lv_obj_set_pos(a->board, BOARD_X + dx, BOARD_Y + dy);
}

/* The score's texts are only rewritten when they change: every
 * lv_label_set_text dirties its area and sends it to be repainted. */
static void hud_tick(app_t *a)
{
    static uint32_t last_score = 0xFFFFFFFFu;
    static int last_level = -1;
    static int last_lvl_w = -1, last_time_w = -1;

    if (a->score != last_score || a->level != last_level) {
        last_score = a->score;
        last_level = a->level;
        hud_refresh(a);
        last_lvl_w = last_time_w = -1;
        return;
    }

    int w = a->level_need ? (BOARD_PX * a->level_pts / a->level_need) : 0;
    w = clampi(w, 0, BOARD_PX);
    if (w != last_lvl_w) {
        last_lvl_w = w;
        lv_obj_set_width(a->bar_level_fill, w);
    }

    if (a->mode == 1) {
        int tw = clampi(BOARD_PX * a->timebar / TIME_MAX, 0, BOARD_PX);
        if (tw != last_time_w) {
            last_time_w = tw;
            lv_obj_set_width(a->bar_time_fill, tw);
            uint32_t col = (a->timebar > TIME_MAX / 2) ? 0x30D158
                         : (a->timebar > TIME_MAX / 5) ? 0xFFD60A : 0xFF453A;
            lv_obj_set_style_bg_color(a->bar_time_fill, lv_color_hex(col), 0);
        }
    }
}

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this 'a' no longer exists */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    gm_snd_tick();

#ifdef AOS_SIM_BUILTIN
    if (getenv("GEMAS_TRACE")) {
        static int n;
        printf("[gemas] cuadro %d estado=%d fase=%d combo=%d\n",
               n++, (int)a->state, a->phase, a->combo);
    }
#endif

    switch (a->state) {
    case ST_SWAP:
    case ST_UNSWAP:  step_swap(a);    break;
    case ST_POP:     step_pop(a);     break;
    case ST_FALL:    step_fall(a);    break;
    case ST_SHUFFLE: step_shuffle(a); break;
    case ST_IDLE:    step_idle(a);    break;
    default: break;
    }

    if (a->state != ST_MENU && a->state != ST_PAUSE && a->state != ST_OVER) {
        step_time(a);
    }

    /* the selection ring pulses so it can be seen that it is held */
    if (a->sel_r >= 0) {
        /* narrowed to 32 bits before dividing: dividing a uint64_t drags in __udivdi3 */
        uint32_t ms = (uint32_t)aos_hal_uptime_ms();
        int t = (int)((ms / 60u) % 20u);
        int k = (t < 10) ? t : 20 - t;
        lv_obj_set_style_border_opa(a->sel_ring, (lv_opa_t)(150 + k * 10), 0);
    }

    gm_fx_step(&a->fx);
    apply_shake(a);
    hud_tick(a);
}

/* --------------------------------------------------------------------------
 * Input
 *
 * It can be played the two ways anybody expects: tapping a jewel and then its
 * neighbour, or dragging a jewel sideways. The drag is resolved at 14 pixels,
 * well before the 50 LVGL needs to call it a gesture, so they never clash.
 * -------------------------------------------------------------------------- */

static void sel_show(app_t *a, int r, int c)
{
    a->sel_r = (int8_t)r;
    a->sel_c = (int8_t)c;
    lv_obj_set_pos(a->sel_ring, c * GM_CELL, r * GM_CELL);
    lv_obj_remove_flag(a->sel_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->sel_ring);
}

static void sel_clear(app_t *a)
{
    a->sel_r = a->sel_c = -1;
    lv_obj_add_flag(a->sel_ring, LV_OBJ_FLAG_HIDDEN);
}

static void try_swap(app_t *a, int r1, int c1, int r2, int c2)
{
    if (a->state != ST_IDLE) {
        return;
    }
    if (r2 < 0 || r2 >= GM_N || c2 < 0 || c2 >= GM_N) {
        return;
    }

    hint_clear(a);
    sel_clear(a);

    a->swap_r1 = (int8_t)r1; a->swap_c1 = (int8_t)c1;
    a->swap_r2 = (int8_t)r2; a->swap_c2 = (int8_t)c2;
    a->state  = ST_SWAP;
    a->phase0 = 8;
    a->phase  = a->phase0;
    gm_snd_play(GM_SFX_SWAP, 0);
}

static bool point_to_cell(int px, int py, int *r, int *c)
{
    int x = px - BOARD_X;
    int y = py - BOARD_Y;
    if (x < 0 || y < 0 || x >= BOARD_PX || y >= BOARD_PX) {
        return false;
    }
    *c = x / GM_CELL;
    *r = y / GM_CELL;
    return true;
}

static void touch_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->pressing = false;
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t area;
    lv_obj_get_coords(a->touch, &area);
    int px = p.x - area.x1;
    int py = p.y - area.y1;

    if (code == LV_EVENT_PRESSED) {
        int r, c;
        a->pressing  = false;
        a->drag_done = false;
        if (!point_to_cell(px, py, &r, &c) || a->state != ST_IDLE) {
            return;
        }
        a->pressing = true;
        a->press_x  = (int16_t)px;
        a->press_y  = (int16_t)py;
        a->press_r  = (int8_t)r;
        a->press_c  = (int8_t)c;
        hint_clear(a);

        if (a->sel_r >= 0) {
            int dr = r - a->sel_r, dc = c - a->sel_c;
            int adr = dr < 0 ? -dr : dr;
            int adc = dc < 0 ? -dc : dc;
            if (adr + adc == 1) {
                try_swap(a, a->sel_r, a->sel_c, r, c);
                return;
            }
            if (adr + adc == 0) {
                sel_clear(a);           /* tapping the same one releases it */
                return;
            }
        }
        sel_show(a, r, c);
        gm_snd_play(GM_SFX_SELECT, 0);
        return;
    }

    /* LV_EVENT_PRESSING: dragging a jewel sideways */
    if (!a->pressing || a->drag_done || a->state != ST_IDLE) {
        return;
    }
    int dx = px - a->press_x;
    int dy = py - a->press_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 14 && ady < 14) {
        return;
    }

    a->drag_done = true;
    int r2 = a->press_r, c2 = a->press_c;
    if (adx > ady) {
        c2 += (dx > 0) ? 1 : -1;
    } else {
        r2 += (dy > 0) ? 1 : -1;
    }
    try_swap(a, a->press_r, a->press_c, r2, c2);
}

/* With AOS_APP_FLAG_NO_SWIPE the back gesture is handled by the app. While
 * playing it does not count: dragging is how the jewels are moved. */
static void touch_gesture(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();

    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }
    if (a->state != ST_MENU) {
        return;
    }
    lv_indev_wait_release(indev);
    a->want_exit = true;
}

/* --------------------------------------------------------------------------
 * Panels and buttons
 *
 * The game is all images, but the menus are text to read and touch: there
 * LVGL's vector typography and the usual large buttons are the right choice.
 * -------------------------------------------------------------------------- */

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, uint32_t color, lv_event_cb_t cb,
                             void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, h / 2, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, aos_font_body, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_center(label);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return btn;
}

static lv_obj_t *make_chip(lv_obj_t *parent, int x, int y, int w,
                           lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, w, 44);
    lv_obj_set_pos(chip, x, y);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 22, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x3A3A46), 0);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(chip);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, aos_font_body, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return chip;
}

static void chip_set(lv_obj_t *chip, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    if (label) {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    }
    lv_obj_set_style_border_color(chip, lv_color_hex(color), 0);
}

static lv_obj_t *make_dialog(lv_obj_t *root, int w, int h, uint32_t border)
{
    lv_obj_t *p = lv_obj_create(root);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x08080C), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_90, 0);
    lv_obj_set_style_radius(p, 26, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(border), 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);      /* covers the board */
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, uint32_t color, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_y(label, y);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void panel_hide_all(app_t *a)
{
    lv_obj_t *const panels[] = { a->menu, a->pause, a->over };
    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++) {
        if (panels[i]) {
            lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void panel_show(app_t *a, lv_obj_t *panel)
{
    panel_hide_all(a);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel);
}

/* --------------------------------------------------------------------------
 * Menu, pause and game over
 * -------------------------------------------------------------------------- */

static void prefs_save(app_t *a)
{
    aos_hal_pref_set_i32(KEY_MODE, a->mode);
    aos_hal_pref_set_i32(KEY_DIFF, a->diff);
    aos_hal_pref_set_i32(KEY_SFX, gm_snd_enabled() ? 1 : 0);
}

static void menu_refresh(app_t *a)
{
    chip_set(a->chip_mode, a->mode == 1 ? _("CONTRARRELOJ") : _("SIN APURO"),
             a->mode == 1 ? 0xFF9F0A : 0x30D158);
    chip_set(a->chip_diff, _(s_diffs[a->diff].name),
             a->diff == 0 ? 0x30D158 : (a->diff == 1 ? 0x0A84FF : 0xFF453A));
    chip_set(a->chip_sfx, gm_snd_enabled() ? _("SONIDO SI") : _("SONIDO NO"),
             gm_snd_enabled() ? 0x0A84FF : 0x8E8E93);

    int32_t best = 0;
    aos_hal_pref_get_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX, &best);
    char buf[48], num[24];
    format_score(num, sizeof(num), (uint32_t)(best > 0 ? best : 0));
    snprintf(buf, sizeof(buf), _("MEJOR   %s"), num);
    lv_label_set_text(a->lbl_best, buf);
}

static void over_refresh(app_t *a, const char *title)
{
    lv_label_set_text(a->lbl_over_title, title);

    char buf[96], num[24], best[24];
    int32_t hi = 0;
    aos_hal_pref_get_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX, &hi);
    format_score(num, sizeof(num), a->score);
    format_score(best, sizeof(best),
                 (uint32_t)(hi > (int32_t)a->score ? hi : (int32_t)a->score));
    lv_label_set_text(a->lbl_over_score, num);
    snprintf(buf, sizeof(buf), _("NIVEL %d   -   MEJOR %s"), a->level, best);
    lv_label_set_text(a->lbl_over_info, buf);
}

static void game_start(app_t *a)
{
    gm_board_init(&a->b, (uint32_t)aos_hal_uptime_ms() ^ 0xA5A5u,
                  s_diffs[a->diff].colors);
    gm_board_fill(&a->b);

    a->score      = 0;
    a->level      = 1;
    a->level_pts  = 0;
    a->level_need = 1450;
    a->timebar    = TIME_MAX;
    a->combo      = 0;
    a->tick_warn  = 0;
    a->hint_on    = false;
    a->swap_r2    = -1;
    a->swap_c2    = -1;

    int32_t hi = 0;
    aos_hal_pref_get_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX, &hi);
    a->hiscore = (uint32_t)(hi > 0 ? hi : 0);

    sel_clear(a);
    gm_fx_clear(&a->fx);
    panel_hide_all(a);

    /* the initial deal is the same fall as always: each column starts a little
     * higher than the one beside it and the board fills itself */
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            cell_view_t *v = &a->v[r][c];
            v->x = (int16_t)cell_home_x(c);
            v->y = (int16_t)(-(GM_N - r + (c % 3)) * GM_CELL * FX16 / 2);
            v->vy = 0;
            v->pop = 0;
            v->delay = 0;
            v->squash = 0;
            v->birth = 0;
            view_sync(a, r, c);
            view_scale(v, 256, 256);
            lv_image_set_rotation(v->img, 0);
            view_place(v);
        }
    }

    lv_obj_remove_flag(a->hud, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->board, LV_OBJ_FLAG_HIDDEN);
    if (a->mode == 1) {
        lv_obj_remove_flag(a->bar_time, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(a->bar_time, LV_OBJ_FLAG_HIDDEN);
    }

    hud_refresh(a);
    gm_snd_play(GM_SFX_START, 0);
    a->state = ST_FALL;
}

static void go_menu(app_t *a)
{
    a->state = ST_MENU;
    sel_clear(a);
    gm_fx_clear(&a->fx);
    menu_refresh(a);
    panel_show(a, a->menu);
}

static void cb_play(lv_event_t *e)     { game_start((app_t *)lv_event_get_user_data(e)); }
static void cb_menu(lv_event_t *e)     { go_menu((app_t *)lv_event_get_user_data(e)); }

static void cb_exit(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->want_exit = true;        /* leaving here would destroy the app inside its own callback */
}

static void cb_mode(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->mode = a->mode ? 0 : 1;
    prefs_save(a);
    menu_refresh(a);
    gm_snd_play(GM_SFX_SELECT, 0);
}

static void cb_diff(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->diff = (a->diff + 1) % 3;
    prefs_save(a);
    menu_refresh(a);
    gm_snd_play(GM_SFX_SELECT, 0);
}

static void cb_sfx(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    gm_snd_enable(!gm_snd_enabled());
    prefs_save(a);
    menu_refresh(a);
    chip_set(a->chip_sfx2, gm_snd_enabled() ? _("SONIDO SI") : _("SONIDO NO"),
             gm_snd_enabled() ? 0x0A84FF : 0x8E8E93);
    gm_snd_play(GM_SFX_SELECT, 0);
}

static void pause_open(app_t *a)
{
    if (a->state == ST_MENU || a->state == ST_OVER || a->state == ST_PAUSE) {
        return;
    }
    a->resume = a->state;
    a->state  = ST_PAUSE;
    chip_set(a->chip_sfx2, gm_snd_enabled() ? _("SONIDO SI") : _("SONIDO NO"),
             gm_snd_enabled() ? 0x0A84FF : 0x8E8E93);
    panel_show(a, a->pause);
}

static void cb_pause(lv_event_t *e)  { pause_open((app_t *)lv_event_get_user_data(e)); }

static void cb_resume(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->state = a->resume;
    panel_hide_all(a);
}

static void cb_retry(lv_event_t *e)  { game_start((app_t *)lv_event_get_user_data(e)); }

/* --------------------------------------------------------------------------
 * Building the screen
 * -------------------------------------------------------------------------- */

static void build_hud(app_t *a, lv_obj_t *root)
{
    a->hud = lv_obj_create(root);
    lv_obj_remove_style_all(a->hud);
    lv_obj_set_size(a->hud, AOS_SCREEN_W, 72);
    lv_obj_set_pos(a->hud, 0, 0);
    lv_obj_remove_flag(a->hud, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->hud, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->hud, LV_OBJ_FLAG_HIDDEN);

    a->lbl_score = lv_label_create(a->hud);
    lv_label_set_text(a->lbl_score, "0");
    lv_obj_set_style_text_font(a->lbl_score, aos_font_title, 0);
    lv_obj_set_style_text_color(a->lbl_score, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(a->lbl_score, 12, 6);
    lv_obj_remove_flag(a->lbl_score, LV_OBJ_FLAG_CLICKABLE);

    /* a fixed-width box with the text on the right: if the label grew with the
     * text it would drift when going from NIVEL 9 to NIVEL 10 */
    a->lbl_level = lv_label_create(a->hud);
    lv_label_set_text(a->lbl_level, _("NIVEL 1"));
    lv_obj_set_style_text_font(a->lbl_level, aos_font_body, 0);
    lv_obj_set_style_text_color(a->lbl_level, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_align(a->lbl_level, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(a->lbl_level, 140, 26);
    lv_obj_set_pos(a->lbl_level, 168, 14);
    lv_obj_remove_flag(a->lbl_level, LV_OBJ_FLAG_CLICKABLE);

    a->btn_pause = lv_obj_create(a->hud);
    lv_obj_remove_style_all(a->btn_pause);
    lv_obj_set_size(a->btn_pause, 34, 34);
    lv_obj_set_pos(a->btn_pause, AOS_SCREEN_W - 46, 8);
    lv_obj_set_style_bg_color(a->btn_pause, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(a->btn_pause, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(a->btn_pause, 17, 0);
    lv_obj_set_style_border_width(a->btn_pause, 2, 0);
    lv_obj_set_style_border_color(a->btn_pause, lv_color_hex(0x3A3A46), 0);
    lv_obj_add_flag(a->btn_pause, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->btn_pause, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->btn_pause, cb_pause, LV_EVENT_CLICKED, a);
    lv_obj_t *pl = lv_label_create(a->btn_pause);
    lv_label_set_text(pl, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_color(pl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(pl);
    lv_obj_remove_flag(pl, LV_OBJ_FLAG_CLICKABLE);

    struct { lv_obj_t **bar, **fill; int y, h; uint32_t color; } bars[] = {
        { &a->bar_level, &a->bar_level_fill, 46, 5,  0x0A84FF },
        { &a->bar_time,  &a->bar_time_fill,  57, 10, 0x30D158 },
    };
    for (unsigned i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        lv_obj_t *bar = lv_obj_create(a->hud);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, BOARD_PX, bars[i].h);
        lv_obj_set_pos(bar, BOARD_X, bars[i].y);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x1C1C22), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, bars[i].h / 2, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *fill = lv_obj_create(bar);
        lv_obj_remove_style_all(fill);
        lv_obj_set_size(fill, 0, bars[i].h);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(bars[i].color), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(fill, bars[i].h / 2, 0);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);

        *bars[i].bar = bar;
        *bars[i].fill = fill;
    }
}

static void build_menu(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = lv_obj_create(root);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(p, touch_gesture, LV_EVENT_GESTURE, a);
    a->menu = p;

    make_text(p, _("GEMAS"), aos_font_huge, 0xFFFFFF, 26);
    make_text(p, _("ALINEA TRES O MAS"), aos_font_small, 0x8E8E93, 84);

    /* a sample of the seven jewels: it is what best says what this is about */
    for (int i = 0; i < GM_TYPES; i++) {
        lv_obj_t *img = lv_image_create(p);
        lv_image_set_src(img, &a->art.gem[i].dsc);
        lv_obj_set_size(img, GM_SPRITE, GM_SPRITE);
        lv_image_set_pivot(img, GM_SPRITE / 2, GM_SPRITE / 2);
        lv_image_set_scale(img, 224);
        lv_obj_set_pos(img, 23 + i * 46, 112);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }

    a->chip_mode = make_chip(p, 54, 174, 260, cb_mode, a);
    a->chip_diff = make_chip(p, 54, 224, 260, cb_diff, a);
    a->chip_sfx  = make_chip(p, 54, 274, 260, cb_sfx,  a);
    make_button(p, _("JUGAR"), 54, 336, 260, 56, 0x30D158, cb_play, a);
    a->lbl_best = make_text(p, _("MEJOR   0"), aos_font_small, 0x8E8E93, 410);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_dialog(root, 300, 300, 0x3A3A46);
    a->pause = p;
    lv_obj_add_event_cb(p, touch_gesture, LV_EVENT_GESTURE, a);

    make_text(p, _("PAUSA"), aos_font_title, 0xFFFFFF, 22);
    make_button(p, _("SEGUIR"), 20, 78, 260, 48, 0x30D158, cb_resume, a);
    a->chip_sfx2 = make_chip(p, 20, 136, 260, cb_sfx, a);
    make_button(p, _("MENU"),  20, 192, 125, 48, 0x0A84FF, cb_menu, a);
    make_button(p, _("SALIR"), 155, 192, 125, 48, 0xFF453A, cb_exit, a);
}

static void build_over(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_dialog(root, 300, 288, 0xFF453A);
    a->over = p;
    lv_obj_add_event_cb(p, touch_gesture, LV_EVENT_GESTURE, a);

    a->lbl_over_title = make_text(p, "", aos_font_body, 0xFF6A5A, 20);
    a->lbl_over_score = make_text(p, "", aos_font_huge, 0xFFFFFF, 50);
    a->lbl_over_info  = make_text(p, "", aos_font_small, 0x8E8E93, 108);

    make_button(p, _("OTRA VEZ"), 20, 142, 260, 48, 0x30D158, cb_retry, a);
    make_button(p, _("MENU"),  20, 200, 125, 48, 0x0A84FF, cb_menu, a);
    make_button(p, _("SALIR"), 155, 200, 125, 48, 0xFF453A, cb_exit, a);
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    int32_t v = 0;
    if (aos_hal_pref_get_i32(KEY_MODE, &v)) {
        a->mode = (v == 1) ? 1 : 0;
    }
    if (aos_hal_pref_get_i32(KEY_DIFF, &v)) {
        a->diff = clampi((int)v, 0, 2);
    }
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) {
        gm_snd_enable(v != 0);
    }
}

static void *gemas_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }
    a->root  = root;
    a->sel_r = a->sel_c = -1;

    /* The sprites add up to some 80 KB and go through malloc(), not through
     * lv_malloc(): on the board LVGL's pool is 64 KB of internal RAM, and with
     * CONFIG_SPIRAM_USE_MALLOC this lands in PSRAM, which is where it
     * belongs. */
    if (!gm_art_init(&a->art)) {
        lv_free(a);
        return NULL;
    }

    gm_snd_init();
    prefs_load(a);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* the board's frame */
    lv_obj_t *frame_obj = lv_obj_create(root);
    lv_obj_remove_style_all(frame_obj);
    lv_obj_set_size(frame_obj, BOARD_PX + 8, BOARD_PX + 8);
    lv_obj_set_pos(frame_obj, BOARD_X - 4, BOARD_Y - 4);
    lv_obj_set_style_radius(frame_obj, 16, 0);
    lv_obj_set_style_border_width(frame_obj, 2, 0);
    lv_obj_set_style_border_color(frame_obj, lv_color_hex(0x2A3350), 0);
    lv_obj_remove_flag(frame_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(frame_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* the board: a container of its own so it can be shaken as a whole, and it
     * also clips the jewels that are still falling in from above */
    a->board = lv_obj_create(root);
    lv_obj_remove_style_all(a->board);
    lv_obj_set_size(a->board, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(a->board, BOARD_X, BOARD_Y);
    lv_obj_remove_flag(a->board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->board, LV_OBJ_FLAG_HIDDEN);

    a->bg = lv_image_create(a->board);
    lv_image_set_src(a->bg, &a->art.tile.dsc);
    lv_obj_set_size(a->bg, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(a->bg, 0, 0);
    lv_image_set_inner_align(a->bg, LV_IMAGE_ALIGN_TILE);
    lv_obj_remove_flag(a->bg, LV_OBJ_FLAG_CLICKABLE);

    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            a->v[r][c].img = make_image(a);
            a->v[r][c].type = 0;
            a->v[r][c].special = GM_SP_NONE;
            lv_obj_add_flag(a->v[r][c].img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    a->sel_ring = lv_obj_create(a->board);
    lv_obj_remove_style_all(a->sel_ring);
    lv_obj_set_size(a->sel_ring, GM_CELL, GM_CELL);
    lv_obj_set_style_radius(a->sel_ring, 12, 0);
    lv_obj_set_style_border_width(a->sel_ring, 3, 0);
    lv_obj_set_style_border_color(a->sel_ring, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(a->sel_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->sel_ring, LV_OBJ_FLAG_CLICKABLE);

    gm_fx_init(&a->fx, root);

    /* touch layer: it covers the screen, but only does anything over the board */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESS_LOST, a);
    lv_obj_add_event_cb(a->touch, touch_gesture, LV_EVENT_GESTURE, a);

    build_hud(a, root);
    build_menu(a, root);
    build_pause(a, root);
    build_over(a, root);

    go_menu(a);

#ifdef AOS_SIM_BUILTIN
    /* Shortcuts for looking at an animation without having to provoke it by
     * playing:
     *
     *   GEMAS_TEST=4     starts with a line of four served up
     *   GEMAS_TEST=5     one of five
     *   GEMAS_TEST=L     an L (leaves a star)
     *   GEMAS_TEST=H     a hypercube beside a pile of its colour
     *   GEMAS_TIME=1     time-attack mode
     */
    a->test_mv[0] = -1;
    const char *test = getenv("GEMAS_TEST");
    const char *timed = getenv("GEMAS_TIME");
    if (timed) {
        a->mode = 1;
    }
    if (test) {
        game_start(a);

        /* a background with no completed lines: two neighbouring cells never match */
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                a->b.c[r][c].type = (int8_t)((r + 2 * c) % a->b.ncolors);
                a->b.c[r][c].special = GM_SP_NONE;
            }
        }

        /* in every case the move is dropping the jewel from (3,3) to (4,3) */
        a->b.c[3][3].type = 0;
        a->test_mv[0] = 3; a->test_mv[1] = 3;
        a->test_mv[2] = 4; a->test_mv[3] = 3;

        switch (test[0]) {
        case '4':   /* line of four: leaves a flame */
            a->b.c[4][1].type = 0; a->b.c[4][2].type = 0; a->b.c[4][4].type = 0;
            break;
        case '5':   /* line of five: leaves a hypercube */
            a->b.c[4][1].type = 0; a->b.c[4][2].type = 0;
            a->b.c[4][4].type = 0; a->b.c[4][5].type = 0;
            break;
        case 'L':   /* cross: leaves a star */
            a->b.c[4][2].type = 0; a->b.c[4][4].type = 0;
            a->b.c[5][3].type = 0; a->b.c[6][3].type = 0;
            break;
        case 'F':   /* a flame already made, to watch it detonate */
            a->b.c[4][1].type = 0; a->b.c[4][2].type = 0;
            a->b.c[4][2].special = GM_SP_FLAME;
            break;
        case 'S':   /* a star already made */
            a->b.c[4][1].type = 0; a->b.c[4][2].type = 0;
            a->b.c[4][2].special = GM_SP_STAR;
            break;
        case 'H':   /* hypercube: swapped with its neighbour, it takes that colour */
            a->b.c[3][3].special = GM_SP_HYPER;
            break;
        default:
            break;
        }

        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                view_sync(a, r, c);
                a->v[r][c].y = (int16_t)cell_home_y(r);
                a->v[r][c].x = (int16_t)cell_home_x(c);
                view_place(&a->v[r][c]);
            }
        }
        a->state = ST_IDLE;
        a->idle_frames = 0;
    } else if (timed && atoi(timed) > 1) {
        /* GEMAS_TIME=400 starts with the bar nearly empty: it is the quick way
         * to reach the game over panel without playing for a minute */
        game_start(a);
        a->timebar = atoi(timed);
    } else if (timed) {
        menu_refresh(a);        /* the chip has to reflect the mode */
    }
#endif

    int frame_ms = FRAME_MS;
#ifdef AOS_SIM_BUILTIN
    /* GEMAS_SLOW=120 lengthens the frame: useful for watching an animation
     * calmly, or for taking screenshots of it without it getting away */
    if (getenv("GEMAS_SLOW")) {
        frame_ms = atoi(getenv("GEMAS_SLOW"));
        if (frame_ms < 10) {
            frame_ms = FRAME_MS;
        }
    }
#endif
    a->timer = lv_timer_create(frame, frame_ms, a);
    return a;
}

static void gemas_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    if (a->hiscore > 0) {
        int32_t hi = 0;
        aos_hal_pref_get_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX, &hi);
        if ((int32_t)a->hiscore > hi) {
            aos_hal_pref_set_i32(a->mode == 1 ? KEY_HI_TIME : KEY_HI_RELAX,
                                 (int32_t)a->hiscore);
        }
    }
    gm_snd_stop();
    gm_art_free(&a->art);
    lv_free(a);
}

/* Leaving in the middle of a game should not cost the board: it pauses. */
static void gemas_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a) {
        pause_open(a);
    }
}

static bool gemas_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    if (a->state == ST_MENU) {
        return false;           /* let the runtime close the app */
    }
    if (a->state == ST_PAUSE || a->state == ST_OVER) {
        return true;            /* the panels already have their own buttons */
    }
    pause_open(a);
    return true;
}

/* The side button pauses and unpauses; in the menu it does not touch it, so it
 * goes on being useful for going back. */
static bool gemas_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a || action != AOS_BUTTON_CLICK) {
        return false;
    }
    if (a->state == ST_MENU || a->state == ST_OVER) {
        return false;
    }
    if (a->state == ST_PAUSE) {
        a->state = a->resume;
        panel_hide_all(a);
    } else {
        pause_open(a);
    }
    return true;
}

static bool gemas_init(aos_app_t *app)
{
    app->desc.id      = "demo.gemas";
    app->desc.name    = "Gemas";
    app->desc.icon    = LV_SYMBOL_SHUFFLE;
    app->desc.icon_vec = AOS_ICON_GEM;
    app->desc.color_a = 0xE81E32;
    app->desc.color_b = 0x2E7BFF;
    app->desc.order   = 147;
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                        AOS_APP_FLAG_NO_SWIPE;

    app->create  = gemas_create;
    app->destroy = gemas_destroy;
    app->hide    = gemas_hide;
    app->back    = gemas_back;
    app->button  = gemas_button;
    return true;
}

AOS_APP_ENTRY(gemas_init);
