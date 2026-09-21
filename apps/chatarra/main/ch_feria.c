/* --------------------------------------------------------------------------
 * LA FERIA DEL PUERTO: la cinta de chatarra
 *
 * Un pueblo donde lo unico que se puede hacer es hablar y pelear es un pasillo
 * con casas dibujadas. Esto es lo otro que se puede hacer: una cinta
 * transportadora que trae chatarra de tres carriles, y hay que agarrar las
 * piezas buenas y dejar pasar las oxidadas. Cuarenta segundos.
 *
 * Por que una cinta y no otra cosa: el motor de esta app dibuja por
 * rectangulos sucios y lo que cuesta es AREA empujada, no dibujo. Seis piezas
 * de 16x16 que se mueven un pixel por cuadro son seis rectangulos chicos, o
 * sea lo mismo que cuesta un pueblo con un gato y un pajaro. Un juego de
 * pantalla completa habria costado el triple.
 *
 * Lo que se gana: creditos siempre, y la primera vez que se pasan los veinte
 * puntos, una pieza. Detras de bandera, porque si no es una maquina de
 * imprimir piezas -la misma leccion que los muebles.
 * -------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>

#include "chatarra.h"
#include "aos_i18n.h"

#define FE_CARRIL   3
#define FE_Y0      54                   /* el primer carril                  */
#define FE_ALTO    34                   /* separacion entre carriles         */
#define FE_PIEZA   22                   /* la pieza a escala 1 mide 20       */
#define FE_DUR   1200                   /* 40 s a 30 cuadros                 */
#define FE_META    20                   /* los puntos que pagan la pieza     */
#define FE_PREMIO_CR 5                  /* creditos por punto                */

void ch_fe_entrar(ch_t *g)
{
    memset(&g->fe, 0, sizeof(g->fe));
    g->fe.resta = FE_DUR;
    g->fe.sem = (uint32_t)(0xC0FFEEu ^ (g->cuadro * 2654435761u));
    g->modo = MODO_FERIA;
    g->rehacer_fondo = 1;
    ch_snd_melodia(g, CH_MEL_NADA);
    ch_sfx(1400, 60);
}

static void soltar(ch_t *g)
{
    for (int i = 0; i < CH_FE_MAX; i++) {
        if (g->fe.p[i].vivo) continue;
        g->fe.p[i].vivo = 1;
        g->fe.p[i].carril = (uint8_t)ch_rnd(&g->fe.sem, FE_CARRIL);
        g->fe.p[i].x = (int16_t)(-FE_PIEZA);
        /* Una de cada tres es oxidada. Mas que eso y el juego es esperar; menos
         * y es tocar todo lo que pasa, que no es un juego. */
        g->fe.p[i].malo = (uint8_t)(ch_rnd(&g->fe.sem, 3) == 0);
        g->fe.p[i].cual = (uint8_t)ch_rnd(&g->fe.sem, PIEZAS);
        return;
    }
}

void ch_fe_tick(ch_t *g)
{
    g->cuadro++;
    if (g->fe.aviso) g->fe.aviso--;

    if (g->fe.resta == 0) return;
    if (--g->fe.resta == 0) {
        /* El pago. Los creditos siempre; la pieza, la primera vez que se
         * llega a la meta. */
        int cr = g->fe.puntos * FE_PREMIO_CR;
        g->s.creditos = (uint16_t)(g->s.creditos + cr > 9999 ? 9999
                                                : g->s.creditos + cr);
        if (g->fe.puntos > g->fe.record) g->fe.record = (uint8_t)g->fe.puntos;
        /* La pieza, UNA sola vez. Va en una bandera y no en un campo nuevo:
         * ch_save_t se acepta por tamano, asi que un byte mas tira todas las
         * partidas en curso. Misma leccion que el juego de piezas. */
        if (g->fe.puntos >= FE_META) {
            /* 1 = te ganaste la pieza, 2 = te la ganaste y no entraba. Las dos
             * se dicen en la pantalla final: enterarse de que llegaste y no
             * habia lugar es lo que te manda al chatarrero. */
            g->fe.premio = (uint8_t)ch_feria_premio(&g->s, &g->fe.sem);
        }
        ch_snd_melodia(g, g->fe.puntos >= FE_META ? CH_MEL_VICTORIA
                                                  : CH_MEL_DERROTA);
        g->hud_sucio = 1;
        g->rehacer_fondo = 1;
        return;
    }

    if ((g->fe.resta % 26) == 0) soltar(g);

    for (int i = 0; i < CH_FE_MAX; i++) {
        if (!g->fe.p[i].vivo) continue;
        g->fe.p[i].x = (int16_t)(g->fe.p[i].x + 2);
        if (g->fe.p[i].x > CH_W) g->fe.p[i].vivo = 0;
    }
}

void ch_fe_toque(ch_t *g, int bx, int by)
{
    if (g->fe.resta == 0) {             /* terminado: un toque y a la calle */
        g->modo = MODO_MAPA;
        g->mel_mapa = 0;
        ch_map_musica(g);
        g->rehacer_fondo = 1;
        g->hud_sucio = 1;
        return;
    }
    for (int i = 0; i < CH_FE_MAX; i++) {
        int x, y;
        if (!g->fe.p[i].vivo) continue;
        x = g->fe.p[i].x;
        y = FE_Y0 + g->fe.p[i].carril * FE_ALTO;
        if (bx < x - 3 || bx >= x + FE_PIEZA + 3) continue;
        if (by < y - 3 || by >= y + FE_PIEZA + 3) continue;

        g->fe.p[i].vivo = 0;
        if (g->fe.p[i].malo) {
            /* La oxidada no resta puntos: resta TIEMPO, que es lo que duele
             * sin poder dejarte en negativo. */
            g->fe.resta = (uint16_t)(g->fe.resta > 90 ? g->fe.resta - 90 : 1);
            g->fe.aviso = 20;
            ch_sfx(200, 90);
        } else {
            g->fe.puntos++;
            ch_sfx(1200 + g->fe.puntos * 20, 40);
        }
        return;
    }
}

void ch_fe_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;

    ch_rect(b, 0, 0, CH_W, MAP_H, ch_rgb(0x10131C));
    ch_vgrad(b, 0, 0, CH_W, 40, ch_rgb(0x2A1F3E), ch_rgb(0x10131C));
    /* Sin subtitulo: la franja del reloj arranca en y=20 y lo tapaba. Lo que
     * hay que hacer lo dice el feriante antes de empezar. */
    ch_text(b, 8, 6, _("CINTA DE CHATARRA"), ch_rgb(0xFFE45E));

    /* Los tres carriles, con la cinta dibujada de fondo. */
    for (int c = 0; c < FE_CARRIL; c++) {
        int y = FE_Y0 + c * FE_ALTO;
        ch_rect(b, 0, y - 4, CH_W, FE_PIEZA + 8, ch_rgb(0x1A2133));
        ch_hline(b, 0, y - 4, CH_W, ch_rgb(0x3D465F));
        ch_hline(b, 0, y + FE_PIEZA + 3, CH_W, ch_rgb(0x05060C));
        for (int x = 0; x < CH_W; x += 8) {
            ch_rect(b, x, y + FE_PIEZA + 1, 4, 2, ch_rgb(0x2C3550));
        }
    }
}

void ch_fe_dibujar(ch_t *g)
{
    ch_buf_t *b = &g->fb;
    char t[44];

    ch_clip(b, 0, 0, CH_W, MAP_H);

    if (g->fe.resta == 0) {
        int cr = g->fe.puntos * FE_PREMIO_CR;
        /* 78 de alto y no 70: el aviso de "no tenes lugar" son DOS lineas y
         * la segunda se salia por el borde de abajo. */
        ch_panel(b, 12, 58, CH_W - 24, 78, ch_rgb(0x8A93AB));
        snprintf(t, sizeof(t), _("%d PIEZAS"), g->fe.puntos);
        ch_text_center(b, CH_W / 2, 66, t, ch_rgb(0xFFFFFF), ch_rgb(0x05060C));
        snprintf(t, sizeof(t), _("+%d CREDITOS"), cr);
        ch_text_center(b, CH_W / 2, 80, t, ch_rgb(0xFFE45E), ch_rgb(0x05060C));
        snprintf(t, sizeof(t), _("RECORD %d"), g->fe.record);
        ch_text_center(b, CH_W / 2, 96, t, ch_rgb(0x8A93AB), ch_rgb(0x05060C));
        if (g->fe.premio == FE_PREMIO_DADA) {
            ch_text_center(b, CH_W / 2, 116, _("TE GANASTE UNA PIEZA!"),
                           ch_rgb(0x4ADE80), ch_rgb(0x05060C));
        } else if (g->fe.premio == FE_PREMIO_LLENA) {
            ch_text_center(b, CH_W / 2, 112, _("TE GANASTE UNA PIEZA"),
                           ch_rgb(0xFFE45E), ch_rgb(0x05060C));
            ch_text_center(b, CH_W / 2, 122, _("PERO NO TENES LUGAR"),
                           ch_rgb(0xFF4A3D), ch_rgb(0x05060C));
        } else {
            ch_text_center(b, CH_W / 2, 116, _("TOCA PARA SALIR"),
                           ch_rgb(0xD5DCEB), ch_rgb(0x05060C));
        }
        ch_dirty_add(&g->d_cur, 12, 58, CH_W - 24, 78);
        return;
    }

    /* El reloj y los puntos, arriba. */
    ch_rect(b, 0, 20, CH_W, 26, ch_rgb(0x10131C));
    snprintf(t, sizeof(t), _("PIEZAS %d"), g->fe.puntos);
    ch_text(b, 8, 22, t, ch_rgb(0xFFFFFF));
    snprintf(t, sizeof(t), "%d", (g->fe.resta + 29) / 30);
    ch_text(b, CH_W - 8 - ch_text_w(t), 22, t,
            g->fe.resta < 150 ? ch_rgb(0xFF4A3D) : ch_rgb(0xD5DCEB));
    ch_barra(b, 8, 34, CH_W - 16, g->fe.resta, FE_DUR,
             g->fe.aviso ? ch_rgb(0xFF4A3D) : ch_rgb(0x4ADE80));
    ch_dirty_add(&g->d_cur, 0, 20, CH_W, 26);

    for (int i = 0; i < CH_FE_MAX; i++) {
        int x, y, cat, var;
        if (!g->fe.p[i].vivo) continue;
        x = g->fe.p[i].x;
        y = FE_Y0 + g->fe.p[i].carril * FE_ALTO;
        cat = g->fe.p[i].cual / PVAR;
        var = g->fe.p[i].cual % PVAR;

        /* La caja: verde la buena, oxidada la mala. Lo que decide no es la
         * pieza sino la CAJA, porque a esta velocidad una pieza de 16 px no
         * se distingue de otra y el juego seria adivinar. */
        ch_rect(b, x, y, FE_PIEZA, FE_PIEZA,
                g->fe.p[i].malo ? ch_rgb(0x6E3A16) : ch_rgb(0x1E7A3C));
        ch_frame(b, x, y, FE_PIEZA, FE_PIEZA,
                 g->fe.p[i].malo ? ch_rgb(0xC06B2E) : ch_rgb(0x4ADE80));
        /* La pieza va CENTRADA en la caja: ch_part_draw() coloca cada
         * categoria con su propio salto, asi que el centro de la caja es el
         * unico punto que sirve para las cuatro. Misma leccion que el
         * registro. */
        ch_part_draw(b, cat, var, x + FE_PIEZA / 2, y + FE_PIEZA / 2, 1,
                     g->fe.p[i].malo ? -1 : (int)g->s.yo.skin);
        ch_dirty_add(&g->d_cur, x - 2, y - 2, FE_PIEZA + 6, FE_PIEZA + 6);
    }
    ch_clip_none(b);
    (void)t;
}
