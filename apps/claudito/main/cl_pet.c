/*
 * Claudito - drawing the character (see cl_pet.h)
 */
#include "cl_pet.h"

#define COL_BODY    0xD97757        /* Claude's orange */
#define COL_LIGHT   0xE99B80
#define COL_DARK    0xB0523A
#define COL_EDGE    0x8E3F2C
#define COL_EYE     0x2A1A14
#define COL_WHITE   0xFFF6EC
#define COL_IN      0x7E2C22        /* inside of the mouth */
#define COL_TONGUE  0xE9647B
#define COL_BLUSH   0xF2917F
#define COL_DIRT    0x6B5138
#define COL_LOVE    0xFF4D6D

void cl_pet_init(cl_pet_t *p)
{
    p->x = CL_ART_W / 2;
    p->y = 60;
    p->squash = 0;
    p->lean   = 0;
    p->step   = 0;
    p->eye    = CL_EYE_OPEN;
    p->mouth  = CL_MOUTH_TOOTH;
    p->arm_l  = 0;
    p->arm_r  = 0;
    p->face_dx = 0;
    p->blush  = false;
    p->shadow = true;
    p->dirt   = 0;
}

/* How far each row is inset so the corners come out bitten off. It is the same
 * sum as cl_round(), but here it has to be done row by row so they can be
 * painted in different colours. */
static int row_inset(int yy, int h, int cut)
{
    if (yy < cut) {
        return cut - yy;
    }
    if (yy >= h - cut) {
        return cut - (h - 1 - yy);
    }
    return 0;
}

/* Geometry according to the pose: on squashing, the body widens and the legs
 * sink, which is what makes a jump read as a jump. */
static void geom(const cl_pet_t *p, int *bx, int *by, int *bw, int *bh, int *legh)
{
    int sq = p->squash;
    if (sq >  6) sq =  6;
    if (sq < -6) sq = -6;

    *bh   = CL_PET_BODY_H - sq;
    *bw   = CL_PET_BODY_W + sq;
    *legh = CL_PET_LEG_H - (sq > 0 ? (sq > 2 ? 2 : sq) : 0);
    *by   = p->y - *legh - *bh;
    *bx   = p->x - *bw / 2 + p->lean;
}

void cl_pet_bbox(const cl_pet_t *p, int *x, int *y, int *w, int *h)
{
    int bx, by, bw, bh, legh;
    geom(p, &bx, &by, &bw, &bh, &legh);
    *x = bx - 3;
    *y = by - 2;
    *w = bw + 6;
    *h = bh + legh + 2;
}

void cl_pet_mouth_at(const cl_pet_t *p, int *x, int *y)
{
    int bx, by, bw, bh, legh;
    geom(p, &bx, &by, &bw, &bh, &legh);
    *x = bx + bw / 2 + p->face_dx;
    *y = by + bh - 5;
}

static int arm_y(const cl_pet_t *p, int by, int bh, int state)
{
    (void)p;
    switch (state) {
    case 2:  return by - 3;             /* arms up, celebrating */
    case 1:  return by + bh / 2 - 3;
    default: return by + bh - 8;
    }
}

void cl_pet_hand_at(const cl_pet_t *p, int right, int *x, int *y)
{
    int bx, by, bw, bh, legh;
    geom(p, &bx, &by, &bw, &bh, &legh);
    int ay = arm_y(p, by, bh, right ? p->arm_r : p->arm_l);
    *x = right ? bx + bw + 2 : bx - 2;
    *y = ay + 1;
}

/* -------------------------------------------------------------------------- */

static void draw_eye(cl_buf_t *b, int x, int y, cl_eye_t kind)
{
    const uint16_t eye   = cl_rgb(COL_EYE);
    const uint16_t white = cl_rgb(COL_WHITE);
    const uint16_t love  = cl_rgb(COL_LOVE);

    switch (kind) {
    case CL_EYE_BLINK:
        cl_rect(b, x, y + 2, 3, 1, eye);
        break;

    case CL_EYE_HAPPY:
        cl_px(b, x + 1, y + 1, eye);
        cl_px(b, x,     y + 2, eye);
        cl_px(b, x + 2, y + 2, eye);
        cl_px(b, x,     y + 3, eye);
        cl_px(b, x + 2, y + 3, eye);
        break;

    case CL_EYE_SLEEP:
        cl_px(b, x,     y + 1, eye);
        cl_px(b, x + 2, y + 1, eye);
        cl_px(b, x + 1, y + 2, eye);
        break;

    case CL_EYE_SQUINT:
        cl_rect(b, x, y + 1, 3, 2, eye);
        break;

    case CL_EYE_WIDE:
        cl_rect(b, x - 1, y - 1, 5, 6, eye);
        cl_rect(b, x,     y,     3, 4, white);
        cl_rect(b, x + 1, y + 1, 2, 2, eye);
        break;

    case CL_EYE_LOOK_L:
        cl_rect(b, x - 1, y, 3, 4, eye);
        break;

    case CL_EYE_LOOK_R:
        cl_rect(b, x + 1, y, 3, 4, eye);
        break;

    case CL_EYE_LOVE:
        cl_px(b, x,     y,     love);
        cl_px(b, x + 2, y,     love);
        cl_rect(b, x - 1, y + 1, 5, 1, love);
        cl_rect(b, x,    y + 2, 3, 1, love);
        cl_px(b, x + 1, y + 3, love);
        break;

    case CL_EYE_DIZZY:
        cl_px(b, x,     y,     eye);
        cl_px(b, x + 2, y,     eye);
        cl_px(b, x + 1, y + 1, eye);
        cl_px(b, x,     y + 2, eye);
        cl_px(b, x + 2, y + 2, eye);
        break;

    case CL_EYE_OPEN:
    default:
        cl_rect(b, x, y, 3, 4, eye);
        cl_px(b, x + 2, y, white);      /* the little highlight that gives it its gaze */
        break;
    }
}

static void draw_mouth(cl_buf_t *b, int cx, int cy, cl_mouth_t kind, int phase)
{
    const uint16_t white  = cl_rgb(COL_WHITE);
    const uint16_t inside = cl_rgb(COL_IN);
    const uint16_t tongue = cl_rgb(COL_TONGUE);
    const uint16_t body   = cl_rgb(COL_BODY);
    const uint16_t dark   = cl_rgb(COL_EDGE);

    switch (kind) {
    case CL_MOUTH_TOOTH:
        /* the smile with the tooth from the original drawing */
        cl_rect(b, cx - 3, cy,     7, 2, white);
        cl_rect(b, cx - 2, cy + 2, 5, 1, white);
        cl_px(b, cx - 1, cy, body);
        cl_px(b, cx + 1, cy, body);
        break;

    case CL_MOUTH_OPEN:
        cl_rect(b, cx - 3, cy,     7, 1, white);
        cl_rect(b, cx - 3, cy + 1, 7, 3, inside);
        cl_rect(b, cx - 2, cy + 3, 5, 1, tongue);
        break;

    case CL_MOUTH_BIG:
        cl_rect(b, cx - 4, cy - 1, 9, 1, white);
        cl_rect(b, cx - 4, cy,     9, 5, inside);
        cl_rect(b, cx - 3, cy + 3, 7, 2, tongue);
        break;

    case CL_MOUTH_LAUGH:
        cl_rect(b, cx - 4, cy,     9, 1, white);
        cl_rect(b, cx - 4, cy + 1, 9, 4, inside);
        cl_rect(b, cx - 2, cy + 3, 5, 2, tongue);
        cl_px(b, cx - 4, cy + 4, body);
        cl_px(b, cx + 4, cy + 4, body);
        break;

    case CL_MOUTH_CHEW:
        /* chewing is the same mouth shifted one pixel each way */
        cl_rect(b, cx - 2 + phase, cy + 1, 5, 2, white);
        break;

    case CL_MOUTH_FLAT:
        cl_rect(b, cx - 2, cy + 1, 5, 1, dark);
        break;

    case CL_MOUTH_SAD:
        cl_rect(b, cx - 1, cy,     3, 1, dark);
        cl_px(b, cx - 2, cy + 1, dark);
        cl_px(b, cx + 2, cy + 1, dark);
        break;

    case CL_MOUTH_OH:
        cl_rect(b, cx - 1, cy,     3, 3, inside);
        cl_rect(b, cx - 2, cy + 1, 5, 1, inside);
        break;

    case CL_MOUTH_SMILE:
    default:
        cl_rect(b, cx - 3, cy,     7, 2, white);
        cl_rect(b, cx - 2, cy + 2, 5, 1, white);
        break;
    }
}

static void draw_arm(cl_buf_t *b, int x, int y, bool right)
{
    const uint16_t body = cl_rgb(COL_BODY);
    const uint16_t dark = cl_rgb(COL_DARK);

    cl_rect(b, x, y, 3, 4, body);
    cl_rect(b, x, y + 3, 3, 1, dark);
    /* the rounded tip points outwards */
    cl_px(b, right ? x + 2 : x, y, cl_rgb(COL_LIGHT));
}

void cl_pet_draw(cl_buf_t *b, const cl_pet_t *p)
{
    int bx, by, bw, bh, legh;
    geom(p, &bx, &by, &bw, &bh, &legh);

    const uint16_t body  = cl_rgb(COL_BODY);
    const uint16_t light = cl_rgb(COL_LIGHT);
    const uint16_t dark  = cl_rgb(COL_DARK);
    const uint16_t edge  = cl_rgb(COL_EDGE);

    /* contact shadow: darkening whatever is underneath works for any
     * background, grass or parquet, without needing an alpha channel */
    if (p->shadow) {
        cl_shade(b, p->x - (bw - 6) / 2,  p->y,     bw - 6,  1, -4);
        cl_shade(b, p->x - (bw - 14) / 2, p->y + 1, bw - 14, 1, -3);
    }

    /* legs: six of them, alternating which one lifts according to the step's
     * phase */
    for (int i = 0; i < 6; i++) {
        int lx   = bx + 2 + i * (bw - 6) / 5;
        int lift = ((i + p->step) & 1) && (p->step > 0) ? 1 : 0;
        int lh   = legh - lift;
        if (lh < 1) {
            lh = 1;
        }
        cl_rect(b, lx, p->y - legh, 2, lh, body);
        cl_px(b, lx,     p->y - legh + lh - 1, dark);
        cl_px(b, lx + 1, p->y - legh + lh - 1, dark);
    }

    /* body */
    for (int yy = 0; yy < bh; yy++) {
        int inset = row_inset(yy, bh, 2);
        uint16_t c = body;
        if (yy <= 1) {
            c = light;
        } else if (yy >= bh - 2) {
            c = dark;
        }
        cl_hline(b, bx + inset, by + yy, bw - 2 * inset, c);
    }
    /* right edge slightly darker: it gives volume without a black outline */
    for (int yy = 2; yy < bh - 2; yy++) {
        cl_px(b, bx + bw - 1, by + yy, dark);
    }
    cl_px(b, bx + bw - 1, by + 1,      edge);
    cl_px(b, bx + bw - 1, by + bh - 2, edge);

    /* arms */
    draw_arm(b, bx - 3,  arm_y(p, by, bh, p->arm_l), false);
    draw_arm(b, bx + bw, arm_y(p, by, bh, p->arm_r), true);

    /* dirt: fixed blotches so they do not dance between frames */
    if (p->dirt > 0) {
        const uint16_t dirt = cl_rgb(COL_DIRT);
        static const int8_t spots[3][2] = { { 3, 3 }, { 20, 11 }, { 13, 2 } };
        for (int i = 0; i < p->dirt && i < 3; i++) {
            int sx = bx + spots[i][0] * bw / CL_PET_BODY_W;
            int sy = by + spots[i][1];
            cl_rect(b, sx, sy, 3, 2, dirt);
            cl_px(b, sx + 3, sy + 1, dirt);
            cl_px(b, sx - 1, sy, dirt);
        }
    }

    /* face */
    int cx = bx + bw / 2 + p->face_dx;
    int ey = by + 5;
    draw_eye(b, cx - 6, ey, p->eye);
    draw_eye(b, cx + 4, ey, p->eye);

    if (p->blush) {
        const uint16_t blush = cl_rgb(COL_BLUSH);
        cl_rect(b, cx - 10, ey + 5, 4, 2, blush);
        cl_rect(b, cx + 7,  ey + 5, 4, 2, blush);
    }

    draw_mouth(b, cx, by + bh - 6, p->mouth, (p->step & 1) ? 1 : -1);
}
