/*
 * AmoledOS - Flappy, an example dynamic app with some substance to it.
 *
 * The same source builds two ways:
 *
 *   .so for the board       cd apps/flappy && idf.py -G 'Unix Makefiles' set-target esp32s3 && idf.py so
 *   built-in simulator app  the simulator builds it (AOS_SIM_BUILTIN)
 *
 * It uses only LVGL and aos_hal.h: no ESP-IDF. That is why it runs on both
 * sides.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Game parameters. Tuned for a 368x448 screen at 50 fps.
 * -------------------------------------------------------------------------- */
#define FRAME_MS        20
#define GROUND_H        56

#define BIRD_SIZE       34
#define BIRD_X          96

#define GRAVITY         0.55f      /* px per frame squared */
#define FLAP_IMPULSE    -7.6f
#define MAX_FALL        10.0f

#define PIPE_W          62
#define PIPE_MARGIN     70         /* minimum margin above and below the gap */

/* Difficulty: it can be overridden from the compiler (-DPIPE_GAP=...) to test
 * variants without touching the source. */
#ifndef PIPE_GAP
#define PIPE_GAP        165
#endif
#ifndef PIPE_SPEED
#define PIPE_SPEED      3
#endif
#ifndef PIPE_SPACING
#define PIPE_SPACING    186        /* horizontal distance between pipes */
#endif

#define PIPE_COUNT      3

#define COL_SKY_TOP     0x0B1B2B
#define COL_SKY_BOT     0x123A4A
#define COL_PIPE        0x30D158
#define COL_PIPE_DARK   0x1E8E3E
#define COL_GROUND      0x3A2E1F
#define COL_BIRD        0xFFD60A

typedef enum {
    STATE_READY = 0,
    STATE_PLAYING,
    STATE_DEAD,
} game_state_t;

typedef struct {
    lv_obj_t *top;
    lv_obj_t *bottom;
    int32_t   x;
    int32_t   gap_y;        /* centre of the gap */
    bool      scored;
} pipe_t;

typedef struct {
    lv_obj_t   *field;      /* playing area, without the ground */
    lv_obj_t   *bird;
    lv_obj_t   *score_label;
    lv_obj_t   *overlay;
    lv_obj_t   *overlay_title;
    lv_obj_t   *overlay_detail;
    lv_timer_t *timer;

    pipe_t      pipes[PIPE_COUNT];
    float       bird_y;
    float       bird_v;
    int32_t     field_h;

    game_state_t state;
    int          score;
    int          best;
    uint32_t     rng;
} flappy_t;

/* --------------------------------------------------------------------------
 * Random numbers of our own: the libc's rand() would force the firmware to
 * export it, and this way the game is also reproducible if debugging is
 * needed.
 * -------------------------------------------------------------------------- */
static uint32_t xorshift(flappy_t *game)
{
    uint32_t x = game->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->rng = x;
    return x;
}

static int32_t random_gap_y(flappy_t *game)
{
#ifdef FLAPPY_FIXED_GAP
    /* The gap always at the same height: useful for testing the score with a
     * bot tapping at a constant rate, which otherwise dodges nothing. */
    (void)game;
    return FLAPPY_FIXED_GAP;
#else
    int32_t span = game->field_h - 2 * PIPE_MARGIN - PIPE_GAP;
    if (span < 20) {
        span = 20;
    }
    return PIPE_MARGIN + PIPE_GAP / 2 + (int32_t)(xorshift(game) % (uint32_t)span);
#endif
}

/* -------------------------------------------------------------------------- */

static lv_obj_t *block(lv_obj_t *parent, uint32_t color, uint32_t border_color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void pipe_place(flappy_t *game, pipe_t *pipe)
{
    int32_t top_h = pipe->gap_y - PIPE_GAP / 2;
    int32_t bottom_y = pipe->gap_y + PIPE_GAP / 2;

    lv_obj_set_pos(pipe->top, pipe->x, top_h - game->field_h);
    lv_obj_set_size(pipe->top, PIPE_W, game->field_h);

    lv_obj_set_pos(pipe->bottom, pipe->x, bottom_y);
    lv_obj_set_size(pipe->bottom, PIPE_W, game->field_h);
}

static void pipes_reset(flappy_t *game)
{
    for (int i = 0; i < PIPE_COUNT; i++) {
        pipe_t *pipe = &game->pipes[i];
        pipe->x      = AOS_SCREEN_W + 60 + i * PIPE_SPACING;
        pipe->gap_y  = random_gap_y(game);
        pipe->scored = false;
        pipe_place(game, pipe);
    }
}

static void bird_apply(flappy_t *game)
{
    lv_obj_set_y(game->bird, (int32_t)game->bird_y);

    /* tilt according to the vertical velocity: it climbs pointing up and dives
     * on the way down. Purely cosmetic but it changes the feel enormously. */
    int32_t angle = (int32_t)(game->bird_v * 40.0f);
    if (angle < -300) angle = -300;
    if (angle > 800)  angle = 800;
    lv_obj_set_style_transform_rotation(game->bird, angle, 0);
}

static void overlay_show(flappy_t *game, const char *title, const char *detail)
{
    lv_label_set_text(game->overlay_title, title);
    lv_label_set_text(game->overlay_detail, detail);
    lv_obj_remove_flag(game->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(game->overlay);
}

static void game_reset(flappy_t *game)
{
    game->state   = STATE_READY;
    game->score   = 0;
    game->bird_y  = (float)(game->field_h / 2 - BIRD_SIZE / 2);
    game->bird_v  = 0.0f;

    pipes_reset(game);
    bird_apply(game);
    lv_label_set_text(game->score_label, "0");

    char detail[64];
    snprintf(detail, sizeof(detail), _("tocar para empezar\nrecord  %d"), game->best);
    overlay_show(game, "Flappy", detail);
}

static void game_over(flappy_t *game)
{
#ifdef FLAPPY_DEBUG
    printf("[flappy] muerte con y=%.1f (campo %d) puntos=%d\n",
           (double)game->bird_y, (int)game->field_h, game->score);
#endif
    game->state = STATE_DEAD;
    aos_hal_beep(220, 180);

    char detail[80];
    if (game->score > game->best) {
        game->best = game->score;
        aos_hal_pref_set_i32("flappy_best", (int32_t)game->best);
        snprintf(detail, sizeof(detail), "%d puntos\nnuevo record!", game->score);
    } else {
        snprintf(detail, sizeof(detail), "%d puntos\nrecord  %d", game->score, game->best);
    }
    overlay_show(game, _("Perdiste"), detail);
}

static bool bird_hits_pipe(const flappy_t *game, const pipe_t *pipe)
{
    const int32_t bird_left   = BIRD_X;
    const int32_t bird_right  = BIRD_X + BIRD_SIZE;
    const int32_t bird_top    = (int32_t)game->bird_y;
    const int32_t bird_bottom = bird_top + BIRD_SIZE;

    if (bird_right < pipe->x || bird_left > pipe->x + PIPE_W) {
        return false;
    }
    return bird_top < pipe->gap_y - PIPE_GAP / 2 ||
           bird_bottom > pipe->gap_y + PIPE_GAP / 2;
}

static void step(lv_timer_t *timer)
{
    flappy_t *game = (flappy_t *)lv_timer_get_user_data(timer);

    if (game->state != STATE_PLAYING) {
        return;
    }

#ifdef FLAPPY_DEBUG
    {
        static int frames; static uint64_t t0;
        if (t0 == 0) t0 = aos_hal_uptime_ms();
        if (++frames % 25 == 0) {
            uint64_t dt = aos_hal_uptime_ms() - t0;
            printf("[flappy] %d frames %.1f ms/f y=%.1f v=%.1f | tubos x/gap: "
                   "%d/%d %d/%d %d/%d | campo %d\n",
                   frames, (double)dt / frames,
                   (double)game->bird_y, (double)game->bird_v,
                   (int)game->pipes[0].x, (int)game->pipes[0].gap_y,
                   (int)game->pipes[1].x, (int)game->pipes[1].gap_y,
                   (int)game->pipes[2].x, (int)game->pipes[2].gap_y,
                   (int)game->field_h);
        }
    }
#endif

    /* the bird's physics */
    game->bird_v += GRAVITY;
    if (game->bird_v > MAX_FALL) {
        game->bird_v = MAX_FALL;
    }
    game->bird_y += game->bird_v;

    if (game->bird_y < 0.0f) {
        game->bird_y = 0.0f;
        game->bird_v = 0.0f;
    }
    bird_apply(game);

    if (game->bird_y + BIRD_SIZE >= (float)game->field_h) {
        game->bird_y = (float)(game->field_h - BIRD_SIZE);
        bird_apply(game);
        game_over(game);
        return;
    }

    /* pipes */
    for (int i = 0; i < PIPE_COUNT; i++) {
        pipe_t *pipe = &game->pipes[i];
        pipe->x -= PIPE_SPEED;

        if (pipe->x + PIPE_W < 0) {
            /* the pipe is recycled at the end of the row */
            int32_t rightmost = pipe->x;
            for (int j = 0; j < PIPE_COUNT; j++) {
                if (game->pipes[j].x > rightmost) {
                    rightmost = game->pipes[j].x;
                }
            }
            pipe->x      = rightmost + PIPE_SPACING;
            pipe->gap_y  = random_gap_y(game);
            pipe->scored = false;
        }

        pipe_place(game, pipe);

        if (!pipe->scored && pipe->x + PIPE_W < BIRD_X) {
            pipe->scored = true;
            game->score++;
            char buf[12];
            snprintf(buf, sizeof(buf), "%d", game->score);
            lv_label_set_text(game->score_label, buf);
            aos_hal_beep(1800, 25);
        }

        if (bird_hits_pipe(game, pipe)) {
            game_over(game);
            return;
        }
    }
}

static void tap_cb(lv_event_t *event)
{
    flappy_t *game = (flappy_t *)lv_event_get_user_data(event);

    switch (game->state) {
    case STATE_READY:
        lv_obj_add_flag(game->overlay, LV_OBJ_FLAG_HIDDEN);
        game->state  = STATE_PLAYING;
        game->bird_v = FLAP_IMPULSE;
        aos_hal_beep(1200, 20);
        break;

    case STATE_PLAYING:
        game->bird_v = FLAP_IMPULSE;
        aos_hal_beep(1200, 15);
        break;

    case STATE_DEAD:
        game_reset(game);
        break;
    }
}

/* -------------------------------------------------------------------------- */

static void *flappy_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    flappy_t *game = lv_malloc_zeroed(sizeof(flappy_t));
    if (!game) {
        return NULL;
    }

    /* seed: the uptime is enough and it does not depend on the libc */
    game->rng = (uint32_t)aos_hal_uptime_ms() | 1u;

    int32_t best = 0;
    if (aos_hal_pref_get_i32("flappy_best", &best)) {
        game->best = (int)best;
    }

    lv_obj_set_style_bg_color(root, lv_color_hex(COL_SKY_TOP), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(COL_SKY_BOT), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    game->field_h = lv_obj_get_height(root) - GROUND_H;
    if (game->field_h < 120) {       /* in case the layout has not run yet */
        game->field_h = AOS_SCREEN_H - 30 - GROUND_H;
    }

    /* playing field: it clips the pipes so they are not drawn over the ground */
    game->field = lv_obj_create(root);
    lv_obj_remove_style_all(game->field);
    lv_obj_set_size(game->field, lv_pct(100), game->field_h);
    lv_obj_set_pos(game->field, 0, 0);
    lv_obj_remove_flag(game->field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(game->field, true, 0);

    for (int i = 0; i < PIPE_COUNT; i++) {
        game->pipes[i].top    = block(game->field, COL_PIPE, COL_PIPE_DARK);
        game->pipes[i].bottom = block(game->field, COL_PIPE, COL_PIPE_DARK);
    }

    /* bird */
    game->bird = lv_obj_create(game->field);
    lv_obj_remove_style_all(game->bird);
    lv_obj_set_size(game->bird, BIRD_SIZE, BIRD_SIZE);
    lv_obj_set_x(game->bird, BIRD_X);
    lv_obj_set_style_bg_color(game->bird, lv_color_hex(COL_BIRD), 0);
    lv_obj_set_style_bg_opa(game->bird, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(game->bird, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(game->bird, 2, 0);
    lv_obj_set_style_border_color(game->bird, lv_color_hex(0xC89000), 0);
    lv_obj_set_style_transform_pivot_x(game->bird, BIRD_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(game->bird, BIRD_SIZE / 2, 0);

    lv_obj_t *eye = lv_obj_create(game->bird);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, 7, 7);
    lv_obj_set_style_bg_color(eye, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -6, 8);

    lv_obj_t *beak = lv_obj_create(game->bird);
    lv_obj_remove_style_all(beak);
    lv_obj_set_size(beak, 12, 6);
    lv_obj_set_style_bg_color(beak, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_bg_opa(beak, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(beak, 3, 0);
    lv_obj_align(beak, LV_ALIGN_RIGHT_MID, 7, 3);

    /* ground */
    lv_obj_t *ground = lv_obj_create(root);
    lv_obj_remove_style_all(ground);
    lv_obj_set_size(ground, lv_pct(100), GROUND_H);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* In LVGL 9 every lv_obj is born clickable, so this ground -which is pure
     * decoration- showed up as a dead 56 px touch area at the bottom of the
     * screen. It broke nothing because the game's touch layer is created
     * afterwards and ends up on top, but it dirtied the audit and it is
     * exactly the sort of ornament that one day eats somebody else's touch. */
    lv_obj_remove_flag(ground, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ground, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ground, lv_color_hex(0x6B5B3E), 0);
    lv_obj_set_style_border_width(ground, 3, 0);
    lv_obj_set_style_border_side(ground, LV_BORDER_SIDE_TOP, 0);

    /* score */
    game->score_label = lv_label_create(root);
    lv_label_set_text(game->score_label, "0");
    lv_obj_set_style_text_color(game->score_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(game->score_label, &aos_montserrat_48, 0);
    lv_obj_align(game->score_label, LV_ALIGN_TOP_MID, 0, 22);

    /* start and game over panels */
    game->overlay = lv_obj_create(root);
    lv_obj_remove_style_all(game->overlay);
    lv_obj_set_size(game->overlay, 250, 150);
    lv_obj_center(game->overlay);
    lv_obj_set_style_bg_color(game->overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(game->overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(game->overlay, 24, 0);
    lv_obj_remove_flag(game->overlay, LV_OBJ_FLAG_CLICKABLE);

    game->overlay_title = lv_label_create(game->overlay);
    lv_obj_set_style_text_color(game->overlay_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(game->overlay_title, &aos_montserrat_28, 0);
    lv_obj_align(game->overlay_title, LV_ALIGN_TOP_MID, 0, 24);

    game->overlay_detail = lv_label_create(game->overlay);
    lv_obj_set_style_text_color(game->overlay_detail, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_align(game->overlay_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(game->overlay_detail, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* A transparent layer above everything to receive the touches. In LVGL 9
     * every lv_obj is born clickable, so the playing field, the pipes and the
     * ground would eat the touch before it reached the root. */
    lv_obj_t *touch = lv_obj_create(root);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(touch, 0, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch, tap_cb, LV_EVENT_PRESSED, game);
    lv_obj_move_foreground(touch);

    game_reset(game);
    game->timer = lv_timer_create(step, FRAME_MS, game);
    return game;
}

static void flappy_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    flappy_t *game = (flappy_t *)inst;
    if (!game) {
        return;
    }
    if (game->timer) {
        lv_timer_delete(game->timer);
    }
    lv_free(game);
}

static void flappy_hide(aos_app_t *self, void *inst)
{
    (void)self;
    flappy_t *game = (flappy_t *)inst;
    if (game && game->state == STATE_PLAYING) {
        game->state = STATE_DEAD;   /* leaving mid-game should not count */
        overlay_show(game, _("Pausa"), _("tocar para reiniciar"));
    }
}

static bool flappy_init(aos_app_t *app)
{
    app->desc.id      = "demo.flappy";
    app->desc.name    = "Flappy";
    app->desc.icon    = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_BIRD;
    app->desc.color_a = 0x30D158;
    app->desc.color_b = 0x0E7A32;
    app->desc.order   = 150;
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE;

    app->create  = flappy_create;
    app->destroy = flappy_destroy;
    app->hide    = flappy_hide;
    return true;
}

AOS_APP_ENTRY(flappy_init);
