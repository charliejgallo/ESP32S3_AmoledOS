/*
 * Test bench for Claude Jump's costumes.
 *
 * Draws all sixteen in a grid and writes a PPM. It needs neither LVGL, nor the
 * HAL, nor the simulator: it is cj_pixel.c and cj_skins.c and nothing else. It
 * serves two different purposes and both matter:
 *
 *   1. Looking at all sixteen together, which is the only way to see whether
 *      any of them is lost against the background or whether two came out too
 *      similar.
 *   2. VERIFYING THAT NONE OF THEM RUNS OUTSIDE HERO_BOX_*. Each costume is
 *      drawn onto a canvas larger than its box, with the box painted a
 *      sentinel colour; if on finishing a pixel has been drawn outside the box,
 *      the program says so with its coordinates and exits with an error. That
 *      is the defect that on the board shows up as a trail stuck on the screen,
 *      and that by looking at the game is indistinguishable from a badly
 *      recorded dirty rectangle.
 *
 *   cc -I../main -I../../../components/aos_ui/include \
 *      cj_skins_harness.c ../main/cj_pixel.c ../main/cj_skins.c -o /tmp/cjsk
 *   /tmp/cjsk /tmp/skins.ppm
 */
#include "cjump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_W  44
#define CELL_H  46
#define COLS    4
#define ROWS    ((CJ_SKINS + COLS - 1) / COLS)

/* Guard margin around the critter's box: if anything is drawn here, the
 * costume runs outside HERO_BOX_* and in the game it would leave a trail. */
#define GUARD   8

static uint16_t cell[CELL_W * CELL_H];

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/skins.ppm";
    int W = CELL_W * COLS;
    int H = CELL_H * ROWS;

    uint16_t *img = calloc((size_t)W * H, sizeof(uint16_t));
    if (!img) {
        return 1;
    }

    int fallos = 0;

    for (int s = 0; s < CJ_SKINS; s++) {
        cj_buf_t b;
        cj_buf_init(&b, cell, CELL_W, CELL_H);

        /* Sentinel: the whole canvas in a colour the drawing does not use.
         * Whatever is left that colour at the end is "nothing was drawn
         * here". */
        const uint16_t centinela = 0xF81F;      /* pure magenta */
        cj_fill(&b, centinela);

        int hx = (CELL_W - HERO_W) / 2;
        int hy = GUARD + HERO_BOX_H - HERO_H;

        cj_rect_t box;
        cj_hero_box(hx, hy, &box);

        cj_hero_draw(&b, hx, hy, s, 0, 0, 0, false);

        /* Every drawn pixel has to fall inside the box. */
        for (int y = 0; y < CELL_H; y++) {
            for (int x = 0; x < CELL_W; x++) {
                if (cell[y * CELL_W + x] == centinela) {
                    continue;
                }
                if (x < box.x0 || x >= box.x1 || y < box.y0 || y >= box.y1) {
                    printf("skin %2d (%s): pixel fuera de la caja en %d,%d "
                           "(caja %d,%d..%d,%d)\n",
                           s, cj_skins[s].name, x, y,
                           box.x0, box.y0, box.x1, box.y1);
                    fallos++;
                    y = CELL_H;         /* one per costume is enough */
                    break;
                }
            }
        }

        /* And now, for the image, a background you can actually look at. */
        cj_fill(&b, cj_rgb(0x1A1E2A));
        cj_frame(&b, 0, 0, CELL_W, CELL_H, cj_rgb(0x3A3A4A));
        cj_hero_draw(&b, hx, hy, s, 0, 0, 0, false);

        int cx = (s % COLS) * CELL_W;
        int cy = (s / COLS) * CELL_H;
        for (int y = 0; y < CELL_H; y++) {
            memcpy(img + (size_t)(cy + y) * W + cx, cell + (size_t)y * CELL_W,
                   CELL_W * sizeof(uint16_t));
        }
    }

    FILE *f = fopen(out, "wb");
    if (!f) {
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = img[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((c >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (unsigned char)(((c >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (unsigned char)((c & 0x1F) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    free(img);

    printf("%d disfraces -> %s (%dx%d), %d fuera de la caja\n",
           CJ_SKINS, out, W, H, fallos);
    return fallos ? 1 : 0;
}
