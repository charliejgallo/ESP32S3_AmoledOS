/*
 * AmoledOS - Minesweeper
 *
 * Three boards: 8x8 with 10 mines, 10x10 with 18 and 12x12 with 28.
 *
 * The decision that orders the whole file is HOW THE BOARD IS DRAWN, and it is
 * not the obvious one. The natural thing in LVGL would be one object per cell,
 * and the largest of the three boards is 144 cells plus the numbers: close to
 * 250 objects. LVGL objects are small allocations and small allocations come
 * out of internal RAM, which is precisely what is scarce; running short of
 * internal RAM on this board does not give an error, it gives GARBAGE ON THE
 * SCREEN (the flush's DMA buffer allocation fails and that strip is not
 * drawn). The full story is in HANDOFF-APPS.md: a 110-object watchface was
 * enough to provoke it.
 *
 * So the whole board is ONE object: an lv_canvas of 368x368 drawn on with
 * LVGL's primitives (lv_draw_rect and lv_draw_label onto a canvas layer, the
 * same path the Photos app uses for JPEG). It costs a 271 KB buffer in PSRAM,
 * of which there is plenty, and not one byte of internal RAM.
 *
 * And since a minesweeper animates nothing -between two touches the screen
 * stands still-, there is no frame timer: only the rectangle of cells that
 * changed is redrawn and only that area is invalidated. The only timer there
 * is is the clock's, once a second, and its label lives OUTSIDE the canvas so
 * as not to force it to repaint.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Geometry -------------------------------------------------------------
 * The canvas takes the full 368 px of width and 368 of height; the 80 left
 * over are the bar. The board is 360 px centred, that is, 4 px of margin: the
 * glass's corners have a 38 px radius and at this height they already bite. */
/* Measured on the board on 2026-09-06: the digitiser reports nothing past
 * y = 395, even though the panel draws down to 448. The last 53 px are seen
 * and not touched, and no calibration fixes it -it was recalibrated and the
 * ceiling stayed the same-. So EVERYTHING TOUCHABLE LIVES ABOVE 390.
 *
 * From that comes this layout, which is not the obvious one:
 *
 *   0..56    bar with the three buttons          (touched)
 *   56..386  board                               (touched)
 *   396..420 the mines/time/best line            (NOT touched: it is text)
 *
 * The information line is sent into the dead strip on purpose. It is the only
 * part of the app that is only read, so it is exactly what should go there;
 * spending live pixels on text and leaving the dead ones empty would be
 * backwards.
 *
 * The board pays for the adjustment: the cell goes from 45/36/30 px to
 * 41/33/27. The alternative was raising the bar and leaving the board below,
 * which is what the user asked for, but that put the minefield's last two rows
 * into the dead strip: it traded three dead buttons for twenty-four dead
 * cells. */
#define CV_W        368
#define CV_H        330
#define CANVAS_Y    56
#define BAR_H       CANVAS_Y
#define INFO_Y      396
#define INFO_H      24

#define MAX_N       12
#define MAX_CELLS   (MAX_N * MAX_N)

/* 360 is divisible by all three sides: 45, 36 and 30 px per cell. */
static const uint8_t LEVEL_N[3]     = { 8, 10, 12 };
static const uint8_t LEVEL_MINES[3] = { 10, 18, 28 };
static const char *const LEVEL_NAME[3] = { "8x8", "10x10", "12x12" };

#define ST_HIDDEN   0
#define ST_OPEN     1
#define ST_FLAG     2

typedef enum { GAME_READY = 0, GAME_RUN, GAME_LOST, GAME_WON } phase_t;

/* Classic number colours. Index 0 is unused. */
static const uint32_t NUM_COLOR[9] = {
    0x000000, 0x0A84FF, 0x30D158, 0xFF453A, 0xBF5AF2,
    0xFF9F0A, 0x40C8E0, 0xFFFFFF, 0x8E8E93,
};

typedef struct {
    lv_obj_t   *canvas;
    lv_obj_t   *lbl_info;
    lv_obj_t   *lbl_level;
    lv_obj_t   *lbl_mode;
    lv_timer_t *timer;

    uint16_t *buf;              /* 368x368 RGB565, PSRAM */

    uint8_t mine[MAX_CELLS];
    uint8_t adj[MAX_CELLS];
    uint8_t state[MAX_CELLS];

    int   level;                /* index into LEVEL_N */
    int   n;                    /* side of the board */
    int   mines;
    int   cell;                 /* px per cell */
    int   off_x, off_y;         /* margin to centre the board in the canvas */
    int   opened;
    int   flags;

    phase_t phase;
    bool    flag_mode;
    bool    xray;               /* MINES_XRAY=1: draws the hidden mines */
    bool    long_done;          /* the CLICKED that follows a LONG_PRESSED */
    int     boom;               /* the cell that exploded, -1 if none */

    uint32_t start_ms;
    uint32_t elapsed_s;
    uint32_t end_ms;            /* when the game ended, for the deafness */
    int      best[3];

    /* rectangle of cells to redraw */
    bool dirty;
    int  dx0, dy0, dx1, dy1;    /* inclusive */

    uint32_t rng;

    /* Stack for the cascading reveal. It goes here and not on the task's stack
     * because LVGL's is 20 KB and 144 integers have no business spending
     * them. */
    int16_t stack[MAX_CELLS];
} mines_t;

static mines_t s_m;

/* -------------------------------------------------------------------------- */

static uint32_t rnd(void)
{
    uint32_t x = s_m.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_m.rng = x;
    return x;
}

static inline int idx(int x, int y) { return y * s_m.n + x; }
static inline bool inside(int x, int y)
{
    return x >= 0 && x < s_m.n && y >= 0 && y < s_m.n;
}

/* -------------------------------------------------------------------------- */
/* Redrawing by rectangle of cells                                             */

static void dirty_add(int x0, int y0, int x1, int y1)
{
    if (!s_m.dirty) {
        s_m.dirty = true;
        s_m.dx0 = x0; s_m.dy0 = y0; s_m.dx1 = x1; s_m.dy1 = y1;
        return;
    }
    if (x0 < s_m.dx0) s_m.dx0 = x0;
    if (y0 < s_m.dy0) s_m.dy0 = y0;
    if (x1 > s_m.dx1) s_m.dx1 = x1;
    if (y1 > s_m.dy1) s_m.dy1 = y1;
}

static void dirty_cell(int x, int y)
{
    dirty_add(x, y, x, y);
}

static void dirty_all(void)
{
    dirty_add(0, 0, s_m.n - 1, s_m.n - 1);
}

/* -------------------------------------------------------------------------- */
/* Drawing a cell                                                              */

static void cell_area(int x, int y, lv_area_t *out)
{
    out->x1 = s_m.off_x + x * s_m.cell;
    out->y1 = s_m.off_y + y * s_m.cell;
    out->x2 = out->x1 + s_m.cell - 1;
    out->y2 = out->y1 + s_m.cell - 1;
}

static void fill(lv_layer_t *layer, const lv_area_t *area, uint32_t color,
                 int32_t radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa   = LV_OPA_COVER;
    dsc.radius   = radius;
    lv_draw_rect(layer, &dsc, area);
}

/* Text centred on both axes inside 'box'. lv_draw_label centres horizontally
 * with 'align' but starts at the top, so the vertical centring has to be done
 * by hand with the font's line height. */
static void centered_text(lv_layer_t *layer, const lv_area_t *box,
                          const char *text, const lv_font_t *font,
                          uint32_t color)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text  = text;
    dsc.font  = font;
    dsc.color = lv_color_hex(color);
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.opa   = LV_OPA_COVER;

    int32_t lh = lv_font_get_line_height(font);
    lv_area_t a = *box;
    a.y1 = (box->y1 + box->y2 + 1 - lh) / 2;
    a.y2 = a.y1 + lh;
    lv_draw_label(layer, &dsc, &a);
}

static const lv_font_t *cell_font(void)
{
    return s_m.cell >= 40 ? aos_font_title : aos_font_body;
}

static void draw_mine(lv_layer_t *layer, const lv_area_t *box)
{
    int cx = (box->x1 + box->x2) / 2;
    int cy = (box->y1 + box->y2) / 2;
    int r  = s_m.cell / 5;

    lv_area_t body = { cx - r, cy - r, cx + r, cy + r };
    fill(layer, &body, 0x000000, LV_RADIUS_CIRCLE);

    /* two cardinal spikes: enough for it to read as a mine */
    int len = s_m.cell / 3;
    int th  = LV_MAX(2, s_m.cell / 12);
    lv_area_t h = { cx - len, cy - th / 2, cx + len, cy - th / 2 + th };
    lv_area_t v = { cx - th / 2, cy - len, cx - th / 2 + th, cy + len };
    fill(layer, &h, 0x000000, 0);
    fill(layer, &v, 0x000000, 0);
}

static void draw_flag(lv_layer_t *layer, const lv_area_t *box)
{
    int cx = (box->x1 + box->x2) / 2;
    int cy = (box->y1 + box->y2) / 2;
    int h  = s_m.cell / 3;
    int th = LV_MAX(2, s_m.cell / 14);

    lv_area_t pole = { cx - th / 2, cy - h, cx - th / 2 + th, cy + h };
    fill(layer, &pole, 0xE0E0E0, 0);

    lv_area_t cloth = { cx - th / 2 - s_m.cell / 4, cy - h,
                        cx - th / 2, cy - h + h };
    fill(layer, &cloth, 0xFF453A, LV_MAX(1, s_m.cell / 12));

    lv_area_t foot = { cx - s_m.cell / 5, cy + h - th, cx + s_m.cell / 5, cy + h };
    fill(layer, &foot, 0xE0E0E0, 0);
}

static void draw_cell(lv_layer_t *layer, int x, int y)
{
    lv_area_t box;
    cell_area(x, y, &box);

    /* two px of air between cells: the board reads without grid lines */
    lv_area_t tile = { box.x1 + 1, box.y1 + 1, box.x2 - 1, box.y2 - 1 };
    int i = idx(x, y);
    int radius = LV_MAX(2, s_m.cell / 6);

    bool reveal_mines = (s_m.phase == GAME_LOST) || s_m.xray;

    if (s_m.state[i] == ST_FLAG) {
        if (reveal_mines && !s_m.mine[i]) {
            fill(layer, &tile, 0x7A4A10, radius);       /* one flag too many */
            draw_flag(layer, &box);
        } else {
            fill(layer, &tile, 0x3A3A3C, radius);
            draw_flag(layer, &box);
        }
        return;
    }

    if (s_m.state[i] == ST_HIDDEN) {
        if (reveal_mines && s_m.mine[i]) {
            fill(layer, &tile, 0x8E1F18, radius);
            draw_mine(layer, &box);
        } else {
            fill(layer, &tile, 0x3A3A3C, radius);
        }
        return;
    }

    /* revealed */
    if (s_m.mine[i]) {
        fill(layer, &tile, i == s_m.boom ? 0xFF453A : 0x8E1F18, radius);
        draw_mine(layer, &box);
        return;
    }

    /* A revealed cell is painted BLACK and fills the whole box, with no
     * radius: that way an opened region comes out as a flat field and not as a
     * mosaic of barely darker tiles, which is what it looked like -on the
     * first test it read as "that part was not drawn"-. As a bonus, on an
     * AMOLED black is a pixel switched off. */
    fill(layer, &box, 0x000000, 0);
    if (s_m.adj[i] > 0) {
        /* The string has to survive until lv_canvas_finish_layer():
         * lv_draw_label() stores THE POINTER, not the text (it only copies it
         * if asked with text_local, and then text_length has to be given as
         * well). With a buffer on this function's stack, by the time the layer
         * is really drawn that buffer no longer exists and broken glyphs come
         * out: it is exactly what happened on the first test. With static
         * literals there is nothing to copy and nothing to free. */
        static const char *const DIGIT[9] = {
            "", "1", "2", "3", "4", "5", "6", "7", "8",
        };
        centered_text(layer, &box, DIGIT[s_m.adj[i]], cell_font(),
                      NUM_COLOR[s_m.adj[i]]);
    }
}

static void present(void)
{
    if (!s_m.dirty || !s_m.canvas) {
        return;
    }
    s_m.dirty = false;

    lv_layer_t layer;
    lv_canvas_init_layer(s_m.canvas, &layer);

    /* Black background over the whole dirty rectangle BEFORE the cells.
     *
     * It is needed because draw_cell() does not paint the whole box: it paints
     * the tile inset by 1 px, and the 2 px of separation between cells are
     * "whatever is underneath". When the app opens that is black (the canvas
     * comes from a memset) and the board reads perfectly, so the bug never
     * shows up on the initial screen. But on CHANGING LEVEL the cell size
     * changes, and the new board's separations land on top of the old one's
     * grey tiles: the board ends up as a flat grey slab with isolated black
     * dots, which are the previous board's separations. It looks as if nothing
     * had been drawn.
     *
     * One rectangle, not 144: clearing per cell would be one more draw task
     * per cell. */
    lv_area_t fondo = {
        .x1 = s_m.off_x + s_m.dx0 * s_m.cell,
        .y1 = s_m.off_y + s_m.dy0 * s_m.cell,
    };
    fondo.x2 = s_m.off_x + (s_m.dx1 + 1) * s_m.cell - 1;
    fondo.y2 = s_m.off_y + (s_m.dy1 + 1) * s_m.cell - 1;
    fill(&layer, &fondo, 0x000000, 0);

    for (int y = s_m.dy0; y <= s_m.dy1; y++) {
        for (int x = s_m.dx0; x <= s_m.dx1; x++) {
            draw_cell(&layer, x, y);
        }
    }
    lv_canvas_finish_layer(s_m.canvas, &layer);
    /* Note: lv_canvas_finish_layer() ends with an lv_obj_invalidate(canvas) of
     * its own, so the flush to the screen is always of the whole canvas and
     * there is no way to narrow it from here. What the dirty rectangle saves
     * is the DRAWING -a few cells instead of 144-, not the flush. Since this
     * only runs when a finger touches something, that is enough; if it ever
     * animates, the board has to be drawn into the buffer by hand as the Maze
     * does, and not with lv_draw_*. */
}

/* -------------------------------------------------------------------------- */
/* Score                                                                       */

static void refresh_info(void)
{
    char buf[64];
    const char *estado = "";
    if (s_m.phase == GAME_WON) {
        estado = _("   GANASTE");
    } else if (s_m.phase == GAME_LOST) {
        estado = _("   BOOM");
    }

    char best[32] = "";
    if (s_m.best[s_m.level] > 0) {
        snprintf(best, sizeof(best), _("   mejor %d\""), s_m.best[s_m.level]);
    }

    snprintf(buf, sizeof(buf), "%d minas   %u\"%s%s",
             s_m.mines - s_m.flags, (unsigned)s_m.elapsed_s, best, estado);
    lv_label_set_text(s_m.lbl_info, buf);
}

/* -------------------------------------------------------------------------- */
/* Rules                                                                       */

static void place_mines(int safe)
{
    int total = s_m.n * s_m.n;
    int sx = safe % s_m.n, sy = safe / s_m.n;

    memset(s_m.mine, 0, sizeof(s_m.mine));
    int puestas = 0;
    while (puestas < s_m.mines) {
        int i = (int)(rnd() % (uint32_t)total);
        if (s_m.mine[i]) {
            continue;
        }
        /* The first cell and its eight neighbours are left clear: that way the
         * first touch always opens a region and the game is not decided on the
         * opening roll, which is how it has been played since Windows 3.1. */
        int x = i % s_m.n, y = i / s_m.n;
        if (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1) {
            continue;
        }
        s_m.mine[i] = 1;
        puestas++;
    }

    for (int y = 0; y < s_m.n; y++) {
        for (int x = 0; x < s_m.n; x++) {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if ((dx || dy) && inside(x + dx, y + dy) &&
                        s_m.mine[idx(x + dx, y + dy)]) {
                        n++;
                    }
                }
            }
            s_m.adj[idx(x, y)] = (uint8_t)n;
        }
    }
}

static void check_win(void)
{
    if (s_m.phase != GAME_RUN) {
        return;
    }
    if (s_m.opened != s_m.n * s_m.n - s_m.mines) {
        return;
    }
    s_m.phase  = GAME_WON;
    s_m.end_ms = lv_tick_get();

    int t = (int)s_m.elapsed_s;
    if (t > 0 && (s_m.best[s_m.level] == 0 || t < s_m.best[s_m.level])) {
        s_m.best[s_m.level] = t;
        char key[32];
        snprintf(key, sizeof(key), "mines_best%d", s_m.level);
        aos_hal_pref_set_i32(key, t);
    }

    /* the remaining mines flag themselves, which is the reward */
    for (int i = 0; i < s_m.n * s_m.n; i++) {
        if (s_m.mine[i] && s_m.state[i] != ST_FLAG) {
            s_m.state[i] = ST_FLAG;
            s_m.flags++;
        }
    }
    dirty_all();
    aos_hal_beep(880, 60);
    aos_hal_beep(1320, 120);
}

/* Cascading reveal, with an explicit stack: a 144-level recursion in the LVGL
 * task's 20 KB of stack is exactly the kind of thing that on the board does
 * not give a clean error. */
static void open_cell(int x, int y)
{
    int first = idx(x, y);
    if (s_m.state[first] != ST_HIDDEN) {
        return;
    }

    /* It is marked on PUSHING, not on popping. Marking on popping, one cell
     * enters the stack up to eight times (once per neighbour) and the stack
     * -which is the size of the board- overflows on a large region. With the
     * mark set before pushing, each cell enters exactly once and MAX_CELLS is
     * an exact bound. */
    int top = 0;
    s_m.state[first] = ST_OPEN;
    s_m.opened++;
    dirty_cell(x, y);
    s_m.stack[top++] = (int16_t)first;

    while (top > 0) {
        int i  = s_m.stack[--top];
        int cx = i % s_m.n, cy = i / s_m.n;

        if (s_m.mine[i]) {      /* it can only be the first: a cell at zero
                                 * never has mines around it */
            s_m.boom   = i;
            s_m.phase  = GAME_LOST;
            s_m.end_ms = lv_tick_get();
            dirty_all();
            aos_hal_beep(140, 350);
            return;
        }
        if (s_m.adj[i] != 0) {
            continue;
        }
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) {
                    continue;
                }
                if (!inside(cx + dx, cy + dy)) {
                    continue;
                }
                int j = idx(cx + dx, cy + dy);
                if (s_m.state[j] != ST_HIDDEN) {
                    continue;   /* the flags are honoured */
                }
                s_m.state[j] = ST_OPEN;
                s_m.opened++;
                dirty_cell(cx + dx, cy + dy);
                s_m.stack[top++] = (int16_t)j;
            }
        }
    }
    check_win();
}

static void toggle_flag(int x, int y)
{
    int i = idx(x, y);
    if (s_m.state[i] == ST_OPEN || s_m.phase == GAME_LOST || s_m.phase == GAME_WON) {
        return;
    }
    if (s_m.state[i] == ST_FLAG) {
        s_m.state[i] = ST_HIDDEN;
        s_m.flags--;
        aos_hal_beep(520, 25);
    } else {
        s_m.state[i] = ST_FLAG;
        s_m.flags++;
        aos_hal_beep(980, 25);
    }
    dirty_cell(x, y);
    refresh_info();
}

/* Touching an already revealed number that has as many flags around it as the
 * number says opens the remaining neighbours. It is the classic "chord" and it
 * is what makes a minesweeper playable with a finger. */
static void chord(int x, int y)
{
    int i = idx(x, y);
    if (s_m.adj[i] == 0) {
        return;
    }
    int marcadas = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx || dy) && inside(x + dx, y + dy) &&
                s_m.state[idx(x + dx, y + dy)] == ST_FLAG) {
                marcadas++;
            }
        }
    }
    if (marcadas != s_m.adj[i]) {
        return;
    }
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx || dy) && inside(x + dx, y + dy) &&
                s_m.state[idx(x + dx, y + dy)] == ST_HIDDEN) {
                open_cell(x + dx, y + dy);
                if (s_m.phase == GAME_LOST) {
                    return;
                }
            }
        }
    }
}

static void new_game(void)
{
    s_m.n     = LEVEL_N[s_m.level];
    s_m.mines = LEVEL_MINES[s_m.level];
    /* The largest cell that fits in the canvas, and the board centred in
     * whatever is left over. The board used to be a fixed 360 px and the cell
     * came out exact for all three sides; now the height rules and it does not
     * always divide. */
    s_m.cell  = CV_H / s_m.n;
    s_m.off_x = (CV_W - s_m.n * s_m.cell) / 2;
    s_m.off_y = (CV_H - s_m.n * s_m.cell) / 2;
    s_m.opened = 0;
    s_m.flags  = 0;
    s_m.boom   = -1;
    s_m.phase  = GAME_READY;
    s_m.elapsed_s = 0;
    s_m.start_ms  = lv_tick_get();
    memset(s_m.mine, 0, sizeof(s_m.mine));
    memset(s_m.adj, 0, sizeof(s_m.adj));
    memset(s_m.state, ST_HIDDEN, sizeof(s_m.state));

    dirty_all();
    refresh_info();
}

/* -------------------------------------------------------------------------- */
/* Input                                                                       */

static bool touch_cell(lv_event_t *event, int *out_x, int *out_y)
{
    (void)event;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t co;
    lv_obj_get_coords(s_m.canvas, &co);
    int x = (p.x - co.x1 - s_m.off_x) / s_m.cell;
    int y = (p.y - co.y1 - s_m.off_y) / s_m.cell;
    if (p.x - co.x1 - s_m.off_x < 0 || p.y - co.y1 - s_m.off_y < 0 ||
        !inside(x, y)) {
        return false;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static void canvas_click_cb(lv_event_t *event)
{
    if (s_m.long_done) {
        /* LVGL sends CLICKED on release even after having sent LONG_PRESSED:
         * without this guard, placing a flag also digs. */
        s_m.long_done = false;
        return;
    }
    int x, y;
    if (!touch_cell(event, &x, &y)) {
        return;
    }
    if (s_m.phase == GAME_LOST || s_m.phase == GAME_WON) {
        /* A moment of deafness after finishing: otherwise the same clumsy
         * finger that stepped on the mine starts the next game and you do not
         * even get to see where the others were. */
        if ((uint32_t)(lv_tick_get() - s_m.end_ms) > 900u) {
            new_game();
            present();
        }
        return;
    }
    if (s_m.flag_mode) {
        /* Flagging does not start the game: the board has no mines yet and
         * placing them before the first dig would defeat the guarantee that
         * the first touch always opens. */
        toggle_flag(x, y);
        present();
        return;
    }

    if (s_m.phase == GAME_READY) {
        place_mines(idx(x, y));
        s_m.phase    = GAME_RUN;
        s_m.start_ms = lv_tick_get();
    }

    int i = idx(x, y);
    if (s_m.state[i] == ST_FLAG) {
        present();
        return;                 /* a flagged cell is not dug with one tap */
    }
    if (s_m.state[i] == ST_OPEN) {
        chord(x, y);
    } else {
        open_cell(x, y);
    }
    refresh_info();
    present();
}

static void canvas_long_cb(lv_event_t *event)
{
    s_m.long_done = true;
    int x, y;
    if (!touch_cell(event, &x, &y)) {
        return;
    }
    if (s_m.phase == GAME_READY || s_m.phase == GAME_RUN) {
        toggle_flag(x, y);
        present();
    }
}

static void mode_cb(lv_event_t *event)
{
    (void)event;
    s_m.flag_mode = !s_m.flag_mode;
    lv_label_set_text(s_m.lbl_mode, s_m.flag_mode ? _("BANDERA") : _("CAVAR"));
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_m.lbl_mode),
                              s_m.flag_mode ? AOS_C_ORANGE : AOS_C_CARD2, 0);
}

static void level_cb(lv_event_t *event)
{
    (void)event;
    s_m.level = (s_m.level + 1) % 3;
    aos_hal_pref_set_i32("mines_level", s_m.level);
    lv_label_set_text(s_m.lbl_level, LEVEL_NAME[s_m.level]);
    new_game();
    present();
}

static void new_cb(lv_event_t *event)
{
    (void)event;
    new_game();
    present();
}

/* The clock: once a second and only if the second changed. The label is
 * OUTSIDE the canvas, so every write is a separate invalid area. */
static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_m.phase != GAME_RUN) {
        return;
    }
    uint32_t s = (lv_tick_get() - s_m.start_ms) / 1000u;
    if (s == s_m.elapsed_s || s > 5999) {
        return;
    }
    s_m.elapsed_s = s;
    refresh_info();
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *bar_button(lv_obj_t *parent, const char *text, int32_t x,
                            int32_t w, lv_color_t color, lv_event_cb_t cb,
                            lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, 40);
    lv_obj_set_pos(btn, x, 8);
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
    (void)self;
    memset(&s_m, 0, sizeof(s_m));
    s_m.rng  = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    s_m.boom = -1;

    /* Development switch: on the board getenv() always returns NULL. The first
     * character is inspected and not just the pointer, because an exported
     * empty variable returns "" and not NULL. */
    {
        const char *xray = getenv("MINES_XRAY");
        s_m.xray = (xray && xray[0] && xray[0] != '0');
    }

    int32_t v = 0;
    if (aos_hal_pref_get_i32("mines_level", &v) && v >= 0 && v < 3) {
        s_m.level = (int)v;
    }
    for (int i = 0; i < 3; i++) {
        char key[32];
        snprintf(key, sizeof(key), "mines_best%d", i);
        if (aos_hal_pref_get_i32(key, &v) && v > 0) {
            s_m.best[i] = (int)v;
        }
    }

    /* The large buffers through malloc(): with CONFIG_SPIRAM_USE_MALLOC they
     * go to PSRAM, of which there is plenty. What has to be looked after is
     * internal RAM. */
    s_m.buf = malloc((size_t)CV_W * CV_H * 2);
    if (!s_m.buf) {
        aos_ui_toast(_("Sin memoria"), 2000);
        return NULL;
    }
    memset(s_m.buf, 0, (size_t)CV_W * CV_H * 2);

    lv_obj_t *page = aos_page(root);

    s_m.canvas = lv_canvas_create(page);
    lv_canvas_set_buffer(s_m.canvas, s_m.buf, CV_W, CV_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_m.canvas, CV_W, CV_H);
    lv_obj_set_pos(s_m.canvas, 0, CANVAS_Y);
    /* The theme gives everything a radius, and on a canvas that forces
     * clipping with a mask and drawing in layers. */
    lv_obj_set_style_radius(s_m.canvas, 0, 0);
    lv_image_set_antialias(s_m.canvas, false);
    lv_obj_add_flag(s_m.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_m.canvas, canvas_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_m.canvas, canvas_long_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, AOS_SCREEN_W, BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    /* The information line does NOT hang off the bar: it goes right at the
     * bottom, in the strip the touch panel cannot reach. It is text and nobody
     * touches it. */
    s_m.lbl_info = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                   AOS_SCREEN_W, INFO_H);
    lv_obj_set_pos(s_m.lbl_info, 0, INFO_Y);

    bar_button(bar, LEVEL_NAME[s_m.level], 12, 96, AOS_C_CARD2, level_cb,
               &s_m.lbl_level);
    bar_button(bar, _("CAVAR"), 120, 128, AOS_C_CARD2, mode_cb, &s_m.lbl_mode);
    bar_button(bar, _("NUEVO"), 260, 96, AOS_C_ACCENT, new_cb, NULL);

    new_game();
    present();

    s_m.timer = lv_timer_create(tick_cb, 200, NULL);
    return &s_m;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_m.timer) {
        lv_timer_delete(s_m.timer);
        s_m.timer = NULL;
    }
    /* The objects first, with the context still standing: the canvas points at
     * a buffer we are about to free. */
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    free(s_m.buf);
    memset(&s_m, 0, sizeof(s_m));
}

static bool buscaminas_init(aos_app_t *app)
{
    app->desc.id       = "aos.mines";
    app->desc.name     = "Buscaminas";
    app->desc.icon     = "Bm";
    app->desc.icon_vec = AOS_ICON_MINES;
    app->desc.color_a  = 0x5A5A5E;
    app->desc.color_b  = 0x1C1C1E;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN;
    app->desc.order    = 154;

    app->create        = create;
    app->destroy       = destroy;
    return true;
}

AOS_APP_ENTRY(buscaminas_init);
