/*
 * CHATARRA - the interface
 *
 * The HUD, the dialogue panel, the menu, the workshop, the items, the robot's
 * data card, the shop and the title screen. All drawn onto the same canvas
 * with the same primitives as the map and the combat: there is not a single
 * LVGL object in the whole game.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE ARE NO WIDGETS
 * ---------------------------------------------------------------------------
 *
 * The temptation is to build the menus with LVGL labels and buttons, which is
 * shorter to write. Two reasons not to, both measured on this board and
 * written down in docs/HANDOFF-APPS.md:
 *
 *   - every LVGL object is an allocation of INTERNAL RAM, which is the
 *     resource that really runs out. Running short there does not give an
 *     error: it gives garbage on the screen, because the bus's intermediate
 *     DMA buffer stops fitting and that strip's flush is not performed.
 *   - rebuilding a twenty-row list costs 111-124 ms with the LVGL thread
 *     blocked. A menu that opens and closes all the time cannot pay that.
 *
 * Drawing on the canvas, a whole menu is a rectangle and six lines of text,
 * and costs the same as a frame of the map.
 *
 * ---------------------------------------------------------------------------
 * WHERE SOMETHING TOUCHABLE CAN GO
 * ---------------------------------------------------------------------------
 *
 * Nothing touchable may end below y=172 of the buffer, which is a real 344.
 * The board's touch panel reports nothing below ~354 and in the simulator that
 * is NOT visible: the mouse reaches everywhere. The rows of every list in this
 * file are worked out so the last one ends at 172.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Drawing helpers
 * -------------------------------------------------------------------------- */

void ch_panel(ch_buf_t *b, int x, int y, int w, int h, uint16_t borde)
{
    ch_round(b, x, y, w, h, 2, ch_rgb(0x141824));
    ch_rect(b, x + 1, y + 1, w - 2, 1, ch_rgb(0x232B41));
    /* the frame, with the corners bitten off so it does not look like a box */
    ch_hline(b, x + 2, y,         w - 4, borde);
    ch_hline(b, x + 2, y + h - 1, w - 4, borde);
    ch_vline(b, x,         y + 2, h - 4, borde);
    ch_vline(b, x + w - 1, y + 2, h - 4, borde);
}

void ch_barra(ch_buf_t *b, int x, int y, int w, int v, int vmax, uint16_t c)
{
    int lleno;

    if (vmax < 1) vmax = 1;
    if (v < 0) v = 0;
    if (v > vmax) v = vmax;
    lleno = (w - 2) * v / vmax;

    ch_rect(b, x, y, w, 6, ch_rgb(0x05060C));
    ch_rect(b, x + 1, y + 1, w - 2, 4, ch_rgb(0x2B3145));
    if (lleno > 0) {
        /* Red when little is left: it is the only information you have to be
         * able to read out of the corner of your eye while playing. */
        uint16_t col = c;
        if (v * 5 <= vmax)      col = ch_rgb(0xFF4A3D);
        else if (v * 5 <= vmax * 2) col = ch_rgb(0xFF9F0A);
        ch_rect(b, x + 1, y + 1, lleno, 4, col);
        ch_rect(b, x + 1, y + 1, lleno, 1, ch_tone(col, 4));
    }
}

/* Breaks the text into lines of at most 'ancho' characters, honouring any
 * breaks it already carries. Returns how many lines came out. */
int ch_wrap(const char *s, int ancho, char dst[][30], int max)
{
    int n = 0, k = 0;

    if (ancho > 29) ancho = 29;
    dst[0][0] = 0;

    while (*s && n < max) {
        if (*s == '\n') {
            dst[n][k] = 0;
            n++; k = 0;
            if (n < max) dst[n][0] = 0;
            s++;
            continue;
        }
        if (k >= ancho) {
            /* search backwards for the last space so words are not split */
            int corte = k;
            while (corte > 0 && dst[n][corte - 1] != ' ') corte--;
            if (corte == 0) corte = k;      /* one very long word             */
            else            s -= (k - corte);
            dst[n][corte] = 0;
            n++; k = 0;
            if (n < max) dst[n][0] = 0;
            while (*s == ' ') s++;
            continue;
        }
        dst[n][k++] = *s++;
    }
    if (n < max) { dst[n][k] = 0; if (k) n++; }
    return n;
}

/* --------------------------------------------------------------------------
 * The HUD
 *
 * It lives from y=176 down, that is, in the strip that CANNOT be touched. That
 * is not waste: it is 96 real pixels that would otherwise be empty, and here
 * they carry the one thing you have to be able to look at without stopping
 * playing.
 * -------------------------------------------------------------------------- */

/* THE BAR, WITH A BODY.
 *
 * The flat one was four pixels of solid colour in a groove. It reads, but it
 * reads like a progress bar in a dialogue box: at a glance you cannot tell
 * half from a third, and half from a third is the whole decision of whether
 * to drink the oil now or after the next hit. This one is ten tall, framed,
 * lit along the top and shaded along the bottom so it has a shape, and
 * notched at every quarter so the eye measures instead of guessing. */
static void barra_hud(ch_buf_t *b, int x, int y, int w, int h, int v, int vmax,
                      uint32_t base)
{
    int lleno;

    if (vmax < 1) vmax = 1;
    if (v < 0) v = 0;
    if (v > vmax) v = vmax;
    lleno = (w - 2) * v / vmax;

    ch_rect(b, x, y, w, h, ch_rgb(0x05060C));
    ch_rect(b, x + 1, y + 1, w - 2, h - 2, ch_rgb(0x1A2133));

    if (lleno > 0) {
        /* Red when little is left: it is the only information you have to be
         * able to read out of the corner of your eye while playing. */
        uint16_t col = ch_rgb(base);
        if (v * 5 <= vmax)          col = ch_rgb(0xFF4A3D);
        else if (v * 5 <= vmax * 2) col = ch_rgb(0xFF9F0A);
        ch_rect(b, x + 1, y + 1,     lleno, h - 2, col);
        ch_rect(b, x + 1, y + 1,     lleno, 1, ch_tone(col,  5));
        ch_rect(b, x + 1, y + h - 2, lleno, 1, ch_tone(col, -4));
    }
    for (int k = 1; k < 4; k++) {                   /* las marcas del cuarto */
        ch_rect(b, x + 1 + (w - 2) * k / 4, y + 1, 1, h - 2, ch_rgb(0x05060C));
    }
    ch_frame(b, x, y, w, h, ch_rgb(0x3D465F));
}

void ch_ui_hud(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    const ch_robot_t *yo = &g->s.yo;
    char t[32];

    ch_rect(b, 0, HUD_Y, CH_W, HUD_H, ch_rgb(0x0B0D14));
    ch_vgrad(b, 0, HUD_Y + 2, CH_W, HUD_Y + 14,
             ch_rgb(0x161C2E), ch_rgb(0x0B0D14));
    ch_hline(b, 0, HUD_Y,     CH_W, ch_rgb(0x3D465F));
    ch_hline(b, 0, HUD_Y + 1, CH_W, ch_rgb(0x1A2133));

    /* first line: where you are and how much you have */
    ch_text(b, 5, HUD_Y + 4, g->aviso_t ? g->aviso : _(r->nombre),
            g->aviso_t ? ch_rgb(0xFFE45E) : ch_rgb(0xD5DCEB));
    snprintf(t, sizeof(t), _("%dC"), g->s.creditos);
    ch_text(b, CH_W - 5 - ch_text_w(t), HUD_Y + 4, t, ch_rgb(0xFFE45E));

    ch_text(b, 5, HUD_Y + 18, _("VID"), ch_rgb(0x8A93AB));
    barra_hud(b, 26, HUD_Y + 16, 106, 11, yo->vida, yo->vida_max, 0x4ADE80);
    snprintf(t, sizeof(t), "%d/%d", yo->vida, yo->vida_max);
    ch_text(b, CH_W - 5 - ch_text_w(t), HUD_Y + 18, t, ch_rgb(0xD5DCEB));

    ch_text(b, 5, HUD_Y + 32, _("ENE"), ch_rgb(0x8A93AB));
    barra_hud(b, 26, HUD_Y + 30, 62, 11, yo->ene, yo->ene_max, 0x4A9DF5);
    {
        uint32_t base = ch_exp_nivel(yo->nivel);
        uint32_t sig  = ch_exp_nivel(yo->nivel + 1);
        uint32_t hoy  = yo->exp > base ? yo->exp - base : 0;
        uint32_t tot  = sig > base ? sig - base : 1;
        ch_text(b, 94, HUD_Y + 32, _("EXP"), ch_rgb(0x8A93AB));
        barra_hud(b, 116, HUD_Y + 30, 63, 11, (int)hoy, (int)tot, 0xBF5AF2);
    }

    /* And who you are, centred, on its own line. It used to share the top row
     * with the room's name and the two of them fought for the width in every
     * language but Spanish. */
    snprintf(t, sizeof(t), _("%s  N%d"), ch_robot_nombre(yo), yo->nivel);
    ch_hline(b, 8, HUD_Y + 44, CH_W - 16, ch_rgb(0x232B41));
    ch_text(b, (CH_W - ch_text_w(t)) / 2, HUD_Y + 47, t, ch_rgb(0xFFFFFF));

    /* and the copy to what is on screen: nothing covers the HUD, so it does
     * not need to go through the dirty rectangle list */
    memcpy(g->fb.px + (size_t)HUD_Y * CH_W, g->bg.px + (size_t)HUD_Y * CH_W,
           (size_t)HUD_H * CH_W * sizeof(uint16_t));
    ch_dirty_add(&g->d_cur, 0, HUD_Y, CH_W, HUD_H);
    g->hud_sucio = 0;
}

void ch_ui_aviso(ch_t *g, const char *texto)
{
    snprintf(g->aviso, sizeof(g->aviso), "%s", texto);
    g->aviso_t = 45;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * The dialogue panel
 * -------------------------------------------------------------------------- */

#define DLG_X     4
#define DLG_W   176
#define DLG_H    58   /* DLG_Y is in chatarra.h: the map looks at it */

static char s_lineas[24][30];
static int  s_nlineas;

void ch_ui_dialogo(ch_t *g, const char *texto, int ent, int luego)
{
    g->dlg = texto;
    g->dlg_ent = (uint8_t)ent;
    g->dlg_luego = (uint8_t)luego;
    g->dlg_pag = 0;
    /* 27 and not 28: the box is 176 px, minus 6 of margin each side is 164,
     * and at 6 px per character 27 fit. With 28 the last letter runs off. */
    s_nlineas = ch_wrap(texto, 27, s_lineas, 24);
    g->dlg_pags = (uint8_t)((s_nlineas + 3) / 4);
    if (!g->dlg_pags) g->dlg_pags = 1;
    g->modo = MODO_DIALOGO;
    g->rehacer_fondo = 1;
}

/* The panel's BACKGROUND carries only the box: the text is drawn per frame,
 * because it appears letter by letter. Leaving it in the background would
 * force a rebuild thirty times a second. */
static void dlg_fondo(ch_t *g)
{
    ch_panel(&g->bg, DLG_X, DLG_Y, DLG_W, DLG_H, ch_rgb(0x8A93AB));
}

/* How many letters the whole page has, to know when it has finished. */
static int dlg_largo(ch_t *g)
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        int l = g->dlg_pag * 4 + i;
        if (l >= s_nlineas) break;
        n += (int)strlen(s_lineas[l]);
    }
    return n;
}

/* The text, revealing itself. It is drawn onto what is on screen and records
 * its rectangle.
 *
 * It is worth it for a reason that is not decorative: text that appears all at
 * once is read at a glance and you tap without reading. Appearing letter by
 * letter the eye follows it, and besides, the tap that speeds it up is the
 * natural way of saying "I have read it". */
static void dlg_texto(ch_t *g)
{
    ch_buf_t *b = &g->fb;
    int quedan = g->dlg_chars;

    for (int i = 0; i < 4; i++) {
        int l = g->dlg_pag * 4 + i;
        int n;
        char corte[30];

        if (l >= s_nlineas) break;
        n = (int)strlen(s_lineas[l]);
        if (quedan <= 0) break;
        if (n > quedan) n = quedan;
        memcpy(corte, s_lineas[l], (size_t)n);
        corte[n] = 0;
        quedan -= (int)strlen(s_lineas[l]);
        ch_text(b, DLG_X + 6, DLG_Y + 6 + i * 12, corte, ch_rgb(0xFFFFFF));
    }

    /* The little arrow only once the page has finished writing itself: while
     * it writes, there is nothing to confirm yet. */
    if (g->dlg_chars >= dlg_largo(g) && ((g->cuadro >> 3) & 1)) {
        ch_rect(b, DLG_X + DLG_W - 11, DLG_Y + DLG_H - 9, 5, 2, ch_rgb(0xFFE45E));
        ch_rect(b, DLG_X + DLG_W - 10, DLG_Y + DLG_H - 7, 3, 2, ch_rgb(0xFFE45E));
        ch_rect(b, DLG_X + DLG_W -  9, DLG_Y + DLG_H - 5, 1, 2, ch_rgb(0xFFE45E));
    }
    ch_dirty_add(&g->d_cur, DLG_X + 4, DLG_Y + 4, DLG_W - 8, DLG_H - 6);
}

static void dlg_avanzar(ch_t *g)
{
    /* The first tap finishes writing the page; the second moves on. It is the
     * genre's convention and it stops an impatient tap eating a text that has
     * not appeared yet. */
    if (g->dlg_chars < dlg_largo(g)) {
        g->dlg_chars = 250;
        return;
    }
    g->dlg_pag++;
    g->dlg_chars = 0;
    if (g->dlg_pag < g->dlg_pags) {
        g->rehacer_fondo = 1;
        return;
    }
    g->modo = g->dlg_luego;
    g->rehacer_fondo = 1;
    /* The boss's dialogue ends in a fight: it is fired on CLOSING it and not
     * on opening it, so it is read before the arena appears. */
    ch_map_dialogo_cerrado(g);
}

/* --------------------------------------------------------------------------
 * The menu: two pages of tiles with an icon
 *
 * It used to be nine rows of 16 px stacked in one column. On a 1.8" screen
 * that is a 172x14 strip per entry and the whole list crossed the touch
 * panel's envelope from end to end: the first row and the last one were the
 * two worst places on the glass. Now it is two pages -what you carry, and
 * what the game is- of tiles at least 40 px tall, all of them between y=36
 * and y=168 of the buffer, which is real 72..336: the middle of the panel.
 *
 * The icons are 12x12 written as text. Two layers -body and detail- because
 * one flat colour at this size reads as a blob, and at x2 (or x3 on the root
 * page) a 12x12 grid is exactly the resolution the rest of the game draws at.
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *fila[12];
    uint32_t    cuerpo, detalle;
} icono_t;


/* Which icon each item wears. The two oils share one and so do the two
 * batteries -they ARE the same thing, bigger- and the errands that are one
 * object each share the toolbox. */
static const uint8_t ICONO_ITEM[ITEMS] = {
    [IT_ACEITE] = IC_ACEITE,    [IT_ACEITE2]  = IC_BARRIL,
    [IT_BATERIA] = IC_BATERIA,  [IT_BATERIA2] = IC_BATERIA,
    [IT_SOLDADOR] = IC_SOLDADOR,[IT_CHIP]     = IC_CHIP,
    [IT_IMAN] = IC_IMAN,        [IT_LLAVE]    = IC_LLAVE,
    [IT_PASE] = IC_PASE,        [IT_TORNILLOS]= IC_TORNILLOS,
    [IT_ANCLA] = IC_ANCLA,      [IT_FUSIBLE]  = IC_HERRAMIENTA,
    [IT_MOLDE] = IC_HERRAMIENTA,[IT_TERMO]    = IC_HERRAMIENTA,
    [IT_CLAVE] = IC_LLAVE,      [IT_ENGRANAJE]= IC_HERRAMIENTA,
};

static const icono_t ICONOS[NICONOS] = {
    [IC_TALLER] = { {              /* a nut: the town is called Villa Tuerca */
        "....####....", "..########..", ".##########.", "###......###",
        "##...++...##", "##..++++..##", "##..++++..##", "##...++...##",
        "###......###", ".##########.", "..########..", "....####....",
    }, 0xC8CEDC, 0x5A6076 },
    [IC_OBJETOS] = { {                                          /* a bag    */
        "...##..##...", "...##..##...", "..########..", ".##########.",
        "############", "############", "##..++++..##", "##..++++..##",
        "############", ".##########.", "..########..", "............",
    }, 0xB97A3E, 0xFFE45E },
    [IC_EQUIPO] = { {           /* three rows of a roster: portrait and bar */
        "............", ".###.#######", ".#+#.#######", ".###........",
        "............", ".###.#######", ".#+#.#######", ".###........",
        "............", ".###.#######", ".#+#.#######", ".###........",
    }, 0x8FA6C4, 0x6FE3FF },
    [IC_REGISTRO] = { {                               /* a page with lines  */
        ".##########.", ".#........#.", ".#.++++++.#.", ".#........#.",
        ".#.++++++.#.", ".#........#.", ".#.++++++.#.", ".#........#.",
        ".#.++++...#.", ".#........#.", ".##########.", "............",
    }, 0xE8E2D0, 0x8A93AB },
    [IC_MAPA] = { {                                      /* a sheet + pin   */
        "############", "#..........#", "#...####...#", "#..##++##..#",
        "#..##++##..#", "#...####...#", "#....##....#", "#....##....#",
        "#..........#", "#..........#", "############", "............",
    }, 0x6FBF73, 0xFF5E5E },
    [IC_AYUDA] = { {                                     /* a question mark */
        "............", "...######...", "..##++++##..", "..##....##..",
        "........##..", ".......##...", ".....###....", ".....##.....",
        ".....##.....", "............", ".....##.....", "............",
    }, 0xFFE45E, 0xB99A2E },
    [IC_SONIDO] = { {                                        /* a speaker   */
        "............", "......##....", ".....###..+.", "...#####.+..",
        "..######.+.+", "..######+.+.", "..######.+.+", "...#####.+..",
        ".....###..+.", "......##....", "............", "............",
    }, 0xD5DCEB, 0x6FE3FF },
    [IC_GUARDAR] = { {                                       /* a floppy    */
        "############", "#++++++++++#", "#+##....##+#", "#+##....##+#",
        "#+########+#", "#++++++++++#", "#+########+#", "#+#......#+#",
        "#+#......#+#", "#+########+#", "############", "............",
    }, 0x3D465F, 0xD5DCEB },
    [IC_CERRAR] = { {                              /* the way out of a room */
        ".####.......", ".#..........", ".#..........", ".#....##....",
        ".#...+##....", ".#..++######", ".#...+##....", ".#....##....",
        ".#..........", ".#..........", ".####.......", "............",
    }, 0x8A93AB, 0xFFE45E },
    [IC_MOCHILA] = { {                          /* root: your robot and you */
        ".....##.....", ".....##.....", ".##########.", "##........##",
        "#..######..#", "#..#++++#..#", "#..#++++#..#", "#..######..#",
        "##........##", ".##########.", "..##....##..", "..##....##..",
    }, 0x8FA6C4, 0x6FE3FF },

    [IC_ACEITE] = { {                                 /* aceitera con pico y asa */
        "............", ".......kk...", "......koK...", ".....koK....",
        "..kkkkkkk...", ".kwoooooOk..", "koyoooooOk..", "koyoooooOkk.",
        "koyoooooOk.k", ".kOOOOOOOk.k", "..kkkkkkk.kk", "............",
    }, 0xFF9F0A, 0xC05A00 },
    [IC_BATERIA] = { {                                /* bateria con el rayo en el cuerpo */
        "...kk..kk...", "..kGGkkGGk..", ".kkkkkkkkkk.", ".kvVVVVVVvk.",
        ".kvVVwwVVvk.", ".kvVVwVVVvk.", ".kvVwwwwVvk.", ".kvVVVwVVvk.",
        ".kvVVwwVVvk.", ".kvVVVVVVvk.", ".kkkkkkkkkk.", "............",
    }, 0x4ADE80, 0x1E7A3C },
    [IC_SOLDADOR] = { {                               /* soldador con la punta al rojo */
        "..........kk", ".........krk", "........krrk", ".......kroK.",
        "......kroK..", ".....kroK...", "....kGGk....", "...kdGk.....",
        "..kjJk......", ".kjJk.......", "kjJk........", "kkk.........",
    }, 0x99A3BC, 0xFF9F0A },
    [IC_CHIP] = { {                                   /* chip con el dado adentro */
        "..k.k.k.k...", ".kkkkkkkkk..", "kkCCCCCCCCkk", ".kCcccccccCk",
        ".kCcKKKKKcCk", ".kCcKwwwKcCk", ".kCcKwCwKcCk", ".kCcKKKKKcCk",
        ".kCcccccccCk", "kkCCCCCCCCkk", ".kkkkkkkkk..", "..k.k.k.k...",
    }, 0x7BE9FF, 0x18A6D8 },
    [IC_IMAN] = { {                                   /* iman de herradura con los polos */
        ".kkkk..kkkk.", "kmMMmkkmMMmk", "kmMMmkkmMMmk", "kmMMmkkmMMmk",
        "kmMMmkkmMMmk", "kmMMmkkmMMmk", "kmMMmkkmMMmk", "kmMMMMMMMMmk",
        "kwwwwkkGGGGk", "kwwwwkkGGGGk", "kkkkk..kkkkk", "............",
    }, 0xFF6FAE, 0xD5DCEB },
    [IC_LLAVE] = { {                                  /* llave con paleton */
        "...kkkk.....", "..kyYYyk....", ".kyYkkYyk...", ".kyk..kyk...",
        ".kyYkkYyk...", "..kyYYyk....", "...kyYk.....", "...kyYk.....",
        "...kyYkk....", "...kyYk.....", "...kyYkkk...", "...kkkk.....",
    }, 0xFFE45E, 0xE0A800 },
    [IC_PASE] = { {                                   /* pase con foto y banda */
        "kkkkkkkkkkkk", "kCCCCCCCCCCk", "kCkwwkCCCCCk", "kCkwwkCGGGCk",
        "kCkwwkCGGGCk", "kCkkkkCCCCCk", "kCGGGGGGGGCk", "kCCCCCCCCCCk",
        "kCyyyyyyyyCk", "kCyKKKKKKyCk", "kCCCCCCCCCCk", "kkkkkkkkkkkk",
    }, 0x18A6D8, 0xD5DCEB },
    [IC_TORNILLOS] = { {                              /* caja con los tornillos a la vista */
        ".kkkkkkkkkk.", ".kJJJJJJJJk.", ".kjjjjjjjjk.", ".kkkkkkkkkk.",
        "kGdGkGdGkGdk", "kdddkdddkddd", "kGdGkGdGkGdk", "kdddkdddkddd",
        "kGdGkGdGkGdk", ".kjjjjjjjjk.", ".kJJJJJJJJk.", ".kkkkkkkkkk.",
    }, 0x99A3BC, 0x3D465F },
    [IC_ANCLA] = { {                                  /* ancla con cepo y brazos */
        ".....kk.....", "....kuuk....", "....kkkk....", "....kuuk....",
        ".kkkkuukkkk.", ".kUUkuukUUk.", "....kuuk....", "k...kuuk...k",
        "ku..kuuk..uk", "kuukkuukkuuk", ".kuUUUUUUuk.", "..kkkkkkkk..",
    }, 0xC06B2E, 0x6E3A16 },
    [IC_HERRAMIENTA] = { {                            /* caja de herramientas con asa y traba */
        ".....kk.....", "....kGGk....", "..kkkkkkkk..", ".kGGGGGGGGk.",
        "kGdddddddddk", "kGdyyyyyyydk", "kGdyKKKKKydk", "kGdyyyyyyydk",
        "kGdddddddddk", ".kGGGGGGGGk.", "..kkkkkkkk..", "............",
    }, 0xD5DCEB, 0xFFE45E },
    [IC_BARRIL] = { {                                 /* barril de aceite con zunchos */
        "..kkkkkkkk..", ".kOOOOOOOOk.", "kOoooooooOk.", "kOoOOOOOoOk.",
        "kkkkkkkkkkkk", "kOoyyyyyoOk.", "kOoyOOOyoOk.", "kOoyyyyyoOk.",
        "kkkkkkkkkkkk", "kOoooooooOk.", ".kOOOOOOOOk.", "..kkkkkkkk..",
    }, 0xFF9F0A, 0xC05A00 },
    [IC_COMBATE] = { {                              /* dos cunas que chocan */
        "kk........kk", "kGk......kGk", "kGGk....kGGk", "kGGGk..kGGGk",
        "kGGGGkkGGGGk", "kGGGkrrkGGGk", "kGGGkrrkGGGk", "kGGGGkkGGGGk",
        "kGGGk..kGGGk", "kGGk....kGGk", "kGk......kGk", "kk........kk",
    }, 0xD5DCEB, 0xFF4A3D },
    [IC_TRUEQUE] = { {                              /* una flecha para cada lado */
        "....v.......", "...vvv......", "..vvvvv.....", "....v.......",
        "....v.......", "....v..n....", "....v..n....", "....v..n....",
        "....v..n....", ".......n....", "....nnnnn...", ".....nnn....",
    }, 0x4ADE80, 0x2AF0C8 },
    [IC_PIEZA] = { {                                /* una cabeza suelta */
        "............", "..kkkkkkkk..", ".kGGGGGGGGk.", "kGGkGGGGkGGk",
        "kGGkGGGGkGGk", "kGGGGGGGGGGk", "kGGccGGccGGk", "kGGccGGccGGk",
        "kGGGGGGGGGGk", ".kGGGGGGGGk.", "..kkkkkkkk..", "............",
    }, 0x8FA6C4, 0x7BE9FF },
    [IC_COLGAR] = { {                               /* el tubo y la flecha abajo */
        "kkk......kkk", "kBBk....kBBk", "kBBk....kBBk", "kBBkkkkkkBBk",
        "kBBBBBBBBBBk", "kkkkkkkkkkkk", "............", "....rrrr....",
        "....rrrr....", "..rrrrrrrr..", "...rrrrrr...", "....rrrr....",
    }, 0x4A9DF5, 0xFF4A3D },
    [IC_DIARIO] = { {                           /* una libreta con un lazo */
        ".kkkkkkkkkk.", ".kJjjjjjjjJk", "kykjjjjjjjJk", "kykjjyyyyjJk",
        "kykjjjjjjjJk", "kykjjyyyyjJk", "kykjjjjjjjJk", "kykjjyyjjjJk",
        "kykjjjjjjjJk", ".kJjjjjjjjJk", ".kkkkkkkkkk.", "............",
    }, 0x8A5A32, 0xFFE45E },
    [IC_AJUSTES] = { {                                /* root: the settings */
        "............", ".##########.", ".....##.....", ".....##.....",
        ".##########.", "...##.......", "...##.......", ".##########.",
        "........##..", "........##..", ".##########.", "............",
    }, 0xD5DCEB, 0xFFE45E },
};

/* TWO COLOURS WAS THE CEILING, and it showed: an item icon came out as a
 * coloured blob with a hole in it. '#' and '+' still mean body and detail -the
 * menu's eleven icons are drawn that way and read fine at three times size-
 * but any other letter is now looked up in the SPRITES' palette, so an icon
 * can have as much detail as the rest of the game's art. Same table, same
 * twelve by twelve, no new format to learn. */
void ch_ui_icono(ch_buf_t *b, int x, int y, int ic, int esc)
{
    const icono_t *o = &ICONOS[ic % NICONOS];
    uint16_t c = ch_rgb(o->cuerpo), d = ch_rgb(o->detalle), p;

    for (int fy = 0; fy < 12; fy++) {
        const char *f = o->fila[fy];
        if (!f) continue;
        for (int fx = 0; f[fx]; fx++) {
            uint16_t col;
            if (f[fx] == '.') continue;
            if (f[fx] == '#')      col = c;
            else if (f[fx] == '+') col = d;
            else if (ch_pal(f[fx], &p)) col = p;
            else                   col = c;
            ch_rect(b, x + fx * esc, y + fy * esc, esc, esc, col);
        }
    }
}


/* --------------------------------------------------------------------------
 * Lists
 *
 * Six rows of 18 px from y=64: the last one ends at 172, which is the limit of
 * what the board's touch panel reaches.
 * -------------------------------------------------------------------------- */

#define LX      6
#define LW     172

/* The list's geometry is set by each screen before drawing it, because each
 * one has a different number of rows. What does NOT change is the bottom
 * limit: the last row cannot go past y=172, which is a real 344, because the
 * board's touch panel does not reach any lower.
 *
 * The heights came from testing with a finger, not from the screen: 18 px of
 * buffer is 36 real and they were tight. Now a list row is 21 (42 real) and a
 * menu one 16 (32), which is what fits while honouring the ceiling. All the
 * sums below end at 168 or 172. */
static int s_ly0 = 64, s_lfh = 18, s_lfilas = 6;

static void lista_geom(int y0, int fh, int filas)
{
    s_ly0 = y0; s_lfh = fh; s_lfilas = filas;
}

static int fila_en(int bx, int by)
{
    if (bx < LX || bx >= LX + LW) return -1;
    if (by < s_ly0 || by >= s_ly0 + s_lfilas * s_lfh) return -1;
    return (by - s_ly0) / s_lfh;
}

/* THE TALL ROW, the one an item lives in.
 *
 * The flat row is 21 px: a name, a number at the right, and nothing else. On
 * a 1.8" screen that is a line of text you have to lean in to read, and in a
 * shop it is worse -you buy by the name because the description is somewhere
 * else. This one is 42: the icon, the name, and the description wrapped
 * underneath, which is the thing you actually needed in order to choose.
 * Three of them fill the screen and that is on purpose: three items you can
 * read beat six you cannot. */
static void fila_item(ch_t *g, int i, int item, const char *der,
                      uint16_t cder, bool activa)
{
    ch_buf_t *b = &g->bg;
    int y = s_ly0 + i * s_lfh;
    int h = s_lfh - 4;
    const ch_item_t *it = &ch_items[item % ITEMS];
    char lin[2][30];
    int n;

    ch_rect(b, LX, y, LW, h, activa ? ch_rgb(0x1A2133) : ch_rgb(0x141720));
    ch_frame(b, LX, y, LW, h, ch_rgb(0x3D465F));
    ch_rect(b, LX + 1, y + 1, LW - 2, 1, ch_rgb(0x2C3550));

    ch_ui_icono(b, LX + 6, y + h / 2 - 12, ICONO_ITEM[item % ITEMS], 2);

    ch_text(b, LX + 36, y + 6, _(it->nombre),
            activa ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85));
    n = ch_wrap(_(it->desc), 21, lin, 2);
    for (int k = 0; k < n && k < 2; k++) {
        ch_text(b, LX + 36, y + 18 + k * 10, lin[k],
                activa ? ch_rgb(0x8A93AB) : ch_rgb(0x4A5268));
    }
    if (der && *der) {
        ch_text(b, LX + LW - 6 - ch_text_w(der), y + 6, der,
                activa ? cder : ch_rgb(0x606B85));
    }
}

static void fila(ch_t *g, int i, const char *izq, const char *der, bool activa)
{
    ch_buf_t *b = &g->bg;
    int y = s_ly0 + i * s_lfh;

    ch_rect(b, LX, y, LW, s_lfh - 2, activa ? ch_rgb(0x232B41) : ch_rgb(0x141720));
    ch_frame(b, LX, y, LW, s_lfh - 2, ch_rgb(0x3D465F));
    ch_text(b, LX + 5, y + 5, izq,
            activa ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85));
    if (der && *der) {
        ch_text(b, LX + LW - 5 - ch_text_w(der), y + 5, der,
                activa ? ch_rgb(0xFFE45E) : ch_rgb(0x606B85));
    }
}

void ch_ui_titulo(ch_t *g, const char *txt, const char *sub)
{
    ch_buf_t *b = &g->bg;

    ch_rect(b, 0, 0, CH_W, MAP_H, ch_rgb(0x0B0D14));
    ch_vgrad(b, 0, 0, CH_W, 40, ch_rgb(0x1B2340), ch_rgb(0x0B0D14));
    ch_text(b, 8, 8, txt, ch_rgb(0xFFE45E));
    if (sub) ch_text(b, 8, 20, sub, ch_rgb(0x8A93AB));
    ch_hline(b, 6, 31, CH_W - 12, ch_rgb(0x3D465F));
}

/* The list's arrows, top right. Both are touchable. */
#define AR_X    (CH_W - 46)
#define AR_Y      4      /* at the top, in the title strip */
#define AR_W     20
#define AR_H     20

static void flechas(ch_t *g, bool arriba, bool abajo)
{
    ch_buf_t *b = &g->bg;

    ch_rect(b, AR_X, AR_Y, AR_W, AR_H, ch_rgb(0x232B41));
    ch_frame(b, AR_X, AR_Y, AR_W, AR_H, ch_rgb(0x3D465F));
    ch_rect(b, AR_X + 24, AR_Y, AR_W, AR_H, ch_rgb(0x232B41));
    ch_frame(b, AR_X + 24, AR_Y, AR_W, AR_H, ch_rgb(0x3D465F));

    /* The tip is marked by the NARROWEST row. Stacking them the other way
     * round gives two triangles pointing exactly the wrong way, which is what
     * was happening. */
    uint16_t ca = arriba ? ch_rgb(0xFFFFFF) : ch_rgb(0x3D465F);
    uint16_t cb = abajo  ? ch_rgb(0xFFFFFF) : ch_rgb(0x3D465F);
    for (int i = 0; i < 4; i++) {
        ch_rect(b, AR_X + 10 - i, AR_Y + 6 + i,  1 + i * 2, 1, ca);   /* up */
        ch_rect(b, AR_X + 34 - i, AR_Y + 13 - i, 1 + i * 2, 1, cb);   /* down */
    }
}

static int flecha_en(int bx, int by)
{
    if (by < AR_Y || by >= AR_Y + AR_H) return 0;
    if (bx >= AR_X && bx < AR_X + AR_W) return -1;
    if (bx >= AR_X + 24 && bx < AR_X + 24 + AR_W) return 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * Main menu
 * -------------------------------------------------------------------------- */

/* What each tile of each page is. The root page has two, and everything the
 * player asked for by name -workshop, items, team, records, map- is on the
 * first one: those are the five you open while playing, and the other four
 * are the ones you open once. */
typedef struct { uint8_t icono; const char *txt; uint8_t accion; } baldosa_t;

enum { AC_TALLER = 1, AC_OBJETOS, AC_EQUIPO, AC_REGISTRO, AC_MAPA,
       AC_DIARIO, AC_AYUDA, AC_SONIDO, AC_GUARDAR, AC_CERRAR,
       AC_PAG1, AC_PAG2 };

static const baldosa_t PAG_RAIZ[] = {
    { IC_MOCHILA, N_("LO TUYO"),   AC_PAG1 },
    { IC_AJUSTES, N_("EL JUEGO"),  AC_PAG2 },
};
static const baldosa_t PAG_TUYO[] = {
    { IC_TALLER,   N_("TALLER"),   AC_TALLER },
    { IC_OBJETOS,  N_("OBJETOS"),  AC_OBJETOS },
    { IC_EQUIPO,   N_("EQUIPO"),   AC_EQUIPO },
    { IC_REGISTRO, N_("REGISTRO"), AC_REGISTRO },
    { IC_MAPA,     N_("MAPA"),     AC_MAPA },
    { IC_DIARIO,   N_("DIARIO"),   AC_DIARIO },
};
static const baldosa_t PAG_JUEGO[] = {
    { IC_AYUDA,   N_("AYUDA"),    AC_AYUDA },
    { IC_SONIDO,  N_("SONIDO"),   AC_SONIDO },
    { IC_GUARDAR, N_("GUARDAR"),  AC_GUARDAR },
    { IC_CERRAR,  N_("CERRAR"),   AC_CERRAR },
};

static const char *const SONIDOS[3] = { N_("MUDO"), N_("EFECTOS"), N_("TODO") };

/* The grid. Everything comes out of these four numbers so that moving the
 * strip is moving one line and not nine. */
#define BX0     8              /* real x 16..352: the panel's envelope exactly */
#define BY0    36
#define BANCHO (CH_W - BX0 * 2)
#define BALTO  (MAP_H - BY0)

static const baldosa_t *pagina(const ch_t *g, int *n, int *cols, const char **tit)
{
    switch (g->sel2) {
    case 1: *n = (int)(sizeof(PAG_TUYO)  / sizeof(PAG_TUYO[0]));
            *cols = 2; *tit = N_("LO TUYO");  return PAG_TUYO;
    case 2: *n = (int)(sizeof(PAG_JUEGO) / sizeof(PAG_JUEGO[0]));
            *cols = 2; *tit = N_("EL JUEGO"); return PAG_JUEGO;
    default: *n = (int)(sizeof(PAG_RAIZ) / sizeof(PAG_RAIZ[0]));
            *cols = 1; *tit = N_("MENU");     return PAG_RAIZ;
    }
}

static void baldosa_caja(int i, int n, int cols, int *x, int *y, int *w, int *h)
{
    int filas = (n + cols - 1) / cols;
    int gap = 6;

    *w = (BANCHO - gap * (cols - 1)) / cols;
    *h = (BALTO - gap * (filas - 1)) / filas;
    *x = BX0 + (i % cols) * (*w + gap);
    *y = BY0 + (i / cols) * (*h + gap);
}

static int baldosa_en(const ch_t *g, int bx, int by)
{
    int n, cols;
    const char *tit;

    (void)pagina(g, &n, &cols, &tit);
    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        baldosa_caja(i, n, cols, &x, &y, &w, &h);
        if (bx >= x && bx < x + w && by >= y && by < y + h) return i;
    }
    return -1;
}

static void menu_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int n, cols;
    const char *tit;
    const baldosa_t *p = pagina(g, &n, &cols, &tit);

    ch_ui_titulo(g, _(tit), g->sel2 ? _("DESLIZA PARA VOLVER") : NULL);

    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        const char *txt = _(p[i].txt);
        const char *sub = p[i].accion == AC_SONIDO
                        ? _(SONIDOS[ch_sonido_get() % 3]) : NULL;

        baldosa_caja(i, n, cols, &x, &y, &w, &h);
        ch_rect(b, x, y, w, h, ch_rgb(0x1A2133));
        ch_frame(b, x, y, w, h, ch_rgb(0x3D465F));
        ch_rect(b, x + 1, y + 1, w - 2, 1, ch_rgb(0x2C3550));

        if (w >= 120) {                 /* wide: the icon to the left of the text */
            ch_ui_icono(b, x + 14, y + h / 2 - 18, p[i].icono, 3);
            ch_text(b, x + 62, y + h / 2 - 4, txt, ch_rgb(0xFFFFFF));
        } else {                        /* narrow: the icon over the text     */
            /* Centred as ONE block -icon, label and, if there is one, the
             * value- because the page with five tiles is 40 px tall and
             * anything anchored to the edges overlaps in the middle. */
            int alto = 24 + 6 + 7 + (sub ? 9 : 0);
            int iy = y + (h - alto) / 2;

            ch_ui_icono(b, x + w / 2 - 12, iy, p[i].icono, 2);
            ch_text(b, x + (w - ch_text_w(txt)) / 2, iy + 30, txt,
                    ch_rgb(0xFFFFFF));
            if (sub) {
                ch_text(b, x + (w - ch_text_w(sub)) / 2, iy + 39, sub,
                        ch_rgb(0xFFE45E));
            }
        }
    }
}

void ch_ui_menu(ch_t *g)
{
    g->modo_prev = g->modo;
    g->modo = MODO_MENU;
    g->sel = 0;
    g->sel2 = 0;                    /* always back to the root page */
    g->scroll = 0;
    g->rehacer_fondo = 1;
    ch_sfx(900, 25);
}

/* --------------------------------------------------------------------------
 * Workshop
 *
 * The part you touch is fitted and the one that was on goes to the bag. It is
 * a swap and not an "equip": that way the bag never overflows on a change and
 * a part is never lost by accident.
 * -------------------------------------------------------------------------- */

static const char *const CATS[P_CATS] = { N_("CAB"), N_("TOR"),
                                         N_("BRA"), N_("PIE") };

/* El indice filtrado que comparten el taller, la mochila y la tienda: dice
 * QUE se muestra en cada fila, no lo que se muestra. */
static uint8_t s_lista[ITEMS];
static int     s_nlista;

/* --------------------------------------------------------------------------
 * EL TALLER, EN DOS PARTES
 *
 * Era una lista de la mochila entera y el robot no se veia: montabas a ciegas
 * y confirmabas mirando cuatro nombres en un encabezado. Ahora hay una vista
 * general -el robot lo mas grande que entra, y a la derecha las cuatro
 * categorias con lo que lleva puesto- y, al tocar una, la lista de lo que
 * tenes en la mochila DE ESA CATEGORIA, a pantalla completa. Elegis, se monta
 * y volves a ver el robot cambiado, que es la unica razon por la que uno abre
 * esta pantalla.
 *
 * sel2: 0 = la vista general, 1+cat = la lista de esa categoria.
 * sel:  0 = nada elegido, 1+fila = la fila tocada una vez (la segunda monta).
 * -------------------------------------------------------------------------- */
#define TA_CY     34                    /* el contenido: 34..166             */
#define TA_RX      6                    /* el robot, a escala 3              */
#define TA_RY     38
#define TA_BX     90                    /* la columna de la derecha          */
#define TA_BW    (CH_W - TA_BX - 6)
#define TA_EY     34                    /* los tres robots del equipo        */
#define TA_EH     22
#define TA_BY     60                    /* los cuatro botones de categoria   */
#define TA_BH     26

/* Cual de los robots del equipo se esta armando. Vive fuera de ch_t porque no
 * es estado del juego: al salir del taller no significa nada. */
static uint8_t s_ta_bot;

static int ta_bot_en(int bx, int by)
{
    int w = (TA_BW - 4) / EQUIPO;

    if (by < TA_EY || by >= TA_EY + TA_EH) return -1;
    for (int i = 0; i < EQUIPO; i++) {
        int x = TA_BX + i * (w + 2);
        if (bx >= x && bx < x + w) return i;
    }
    return -1;
}

static int ta_cat_en(int bx, int by)
{
    if (bx < TA_BX || bx >= TA_BX + TA_BW) return -1;
    for (int c = 0; c < P_CATS; c++) {
        int y = TA_BY + c * TA_BH;
        if (by >= y && by < y + TA_BH - 3) return c;
    }
    return -1;
}

/* El robot que se esta armando: el activo, o el de la reserva que elegiste. */
static ch_robot_t *ta_robot(ch_t *g)
{
    ch_robot_t *r = ch_eq(&g->s, s_ta_bot % EQUIPO);
    if (!r) { s_ta_bot = 0; r = &g->s.yo; }
    return r;
}

/* Las piezas de la mochila de una categoria, en s_lista, CON LA PUESTA
 * PRIMERA.
 *
 * Sin ella la lista contesta "cual de las sueltas" y la pregunta es "cual de
 * todas": el numero de la que llevas puesta estaba arriba, en otra linea, y
 * compararla con tres candidatas era ir y venir con la vista. Ahora es una
 * fila mas, marcada, con los mismos numeros y la misma pieza dibujada. */
#define TA_PUESTA  0xFE                 /* la marca de "esta es la que llevas" */

static void ta_juntar(ch_t *g, int cat)
{
    s_nlista = 0;
    s_lista[s_nlista++] = TA_PUESTA;
    for (int i = 0; i < MOCHILA; i++) {
        if (g->s.piezas[i] == 0xFF) continue;
        if (PIEZA_CAT(g->s.piezas[i]) != cat) continue;
        s_lista[s_nlista++] = (uint8_t)i;
    }
}

/* Que pieza es una fila de la lista, sea de la mochila o la puesta. */
static uint8_t ta_pieza(ch_t *g, int cat, int fila)
{
    uint8_t e = s_lista[fila % (s_nlista ? s_nlista : 1)];
    return e == TA_PUESTA ? PIEZA_ID(cat, ta_robot(g)->pieza[cat])
                          : g->s.piezas[e];
}

static void taller_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    char t[40];

    if (g->sel2 == 0) {
        /* --- la vista general ------------------------------------------- */
        ch_robot_t *r = ta_robot(g);
        int sueltas = 0;
        int ew = (TA_BW - 4) / EQUIPO;

        {
            int pct = 0;
            const char *j = ch_robot_juego_nombre(r, &pct);
            if (j) {
                snprintf(t, sizeof(t), _("%s  %s +%d%%"),
                         _(ch_tipo_nombre[r->tipo % TIPOS]), _(j), pct);
            } else {
                snprintf(t, sizeof(t), _("TIPO %s"),
                         _(ch_tipo_nombre[r->tipo % TIPOS]));
            }
            ch_ui_titulo(g, _("TALLER"), t);
        }
        ch_robot_draw(b, TA_RX + 39, TA_RY, r, 3, false, 0, 0);

        /* LOS TRES DEL EQUIPO. El taller armaba SIEMPRE el robot que sale a
         * pelear, asi que las piezas del segundo y del tercero no se podian
         * tocar sin cambiar cual sale primero. Son tres botones. */
        for (int i = 0; i < EQUIPO; i++) {
            const ch_robot_t *q = ch_eq(&g->s, i);
            int x = TA_BX + i * (ew + 2);
            bool aqui = (i == s_ta_bot % EQUIPO);

            ch_round(b, x, TA_EY, ew, TA_EH, 3,
                     ch_rgb(aqui ? 0x2B3145 : 0x141720));
            ch_frame(b, x, TA_EY, ew, TA_EH,
                     ch_rgb(aqui ? 0xFFE45E : 0x3D465F));
            if (!q) {
                ch_text_center(b, x + ew / 2, TA_EY + 8, "-",
                               ch_rgb(0x3D465F), ch_rgb(0x05060C));
                continue;
            }
            snprintf(t, sizeof(t), "%.4s", ch_robot_nombre(q));
            ch_text_center(b, x + ew / 2, TA_EY + 3, t,
                           ch_rgb(aqui ? 0xFFFFFF : 0x606B85), ch_rgb(0x05060C));
            snprintf(t, sizeof(t), _("N%d"), q->nivel);
            ch_text_center(b, x + ew / 2, TA_EY + 12, t,
                           ch_rgb(aqui ? 0xFFE45E : 0x606B85), ch_rgb(0x05060C));
        }

        for (int c = 0; c < P_CATS; c++) {
            const ch_part_t *p = &ch_partes[PIEZA_ID(c, r->pieza[c])];
            int y = TA_BY + c * TA_BH;
            int n = 0;

            for (int i = 0; i < MOCHILA; i++) {
                if (g->s.piezas[i] != 0xFF &&
                    PIEZA_CAT(g->s.piezas[i]) == c) n++;
            }
            ch_round(b, TA_BX, y, TA_BW, TA_BH - 3, 3, ch_rgb(0x1A2133));
            ch_frame(b, TA_BX, y, TA_BW, TA_BH - 3,
                     ch_rgb(n ? 0x8A93AB : 0x3D465F));
            ch_text(b, TA_BX + 5, y + 3, _(CATS[c]), ch_rgb(0x8A93AB));
            if (n) {
                snprintf(t, sizeof(t), "+%d", n);
                ch_text(b, TA_BX + TA_BW - 5 - ch_text_w(t), y + 3, t,
                        ch_rgb(0x4ADE80));
            }
            snprintf(t, sizeof(t), "%.14s", _(p->nombre));
            ch_text(b, TA_BX + 5, y + 13, t, ch_rgb(0xFFFFFF));
            sueltas += n;
        }
        /* Y si no hay NADA suelto, decirlo: cuatro botones que se abren a una
         * lista vacia son cuatro caminos a ninguna parte. */
        if (!sueltas) {
            /* En dos lineas: de una sola son 34 caracteres, o sea 204 px de
             * ancho en una pantalla de 184. */
            ch_text_center(b, 46, 146, _("GANA COMBATES"),
                           ch_rgb(0x606B85), ch_rgb(0x05060C));
            ch_text_center(b, 46, 156, _("PARA PIEZAS"),
                           ch_rgb(0x606B85), ch_rgb(0x05060C));
        }
        return;
    }

    /* --- la lista de una categoria ------------------------------------- */
    {
        int cat = (g->sel2 - 1) % P_CATS;

        ta_juntar(g, cat);
        lista_geom(60, 36, 3);
        snprintf(t, sizeof(t), "%s  %s", _("TALLER"), _(CATS[cat]));
        ch_ui_titulo(g, t, _("TOCA 2 VECES"));

        flechas(g, g->scroll > 0, g->scroll + s_lfilas < s_nlista);
        if (s_nlista == 1) {
            ch_text(b, 6, 46, _("NO TENES OTRA DE ESTE TIPO"), ch_rgb(0x8A93AB));
        }

        /* Lo que cambiaria, si ya tocaste una fila una vez. */
        if (g->sel) {
            ch_robot_t *r = ta_robot(g);
            ch_robot_t prueba = *r;
            uint8_t id = ta_pieza(g, cat, g->sel - 1);
            char t2[72];

            prueba.pieza[cat] = PIEZA_VAR(id);
            ch_robot_stats(&prueba);
            ch_text(b, 6, 42, _(ch_partes[id].nombre), ch_rgb(0xFFE45E));
            ch_text(b, CH_W - 6 - ch_text_w(_("TOCA 2X")), 42, _("TOCA 2X"),
                    ch_rgb(0x8A93AB));
            snprintf(t2, sizeof(t2), "PV%d>%d A%d>%d D%d>%d V%d>%d",
                     r->vida_max, prueba.vida_max,
                     r->atk, prueba.atk, r->def, prueba.def,
                     r->vel, prueba.vel);
            ch_text(b, 6, 52, t2, ch_rgb(0xD5DCEB));
            {   /* y en que quedaria el juego, que es media decision */
                int ja = ch_robot_juego(r), jb = ch_robot_juego(&prueba);
                if (ja != jb) {
                    snprintf(t, sizeof(t), _("JUEGO %d>%d"), ja, jb);
                    ch_text(b, CH_W - 6 - ch_text_w(t), 52, t,
                            jb > ja ? ch_rgb(0x4ADE80) : ch_rgb(0xFF4A3D));
                }
            }
        }

        for (int i = 0; i < s_lfilas; i++) {
            int k = g->scroll + i;
            int y = s_ly0 + i * s_lfh, h = s_lfh - 4;
            uint8_t id;
            const ch_part_t *p, *puesta;

            if (k >= s_nlista) break;
            id = ta_pieza(g, cat, k);
            p = &ch_partes[id];
            puesta = &ch_partes[PIEZA_ID(cat, ta_robot(g)->pieza[cat])];
            bool esta = (s_lista[k] == TA_PUESTA);

            ch_rect(b, LX, y, LW, h,
                    ch_rgb(g->sel == k + 1 ? 0x2A3350
                                           : (esta ? 0x14261C : 0x1A2133)));
            ch_frame(b, LX, y, LW, h,
                     ch_rgb(g->sel == k + 1 ? 0xFFE45E
                                            : (esta ? 0x4ADE80 : 0x3D465F)));
            ch_rect(b, LX + 1, y + 1, LW - 2, 1, ch_rgb(0x2C3550));
            ch_rect(b, LX + 3, y + 3, 30, h - 6, ch_rgb(0x0E111A));
            ch_part_draw(b, cat, PIEZA_VAR(id), LX + 18, y + h / 2, 1,
                         (int)ta_robot(g)->skin);
            ch_text(b, LX + 38, y + 5, _(p->nombre), ch_rgb(0xFFFFFF));
            snprintf(t, sizeof(t), _("PV%d E%d"), p->vida, p->energia);
            ch_text(b, LX + 38, y + 17, t, ch_rgb(0x606B85));
            /* EL TIPO DE LA PIEZA, que es lo que arma el juego: sin el, la
             * bonificacion por llevar varias del mismo tipo es un numero que
             * cambia solo y no se sabe por que. */
            {
                const char *tn = _(ch_tipo_nombre[p->tipo % TIPOS]);
                ch_text(b, LX + 38 + ch_text_w(_(p->nombre)) + 6, y + 5, tn,
                        ch_rgb(ch_tipo_color[p->tipo % TIPOS]));
            }
            if (esta) {
                ch_text(b, LX + LW - 5 - ch_text_w(_("PUESTA")), y + 5,
                        _("PUESTA"), ch_rgb(0x4ADE80));
            }

            /* LA DIFERENCIA, NO EL NUMERO PELADO. La fila decia "A24 D9 V6" y
             * eso no contesta la unica pregunta que uno tiene delante de la
             * mochila: si es mejor o peor que la que lleva puesta. */
            if (!esta) {
                static const char *const LET[3] = { "A", "D", "V" };
                int dif[3] = { p->atk - puesta->atk, p->def - puesta->def,
                               p->vel - puesta->vel };
                int x = LX + LW - 5;
                for (int q = 2; q >= 0; q--) {
                    snprintf(t, sizeof(t), "%s%+d", LET[q], dif[q]);
                    x -= ch_text_w(t) + 4;
                    ch_text(b, x, y + 17, t,
                            dif[q] > 0 ? ch_rgb(0x4ADE80)
                                       : (dif[q] < 0 ? ch_rgb(0xFF4A3D)
                                                     : ch_rgb(0x606B85)));
                }
            }
        }
    }
}

static void taller_toque(ch_t *g, int bx, int by)
{
    if (g->sel2 == 0) {
        int c = ta_bot_en(bx, by);
        if (c >= 0) {
            if (!ch_eq(&g->s, c)) { ch_sfx(220, 40); return; }
            s_ta_bot = (uint8_t)c;
            ch_sfx(1000, 25);
            g->rehacer_fondo = 1;
            return;
        }
        c = ta_cat_en(bx, by);
        if (c < 0) return;
        g->sel2 = (uint8_t)(c + 1);
        g->sel = 0;
        g->scroll = 0;
        ch_sfx(1000, 25);
        g->rehacer_fondo = 1;
        return;
    }
    {
        int cat = (g->sel2 - 1) % P_CATS;
        int f = flecha_en(bx, by);
        int k;
        uint8_t id, antes;

        if (f) {
            int nuevo = (int)g->scroll + f;
            if (nuevo >= 0 && nuevo + s_lfilas <= s_nlista) {
                g->scroll = (uint8_t)nuevo;
                g->rehacer_fondo = 1;
            }
            return;
        }
        f = fila_en(bx, by);
        if (f < 0) return;
        k = g->scroll + f;
        if (k >= s_nlista) return;
        if (s_lista[k] == TA_PUESTA) {
            /* Es la que ya llevas: se muestra para comparar, no para montar. */
            g->sel = (uint8_t)(k + 1);
            ch_sfx(900, 20);
            g->rehacer_fondo = 1;
            return;
        }

        /* Primer toque: mostrar en que cambiaria. Segundo: montarla. Montar
         * de una hacia facil errarle a la fila y cambiar el robot sin
         * querer. */
        if (g->sel != (uint8_t)(k + 1)) {
            g->sel = (uint8_t)(k + 1);
            ch_sfx(900, 20);
            g->rehacer_fondo = 1;
            return;
        }
        {
            ch_robot_t *r = ta_robot(g);
            id = g->s.piezas[s_lista[k]];
            antes = PIEZA_ID(cat, r->pieza[cat]);
            r->pieza[cat] = PIEZA_VAR(id);
            g->s.piezas[s_lista[k]] = antes;
            ch_ver(&g->s, id);
            ch_ver(&g->s, antes);
            ch_robot_stats(r);
            if (r->vida > r->vida_max) r->vida = r->vida_max;
        }

        ch_sfx(1200, 40);
        ch_ui_aviso(g, _(ch_partes[id].nombre));
        /* Y de vuelta al robot, que es lo que uno queria ver. */
        g->sel2 = 0;
        g->sel = 0;
        g->scroll = 0;
        g->rehacer_fondo = 1;
        g->hud_sucio = 1;
    }
}


/* --------------------------------------------------------------------------
 * Items
 * -------------------------------------------------------------------------- */


static void objetos_fondo(ch_t *g)
{
    char t[30], d[12];

    lista_geom(38, 42, 3);
    ch_ui_titulo(g, _("OBJETOS"), _("TOCA UNO PARA USARLO"));

    s_nlista = 0;
    for (int i = 1; i < ITEMS; i++) {
        if (g->s.obj[i]) s_lista[s_nlista++] = (uint8_t)i;
    }
    if (!s_nlista) {
        ch_text_center(&g->bg, CH_W / 2, 100, _("NO TENES NADA."),
                       ch_rgb(0x8A93AB), ch_rgb(0x05060C));
        return;
    }
    flechas(g, g->scroll > 0, g->scroll + s_lfilas < s_nlista);
    for (int i = 0; i < s_lfilas; i++) {
        int k = g->scroll + i;
        if (k >= s_nlista) break;
        snprintf(d, sizeof(d), "x%d", g->s.obj[s_lista[k]]);
        fila_item(g, i, s_lista[k], d, ch_rgb(0xFFE45E), true);
        (void)t;
    }
}

static void objetos_toque(ch_t *g, int bx, int by)
{
    int f = flecha_en(bx, by);
    if (f) {
        int s = (int)g->scroll + f;
        if (s >= 0 && s + s_lfilas <= s_nlista) { g->scroll = (uint8_t)s; g->rehacer_fondo = 1; }
        return;
    }
    f = fila_en(bx, by);
    if (f < 0) return;
    int k = g->scroll + f;
    if (k >= s_nlista) return;

    int it = s_lista[k];
    const ch_item_t *d = &ch_items[it];
    char aviso[26];

    switch (it) {
    case IT_ACEITE: case IT_ACEITE2: {
        if (g->s.yo.vida >= g->s.yo.vida_max) { ch_sfx(220, 40); return; }
        int cura = d->valor;
        if (g->s.yo.vida + cura > g->s.yo.vida_max) cura = g->s.yo.vida_max - g->s.yo.vida;
        g->s.yo.vida = (int16_t)(g->s.yo.vida + cura);
        g->s.obj[it]--;
        snprintf(aviso, sizeof(aviso), _("+%d DE VIDA"), cura);
        break;
    }
    case IT_BATERIA: case IT_BATERIA2: {
        if (g->s.yo.ene >= g->s.yo.ene_max) { ch_sfx(220, 40); return; }
        int c = d->valor;
        if (g->s.yo.ene + c > g->s.yo.ene_max) c = g->s.yo.ene_max - g->s.yo.ene;
        g->s.yo.ene = (int16_t)(g->s.yo.ene + c);
        g->s.obj[it]--;
        snprintf(aviso, sizeof(aviso), _("+%d DE ENERGIA"), c);
        break;
    }
    case IT_CHIP:
        g->s.yo.exp += 300;
        g->s.obj[it]--;
        while (g->s.yo.nivel < 60 && g->s.yo.exp >= ch_exp_nivel(g->s.yo.nivel + 1)) {
            g->s.yo.nivel++;
            ch_robot_curar(&g->s.yo);
            ch_snd_melodia(g, CH_MEL_NIVEL);
        }
        snprintf(aviso, sizeof(aviso), _("NIVEL %d"), g->s.yo.nivel);
        break;
    default:
        ch_sfx(220, 40);
        return;
    }

    ch_sfx(1100, 40);
    ch_ui_aviso(g, aviso);
    g->rehacer_fondo = 1;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * The robot's data card
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * THE TEAM (what used to be the robot's data card)
 *
 * With three robots the card stopped being about one of them. Rather than add
 * a tenth row to a menu that already reaches the bottom of the touch window,
 * the card GREW: three tabs at the top choose which robot you are looking at,
 * and the two buttons at the bottom are what you can do with that one.
 *
 * An empty slot is not hidden, it is an offer: ARMAR builds a robot out of the
 * loose parts in the bag. That is the only place in the game that explains why
 * you would keep a spare head.
 * -------------------------------------------------------------------------- */

/* EQUIPO, medido de arriba hacia abajo y terminando ANTES del HUD.
 *
 * Los botones estaban en 156 y miden 16, o sea acababan en 172, y el HUD manda
 * desde 168: en la placa se veia media palabra. Ahora la pantalla tiene tres
 * vistas -el robot, sus numeros y sus golpes- y cada una ocupa el mismo hueco
 * medido, que es lo que hace que ninguna se desborde por agregarle una linea.  */
#define EQ_Y      34                    /* los tres del equipo               */
#define EQ_H      24
#define EQ_W      58
#define EQ_RX     34                    /* el robot, a escala 2: 62..142     */
#define EQ_RY     62
#define EQ_PX     72                    /* el panel de la derecha            */
#define EQ_PW    (CH_W - EQ_PX - 6)
#define EQ_VY     62                    /* el par de botones del panel       */
#define EQ_VH     14
#define EQ_CY     79
#define EQ_BY    148                    /* los botones de accion: 148..166   */
#define EQ_BH     18

static int eq_slot_en(int bx, int by)
{
    if (by < EQ_Y || by >= EQ_Y + EQ_H) return -1;
    for (int i = 0; i < EQUIPO; i++) {
        int x = 4 + i * (EQ_W + 3);
        if (bx >= x && bx < x + EQ_W) return i;
    }
    return -1;
}

static int eq_boton_en(int bx, int by)
{
    if (by < EQ_BY || by >= EQ_BY + EQ_BH) return -1;
    return bx < CH_W / 2 ? 0 : 1;
}

/* Uno de los dos botones del panel: los numeros o los golpes. */
static void eq_vista(ch_t *g, int i, const char *txt, bool sel)
{
    ch_buf_t *b = &g->bg;
    int w = (EQ_PW - 3) / 2;
    int x = EQ_PX + i * (w + 3);

    ch_round(b, x, EQ_VY, w, EQ_VH, 3, ch_rgb(sel ? 0x2B3145 : 0x141720));
    ch_frame(b, x, EQ_VY, w, EQ_VH, ch_rgb(sel ? 0xFFE45E : 0x3D465F));
    ch_text_center(b, x + w / 2, EQ_VY + 4, txt,
                   ch_rgb(sel ? 0xFFFFFF : 0x606B85), ch_rgb(0x05060C));
}

static int eq_vista_en(int bx, int by)
{
    int w = (EQ_PW - 3) / 2;
    if (by < EQ_VY || by >= EQ_VY + EQ_VH) return -1;
    for (int i = 0; i < 2; i++) {
        int x = EQ_PX + i * (w + 3);
        if (bx >= x && bx < x + w) return i;
    }
    return -1;
}

static void eq_boton(ch_t *g, int i, const char *txt, bool activo)
{
    ch_buf_t *b = &g->bg;
    int x = i ? CH_W / 2 + 2 : 4;
    int w = CH_W / 2 - 6;

    ch_round(b, x, EQ_BY, w, EQ_BH, 3, ch_rgb(activo ? 0x2A3350 : 0x141824));
    ch_frame(b, x, EQ_BY, w, EQ_BH, ch_rgb(activo ? 0x8A93AB : 0x2B3145));
    ch_text_center(b, x + w / 2, EQ_BY + 6, txt,
                   ch_rgb(activo ? 0xFFFFFF : 0x545C70), ch_rgb(0x05060C));
}

static void ficha_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int sel = g->sel < EQUIPO ? g->sel : 0;
    const ch_robot_t *r = ch_eq(&g->s, sel);
    char t[30];

    if (!r) { sel = 0; r = &g->s.yo; g->sel = 0; }

    {
        int pct = 0;
        const char *j = ch_robot_juego_nombre(r, &pct);
        if (j) {
            snprintf(t, sizeof(t), _("%s  %s +%d%%"),
                     _(ch_tipo_nombre[r->tipo % TIPOS]), _(j), pct);
        } else {
            snprintf(t, sizeof(t), _("NIVEL %d   TIPO %s"), r->nivel,
                     _(ch_tipo_nombre[r->tipo % TIPOS]));
        }
    }
    ch_ui_titulo(g, ch_robot_nombre(r), t);

    /* --- the three tabs --------------------------------------------------- */
    for (int i = 0; i < EQUIPO; i++) {
        const ch_robot_t *q = ch_eq(&g->s, i);
        int x = 4 + i * (EQ_W + 3);
        bool aqui = (i == sel);

        ch_round(b, x, EQ_Y, EQ_W, EQ_H, 3,
                 ch_rgb(aqui ? 0x2A3350 : 0x141824));
        ch_frame(b, x, EQ_Y, EQ_W, EQ_H,
                 ch_rgb(aqui ? 0xFFE45E : 0x2B3145));
        if (!q) {
            ch_text_center(b, x + EQ_W / 2, EQ_Y + 9, _("VACIO"),
                           ch_rgb(0x545C70), ch_rgb(0x05060C));
            continue;
        }
        /* Three rows inside 24 px: the name, the level with its state, and the
         * health. Eight characters is what fits at six pixels a letter, and a
         * name cut short still tells the robots apart. */
        snprintf(t, sizeof(t), "%.8s", ch_robot_nombre(q));
        ch_text(b, x + 4, EQ_Y + 3, t, ch_rgb(q->vida > 0 ? 0xFFFFFF : 0xE05252));
        snprintf(t, sizeof(t), _("N%d"), q->nivel);
        ch_text(b, x + 4, EQ_Y + 12, t, ch_rgb(0xFFE45E));
        {
            const char *e = i == 0 ? _("SALE") : (q->vida <= 0 ? _("ROTO") : "");
            if (e[0]) {
                ch_text(b, x + EQ_W - 4 - ch_text_w(e), EQ_Y + 12, e,
                        ch_rgb(i == 0 ? 0x8A93AB : 0xE05252));
            }
        }
        ch_barra(b, x + 4, EQ_Y + 20, EQ_W - 8, q->vida, q->vida_max,
                 ch_rgb(q->vida > 0 ? 0x4ADE80 : 0xE05252));
    }

    /* --- the selected robot, whole ---------------------------------------- */
    ch_robot_draw(b, EQ_RX, EQ_RY, r, 2, false, 0, 0);
    /* Sin barras debajo del robot: a escala 2 llega hasta 142 y lo unico que
     * queda abajo son los botones. La vida ya esta en la pestana del equipo y
     * otra vez en el panel de numeros. */

    /* --- and the panel, which is one of TWO -------------------------------- */
    eq_vista(g, 0, _("NUMEROS"), g->sel2 == 0);
    eq_vista(g, 1, _("GOLPES"),  g->sel2 != 0);
    ch_rect(b, EQ_PX, EQ_CY, EQ_PW, EQ_BY - EQ_CY - 3, ch_rgb(0x111420));
    ch_frame(b, EQ_PX, EQ_CY, EQ_PW, EQ_BY - EQ_CY - 3, ch_rgb(0x3D465F));

    if (g->sel2 == 0) {
        static const char *const ET[5] = { N_("VIDA"), N_("ENER"), N_("ATAQ"),
                                           N_("DEFE"), N_("VELO") };
        int val[5] = { r->vida, r->ene, r->atk, r->def, r->vel };
        int tope[5] = { r->vida_max, r->ene_max, 0, 0, 0 };
        uint32_t col[5] = { 0x4ADE80, 0x4A9DF5, 0xFF9F0A, 0x7BE9FF, 0xB072F0 };

        for (int i = 0; i < 5; i++) {
            int y = EQ_CY + 5 + i * 12;
            ch_text(b, EQ_PX + 5, y, _(ET[i]), ch_rgb(0x8A93AB));
            if (tope[i]) snprintf(t, sizeof(t), "%d/%d", val[i], tope[i]);
            else         snprintf(t, sizeof(t), "%d", val[i]);
            ch_text(b, EQ_PX + EQ_PW - 5 - ch_text_w(t), y, t, ch_rgb(col[i]));
        }
    } else {
        for (int i = 0; i < 4; i++) {
            int y = EQ_CY + 3 + i * 16;
            const ch_move_t *m;
            if (i) ch_hline(b, EQ_PX + 4, y - 3, EQ_PW - 8, ch_rgb(0x232B41));
            if (i >= r->nmov) {
                ch_text(b, EQ_PX + 5, y + 3, "-", ch_rgb(0x3D465F));
                continue;
            }
            m = &ch_moves[r->mov[i] % MOVES];
            ch_text(b, EQ_PX + 5, y, _(m->nombre), ch_rgb(0xFFFFFF));
            ch_text(b, EQ_PX + 5, y + 8, _(ch_tipo_nombre[m->tipo % TIPOS]),
                    ch_rgb(ch_tipo_color[m->tipo % TIPOS]));
            snprintf(t, sizeof(t), _("%d/%dE"), m->poder, m->costo);
            ch_text(b, EQ_PX + EQ_PW - 5 - ch_text_w(t), y + 8, t,
                    ch_rgb(0x8A93AB));
        }
    }

    /* --- what can be done with it ----------------------------------------- */
    if (sel == 0) {
        bool puede = ch_eq_puede_armar(&g->s);
        eq_boton(g, 0, _("ARMAR OTRO"), puede);
        eq_boton(g, 1, _("VOLVER"), true);

    } else {
        eq_boton(g, 0, _("QUE SALGA ESTE"), r->vida > 0);
        eq_boton(g, 1, _("DESARMAR"), true);
    }
}

static void ficha_toque(ch_t *g, int bx, int by)
{
    int i = eq_slot_en(bx, by);
    int b;

    if (i >= 0) {
        if (ch_eq(&g->s, i)) { g->sel = (uint8_t)i; ch_sfx(1000, 20); }
        else if (ch_eq_puede_armar(&g->s)) {
            int slot = ch_eq_armar(&g->s);
            if (slot > 0) {
                g->sel = (uint8_t)slot;
                ch_sfx(1500, 90);
                ch_ui_aviso(g, _("ROBOT ARMADO!"));
                g->quiere_guardar = 1;
            }
        } else {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("FALTAN PIEZAS SUELTAS"));
        }
        g->rehacer_fondo = 1;
        return;
    }

    {   /* los dos botones del panel: numeros o golpes */
        int v = eq_vista_en(bx, by);
        if (v >= 0) {
            g->sel2 = (uint8_t)v;
            ch_sfx(1000, 20);
            g->rehacer_fondo = 1;
            return;
        }
    }

    b = eq_boton_en(bx, by);
    if (b < 0) return;

    if (g->sel == 0) {
        if (b == 1) { g->sel = 0; g->modo = MODO_MENU; ch_sfx(700, 30); }
        else if (ch_eq_puede_armar(&g->s)) {
            int slot = ch_eq_armar(&g->s);
            if (slot > 0) {
                g->sel = (uint8_t)slot;
                ch_sfx(1500, 90);
                ch_ui_aviso(g, _("ROBOT ARMADO!"));
                g->quiere_guardar = 1;
            }
        } else {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("FALTAN PIEZAS SUELTAS"));
        }
    } else if (b == 0) {
        const ch_robot_t *r = ch_eq(&g->s, g->sel);
        if (!r || r->vida <= 0) {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("ESTA ROTO: AL TALLER"));
        } else {
            ch_eq_activar(&g->s, g->sel);
            g->sel = 0;
            ch_sfx(1400, 70);
            ch_ui_aviso(g, _("CAMBIASTE DE ROBOT"));
            g->hud_sucio = 1;
            g->quiere_guardar = 1;
        }
    } else {
        if (ch_eq_desarmar(&g->s, g->sel)) {
            g->sel = 0;
            ch_sfx(500, 90);
            ch_ui_aviso(g, _("PIEZAS A LA MOCHILA"));
            g->quiere_guardar = 1;
        } else {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("MOCHILA LLENA!"));
        }
    }
    g->rehacer_fondo = 1;
}

/* --------------------------------------------------------------------------
 * Shop
 * -------------------------------------------------------------------------- */

/* EL SURTIDO CRECE CON LA ZONA.
 *
 * Las ocho tiendas vendian exactamente lo mismo, asi que llegar a un pueblo
 * nuevo no cambiaba nada: el aceite puro estaba a la venta en el minuto cinco
 * -con 180 creditos que no tenias- y el iman tambien. Ahora cada zona agrega
 * lo suyo, que es lo que hace que valga la pena entrar a la tienda de un
 * pueblo al que acabas de llegar.
 *
 * Es una tabla de cuantos items del arreglo se ven, no ocho arreglos: el
 * orden ya es de barato a caro. */
static const uint8_t SURTIDO[] = {
    IT_ACEITE, IT_BATERIA, IT_ACEITE2, IT_BATERIA2, IT_CHIP, IT_IMAN,
    IT_SOLDADOR,
};
static const uint8_t SURTIDO_ZONA[ZONAS] = { 2, 3, 4, 5, 5, 6, 7, 7 };

static int surtido_n(const ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int z = r->zona < 1 ? 1 : (r->zona > ZONAS ? ZONAS : r->zona);
    return SURTIDO_ZONA[z - 1];
}
#define NSURTIDO surtido_n(g)

static void tienda_fondo(ch_t *g)
{
    char t[30], d[16];

    lista_geom(38, 42, 3);
    ch_ui_titulo(g, _("TIENDA"), NULL);
    snprintf(t, sizeof(t), _("TENES %d CREDITOS"), g->s.creditos);
    ch_text(&g->bg, 8, 20, t, ch_rgb(0xFFE45E));

    flechas(g, g->scroll > 0, g->scroll + s_lfilas < NSURTIDO);
    for (int i = 0; i < s_lfilas; i++) {
        int k = g->scroll + i;
        if (k >= NSURTIDO) break;
        const ch_item_t *it = &ch_items[SURTIDO[k]];
        snprintf(d, sizeof(d), _("%dC x%d"), it->precio, g->s.obj[SURTIDO[k]]);
        /* The description no longer lives in a panel at the bottom that shows
         * ONE item: it is in every row. Buying blind by the name is what made
         * nobody buy anything but oil. */
        fila_item(g, i, SURTIDO[k], d, ch_rgb(0xFFE45E),
                  g->s.creditos >= it->precio);
    }
}

static void tienda_toque(ch_t *g, int bx, int by)
{
    int f = fila_en(bx, by);
    if (f < 0 || f >= NSURTIDO) return;

    /* The first tap shows what it does; the second buys. A stray tap cannot
     * cost you 400 credits. */
    if (g->sel != f) {
        g->sel = (uint8_t)f;
        ch_sfx(900, 20);
        g->rehacer_fondo = 1;
        return;
    }

    int it = SURTIDO[f];
    const ch_item_t *d = &ch_items[it];
    if (g->s.creditos < d->precio) { ch_sfx(220, 40); ch_ui_aviso(g, _("NO TE ALCANZA")); g->rehacer_fondo = 1; return; }
    if (g->s.obj[it] >= 99) return;

    g->s.creditos = (uint16_t)(g->s.creditos - d->precio);
    g->s.obj[it]++;
    ch_sfx(1300, 40);
    ch_ui_aviso(g, _("COMPRADO"));
    g->rehacer_fondo = 1;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * The parts register
 *
 * The 64 parts, really drawn and not listed by name. The ones you have not
 * come across yet appear as a SILHOUETTE: you see the shape and nothing else.
 *
 * This screen exists thanks to the decision to draw the parts by descriptor
 * and not by sprite. With bitmaps it would have cost 64 images; this way it
 * cost a thirty-line dispatch function.
 *
 * Four tabs at the top, four by four below, and the data card of the selected
 * one in the bottom strip. It all ends at y=172, which is the touch limit.
 * -------------------------------------------------------------------------- */

/* THE CARD GOES ON TOP, NOT UNDERNEATH.
 *
 * It used to live at y=152 and be 22 tall, so it ended at 174 and the HUD -
 * which owns everything from 168 - ate its second line. The fix is not to
 * move it up by six: it is that the only thing with a variable number of rows
 * on this screen is the GRID, so the grid is what takes the leftover space
 * and everything else is measured from the top. With the card in the header
 * the grid gets 104 px, which is two rows of 52 - twice the old cell - and
 * that is what makes the parts big enough to tell apart. */
#define RG_HDR   40                     /* title + the selected part's card  */
#define RG_TX     6                     /* tabs                              */
#define RG_TY    42
#define RG_TW    43
#define RG_TH    16
#define RG_GX     8                     /* grid                              */
#define RG_GY    60
#define RG_CW    42
#define RG_CH    52                     /* 2 rows x 52 = 104: 60..164        */
#define RG_FILAS  2
#define RG_PAG   (RG_FILAS * 4)

static int rg_celda(int bx, int by)
{
    int c, f;

    if (bx < RG_GX || by < RG_GY) return -1;
    c = (bx - RG_GX) / RG_CW;
    f = (by - RG_GY) / RG_CH;
    if (c > 3 || f >= RG_FILAS) return -1;
    return f * 4 + c;
}

static void registro_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int cat = g->sel2 % P_CATS;
    int pag = g->scroll ? 1 : 0;
    int n = 0;
    char t[34];

    for (int i = 0; i < PIEZAS; i++) if (ch_visto(&g->s, i)) n++;

    ch_rect(b, 0, 0, CH_W, MAP_H, ch_rgb(0x0B0D14));
    ch_vgrad(b, 0, 0, CH_W, RG_HDR, ch_rgb(0x1B2340), ch_rgb(0x0B0D14));
    /* The counter goes NEXT TO the title and not in the right-hand corner:
     * that corner belongs to the page arrows and the two were drawn on top of
     * each other. */
    ch_text(b, 8, 5, _("REGISTRO"), ch_rgb(0xFFE45E));
    snprintf(t, sizeof(t), "%d/%d", n, PIEZAS);
    ch_text(b, 8 + ch_text_w(_("REGISTRO")) + 8, 5, t, ch_rgb(0x8A93AB));

    /* The card: the part you are looking at, in the header. */
    {
        int var = pag * RG_PAG + (g->sel % RG_PAG);
        const ch_part_t *p = &ch_partes[PIEZA_ID(cat, var % PVAR)];
        bool visto = ch_visto(&g->s, PIEZA_ID(cat, var % PVAR));

        if (!visto) {
            ch_text(b, 8, 18, _("SIN DATOS"), ch_rgb(0x606B85));
        } else {
            /* El tipo va en la SEGUNDA linea y no arriba a la derecha: esa
             * esquina es de las flechas de pagina y se pisaban. */
            const char *tn = _(ch_tipo_nombre[p->tipo % TIPOS]);
            ch_text(b, 8, 18, _(p->nombre), ch_rgb(0xFFFFFF));
            snprintf(t, sizeof(t), _("PV%d A%d D%d V%d E%d"),
                     p->vida, p->atk, p->def, p->vel, p->energia);
            ch_text(b, 8, 29, t, ch_rgb(0x8A93AB));
            ch_text(b, CH_W - 8 - ch_text_w(tn), 29, tn,
                    ch_rgb(ch_tipo_color[p->tipo % TIPOS]));
        }
    }
    ch_hline(b, 6, RG_HDR - 1, CH_W - 12, ch_rgb(0x3D465F));
    flechas(g, pag > 0, pag == 0);

    for (int c = 0; c < P_CATS; c++) {
        int x = RG_TX + c * (RG_TW + 2);
        bool sel = (c == cat);
        ch_rect(b, x, RG_TY, RG_TW, RG_TH, sel ? ch_rgb(0x2B3145) : ch_rgb(0x141720));
        ch_frame(b, x, RG_TY, RG_TW, RG_TH, sel ? ch_rgb(0xFFE45E) : ch_rgb(0x3D465F));
        ch_text_center(b, x + RG_TW / 2, RG_TY + 5, _(CATS[c]),
                       sel ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85), ch_rgb(0x05060C));
    }

    for (int k = 0; k < RG_PAG; k++) {
        int i = pag * RG_PAG + k;
        int x = RG_GX + (k % 4) * RG_CW;
        int y = RG_GY + (k / 4) * RG_CH;
        bool visto, puesta;

        if (i >= PVAR) break;
        visto  = ch_visto(&g->s, PIEZA_ID(cat, i));
        puesta = (g->s.yo.pieza[cat] == i);

        ch_rect(b, x + 1, y + 1, RG_CW - 3, RG_CH - 3, ch_rgb(0x0E111A));
        /* EL CENTRO DE LA CELDA, no su pie. ch_part_draw() ya coloca cada
         * categoria para que quede centrada en el punto que se le da -la
         * cabeza arriba, las piernas abajo, cada una con su propio salto-,
         * asi que darle el pie de la celda corre las cuatro hacia abajo, y
         * distinto segun la categoria. Por eso se notaba en cabezas, brazos
         * y piernas y no en los torsos. */
        ch_part_draw(b, cat, i, x + RG_CW / 2 - 1, y + RG_CH / 2, 2,
                     visto ? (int)g->s.yo.skin : -1);
        /* Two different frames and not one: GREEN the one you are wearing,
         * YELLOW the one you are looking at. With a single frame you cannot
         * tell whether the card is talking about the one fitted or the one
         * you touched. */
        if (puesta) ch_frame(b, x, y, RG_CW - 1, RG_CH - 1, ch_rgb(0x4ADE80));
        if (k == (g->sel % RG_PAG)) {
            ch_frame(b, x + 1, y + 1, RG_CW - 3, RG_CH - 3, ch_rgb(0xFFE45E));
        }
    }
}

static void registro_toque(ch_t *g, int bx, int by)
{
    int c;

    if (by >= RG_TY && by < RG_TY + RG_TH) {
        c = (bx - RG_TX) / (RG_TW + 2);
        if (c >= 0 && c < P_CATS) {
            g->sel2 = (uint8_t)c;
            g->sel = 0;
            ch_sfx(1000, 20);
            g->rehacer_fondo = 1;
        }
        return;
    }
    {   /* the page arrows, which is how the other eight variants are got to */
        int f = flecha_en(bx, by);
        if (f) {
            g->scroll = (uint8_t)(f < 0 ? 0 : 1);
            g->sel = 0;
            ch_sfx(1000, 20);
            g->rehacer_fondo = 1;
            return;
        }
    }
    c = rg_celda(bx, by);
    if (c >= 0) {
        g->sel = (uint8_t)c;
        ch_sfx(1100, 20);
        g->rehacer_fondo = 1;
    }
}

/* --------------------------------------------------------------------------
 * The closing screen
 *
 * It appears on beating the champion. It is not a credits list: it is the
 * robot you built, large and in the middle, and the four numbers of what it
 * cost. That is what you want to see after twenty hours, and it is information
 * the game already had stored without ever showing it together.
 * -------------------------------------------------------------------------- */
static void final_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    char t[30];
    int vistas = 0;

    for (int i = 0; i < PIEZAS; i++) if (ch_visto(&g->s, i)) vistas++;

    ch_vgrad(b, 0, 0, CH_W, MAP_H, ch_rgb(0x2B2145), ch_rgb(0x0B0D14));
    for (int i = 0; i < 50; i++) {
        ch_px(b, (i * 61) % CH_W, (i * 37) % MAP_H,
              ch_rgb(((i * 7) & 1) ? 0x8A93AB : 0x3D465F));
    }

    ch_text_center(b, CH_W / 2, 10, _("CAMPEON"), ch_rgb(0xFFE45E),
                   ch_rgb(0x8E4630));
    ch_robot_draw(b, CH_W / 2, 26, &g->s.yo, 2, false, 0, 0);

    ch_text_center(b, CH_W / 2, 112, ch_robot_nombre(&g->s.yo),
                   ch_rgb(0xFFFFFF), ch_rgb(0x05060C));

    ch_panel(b, 8, 124, CH_W - 16, 46, ch_rgb(0x8A93AB));
    snprintf(t, sizeof(t), _("NIVEL %d"), g->s.yo.nivel);
    ch_text(b, 14, 130, t, ch_rgb(0xD5DCEB));
    snprintf(t, sizeof(t), _("COMBATES %d"), g->s.victorias);
    ch_text(b, CH_W - 14 - ch_text_w(t), 130, t, ch_rgb(0xD5DCEB));
    snprintf(t, sizeof(t), _("PIEZAS %d/%d"), vistas, PIEZAS);
    ch_text(b, 14, 142, t, ch_rgb(0xD5DCEB));
    snprintf(t, sizeof(t), _("PASOS %d"), (int)g->s.pasos);
    ch_text(b, CH_W - 14 - ch_text_w(t), 142, t, ch_rgb(0xD5DCEB));
    ch_text_center(b, CH_W / 2, 157, _("NI UNA PIEZA TE LA REGALARON"),
                   ch_rgb(0x8A93AB), ch_rgb(0x05060C));
}

/* --------------------------------------------------------------------------
 * The help
 *
 * Four pages. The game has parts, types, workshop, bag, controls and
 * sub-bosses, and all that is explained today in the grandmother's dialogue:
 * if you skipped it or came back two weeks later, there is nowhere to look it
 * up. A help screen is not documentation, it is the difference between a
 * system and a mystery.
 * -------------------------------------------------------------------------- */

static const char *const AYUDA[] = {
    N_("TU ROBOT SON CUATRO PIEZAS:\n"
       "CABEZA, TORSO, BRAZOS Y\n"
       "PIERNAS.\n"
       "\n"
       "CADA UNA APORTA VIDA,\n"
       "ATAQUE, DEFENSA, VELOCIDAD\n"
       "Y ENERGIA, Y TRAE SUS\n"
       "PROPIOS ATAQUES.\n"
       "\n"
       "EL TORSO ADEMAS DECIDE EL\n"
       "TIPO DE TU ROBOT."),
    N_("AL GANAR UN COMBATE PODES\n"
       "ARRANCARLE UNA PIEZA AL\n"
       "RIVAL.\n"
       "\n"
       "VA A LA MOCHILA Y SE MONTA\n"
       "EN EL MENU, EN TALLER.\n"
       "\n"
       "LA QUE TENIAS PUESTA VUELVE\n"
       "A LA MOCHILA: NUNCA SE\n"
       "PIERDE NADA."),
    N_("HAY SEIS TIPOS Y SE GANAN\n"
       "ENTRE ELLOS:\n"
       "\n"
       "IMPACTO > CRIO Y ACIDO\n"
       "PLASMA  > IMPACTO Y VOLT\n"
       "FUEGO   > CRIO Y ACIDO\n"
       "CRIO    > PLASMA Y VOLT\n"
       "VOLT    > FUEGO E IMPACTO\n"
       "ACIDO   > VOLT Y PLASMA\n"
       "\n"
       "PEGAR CON TU PROPIO TIPO\n"
       "HACE MAS DANO."),
    N_("CADA ZONA TIENE UN DUNGEON\n"
       "Y UN SUBJEFE AL FONDO.\n"
       "\n"
       "VENCERLO TE DA UN PASE DE\n"
       "SECTOR, Y ESE PASE ABRE EL\n"
       "CONTROL DE LA ZONA QUE\n"
       "SIGUE.\n"
       "\n"
       "TOCA DONDE QUERES CAMINAR.\n"
       "EL BOTON DE LA PLACA, O\n"
       "TOCAR TU ROBOT, ABRE ESTE\n"
       "MENU."),
};
#define NAYUDA ((int)(sizeof(AYUDA) / sizeof(AYUDA[0])))

/* --------------------------------------------------------------------------
 * EL DIARIO
 *
 * Los ocho encargos ya existian enteros en los datos: un PNJ con `p3` -la
 * bandera de "traeme esto"- y un cofre en alguna parte cuya `p3` es la misma.
 * Lo unico que faltaba era una pantalla que los junte, porque el juego te
 * contaba el encargo una vez, en un dialogo, y tres pueblos despues no habia
 * forma de acordarse de que buscabas ni donde te lo habian pedido.
 *
 * No lleva tabla nueva: se recorre el mundo. 61 salas por diez entidades es
 * nada, y se hace una vez por fondo, no por cuadro. Y al no haber tabla no
 * puede quedar desincronizada con los encargos de verdad, que es el modo en
 * que estas listas se pudren.
 * -------------------------------------------------------------------------- */
#define DIARIO_MAX  12

static struct { uint8_t sala, item, estado; } s_diario[DIARIO_MAX];
static int s_ndiario;

static void diario_juntar(ch_t *g)
{
    s_ndiario = 0;
    for (int si = 0; si < ch_nsalas && s_ndiario < DIARIO_MAX; si++) {
        const ch_room_t *r = &ch_salas[si];
        for (int i = 0; i < r->nents && s_ndiario < DIARIO_MAX; i++) {
            const ch_ent_t *e = &r->ents[i];
            uint8_t item = 0;

            if (e->tipo != E_PNJ || !e->p3) continue;
            /* que hay que traerle: el cofre cuya bandera es la misma */
            for (int sj = 0; sj < ch_nsalas && !item; sj++) {
                const ch_room_t *q = &ch_salas[sj];
                for (int k = 0; k < q->nents; k++) {
                    if (q->ents[k].tipo == E_COFRE &&
                        q->ents[k].p3 == e->p3) { item = q->ents[k].p1; break; }
                }
            }
            s_diario[s_ndiario].sala = (uint8_t)si;
            s_diario[s_ndiario].item = item;
            s_diario[s_ndiario].estado =
                (e->p2 && ch_flag(&g->s, e->p2)) ? 2
                : (ch_flag(&g->s, e->p3) ? 1 : 0);
            s_ndiario++;
        }
    }
}

static void diario_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    char t[40];
    int abiertos = 0;

    diario_juntar(g);
    for (int i = 0; i < s_ndiario; i++) if (s_diario[i].estado != 2) abiertos++;

    lista_geom(38, 42, 3);
    ch_ui_titulo(g, _("DIARIO"), _("LO QUE TE PIDIERON"));
    snprintf(t, sizeof(t), "%d/%d", abiertos, s_ndiario);
    ch_text(b, 8 + ch_text_w(_("DIARIO")) + 8, 9, t, ch_rgb(0x8A93AB));

    if (!s_ndiario) {
        ch_text_center(b, CH_W / 2, 100, _("NADIE TE PIDIO NADA"),
                       ch_rgb(0x8A93AB), ch_rgb(0x05060C));
        return;
    }
    flechas(g, g->scroll > 0, g->scroll + s_lfilas < s_ndiario);

    for (int i = 0; i < s_lfilas; i++) {
        int k = g->scroll + i;
        int y = s_ly0 + i * s_lfh, h = s_lfh - 4;
        int est;
        uint32_t c;

        if (k >= s_ndiario) break;
        est = s_diario[k].estado;
        c = est == 2 ? 0x4ADE80 : (est == 1 ? 0xFFE45E : 0x8A93AB);

        ch_rect(b, LX, y, LW, h, ch_rgb(est == 2 ? 0x14261C : 0x1A2133));
        ch_frame(b, LX, y, LW, h, ch_rgb(est == 2 ? 0x1E7A3C : 0x3D465F));
        ch_rect(b, LX + 1, y + 1, LW - 2, 1, ch_rgb(0x2C3550));
        ch_ui_icono(b, LX + 6, y + h / 2 - 12,
                    s_diario[k].item < ITEMS && ICONO_ITEM[s_diario[k].item]
                        ? ICONO_ITEM[s_diario[k].item] : IC_HERRAMIENTA, 2);

        ch_text(b, LX + 36, y + 5, _(ch_salas[s_diario[k].sala].nombre),
                ch_rgb(0xFFFFFF));
        if (est == 2) {
            snprintf(t, sizeof(t), "%s", _("ENTREGADO"));
        } else if (est == 1) {
            snprintf(t, sizeof(t), "%s", _("LO TENES: VOLVE"));
        } else if (s_diario[k].item < ITEMS) {
            snprintf(t, sizeof(t), _("TRAE: %s"),
                     _(ch_items[s_diario[k].item].nombre));
        } else {
            snprintf(t, sizeof(t), "%s", _("PENDIENTE"));
        }
        ch_text(b, LX + 36, y + 18, t, ch_rgb(c));
    }
}

static void ayuda_fondo(ch_t *g)
{
    char lin[14][30];
    int n, pag = g->sel % NAYUDA;
    char t[12];

    snprintf(t, sizeof(t), "%d/%d", pag + 1, NAYUDA);
    ch_ui_titulo(g, _("AYUDA"), NULL);
    ch_text(&g->bg, CH_W - 8 - ch_text_w(t), 12, t, ch_rgb(0xFFE45E));

    /* ONCE LINEAS Y LA PISTA EN 156, no catorce y la pista en 164.
     *
     * El mapa termina en 168 y el HUD manda de ahi para abajo: con catorce
     * lineas desde y=42 el texto llegaba a 182 y la pista a 171, o sea que en
     * la placa se leia "TAP TO CONTI". Misma cuenta que en registro y en
     * equipo, tercera vez. */
    n = ch_wrap(_(AYUDA[pag]), 27, lin, 11);
    for (int i = 0; i < n && i < 11; i++) {
        ch_text(&g->bg, 8, 42 + i * 10, lin[i], ch_rgb(0xD5DCEB));
    }
    ch_hline(&g->bg, 8, 152, CH_W - 16, ch_rgb(0x232B41));
    ch_text_center(&g->bg, CH_W / 2, 157, _("TOCA PARA SEGUIR"),
                   ch_rgb(0x606B85), ch_rgb(0x05060C));
}

/* --------------------------------------------------------------------------
 * The world map
 *
 * Eight nodes in a chain, which is the world's real shape: you go west to east
 * and there are no shortcuts. It marks where you are and which sub-bosses you
 * have felled.
 *
 * In 51 rooms you get lost, and the game had NO way of answering "where am I"
 * or "how much is left". This screen is cheap and both answers come out of two
 * fields of a table.
 * -------------------------------------------------------------------------- */

/* EL MAPA ES OCHO BOTONES, no ocho renglones.
 *
 * Era una lista de 15 px por zona que no se podia tocar y no hacia falta que
 * se pudiera, porque no hacia nada. Ahora cada zona es una ficha de 30 px con
 * su icono, su estado y -si ya estuviste- el viaje. 128 px de alto para ocho
 * fichas no entran, asi que van de a cuatro y se pasa de pagina. */
#define MM_Y0    38
#define MM_FH    31
#define MM_FILAS  4
#define MM_H     28

/* VIAJE RAPIDO. Solo a una zona en la que ya estuviste, y siempre a su
 * pueblo: aterrizar en una mazmorra a la que entraste una vez seria una
 * trampa, y el pueblo es donde estan el taller y la tienda, que es para lo
 * que se vuelve. */
static void mapa_toque(ch_t *g, int bx, int by)
{
    int pag = g->scroll ? 1 : 0;
    int f = flecha_en(bx, by);

    if (f) {
        g->scroll = (uint8_t)(f < 0 ? 0 : 1);
        ch_sfx(1000, 20);
        g->rehacer_fondo = 1;
        return;
    }
    if (by < MM_Y0 || bx < 6 || bx >= CH_W - 6) {
        g->modo = MODO_MENU;
        g->rehacer_fondo = 1;
        return;
    }
    {
        int i = (by - MM_Y0) / MM_FH;
        int z = pag * MM_FILAS + i;
        const ch_zona_t *zo;

        if (i < 0 || i >= MM_FILAS || z >= ZONAS) return;
        if ((by - MM_Y0) % MM_FH >= MM_H) return;       /* el hueco entre fichas */
        zo = &ch_zonas_tab[z];

        if (!ch_flag(&g->s, zo->visita)) {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("TODAVIA NO ESTUVISTE AHI"));
            return;
        }
        if (g->s.sala >= zo->sala0 && g->s.sala <= zo->sala1) {
            ch_sfx(220, 40);
            ch_ui_aviso(g, _("YA ESTAS AHI"));
            return;
        }
        ch_sfx(1500, 90);
        ch_ui_aviso(g, _("VIAJANDO..."));
        g->sel = g->sel2 = g->scroll = 0;
        g->modo = MODO_MAPA;
        ch_map_entrar(g, zo->casa, zo->casa_x, zo->casa_y);
        g->quiere_guardar = 1;
        g->rehacer_fondo = 1;
    }
}

static void mapa_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int aqui = 0, hechas = 0, pag = g->scroll ? 1 : 0;
    char t[24];

    for (int z = 0; z < ZONAS; z++) {
        if (g->s.sala >= ch_zonas_tab[z].sala0 &&
            g->s.sala <= ch_zonas_tab[z].sala1) aqui = z;
        if (ch_flag(&g->s, ch_zonas_tab[z].bandera)) hechas++;
    }

    snprintf(t, sizeof(t), "%d/%d", hechas, ZONAS);
    ch_ui_titulo(g, _("MAPA"), _("TOCA PARA VIAJAR"));
    ch_text(b, 8 + ch_text_w(_("MAPA")) + 8, 9, t, ch_rgb(0x8A93AB));
    flechas(g, pag > 0, pag == 0);

    for (int i = 0; i < MM_FILAS; i++) {
        int z = pag * MM_FILAS + i;
        int y = MM_Y0 + i * MM_FH;
        bool ok, yo, visto;
        uint16_t marco;

        if (z >= ZONAS) break;
        ok    = ch_flag(&g->s, ch_zonas_tab[z].bandera);
        yo    = (z == aqui);
        visto = ch_flag(&g->s, ch_zonas_tab[z].visita);
        marco = yo ? ch_rgb(0xFFE45E)
                   : (visto ? ch_rgb(0x3D465F) : ch_rgb(0x232B41));

        ch_round(b, 6, y, CH_W - 12, MM_H, 3,
                 ch_rgb(yo ? 0x2A3350 : (visto ? 0x161C2E : 0x101320)));
        ch_frame(b, 6, y, CH_W - 12, MM_H, marco);

        /* El numero de zona, en su propia casilla: es el orden del mundo. */
        ch_rect(b, 10, y + 4, 20, MM_H - 8,
                ch_rgb(ok ? 0x1E7A3C : (visto ? 0x232B41 : 0x171B29)));
        ch_frame(b, 10, y + 4, 20, MM_H - 8, ch_rgb(0x3D465F));
        snprintf(t, sizeof(t), "%d", z + 1);
        ch_text_center(b, 20, y + MM_H / 2 - 3, t,
                       ch_rgb(visto ? 0xFFFFFF : 0x545C70), ch_rgb(0x05060C));

        ch_text(b, 36, y + 5, _(ch_zonas_tab[z].nombre),
                ch_rgb(visto ? (yo ? 0xFFFFFF : 0xD5DCEB) : 0x545C70));

        {
            const char *e = !visto ? _("SIN EXPLORAR")
                          : yo      ? _("ESTAS AQUI")
                          : ok      ? _("VENCIDA - VIAJAR")
                                    : _("VIAJAR");
            ch_text(b, 36, y + 16, e,
                    ch_rgb(!visto ? 0x545C70 : yo ? 0xFFE45E
                                  : ok ? 0x4ADE80 : 0x8A93AB));
        }
        /* La flecha de viaje: solo donde se puede viajar. Un boton que no
         * hace nada es peor que no tener boton. */
        if (visto && !yo) {
            for (int k = 0; k < 5; k++) {
                ch_rect(b, CH_W - 22 + k, y + MM_H / 2 - 5 + k, 1,
                        11 - k * 2, ch_rgb(0x4ADE80));
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Title screen
 * -------------------------------------------------------------------------- */

#define TIT_BX   34
#define TIT_BW  116
#define TIT_BH   24

static void titulo_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    ch_robot_t demo;
    uint32_t semilla = 0xC0FFEEu;

    ch_vgrad(b, 0, 0, CH_W, 140, ch_rgb(0x0B0D14), ch_rgb(0x2B2145));
    ch_vgrad(b, 0, 140, CH_W, CH_H, ch_rgb(0x2B2145), ch_rgb(0x120E1E));

    /* fixed stars: they come from an implicit table (the coordinate) and not
     * from a roll, so the background can be repainted identically */
    for (int i = 0; i < 60; i++) {
        int x = (i * 61) % CH_W, y = (i * 37) % 130;
        ch_px(b, x, y, ch_rgb(((i * 7) & 1) ? 0x8A93AB : 0x3D465F));
    }

    ch_robot_random(&demo, &semilla, 20, 6);
    demo.skin = 2;
    ch_robot_draw(b, CH_W / 2, 34, &demo, 2, false, 0, 0);

    ch_text_center(b, CH_W / 2, 122, "C H A T A R R A", ch_rgb(0xFFE45E),
                   ch_rgb(0x8E4630));
    ch_text_center(b, CH_W / 2, 134, _("RPG DE ROBOTS"), ch_rgb(0x8A93AB),
                   ch_rgb(0x05060C));

    ch_snd_melodia(g, CH_MEL_TITULO);
    ch_panel(b, TIT_BX, 148, TIT_BW, TIT_BH, ch_rgb(0x8A93AB));
    ch_text_center(b, CH_W / 2, 156, _("EMPEZAR"), ch_rgb(0xFFFFFF), ch_rgb(0x05060C));

    ch_rect(b, 0, HUD_Y, CH_W, HUD_H, ch_rgb(0x0B0D14));
    /* Three lines of at most 30 characters: at 6 px per character, more than
     * that runs off the buffer's 184 and is clipped on the board. */
    ch_text_center(b, CH_W / 2, HUD_Y + 8, _("TOCA DONDE QUERES CAMINAR"),
                   ch_rgb(0x8A93AB), ch_rgb(0x05060C));
    ch_text_center(b, CH_W / 2, HUD_Y + 20, _("EL BOTON DE LA PLACA: MENU"),
                   ch_rgb(0x606B85), ch_rgb(0x05060C));
    ch_text_center(b, CH_W / 2, HUD_Y + 32, _("O TOCA TU PROPIO ROBOT"),
                   ch_rgb(0x606B85), ch_rgb(0x05060C));
}

/* --------------------------------------------------------------------------
 * The dispatcher
 * -------------------------------------------------------------------------- */

void ch_ui_fondo(ch_t *g)
{
    switch (g->modo) {
    case MODO_TITULO:  titulo_fondo(g);  break;
    case MODO_MENU:    menu_fondo(g);    break;
    case MODO_TALLER:  taller_fondo(g);  break;
    case MODO_OBJETOS: objetos_fondo(g); break;
    case MODO_FICHA:   ficha_fondo(g);   break;
    case MODO_TIENDA:  tienda_fondo(g);  break;
    case MODO_REGISTRO: registro_fondo(g); break;
    case MODO_MAPAMUNDI: mapa_fondo(g); break;
    case MODO_DIARIO:  diario_fondo(g); break;
    case MODO_FERIA:   ch_fe_fondo(g);  break;
    case MODO_AYUDA:   ayuda_fondo(g); break;
    case MODO_FINAL:   final_fondo(g); break;
    case MODO_CABINA:  ch_lk_fondo(g); break;
    case MODO_DIALOGO: ch_map_fondo(g); dlg_fondo(g); break;
    default:           ch_map_fondo(g);  break;
    }
}

void ch_ui_dibujar(ch_t *g)
{
    /* In the menus nothing moves: the background IS the screen already. Only
     * the map and the dialogue have things on top. */
    if (g->modo == MODO_MAPA || g->modo == MODO_DIALOGO) {
        ch_map_dibujar(g);
    }
    if (g->modo == MODO_DIALOGO) {
        if (g->dlg_chars < 250) g->dlg_chars = (uint8_t)(g->dlg_chars + 2);
        dlg_texto(g);
    }
    if (g->modo == MODO_CABINA) ch_lk_dibujar(g);
}

/* --------------------------------------------------------------------------
 * The touch
 * -------------------------------------------------------------------------- */

void ch_ui_toque(ch_t *g, int bx, int by)
{
    switch (g->modo) {
    case MODO_TITULO:
        if (by >= 148 && by < 148 + TIT_BH && bx >= TIT_BX && bx < TIT_BX + TIT_BW) {
            ch_sfx(1200, 60);
            /* Sin parar nada: ch_map_entrar() pone el tema de la zona, y
             * pararlo aca dejaba un silencio de un cuadro en el medio. */
            g->modo = MODO_MAPA;
            g->mel_mapa = 0;
            ch_map_entrar(g, g->s.sala, g->s.x, g->s.y);
        }
        break;

    case MODO_DIALOGO:
        dlg_avanzar(g);
        break;

    case MODO_MENU: {
        int n, cols;
        const char *tit;
        const baldosa_t *p = pagina(g, &n, &cols, &tit);
        int f = baldosa_en(g, bx, by);

        if (f < 0) return;
        ch_sfx(1000, 25);
        g->sel = 0;
        g->scroll = 0;
        switch (p[f].accion) {
        case AC_PAG1:    g->sel2 = 1; break;
        case AC_PAG2:    g->sel2 = 2; break;
        case AC_TALLER:  g->modo = MODO_TALLER;  g->sel2 = 0;
                         g->sel = 0; g->scroll = 0; s_ta_bot = 0; break;
        case AC_OBJETOS: g->modo = MODO_OBJETOS; g->sel2 = 0; break;
        case AC_EQUIPO:  g->modo = MODO_FICHA;   g->sel2 = 0; break;
        case AC_REGISTRO:g->modo = MODO_REGISTRO;g->sel2 = 0; break;
        case AC_MAPA:    g->modo = MODO_MAPAMUNDI; g->sel2 = 0; break;
        case AC_DIARIO:  g->modo = MODO_DIARIO;  g->sel2 = 0;
                         g->scroll = 0; break;
        case AC_AYUDA:   g->modo = MODO_AYUDA;   g->sel2 = 0; break;
        case AC_SONIDO:  ch_sonido_set((ch_sonido_get() + 1) % 3); break;
        case AC_GUARDAR: g->quiere_guardar = 1;
                         ch_ui_aviso(g, _("PARTIDA GUARDADA")); break;
        case AC_CERRAR:  g->modo = MODO_MAPA;    g->sel2 = 0; break;
        default: break;
        }
        g->rehacer_fondo = 1;
        break;
    }

    case MODO_FINAL:
        g->modo = MODO_MAPA;
        g->mel_mapa = 0;
        ch_map_musica(g);
        g->rehacer_fondo = 1;
        break;

    case MODO_TALLER:  taller_toque(g, bx, by);  break;
    case MODO_OBJETOS: objetos_toque(g, bx, by); break;
    case MODO_TIENDA:  tienda_toque(g, bx, by);  break;
    case MODO_REGISTRO: registro_toque(g, bx, by); break;
    case MODO_FICHA:   ficha_toque(g, bx, by); break;
    case MODO_MAPAMUNDI: mapa_toque(g, bx, by); break;
    case MODO_DIARIO: {
        int f = flecha_en(bx, by);
        if (f) {
            int n = (int)g->scroll + f;
            if (n >= 0 && n + s_lfilas <= s_ndiario) {
                g->scroll = (uint8_t)n;
                g->rehacer_fondo = 1;
            }
            return;
        }
        g->modo = MODO_MENU;
        g->sel2 = 1;
        g->rehacer_fondo = 1;
        break;
    }
    case MODO_AYUDA:
        if (++g->sel >= NAYUDA) { g->sel = 0; g->modo = MODO_MENU; }
        ch_sfx(1000, 20);
        g->rehacer_fondo = 1;
        break;

    case MODO_COMBATE: ch_bt_toque(g, bx, by);   break;
    case MODO_CABINA:  ch_lk_toque(g, bx, by);   break;
    case MODO_FERIA:   ch_fe_toque(g, bx, by);  break;

    default:
        if (by < MAP_H) ch_map_toque(g, bx, by);
        break;
    }
}

/* The back gesture. Returns true if the app consumed it. */
bool ch_ui_atras(ch_t *g)
{
    switch (g->modo) {
    case MODO_TALLER:
        if (g->sel2) { g->sel2 = 0; g->sel = 0; g->scroll = 0;
                       g->rehacer_fondo = 1; return true; }
        g->sel = 0;
        g->modo = MODO_MENU;
        g->rehacer_fondo = 1;
        return true;
    case MODO_OBJETOS:
    case MODO_FICHA:
    case MODO_REGISTRO:
    case MODO_MAPAMUNDI:
    case MODO_DIARIO:
    case MODO_AYUDA:
        g->sel = 0;
        g->modo = MODO_MENU;
        g->rehacer_fondo = 1;
        return true;
    case MODO_MENU:
        /* The back gesture climbs ONE step: from a page to the root, and from
         * the root out to the map. Anything else and the two pages would be a
         * trap you can only leave by picking something. */
        if (g->sel2) { g->sel2 = 0; g->rehacer_fondo = 1; return true; }
        g->modo = MODO_MAPA;
        g->rehacer_fondo = 1;
        return true;
    case MODO_TIENDA:
        g->modo = MODO_MAPA;
        g->rehacer_fondo = 1;
        return true;
    case MODO_DIALOGO:
        dlg_avanzar(g);
        return true;
    case MODO_COMBATE:
        return ch_bt_atras(g);
    case MODO_CABINA:
        return ch_lk_atras(g);
    default:
        return false;
    }
}
