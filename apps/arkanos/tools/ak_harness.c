/*
 * ARKANOS - test bench without a screen
 *
 * It compiles the game WITHOUT LVGL and without SDL
 * (ak_play/ak_draw/ak_pixel/ak_level depend on nothing but the HAL, and of
 * that only on the accelerometer) and makes it play itself for thousands of
 * frames. It serves two purposes:
 *
 *  1. VERIFYING THE DIRTY LIST. On every frame it draws twice: once by the
 *     fast path (restoring only the recorded rectangles) and once rebuilding
 *     the whole screen from the background. If the two buffers do not come out
 *     identical, somebody moved without recording their rectangle, which is
 *     THE bug of this scheme and on screen looks like a dirty trail.
 *
 *  2. MEASURING how much is saved: what percentage of the screen is upscaled
 *     and invalidated per frame.
 *
 *   cc -O2 -I <main> -I <aos_hal/include> ak_harness.c ../main/ak_*.c -o /tmp/akh
 *   /tmp/akh [frames] [level] [capture_prefix]
 */
#include "arkanos.h"
#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- the little of the system the game uses ------------------------------- */

static int s_beeps;

void ak_sfx(int freq_hz, int ms)
{
    (void)freq_hz;
    (void)ms;
    s_beeps++;
}

/* The accelerometer can be faked, to test the axis mapping. */
static float s_ax, s_ay;
static bool  s_imu_on;

bool aos_hal_imu_read(aos_imu_t *out)
{
    if (!s_imu_on) {
        return false;
    }
    out->ax = s_ax;
    out->ay = s_ay;
    out->az = 1.0f;
    out->gx = out->gy = out->gz = 0.0f;
    out->temperature = 25.0f;
    return true;
}

/* --- image ----------------------------------------------------------------*/

static void dump_ppm(const uint16_t *px, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", AK_W * AK_SCALE, AK_H * AK_SCALE);
    for (int y = 0; y < AK_H * AK_SCALE; y++) {
        for (int x = 0; x < AK_W * AK_SCALE; x++) {
            uint16_t c = px[y * AK_W * AK_SCALE + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(((c >> 11) & 0x1F) * 255 / 31);
            rgb[1] = (unsigned char)(((c >> 5) & 0x3F) * 255 / 63);
            rgb[2] = (unsigned char)((c & 0x1F) * 255 / 31);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* --- axis mapping ---------------------------------------------------------
 *
 * Which of the sensor's two axes runs across the screen depends on the
 * mounting, and on this board it turned out to be imu.ay and not imu.ax
 * (measured on 28/08: tilting forwards and backwards moved the paddle). The
 * setting lives in g.tilt_axis. This prints the table of the four options and
 * serves as proof that the switch does what it says.
 * -------------------------------------------------------------------------- */

static const char *const eje_txt[4] = { "EJE X+", "EJE X-", "EJE Y+", "EJE Y-" };

static int probe(ak_t *gg, int axis, int cual)
{
    gg->control = CTRL_TILT;
    gg->autoplay = 0;
    gg->tilt_axis = (uint8_t)axis;
    gg->tilt_zeroed = 0;
    gg->imu_skip = 0;
    gg->pad_x = FX(AK_W / 2);

    s_imu_on = 1;
    s_ax = s_ay = 0.0f;
    for (int i = 0; i < 10; i++) {
        ak_step(gg);
    }
    /* +0.2 g, well above the dead zone of 35 mg */
    if (cual == 0) {
        s_ax = 0.2f;
    } else {
        s_ay = 0.2f;
    }
    for (int i = 0; i < 40; i++) {
        ak_step(gg);
    }
    s_imu_on = 0;

    int d = UNFX(gg->pad_x) - AK_W / 2;
    return (d > 4) ? 1 : (d < -4) ? -1 : 0;
}

static void probe_axes(void)
{
    static ak_t gg;
    static uint16_t f2[AK_W * AK_H], b2[AK_W * AK_H];

    printf("mapeo del acelerometro (que hace cada opcion del chip EJE):\n");
    for (int axis = 0; axis < 4; axis++) {
        memset(&gg, 0, sizeof(gg));
        ak_buf_init(&gg.fb, f2, AK_W, AK_H);
        ak_buf_init(&gg.bg, b2, AK_W, AK_H);
        gg.rng = 99;
        ak_game_start(&gg);
        gg.state = ST_PLAY;

        int rx = probe(&gg, axis, 0);
        int ry = probe(&gg, axis, 1);
        static const char *const dir[3] = { "izquierda", "quieta   ", "derecha  " };
        printf("  %s :  ax +0.2g -> %s   ay +0.2g -> %s\n",
               eje_txt[axis], dir[rx + 1], dir[ry + 1]);
    }
    printf("\n");
}

/* --- the loop -------------------------------------------------------------*/

static ak_t g;
static uint16_t fb[AK_W * AK_H];
static uint16_t bg[AK_W * AK_H];
static uint16_t big[AK_W * AK_H * AK_SCALE * AK_SCALE];
static uint16_t ref[AK_W * AK_H];       /* the "rebuilt whole" version */

int main(int argc, char **argv)
{
    int cuadros = argc > 1 ? atoi(argv[1]) : 3000;
    int nivel   = argc > 2 ? atoi(argv[2]) : 0;
    const char *shots = argc > 3 ? argv[3] : NULL;
    /* AK_TORPE=1 switches the autopilot off now and then, so the ball falls
     * and the lose-a-life and game-over paths are exercised too. */
    int torpe = getenv("AK_TORPE") ? 1 : 0;

    probe_axes();

    ak_buf_init(&g.fb, fb, AK_W, AK_H);
    ak_buf_init(&g.bg, bg, AK_W, AK_H);
    g.rng = 12345;
    g.autoplay = 1;
    g.control = CTRL_TOUCH;
    g.show_fps = 1;

    ak_game_start(&g);
    if (nivel) {
        ak_load_level(&g, nivel);
    }

    long area_total = 0, area_max = 0;
    int fallos = 0, niveles = 0, muertes = 0;
    uint8_t nivel_prev = g.level;
    static const ak_rect_t entera = { 0, 0, AK_W, AK_H };

    for (int f = 0; f < cuadros; f++) {
        if (g.state == ST_OVER || g.state == ST_WIN) {
            /* keep measuring: it restarts and carries on */
            if (g.state == ST_WIN) {
                ak_load_level(&g, ak_level_count());
            } else {
                muertes++;
                ak_game_start(&g);
            }
        }
        if (torpe) {
            g.autoplay = ((f / 300) % 2) ? 0 : 1;
        }
        /* It switches the score's highlight on and off the way the app does
         * when a finger rests on the bar, so that strip's repaint (which is
         * not restored on every frame) enters the comparison. */
        {
            uint8_t hold = ((f / 97) % 2) ? 1 : 0;
            if (hold != g.hud_hold) {
                g.hud_hold = hold;
                g.hud_dirty = 1;
            }
        }
        ak_step(&g);

        /* ---- fast path ---- */
        g.d_push = g.d_prev;
        ak_dirty_join(&g.d_push, &g.d_bg);
        if (g.d_push.all) {
            ak_restore(fb, bg, &entera);
        } else {
            for (int i = 0; i < g.d_push.n; i++) {
                ak_restore(fb, bg, &g.d_push.r[i]);
            }
        }
        ak_draw_movers(&g);
        bool hud = g.hud_dirty != 0;
        if (hud) {
            ak_draw_hud(&g);
        }
        ak_dirty_join(&g.d_push, &g.d_cur);

        int area = ak_dirty_area(&g.d_push);
        area_total += area;
        if (area > area_max) {
            area_max = area;
        }
        if (g.d_push.all) {
            ak_expand(fb, big, &entera);
        } else {
            for (int i = 0; i < g.d_push.n; i++) {
                ak_expand(fb, big, &g.d_push.r[i]);
            }
        }
        /* ---- reference: the whole screen, from scratch ----
         * NOTE: g.last_area is updated AFTER comparing. The FPS counter is
         * drawn by the game itself, so if it were updated first, the reference
         * pass would write a different number and the comparison would fail
         * because of the test bench and not because of the game. */
        ak_dirty_t save_push = g.d_push;
        ak_dirty_t save_cur  = g.d_cur;
        memcpy(ref, bg, sizeof(ref));
        g.fb.px = ref;
        ak_draw_movers(&g);
        g.hud_dirty = 1;
        ak_draw_hud(&g);
        g.fb.px = fb;
        g.d_push = save_push;
        g.d_cur  = save_cur;
        g.hud_dirty = 0;

        if (memcmp(fb, ref, sizeof(ref)) != 0) {
            int primero = -1, cuantos = 0;
            for (int i = 0; i < AK_W * AK_H; i++) {
                if (fb[i] != ref[i]) {
                    if (primero < 0) {
                        primero = i;
                    }
                    cuantos++;
                }
            }
            if (fallos < 8) {
                printf("CUADRO %d: %d pixeles sin anotar, el primero en "
                       "(%d,%d) estado=%d rects=%d\n",
                       f, cuantos, primero % AK_W, primero / AK_W,
                       g.state, g.d_push.n);
            }
            fallos++;
            memcpy(fb, ref, sizeof(ref));   /* carry on measuring from something sane */
        }

        g.last_area = (uint16_t)(area * 100 / (AK_W * AK_H));
        g.d_prev = g.d_cur;
        ak_dirty_reset(&g.d_bg);

        if (g.level != nivel_prev) {
            niveles++;
            nivel_prev = g.level;
        }
        if (shots && (f % 400) == 60) {
            char path[256];
            snprintf(path, sizeof(path), "%s%02d.ppm", shots, f / 400);
            dump_ppm(big, path);
        }
    }

    printf("\n%d cuadros | %d niveles superados | %d partidas perdidas | "
           "%d pitidos\n", cuadros, niveles, muertes, s_beeps);
    /* with one decimal: truncating to an integer, 2.9% and 3.0% show as "2"
       and "3" and look like a regression where there is none */
    long prom10 = area_total * 1000 / cuadros / (AK_W * AK_H);
    printf("area empujada: %ld.%ld%% en promedio, %ld%% el peor cuadro "
           "(la pantalla entera son %d pixeles)\n",
           prom10 / 10, prom10 % 10,
           area_max * 100 / (AK_W * AK_H), AK_W * AK_H);
    printf("puntaje %lu | record %lu | nivel %d | vidas %d\n",
           (unsigned long)g.score, (unsigned long)g.hiscore, g.level + 1, g.lives);

    if (fallos) {
        printf("\nFALLARON %d cuadros: hay algo que se mueve sin anotar su "
               "rectangulo\n", fallos);
        return 1;
    }
    printf("\nla lista de sucios coincide con el redibujado completo en los "
           "%d cuadros\n", cuadros);
    return 0;
}
