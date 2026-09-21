/*
 * CHATARRA - the phone booth: the other watch
 *
 * Every town has a booth. Walk into it and the watch talks to the one it is
 * paired with -the bump of docs/LINK.md- and the two of you can fight or swap
 * robots and parts. It is the same link Pong, Truco, Pixel Art, the radar and
 * the walkie use, and this file is the sixth reading of the same lessons.
 *
 * ---------------------------------------------------------------------------
 * WHY THE RADIO IS NOT ON WHILE YOU PLAY
 * ---------------------------------------------------------------------------
 *
 * A watch in the launcher is not on the air, and a watch walking around
 * Chatarra's 51 rooms is not either. The link costs 4.5 KB of internal RAM and
 * radio time, and nearly every minute of this game is played alone. It goes up
 * when you step into the booth and comes down when you step out, which is also
 * what makes the booth a PLACE: being reachable is something you walk to,
 * exactly as it was in the game this one is a love letter to.
 *
 * ---------------------------------------------------------------------------
 * A BATTLE BETWEEN TWO WATCHES SENDS CHOICES, NEVER RESULTS
 * ---------------------------------------------------------------------------
 *
 * Truco's lesson, one app over (apps/truco/main/tl_link.h): the engine is
 * deterministic given its dice, so both watches run the SAME combat and only
 * the two choices of each turn travel. Nobody sends "I did 14 damage"; both
 * compute 14.
 *
 * Three things that took care to get right, and all three are the difference
 * between a shared battle and two battles that slowly disagree:
 *
 *   1. THE DICE ARE THE HOST'S. `g->rng_bt` is seeded from the frame the host
 *      sends and nothing but the combat is allowed to read it. The map's
 *      particles roll on `g->rng` and would otherwise pull the sequence apart
 *      on one side only.
 *
 *   2. THE HOST ORDERS. The guest sends its choice and applies NOTHING, its
 *      own tap included, until the host echoes both. A guest that resolved its
 *      own move the moment it was tapped would be a turn ahead half the time.
 *
 *   3. A TIE ON SPEED IS DECIDED GLOBALLY. The local engine breaks a tie with
 *      "does the rival go first?", which is the OPPOSITE question on the two
 *      watches: the same coin would have both answering "the other one". The
 *      link draws "does the HOST go first?" instead, which means the same
 *      thing on both sides.
 *
 * And a fourth that is not design but insurance: every choice carries the two
 * health totals as the sender sees them. If they disagree with what this side
 * computed, the battle is stopped and says so, instead of drifting into two
 * different games that both look fine.
 *
 * ---------------------------------------------------------------------------
 * THE MODEL DOES NOT KNOW THE HAL IS THERE
 * ---------------------------------------------------------------------------
 *
 * Everything below goes through the eight ch_net_* calls chatarra.c
 * implements, the same way the music goes through ch_tono(). So this file is
 * plain model code: it can be read, and run in the simulator with two windows,
 * without a radio anywhere in sight.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * The protocol
 *
 * One byte of type and then the payload. Everything goes on the RELIABLE
 * channel -in order, acknowledged, resent- because every message here changes
 * the state of both games: a lost one is not 33 ms of nothing, it is a turn
 * that never happens.
 *
 * The structures travel as they are laid out in memory, with no packing and no
 * byte order conversion, and that is legitimate for exactly one reason: the
 * two watches run the SAME BUILD. The moment that stops being true -a watch on
 * an old version- the version byte in the hello is what catches it.
 * -------------------------------------------------------------------------- */

#define M_HOLA      'H'
#define M_EQUIPO    'E'         /* the team, the seed: a battle is proposed  */
#define M_ELIJO     'M'         /* guest -> host: my choice this turn        */
#define M_APLICA    'A'         /* host -> guest: both choices               */
#define M_FIN       'F'         /* the battle is over                        */
#define M_OFERTA    'O'         /* what I put on the table                   */
#define M_RETIRO    'X'         /* I take it back / I hang up                */

#define LK_PROTO    1           /* bumped when a message changes shape       */

typedef struct {
    uint8_t  tipo;
    uint8_t  proto;
    uint16_t pad;
    uint32_t nonce;
} msg_hola_t;

typedef struct {
    uint8_t    tipo;
    uint8_t    n;               /* robots in the team                        */
    uint8_t    host;            /* 1 if the sender believes it is the host   */
    uint8_t    pad;
    uint32_t   semilla;         /* the host's dice; 0 from the guest         */
    ch_robot_t eq[EQUIPO];
} msg_equipo_t;

typedef struct {
    uint8_t  tipo;
    uint8_t  turno;
    uint8_t  eleccion;
    uint8_t  eleccion2;         /* only in M_APLICA: the guest's             */
    int16_t  hp_host, hp_guest; /* what the sender computed: the check       */
} msg_turno_t;

typedef struct {
    uint8_t    tipo;
    uint8_t    clase;           /* 0 a robot, 1 a part                       */
    uint8_t    pieza;           /* clase 1: the part's id                    */
    uint8_t    pad;
    ch_robot_t robot;           /* clase 0                                   */
} msg_oferta_t;

/* The choice of a turn is ONE byte: 0..3 an attack slot, 0x10+i an item,
 * 0x20+slot a robot coming out. One byte is all the other watch needs, because
 * it was sent the whole team when the battle started and it has the same item
 * table: it can work out what happened without being told. Read by
 * ch_bt_aplicar_enlace(), which is where the four lines that decode it live.
 *
 * And the one measurement this protocol depends on: the biggest message has to
 * fit in a frame of the reliable channel, which is 242 bytes. Three robots plus
 * a header is comfortably inside it today, and the day somebody adds a field to
 * ch_robot_t this line is what says so.
 */
_Static_assert(sizeof(msg_equipo_t) <= 242,
               "el equipo no entra en una trama del canal fiable");

/* --------------------------------------------------------------------------
 * Small drawing helpers
 *
 * The booth's screen is a title, a panel of three lines and a column of fat
 * buttons. Fat on purpose: this is the screen you use standing next to
 * somebody, holding a watch in each hand.
 * -------------------------------------------------------------------------- */

#define BOT_X       10
#define BOT_W       164
#define BOT_H       20
#define BOT_Y0      84
#define BOT_SEP     22
#define BOT_MAX     4

/* EL MENU DE LA CABINA, EN BALDOSAS.
 *
 * Eran cuatro renglones de 20 px, que es la misma lista plana que el resto de
 * v2 ya dejo atras: en una pantalla de 1.8" un renglon de texto es algo que
 * hay que acercarse a leer. Cuatro baldosas de 84x44 -168x88 reales- con su
 * icono dicen lo mismo de un vistazo y se tocan sin apuntar. Los otros
 * estados de la cabina siguen siendo botones porque lo que muestran es una
 * LISTA de nombres, que no cabe en una baldosa. */
#define TL_X        6
#define TL_Y       76
#define TL_W      ((CH_W - TL_X * 2 - 4) / 2)
#define TL_H       44

static int baldosa_en(int bx, int by)
{
    for (int i = 0; i < 4; i++) {
        int x = TL_X + (i & 1) * (TL_W + 4);
        int y = TL_Y + (i >> 1) * (TL_H + 2);
        if (bx >= x && bx < x + TL_W && by >= y && by < y + TL_H) return i;
    }
    return -1;
}

static void baldosa(ch_t *g, int i, int icono, const char *txt, bool activo)
{
    ch_buf_t *b = &g->bg;
    int x = TL_X + (i & 1) * (TL_W + 4);
    int y = TL_Y + (i >> 1) * (TL_H + 2);

    ch_round(b, x, y, TL_W, TL_H, 3, ch_rgb(activo ? 0x1A2133 : 0x111420));
    ch_frame(b, x, y, TL_W, TL_H, ch_rgb(activo ? 0x3D465F : 0x232B41));
    ch_rect(b, x + 1, y + 1, TL_W - 2, 1, ch_rgb(0x2C3550));
    ch_ui_icono(b, x + TL_W / 2 - 12, y + 5, icono, 2);
    ch_text_center(b, x + TL_W / 2, y + 33, txt,
                   ch_rgb(activo ? 0xFFFFFF : 0x545C70), ch_rgb(0x05060C));
}

static int boton_en(int bx, int by)
{
    if (bx < BOT_X || bx >= BOT_X + BOT_W) return -1;
    for (int i = 0; i < BOT_MAX; i++) {
        int y = BOT_Y0 + i * BOT_SEP;
        if (by >= y && by < y + BOT_H) return i;
    }
    return -1;
}

static void boton(ch_t *g, int i, const char *txt, bool activo)
{
    ch_buf_t *b = &g->bg;
    int y = BOT_Y0 + i * BOT_SEP;

    ch_round(b, BOT_X, y, BOT_W, BOT_H, 3,
             ch_rgb(activo ? 0x2A3350 : 0x1A1F2E));
    ch_frame(b, BOT_X, y, BOT_W, BOT_H,
             ch_rgb(activo ? 0x6E7DA8 : 0x333A4D));
    ch_text_center(b, BOT_X + BOT_W / 2, y + 7, txt,
                   ch_rgb(activo ? 0xFFFFFF : 0x646C82), ch_rgb(0x000000));
}

static void lineas(ch_t *g, int y)
{
    ch_buf_t *b = &g->bg;

    ch_panel(b, 8, y, CH_W - 16, 40, ch_rgb(0x3D465F));
    for (int i = 0; i < 3; i++) {
        if (g->lk.linea[i][0]) {
            ch_text(b, 13, y + 5 + i * 11, g->lk.linea[i], ch_rgb(0xD5DCF0));
        }
    }
}

static void decir(ch_t *g, const char *a, const char *b, const char *c)
{
    snprintf(g->lk.linea[0], sizeof(g->lk.linea[0]), "%s", a ? a : "");
    snprintf(g->lk.linea[1], sizeof(g->lk.linea[1]), "%s", b ? b : "");
    snprintf(g->lk.linea[2], sizeof(g->lk.linea[2]), "%s", c ? c : "");
    g->rehacer_fondo = 1;
}

static void estado(ch_t *g, int e)
{
    g->lk.estado = (uint8_t)e;
    g->lk.t = 0;
    g->lk.sel = 0;
    g->rehacer_fondo = 1;
}

/* --------------------------------------------------------------------------
 * Coming in and going out
 * -------------------------------------------------------------------------- */

static void hola(ch_t *g)
{
    msg_hola_t m = { M_HOLA, LK_PROTO, 0, g->lk.nonce };
    ch_net_mandar(&m, (int)sizeof(m));
}

void ch_lk_entrar(ch_t *g)
{
    memset(&g->lk, 0, sizeof(g->lk));
    g->lk.ofrecen = 0xFF;
    g->lk.ofrezco = 0xFF;
    g->modo = MODO_CABINA;
    g->rehacer_fondo = 1;

    /* THE RADIO GOES UP FIRST AND THE PARTNER IS LOOKED FOR AFTERWARDS.
     *
     * On the board the partner lives in NVS and either order would work. In
     * the simulator it does NOT: the partner is only put there by the link's
     * own tick, so an app that asks "is there anybody?" before starting the
     * radio is an app that never sees one. Truco and Pixel Art papered over it
     * with a development flag; asking in this order needs none, and it is the
     * honest order anyway - the question is whether there is somebody on the
     * air, and you cannot ask that with the radio off. */
    if (!ch_net_empezar()) {
        estado(g, LK_SIN_ENLACE);
        decir(g, _("LA RADIO NO ARRANCO."), _("PROBA DE NUEVO."), "");
        return;
    }

    /* A nonce per visit, as Truco's hello carries: a hello with a new number
     * from the same watch means they walked back in, and both start over
     * instead of one side answering a conversation the other has forgotten. */
    g->lk.nonce = ch_rand(&g->rng);
    estado(g, LK_LLAMANDO);
    decir(g, _("BUSCANDO EL OTRO RELOJ..."), "", "");
}

void ch_lk_salir(ch_t *g)
{
    uint8_t x = M_RETIRO;

    if (g->lk.estado != LK_SIN_ENLACE) ch_net_mandar(&x, 1);
    ch_net_parar();
    memset(&g->lk, 0, sizeof(g->lk));
    g->modo = MODO_MAPA;
    g->rehacer_fondo = 1;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * The battle
 * -------------------------------------------------------------------------- */

static void mandar_equipo(ch_t *g)
{
    msg_equipo_t m;

    memset(&m, 0, sizeof(m));
    m.tipo = M_EQUIPO;
    m.n    = (uint8_t)ch_eq_n(&g->s);
    m.host = g->lk.host;
    /* Only the host's seed counts. Sending one from the guest as well would
     * be two numbers where the rule says there is one, and one day the wrong
     * one would win. */
    if (g->lk.host) {
        g->rng_bt = ch_rand(&g->rng) | 1u;
        m.semilla = g->rng_bt;
    }
    for (int i = 0; i < m.n; i++) m.eq[i] = *ch_eq(&g->s, i);
    ch_net_mandar(&m, (int)sizeof(m));
}

static void empezar_combate(ch_t *g)
{
    ch_robot_t rival = g->lk.equipo_e[0];

    /* The arena is the one of the town you are standing in - each watch sees
     * its own, which is right: the two of you are not in the same place. */
    ch_bt_empezar(g, &rival, 0, ch_salas[g->s.sala % ch_nsalas].zona);
    g->bt.enlace     = 1;
    g->bt.eleccion   = 0xFF;
    g->bt.eleccion_e = 0xFF;
    g->bt.nturno     = 0;
    g->bt.premio_pieza = 0xFF;      /* you do not rip a part off a friend    */
    g->bt.premio_cred  = 0;
}

void ch_lk_elegir(ch_t *g, uint8_t eleccion)
{
    msg_turno_t m;

    g->bt.eleccion = eleccion;
    memset(&m, 0, sizeof(m));
    m.turno    = g->bt.nturno;
    m.eleccion = eleccion;
    m.hp_host  = g->lk.host ? g->s.yo.vida : g->bt.rival.vida;
    m.hp_guest = g->lk.host ? g->bt.rival.vida : g->s.yo.vida;

    if (!g->lk.host) {
        m.tipo = M_ELIJO;
        ch_net_mandar(&m, (int)sizeof(m));
        g->bt.fase = CB_ESPERA;
        g->rehacer_fondo = 1;
        return;
    }
    /* The host waits for the guest's, then says what BOTH did. */
    if (g->bt.eleccion_e == 0xFF) {
        g->bt.fase = CB_ESPERA;
        g->rehacer_fondo = 1;
        return;
    }
    m.tipo      = M_APLICA;
    m.eleccion  = g->bt.eleccion;
    m.eleccion2 = g->bt.eleccion_e;
    ch_net_mandar(&m, (int)sizeof(m));
    ch_bt_aplicar_enlace(g, g->bt.eleccion, g->bt.eleccion_e);
}

/* --------------------------------------------------------------------------
 * UN AMISTOSO NO TE DEJA EL ROBOT ROTO
 *
 * A friendly costs nothing. Losing one used to leave the robot that fell at
 * zero health - broken, walk to a workshop - which is the same punishment as
 * losing to the world, for a fight that gave you neither credits nor a part.
 * The game this one comes from puts everything back after a battle over the
 * cable, and for the same reason: the point of the booth is to play with
 * somebody, not to be charged for it.
 *
 * Every way out of a link battle comes through here - the end of the fight,
 * the other side hanging up, the channel giving up, the two of them drifting
 * out of step - because a repair that only happens on the tidy path is a
 * repair you cannot rely on.
 * -------------------------------------------------------------------------- */
static void cerrar_combate(ch_t *g)
{
    if (!g->bt.enlace) return;
    g->bt.enlace = 0;
    ch_eq_curar(&g->s);
    g->modo = MODO_CABINA;
    g->hud_sucio = 1;
    g->quiere_guardar = 1;
}

void ch_lk_combate_fin(ch_t *g)
{
    uint8_t f = M_FIN;

    ch_net_mandar(&f, 1);
    cerrar_combate(g);
    estado(g, LK_MENU);
    decir(g, _("BUEN COMBATE."), _("TU EQUIPO QUEDO COMO NUEVO."), "");
}

/* --------------------------------------------------------------------------
 * The swap
 *
 * Both sides put something on the table and neither moves until both have.
 * Then each one gives what it offered and takes what the other did, which is
 * the same operation seen from the two ends: there is no "who applies first",
 * and so no way for one to apply and the other not.
 * -------------------------------------------------------------------------- */

static void cerrar_trueque(ch_t *g)
{
    if (g->lk.clase == 0) {
        ch_robot_t *mio = ch_eq(&g->s, g->lk.ofrezco);
        if (mio) {
            ch_robot_t nuevo = g->lk.robot_e;
            ch_robot_stats(&nuevo);
            *mio = nuevo;
            ch_robot_visto(&g->s, mio);
        }
        decir(g, _("CAMBIO HECHO!"), ch_robot_nombre(&g->lk.robot_e), "");
    } else {
        if (g->lk.ofrezco < ch_mochila(&g->s) && g->lk.ofrecen < PIEZAS) {
            g->s.piezas[g->lk.ofrezco] = g->lk.ofrecen;
            ch_ver(&g->s, g->lk.ofrecen);
        }
        decir(g, _("CAMBIO HECHO!"),
              g->lk.ofrecen < PIEZAS ? _(ch_partes[g->lk.ofrecen].nombre) : "", "");
    }
    g->lk.ofrezco = 0xFF;
    g->lk.ofrecen = 0xFF;
    ch_sfx(1400, 90);
    estado(g, LK_HECHO);
    g->quiere_guardar = 1;
}

static void mandar_oferta(ch_t *g)
{
    msg_oferta_t m;

    memset(&m, 0, sizeof(m));
    m.tipo  = M_OFERTA;
    m.clase = g->lk.clase;
    if (g->lk.clase == 0) {
        const ch_robot_t *r = ch_eq(&g->s, g->lk.ofrezco);
        if (r) m.robot = *r;
    } else {
        m.pieza = g->lk.ofrezco < ch_mochila(&g->s) ? g->s.piezas[g->lk.ofrezco]
                                                   : 0xFF;
    }
    ch_net_mandar(&m, (int)sizeof(m));
}

/* --------------------------------------------------------------------------
 * What comes in
 * -------------------------------------------------------------------------- */

static void llego(ch_t *g, const uint8_t *d, int n)
{
    switch (d[0]) {

    case M_HOLA: {
        msg_hola_t m;
        if (n < (int)sizeof(m)) return;
        memcpy(&m, d, sizeof(m));
        if (m.proto != LK_PROTO) {
            estado(g, LK_CAIDO);
            decir(g, _("EL OTRO RELOJ TIENE"), _("OTRA VERSION DEL JUEGO."), "");
            return;
        }
        /* Answer only a hello we have not answered: otherwise the two watches
         * greet each other forever. */
        if (m.nonce != g->lk.nonce_e) {
            g->lk.nonce_e = m.nonce;
            hola(g);
            estado(g, LK_MENU);
            decir(g, _("CONECTADO CON"), g->lk.nombre, "");
            ch_sfx(1600, 60);
        } else if (g->lk.estado == LK_LLAMANDO) {
            estado(g, LK_MENU);
            decir(g, _("CONECTADO CON"), g->lk.nombre, "");
            ch_sfx(1600, 60);
        }
        return;
    }

    case M_EQUIPO: {
        msg_equipo_t m;
        if (n < (int)sizeof(m)) return;
        memcpy(&m, d, sizeof(m));
        if (m.n < 1 || m.n > EQUIPO) return;
        g->lk.nequipo_e = m.n;
        for (int i = 0; i < m.n; i++) {
            g->lk.equipo_e[i] = m.eq[i];
            ch_robot_stats(&g->lk.equipo_e[i]);
            ch_robot_visto(&g->s, &g->lk.equipo_e[i]);
        }
        if (!g->lk.host) g->rng_bt = m.semilla | 1u;

        /* If we had not offered yet, we answer with ours and start; if we had,
         * this is their answer and we start too. Both ends reach the same
         * line, which is the only thing that matters. */
        if (g->lk.estado != LK_OFRECIDO) mandar_equipo(g);
        empezar_combate(g);
        return;
    }

    case M_ELIJO: {
        msg_turno_t m;
        if (n < (int)sizeof(m) || !g->bt.enlace || !g->lk.host) return;
        memcpy(&m, d, sizeof(m));
        if (m.turno != g->bt.nturno) return;        /* an echo of a past turn */
        g->bt.eleccion_e = m.eleccion;
        if (g->bt.eleccion != 0xFF) {
            msg_turno_t a = m;
            a.tipo      = M_APLICA;
            a.eleccion  = g->bt.eleccion;
            a.eleccion2 = g->bt.eleccion_e;
            a.hp_host   = g->s.yo.vida;
            a.hp_guest  = g->bt.rival.vida;
            ch_net_mandar(&a, (int)sizeof(a));
            ch_bt_aplicar_enlace(g, g->bt.eleccion, g->bt.eleccion_e);
        }
        return;
    }

    case M_APLICA: {
        msg_turno_t m;
        if (n < (int)sizeof(m) || !g->bt.enlace || g->lk.host) return;
        memcpy(&m, d, sizeof(m));
        if (m.turno != g->bt.nturno) return;
        /* The insurance: they tell us the two health totals as they see them
         * BEFORE the turn. If they are not ours, the two battles have already
         * drifted apart and everything after this would be theatre. */
        if (m.hp_host != g->bt.rival.vida || m.hp_guest != g->s.yo.vida) {
            cerrar_combate(g);
            estado(g, LK_CAIDO);
            decir(g, _("LOS DOS COMBATES"), _("SE DESINCRONIZARON."),
                     _("SE CORTO LA PELEA."));
            return;
        }
        ch_bt_aplicar_enlace(g, m.eleccion2, m.eleccion);
        return;
    }

    case M_FIN:
        if (g->bt.enlace) {
            cerrar_combate(g);
            estado(g, LK_MENU);
            decir(g, _("BUEN COMBATE."), _("TU EQUIPO QUEDO COMO NUEVO."), "");
        }
        return;

    case M_OFERTA: {
        msg_oferta_t m;
        if (n < (int)sizeof(m)) return;
        memcpy(&m, d, sizeof(m));
        g->lk.clase   = m.clase;
        g->lk.robot_e = m.robot;
        g->lk.ofrecen = m.clase ? m.pieza : 0;
        if (g->lk.estado == LK_OFRECIDO) {
            cerrar_trueque(g);
        } else {
            estado(g, LK_ELIGIENDO);
            if (m.clase == 0) {
                decir(g, _("TE OFRECEN UN ROBOT:"),
                      ch_robot_nombre(&m.robot), _("ELEGI QUE DAS A CAMBIO."));
            } else {
                decir(g, _("TE OFRECEN UNA PIEZA:"),
                      m.pieza < PIEZAS ? _(ch_partes[m.pieza].nombre) : "",
                      _("ELEGI QUE DAS A CAMBIO."));
            }
            ch_sfx(1300, 50);
        }
        return;
    }

    case M_RETIRO:
        cerrar_combate(g);
        g->lk.ofrecen = 0xFF;
        g->lk.ofrezco = 0xFF;
        estado(g, LK_CAIDO);
        decir(g, _("COLGARON DEL OTRO LADO."), "", "");
        return;

    default:
        return;
    }
}

void ch_lk_tick(ch_t *g)
{
    uint8_t buf[sizeof(msg_equipo_t) + 8];
    int n;

    if (g->lk.estado == LK_SIN_ENLACE) return;

    while ((n = ch_net_recibir(buf, (int)sizeof(buf))) > 0) {
        llego(g, buf, n);
    }

    if (g->lk.t < 0xFFF0) g->lk.t++;

    if (ch_net_caido()) {
        ch_net_reset_canal();
        cerrar_combate(g);
        estado(g, LK_CAIDO);
        decir(g, _("SE CORTO LA LLAMADA."), _("EL OTRO RELOJ NO CONTESTA."), "");
        return;
    }

    /* --------------------------------------------------------------------
     * WAITING FOR SOMETHING THAT MAY NEVER COME
     *
     * `ch_net_caido()` only fires when OUR frames are not being acknowledged.
     * The side that has already spoken and is waiting to be answered has
     * nothing outstanding, so nothing times out - and it sits on ESPERANDO
     * for ever if the other watch walks out of the game without its goodbye
     * arriving. That happened on the board: one watch won, went back to the
     * booth and hung up; the other stayed waiting with a dead battle on
     * screen and `rel_tx 9, rel_acked 9`, nothing pending, nothing wrong.
     *
     * The signal that settles it is their BEACON: it says which app they are
     * offering and it stops five seconds after they leave. Two checks a
     * second apart is proof enough. The 60-second cap underneath is not for
     * that case - it is so that no combination of losses can leave this
     * screen stuck, and it is generous because on the other side there is a
     * person deciding, not a machine.
     * ------------------------------------------------------------------- */
    {
        bool esperando = (g->bt.enlace && g->bt.fase == CB_ESPERA) ||
                         g->lk.estado == LK_OFRECIDO;
        if (!esperando) {
            g->lk.espera = 0;
            g->lk.mudos = 0;
        } else if (++g->lk.espera % 30 == 0) {
            if (ch_net_alla()) {
                g->lk.mudos = 0;
            } else if (++g->lk.mudos >= 2) {
                cerrar_combate(g);
                g->lk.ofrezco = 0xFF;
                g->lk.ofrecen = 0xFF;
                estado(g, LK_CAIDO);
                decir(g, _("SE FUE DE CHATARRA."), g->lk.nombre, "");
                return;
            }
            if (g->lk.espera > 1800) {          /* 60 s: never stuck        */
                cerrar_combate(g);
                g->lk.ofrezco = 0xFF;
                g->lk.ofrecen = 0xFF;
                estado(g, LK_CAIDO);
                decir(g, _("NO CONTESTAN."), _("SE CORTO LA ESPERA."), "");
                return;
            }
        }
    }

    if (g->lk.estado == LK_LLAMANDO) {
        if (!g->lk.nombre[0]) {
            /* Still looking for the paired watch. Two seconds is plenty: on
             * the board it is in NVS and answers at once, and in the
             * simulator it takes a tick or two to appear. */
            if (ch_net_hay_pareja(g->lk.nombre, sizeof(g->lk.nombre))) {
                g->lk.host = ch_net_soy_host() ? 1 : 0;
                g->lk.t = 0;
                decir(g, _("LLAMANDO..."), g->lk.nombre, "");
                hola(g);
            } else if (g->lk.t > 60) {
                estado(g, LK_SIN_ENLACE);
                decir(g, _("NO TENES OTRO RELOJ"),
                         _("EMPAREJADO. CHOCA LOS DOS"), _("EN LA APP ENLACE."));
            }
            return;
        }
        if (g->lk.t % 30 == 29) hola(g);
        /* Their beacon says which app they are in. Saying so beats waiting out
         * a channel that was never going to answer, which is the trap Pixel
         * Art wrote down. */
        if (g->lk.t == 120 && !ch_net_alla()) {
            decir(g, _("LLAMANDO..."), g->lk.nombre,
                     _("NO ESTA EN CHATARRA."));
        }
        if (g->lk.t > 450) {
            estado(g, LK_CAIDO);
            decir(g, _("NO CONTESTAN."),
                     _("QUE ABRAN CHATARRA Y"), _("ENTREN A UNA CABINA."));
        }
    }
}

/* --------------------------------------------------------------------------
 * The screen
 * -------------------------------------------------------------------------- */

/* The list of things you can put on the table. Robots are the team; parts are
 * the loose ones in the bag. */
static int opciones(const ch_t *g, uint8_t *ids, int max)
{
    int n = 0;

    if (g->lk.clase == 0) {
        for (int i = 0; i < ch_eq_n(&g->s) && n < max; i++) ids[n++] = (uint8_t)i;
    } else {
        for (int i = 0; i < ch_mochila(&g->s) && n < max; i++) {
            if (g->s.piezas[i] < PIEZAS) ids[n++] = (uint8_t)i;
        }
    }
    return n;
}

static void nombre_opcion(const ch_t *g, int id, char *dst, size_t n)
{
    if (g->lk.clase == 0) {
        const ch_robot_t *r = ch_eq((ch_save_t *)&g->s, id);
        snprintf(dst, n, "%s N%d", r ? ch_robot_nombre(r) : "", r ? r->nivel : 0);
    } else {
        int p = g->s.piezas[id];
        snprintf(dst, n, "%s", p < PIEZAS ? _(ch_partes[p].nombre) : "");
    }
}

void ch_lk_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    char t[40];

    ch_ui_titulo(g, _("CABINA"),
                 g->lk.estado == LK_SIN_ENLACE ? _("SIN PAREJA") : g->lk.nombre);

    switch (g->lk.estado) {

    case LK_SIN_ENLACE:
    case LK_CAIDO:
    case LK_HECHO:
        lineas(g, 44);
        boton(g, 0, _("SALIR"), true);
        break;

    case LK_LLAMANDO:
        lineas(g, 44);
        break;

    case LK_MENU:
        lineas(g, 34);
        baldosa(g, 0, IC_COMBATE, _("COMBATIR"), true);
        baldosa(g, 1, IC_TRUEQUE, _("ROBOT"), true);
        baldosa(g, 2, IC_PIEZA,   _("PIEZA"), ch_lk_hay_piezas(g));
        baldosa(g, 3, IC_COLGAR,  _("COLGAR"), true);
        break;

    case LK_ELIGIENDO: {
        uint8_t ids[CH_MAX_MOCHILA];
        int n = opciones(g, ids, CH_MAX_MOCHILA);
        lineas(g, 40);
        for (int i = 0; i < BOT_MAX; i++) {
            int k = g->lk.sel + i;
            if (k >= n) { boton(g, i, "", false); continue; }
            nombre_opcion(g, ids[k], t, sizeof(t));
            boton(g, i, t, true);
        }
        /* The page counter goes in the HEADER and not under the panel: four
         * buttons this size leave no strip between the text and the first one,
         * and a line that lands inside the panel is worse than no line. */
        if (n > BOT_MAX) {
            snprintf(t, sizeof(t), _("%d-%d DE %d"), g->lk.sel + 1,
                     g->lk.sel + BOT_MAX > n ? n : g->lk.sel + BOT_MAX, n);
            ch_text(b, CH_W - 8 - ch_text_w(t), 20, t, ch_rgb(0xFFE45E));
        }
        break;
    }

    case LK_OFRECIDO:
        lineas(g, 44);
        boton(g, 0, _("ESPERANDO..."), false);
        boton(g, 1, _("RETIRAR"), true);
        break;

    default:
        break;
    }
}

void ch_lk_dibujar(ch_t *g)
{
    /* The only thing that moves is the little dot that says the call is alive,
     * and it is one 6x6 rectangle: everything else IS the background. */
    ch_buf_t *b = &g->fb;
    bool on = (g->cuadro / 15) & 1;
    uint16_t c = g->lk.estado == LK_SIN_ENLACE || g->lk.estado == LK_CAIDO
                 ? ch_rgb(0xE05252)
                 : (on ? ch_rgb(0x4ADE80) : ch_rgb(0x1E4A2C));

    ch_rect(b, CH_W - 14, 9, 6, 6, c);
    ch_dirty_add(&g->d_cur, CH_W - 14, 9, 6, 6);
}

void ch_lk_toque(ch_t *g, int bx, int by)
{
    /* En el menu la cabina son baldosas; en los demas estados, botones. */
    int b = (g->lk.estado == LK_MENU) ? baldosa_en(bx, by) : boton_en(bx, by);

    switch (g->lk.estado) {

    case LK_SIN_ENLACE:
    case LK_CAIDO:
    case LK_HECHO:
        if (b == 0) { ch_sfx(700, 40); ch_lk_salir(g); }
        return;

    case LK_MENU:
        if (b < 0) return;
        ch_sfx(1100, 30);
        if (b == 0) {
            mandar_equipo(g);
            estado(g, LK_OFRECIDO);
            decir(g, _("DESAFIO MANDADO."), _("ESPERANDO AL OTRO RELOJ."), "");
        } else if (b == 1 || b == 2) {
            if (b == 2 && !ch_lk_hay_piezas(g)) { ch_sfx(220, 60); return; }
            g->lk.clase = (uint8_t)(b == 1 ? 0 : 1);
            estado(g, LK_ELIGIENDO);
            decir(g, b == 1 ? _("QUE ROBOT OFRECES?")
                            : _("QUE PIEZA OFRECES?"), "", "");
        } else {
            ch_lk_salir(g);
        }
        return;

    case LK_ELIGIENDO: {
        uint8_t ids[CH_MAX_MOCHILA];
        int n = opciones(g, ids, CH_MAX_MOCHILA);
        /* Touching the title pages the list: there is no room for arrows next
         * to buttons this size, and paging by the header is what the register
         * already does. */
        if (by < 34) {
            if (n > BOT_MAX) {
                g->lk.sel = (uint8_t)(g->lk.sel + BOT_MAX >= n
                                      ? 0 : g->lk.sel + BOT_MAX);
                ch_sfx(900, 20);
                g->rehacer_fondo = 1;
            }
            return;
        }
        if (b < 0 || g->lk.sel + b >= n) return;
        ch_sfx(1100, 30);
        g->lk.ofrezco = ids[g->lk.sel + b];
        if (g->lk.ofrecen != 0xFF) {        /* they had already offered      */
            mandar_oferta(g);
            cerrar_trueque(g);
        } else {
            mandar_oferta(g);
            estado(g, LK_OFRECIDO);
            decir(g, _("OFERTA MANDADA."), _("ESPERANDO AL OTRO RELOJ."), "");
        }
        return;
    }

    case LK_OFRECIDO:
        if (b == 1) {
            uint8_t x = M_RETIRO;
            ch_sfx(700, 40);
            ch_net_mandar(&x, 1);
            g->lk.ofrezco = 0xFF;
            g->lk.ofrecen = 0xFF;
            estado(g, LK_MENU);
            decir(g, _("RETIRADO."), "", "");
        }
        return;

    default:
        return;
    }
}

bool ch_lk_atras(ch_t *g)
{
    switch (g->lk.estado) {
    case LK_ELIGIENDO:
    case LK_OFRECIDO:
        g->lk.ofrezco = 0xFF;
        estado(g, LK_MENU);
        decir(g, _("CONECTADO CON"), g->lk.nombre, "");
        return true;
    default:
        ch_lk_salir(g);
        return true;
    }
}

bool ch_lk_hay_piezas(const ch_t *g)
{
    for (int i = 0; i < ch_mochila(&g->s); i++) if (g->s.piezas[i] < PIEZAS) return true;
    return false;
}
