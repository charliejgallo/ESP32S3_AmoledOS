/*
 * CHATARRA - the app
 *
 * The only thing that sees LVGL, the HAL and the file system. The whole game
 * -world, combat, workshop- does not know any of the three exist.
 *
 * Living here:
 *   - the flush to the screen by dirty rectangles (present())
 *   - the touch, the back gesture and the physical button
 *   - the save file
 *
 * ---------------------------------------------------------------------------
 * THE FLUSH
 * ---------------------------------------------------------------------------
 *
 * Three buffers, all through malloc() and not lv_malloc(): with
 * CONFIG_SPIRAM_USE_MALLOC they land in PSRAM, which is where they belong.
 *
 *     bg    184x224   82 KB   the room, the panels, the menus: what stands still
 *     fb    184x224   82 KB   the frame on screen
 *     big   368x448  330 KB   the same frame upscaled x2, which is the canvas
 *
 * The upscaling is done by the app and NEVER by LVGL: leaving it to
 * LV_IMAGE_ALIGN_STRETCH costs 129 ms per frame on the board, measured,
 * because lv_draw_sw_transform invents an alpha plane for an RGB565 source and
 * composites with blending instead of copying.
 *
 * ---------------------------------------------------------------------------
 * THE MENU HAS NO ON-SCREEN BUTTON, AND THAT IS DELIBERATE
 * ---------------------------------------------------------------------------
 *
 * This board's touch panel reports nothing below a real y=354, so the bottom
 * strip -where a menu bar would naturally go- is dead. The two ways out the
 * game uses are the PHYSICAL BUTTON and touching the robot itself, and both
 * are written on the title screen because neither is guessable.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "chatarra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS    33          /* 30 frames per second                      */

#define SAVE_MAGIC  0x43485431u /* "CHT1"                                    */
#define SAVE_VER    3           /* v3: fixed-size arrays (see below)         */

/* --------------------------------------------------------------------------
 * The v2 format, exactly as it was, so it can be converted
 *
 * It exists because of a bug that reached the board: up to v2 the save file's
 * arrays were sized from the game's enums (`obj[ITEMS]`), so adding an errand
 * item grew an array IN THE MIDDLE of the structure and shifted everything
 * behind it. The loader copied the old save verbatim and read `piezas[]`
 * -full of 0xFF- as if it were quantities: six errands with 255 units.
 *
 * It was fixed by using fixed caps (CH_MAX_OBJ and company). This structure is
 * the EXACT mould of what was written to the cards, and it is converted field
 * by field: nobody loses their game over a bug of mine.
 * -------------------------------------------------------------------------- */
#define V2_ITEMS    17
#define V2_MOCHILA  12
#define V2_PIEZAS   64

typedef struct {
    ch_robot_t yo;
    uint8_t    sala;
    uint8_t    x, y;
    uint8_t    dir;
    uint16_t   creditos;
    uint8_t    obj[V2_ITEMS];
    uint8_t    piezas[V2_MOCHILA];
    uint8_t    bandera[BANDERAS / 8];
    uint16_t   victorias;
    uint32_t   pasos;
    uint8_t    visto[V2_PIEZAS / 8];
} ch_save_v2_t;

typedef struct {
    uint32_t  magic;
    uint16_t  ver;
    uint16_t  largo;
    ch_save_t s;
} sav_t;

typedef struct {
    ch_t        g;

    lv_obj_t   *root;
    lv_obj_t   *canvas;
    lv_obj_t   *touch;
    lv_timer_t *timer;

    uint16_t   *fbmem, *bgmem, *big;

    bool        closing;
    bool        saliendo;           /* see chatarra_back()                   */
    uint32_t    ultimo_gesto;
    uint8_t     boton_menu;         /* the button asked to open/close the menu */
    uint16_t    frames;
    uint8_t     mostrar_fps;
    uint64_t    prev_ms;
    int16_t     fps;
} app_t;

/* 0 mute, 1 effects only, 2 effects and music. It lives in a PREFERENCE and
 * not in the save file: adding a field to ch_save_t changes its size, and the
 * loader rejects saves whose size does not match. A new option cannot cost
 * anybody their game. */
#define KEY_SND     "ch_snd"
static int s_sonido = 2;

/* --------------------------------------------------------------------------
 * Sound
 *
 * The game calls this without knowing there is a HAL on the other side.
 * aos_hal_beep() enqueues and plays from its own task, so it does not block
 * the drawing.
 * -------------------------------------------------------------------------- */

void ch_sfx(int freq_hz, int ms)
{
    if (s_sonido >= 1) {
        aos_hal_beep(freq_hz, ms);
    }
}

void ch_tono(int freq_hz, int ms)
{
    if (s_sonido >= 2) {
        aos_hal_beep(freq_hz, ms);
    }
}

int ch_sonido_get(void) { return s_sonido; }

void ch_sonido_set(int v)
{
    s_sonido = v < 0 ? 0 : (v > 2 ? 2 : v);
    aos_hal_pref_set_i32(KEY_SND, s_sonido);
}

/* --------------------------------------------------------------------------
 * The game
 * -------------------------------------------------------------------------- */

static void ruta_save(char *dst, size_t n)
{
    snprintf(dst, n, "%s/chatarra.sav", aos_hal_path_data());
}

static void nueva_partida(ch_t *g)
{
    memset(&g->s, 0, sizeof(g->s));

    /* The robot you start with: the four most basic parts. It is ugly on
     * purpose, so the first part you tear off somebody shows. */
    for (int c = 0; c < P_CATS; c++) g->s.yo.pieza[c] = 0;
    g->s.yo.skin  = 5;                      /* steel                         */
    g->s.yo.nivel = 5;
    g->s.yo.exp   = ch_exp_nivel(5);
    ch_robot_curar(&g->s.yo);

    for (int i = 0; i < CH_MAX_MOCHILA; i++) g->s.piezas[i] = 0xFF;
    ch_robot_visto(&g->s, &g->s.yo);
    g->s.obj[IT_ACEITE] = 3;
    g->s.creditos = 250;
    g->s.sala = 0;
    g->s.x = 11;
    g->s.y = 14;
    g->s.dir = 1;
}

static bool cargar(ch_t *g)
{
    char ruta[128];
    sav_t sv;
    FILE *f;
    size_t n;

    ruta_save(ruta, sizeof(ruta));
    f = fopen(ruta, "rb");
    if (!f) return false;
    n = fread(&sv, 1, sizeof(sv), f);
    fclose(f);

    if (n < sizeof(sv) - sizeof(ch_save_t) || sv.magic != SAVE_MAGIC) {
        aos_hal_log("chatarra", "guardado ilegible, se descarta");
        return false;
    }

    memset(&g->s, 0, sizeof(g->s));

    if (sv.ver == SAVE_VER && sv.largo == (uint16_t)sizeof(ch_save_t)) {
        g->s = sv.s;
    } else if (sv.ver == 2 && sv.largo == (uint16_t)sizeof(ch_save_v2_t)) {
        /* Field-by-field conversion, which is the only thing that does not
         * take care of itself when a size changes. */
        const ch_save_v2_t *v = (const ch_save_v2_t *)(const void *)&sv.s;
        g->s.yo        = v->yo;
        g->s.sala      = v->sala;
        g->s.x         = v->x;
        g->s.y         = v->y;
        g->s.dir       = v->dir;
        g->s.creditos  = v->creditos;
        g->s.victorias = v->victorias;
        g->s.pasos     = v->pasos;
        memcpy(g->s.obj,     v->obj,     V2_ITEMS);
        memcpy(g->s.piezas,  v->piezas,  V2_MOCHILA);
        memcpy(g->s.bandera, v->bandera, sizeof(v->bandera));
        memcpy(g->s.visto,   v->visto,   sizeof(v->visto));
        aos_hal_log("chatarra", "guardado v2 convertido a v3");
    } else {
        aos_hal_log("chatarra", "guardado v%u de %u B: no se sabe convertir",
                    (unsigned)sv.ver, (unsigned)sv.largo);
        return false;
    }

    /* CLEANING UP THE BUG.
     *
     * The games written while the items array was growing in the middle were
     * left with rubbish in the quantities: six errands with 255. An errand's
     * quantity decides nothing -what checks whether you have it is the chest's
     * FLAG, not the counter- so clamping it is safe and leaves the game
     * playable instead of throwing it away. */
    for (int i = 1; i < ITEMS; i++) {
        int tope = (ch_items[i].precio || ch_items[i].combate) ? 99 : 1;
        if (g->s.obj[i] > tope) {
            g->s.obj[i] = (uint8_t)tope;
        }
    }
    for (int i = ITEMS; i < CH_MAX_OBJ; i++) g->s.obj[i] = 0;
    for (int i = MOCHILA; i < CH_MAX_MOCHILA; i++) g->s.piezas[i] = 0xFF;
    if (g->s.sala >= ch_nsalas) g->s.sala = 0;
    if (g->s.sala >= ch_nsalas) g->s.sala = 0;
    ch_robot_stats(&g->s.yo);
    return true;
}

static void guardar(ch_t *g)
{
    char ruta[128], tmp[136];
    sav_t sv;
    FILE *f;

    ruta_save(ruta, sizeof(ruta));
    snprintf(tmp, sizeof(tmp), "%s.tmp", ruta);

    sv.magic = SAVE_MAGIC;
    sv.ver   = SAVE_VER;
    sv.largo = (uint16_t)sizeof(ch_save_t);
    sv.s     = g->s;

    /* It is written to a temporary file and renamed: if the power goes
     * halfway, the good save stays where it was. It is the same recipe the
     * portal uses for 'remoto's profile. */
    f = fopen(tmp, "wb");
    if (!f) {
        aos_hal_log("chatarra", "no se pudo escribir %s", tmp);
        return;
    }
    fwrite(&sv, 1, sizeof(sv), f);
    fclose(f);
    remove(ruta);
    rename(tmp, ruta);
}

#ifdef AOS_SIM_BUILTIN
/* --------------------------------------------------------------------------
 * Screenshots without a screen
 *
 * On this Mac screencapture -R returns black (the screen recording permission
 * is missing), so the app dumps its own upscaled buffer to PPM and
 * tools/ppm2png.py turns it into PNG. It is the same thing arkanos's and
 * cjump's test benches do, and it also comes out more faithful: it is exactly
 * what is handed to the panel, without the desktop's compositor in between.
 * -------------------------------------------------------------------------- */
static const char *s_shot;
static int         s_shot_n;
static bool        s_shot_anim;

static void volcar(const uint16_t *big)
{
    char ruta[160];
    FILE *f;

    if (!s_shot || s_shot_n >= 90) return;
    snprintf(ruta, sizeof(ruta), "%s%02d.ppm", s_shot, s_shot_n++);
    f = fopen(ruta, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", CH_W * CH_SCALE, CH_H * CH_SCALE);
    for (int i = 0; i < CH_W * CH_SCALE * CH_H * CH_SCALE; i++) {
        uint16_t c = big[i];
        uint8_t px[3] = {
            (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63),
            (uint8_t)(( c        & 0x1F) * 255 / 31),
        };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
#endif

/* --------------------------------------------------------------------------
 * Upscaling and flushing
 * -------------------------------------------------------------------------- */

static void push_rect(app_t *a, const ch_rect_t *r)
{
    lv_area_t area;
    lv_area_t coords;

    ch_expand(a->fbmem, a->big, r);

    lv_obj_get_coords(a->canvas, &coords);
    area.x1 = coords.x1 + r->x0 * CH_SCALE;
    area.y1 = coords.y1 + r->y0 * CH_SCALE;
    area.x2 = coords.x1 + r->x1 * CH_SCALE - 1;
    area.y2 = coords.y1 + r->y1 * CH_SCALE - 1;
    lv_obj_invalidate_area(a->canvas, &area);
}

static void push_all(app_t *a)
{
    push_rect(a, &ch_entera);
}

/* Rebuilds the whole background for the current mode. It is the only expensive
 * frame and it happens on changing room, menu or combat phase: never
 * continuously. */
static void rehacer(app_t *a)
{
    ch_t *g = &a->g;

    if (g->modo == MODO_COMBATE) {
        ch_bt_fondo(g);
    } else {
        ch_ui_fondo(g);
        if (g->modo != MODO_TITULO) {
            ch_ui_hud(g);
        }
    }

    memcpy(a->fbmem, a->bgmem, (size_t)CH_W * CH_H * sizeof(uint16_t));

    ch_dirty_reset(&g->d_prev);
    ch_dirty_reset(&g->d_cur);

    if (g->modo == MODO_COMBATE) {
        ch_bt_dibujar(g);
    } else {
        ch_ui_dibujar(g);
    }

    push_all(a);
    g->d_prev = g->d_cur;
    g->rehacer_fondo = 0;
    g->hud_sucio = 0;

#ifdef AOS_SIM_BUILTIN
    /* One capture per SCREEN CHANGE, which is exactly what you want to look
     * at: entering a room, opening a menu, changing combat phase. */
    volcar(a->big);
#endif
}

static void present(app_t *a)
{
    ch_t *g = &a->g;

    if (g->rehacer_fondo) {
        rehacer(a);
        return;
    }

    /* 1. restore from the background whatever we dirtied last frame */
    g->d_push = g->d_prev;
    if (g->d_push.all) {
        ch_restore(a->fbmem, a->bgmem, &ch_entera);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            ch_restore(a->fbmem, a->bgmem, &g->d_push.r[i]);
        }
    }

    /* 2. draw what moves; fills d_cur */
    ch_dirty_reset(&g->d_cur);
    if (g->modo == MODO_COMBATE) {
        ch_bt_dibujar(g);
    } else {
        ch_ui_dibujar(g);
    }

    /* 3. the HUD, only when a number changed. It is refreshed by the clock and
     *    not per frame: it is the rule that already came out of the Game of
     *    Life and the tuner. */
    if (g->hud_sucio && g->modo != MODO_TITULO && g->modo != MODO_COMBATE) {
        ch_ui_hud(g);
    }

    /* 4. push the union of the two sets */
#ifdef AOS_SIM_BUILTIN
    /* With CH_SHOT_ANIM EVERY frame is dumped while the combat animates: it is
     * the only way to look at a 22-frame animation without the board. */
    if (s_shot_anim && ((g->modo == MODO_COMBATE && (g->bt.anim || g->bt.dmg_t))
                        || g->trans
                        || (g->modo == MODO_DIALOGO && g->dlg_chars < 80))) {
        ch_expand(a->fbmem, a->big, &ch_entera);
        volcar(a->big);
    }
#endif

    ch_dirty_join(&g->d_push, &g->d_cur);
    if (g->d_push.all) {
        push_all(a);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            push_rect(a, &g->d_push.r[i]);
        }
    }

    g->d_prev = g->d_cur;
}

/* --------------------------------------------------------------------------
 * The game's clock
 * -------------------------------------------------------------------------- */

static void tick(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    ch_t  *g = &a->g;

    if (a->closing) return;

    /* Leaving has to be deferred: aos_ui_back() destroys the app, so calling
     * it from an event callback would be destroying it while it runs. */
    if (g->quiere_salir) {
        g->quiere_salir = 0;
        a->saliendo = true;         /* so back() lets the call through       */
        guardar(g);
        aos_ui_back();
        return;
    }
    if (g->quiere_guardar) {
        g->quiere_guardar = 0;
        guardar(g);
    }
    if (a->boton_menu) {
        a->boton_menu = 0;
        if (g->modo == MODO_MAPA)      ch_ui_menu(g);
        else if (g->modo != MODO_TITULO && !ch_ui_atras(g)) { /* nothing */ }
    }

    /* On the v2 the touch chip eats the slow gestures, so it has to be asked
     * as well as listening to LVGL. And since both paths arrive, the second is
     * discarded if it comes too close on the heels of the first. */
    int gesto = aos_ui_take_gesture();
    if (gesto) {
        uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
        if (ahora - a->ultimo_gesto > 400) {
            a->ultimo_gesto = ahora;
            if (!ch_ui_atras(g) && g->modo == MODO_MAPA) g->quiere_salir = 1;
        }
    }

    if (g->aviso_t && --g->aviso_t == 0) g->hud_sucio = 1;
    ch_snd_tick(g);

    switch (g->modo) {
    case MODO_MAPA:
    case MODO_DIALOGO:
        ch_map_tick(g);
        break;
    case MODO_COMBATE:
        ch_bt_tick(g);
        break;
    default:
        g->cuadro++;
        break;
    }

    present(a);

    if (a->mostrar_fps && ++a->frames >= 30) {
        uint64_t ahora = aos_hal_uptime_ms();
        uint32_t dt = (uint32_t)(ahora - a->prev_ms);
        a->prev_ms = ahora;
        a->fps = (int16_t)(dt ? (int)(a->frames * 1000u / dt) : 0);
        aos_hal_log("chatarra", "%d fps, %d %% de pantalla",
                    a->fps, ch_dirty_area(&a->g.d_push) * 100 / (CH_W * CH_H));
        a->frames = 0;
    }
}

/* --------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------- */

static void touch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_point_t p;
    lv_area_t  coords;

    if (a->closing) return;

    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(a->canvas, &coords);

    int bx = (p.x - coords.x1) / CH_SCALE;
    int by = (p.y - coords.y1) / CH_SCALE;
    if (bx < 0 || bx >= CH_W || by < 0 || by >= CH_H) return;

    ch_ui_toque(&a->g, bx, by);
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());

    if (a->closing) return;
    if (d != LV_DIR_RIGHT) return;

    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (ahora - a->ultimo_gesto <= 400) return;
    a->ultimo_gesto = ahora;

    if (!ch_ui_atras(&a->g) && a->g.modo == MODO_MAPA) {
        a->g.quiere_salir = 1;
    }
}

static bool chatarra_button(aos_app_t *self, void *inst, int action)
{
    app_t *a = (app_t *)inst;

    (void)self;
    /* Only the click: the long press is left to the system, which is the
     * guaranteed way out to the clock. Keeping both would be locking the user
     * in. */
    if (action == AOS_BUTTON_CLICK) {
        a->boton_menu = 1;      /* dealt with on the next tick               */
        return true;
    }
    return false;
}

static bool chatarra_back(aos_app_t *self, void *inst)
{
    app_t *a = (app_t *)inst;

    (void)self;

    /* THE TRAP: aos_ui_back() asks THIS callback first. Since leaving has to
     * be deferred -it destroys the app, and calling it from an event is
     * destroying it while it runs-, the tick ends up calling aos_ui_back()...
     * which re-enters here, asks to leave again and NEVER closes. From outside
     * it looks like an app you cannot leave, and the loop gives no error at
     * all. The flag is what breaks the cycle. */
    if (a->saliendo) return false;

    if (ch_ui_atras(&a->g)) return true;
    a->g.quiere_salir = 1;
    return true;
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void *chatarra_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    size_t chico = (size_t)CH_W * CH_H * sizeof(uint16_t);

    if (!a) return NULL;

    uint32_t hint = 0, hpsram = 0;
    aos_hal_heap_info(&hint, &hpsram);
    aos_hal_log("chatarra", "abriendo | interna %u B, psram %u B",
                (unsigned)hint, (unsigned)hpsram);

    a->fbmem = (uint16_t *)malloc(chico);
    a->bgmem = (uint16_t *)malloc(chico);
    a->big   = (uint16_t *)malloc(chico * CH_SCALE * CH_SCALE);
    if (!a->fbmem || !a->bgmem || !a->big) {
        aos_hal_log("chatarra", "sin memoria para los buffers");
        free(a->fbmem); free(a->bgmem); free(a->big);
        lv_free(a);
        return NULL;
    }
    memset(a->fbmem, 0, chico);
    memset(a->bgmem, 0, chico);
    memset(a->big, 0, chico * CH_SCALE * CH_SCALE);

    ch_pal_init();
    ch_buf_init(&a->g.fb, a->fbmem, CH_W, CH_H);
    ch_buf_init(&a->g.bg, a->bgmem, CH_W, CH_H);
    a->g.rng = (uint32_t)aos_hal_uptime_ms() | 1u;

    if (!cargar(&a->g)) {
        nueva_partida(&a->g);
    }
    {
        int32_t v = 2;
        if (!aos_hal_pref_get_i32(KEY_SND, &v) || v < 0 || v > 2) v = 2;
        s_sonido = (int)v;
    }
    a->g.modo = MODO_TITULO;
    a->g.rehacer_fondo = 1;

    a->root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    /* The canvas receives the ALREADY upscaled buffer and is drawn 1:1: no STRETCH */
    lv_canvas_set_buffer(a->canvas, a->big, CH_W * CH_SCALE, CH_H * CH_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, CH_W * CH_SCALE, CH_H * CH_SCALE);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* Touch layer: in LVGL 9 every object is born clickable, so without this
     * the canvas would eat the finger. */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, CH_W * CH_SCALE, CH_H * CH_SCALE);
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);

    /* The gesture is listened for on the ROOT and with GESTURE_BUBBLE taken
     * off it: LVGL does not send LV_EVENT_GESTURE to the object under the
     * finger, it climbs through the parents as long as it finds the flag and
     * gives it to the first one that does not have it. A transparent layer is
     * never on that path. */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

#ifdef AOS_SIM_BUILTIN
    /* Development switches. They only exist in the simulator: on the board
     * getenv() always returns NULL.
     *
     *   CH_SALA=5     starts straight in that room
     *   CH_NIVEL=20   the robot's level
     *   CH_PIEZAS=1   the bag full, for testing the workshop
     *   CH_COMBATE=1  opens straight into a fight
     *   CH_FPS=1      frames per second and % of screen pushed
     *   CH_MUDO=1     no beeps
     *   CH_SHOT=/tmp/ch   one .ppm capture per screen change
     *   CH_CHECK=1    checks the doors and decorations of EVERY room
     *   CH_SHOT_ANIM=1 dumps one .ppm per frame while the combat animates
     *   CH_FINAL=1    opens the closing screen straight away
     */
    {
        const char *v;
        if ((v = getenv("CH_MUDO")) && v[0]) s_sonido = 0;
        if ((v = getenv("CH_SHOT")) && v[0]) s_shot = v;
        if ((v = getenv("CH_CHECK")) && v[0]) ch_map_check();
        if ((v = getenv("CH_SHOT_ANIM")) && v[0]) s_shot_anim = true;
        if ((v = getenv("CH_FINAL")) && v[0]) { a->g.modo = MODO_FINAL; }
        /* CH_MKV2=1 writes a save in the OLD format, with the bag full of
         * 0xFF, which is exactly what is on the cards. It is for checking the
         * conversion without the board: run once with the variable set and
         * once without. */
        if ((v = getenv("CH_MKV2")) && v[0]) {
            struct { uint32_t magic; uint16_t ver; uint16_t largo;
                     ch_save_v2_t s; } vv;
            char ruta[128];
            FILE *f;
            memset(&vv, 0, sizeof(vv));
            vv.magic = SAVE_MAGIC; vv.ver = 2;
            vv.largo = (uint16_t)sizeof(ch_save_v2_t);
            vv.s.yo = a->g.s.yo;
            vv.s.sala = 4; vv.s.x = 7; vv.s.y = 10; vv.s.creditos = 1234;
            vv.s.victorias = 7; vv.s.pasos = 999;
            vv.s.obj[IT_ACEITE] = 3;
            for (int i = 0; i < V2_MOCHILA; i++) vv.s.piezas[i] = 0xFF;
            vv.s.piezas[0] = 20;
            vv.s.bandera[0] = 0x06;
            ruta_save(ruta, sizeof(ruta));
            f = fopen(ruta, "wb");
            if (f) { fwrite(&vv, 1, sizeof(vv), f); fclose(f); }
            aos_hal_log("chatarra", "guardado v2 de prueba escrito (%u B)",
                        (unsigned)sizeof(vv));
        }
        if ((v = getenv("CH_FPS")) && v[0])  a->mostrar_fps = 1;
        if ((v = getenv("CH_NIVEL")) && v[0]) {
            a->g.s.yo.nivel = (uint8_t)atoi(v);
            a->g.s.yo.exp = ch_exp_nivel(a->g.s.yo.nivel);
            ch_robot_curar(&a->g.s.yo);
        }
        if ((v = getenv("CH_PIEZAS")) && v[0]) {
            uint32_t sem = 7u;
            /* Besides filling the bag, it fits a set of parts with attacks of
             * SEVERAL types: without this the starting robot only hits with
             * impact and there is no way to see the ranged animations. */
            a->g.s.yo.pieza[P_CABEZA]  = 6;      /* Cyclops: fire           */
            a->g.s.yo.pieza[P_TORSO]   = 3;      /* Reactor: plasma         */
            a->g.s.yo.pieza[P_BRAZOS]  = 12;     /* Lances: cryo            */
            a->g.s.yo.pieza[P_PIERNAS] = 10;     /* Unicycle: volt          */
            ch_robot_curar(&a->g.s.yo);
            for (int i = 0; i < MOCHILA; i++) {
                a->g.s.piezas[i] = (uint8_t)ch_rnd(&sem, PIEZAS);
            }
            for (int i = 1; i < ITEMS; i++) a->g.s.obj[i] = 5;
            a->g.s.creditos = 4000;
        }
        if ((v = getenv("CH_SALA")) && v[0]) {
            a->g.modo = MODO_MAPA;
            ch_map_entrar(&a->g, atoi(v), 11, 14);
        }
        if ((v = getenv("CH_COMBATE")) && v[0]) {
            ch_robot_t rival;
            uint32_t sem = 12345u;
            a->g.modo = MODO_MAPA;
            ch_map_entrar(&a->g, 4, 7, 10);
            ch_robot_random(&rival, &sem, a->g.s.yo.nivel + 1, 3);
            ch_bt_empezar(&a->g, &rival, 0, 2);
        }
    }
#endif

    a->prev_ms = aos_hal_uptime_ms();
    a->timer = lv_timer_create(tick, FRAME_MS, a);
    (void)self;
    return a;
}

static void chatarra_destroy(aos_app_t *self, void *inst)
{
    app_t *a = (app_t *)inst;

    (void)self;
    if (!a) return;
    a->closing = true;

    if (a->timer) lv_timer_delete(a->timer);
    guardar(&a->g);

    /* With the context still alive, deleting the objects here makes any event
     * from the deletion -DELETE, PRESS_LOST- harmless. Leaving them to the
     * runtime is a use after free. */
    if (a->root) lv_obj_clean(a->root);

    free(a->fbmem);
    free(a->bgmem);
    free(a->big);
    lv_free(a);
}

static bool chatarra_init(aos_app_t *app)
{
    app->desc.id      = "demo.chatarra";
    app->desc.name    = "Chatarra";
    app->desc.icon    = "CH";
    app->desc.icon_vec = AOS_ICON_GAMEPAD;
    app->desc.color_a = 0x8E4630;
    app->desc.color_b = 0x2B1810;
    app->desc.order   = 265;
    app->desc.flags   = AOS_APP_FLAG_FULLSCREEN | AOS_APP_FLAG_KEEP_AWAKE |
                        AOS_APP_FLAG_NO_SWIPE;

    app->create  = chatarra_create;
    app->destroy = chatarra_destroy;
    app->back    = chatarra_back;
    app->button  = chatarra_button;
    return true;
}

AOS_APP_ENTRY(chatarra_init);
