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

void ch_ui_hud(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    const ch_robot_t *yo = &g->s.yo;
    char t[32];

    ch_rect(b, 0, HUD_Y, CH_W, HUD_H, ch_rgb(0x0B0D14));
    ch_hline(b, 0, HUD_Y, CH_W, ch_rgb(0x3D465F));

    /* first line: where you are and how much you have */
    ch_text(b, 4, HUD_Y + 4, g->aviso_t ? g->aviso : _(r->nombre),
            g->aviso_t ? ch_rgb(0xFFE45E) : ch_rgb(0xD5DCEB));
    snprintf(t, sizeof(t), _("%dC"), g->s.creditos);
    ch_text(b, CH_W - 4 - ch_text_w(t), HUD_Y + 4, t, ch_rgb(0xFFE45E));

    /* the robot */
    snprintf(t, sizeof(t), _("%s N%d"), ch_robot_nombre(yo), yo->nivel);
    ch_text(b, 4, HUD_Y + 15, t, ch_rgb(0xFFFFFF));

    ch_text(b, 4, HUD_Y + 26, _("VID"), ch_rgb(0x8A93AB));
    ch_barra(b, 24, HUD_Y + 25, 96, yo->vida, yo->vida_max, ch_rgb(0x4ADE80));
    snprintf(t, sizeof(t), "%d/%d", yo->vida, yo->vida_max);
    ch_text(b, 124, HUD_Y + 26, t, ch_rgb(0xD5DCEB));

    ch_text(b, 4, HUD_Y + 36, _("ENE"), ch_rgb(0x8A93AB));
    ch_barra(b, 24, HUD_Y + 35, 96, yo->ene, yo->ene_max, ch_rgb(0x4A9DF5));
    {
        uint32_t base = ch_exp_nivel(yo->nivel);
        uint32_t sig  = ch_exp_nivel(yo->nivel + 1);
        uint32_t hoy  = yo->exp > base ? yo->exp - base : 0;
        uint32_t tot  = sig > base ? sig - base : 1;
        ch_text(b, 124, HUD_Y + 36, _("EXP"), ch_rgb(0x8A93AB));
        ch_barra(b, 144, HUD_Y + 35, 36, (int)hoy, (int)tot, ch_rgb(0xBF5AF2));
    }

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

static void titulo(ch_t *g, const char *txt, const char *sub)
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

static const char *const MENU[] = {
    N_("TALLER"), N_("OBJETOS"), N_("FICHA DEL ROBOT"), N_("REGISTRO"),
    N_("MAPA"), N_("AYUDA"), N_("SONIDO"), N_("GUARDAR PARTIDA"), N_("CERRAR"),
};
static const char *const SONIDOS[3] = { N_("MUDO"), N_("EFECTOS"), N_("TODO") };
#define NMENU ((int)(sizeof(MENU) / sizeof(MENU[0])))

void ch_ui_menu(ch_t *g)
{
    g->modo_prev = g->modo;
    g->modo = MODO_MENU;
    g->sel = 0;
    g->scroll = 0;
    g->rehacer_fondo = 1;
    ch_sfx(900, 25);
}

static void menu_fondo(ch_t *g)
{
    char t[24];

    lista_geom(28, 16, NMENU);
    /* Without the credits line: with eight rows the list starts at y=40 and
     * covered it. The HUD already shows them at the bottom right, always. */
    titulo(g, _("MENU"), NULL);
    (void)t;
    for (int i = 0; i < NMENU; i++) {
        {
            fila(g, i, _(MENU[i]),
                 i == 6 ? _(SONIDOS[ch_sonido_get() % 3]) : NULL, true);
        }
    }
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

static void taller_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    char t[30], d[16];

    lista_geom(62, 21, 5);
    titulo(g, _("TALLER"), _("TOCA PARA MONTAR"));

    /* At the top, either what you are wearing or -if you touched a part- WHAT
     * THE WHOLE ROBOT WOULD LOOK LIKE with it. It is the only question that
     * matters in front of the bag, and until now you had to fit it to answer
     * it. */
    if (g->sel2) {
        ch_robot_t prueba = g->s.yo;
        uint8_t id = g->s.piezas[(g->sel2 - 1) % MOCHILA];
        char t2[30];

        prueba.pieza[PIEZA_CAT(id)] = PIEZA_VAR(id);
        ch_robot_stats(&prueba);
        ch_text(b, 6, 42, _(ch_partes[id].nombre), ch_rgb(0xFFE45E));
        snprintf(t2, sizeof(t2), _("PV%d A%d D%d V%d"),
                 prueba.vida_max, prueba.atk, prueba.def, prueba.vel);
        ch_text(b, 6, 52, t2, ch_rgb(0xD5DCEB));
        snprintf(t2, sizeof(t2), _("AHORA PV%d A%d D%d V%d"),
                 g->s.yo.vida_max, g->s.yo.atk, g->s.yo.def, g->s.yo.vel);
        ch_text(b, CH_W - 6 - ch_text_w(t2), 42, t2, ch_rgb(0x606B85));
        ch_text(b, CH_W - 6 - ch_text_w(_("TOCA OTRA VEZ PARA MONTAR")), 52,
                _("TOCA OTRA VEZ PARA MONTAR"), ch_rgb(0x8A93AB));
    } else {
        for (int c = 0; c < P_CATS; c++) {
            const ch_part_t *p = &ch_partes[PIEZA_ID(c, g->s.yo.pieza[c])];
            int x = 6 + (c & 1) * 90;
            int y = 42 + (c >> 1) * 10;
            ch_text(b, x, y, _(CATS[c]), ch_rgb(0x606B85));
            ch_text(b, x + 20, y, _(p->nombre), ch_rgb(0xD5DCEB));
        }
    }

    int n = 0;
    for (int i = 0; i < MOCHILA; i++) if (g->s.piezas[i] != 0xFF) n++;

    if (!n) {
        ch_text_center(b, CH_W / 2, 100, _("LA MOCHILA ESTA VACIA."),
                       ch_rgb(0x8A93AB), ch_rgb(0x05060C));
        ch_text_center(b, CH_W / 2, 112, _("GANA COMBATES PARA"),
                       ch_rgb(0x606B85), ch_rgb(0x05060C));
        ch_text_center(b, CH_W / 2, 122, _("ARRANCAR PIEZAS."),
                       ch_rgb(0x606B85), ch_rgb(0x05060C));
        return;
    }

    flechas(g, g->scroll > 0, g->scroll + s_lfilas < MOCHILA);

    for (int i = 0; i < s_lfilas; i++) {
        int k = g->scroll + i;
        if (k >= MOCHILA) break;
        uint8_t id = g->s.piezas[k];
        if (id == 0xFF) { fila(g, i, "-", NULL, false); continue; }
        const ch_part_t *p = &ch_partes[id];
        int cat = PIEZA_CAT(id);
        const ch_part_t *puesta = &ch_partes[PIEZA_ID(cat, g->s.yo.pieza[cat])];

        snprintf(t, sizeof(t), "%s %s", _(CATS[cat]), _(p->nombre));
        fila(g, i, t, NULL, true);

        /* THE DIFFERENCE, NOT THE BARE NUMBER.
         *
         * The row used to say "A24 D9 V6" and that does not answer the one
         * question you have in front of the bag: whether it is better or worse
         * than the one you are wearing. With the sign and the colour it is
         * answered at a glance, and without opening another screen. */
        {
            static const char *const LET[3] = { "A", "D", "V" };
            int dif[3] = { p->atk - puesta->atk,
                           p->def - puesta->def,
                           p->vel - puesta->vel };
            int x = LX + LW - 5;
            for (int k = 2; k >= 0; k--) {
                snprintf(d, sizeof(d), "%s%+d", LET[k], dif[k]);
                x -= ch_text_w(d) + 4;
                ch_text(&g->bg, x, s_ly0 + i * s_lfh + 5, d,
                        dif[k] > 0 ? ch_rgb(0x4ADE80)
                                   : (dif[k] < 0 ? ch_rgb(0xFF4A3D)
                                                 : ch_rgb(0x606B85)));
            }
        }
    }
}

static void taller_toque(ch_t *g, int bx, int by)
{
    int f = flecha_en(bx, by);
    if (f) {
        int s = (int)g->scroll + f;
        if (s >= 0 && s + s_lfilas <= MOCHILA) { g->scroll = (uint8_t)s; g->rehacer_fondo = 1; }
        return;
    }
    f = fila_en(bx, by);
    if (f < 0) return;
    int k = g->scroll + f;
    if (k >= MOCHILA || g->s.piezas[k] == 0xFF) return;

    /* First tap: show what it would look like. Second: fit it. Fitting
     * straight away made it easy to get the wrong row and change the robot by
     * accident. */
    if (g->sel2 != (uint8_t)(k + 1)) {
        g->sel2 = (uint8_t)(k + 1);
        ch_sfx(900, 20);
        g->rehacer_fondo = 1;
        return;
    }
    g->sel2 = 0;

    uint8_t id = g->s.piezas[k];
    int cat = PIEZA_CAT(id);
    uint8_t antes = PIEZA_ID(cat, g->s.yo.pieza[cat]);

    g->s.yo.pieza[cat] = PIEZA_VAR(id);
    g->s.piezas[k] = antes;
    ch_ver(&g->s, id);
    ch_ver(&g->s, antes);
    ch_robot_stats(&g->s.yo);
    if (g->s.yo.vida > g->s.yo.vida_max) g->s.yo.vida = g->s.yo.vida_max;

    ch_sfx(1200, 40);
    ch_ui_aviso(g, _(ch_partes[id].nombre));
    g->rehacer_fondo = 1;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * Items
 * -------------------------------------------------------------------------- */

static uint8_t s_lista[ITEMS];
static int     s_nlista;

static void objetos_fondo(ch_t *g)
{
    char t[30], d[12];

    lista_geom(46, 21, 6);
    titulo(g, _("OBJETOS"), _("TOCA UNO PARA USARLO"));

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
        const ch_item_t *it = &ch_items[s_lista[k]];
        snprintf(t, sizeof(t), "%s", _(it->nombre));
        snprintf(d, sizeof(d), "x%d", g->s.obj[s_lista[k]]);
        fila(g, i, t, d, true);
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

static void ficha_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    const ch_robot_t *r = &g->s.yo;
    char t[30];

    snprintf(t, sizeof(t), _("NIVEL %d   TIPO %s"), r->nivel,
             _(ch_tipo_nombre[r->tipo % TIPOS]));
    titulo(g, ch_robot_nombre(r), t);

    /* The robot at scale 2, which is how it looks in combat: the card exists
     * precisely to look at what you built. At scale 1 it was the size of a
     * postage stamp and you could not tell one part from another. */
    ch_robot_draw(b, 38, 42, r, 2, false, 0);

    int y = 44;
    snprintf(t, sizeof(t), _("VIDA %d/%d"), r->vida, r->vida_max);
    ch_text(b, 76, y, t, ch_rgb(0x4ADE80));  y += 12;
    snprintf(t, sizeof(t), _("ENER %d/%d"), r->ene, r->ene_max);
    ch_text(b, 76, y, t, ch_rgb(0x4A9DF5));  y += 12;
    snprintf(t, sizeof(t), _("ATAQUE   %d"), r->atk);
    ch_text(b, 76, y, t, ch_rgb(0xD5DCEB));  y += 12;
    snprintf(t, sizeof(t), _("DEFENSA  %d"), r->def);
    ch_text(b, 76, y, t, ch_rgb(0xD5DCEB));  y += 12;
    snprintf(t, sizeof(t), _("VELOCID. %d"), r->vel);
    ch_text(b, 76, y, t, ch_rgb(0xD5DCEB));

    /* The game's statistics. A long RPG needs to be able to answer "how long
     * have I been playing": they are four numbers that were already stored and
     * that until now were shown nowhere. */
    {
        int vistas = 0;
        for (int i = 0; i < PIEZAS; i++) if (ch_visto(&g->s, i)) vistas++;
        snprintf(t, sizeof(t), _("COMBATES %d"), g->s.victorias);
        ch_text(b, 8, 106, t, ch_rgb(0x8A93AB));
        snprintf(t, sizeof(t), _("PIEZAS %d/%d"), vistas, PIEZAS);
        ch_text(b, CH_W - 8 - ch_text_w(t), 106, t, ch_rgb(0x8A93AB));
        snprintf(t, sizeof(t), _("PASOS %d"), (int)g->s.pasos);
        ch_text(b, 8, 116, t, ch_rgb(0x606B85));
        snprintf(t, sizeof(t), _("EXP %d"), (int)r->exp);
        ch_text(b, CH_W - 8 - ch_text_w(t), 116, t, ch_rgb(0x606B85));
    }

    for (int i = 0; i < r->nmov; i++) {
        const ch_move_t *m = &ch_moves[r->mov[i] % MOVES];
        int fy = 130 + i * 11;
        ch_rect(b, 6, fy - 2, CH_W - 12, 10, ch_rgb(0x171B29));
        ch_text(b, 10, fy, _(m->nombre), ch_rgb(0xFFFFFF));
        snprintf(t, sizeof(t), "%s", _(ch_tipo_nombre[m->tipo % TIPOS]));
        ch_text(b, 104, fy, t, ch_rgb(ch_tipo_color[m->tipo % TIPOS]));
        snprintf(t, sizeof(t), "%d/%d", m->poder, m->costo);
        ch_text(b, CH_W - 10 - ch_text_w(t), fy, t, ch_rgb(0x8A93AB));
    }
}

/* --------------------------------------------------------------------------
 * Shop
 * -------------------------------------------------------------------------- */

static const uint8_t SURTIDO[] = {
    IT_ACEITE, IT_ACEITE2, IT_BATERIA, IT_BATERIA2, IT_SOLDADOR, IT_IMAN,
};
#define NSURTIDO ((int)(sizeof(SURTIDO) / sizeof(SURTIDO[0])))

static void tienda_fondo(ch_t *g)
{
    char t[30], d[16];

    lista_geom(46, 21, 6);
    titulo(g, _("TIENDA"), NULL);
    snprintf(t, sizeof(t), _("TENES %d CREDITOS"), g->s.creditos);
    ch_text(&g->bg, 8, 46, t, ch_rgb(0xFFE45E));

    for (int i = 0; i < s_lfilas && i < NSURTIDO; i++) {
        const ch_item_t *it = &ch_items[SURTIDO[i]];
        snprintf(t, sizeof(t), "%s", _(it->nombre));
        snprintf(d, sizeof(d), _("%dC (x%d)"), it->precio, g->s.obj[SURTIDO[i]]);
        fila(g, i, t, d, g->s.creditos >= it->precio);
    }

    /* And what the thing you are looking at does. Buying blind by the name is
     * what makes nobody buy anything but oil. */
    {
        const ch_item_t *it = &ch_items[SURTIDO[g->sel % NSURTIDO]];
        char lin[2][30];
        int n = ch_wrap(_(it->desc), 27, lin, 2);
        ch_panel(&g->bg, 4, 174 - 26, CH_W - 8, 24, ch_rgb(0x3D465F));
        for (int i = 0; i < n && i < 2; i++) {
            ch_text(&g->bg, 9, 174 - 22 + i * 10, lin[i], ch_rgb(0xD5DCEB));
        }
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

#define RG_TX     6                     /* tabs                              */
#define RG_TY    26
#define RG_TW    43
#define RG_TH    18
#define RG_GX     8                     /* grid                              */
#define RG_GY    46
#define RG_CW    42
#define RG_CH    26                     /* 4 rows x 26 = 104: 46..150        */
#define RG_INFO 152                     /* the card, below the grid          */

static int rg_celda(int bx, int by)
{
    int c, f;
    if (bx < RG_GX || by < RG_GY) return -1;
    c = (bx - RG_GX) / RG_CW;
    f = (by - RG_GY) / RG_CH;
    if (c > 3 || f > 3) return -1;
    return f * 4 + c;
}

static void registro_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int cat = g->sel2 % P_CATS;
    int n = 0;
    char t[34];

    for (int i = 0; i < PIEZAS; i++) if (ch_visto(&g->s, i)) n++;
    snprintf(t, sizeof(t), "%d/%d", n, PIEZAS);
    titulo(g, _("REGISTRO"), NULL);
    ch_text(b, CH_W - 8 - ch_text_w(t), 9, t, ch_rgb(0xFFE45E));

    for (int c = 0; c < P_CATS; c++) {
        int x = RG_TX + c * (RG_TW + 2);
        bool sel = (c == cat);
        ch_rect(b, x, RG_TY, RG_TW, RG_TH, sel ? ch_rgb(0x2B3145) : ch_rgb(0x141720));
        ch_frame(b, x, RG_TY, RG_TW, RG_TH, sel ? ch_rgb(0xFFE45E) : ch_rgb(0x3D465F));
        ch_text_center(b, x + RG_TW / 2, RG_TY + 6, _(CATS[c]),
                       sel ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85), ch_rgb(0x05060C));
    }

    for (int i = 0; i < PVAR; i++) {
        int x = RG_GX + (i % 4) * RG_CW;
        int y = RG_GY + (i / 4) * RG_CH;
        bool visto = ch_visto(&g->s, PIEZA_ID(cat, i));
        bool puesta = (g->s.yo.pieza[cat] == i);

        ch_rect(b, x + 1, y + 1, RG_CW - 3, RG_CH - 3, ch_rgb(0x0E111A));
        ch_part_draw(b, cat, i, x + RG_CW / 2 - 1, y + RG_CH / 2 + 2, 1,
                     visto ? (int)g->s.yo.skin : -1);
        /* Two different frames and not one: GREEN the one you are wearing,
         * YELLOW the one you are looking at. With a single frame you cannot
         * tell whether the card below is talking about the one fitted or the
         * one you touched. */
        if (puesta) ch_frame(b, x, y, RG_CW - 1, RG_CH - 1, ch_rgb(0x4ADE80));
        if (i == (g->sel % PVAR)) {
            ch_frame(b, x + 1, y + 1, RG_CW - 3, RG_CH - 3, ch_rgb(0xFFE45E));
        }
    }

    /* The selected one's card. 'sel' stores which, within the category. */
    {
        int var = g->sel % PVAR;
        const ch_part_t *p = &ch_partes[PIEZA_ID(cat, var)];
        bool visto = ch_visto(&g->s, PIEZA_ID(cat, var));

        ch_panel(b, 4, RG_INFO, CH_W - 8, 22, ch_rgb(0x8A93AB));
        if (!visto) {
            ch_text_center(b, CH_W / 2, RG_INFO + 7, _("SIN DATOS"),
                           ch_rgb(0x606B85), ch_rgb(0x05060C));
        } else {
            ch_text(b, 9, RG_INFO + 3, _(p->nombre), ch_rgb(0xFFFFFF));
            ch_text(b, CH_W - 9 - ch_text_w(_(ch_tipo_nombre[p->tipo % TIPOS])),
                    RG_INFO + 3, _(ch_tipo_nombre[p->tipo % TIPOS]),
                    ch_rgb(ch_tipo_color[p->tipo % TIPOS]));
            /* PV and not V: V is already speed in the workshop, and two
             * identical letters with two meanings on the same line cannot be
             * read. */
            snprintf(t, sizeof(t), _("PV%d A%d D%d V%d E%d"),
                     p->vida, p->atk, p->def, p->vel, p->energia);
            ch_text(b, 9, RG_INFO + 13, t, ch_rgb(0xD5DCEB));
            snprintf(t, sizeof(t), _("Z%d"), p->nivel);
            ch_text(b, CH_W - 9 - ch_text_w(t), RG_INFO + 13, t, ch_rgb(0x8A93AB));
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
    ch_robot_draw(b, CH_W / 2, 26, &g->s.yo, 2, false, 0);

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

static void ayuda_fondo(ch_t *g)
{
    char lin[14][30];
    int n, pag = g->sel % NAYUDA;
    char t[12];

    snprintf(t, sizeof(t), "%d/%d", pag + 1, NAYUDA);
    titulo(g, _("AYUDA"), NULL);
    ch_text(&g->bg, CH_W - 8 - ch_text_w(t), 12, t, ch_rgb(0xFFE45E));

    n = ch_wrap(_(AYUDA[pag]), 27, lin, 14);
    for (int i = 0; i < n; i++) {
        ch_text(&g->bg, 8, 42 + i * 10, lin[i], ch_rgb(0xD5DCEB));
    }
    ch_text_center(&g->bg, CH_W / 2, 164, _("TOCA PARA SEGUIR"),
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

#define MM_Y0    40
#define MM_FH    15

static void mapa_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int aqui = 0, hechas = 0;
    char t[8];

    for (int z = 0; z < ZONAS; z++) {
        if (g->s.sala >= ch_zonas_tab[z].sala0 &&
            g->s.sala <= ch_zonas_tab[z].sala1) aqui = z;
        if (ch_flag(&g->s, ch_zonas_tab[z].bandera)) hechas++;
    }

    snprintf(t, sizeof(t), "%d/%d", hechas, ZONAS);
    titulo(g, _("MAPA"), NULL);
    ch_text(b, CH_W - 8 - ch_text_w(t), 9, t, ch_rgb(0xFFE45E));

    /* The line joining them: it is what says the world is a chain. */
    ch_vline(b, 18, MM_Y0 + 6, (ZONAS - 1) * MM_FH, ch_rgb(0x3D465F));

    for (int z = 0; z < ZONAS; z++) {
        int y = MM_Y0 + z * MM_FH;
        bool ok = ch_flag(&g->s, ch_zonas_tab[z].bandera);
        bool yo = (z == aqui);
        uint16_t c = ok ? ch_rgb(0x4ADE80) : (yo ? ch_rgb(0xFFE45E)
                                                 : ch_rgb(0x606B85));

        if (yo) {
            ch_rect(b, 4, y - 2, CH_W - 8, 14, ch_rgb(0x232B41));
            ch_frame(b, 4, y - 2, CH_W - 8, 14, ch_rgb(0x3D465F));
        }
        /* Filled node = cleared, ring = where you are, dot = pending. */
        if (ok)       ch_disc(b, 18, y + 5, 4, c);
        else if (yo)  ch_ring(b, 18, y + 5, 4, c);
        else          ch_disc(b, 18, y + 5, 2, c);

        snprintf(t, sizeof(t), "%d", z + 1);
        ch_text(b, 8, y + 2, t, ch_rgb(0x8A93AB));
        ch_text(b, 30, y + 2, _(ch_zonas_tab[z].nombre),
                yo ? ch_rgb(0xFFFFFF) : (ok ? ch_rgb(0xD5DCEB)
                                            : ch_rgb(0x606B85)));
        if (ok) {
            ch_text(b, CH_W - 10 - ch_text_w(_("OK")), y + 2, _("OK"),
                    ch_rgb(0x4ADE80));
        } else if (yo) {
            ch_text(b, CH_W - 10 - ch_text_w(_("AQUI")), y + 2, _("AQUI"),
                    ch_rgb(0xFFE45E));
        }
    }

    ch_text_center(b, CH_W / 2, MM_Y0 + ZONAS * MM_FH + 4,
                   _("VENCE AL SUBJEFE PARA SEGUIR"),
                   ch_rgb(0x606B85), ch_rgb(0x05060C));
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
    ch_robot_draw(b, CH_W / 2, 34, &demo, 2, false, 0);

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
    case MODO_AYUDA:   ayuda_fondo(g); break;
    case MODO_FINAL:   final_fondo(g); break;
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
            ch_snd_melodia(g, CH_MEL_NADA);
            g->modo = MODO_MAPA;
            ch_map_entrar(g, g->s.sala, g->s.x, g->s.y);
        }
        break;

    case MODO_DIALOGO:
        dlg_avanzar(g);
        break;

    case MODO_MENU: {
        int f = fila_en(bx, by);
        if (f < 0 || f >= NMENU) return;
        ch_sfx(1000, 25);
        g->sel = 0;
        g->scroll = 0;
        switch (f) {
        case 0: g->modo = MODO_TALLER;  break;
        case 1: g->modo = MODO_OBJETOS; break;
        case 2: g->modo = MODO_FICHA;   break;
        case 3: g->modo = MODO_REGISTRO; g->sel2 = 0; break;
        case 4: g->modo = MODO_MAPAMUNDI; break;
        case 5: g->modo = MODO_AYUDA;   break;
        case 6: ch_sonido_set((ch_sonido_get() + 1) % 3); break;
        case 7: g->quiere_guardar = 1;  ch_ui_aviso(g, _("PARTIDA GUARDADA")); break;
        case 8: g->modo = MODO_MAPA;    break;
        default: break;
        }
        g->rehacer_fondo = 1;
        break;
    }

    case MODO_FINAL:
        g->modo = MODO_MAPA;
        ch_snd_melodia(g, CH_MEL_NADA);
        g->rehacer_fondo = 1;
        break;

    case MODO_TALLER:  taller_toque(g, bx, by);  break;
    case MODO_OBJETOS: objetos_toque(g, bx, by); break;
    case MODO_TIENDA:  tienda_toque(g, bx, by);  break;
    case MODO_REGISTRO: registro_toque(g, bx, by); break;
    case MODO_FICHA:
    case MODO_MAPAMUNDI: g->modo = MODO_MENU; g->rehacer_fondo = 1; break;
    case MODO_AYUDA:
        if (++g->sel >= NAYUDA) { g->sel = 0; g->modo = MODO_MENU; }
        ch_sfx(1000, 20);
        g->rehacer_fondo = 1;
        break;

    case MODO_COMBATE: ch_bt_toque(g, bx, by);   break;

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
    case MODO_OBJETOS:
    case MODO_FICHA:
    case MODO_REGISTRO:
    case MODO_MAPAMUNDI:
    case MODO_AYUDA:
        g->sel = 0;
        g->modo = MODO_MENU;
        g->rehacer_fondo = 1;
        return true;
    case MODO_TIENDA:
    case MODO_MENU:
        g->modo = MODO_MAPA;
        g->rehacer_fondo = 1;
        return true;
    case MODO_DIALOGO:
        dlg_avanzar(g);
        return true;
    case MODO_COMBATE:
        return ch_bt_atras(g);
    default:
        return false;
    }
}
