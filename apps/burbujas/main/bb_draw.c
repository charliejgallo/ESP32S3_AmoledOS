/*
 * BURBUJAS - the drawing (see burbujas.h)
 *
 * Topos's compositor, with one difference that is the whole design of this
 * app: THE BOARD IS NOT A SLOT, IT IS THE BACKGROUND. Fifty bubbles hanging
 * still are what a bubble shooter mostly is, and none of them cost anything
 * per frame; when a cell changes, bb_game.c marks its rectangle and this file
 * repaints that rectangle of the background from the backdrop plus whatever
 * bubbles reach into it.
 *
 * On top of that background go the slots -the shot, the guide, the bursts,
 * the falls, the launcher- with Topos's rule unchanged: a slot is drawn from
 * its bb_dp_t and nothing else, so if the parameters did not change neither
 * did the pixels, and what did change is rebuilt whole. The harness compares
 * every frame against a full redraw, which is the only way to know that is
 * true.
 */
#include "bb_art.h"
#include "burbujas.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Colours of everything that is not a bubble
 * -------------------------------------------------------------------------- */
#define C_GAP           0x24252B        /* between the backdrop's tiles       */
#define C_TILE_A        0x161719
#define C_TILE_B        0x1B1C21
#define C_GAP_LOW       0x1A1B20        /* the launcher's strip, a bit darker */
#define C_TILE_LOW_A    0x101114
#define C_TILE_LOW_B    0x141519
#define C_HUD           0x0B0B0F
#define C_HUD_EDGE      0x2A2C34
#define C_LINE          0xB9C0D0
#define C_LINE_DIM      0x40444E
#define C_DANGER        0xFF453A
#define C_PLATE         0x4A505C
#define C_PLATE_LIT     0x8A93A3
#define C_PLATE_DARK    0x272B33
#define C_PIPE_LIT      0x9AA3B2
#define C_PIPE          0x6E7684
#define C_PIPE_DARK     0x3A404B
#define C_RIM           0xB2BAC8
#define C_PTR           0xE6ECF8
#define C_PTR_EDGE      0x5A6376
#define C_METER_ON      0xD8DCE6
#define C_METER_OFF     0x41454F
#define C_TEXT_OL       0x07080B
#define C_DIM           0x8A90A0
#define C_RING          0xBFD8FF

#define TILE_PITCH      12
#define TILE_SIZE       10

#define METER_X         176
#define METER_Y         206
#define METER_GAP       9

static inline int field_y0(const bb_game_t *g)
{
    return g->state == GS_TITLE ? 0 : BB_HUD_H;
}

/* --------------------------------------------------------------------------
 * The backdrop: the grid of dark tiles of the photo
 *
 * Procedural, which is what makes it repaintable by rectangle: rebuilding a
 * 22x22 patch has to give the same pixels as painting the whole screen, or
 * every burst would leave a seam.
 * -------------------------------------------------------------------------- */

static void backdrop(bb_buf_t *b, const bb_rect_t *r)
{
    /* two grounds: the board's and the launcher's, split at the line */
    const int split = BB_DEAD_Y + 2;

    bb_rect(b, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0, bb_rgb(C_GAP));
    if (r->y1 > split) {
        bb_rect(b, r->x0, split, r->x1 - r->x0, r->y1 - split, bb_rgb(C_GAP_LOW));
    }

    const int i0 = (r->x0 - 2) / TILE_PITCH - 1, i1 = (r->x1 - 2) / TILE_PITCH + 1;
    const int j0 = (r->y0 - 2) / TILE_PITCH - 1, j1 = (r->y1 - 2) / TILE_PITCH + 1;

    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            int x = 2 + i * TILE_PITCH;
            int y = 2 + j * TILE_PITCH;
            if (x + TILE_SIZE < r->x0 || x >= r->x1 ||
                y + TILE_SIZE < r->y0 || y >= r->y1) {
                continue;
            }
            bool low = y >= split;
            uint32_t c = ((i + j) & 1)
                       ? (low ? C_TILE_LOW_A : C_TILE_A)
                       : (low ? C_TILE_LOW_B : C_TILE_B);
            bb_round(b, x, y, TILE_SIZE, TILE_SIZE, 2, bb_rgb(c));
        }
    }
}

/* The plate the ceiling comes down with, in the levels mode. */
static void plate(bb_buf_t *b, const bb_game_t *g)
{
    int bottom = g->board_top + g->slide;
    if (bottom <= BB_CEIL_Y) {
        return;
    }
    int h = bottom - BB_CEIL_Y;
    bb_rect(b, 0, BB_CEIL_Y, BB_W, h, bb_rgb(C_PLATE));
    bb_rect(b, 0, BB_CEIL_Y, BB_W, 1, bb_rgb(C_PLATE_LIT));
    bb_rect(b, 0, bottom - 2, BB_W, 2, bb_rgb(C_PLATE_DARK));

    /* rivets, one per bubble column and one per row it has come down */
    for (int y = BB_CEIL_Y + BB_ROW_H / 2; y < bottom - 3; y += BB_ROW_H) {
        for (int x = 15; x < BB_W; x += BB_D) {
            bb_rect(b, x - 1, y - 1, 2, 2, bb_rgb(C_PLATE_LIT));
        }
    }
}

static void board_paint(bb_buf_t *b, const bb_game_t *g, const bb_rect_t *r)
{
    for (int row = 0; row < BB_ROWS; row++) {
        int cy = bb_cell_y(g, row) + g->slide;
        if (cy + BB_R < r->y0 || cy - BB_R >= r->y1) {
            continue;
        }
        for (int c = 0; c < bb_ncols(g, row); c++) {
            if (!g->cell[row][c]) {
                continue;
            }
            int cx = bb_cell_x(g, row, c);
            if (cx + BB_R < r->x0 || cx - BB_R >= r->x1) {
                continue;
            }
            int color = row >= g->grey_from ? BC_GREY : g->cell[row][c];
            bb_bubble(b, cx, cy, color, BS_NORMAL, 0);
        }
    }
}

/* The title's scene: a cluster hanging where the buttons are not. */
static void title_decor(bb_buf_t *b)
{
    static const uint8_t ROW[4][8] = {
        { BC_RED, BC_ORANGE, BC_YELLOW, BC_GREEN, BC_BLUE, BC_PURPLE, BC_RED, BC_ORANGE },
        { BC_GREEN, BC_BLUE, BC_PURPLE, BC_RED, BC_YELLOW, BC_GREEN, BC_BLUE, 0 },
        { BC_PURPLE, BC_YELLOW, BC_RED, BC_BLUE, BC_ORANGE, BC_GREEN, 0, 0 },
        { BC_BLUE, BC_GREEN, BC_YELLOW, BC_PURPLE, 0, 0, 0, 0 },
    };
    static const uint8_t N[4] = { 8, 7, 6, 4 };

    for (int r = 0; r < 4; r++) {
        int y = 40 + r * BB_ROW_H;
        int w = N[r] * BB_D - (r & 1 ? 0 : 0);
        int x0 = (BB_W - w) / 2 + BB_R;
        for (int c = 0; c < N[r]; c++) {
            bb_bubble(b, x0 + c * BB_D, y, ROW[r][c], BS_NORMAL, 0);
        }
    }
}

static void bg_paint(bb_game_t *g, const bb_rect_t *r)
{
    bb_buf_t *b = &g->bg;
    bb_rect_t c = *r;

    if (c.x0 < 0) c.x0 = 0;
    if (c.y0 < 0) c.y0 = 0;
    if (c.x1 > BB_W) c.x1 = BB_W;
    if (c.y1 > BB_H) c.y1 = BB_H;
    if (c.x1 <= c.x0 || c.y1 <= c.y0) {
        return;
    }

    bb_clip(b, c.x0, c.y0, c.x1, c.y1);
    backdrop(b, &c);
    if (g->state == GS_TITLE) {
        title_decor(b);
    } else {
        plate(b, g);
        board_paint(b, g, &c);
    }
    bb_clip_none(b);
}

/* --------------------------------------------------------------------------
 * Painting a slot
 * -------------------------------------------------------------------------- */

static void popup_text(char *dst, unsigned value)
{
    dst[0] = '+';
    bb_num(dst + 1, value, 1);
}

/* A dot of the guide. The cross is the light tone and only the corners are
 * blended: with the arms in the base tone it read as a plus sign at x2 and
 * not as a dot. */
static void dot_paint(bb_buf_t *b, int x, int y, int color)
{
    uint16_t base = bb_col_base(color), light = bb_col_light(color);

    bb_px(b, x, y - 1, light);
    bb_px(b, x - 1, y, light);
    bb_px(b, x + 1, y, light);
    bb_px(b, x, y + 1, light);
    bb_px(b, x, y, light);
    bb_px_mix(b, x - 1, y - 1, base, 9);
    bb_px_mix(b, x + 1, y - 1, base, 9);
    bb_px_mix(b, x - 1, y + 1, base, 9);
    bb_px_mix(b, x + 1, y + 1, base, 9);
}

/* A filled triangle, by the sign of the three cross products. Used for the
 * launcher's pointer, which is the only diagonal thing in the app. */
static void tri_fill(bb_buf_t *b, int x0, int y0, int x1, int y1, int x2,
                     int y2, uint16_t c)
{
    int xa = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int xb = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int ya = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int yb = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    for (int y = ya; y <= yb; y++) {
        for (int x = xa; x <= xb; x++) {
            int a = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            int d = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            int e = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if ((a >= 0 && d >= 0 && e >= 0) || (a <= 0 && d <= 0 && e <= 0)) {
                bb_px(b, x, y, c);
            }
        }
    }
}

static void pointer_paint(bb_buf_t *b, int vx, int vy)
{
    const int cx = BB_LAUNCH_X, cy = BB_LAUNCH_Y;
    /* unit vector and its perpendicular, in 1/1024 */
    const int ux = vx, uy = vy, px = -vy, py = vx;

    int tipx = cx + ux * 19 / BB_FP, tipy = cy + uy * 19 / BB_FP;
    int basx = cx + ux * 13 / BB_FP, basy = cy + uy * 13 / BB_FP;
    tri_fill(b, tipx, tipy,
             basx + px * 4 / BB_FP, basy + py * 4 / BB_FP,
             basx - px * 4 / BB_FP, basy - py * 4 / BB_FP, bb_rgb(C_PTR));

    /* the tail, so the launcher reads as a needle and not as an arrow that
     * lost its stick */
    int tailx = cx - ux * 17 / BB_FP, taily = cy - uy * 17 / BB_FP;
    int tbx = cx - ux * 12 / BB_FP, tby = cy - uy * 12 / BB_FP;
    tri_fill(b, tailx, taily,
             tbx + px * 3 / BB_FP, tby + py * 3 / BB_FP,
             tbx - px * 3 / BB_FP, tby - py * 3 / BB_FP, bb_rgb(C_PTR_EDGE));
}

static void next_paint(bb_buf_t *b, int bx, int color, int phase)
{
    bb_bubble(b, bx, BB_PIPE_Y, color, BS_NORMAL, phase);

    /* the pipe, painted OVER the bubble: that is what makes it come out of
     * it instead of sitting next to it */
    const int y = BB_PIPE_Y;
    bb_rect(b, 0, y - 10, 25, 3, bb_rgb(C_PIPE_LIT));
    bb_rect(b, 0, y - 7, 25, 8, bb_rgb(C_PIPE));
    bb_rect(b, 0, y + 1, 25, 5, bb_rgb(C_PIPE_DARK));
    bb_rect(b, 0, y + 6, 25, 4, bb_rgb(C_PLATE_DARK));
    bb_rect(b, 25, y - 12, 4, 24, bb_rgb(C_RIM));
    bb_rect(b, 25, y + 6, 4, 6, bb_rgb(C_PIPE));
}

static void meter_paint(bb_buf_t *b, int total, int left, int red)
{
    for (int i = 0; i < total && i < 8; i++) {
        int x = METER_X - i * METER_GAP;
        if (i < left) {
            uint16_t c = red ? bb_rgb(C_DANGER) : bb_rgb(C_METER_ON);
            bb_disc(b, x, METER_Y, 3, c);
        } else {
            bb_ring(b, x, METER_Y, 3, bb_rgb(C_METER_OFF));
        }
    }
}

void bb_slot_paint(bb_buf_t *b, const bb_dp_t *p)
{
    switch (p->kind) {
    case DK_LINE: {
        uint16_t c = p->v[0] ? bb_rgb(C_DANGER) : bb_rgb(C_LINE);
        bb_rect(b, BB_WALL_L, BB_DEAD_Y, BB_WALL_R - BB_WALL_L, 1, c);
        bb_rect(b, BB_WALL_L, BB_DEAD_Y + 1, BB_WALL_R - BB_WALL_L, 1,
                bb_rgb(C_LINE_DIM));
        break;
    }
    case DK_GHOST:
        bb_bubble(b, p->x, p->y, p->v[0], BS_GHOST, 0);
        break;

    case DK_DOT:
        dot_paint(b, p->x, p->y, p->v[0]);
        break;

    case DK_BUBBLE:
        bb_bubble(b, p->x, p->y, p->v[0], BS_NORMAL, p->v[1]);
        break;

    case DK_POP:
        bb_pop_draw(b, p->x, p->y, p->v[0], p->v[1], p->v[2]);
        break;

    case DK_BLAST:
        bb_blast_draw(b, p->x, p->y, p->v[1]);
        break;

    case DK_POPUP: {
        char t[8];
        popup_text(t, (unsigned)p->w0);
        bb_text_ol(b, p->x - bb_text_w(t, 1) / 2, p->y, t,
                   bb_col_light(p->v[0]), bb_rgb(C_TEXT_OL), 1);
        break;
    }
    case DK_PTR:
        pointer_paint(b, p->w0, p->w1);
        break;

    case DK_NEXT:
        next_paint(b, p->x, p->v[0], p->v[1]);
        break;

    case DK_METER:
        meter_paint(b, p->v[0], p->v[1], p->v[2]);
        break;

    case DK_RING:
        bb_wave(b, p->x, p->y, p->v[0], 1, bb_rgb(C_RING), p->v[1]);
        break;

    default:
        break;
    }
}

void bb_slot_box(const bb_dp_t *p, bb_rect_t *out, bool *any)
{
    bb_rect_t r = { 0, 0, 0, 0 };
    bool ok = true;

    switch (p->kind) {
    case DK_LINE:
        r.x0 = BB_WALL_L;
        r.y0 = BB_DEAD_Y;
        r.x1 = BB_WALL_R;
        r.y1 = BB_DEAD_Y + 2;
        break;

    case DK_GHOST:
    case DK_BUBBLE:
        bb_bubble_box(p->v[0], p->x, p->y, &r);
        break;

    case DK_DOT:
        r.x0 = (int16_t)(p->x - 1);
        r.y0 = (int16_t)(p->y - 1);
        r.x1 = (int16_t)(p->x + 2);
        r.y1 = (int16_t)(p->y + 2);
        break;

    case DK_POP:
        if (p->v[1] < 2) {
            bb_bubble_box(p->v[0], p->x, p->y, &r);
        } else {
            r.x0 = (int16_t)(p->x - BB_POP_REACH);
            r.y0 = (int16_t)(p->y - BB_POP_REACH);
            r.x1 = (int16_t)(p->x + BB_POP_REACH + 1);
            r.y1 = (int16_t)(p->y + BB_POP_REACH + 1);
        }
        break;

    case DK_BLAST:
        r.x0 = (int16_t)(p->x - BB_BLAST_REACH);
        r.y0 = (int16_t)(p->y - BB_BLAST_REACH);
        r.x1 = (int16_t)(p->x + BB_BLAST_REACH + 1);
        r.y1 = (int16_t)(p->y + BB_BLAST_REACH + 1);
        break;

    case DK_POPUP: {
        char t[8];
        popup_text(t, (unsigned)p->w0);
        int w = bb_text_w(t, 1);
        r.x0 = (int16_t)(p->x - w / 2 - 1);
        r.y0 = (int16_t)(p->y - 1);
        r.x1 = (int16_t)(p->x - w / 2 + w + 2);
        r.y1 = (int16_t)(p->y + BB_CH_H + 3);
        break;
    }
    case DK_PTR:
        r.x0 = (int16_t)(BB_LAUNCH_X - 21);
        r.y0 = (int16_t)(BB_LAUNCH_Y - 21);
        r.x1 = (int16_t)(BB_LAUNCH_X + 21);
        r.y1 = (int16_t)(BB_LAUNCH_Y + 21);
        break;

    case DK_NEXT:
        /* The pipe is painted by this slot too, and it reaches x=29 whatever
         * the bubble is doing: while the next one slides out, the bubble is
         * the narrower of the two. */
        r.x0 = 0;
        r.y0 = (int16_t)(BB_PIPE_Y - 20);
        r.x1 = (int16_t)(p->x + BB_R + 1 > 29 ? p->x + BB_R + 1 : 29);
        r.y1 = (int16_t)(BB_PIPE_Y + 13);
        break;

    case DK_METER:
        r.x0 = (int16_t)(METER_X - 7 * METER_GAP - 4);
        r.y0 = (int16_t)(METER_Y - 4);
        r.x1 = (int16_t)(METER_X + 4);
        r.y1 = (int16_t)(METER_Y + 4);
        break;

    case DK_RING:
        r.x0 = (int16_t)(p->x - p->v[0] - 2);
        r.y0 = (int16_t)(p->y - p->v[0] - 2);
        r.x1 = (int16_t)(p->x + p->v[0] + 3);
        r.y1 = (int16_t)(p->y + p->v[0] + 3);
        break;

    default:
        ok = false;
        break;
    }

    *out = r;
    if (any) {
        *any = ok;
    }
}

/* --------------------------------------------------------------------------
 * Building this frame's slots
 * -------------------------------------------------------------------------- */

static void slot_put(bb_game_t *g, int idx, const bb_dp_t *p)
{
    bool any = false;
    g->slot[idx].p = *p;
    bb_slot_box(p, &g->slot[idx].box, &any);
    g->slot[idx].vis = any;
}

static int lerp(int a, int b, int num, int den)
{
    return a + (b - a) * num / den;
}

static void build_slots(bb_game_t *g)
{
    bb_dp_t p;
    memset(g->slot, 0, sizeof(g->slot));

    if (g->state == GS_TITLE) {
        for (int i = 0; i < BB_TITLE_RINGS; i++) {
            uint32_t t = (g->title_t + (uint32_t)i * 840u) % 4200u;
            memset(&p, 0, sizeof(p));
            p.kind = DK_RING;
            p.x    = (int16_t)(26 + i * 33 +
                               bb_sin((int)(t / 25) + i * 40) * 6 / 256);
            p.y    = (int16_t)(BB_H - (int)(t * (BB_H + 24) / 4200));
            p.v[0] = (uint8_t)(3 + i % 3);
            p.v[1] = 7;
            slot_put(g, BB_SLOT_RING0 + i, &p);
        }
        return;
    }

    const bool playing = g->state == GS_PLAY;

    /* the line, which blinks when the board is one row away from it */
    memset(&p, 0, sizeof(p));
    p.kind = DK_LINE;
    p.v[0] = (uint8_t)(g->danger && ((g->elapsed_ms / 350) & 1));
    slot_put(g, BB_SLOT_LINE, &p);

    /* the guide */
    if (playing && g->aiming && g->aim_ok) {
        if (g->ghost_r >= 0) {
            memset(&p, 0, sizeof(p));
            p.kind = DK_GHOST;
            p.v[0] = g->cur == BC_BOMB || g->cur == BC_RAINBOW ? BC_RAINBOW : g->cur;
            p.x    = (int16_t)bb_cell_x(g, g->ghost_r, g->ghost_c);
            p.y    = (int16_t)(bb_cell_y(g, g->ghost_r) + g->slide);
            slot_put(g, BB_SLOT_GHOST, &p);
        }
        for (int i = 0; i < g->ndots && i < BB_MAX_DOTS; i++) {
            memset(&p, 0, sizeof(p));
            p.kind = DK_DOT;
            p.v[0] = g->cur;
            p.x    = g->dotx[i];
            p.y    = g->doty[i];
            slot_put(g, BB_SLOT_DOT0 + i, &p);
        }
    }

    /* effects */
    for (int i = 0; i < BB_MAX_FX; i++) {
        const bb_fx_t *f = &g->fx[i];
        if (!f->kind) {
            continue;
        }
        memset(&p, 0, sizeof(p));
        p.x = (int16_t)((f->x + 8) >> 4);
        p.y = (int16_t)((f->y + 8) >> 4);

        if (f->delay > 0) {
            p.kind = DK_BUBBLE;         /* still there, waiting its turn */
            p.v[0] = f->color;
        } else if (f->kind == FX_POP) {
            p.kind = DK_POP;
            p.v[0] = f->color;
            p.v[1] = (uint8_t)(f->t / BB_POP_MS);
            p.v[2] = f->seed;
            if (p.v[1] >= BB_POP_FRAMES) {
                continue;
            }
        } else if (f->kind == FX_BLAST) {
            p.kind = DK_BLAST;
            p.v[1] = (uint8_t)(f->t / BB_BLAST_MS);
            if (p.v[1] >= BB_BLAST_FRAMES) {
                continue;
            }
        } else {
            p.kind = DK_BUBBLE;
            p.v[0] = f->color;
        }
        slot_put(g, BB_SLOT_FX0 + i, &p);
    }

    /* the shot in the air */
    if (playing && g->sub == PS_FLY) {
        memset(&p, 0, sizeof(p));
        p.kind = DK_BUBBLE;
        p.v[0] = g->shot_color;
        p.v[1] = (uint8_t)((g->elapsed_ms / 130) & 1);
        p.x    = (int16_t)(g->shot.x / BB_FP);
        p.y    = (int16_t)(g->shot.y / BB_FP);
        slot_put(g, BB_SLOT_SHOT, &p);
    }

    if (playing) {
        const int spark = (int)((g->elapsed_ms / 130) & 1);

        memset(&p, 0, sizeof(p));
        p.kind = DK_PTR;
        p.w0   = g->aim_vx;
        p.w1   = g->aim_vy;
        slot_put(g, BB_SLOT_PTR, &p);

        /* the launcher's bubble, sliding in from the pipe after a shot */
        memset(&p, 0, sizeof(p));
        p.kind = DK_BUBBLE;
        p.v[0] = g->cur;
        p.v[1] = (uint8_t)(g->cur == BC_BOMB ? spark : 0);
        if (g->reload_ms) {
            int done = BB_RELOAD_MS - g->reload_ms;
            p.x = (int16_t)lerp(BB_PIPE_X, BB_LAUNCH_X, done, BB_RELOAD_MS);
            p.y = (int16_t)lerp(BB_PIPE_Y, BB_LAUNCH_Y, done, BB_RELOAD_MS);
        } else {
            p.x = BB_LAUNCH_X;
            p.y = BB_LAUNCH_Y;
        }
        slot_put(g, BB_SLOT_CUR, &p);

        memset(&p, 0, sizeof(p));
        p.kind = DK_NEXT;
        p.v[0] = g->nxt;
        p.v[1] = (uint8_t)(g->nxt == BC_BOMB ? spark : 0);
        p.x    = (int16_t)(g->reload_ms
                           ? lerp(BB_PIPE_HIDDEN, BB_PIPE_X,
                                  BB_RELOAD_MS - g->reload_ms, BB_RELOAD_MS)
                           : BB_PIPE_X);
        slot_put(g, BB_SLOT_NEXT, &p);

        /* how many shots until the next row: dots by misses, or by the clock
         * in the timed mode */
        memset(&p, 0, sizeof(p));
        p.kind = DK_METER;
        if (g->mode == MODE_TIMED) {
            int left = g->push_every > 0
                     ? (int)((g->push_ms * 5 + g->push_every - 1) / g->push_every)
                     : 0;
            p.v[0] = 5;
            p.v[1] = (uint8_t)(left < 0 ? 0 : left > 5 ? 5 : left);
        } else {
            p.v[0] = g->miss_limit;
            p.v[1] = (uint8_t)(g->miss_limit - g->misses);
        }
        p.v[2] = (uint8_t)(p.v[1] <= 1 && ((g->elapsed_ms / 300) & 1));
        slot_put(g, BB_SLOT_METER, &p);
    }

    for (int i = 0; i < BB_MAX_POPUPS; i++) {
        if (!g->popup[i].on) {
            continue;
        }
        memset(&p, 0, sizeof(p));
        p.kind = DK_POPUP;
        p.v[0] = g->popup[i].color;
        p.w0   = (int16_t)g->popup[i].value;
        p.x    = g->popup[i].x;
        p.y    = (int16_t)(g->popup[i].y - g->popup[i].t * 14 / 700);
        slot_put(g, BB_SLOT_POP0 + i, &p);
    }
}

/* --------------------------------------------------------------------------
 * Composing
 * -------------------------------------------------------------------------- */

static bool changed(const bb_slot_t *a, const bb_slot_t *b)
{
    return a->vis != b->vis || (a->vis && memcmp(&a->p, &b->p, sizeof(a->p)));
}

static void add_rect(bb_dirty_t *d, const bb_rect_t *r, int y0)
{
    int ry0 = r->y0 < y0 ? y0 : r->y0;
    bb_dirty_add(d, r->x0, ry0, r->x1 - r->x0, r->y1 - ry0);
}

static void compose(bb_game_t *g, const bb_rect_t *r)
{
    const int y0 = field_y0(g);
    bb_rect_t c = *r;

    if (c.x0 < 0) c.x0 = 0;
    if (c.y0 < y0) c.y0 = (int16_t)y0;
    if (c.x1 > BB_W) c.x1 = BB_W;
    if (c.y1 > BB_H) c.y1 = BB_H;
    if (c.x1 <= c.x0 || c.y1 <= c.y0) {
        return;
    }

    bb_restore(g->fb.px, g->bg.px, &c);
    bb_clip(&g->fb, c.x0, c.y0, c.x1, c.y1);
    for (int i = 0; i < BB_SLOTS; i++) {
        if (g->slot[i].vis && bb_rect_hit(&g->slot[i].box, &c)) {
            bb_slot_paint(&g->fb, &g->slot[i].p);
        }
    }
    bb_clip_none(&g->fb);
}

/* --------------------------------------------------------------------------
 * The score's strip
 * -------------------------------------------------------------------------- */

static void icon_crown(bb_buf_t *b, int x, int y, uint16_t c)
{
    bb_rect(b, x, y + 3, 7, 2, c);
    bb_rect(b, x, y, 1, 3, c);
    bb_rect(b, x + 3, y + 1, 1, 2, c);
    bb_rect(b, x + 6, y, 1, 3, c);
}

static void icon_clock(bb_buf_t *b, int x, int y, uint16_t c)
{
    bb_ring(b, x + 4, y + 4, 4, c);
    bb_rect(b, x + 4, y + 1, 1, 4, c);
    bb_rect(b, x + 4, y + 4, 3, 1, c);
}

static void icon_flag(bb_buf_t *b, int x, int y, uint16_t c)
{
    bb_rect(b, x, y, 1, 9, c);
    bb_rect(b, x + 1, y, 6, 4, c);
}

static void hud_draw(bb_game_t *g)
{
    bb_buf_t *b = &g->fb;
    const uint16_t white = bb_rgb(0xFFFFFF), ol = bb_rgb(C_TEXT_OL);
    const uint16_t dim = bb_rgb(C_DIM), red = bb_rgb(C_DANGER);
    char buf[16];

    bb_clip(b, 0, 0, BB_W, BB_HUD_H);
    bb_rect(b, 0, 0, BB_W, BB_HUD_H, bb_rgb(C_HUD));
    bb_rect(b, 0, BB_HUD_H - 1, BB_W, 1, bb_rgb(C_HUD_EDGE));

    /* Pause, on the left, and deliberately in from the edge: the glass has a
     * 38 px radius (19 here) and the bezel eats a little more. The whole
     * corner is the button, in burbujas.c. */
    bb_round(b, 11, 4, 18, 16, 4, bb_rgb(0x1E1F26));
    bb_rect(b, 16, 8, 3, 8, white);
    bb_rect(b, 21, 8, 3, 8, white);

    bb_num(buf, g->hud.score, 1);
    bb_text_ol(b, BB_W / 2 - bb_text_w(buf, 2) / 2, 4, buf,
               g->hud.danger ? red : white, ol, 2);

    switch (g->hud.mode) {
    case MODE_TIMED:
        bb_num(buf, g->hud.right, 1);
        icon_clock(b, 170 - bb_text_w(buf, 2) - 11, 8, g->hud.red ? red : dim);
        bb_text_ol(b, 170 - bb_text_w(buf, 2), 4, buf,
                   g->hud.red ? red : white, ol, 2);
        break;

    case MODE_LEVELS:
        bb_num(buf, g->hud.right, 1);
        icon_flag(b, 170 - bb_text_w(buf, 2) - 10, 7, dim);
        bb_text_ol(b, 170 - bb_text_w(buf, 2), 4, buf, white, ol, 2);
        break;

    default:
        if (g->hud.right) {
            bb_num(buf, g->hud.right, 1);
            icon_crown(b, 172 - bb_text_w(buf, 1) - 9, 8, dim);
            bb_text(b, 172 - bb_text_w(buf, 1), 9, buf, dim, 1);
        }
        break;
    }

    if (g->show_fps) {
        char f[12];
        char *e = bb_num(f, (uint32_t)(g->fps10 / 10), 1);
        *e++ = '.';
        bb_num(e, (uint32_t)(g->fps10 % 10), 1);
        bb_text(b, 33, 16, f, bb_rgb(0x7BE9FF), 1);
    }

    bb_clip_none(b);
}

static void hud_update(bb_game_t *g)
{
    uint32_t right = 0;
    uint8_t  red = 0;

    switch (g->mode) {
    case MODE_TIMED:
        right = (uint32_t)((g->time_ms + 999) / 1000);
        red   = g->time_ms <= 10000 &&
                (g->time_ms > 5000 || ((g->time_ms / 250) & 1) == 0);
        break;
    case MODE_LEVELS: right = g->level; break;
    default:          right = g->best;  break;
    }

    if (g->hud_valid && g->hud.score == g->score && g->hud.right == right &&
        g->hud.mode == g->mode && g->hud.red == red &&
        g->hud.danger == g->danger &&
        !(g->show_fps && g->hud_fps != g->fps10)) {
        return;
    }
    g->hud.score  = g->score;
    g->hud.right  = right;
    g->hud.mode   = g->mode;
    g->hud.red    = red;
    g->hud.danger = g->danger;
    g->hud_fps    = g->fps10;
    g->hud_valid  = 1;
    hud_draw(g);
    g->hud_push = 1;
}

/* --------------------------------------------------------------------------
 * Public
 * -------------------------------------------------------------------------- */

void bb_bg_build(bb_game_t *g, bool title)
{
    bb_rect_t all = { 0, 0, BB_W, BB_H };

    g->state = title ? GS_TITLE : g->state;
    bg_paint(g, &all);
    memcpy(g->fb.px, g->bg.px, (size_t)BB_W * BB_H * sizeof(uint16_t));
    memset(g->prev, 0, sizeof(g->prev));
    bb_dirty_reset(&g->bgd);
    g->bg_all    = 0;
    g->full      = 1;
    g->hud_valid = 0;
}

void bb_present(bb_game_t *g)
{
    const int y0 = field_y0(g);

    /* First the background, where the still board lives: a cell that changed
     * marked its rectangle, and a slide marks the lot. */
    if (g->bg_all) {
        bb_rect_t all = { 0, 0, BB_W, BB_H };
        bg_paint(g, &all);
        bb_dirty_reset(&g->bgd);
        g->bg_all = 0;
        g->full   = 1;
    } else {
        for (int i = 0; i < g->bgd.n; i++) {
            bg_paint(g, &g->bgd.r[i]);
        }
    }

    build_slots(g);
    bb_dirty_reset(&g->push);

    if (g->full) {
        g->full = 0;
        bb_rect_t all = { 0, (int16_t)y0, BB_W, BB_H };
        compose(g, &all);
        bb_dirty_add(&g->push, 0, y0, BB_W, BB_H - y0);
    } else {
        for (int i = 0; i < g->bgd.n; i++) {
            add_rect(&g->push, &g->bgd.r[i], y0);
        }
        for (int i = 0; i < BB_SLOTS; i++) {
            const bb_slot_t *now = &g->slot[i], *was = &g->prev[i];
            if (!changed(now, was)) {
                continue;
            }
            if (was->vis) {
                add_rect(&g->push, &was->box, y0);
            }
            if (now->vis) {
                add_rect(&g->push, &now->box, y0);
            }
        }
        for (int i = 0; i < g->push.n; i++) {
            compose(g, &g->push.r[i]);
        }
    }

    bb_dirty_reset(&g->bgd);
    memcpy(g->prev, g->slot, sizeof(g->slot));

    g->hud_push = 0;
    if (g->state != GS_TITLE) {
        hud_update(g);
    }
}

void bb_render_full(bb_game_t *g, uint16_t *out)
{
    const int y0 = field_y0(g);
    memcpy(out, g->bg.px, (size_t)BB_W * BB_H * sizeof(uint16_t));

    bb_buf_t b;
    bb_buf_init(&b, out, BB_W, BB_H);
    bb_clip(&b, 0, y0, BB_W, BB_H);
    for (int i = 0; i < BB_SLOTS; i++) {
        if (g->slot[i].vis) {
            bb_slot_paint(&b, &g->slot[i].p);
        }
    }
    /* the score's strip is drawn apart: take it from fb as it is */
    memcpy(out, g->fb.px, (size_t)y0 * BB_W * sizeof(uint16_t));
}
