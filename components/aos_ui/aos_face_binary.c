/*
 * AmoledOS - Binary face
 *
 * A BCD binary clock: six columns -tens and units of hour, minute and second-
 * each in binary from the bottom up, with the bit of weight 1 on the bottom
 * row.
 *
 * Only the dots that can exist are drawn: the tens of hours reach 2 and those
 * of minutes 5, so they carry two and three bits. Binary clocks that draw four
 * dots in every column force you to read a pile of zeros that never light up.
 *
 * Below it goes the time in numbers, which is what turns the joke into a clock
 * you can glance at.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

#define COLS        6
#define ROWS        4
#define DOT         30
#define COL_STEP    54
#define ROW_STEP    54
#define GRID_Y      118

/* Useful bits per column: h10 h1 m10 m1 s10 s1 */
static const uint8_t BITS[COLS] = { 2, 4, 3, 4, 3, 4 };
static const char *const HEAD[COLS] = { "H", "H", "M", "M", "S", "S" };

typedef struct {
    lv_obj_t *dot[COLS][ROWS];      /* row 0 = the bit of weight 8 */
    lv_obj_t *head[COLS];
    lv_obj_t *digits;
    lv_obj_t *date;
    bool      aod;
    uint8_t   last[COLS];
    char      last_hhmm[8];
} binary_t;

/* One colour per magnitude: blue the hour, green the minute, amber the second. */
static lv_color_t col_color(int col)
{
    if (col < 2) {
        return AOS_C_ACCENT;
    }
    return (col < 4) ? AOS_C_GREEN : AOS_C_ORANGE;
}

static lv_color_t col_color_aod(int col)
{
    return (col < 2) ? lv_color_hex(0x2E4A6E) : lv_color_hex(0x2E5A3A);
}

static void *create(lv_obj_t *root)
{
    binary_t *face = lv_malloc_zeroed(sizeof(binary_t));
    if (!face) {
        return NULL;
    }
    memset(face->last, 0xFF, sizeof(face->last));

    /* Six columns of 54 is 324, centred that leaves 22 of margin. */
    const int32_t x0 = (AOS_SCREEN_W - COLS * COL_STEP) / 2 + COL_STEP / 2;

    face->date = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->date, LV_ALIGN_TOP_MID, 0, 44);

    for (int c = 0; c < COLS; c++) {
        face->head[c] = aos_label_boxed(root, HEAD[c], aos_font_small,
                                        lv_color_hex(0x48484A), COL_STEP, 20);
        lv_obj_align(face->head[c], LV_ALIGN_TOP_LEFT,
                     x0 + c * COL_STEP - COL_STEP / 2, GRID_Y - 26);

        for (int r = 0; r < ROWS; r++) {
            if (r < ROWS - BITS[c]) {
                continue;               /* a bit that never lights up */
            }
            lv_obj_t *dot = lv_obj_create(root);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, DOT, DOT);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x1C1C1E), 0);
            lv_obj_align(dot, LV_ALIGN_TOP_LEFT,
                         x0 + c * COL_STEP - DOT / 2, GRID_Y + r * ROW_STEP);
            face->dot[c][r] = dot;
        }
    }

    face->digits = aos_label_boxed(root, "--:--:--", aos_font_title, AOS_C_TEXT,
                                   AOS_SCREEN_W, 36);
    lv_obj_align(face->digits, LV_ALIGN_TOP_MID, 0, GRID_Y + ROWS * ROW_STEP + 6);

    return face;
}

static void paint(binary_t *face, int col, int value)
{
    lv_color_t on  = face->aod ? col_color_aod(col) : col_color(col);
    lv_color_t off = lv_color_hex(face->aod ? 0x0A0A0B : 0x1C1C1E);

    for (int r = 0; r < ROWS; r++) {
        lv_obj_t *dot = face->dot[col][r];
        if (!dot) {
            continue;
        }
        int weight = 1 << (ROWS - 1 - r);
        lv_obj_set_style_bg_color(dot, (value & weight) ? on : off, 0);
    }
}

static void set_aod(void *ctx, bool aod)
{
    binary_t *face = (binary_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;
    memset(face->last, 0xFF, sizeof(face->last));   /* force the repaint */

    /* In dimmed mode the seconds go: they are two columns that would change
     * every second, and dimmed mode refreshes once a minute. */
    for (int c = 4; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            if (!face->dot[c][r]) {
                continue;
            }
            if (aod) {
                lv_obj_add_flag(face->dot[c][r], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(face->dot[c][r], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (aod) {
            lv_obj_add_flag(face->head[c], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(face->head[c], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_set_style_text_color(face->digits,
                                aod ? lv_color_hex(0x8A8A8A) : AOS_C_TEXT, 0);
    if (aod) {
        lv_obj_add_flag(face->date, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->date, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    binary_t *face = (binary_t *)ctx;
    if (!face) {
        return;
    }

    const int value[COLS] = {
        now->tm_hour / 10, now->tm_hour % 10,
        now->tm_min  / 10, now->tm_min  % 10,
        now->tm_sec  / 10, now->tm_sec  % 10,
    };

    int cols = face->aod ? 4 : COLS;
    for (int c = 0; c < cols; c++) {
        if (face->last[c] == (uint8_t)value[c]) {
            continue;                   /* only what changed is repainted */
        }
        face->last[c] = (uint8_t)value[c];
        paint(face, c, value[c]);
    }

    char hhmm[16];
    if (face->aod) {
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    } else {
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d:%02d", now->tm_hour, now->tm_min,
                 now->tm_sec);
    }
    lv_label_set_text(face->digits, hhmm);

    if (face->aod) {
        return;
    }

    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(face->last_hhmm, hm) != 0) {
        memcpy(face->last_hhmm, hm, sizeof(face->last_hhmm));
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d %s", aos_day_name(now->tm_wday),
                 now->tm_mday, aos_month_name(now->tm_mon));
        lv_label_set_text(face->date, buf);
    }
}

static void destroy(void *ctx)
{
    lv_free(ctx);
}

void aos_face_binary_get(aos_watchface_t *face)
{
    face->id      = "binary";
    face->name    = N_("Binaria");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
