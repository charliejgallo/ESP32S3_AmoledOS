/*
 * AmoledOS - Words: the time written out, in Spanish, English or German.
 *
 * A grid of 11x10 letters where the ones that spell the time are lit: "SON
 * LAS DIEZ Y CUARTO", "IT IS A QUARTER PAST TEN", "ES IST VIERTEL NACH ZEHN".
 * The time is rounded down to five minutes and the minutes left over are
 * four dots under the grid. It follows the watch's language; a tap goes
 * through the three, and back to following it.
 *
 * This is the face that was written once, worked, and was taken out of the
 * firmware: one LVGL object per letter is 110 objects, and with the Clima app
 * open the internal RAM fell to 26 KB and the panel's DMA buffer stopped
 * fitting. The note it left said "if it is ever redone, the grid is drawn in
 * a canvas". So it is: one object, the letters drawn into its buffer, and
 * only the ones that change drawn again, fading over half a second.
 *
 * Each language has its own grid (they are the classic ones) and its own way
 * of saying the time: Spanish and English count from the hour until half
 * past and to the next one after; German says "fuenf vor halb elf".
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLS        11
#define ROWS        10
#define CELLS       (COLS * ROWS)
#define CELL_W      32
#define CELL_H      38
#define GRID_X      ((AOS_SCREEN_W - COLS * CELL_W) / 2)
#define GRID_Y      20
#define DOTS_Y      (GRID_Y + ROWS * CELL_H + 22)
#define FADE_MS     450

#define PREF_LANG   "fw_lang"       /* 0 the watch's, 1 es, 2 en, 3 de */

enum { L_ES = 0, L_EN, L_DE, L_COUNT };

typedef struct { uint8_t row, col, len; } word_t;

/* -------------------------------------------------------------------------- */
/* The three grids                                                            */
/* -------------------------------------------------------------------------- */

static const char *const GRID[L_COUNT][ROWS] = {
    [L_ES] = {
        "ESONELASUNA", "DOSITRESORE", "CUATROCINCO", "SEISASIETEN", "OCHONUEVEYO",
        "LADIEZSONCE", "DOCELYMENOS", "OVEINTEDIEZ", "VEINTICINCO", "MEDIACUARTO",
    },
    [L_EN] = {
        "ITLISASAMPM", "ACQUARTERDC", "TWENTYFIVEX", "HALFSTENFTO", "PASTERUNINE",
        "ONESIXTHREE", "FOURFIVETWO", "EIGHTELEVEN", "SEVENTWELVE", "TENSEOCLOCK",
    },
    [L_DE] = {
        "ESKISTAF\xC3\x9CNF", "ZEHNZWANZIG", "DREIVIERTEL", "VORFUNKNACH",
        "HALBAELF\xC3\x9CNF", "EINSXAMZWEI", "DREIPMJVIER", "SECHSNLACHT",
        "SIEBENZW\xC3\x96LF", "ZEHNEUNKUHR",
    },
};

/* the hours, 1..12 at [0..11] */
static const word_t HOUR[L_COUNT][12] = {
    [L_ES] = { {0,8,3}, {1,0,3}, {1,4,4}, {2,0,6}, {2,6,5}, {3,0,4},
               {3,5,5}, {4,0,4}, {4,4,5}, {5,2,4}, {5,7,4}, {6,0,4} },
    [L_EN] = { {5,0,3}, {6,8,3}, {5,6,5}, {6,0,4}, {6,4,4}, {5,3,3},
               {8,0,5}, {7,0,5}, {4,7,4}, {9,0,3}, {7,5,6}, {8,5,6} },
    [L_DE] = { {5,0,4}, {5,7,4}, {6,0,4}, {6,7,4}, {4,7,4}, {7,0,5},
               {8,0,6}, {7,7,4}, {9,3,4}, {9,0,4}, {4,5,3}, {8,6,5} },
};

static void light(uint8_t *lit, word_t w)
{
    for (int i = 0; i < w.len; i++) {
        lit[w.row * COLS + w.col + i] = 1;
    }
}

/* Which letters spell h:m (m already a multiple of five) in each language. */
static void spell(int lang, int h, int m, uint8_t *lit)
{
    memset(lit, 0, CELLS);
    int step = m / 5;               /* 0..11 */

    if (lang == L_ES) {
        static const word_t ES = {0,0,2}, SON = {0,1,3}, LA = {0,5,2}, LAS = {0,5,3},
                            Y = {6,5,1}, MENOS = {6,6,5}, VEINTE = {7,1,6}, DIEZ = {7,7,4},
                            VEINTICINCO = {8,0,11}, CINCO = {8,6,5}, MEDIA = {9,0,5},
                            CUARTO = {9,5,6};
        static const word_t *const PART[7] = { NULL, &CINCO, &DIEZ, &CUARTO, &VEINTE,
                                               &VEINTICINCO, &MEDIA };
        if (step > 6) {
            h++;                    /* "las once menos diez" */
        }
        int h12 = (h + 11) % 12;    /* 0 = one o'clock */
        light(lit, h12 == 0 ? ES : SON);
        light(lit, h12 == 0 ? LA : LAS);
        light(lit, HOUR[L_ES][h12]);
        if (step >= 1 && step <= 6) {
            light(lit, Y);
            light(lit, *PART[step]);
        } else if (step > 6) {
            light(lit, MENOS);
            light(lit, *PART[12 - step]);
        }
    } else if (lang == L_EN) {
        static const word_t IT = {0,0,2}, IS = {0,3,2}, A = {1,0,1}, QUARTER = {1,2,7},
                            TWENTY = {2,0,6}, FIVE = {2,6,4}, HALF = {3,0,4}, TEN = {3,5,3},
                            TO = {3,9,2}, PAST = {4,0,4}, OCLOCK = {9,5,6};
        if (step > 6) {
            h++;
        }
        int h12 = (h + 11) % 12;
        light(lit, IT);
        light(lit, IS);
        light(lit, HOUR[L_EN][h12]);
        int k = step <= 6 ? step : 12 - step;
        switch (k) {
        case 0: light(lit, OCLOCK); break;
        case 1: light(lit, FIVE); break;
        case 2: light(lit, TEN); break;
        case 3: light(lit, A); light(lit, QUARTER); break;
        case 4: light(lit, TWENTY); break;
        case 5: light(lit, TWENTY); light(lit, FIVE); break;
        case 6: light(lit, HALF); break;
        }
        if (step >= 1) {
            light(lit, step <= 6 ? PAST : TO);
        }
    } else {
        static const word_t ES = {0,0,2}, IST = {0,3,3}, FUENF = {0,7,4}, ZEHN = {1,0,4},
                            ZWANZIG = {1,4,7}, VIERTEL = {2,4,7}, VOR = {3,0,3},
                            NACH = {3,7,4}, HALB = {4,0,4}, EIN = {5,0,3}, UHR = {9,8,3};
        /* from "fuenf vor halb" on, German already names the next hour */
        if (step >= 5) {
            h++;
        }
        int h12 = (h + 11) % 12;
        light(lit, ES);
        light(lit, IST);
        /* "ein Uhr", but "fuenf nach eins" */
        light(lit, (step == 0 && h12 == 0) ? EIN : HOUR[L_DE][h12]);
        switch (step) {
        case 0:  light(lit, UHR); break;
        case 1:  light(lit, FUENF); light(lit, NACH); break;
        case 2:  light(lit, ZEHN); light(lit, NACH); break;
        case 3:  light(lit, VIERTEL); light(lit, NACH); break;
        case 4:  light(lit, ZWANZIG); light(lit, NACH); break;
        case 5:  light(lit, FUENF); light(lit, VOR); light(lit, HALB); break;
        case 6:  light(lit, HALB); break;
        case 7:  light(lit, FUENF); light(lit, NACH); light(lit, HALB); break;
        case 8:  light(lit, ZWANZIG); light(lit, VOR); break;
        case 9:  light(lit, VIERTEL); light(lit, VOR); break;
        case 10: light(lit, ZEHN); light(lit, VOR); break;
        case 11: light(lit, FUENF); light(lit, VOR); break;
        }
    }
}

/* -------------------------------------------------------------------------- */

typedef struct {
    lv_obj_t  *canvas;
    uint16_t  *buf;
    bool       aod;
    int        lang;                /* the one shown */
    int        pref;                /* 0 the watch's, else lang + 1 */
    char       letter[CELLS][3];    /* UTF-8, at most two bytes and the NUL */
    uint8_t    want[CELLS];         /* lit or not */
    uint8_t    level[CELLS];        /* 0..255, where the fade is */
    uint8_t    drawn[CELLS];        /* the level on the canvas; 0xFF never drawn... */
    bool       fresh;               /* nothing drawn yet: draw every cell */
    int        dots, dots_drawn;    /* minutes over the five, 0..4 */
    int        last_h, last_m;
} palabras_t;

static int watch_lang(void)
{
    const char *code = aos_i18n_current();
    if (code && code[0] == 'e' && code[1] == 'n') return L_EN;
    if (code && code[0] == 'd' && code[1] == 'e') return L_DE;
    return L_ES;
}

static void load_letters(palabras_t *p)
{
    for (int r = 0; r < ROWS; r++) {
        const unsigned char *s = (const unsigned char *)GRID[p->lang][r];
        for (int c = 0; c < COLS; c++) {
            char *out = p->letter[r * COLS + c];
            int n = (*s >= 0xC0) ? 2 : 1;       /* the umlauts are two bytes */
            memcpy(out, s, (size_t)n);
            out[n] = '\0';
            s += n;
        }
    }
}

/* Draws the cells whose level is not the one on the canvas, and the dots. */
static void paint(palabras_t *p)
{
    lv_layer_t layer;
    bool open = false;
    /* dimmed, the letters that are off are not there at all: black is free */
    lv_color_t off = p->aod ? lv_color_hex(0x000000) : lv_color_hex(0x2C2C30);
    lv_color_t on  = p->aod ? lv_color_hex(0x9A9A9A) : lv_color_hex(0xFFFFFF);

    for (int i = 0; i < CELLS; i++) {
        if (!p->fresh && p->drawn[i] == p->level[i]) {
            continue;
        }
        if (!open) {
            lv_canvas_init_layer(p->canvas, &layer);
            open = true;
        }
        int x = GRID_X + (i % COLS) * CELL_W, y = GRID_Y + (i / COLS) * CELL_H;
        lv_area_t cell = { x, y, x + CELL_W - 1, y + CELL_H - 1 };
        lv_draw_rect_dsc_t bg;
        lv_draw_rect_dsc_init(&bg);
        bg.bg_color = lv_color_hex(0x000000);
        lv_draw_rect(&layer, &bg, &cell);

        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = aos_font_title;
        dsc.color = lv_color_mix(on, off, p->level[i]);
        dsc.text = p->letter[i];
        dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t text = { x, y + (CELL_H - lv_font_get_line_height(aos_font_title)) / 2,
                           x + CELL_W - 1, y + CELL_H - 1 };
        lv_draw_label(&layer, &dsc, &text);
        p->drawn[i] = p->level[i];
    }
    if (p->fresh || p->dots != p->dots_drawn) {
        if (!open) {
            lv_canvas_init_layer(p->canvas, &layer);
            open = true;
        }
        for (int d = 0; d < 4; d++) {
            int cx = AOS_SCREEN_W / 2 + (d * 2 - 3) * 14;
            lv_area_t a = { cx - 4, DOTS_Y - 4, cx + 4, DOTS_Y + 4 };
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.radius = LV_RADIUS_CIRCLE;
            dot.bg_color = d < p->dots ? on : off;
            lv_draw_rect(&layer, &dot, &a);
        }
        p->dots_drawn = p->dots;
    }
    if (open) {
        lv_canvas_finish_layer(p->canvas, &layer);
    }
    p->fresh = false;
}

static void set_time(palabras_t *p, const struct tm *now, bool jump)
{
    spell(p->lang, now->tm_hour, now->tm_min - now->tm_min % 5, p->want);
    p->dots = now->tm_min % 5;
    p->last_h = now->tm_hour;
    p->last_m = now->tm_min;
    if (jump || p->aod) {
        for (int i = 0; i < CELLS; i++) {
            p->level[i] = p->want[i] ? 255 : 0;
        }
    }
    if (p->aod || jump) {
        paint(p);           /* lit, frame() fades the rest in */
    }
}

static void set_lang(palabras_t *p)
{
    p->lang = p->pref ? p->pref - 1 : watch_lang();
    load_letters(p);
    p->fresh = true;
    struct tm now;
    aos_hal_time_now(&now);
    set_time(p, &now, true);
}

static void *create(lv_obj_t *root)
{
    palabras_t *p = lv_malloc_zeroed(sizeof(palabras_t));
    if (!p) {
        return NULL;
    }
    p->buf = calloc(AOS_SCREEN_W * AOS_SCREEN_H, sizeof(uint16_t));
    if (!p->buf) {
        lv_free(p);
        return NULL;
    }
    p->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(p->canvas, p->buf, AOS_SCREEN_W, AOS_SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(p->canvas, 0, 0);

    int32_t pref = 0;
    aos_hal_pref_get_i32(PREF_LANG, &pref);
    p->pref = (pref >= 0 && pref <= L_COUNT) ? (int)pref : 0;
    p->dots_drawn = -1;
    set_lang(p);
    return p;
}

static void refresh(void *ctx, const struct tm *now)
{
    palabras_t *p = ctx;
    if (!p || (now->tm_hour == p->last_h && now->tm_min == p->last_m)) {
        return;
    }
    set_time(p, now, false);
}

/* The fade: every letter moves towards lit or off, and only those are drawn
 * again. Once they are all there, a frame costs a loop over 110 bytes. */
static void frame(void *ctx, uint32_t dt_ms)
{
    palabras_t *p = ctx;
    if (!p || p->aod) {
        return;
    }
    int step = (int)(dt_ms * 255 / FADE_MS);
    bool moved = p->dots != p->dots_drawn;
    for (int i = 0; i < CELLS; i++) {
        int goal = p->want[i] ? 255 : 0;
        if (p->level[i] != goal) {
            int v = p->level[i] + (goal > p->level[i] ? step : -step);
            p->level[i] = (uint8_t)LV_CLAMP(0, v, 255);
            moved = true;
        }
    }
    if (moved) {
        paint(p);
    }
}

static void set_aod(void *ctx, bool aod)
{
    palabras_t *p = ctx;
    if (!p || p->aod == aod) {
        return;
    }
    p->aod = aod;
    p->fresh = true;
    struct tm now;
    aos_hal_time_now(&now);
    set_time(p, &now, true);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    (void)x;
    (void)y;
    palabras_t *p = ctx;
    if (!p || what != AOS_FACE_TOUCH_TAP) {
        return false;
    }
    p->pref = (p->pref + 1) % (L_COUNT + 1);
    aos_hal_pref_set_i32(PREF_LANG, p->pref);
    set_lang(p);
    static const char *const NAME[L_COUNT] = { "Espa\xC3\xB1ol", "English", "Deutsch" };
    aos_ui_toast(p->pref ? NAME[p->lang] : _("El idioma del reloj"), 1200);
    return true;
}

static void destroy(void *ctx)
{
    palabras_t *p = ctx;
    if (!p) {
        return;
    }
    free(p->buf);           /* the canvas goes with the root; its buffer is ours */
    lv_free(p);
}

static bool palabras_get(aos_watchface_t *face)
{
    face->id       = "face.palabras";
    face->name     = N_("Palabras");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 50;
    return true;
}

AOS_FACE_ENTRY(palabras_get);
