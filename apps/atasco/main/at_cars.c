#include "at_cars.h"

/* --------------------------------------------------------------------------
 * Models
 *
 * Everything is drawn with rounded rectangles and linear GRADIENTS, which is
 * the only "pretty" thing LVGL does without creating a layer: a gradient is
 * resolved inside the same blit, whereas a rotation or an opacity on a
 * container with children allocates a separate buffer and on this board that
 * is fatal (see docs/DECISIONES.md, "Las capas de LVGL son carisimas"). That
 * is why the cars are not rotated: the vertical version is built with the same
 * numbers but with the axes swapped in place().
 * -------------------------------------------------------------------------- */

typedef enum {
    BODY_CAR = 0,   /* 2 cells: nose, windscreen, roof, rear window and tail */
    BODY_VAN,       /* 3 cells: a one-piece van, side windows              */
    BODY_BOX,       /* 3 cells: cab in front + load bed behind             */
    BODY_DUMP,      /* 3 cells: cab + loaded tipper                        */
    BODY_FIRE,      /* 3 cells: fire engine, with the ladder along it      */
} body_kind_t;

typedef struct {
    uint32_t    body;       /* body, lit side of the gradient */
    uint32_t    body_lo;    /* the other end, shaded side     */
    uint32_t    glass;      /* glass, reflection side         */
    uint32_t    glass_lo;   /* ... and its shadow. Without this the gradient */
                            /* ended in pure white and the glass    */
                            /* looked white instead of pale blue.   */
    uint32_t    trim;       /* stripe, cab or load, depending on the model */
    body_kind_t kind;
} at_skin_def_t;

/* 2-cell cars. The colours come from the reference: saturated bodies and pale
 * blue glass, which is what makes them read as toy cars seen from above and
 * not as coloured rectangles. */
static const at_skin_def_t s_skin2[] = {
    { 0x4AA8FF, 0x1A5FC0, 0xD4ECFF, 0x7FB3DC, 0xFFFFFF, BODY_CAR }, /* blue     */
    { 0x5EDC5E, 0x1E9A2E, 0xD4ECFF, 0x7FB3DC, 0xFFFFFF, BODY_CAR }, /* green    */
    { 0xFFA22A, 0xC96A00, 0xD4ECFF, 0x7FB3DC, 0xFFFFFF, BODY_CAR }, /* orange   */
    { 0xFFCE1F, 0xC08F00, 0x4A4A54, 0x26262C, 0x1C1C1E, BODY_CAR }, /* taxi     */
    { 0xBB6BEE, 0x6E2FA8, 0xD4ECFF, 0x7FB3DC, 0xFFFFFF, BODY_CAR }, /* violet   */
    { 0x3AD5DE, 0x0E8A94, 0xD4ECFF, 0x7FB3DC, 0xFFFFFF, BODY_CAR }, /* sky blue */
};
#define AT_SKIN2_COUNT (int)(sizeof(s_skin2) / sizeof(s_skin2[0]))

/* 3-cell vehicles: the "big ones" that block the car park. */
static const at_skin_def_t s_skin3[] = {
    { 0xF6F6F8, 0xC4C4CA, 0x4A6E96, 0x2A4666, 0xE0E0E4, BODY_VAN  }, /* van      */
    { 0x3D93FF, 0x1552A8, 0xD4ECFF, 0x7FB3DC, 0xF2F2F4, BODY_BOX  }, /* blue box */
    { 0xFFC81F, 0xC08E00, 0xD4ECFF, 0x7FB3DC, 0x39393C, BODY_DUMP }, /* tipper   */
    { 0xE83A2E, 0xA01C15, 0xD4ECFF, 0x7FB3DC, 0xD8D8DC, BODY_FIRE }, /* fire engine */
    { 0x7C9B45, 0x455C22, 0xD4ECFF, 0x7FB3DC, 0x3F5219, BODY_BOX  }, /* military */
};
#define AT_SKIN3_COUNT (int)(sizeof(s_skin3) / sizeof(s_skin3[0]))

/* The target: always the same red sports car. Its glass is the same pale blue
 * as the rest (pink glass made it look washed out) and what makes it
 * unmistakable is the light outline at_car_view_create adds: it is needed
 * because the fire engine is ALSO red, and at a glance "the red car" has to be
 * one car only. */
static const at_skin_def_t s_skin_target = {
    0xFF4438, 0xB0201A, 0xD4ECFF, 0x7FB3DC, 0xFFE9E6, BODY_CAR,
};

/* --------------------------------------------------------------------------
 * Geometry
 *
 * Everything is thought of in "along the car" (lx, lw) and "across the car"
 * (ly, lh) coordinates, and place() converts them according to the
 * orientation. That way there is ONE set of formulas for horizontal cars and
 * vertical ones.
 * -------------------------------------------------------------------------- */

static void place(lv_obj_t *obj, bool horizontal, int lx, int ly, int lw, int lh)
{
    if (horizontal) {
        lv_obj_set_pos(obj, lx, ly);
        lv_obj_set_size(obj, lw, lh);
    } else {
        lv_obj_set_pos(obj, ly, lx);
        lv_obj_set_size(obj, lh, lw);
    }
}

static lv_obj_t *rect(lv_obj_t *parent, uint32_t color, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    /* In LVGL 9 every lv_obj is born clickable and would eat the touch meant
     * for the drag layer that sits above the board. */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

/* Gradient ACROSS the car: it gives the volume of a bulging body. Its
 * direction depends on how the vehicle ended up resting. */
static void shade(lv_obj_t *obj, bool horizontal, uint32_t to)
{
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(to), 0);
    lv_obj_set_style_bg_grad_dir(obj, horizontal ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, 0);
}

/* The four wheels, just poking out at the sides. */
static void add_wheels(lv_obj_t *body, bool h, int L, int W)
{
    int len = L * 20 / 100;          /* length of the wheel */
    int thk = W * 9 / 100;           /* how far it pokes out */
    if (thk < 3) thk = 3;

    for (int end = 0; end < 2; end++) {
        for (int side = 0; side < 2; side++) {
            lv_obj_t *w = rect(body, 0x17171A, thk / 2);
            int lx = end ? (L - L * 26 / 100) : (L * 6 / 100);
            int ly = side ? (W - thk) : 0;
            place(w, h, lx, ly, len, thk);
        }
    }
}

/* Windscreen + roof + rear window, which is what makes a rectangle read as a
 * car seen from above. 'front' is the +lx end. */
static void add_cabin(lv_obj_t *body, bool h, int L, int W, const at_skin_def_t *sk)
{
    int inset = W * 15 / 100;
    int gh    = W - 2 * inset;

    lv_obj_t *glass = rect(body, sk->glass, gh * 30 / 100);
    shade(glass, h, sk->glass_lo);
    place(glass, h, L * 26 / 100, inset, L * 56 / 100, gh);

    /* The roof splits the glass into a windscreen and a rear window.
     * Deliberately narrow: wide, the car read as three stripes instead of as a
     * cabin. */
    lv_obj_t *roof = rect(body, sk->body, 0);
    shade(roof, h, sk->body_lo);
    place(roof, h, L * 47 / 100, inset - W * 4 / 100, L * 11 / 100, gh + W * 8 / 100);
}

static void add_headlights(lv_obj_t *body, bool h, int L, int W)
{
    int lw = L * 5 / 100;
    if (lw < 3) lw = 3;
    for (int side = 0; side < 2; side++) {
        lv_obj_t *hl = rect(body, 0xFFF6D0, lw / 2);
        place(hl, h, L - lw - L * 3 / 100,
              side ? (W - W * 34 / 100) : W * 14 / 100, lw, W * 20 / 100);
    }
}

/* Cab of a large vehicle: the block at the front, with its windscreen. */
static void add_truck_cab(lv_obj_t *body, bool h, int L, int W,
                          const at_skin_def_t *sk, int cab_from)
{
    lv_obj_t *cab = rect(body, sk->trim, W * 22 / 100);
    shade(cab, h, sk->body_lo);
    place(cab, h, cab_from, 0, L - cab_from, W);

    int inset = W * 16 / 100;
    lv_obj_t *ws = rect(body, sk->glass, W * 12 / 100);
    shade(ws, h, sk->glass_lo);
    place(ws, h, cab_from + (L - cab_from) * 22 / 100, inset,
          (L - cab_from) * 44 / 100, W - 2 * inset);
}

lv_obj_t *at_car_view_create(lv_obj_t *parent, const at_car_t *car,
                             int cell_px, int gap_px)
{
    const at_skin_def_t *sk = &s_skin_target;
    if (!car->target) {
        sk = (car->len >= 3) ? &s_skin3[car->skin % AT_SKIN3_COUNT]
                             : &s_skin2[car->skin % AT_SKIN2_COUNT];
    }

    const bool h = car->horizontal;
    const int  L = car->len * cell_px - gap_px;   /* length */
    const int  W = cell_px - gap_px;              /* width */

    lv_obj_t *body = rect(parent, sk->body, W * 34 / 100);
    shade(body, h, sk->body_lo);
    lv_obj_set_size(body, h ? L : W, h ? W : L);
    lv_obj_set_pos(body, 0, 0);

    switch (sk->kind) {
    case BODY_CAR:
        add_wheels(body, h, L, W);
        add_cabin(body, h, L, W, sk);
        add_headlights(body, h, L, W);
        /* The target's light outline. It is the only thing that distinguishes
         * it from the fire engine, which shares the red, and it is visible
         * even when the car is surrounded by others. */
        if (car->target) {
            lv_obj_set_style_border_color(body, lv_color_hex(sk->trim), 0);
            lv_obj_set_style_border_width(body, 2, 0);
            lv_obj_set_style_border_opa(body, LV_OPA_COVER, 0);
        }
        break;

    case BODY_VAN: {
        add_wheels(body, h, L, W);
        /* Windows running along both sides, like a van. */
        for (int side = 0; side < 2; side++) {
            lv_obj_t *win = rect(body, sk->glass, W * 6 / 100);
            place(win, h, L * 12 / 100, side ? (W - W * 22 / 100) : W * 10 / 100,
                  L * 46 / 100, W * 12 / 100);
        }
        add_truck_cab(body, h, L, W, sk, L * 62 / 100);
        add_headlights(body, h, L, W);
        break;
    }

    case BODY_BOX: {
        add_wheels(body, h, L, W);
        /* The load bed: a matte box, darker than the cab. */
        lv_obj_t *box = rect(body, sk->body_lo, W * 12 / 100);
        place(box, h, L * 4 / 100, W * 7 / 100, L * 58 / 100, W - W * 14 / 100);
        /* Two ribs, which give it a lorry's scale. */
        for (int k = 0; k < 2; k++) {
            lv_obj_t *rib = rect(body, sk->body, 0);
            place(rib, h, L * (20 + k * 22) / 100, W * 7 / 100,
                  L * 3 / 100, W - W * 14 / 100);
        }
        add_truck_cab(body, h, L, W, sk, L * 66 / 100);
        add_headlights(body, h, L, W);
        break;
    }

    case BODY_DUMP: {
        add_wheels(body, h, L, W);
        /* The tipper and its load: dark stones poking out, as in the photo. */
        lv_obj_t *bed = rect(body, sk->trim, W * 10 / 100);
        place(bed, h, L * 4 / 100, W * 8 / 100, L * 56 / 100, W - W * 16 / 100);
        for (int k = 0; k < 3; k++) {
            int d = W * 26 / 100;
            lv_obj_t *rock = rect(body, (k % 2) ? 0x55555A : 0x6A6A70, d / 2);
            place(rock, h, L * (10 + k * 16) / 100, W * (26 + (k % 2) * 16) / 100, d, d);
        }
        add_truck_cab(body, h, L, W, sk, L * 64 / 100);
        add_headlights(body, h, L, W);
        break;
    }

    case BODY_FIRE: {
        add_wheels(body, h, L, W);
        /* The ladder, laid along the whole body. */
        lv_obj_t *rail = rect(body, sk->trim, W * 6 / 100);
        place(rail, h, L * 5 / 100, W * 30 / 100, L * 58 / 100, W * 40 / 100);
        for (int k = 0; k < 4; k++) {
            lv_obj_t *rung = rect(body, sk->body_lo, 0);
            place(rung, h, L * (12 + k * 13) / 100, W * 30 / 100,
                  L * 3 / 100, W * 40 / 100);
        }
        add_truck_cab(body, h, L, W, sk, L * 66 / 100);
        add_headlights(body, h, L, W);
        break;
    }
    }

    return body;
}
