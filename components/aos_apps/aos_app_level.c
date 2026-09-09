/*
 * AmoledOS - Spirit level.
 *
 * Two modes, chosen automatically from how the board is resting:
 *
 *   FLAT   face up on a surface. Bubble in a circle, with the tilt on both
 *          axes.
 *   SIDE   standing on edge, like a level against a wall. Horizontal tube and
 *          the angle from vertical.
 *
 * The "Cero" button stores the current reading as a reference: useful both for
 * compensating a surface that is not perfectly flat and for correcting the
 * sensor's mounting on the board.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>

#define FLAT_THRESHOLD_G    0.72f   /* |az| above this = face up               */
#define LEVEL_TOLERANCE_DEG 0.6f    /* within this it counts as level          */
#define DISC_R              108     /* usable radius of the flat mode's circle */
#define TUBE_W              300
#define TUBE_H              64
#define BUBBLE_D            44
#define DEG_FULL_SCALE      12.0f   /* degrees corresponding to full scale     */
#define FILTER_ALPHA        0.25f

typedef struct {
    lv_obj_t *flat_view;
    lv_obj_t *disc;
    lv_obj_t *target;
    lv_obj_t *flat_bubble;
    lv_obj_t *flat_readout;

    lv_obj_t *side_view;
    lv_obj_t *tube;
    lv_obj_t *side_bubble;
    lv_obj_t *side_readout;

    lv_obj_t *mode_label;
    lv_timer_t *timer;

    float ax, ay, az;           /* filtered reading */
    float zero_x, zero_y;       /* flat mode reference, in g */
    float zero_side_deg;        /* side mode reference, in degrees */
    bool  flat;
    bool  primed;
} level_t;

static level_t s_level;

/* -------------------------------------------------------------------------- */

static lv_obj_t *make_bubble(lv_obj_t *parent)
{
    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_remove_style_all(bubble);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bubble, BUBBLE_D, BUBBLE_D);
    lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bubble, AOS_C_GREEN, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bubble, 2, 0);
    lv_obj_set_style_border_color(bubble, AOS_C_TEXT, 0);
    lv_obj_set_style_border_opa(bubble, LV_OPA_40, 0);
    return bubble;
}

static void set_bubble_ok(lv_obj_t *bubble, bool ok)
{
    lv_obj_set_style_bg_color(bubble, ok ? AOS_C_GREEN : AOS_C_ORANGE, 0);
    lv_obj_set_style_bg_opa(bubble, ok ? LV_OPA_90 : LV_OPA_60, 0);
}

static void zero_cb(lv_event_t *event)
{
    (void)event;
    if (s_level.flat) {
        s_level.zero_x = s_level.ax;
        s_level.zero_y = s_level.ay;
        aos_ui_toast(_("Plano puesto a cero"), 1200);
    } else {
        /* what is shown is (raw angle - reference): setting the reference
         * equal to the current angle leaves the display at zero */
        /* SAME formula as the drawing: if only one is changed, the zero is
         * computed with one and the screen with the other, and the level shows
         * a fixed offset that looks like a sensor calibration fault. */
        s_level.zero_side_deg = atan2f(-s_level.ay, s_level.ax) * 57.2957795f;
        aos_ui_toast(_("Lateral puesto a cero"), 1200);
    }
    aos_hal_beep(1500, 25);
}

static void reset_cb(lv_event_t *event)
{
    (void)event;
    s_level.zero_x = 0.0f;
    s_level.zero_y = 0.0f;
    s_level.zero_side_deg = 0.0f;
    aos_ui_toast(_("Referencia de fabrica"), 1200);
}

/* -------------------------------------------------------------------------- */

static void show_mode(bool flat)
{
    if (flat) {
        lv_obj_remove_flag(s_level.flat_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_level.side_view, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_level.mode_label, "plano");
    } else {
        lv_obj_add_flag(s_level.flat_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_level.side_view, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_level.mode_label, "lateral");
    }
}

static void update(lv_timer_t *timer)
{
    (void)timer;

    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        return;
    }

    /* low-pass: the accelerometer trembles and the bubble would be jittery */
    if (!s_level.primed) {
        s_level.ax = imu.ax;
        s_level.ay = imu.ay;
        s_level.az = imu.az;
        s_level.primed = true;
    } else {
        s_level.ax += (imu.ax - s_level.ax) * FILTER_ALPHA;
        s_level.ay += (imu.ay - s_level.ay) * FILTER_ALPHA;
        s_level.az += (imu.az - s_level.az) * FILTER_ALPHA;
    }

    bool flat = fabsf(s_level.az) > FLAT_THRESHOLD_G;
    if (flat != s_level.flat) {
        s_level.flat = flat;
        show_mode(flat);
    }

    char buf[64];

    if (flat) {
        float ax = s_level.ax - s_level.zero_x;
        float ay = s_level.ay - s_level.zero_y;

        /* tilt of each axis from horizontal */
        /* Axes MEASURED on the board (2026-08-28): ax is the screen's VERTICAL
         * axis and ay the HORIZONTAL one, the opposite of how it was. Also,
         * the right of the screen is -ay and the bottom edge is +ax.
         *
         * deg_ancho  = tilt across the screen, comes from ay
         * deg_alto   = tilt along the screen, comes from ax
         * On crossing them the argument inside the atan2 has to be crossed
         * too: it is the tilt of EACH axis against the horizontal. */
        float deg_ancho = atan2f(ay, sqrtf(ax * ax + s_level.az * s_level.az)) * 57.2957795f;
        float deg_alto  = atan2f(ax, sqrtf(ay * ay + s_level.az * s_level.az)) * 57.2957795f;

        /* It behaves like a real bubble: it goes towards the RAISED side. If
         * the right edge goes down, ay turns negative and the bubble goes
         * left; if the top edge goes down, ax turns negative and the bubble
         * goes down. */
        int32_t px = (int32_t)(deg_ancho / DEG_FULL_SCALE * (float)DISC_R);
        int32_t py = (int32_t)(-deg_alto / DEG_FULL_SCALE * (float)DISC_R);

        /* the bubble does not leave the disc */
        int32_t radius = (int32_t)sqrtf((float)(px * px + py * py));
        if (radius > DISC_R) {
            px = px * DISC_R / radius;
            py = py * DISC_R / radius;
        }
        lv_obj_align(s_level.flat_bubble, LV_ALIGN_CENTER, px, py);

        bool ok = fabsf(deg_ancho) < LEVEL_TOLERANCE_DEG &&
                  fabsf(deg_alto) < LEVEL_TOLERANCE_DEG;
        set_bubble_ok(s_level.flat_bubble, ok);

        /* the degree sign is U+00B0, two bytes in UTF-8: 0xC2 0xB0 */
        /* They are named for what they mean and not after the sensor's axes:
         * mixing the two is precisely what caused this bug. */
        snprintf(buf, sizeof(buf), _("ancho %+.1f\xC2\xB0   alto %+.1f\xC2\xB0"),
                 (double)deg_ancho, (double)deg_alto);
        lv_label_set_text(s_level.flat_readout, buf);
    } else {
        /* on edge: how far it deviates from vertical */
        /* On edge gravity goes through ax (not through -ay) and the sideways
         * deviation through ay. Positive = leaning towards the right of the
         * screen. */
        float deg = atan2f(-s_level.ay, s_level.ax) * 57.2957795f - s_level.zero_side_deg;

        int32_t px = (int32_t)(deg / DEG_FULL_SCALE * (float)(TUBE_W / 2 - BUBBLE_D / 2));
        int32_t limit = TUBE_W / 2 - BUBBLE_D / 2 - 4;
        px = LV_CLAMP(-limit, px, limit);
        lv_obj_align(s_level.side_bubble, LV_ALIGN_CENTER, px, 0);

        bool ok = fabsf(deg) < LEVEL_TOLERANCE_DEG;
        set_bubble_ok(s_level.side_bubble, ok);

        snprintf(buf, sizeof(buf), "%+.1f\xC2\xB0", (double)deg);
        lv_label_set_text(s_level.side_readout, buf);
    }
}

/* -------------------------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    /* --- flat mode --- */
    s_level.flat_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_level.flat_view);
    lv_obj_set_size(s_level.flat_view, lv_pct(100), lv_pct(100));

    s_level.disc = lv_obj_create(s_level.flat_view);
    lv_obj_remove_style_all(s_level.disc);
    lv_obj_set_size(s_level.disc, DISC_R * 2 + BUBBLE_D, DISC_R * 2 + BUBBLE_D);
    lv_obj_set_style_radius(s_level.disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_level.disc, 3, 0);
    lv_obj_set_style_border_color(s_level.disc, AOS_C_CARD2, 0);
    lv_obj_align(s_level.disc, LV_ALIGN_TOP_MID, 0, 24);

    s_level.target = lv_obj_create(s_level.disc);
    lv_obj_remove_style_all(s_level.target);
    lv_obj_set_size(s_level.target, BUBBLE_D + 14, BUBBLE_D + 14);
    lv_obj_set_style_radius(s_level.target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_level.target, 2, 0);
    lv_obj_set_style_border_color(s_level.target, AOS_C_DIM, 0);
    lv_obj_center(s_level.target);

    s_level.flat_bubble = make_bubble(s_level.disc);
    lv_obj_center(s_level.flat_bubble);

    s_level.flat_readout = aos_label_boxed(s_level.flat_view, "", aos_font_body,
                                           AOS_C_TEXT, AOS_SCREEN_W, 30);
    /* The reading drops into the strip the touch panel cannot reach: it is
     * text and nobody touches it. The buttons keep the last live pixels. */
    lv_obj_align(s_level.flat_readout, LV_ALIGN_BOTTOM_MID, 0, -26);

    /* --- side mode --- */
    s_level.side_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_level.side_view);
    lv_obj_set_size(s_level.side_view, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_level.side_view, LV_OBJ_FLAG_HIDDEN);

    s_level.side_readout = aos_label_boxed(s_level.side_view, "", aos_font_huge,
                                           AOS_C_TEXT, AOS_SCREEN_W, 60);
    lv_obj_align(s_level.side_readout, LV_ALIGN_TOP_MID, 0, 70);

    s_level.tube = lv_obj_create(s_level.side_view);
    lv_obj_remove_style_all(s_level.tube);
    lv_obj_set_size(s_level.tube, TUBE_W, TUBE_H);
    lv_obj_set_style_radius(s_level.tube, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_level.tube, 3, 0);
    lv_obj_set_style_border_color(s_level.tube, AOS_C_CARD2, 0);
    lv_obj_align(s_level.tube, LV_ALIGN_CENTER, 0, 20);

    /* the two centre marks, as on a real level */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *mark = lv_obj_create(s_level.tube);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, 2, TUBE_H - 18);
        lv_obj_set_style_bg_color(mark, AOS_C_DIM, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_align(mark, LV_ALIGN_CENTER, (i == 0 ? -1 : 1) * (BUBBLE_D / 2 + 5), 0);
    }

    s_level.side_bubble = make_bubble(s_level.tube);
    lv_obj_center(s_level.side_bubble);

    /* --- common --- */
    s_level.mode_label = aos_label_boxed(page, "plano", aos_font_small,
                                         AOS_C_DIM, AOS_SCREEN_W, 20);
    lv_obj_align(s_level.mode_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_t *zero = aos_button(page, _("Cero"), AOS_C_ACCENT, zero_cb, NULL);
    lv_obj_set_size(zero, 120, 40);
    /* -62 and not -4: with a height of 40 px they end up at y 346..386, inside
     * the touch ceiling (AOS_TOUCH_Y_MAX). They were at 404..443 and did not
     * respond. */
    lv_obj_align(zero, LV_ALIGN_BOTTOM_LEFT, 30, -62);

    lv_obj_t *reset = aos_button(page, _("Reset"), AOS_C_CARD2, reset_cb, NULL);
    lv_obj_set_size(reset, 120, 40);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_RIGHT, -30, -62);

    s_level.primed = false;
    s_level.flat = true;
    show_mode(true);
    s_level.timer = lv_timer_create(update, 60, NULL);
    update(NULL);
    return &s_level;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_level.timer) {
        lv_timer_delete(s_level.timer);
        s_level.timer = NULL;
    }
}

void aos_app_level_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.level",
            .name     = "Nivel",
            .icon_vec = AOS_ICON_LEVEL,
            .color_a  = 0x40C8E0,
            .color_b  = 0x1F6E80,
            .flags    = AOS_APP_FLAG_KEEP_AWAKE,
            .order    = 65,
        },
        .create  = create,
        .destroy = destroy,
    };
}
