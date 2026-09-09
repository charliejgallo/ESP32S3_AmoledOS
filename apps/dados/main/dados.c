/*
 * AmoledOS - Dice
 *
 * Rolls one to six dice of four, six, eight, ten, twelve or twenty faces. You
 * roll by touching the big button, by pressing the physical button or -the one
 * thing that justifies this living on a watch- by shaking the board.
 *
 * Three decisions worth writing down:
 *
 *   - The dice are drawn with a NUMBER and not with pips. Pips only work for
 *     the d6, and doing them would be seven objects per die (42 in total) for
 *     one case out of six; the number works just as well for the d20.
 *   - The six squares exist from the start and the spare ones are hidden with
 *     LV_OBJ_FLAG_HIDDEN. Rebuilding them on every roll would be clima's
 *     hourly-strip mistake: creating and destroying objects costs 20-30 times
 *     more on the board than on the Mac.
 *   - During the animation each die's text is rewritten every 60 ms, but the
 *     square's COLOUR is only written when it changes. LVGL does not compare:
 *     an lv_obj_set_style_* with the same value invalidates all the same, and
 *     with twelve invalidations per frame it is best not to waste them.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_DICE        6
#define HISTORY         6

#define DIE_SIZE        88
#define DIE_GAP         10
#define DIE_COLS        3
#define DIE_X0          ((AOS_SCREEN_W - (DIE_COLS * DIE_SIZE + (DIE_COLS - 1) * DIE_GAP)) / 2)
#define DIE_Y0          4
#define TOTAL_GAP       14          /* air between the dice and the total */
#define TOTAL_H         34
#define CONTROLS_Y      232         /* where the row of die types starts */

/* Shaking: the magnitude of the difference between two consecutive readings,
 * in g. A flick of the wrist passes 1 g with room to spare; resting the board
 * on the table does not reach it. */
#define SHAKE_G         0.90f
#define SHAKE_COOLDOWN  900         /* ms between two shake rolls */
#define IMU_PERIOD_MS   100         /* the IMU shares the I2C bus with the touch panel */

#define ROLL_MS         700         /* how long the tumble lasts */
#define ROLL_FRAME_MS   60

static const uint8_t SIDES[] = { 4, 6, 8, 10, 12, 20 };
#define N_SIDES         (int)(sizeof(SIDES) / sizeof(SIDES[0]))

typedef struct {
    lv_obj_t *box[MAX_DICE];
    lv_obj_t *num[MAX_DICE];
    uint32_t  box_color[MAX_DICE];      /* the last thing written, so as not to repeat */

    lv_obj_t *lbl_total;
    lv_obj_t *lbl_hist;
    lv_obj_t *lbl_count;
    lv_obj_t *chip[N_SIDES];
    lv_obj_t *roll_label;

    lv_timer_t *timer;

    int  sides_idx;
    int  count;
    int  value[MAX_DICE];

    bool     rolling;
    uint32_t roll_end_ms;
    uint32_t last_frame_ms;

    /* shaking */
    uint32_t last_imu_ms;
    uint32_t last_shake_ms;
    float    ax, ay, az;
    bool     imu_primed;

    bool pending_roll;                  /* left by the physical button */

    int  hist[HISTORY];
    int  hist_n;

    uint32_t rng;
} dice_t;

static dice_t s_dice;

/* -------------------------------------------------------------------------- */

static uint32_t rnd(void)
{
    uint32_t x = s_dice.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_dice.rng = x;
    return x;
}

static int die_sides(void)
{
    return SIDES[s_dice.sides_idx];
}

/* -------------------------------------------------------------------------- */
/* Drawing a die                                                               */

static void paint_die(int i, bool settled)
{
    int v = s_dice.value[i];
    int n = die_sides();

    uint32_t color = 0x2C2C2E;
    if (settled && v > 0) {
        if (v == n && n > 4) {
            color = 0x1E8E3E;       /* the highest face */
        } else if (v == 1) {
            color = 0x8E1F18;       /* the one */
        }
    }
    if (color != s_dice.box_color[i]) {
        lv_obj_set_style_bg_color(s_dice.box[i], lv_color_hex(color), 0);
        s_dice.box_color[i] = color;
    }

    /* v == 0 is "not rolled yet": the square stays grey with a dash, so the
     * first screen does not show six red ones as if they were a catastrophic
     * roll. */
    /* 16 and not 8: for GCC a %d takes up to 11 characters, it does not care
     * that the number is a die face, and with -Werror=format-truncation that
     * stops the firmware's build even though the simulator says nothing. */
    char buf[16];
    if (v <= 0) {
        snprintf(buf, sizeof(buf), "%s", "-");
    } else {
        snprintf(buf, sizeof(buf), "%d", v);
    }
    lv_label_set_text(s_dice.num[i], buf);
}

static void refresh_total(bool settled)
{
    int total = 0;
    for (int i = 0; i < s_dice.count; i++) {
        total += s_dice.value[i];
    }

    char buf[32];
    if (total == 0) {                   /* freshly opened or freshly changed */
        snprintf(buf, sizeof(buf), "%dd%d", s_dice.count, die_sides());
    } else if (s_dice.count == 1) {
        snprintf(buf, sizeof(buf), "d%d  =  %d", die_sides(), total);
    } else {
        snprintf(buf, sizeof(buf), "%dd%d  =  %d", s_dice.count, die_sides(), total);
    }
    lv_label_set_text(s_dice.lbl_total, buf);
    lv_obj_set_style_text_color(s_dice.lbl_total,
                                settled ? AOS_C_TEXT : AOS_C_DIM, 0);
}

static void refresh_history(void)
{
    if (s_dice.hist_n == 0) {
        lv_label_set_text(s_dice.lbl_hist, _("agita la placa para tirar"));
        return;
    }
    /* "999" per roll plus the separator: six entries fit with room to
     * spare. */
    char buf[64];
    int  used = snprintf(buf, sizeof(buf), "%s", "ultimas:");
    for (int i = 0; i < s_dice.hist_n && used < (int)sizeof(buf) - 8; i++) {
        used += snprintf(buf + used, sizeof(buf) - (size_t)used, "%s%d",
                         i ? " - " : "  ", s_dice.hist[i]);
    }
    lv_label_set_text(s_dice.lbl_hist, buf);
}

/* The six squares always exist; what changes is which are visible and where.
 * With two dice, leaving them at the top left and an 88 px gap below looks
 * like a layout bug, so the block is centred on both axes according to how
 * many there are. */
static void show_dice(void)
{
    int rows = (s_dice.count + DIE_COLS - 1) / DIE_COLS;
    int32_t block_h = rows * DIE_SIZE + (rows - 1) * DIE_GAP;

    /* The GROUP (the squares plus the total's line) is centred in the strip
     * left free above the controls. Centring only the squares leaves the total
     * hanging far away and with one die it looks like a bug. */
    int32_t group_h = block_h + TOTAL_GAP + TOTAL_H;
    int32_t y0 = DIE_Y0 + (CONTROLS_Y - DIE_Y0 - group_h) / 2;
    if (y0 < DIE_Y0) {
        y0 = DIE_Y0;
    }
    if (s_dice.lbl_total) {
        lv_obj_set_pos(s_dice.lbl_total, 0, y0 + block_h + TOTAL_GAP);
    }

    for (int i = 0; i < MAX_DICE; i++) {
        if (i >= s_dice.count) {
            lv_obj_add_flag(s_dice.box[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int r    = i / DIE_COLS;
        int cols = s_dice.count - r * DIE_COLS;
        if (cols > DIE_COLS) {
            cols = DIE_COLS;
        }
        int32_t x0 = (AOS_SCREEN_W - (cols * DIE_SIZE + (cols - 1) * DIE_GAP)) / 2;
        lv_obj_set_pos(s_dice.box[i], x0 + (i % DIE_COLS) * (DIE_SIZE + DIE_GAP),
                                      y0 + r * (DIE_SIZE + DIE_GAP));
        lv_obj_remove_flag(s_dice.box[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* Roll                                                                        */

static void settle(void)
{
    int total = 0;
    for (int i = 0; i < s_dice.count; i++) {
        s_dice.value[i] = (int)(rnd() % (uint32_t)die_sides()) + 1;
        total += s_dice.value[i];
        paint_die(i, true);
    }
    refresh_total(true);

    /* The history is a short strip read left to right: the latest first. Six
     * entries and the rest falls off. */
    for (int i = HISTORY - 1; i > 0; i--) {
        s_dice.hist[i] = s_dice.hist[i - 1];
    }
    s_dice.hist[0] = total;
    if (s_dice.hist_n < HISTORY) {
        s_dice.hist_n++;
    }
    refresh_history();

    /* Two short notes: the second higher the better the roll came out. */
    int max = s_dice.count * die_sides();
    int min = s_dice.count;
    int f   = 700 + (max > min ? (total - min) * 900 / (max - min) : 450);
    aos_hal_beep(f, 40);
}

static void start_roll(void)
{
    if (s_dice.rolling) {
        return;
    }
    s_dice.rolling      = true;
    s_dice.roll_end_ms  = lv_tick_get() + ROLL_MS;
    s_dice.last_frame_ms = 0;
    refresh_total(false);
    aos_hal_beep(420, 30);
}

/* -------------------------------------------------------------------------- */
/* Controls                                                                    */

static void roll_cb(lv_event_t *event)
{
    (void)event;
    start_roll();
}

static void paint_chips(void)
{
    for (int i = 0; i < N_SIDES; i++) {
        bool on = (i == s_dice.sides_idx);
        lv_obj_set_style_bg_color(s_dice.chip[i],
                                  on ? AOS_C_ACCENT : AOS_C_CARD, 0);
    }
}

static void chip_cb(lv_event_t *event)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(event);
    if (idx == s_dice.sides_idx) {
        return;
    }
    s_dice.sides_idx = idx;
    aos_hal_pref_set_i32("dice_sides", idx);
    paint_chips();

    /* Changing die invalidates what was on screen: the old values may not
     * exist on the new face. */
    s_dice.hist_n = 0;
    for (int i = 0; i < MAX_DICE; i++) {
        s_dice.value[i] = 0;
    }
    start_roll();
}

static void count_cb(lv_event_t *event)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(event);
    int next  = s_dice.count + delta;
    if (next < 1 || next > MAX_DICE) {
        return;
    }
    s_dice.count = next;
    aos_hal_pref_set_i32("dice_count", next);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d dado%s", next, next == 1 ? "" : "s");
    lv_label_set_text(s_dice.lbl_count, buf);

    show_dice();
    s_dice.hist_n = 0;
    start_roll();
}

/* -------------------------------------------------------------------------- */
/* Shaking                                                                     */

static void check_shake(uint32_t now)
{
    if ((uint32_t)(now - s_dice.last_imu_ms) < IMU_PERIOD_MS) {
        return;
    }
    s_dice.last_imu_ms = now;

    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        return;
    }
    if (!s_dice.imu_primed) {
        s_dice.ax = imu.ax;
        s_dice.ay = imu.ay;
        s_dice.az = imu.az;
        s_dice.imu_primed = true;
        return;
    }

    float dx = imu.ax - s_dice.ax;
    float dy = imu.ay - s_dice.ay;
    float dz = imu.az - s_dice.az;
    s_dice.ax = imu.ax;
    s_dice.ay = imu.ay;
    s_dice.az = imu.az;

    float jerk = sqrtf(dx * dx + dy * dy + dz * dz);
    if (jerk < SHAKE_G) {
        return;
    }
    if ((uint32_t)(now - s_dice.last_shake_ms) < SHAKE_COOLDOWN) {
        return;
    }
    s_dice.last_shake_ms = now;
    start_roll();
}

/* -------------------------------------------------------------------------- */

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (s_dice.pending_roll) {
        s_dice.pending_roll = false;
        start_roll();
    }

    if (s_dice.rolling) {
        if ((int32_t)(now - s_dice.roll_end_ms) >= 0) {
            s_dice.rolling = false;
            settle();
        } else if ((uint32_t)(now - s_dice.last_frame_ms) >= ROLL_FRAME_MS) {
            s_dice.last_frame_ms = now;
            for (int i = 0; i < s_dice.count; i++) {
                s_dice.value[i] = (int)(rnd() % (uint32_t)die_sides()) + 1;
                paint_die(i, false);
            }
        }
        return;         /* while it tumbles the accelerometer is not watched */
    }

    check_shake(now);
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *flat_button(lv_obj_t *parent, const char *text,
                             int32_t x, int32_t y, int32_t w, int32_t h,
                             lv_color_t color, const lv_font_t *font,
                             lv_event_cb_t cb, void *user_data,
                             lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, h / 3, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = aos_label(btn, text, font, AOS_C_TEXT);
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
    memset(&s_dice, 0, sizeof(s_dice));
    s_dice.rng = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;

    int32_t v = 0;
    if (aos_hal_pref_get_i32("dice_sides", &v) && v >= 0 && v < N_SIDES) {
        s_dice.sides_idx = (int)v;
    } else {
        s_dice.sides_idx = 1;       /* d6 */
    }
    if (aos_hal_pref_get_i32("dice_count", &v) && v >= 1 && v <= MAX_DICE) {
        s_dice.count = (int)v;
    } else {
        s_dice.count = 2;
    }
    lv_obj_t *page = aos_page(root);

    /* ---- the six squares ---- */
    for (int i = 0; i < MAX_DICE; i++) {
        lv_obj_t *box = lv_obj_create(page);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, DIE_SIZE, DIE_SIZE);
        lv_obj_set_pos(box, DIE_X0 + (i % DIE_COLS) * (DIE_SIZE + DIE_GAP),
                            DIE_Y0 + (i / DIE_COLS) * (DIE_SIZE + DIE_GAP));
        lv_obj_set_style_radius(box, 20, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        s_dice.box[i]       = box;
        s_dice.box_color[i] = 0x2C2C2E;

        /* Fixed box with the text centred inside: the number goes from one
         * digit to two and a content-sized label would drift. */
        s_dice.num[i] = aos_label_boxed(box, "-", aos_font_huge, AOS_C_TEXT,
                                        DIE_SIZE, 56);
        lv_obj_center(s_dice.num[i]);
        lv_obj_remove_flag(s_dice.num[i], LV_OBJ_FLAG_CLICKABLE);
    }
    /* ---- total ----
     * It is created before show_dice() because it is show_dice() that
     * positions it: the height of the dice block depends on how many there
     * are. */
    s_dice.lbl_total = aos_label_boxed(page, "", aos_font_title, AOS_C_TEXT,
                                       AOS_SCREEN_W, TOTAL_H);
    show_dice();

    /* ---- choosing the die ---- */
    {
        const int32_t w = 52, gap = 6;
        const int32_t x0 = (AOS_SCREEN_W - (N_SIDES * w + (N_SIDES - 1) * gap)) / 2;
        for (int i = 0; i < N_SIDES; i++) {
            char name[8];
            snprintf(name, sizeof(name), "d%d", SIDES[i]);
            s_dice.chip[i] = flat_button(page, name, x0 + i * (w + gap),
                                         CONTROLS_Y + 6,
                                         w, 38, AOS_C_CARD, aos_font_small,
                                         chip_cb, (void *)(intptr_t)i, NULL);
        }
        paint_chips();
    }

    /* ---- how many ---- */
    flat_button(page, "-", 44, 282, 56, 44, AOS_C_CARD, aos_font_title,
                count_cb, (void *)(intptr_t)-1, NULL);
    flat_button(page, "+", AOS_SCREEN_W - 44 - 56, 282, 56, 44, AOS_C_CARD,
                aos_font_title, count_cb, (void *)(intptr_t)1, NULL);
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d dado%s", s_dice.count,
                 s_dice.count == 1 ? "" : "s");
        s_dice.lbl_count = aos_label_boxed(page, buf, aos_font_body, AOS_C_TEXT,
                                           160, 28);
        lv_obj_set_pos(s_dice.lbl_count, (AOS_SCREEN_W - 160) / 2, 290);
    }

    /* ---- roll ---- */
    flat_button(page, _("TIRAR"), 44, 334, AOS_SCREEN_W - 88, 62, AOS_C_ACCENT,
                aos_font_title, roll_cb, NULL, &s_dice.roll_label);

    s_dice.lbl_hist = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                      AOS_SCREEN_W, 20);
    lv_obj_set_pos(s_dice.lbl_hist, 0, 398);
    refresh_history();
    refresh_total(true);
    for (int i = 0; i < MAX_DICE; i++) {
        paint_die(i, true);
    }

    s_dice.last_imu_ms   = lv_tick_get();
    s_dice.last_shake_ms = lv_tick_get();
    s_dice.timer = lv_timer_create(frame_cb, 30, NULL);
    return &s_dice;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_dice.timer) {
        lv_timer_delete(s_dice.timer);
        s_dice.timer = NULL;
    }
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_dice, 0, sizeof(s_dice));
}

/* The physical button rolls. It runs from the HAL's task, so it only leaves a
 * note and the next frame picks it up. The long press is left to the system,
 * which is the way out to the clock. */
static bool button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    (void)inst;
    if (action == AOS_BUTTON_CLICK) {
        s_dice.pending_roll = true;
        return true;
    }
    return false;
}

static bool dados_init(aos_app_t *app)
{
    app->desc.id       = "aos.dice";
    app->desc.name     = "Dados";
    app->desc.icon     = "d6";
    app->desc.icon_vec = AOS_ICON_DICE;
    app->desc.color_a  = 0xBF5AF2;
    app->desc.color_b  = 0x5B2078;
    app->desc.order    = 75;

    app->create        = create;
    app->destroy       = destroy;
    app->button        = button;
    return true;
}

AOS_APP_ENTRY(dados_init);
