/*
 * Sprite sheet for Topos: every sprite, and every face composed on a body,
 * over the lawn's green, magnified. It needs neither LVGL, nor the HAL, nor
 * the simulator: tp_pixel.c and tp_art.c and nothing else.
 *
 * It exists because a pixel art face cannot be judged in the code. Look at it
 * after touching tp_art.c:
 *
 *   cc -O1 -I../main tp_sheet.c ../main/tp_pixel.c ../main/tp_art.c -o /tmp/tps
 *   /tmp/tps /tmp/sheet.ppm && python3 ../../../tools/ppm2png.py /tmp/sheet.ppm /tmp/sheet.png
 */
#include "tp_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW      48          /* cell, in sprite pixels */
#define CH      58
#define COLS    9
#define ROWS    6
#define ZOOM    4

static uint16_t s_px[CW * COLS * CH * ROWS];

static void grass(tp_buf_t *b, int x, int y, int w, int h)
{
    tp_rect(b, x, y, w, h, tp_rgb(0x88C04F));
    tp_frame(b, x, y, w, h, tp_rgb(0x6FA63C));
}

/* A hole, the way tp_draw.c paints it (simplified): a dirt ring and a dark
 * opening. Enough to judge a face in context. */
static int16_t s_lip[2 * 17 + 1];

static void hole(tp_buf_t *b, int cx, int cy)
{
    tp_ellipse(b, cx + 1, cy + 2, 25, 10, tp_rgb(0x6FA63C));
    tp_ellipse(b, cx, cy, 25, 10, tp_rgb(0xDDA05E));
    tp_ellipse(b, cx, cy, 17, 6, tp_rgb(0x2C170C));
    for (int dx = -17; dx <= 17; dx++) {
        int s = tp_isqrt((17 * 17 - dx * dx) * 1024);
        s_lip[dx + 17] = (int16_t)(cy + 6 * s / (17 * 32));
    }
}

static void mole(tp_buf_t *b, int cx, int cy, int body, int ex, int mo,
                 bool helmet, bool paws, int sq)
{
    hole(b, cx, cy);
    int top = cy + 10 - 40 + (sq ? TP_SQUASH_DY : 0);
    tp_lip(b, s_lip, cx - 17, 35, cy);
    tp_img_draw(b, body, cx, top, 1);
    tp_img_draw(b, IMG_EYES0 + ex, cx, top, 1);
    tp_img_draw(b, IMG_MOUTH0 + mo, cx, top, 1);
    if (helmet) {
        tp_img_draw(b, IMG_HELMET0, cx, top + 2, 1);
    }
    tp_lip_off(b);
    if (paws) {
        tp_img_draw(b, IMG_PAW_L, cx - 12, cy + 4, 1);
        tp_img_draw(b, IMG_PAW_R, cx + 12, cy + 4, 1);
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/sheet.ppm";
    const int W = CW * COLS, H = CH * ROWS;

    if (!tp_art_init()) {
        printf("tp_art_init failed\n");
        return 1;
    }

    tp_buf_t b;
    tp_buf_init(&b, s_px, W, H);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            grass(&b, c * CW, r * CH, CW, CH);
        }
    }

    /* row 0: every expression, with the buck teeth */
    for (int e = 0; e < EX_COUNT; e++) {
        mole(&b, e * CW + CW / 2, 0 * CH + 44, IMG_BODY, e, MO_TEETH,
             false, true, 0);
    }

    /* row 1: the mouths, then whacked, gold and the hard hat */
    for (int m = 0; m < MO_COUNT; m++) {
        mole(&b, m * CW + CW / 2, 1 * CH + 44, IMG_BODY, EX_NORMAL, m,
             false, true, 0);
    }
    mole(&b, 6 * CW + CW / 2, 1 * CH + 44, IMG_BODY_SQ, EX_DIZZY, MO_OPEN,
         false, false, 1);
    mole(&b, 7 * CW + CW / 2, 1 * CH + 44, IMG_BODY_GOLD, EX_NORMAL, MO_TEETH,
         false, true, 0);
    mole(&b, 8 * CW + CW / 2, 1 * CH + 44, IMG_BODY, EX_SMUG, MO_SMIRK,
         true, true, 0);

    /* row 2: the hat's eight frames, and an angry one without it */
    for (int i = 0; i < TP_HELMET_FRAMES; i++) {
        tp_img_draw(&b, IMG_HELMET0 + i, i * CW + CW / 2, 2 * CH + CH / 2, 1);
    }
    mole(&b, 8 * CW + CW / 2, 2 * CH + 44, IMG_BODY, EX_ANGRY, MO_GRIT,
         false, true, 0);

    /* row 3: the bombs with a fuse and a spark, and the mallet's frames */
    for (int v = 0; v < 3; v++) {
        int cx = v * CW + CW / 2, cy = 3 * CH + 34;
        tp_img_draw(&b, IMG_BOMB + v, cx, cy, 1);
        for (int k = 0; k < 8; k++) {
            tp_rect(&b, cx + k - 1, cy - 15 - k / 2 - 1, 3, 3, tp_rgb(0x2A1A0C));
        }
        for (int k = 0; k < 8; k++) {
            tp_rect(&b, cx + k, cy - 15 - k / 2, 1, 1,
                    tp_rgb((k & 1) ? 0xD9B27A : 0x8A6438));
        }
        if (v < 2) {
            tp_img_draw(&b, IMG_SPARK0 + v, cx + 8, cy - 19, 1);
        }
    }
    for (int i = 0; i < TP_MALLET_FRAMES; i++) {
        tp_img_draw(&b, IMG_MALLET0 + i, (3 + 2 * i) * CW + 12, 3 * CH + 52, 1);
    }

    /* row 4: the small ones */
    {
        static const int ids[] = {
            IMG_STAR, IMG_STAR_SMALL, IMG_TWINKLE0, IMG_TWINKLE0 + 1,
            IMG_SWEAT, IMG_HEART, IMG_HEART_EMPTY, IMG_CLOCK, IMG_SPARK0 + 2,
        };
        for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
            const tp_img_t *im = tp_img(ids[i]);
            tp_img_draw(&b, ids[i], i * CW + CW / 2 - im->w / 2 + im->ax,
                        4 * CH + CH / 2 - im->h / 2 + im->ay, 1);
        }
    }

    /* row 5: gold whacked, the paws alone, the helmeted mole half out */
    mole(&b, 0 * CW + CW / 2, 5 * CH + 44, IMG_BODY_GOLD_SQ, EX_DIZZY, MO_OPEN,
         false, false, 1);
    {
        hole(&b, 1 * CW + CW / 2, 5 * CH + 44);
        int cx = 1 * CW + CW / 2, cy = 5 * CH + 44;
        int top = cy + 10 - 22;
        tp_lip(&b, s_lip, cx - 17, 35, cy);
        tp_img_draw(&b, IMG_BODY, cx, top, 1);
        tp_img_draw(&b, IMG_EYES0 + EX_WIDE, cx, top, 1);
        tp_img_draw(&b, IMG_MOUTH0 + MO_O, cx, top, 1);
        tp_img_draw(&b, IMG_HELMET0, cx, top + 2, 1);
        tp_lip_off(&b);
    }

    /* magnify and write */
    FILE *f = fopen(out, "wb");
    if (!f) {
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", W * ZOOM, H * ZOOM);
    unsigned char *line = malloc((size_t)W * ZOOM * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t c = s_px[y * W + x];
            unsigned char r = (unsigned char)(((c >> 11) & 0x1F) * 255 / 31);
            unsigned char g = (unsigned char)(((c >> 5) & 0x3F) * 255 / 63);
            unsigned char bl = (unsigned char)((c & 0x1F) * 255 / 31);
            for (int k = 0; k < ZOOM; k++) {
                line[(x * ZOOM + k) * 3 + 0] = r;
                line[(x * ZOOM + k) * 3 + 1] = g;
                line[(x * ZOOM + k) * 3 + 2] = bl;
            }
        }
        for (int k = 0; k < ZOOM; k++) {
            fwrite(line, 1, (size_t)W * ZOOM * 3, f);
        }
    }
    free(line);
    fclose(f);
    tp_art_free();
    printf("sheet -> %s (%dx%d)\n", out, W * ZOOM, H * ZOOM);
    return 0;
}
