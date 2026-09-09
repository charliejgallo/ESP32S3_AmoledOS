/*
 * AmoledOS - Simon
 *
 * The memory game of 1978: the machine plays a sequence of colours and you
 * have to repeat it. Each success adds a step and takes a few milliseconds
 * off.
 *
 * The whole game is a state machine driven by a single 40 ms lv_timer. There
 * are deliberately no LVGL animations: an opacity animation per panel would do
 * the job just as well, but the sound and the light have to land TOGETHER and
 * with a timer of our own both come out of the same frame. The note's duration
 * is the same as the light's, and the silence between notes is a third of
 * that.
 *
 * Four panels, a circle in the middle and two labels: six objects. No canvas
 * or anything like it is needed, because between one note and the next nothing
 * changes.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>

#define MAX_STEPS       99

#define PAD_SIZE        156
#define PAD_GAP         14
#define PAD_X0          ((AOS_SCREEN_W - (2 * PAD_SIZE + PAD_GAP)) / 2)
#define PAD_Y0          60
#define EYE_D           112

/* The four notes of the original Simon (Milton Bradley, 1978): an E major
 * chord in second inversion, chosen so any combination sounds good. The fifth
 * -the low one- is the error note. */
static const uint16_t TONE[4]  = { 415, 310, 252, 209 };
static const uint32_t BRIGHT[4] = { 0x30D158, 0xFF453A, 0xFFD60A, 0x0A84FF };
static const uint32_t DIM[4]    = { 0x0F4A20, 0x581712, 0x584A04, 0x06325F };

typedef enum {
    ST_IDLE = 0,        /* waiting for a touch to start      */
    ST_SHOW,            /* the machine plays the sequence    */
    ST_WAIT,            /* the player's turn                 */
    ST_OVER,            /* failed; a touch starts over       */
} state_t;

typedef struct {
    lv_obj_t *pad[4];
    lv_obj_t *eye;
    lv_obj_t *lbl_eye;
    lv_obj_t *lbl_foot;
    lv_timer_t *timer;

    uint32_t pad_color[4];      /* the last thing written, so as not to invalidate for nothing */

    uint8_t seq[MAX_STEPS];
    int     len;                /* steps of the current sequence */
    int     pos;               /* in ST_SHOW: which one is playing; in ST_WAIT: how many have gone */

    state_t  state;
    uint32_t next_ms;           /* when the current step expires */
    bool     lit;               /* in ST_SHOW: the panel is lit */
    int      flash;             /* panel lit by the player, -1 if none */
    uint32_t flash_end_ms;

    int  best;
    bool pending_start;         /* left by the physical button */

    uint32_t rng;
} simon_t;

static simon_t s_simon;

/* -------------------------------------------------------------------------- */

static uint32_t rnd(void)
{
    uint32_t x = s_simon.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_simon.rng = x;
    return x;
}

/* The sequence speeds up with the level and bottoms out at 220 ms, which is
 * already the limit of what can be followed. */
static uint32_t note_ms(void)
{
    int ms = 520 - s_simon.len * 16;
    return (uint32_t)(ms < 220 ? 220 : ms);
}

static void set_pad(int i, bool on)
{
    uint32_t color = on ? BRIGHT[i] : DIM[i];
    if (color == s_simon.pad_color[i]) {
        return;
    }
    lv_obj_set_style_bg_color(s_simon.pad[i], lv_color_hex(color), 0);
    s_simon.pad_color[i] = color;
}

static void all_pads_off(void)
{
    for (int i = 0; i < 4; i++) {
        set_pad(i, false);
    }
}

static void set_eye(const char *text, lv_color_t color)
{
    lv_label_set_text(s_simon.lbl_eye, text);
    lv_obj_set_style_text_color(s_simon.lbl_eye, color, 0);
}

static void refresh_foot(void)
{
    char buf[48];
    if (s_simon.best > 0) {
        snprintf(buf, sizeof(buf), _("mejor  %d"), s_simon.best);
    } else {
        snprintf(buf, sizeof(buf), "%s", _("repeti la secuencia"));
    }
    lv_label_set_text(s_simon.lbl_foot, buf);
}

/* -------------------------------------------------------------------------- */
/* Transitions                                                                 */

static void go_idle(void)
{
    s_simon.state = ST_IDLE;
    s_simon.len   = 0;
    s_simon.pos   = 0;
    all_pads_off();
    set_eye(_("TOCA"), AOS_C_TEXT);
    refresh_foot();
}

/* Starts a new game. It is separate from grow_and_show() because the bug the
 * first test uncovered was precisely that: touching after losing called
 * grow_and_show() without zeroing the sequence, so the "new" game started with
 * the old one inside it and with the inherited length. */
static void restart(void)
{
    s_simon.len = 0;
    s_simon.pos = 0;
    all_pads_off();
    s_simon.flash = -1;
}

/* One more step and the machine plays it from the beginning. */
static void grow_and_show(void)
{
    if (s_simon.len < MAX_STEPS) {
        s_simon.seq[s_simon.len++] = (uint8_t)(rnd() & 3u);
    }
    s_simon.state   = ST_SHOW;
    s_simon.pos     = 0;
    s_simon.lit     = false;
    /* Half a second of air before starting: without that the first colour
     * collides with the player's touch and it looks as if the machine had
     * jumped the gun. */
    s_simon.next_ms = lv_tick_get() + 500;

    /* 16 and not 8: for GCC a %d takes up to 11 characters and it does not
     * care that the sequence is capped at 99. See HANDOFF-APPS.md. */
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s_simon.len);
    set_eye(buf, AOS_C_DIM);
}

static void game_over(void)
{
    s_simon.state = ST_OVER;
    all_pads_off();
    s_simon.flash = -1;

    int score = s_simon.len - 1;        /* the step that failed does not count */
    if (score > s_simon.best) {
        s_simon.best = score;
        aos_hal_pref_set_i32("simon_best", score);
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", score);
    set_eye(buf, AOS_C_RED);

    char foot[48];
    snprintf(foot, sizeof(foot), _("fallaste en %d   mejor %d"), score + 1, s_simon.best);
    lv_label_set_text(s_simon.lbl_foot, foot);

    aos_hal_beep(110, 420);
    s_simon.next_ms = lv_tick_get() + 900;
}

/* -------------------------------------------------------------------------- */
/* Input                                                                       */

/* Start, if it is time to start. Called by the eye in the middle, the physical
 * button and any panel when a game is not under way. */
static void try_start(void)
{
    if (s_simon.state != ST_IDLE && s_simon.state != ST_OVER) {
        return;
    }
    /* After losing, the board stays deaf for a moment: otherwise the same
     * clumsy finger that failed starts the next game and you do not even get
     * to see what you finished on. */
    if (s_simon.state == ST_OVER &&
        (int32_t)(lv_tick_get() - s_simon.next_ms) < 0) {
        return;
    }
    restart();
    grow_and_show();
}

static void press_pad(int i)
{
    if (s_simon.state == ST_IDLE || s_simon.state == ST_OVER) {
        try_start();
        return;
    }
    if (s_simon.state != ST_WAIT) {
        return;         /* while the machine plays, the board does not listen */
    }

    uint32_t dur = note_ms();
    set_pad(i, true);
    s_simon.flash        = i;
    s_simon.flash_end_ms = lv_tick_get() + dur;
    aos_hal_beep(TONE[i], (int)dur);

    if (s_simon.seq[s_simon.pos] != (uint8_t)i) {
        game_over();
        /* the wrong panel stays lit for as long as the note lasts: you can see
         * which the mistake was */
        set_pad(i, true);
        s_simon.flash        = i;
        s_simon.flash_end_ms = lv_tick_get() + dur;
        return;
    }

    s_simon.pos++;
    if (s_simon.pos >= s_simon.len) {
        /* the round is complete: it waits for the note to finish and carries
         * on */
        s_simon.state   = ST_SHOW;
        s_simon.pos     = -1;       /* -1 = the sequence has not grown yet */
        s_simon.next_ms = lv_tick_get() + dur + 320;
        s_simon.lit     = false;
    }
}

static void pad_cb(lv_event_t *event)
{
    press_pad((int)(intptr_t)lv_event_get_user_data(event));
}

static void eye_cb(lv_event_t *event)
{
    (void)event;
    try_start();
}

/* -------------------------------------------------------------------------- */

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (s_simon.pending_start) {
        s_simon.pending_start = false;
        try_start();
    }

    /* switch off the panel the player lit */
    if (s_simon.flash >= 0 && (int32_t)(now - s_simon.flash_end_ms) >= 0) {
        set_pad(s_simon.flash, false);
        s_simon.flash = -1;
    }

    if (s_simon.state != ST_SHOW || (int32_t)(now - s_simon.next_ms) < 0) {
        return;
    }

    /* pos == -1 is the pause that follows completing a round */
    if (s_simon.pos < 0) {
        grow_and_show();
        return;
    }

    uint32_t dur = note_ms();
    if (!s_simon.lit) {
        int i = s_simon.seq[s_simon.pos];
        set_pad(i, true);
        aos_hal_beep(TONE[i], (int)dur);
        s_simon.lit     = true;
        s_simon.next_ms = now + dur;
        return;
    }

    set_pad(s_simon.seq[s_simon.pos], false);
    s_simon.lit = false;
    s_simon.pos++;

    if (s_simon.pos >= s_simon.len) {
        s_simon.state = ST_WAIT;
        s_simon.pos   = 0;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_simon.len);
        set_eye(buf, AOS_C_TEXT);
        return;
    }
    s_simon.next_ms = now + dur / 3;
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_simon, 0, sizeof(s_simon));
    s_simon.rng   = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    s_simon.flash = -1;

    int32_t v = 0;
    if (aos_hal_pref_get_i32("simon_best", &v) && v > 0 && v <= MAX_STEPS) {
        s_simon.best = (int)v;
    }

    lv_obj_t *page = aos_page(root);

    lv_obj_t *title = aos_label(page, _("SIMON"), aos_font_body, AOS_C_DIM);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_remove_flag(title, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *pad = lv_obj_create(page);
        lv_obj_remove_style_all(pad);
        lv_obj_set_size(pad, PAD_SIZE, PAD_SIZE);
        lv_obj_set_pos(pad, PAD_X0 + (i & 1) * (PAD_SIZE + PAD_GAP),
                            PAD_Y0 + (i >> 1) * (PAD_SIZE + PAD_GAP));
        lv_obj_set_style_radius(pad, 28, 0);
        lv_obj_set_style_bg_color(pad, lv_color_hex(DIM[i]), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
        lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
        /* CLICKED only: registering LV_EVENT_ALL also brings the deletion's
         * events, which arrive with the context already freed. */
        lv_obj_add_event_cb(pad, pad_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_simon.pad[i]       = pad;
        s_simon.pad_color[i] = DIM[i];
    }

    /* The eye in the middle covers the gap between the four panels and is also
     * the start button. It goes AFTER the panels so it ends up on top. */
    s_simon.eye = lv_obj_create(page);
    lv_obj_remove_style_all(s_simon.eye);
    lv_obj_set_size(s_simon.eye, EYE_D, EYE_D);
    lv_obj_set_pos(s_simon.eye, (AOS_SCREEN_W - EYE_D) / 2,
                   PAD_Y0 + PAD_SIZE + PAD_GAP / 2 - EYE_D / 2);
    lv_obj_set_style_radius(s_simon.eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_simon.eye, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_simon.eye, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_simon.eye, 4, 0);
    lv_obj_set_style_border_color(s_simon.eye, lv_color_hex(0x2C2C2E), 0);
    lv_obj_remove_flag(s_simon.eye, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_simon.eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_simon.eye, eye_cb, LV_EVENT_CLICKED, NULL);

    /* 68 tall and not 34: the box was only good for ONE line, so a longer word
     * wrapped onto two and the second came out clipped. The circle is
     * EYE_D=112, there is room to spare. */
    s_simon.lbl_eye = aos_label_boxed(s_simon.eye, _("TOCA"), aos_font_title,
                                      AOS_C_TEXT, EYE_D - 8, 68);
    lv_obj_center(s_simon.lbl_eye);
    lv_obj_remove_flag(s_simon.lbl_eye, LV_OBJ_FLAG_CLICKABLE);

    s_simon.lbl_foot = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                       AOS_SCREEN_W, 24);
    lv_obj_align(s_simon.lbl_foot, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_remove_flag(s_simon.lbl_foot, LV_OBJ_FLAG_CLICKABLE);

    go_idle();
    s_simon.timer = lv_timer_create(frame_cb, 40, NULL);
    return &s_simon;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_simon.timer) {
        lv_timer_delete(s_simon.timer);
        s_simon.timer = NULL;
    }
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_simon, 0, sizeof(s_simon));
}

static bool button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    (void)inst;
    if (action == AOS_BUTTON_CLICK) {
        s_simon.pending_start = true;
        return true;
    }
    return false;
}

static bool simon_init(aos_app_t *app)
{
    app->desc.id       = "aos.simon";
    app->desc.name     = "Simon";
    app->desc.icon     = "Si";
    app->desc.icon_vec = AOS_ICON_SIMON;
    app->desc.color_a  = 0x2C2C2E;
    app->desc.color_b  = 0x000000;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN | AOS_APP_FLAG_KEEP_AWAKE;
    app->desc.order    = 153;

    app->create        = create;
    app->destroy       = destroy;
    app->button        = button;
    return true;
}

AOS_APP_ENTRY(simon_init);
