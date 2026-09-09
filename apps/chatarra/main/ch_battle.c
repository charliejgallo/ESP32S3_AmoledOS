/*
 * CHATARRA - combat
 *
 * Turn-based, two robots, six elemental types and an effectiveness table. The
 * shape is the classic one and rightly so: what makes this game different is
 * not the combat, it is that the robot you fight with is one you built
 * yourself out of the parts you tore off others.
 *
 * ---------------------------------------------------------------------------
 * HOW THE DRAWING IS DIVIDED
 * ---------------------------------------------------------------------------
 *
 * The same as the map: what stands still goes into the background (bg) and
 * only the two robots are redrawn per frame. The combat's background carries
 * the arena, the two panels with the bars and the box at the bottom with the
 * menu or the message.
 *
 * That means changing phase -from the menu to the attacks, from the message to
 * the menu- or moving a health bar REBUILDS the background. It is an expensive
 * frame, but it happens once a turn and not thirty times a second, which is
 * exactly the division we want. The alternative -drawing the panels as if they
 * moved- would dirty half the screen on every frame to show numbers that
 * change once every few seconds.
 *
 * ---------------------------------------------------------------------------
 * DAMAGE
 * ---------------------------------------------------------------------------
 *
 * All in integers. The formula is the usual one, with the effectiveness in
 * eighths so it is a multiplication and a shift, and the attack and defence
 * stages in a table also in eighths. No floating point: on the S3 it drags in
 * __divsf3 and fattens the .so without buying anything.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Geometry of the combat screen
 *
 * Everything touchable ends at y=172, that is, a real 344: below 354 the
 * board's touch panel does not respond.
 * -------------------------------------------------------------------------- */

/* The two robots are 52x80 (the 26x40 box at scale 2) and CANNOT overlap
 * either the panels or the box at the bottom: they are drawn on every frame on
 * top of the background, so any overlap shows up as eaten text. That is where
 * these coordinates come from, and that is why they are all together with the
 * sums in plain sight.
 *
 *   opponent  x 106..158   y   2.. 82
 *   opponent's panel       x   4.. 96   y  2..28
 *   player    x  20.. 72   y  44..124
 *   player's panel         x  88..180   y 88..128
 *   box                    x   4..180   y 130..174   (a real 344: the touch ceiling)
 */
#define RIVAL_CX    132
#define RIVAL_Y       2
#define YO_CX        46
#define YO_Y         44

#define PAN_R_X       4
#define PAN_R_Y       2
#define PAN_R_H      26
#define PAN_YO_X     88
#define PAN_YO_Y     88
#define PAN_YO_H     40
#define PAN_W        92

#define CAJA_X        4
#define CAJA_Y      130
#define CAJA_W      176
#define CAJA_H       44

#define BOT_W        86
#define BOT_H        19

static const int BOT_X[4] = { CAJA_X + 2, CAJA_X + 90, CAJA_X + 2, CAJA_X + 90 };
static const int BOT_Y[4] = { CAJA_Y + 2, CAJA_Y + 2, CAJA_Y + 23, CAJA_Y + 23 };

static const char *const MENU_PPAL[4] = { N_("ATACAR"), N_("OBJETO"),
                                         N_("ANALIZAR"), N_("HUIR") };

/* --------------------------------------------------------------------------
 * Attack and defence stages, in eighths
 * -------------------------------------------------------------------------- */

static const uint8_t ETAPA[13] = { 2, 2, 3, 3, 4, 5, 8, 12, 16, 20, 24, 28, 32 };

static int con_etapa(int v, int et)
{
    if (et < -6) et = -6;
    if (et >  6) et =  6;
    return v * ETAPA[et + 6] / 8;
}

/* --------------------------------------------------------------------------
 * Messages
 * -------------------------------------------------------------------------- */

static void msg(ch_t *g, const char *a, const char *b, const char *c)
{
    snprintf(g->bt.linea[0], sizeof(g->bt.linea[0]), "%s", a ? a : "");
    snprintf(g->bt.linea[1], sizeof(g->bt.linea[1]), "%s", b ? b : "");
    snprintf(g->bt.linea[2], sizeof(g->bt.linea[2]), "%s", c ? c : "");
    g->bt.fase = CB_MENSAJE;
    g->rehacer_fondo = 1;
}

static void msg1(ch_t *g, const char *fmt, const char *arg)
{
    char t[30];
    snprintf(t, sizeof(t), fmt, arg);
    msg(g, t, "", "");
}

/* --------------------------------------------------------------------------
 * Starting
 * -------------------------------------------------------------------------- */

void ch_bt_empezar(ch_t *g, const ch_robot_t *rival, int jefe, int zona)
{
    memset(&g->bt, 0, sizeof(g->bt));
    g->bt.rival = *rival;
    g->bt.jefe  = (uint8_t)jefe;
    g->bt.zona  = (uint8_t)zona;
    g->bt.origen = 0xFF;
    g->bt.premio_pieza = 0xFF;
    ch_robot_stats(&g->bt.rival);
    ch_robot_visto(&g->s, &g->bt.rival);
    if (g->bt.rival.vida <= 0) {
        g->bt.rival.vida = g->bt.rival.vida_max;
        g->bt.rival.ene  = g->bt.rival.ene_max;
    }
    ch_robot_stats(&g->s.yo);

    g->bt.hp_ver[0] = g->s.yo.vida;
    g->bt.hp_ver[1] = g->bt.rival.vida;

    g->modo = MODO_COMBATE;
    g->bt.entrada = 12;
    g->bt.pend = CB_MENU;
    ch_snd_melodia(g, jefe ? CH_MEL_JEFE : CH_MEL_COMBATE);
    msg1(g, _("TE ATACA %s!"), ch_robot_nombre(&g->bt.rival));
    ch_sfx(500, 60);
}

/* --------------------------------------------------------------------------
 * Damage
 * -------------------------------------------------------------------------- */

static int danio(ch_t *g, const ch_robot_t *at, const ch_robot_t *df,
                 const ch_move_t *m, int et_a, int et_d, int *efec)
{
    int atk, def, base, ef;

    *efec = 8;
    if (!m->poder) return 0;

    atk = con_etapa(at->atk, et_a);
    def = con_etapa(df->def, et_d);
    if (def < 1) def = 1;

    base = ((2 * at->nivel / 5 + 2) * m->poder * atk / def) / 25 + 2;

    ef = ch_efectividad(m->tipo, df->tipo);
    *efec = ef;
    base = base * ef / 8;

    /* Bonus for using an attack of your own type: the type comes from the
     * torso, so building the robot around one element has a reward. */
    if (m->tipo == at->tipo) base = base * 5 / 4;

    base = base * (85 + ch_rnd(&g->rng, 16)) / 100;

    /* Critical: one in ten. It exists because turn-based combat with nearly
     * fixed damage turns into a sum and not a fight; the critical is what
     * makes the extra turn worth trying when you are losing. */
    g->bt.critico = 0;
    if (ch_rnd(&g->rng, 100) < 10) {
        base = base * 3 / 2;
        g->bt.critico = 1;
    }
    return base < 1 ? 1 : base;
}

/* --------------------------------------------------------------------------
 * One attack
 *
 * 'quien' is 0 for the player and 1 for the opponent. Returns true if whoever
 * took the hit is out of the fight.
 * -------------------------------------------------------------------------- */

static bool atacar(ch_t *g, int quien, int mv)
{
    ch_robot_t *at = quien ? &g->bt.rival : &g->s.yo;
    ch_robot_t *df = quien ? &g->s.yo     : &g->bt.rival;
    const ch_move_t *m = &ch_moves[mv % MOVES];
    char l1[30], l2[30], l3[30];
    int ef = 8, d = 0;

    l1[0] = l2[0] = l3[0] = 0;
    snprintf(l1, sizeof(l1), _("%s USA %s"),
             quien ? _("EL RIVAL") : _("TU ROBOT"), _(m->nombre));

    /* Short circuit: the turn is lost. */
    if (g->bt.corto[quien]) {
        g->bt.corto[quien]--;
        if (ch_rnd(&g->rng, 100) < 45) {
            snprintf(l1, sizeof(l1), _("%s ESTA"),
                     quien ? _("EL RIVAL") : _("TU ROBOT"));
            msg(g, l1, _("EN CORTOCIRCUITO Y"), _("NO PUEDE MOVERSE."));
            return false;
        }
    }

    if (at->ene < m->costo) {
        msg(g, l1, _("PERO NO TIENE ENERGIA."), "");
        return false;
    }
    at->ene = (int16_t)(at->ene - m->costo);

    if (ch_rnd(&g->rng, 100) >= m->precision) {
        msg(g, l1, _("PERO FALLA."), "");
        ch_sfx(200, 50);
        return false;
    }

    d = danio(g, at, df, m, g->bt.et_atk[quien], g->bt.et_def[!quien], &ef);
    ch_bt_animar(g, quien, m->tipo, d == 0);
    if (d) {
        df->vida = (int16_t)(df->vida - d);
        if (df->vida < 0) df->vida = 0;
        g->bt.sacude = 8;
        g->bt.sacude_quien = (uint8_t)!quien;
        g->bt.flash = 3;
        g->bt.dmg_val = (int16_t)d;
        g->bt.dmg_t = 26;
        g->bt.dmg_quien = (uint8_t)!quien;
        /* Each type with its own sound. A single beep for all six elements
         * wastes the one audio channel there is: with these, the hit is
         * recognised without looking. */
        switch (m->tipo) {
        case TIPO_FUEGO:  ch_sfx(180, 90);  break;   /* low whoosh           */
        case TIPO_CRIO:   ch_sfx(1400, 45); break;   /* glass                */
        case TIPO_VOLT:   ch_sfx(2200, 30); break;   /* crack                */
        case TIPO_PLASMA: ch_sfx(900, 60);  break;
        case TIPO_ACIDO:  ch_sfx(240, 70);  break;
        default:          ch_sfx(quien ? 300 : 700, 50); break;
        }
        if (g->bt.critico) {
            snprintf(l2, sizeof(l2), _("CRITICO! %d DE DANO"), d);
            g->bt.sacude = 14;
            g->bt.flash = 6;
        }
        else if (ef > 8) snprintf(l2, sizeof(l2), _("MUY EFICAZ! %d DE DANO"), d);
        else if (ef < 8) snprintf(l2, sizeof(l2), _("POCO EFICAZ. %d DE DANO"), d);
        else             snprintf(l2, sizeof(l2), _("%d DE DANO."), d);
    }

    /* The effect, if there is one and if it lands. */
    if (m->efecto != EF_NADA && ch_rnd(&g->rng, 100) < m->prob) {
        switch (m->efecto) {
        case EF_BAJA_DEF:
            if (g->bt.et_def[!quien] > -6) g->bt.et_def[!quien]--;
            snprintf(l3, sizeof(l3), "%s", _("BAJA SU DEFENSA."));
            break;
        case EF_BAJA_ATK:
            if (g->bt.et_atk[!quien] > -6) g->bt.et_atk[!quien]--;
            snprintf(l3, sizeof(l3), "%s", _("BAJA SU ATAQUE."));
            break;
        case EF_SUBE_ATK:
            if (g->bt.et_atk[quien] < 6) g->bt.et_atk[quien]++;
            snprintf(l3, sizeof(l3), "%s", _("SUBE SU ATAQUE."));
            break;
        case EF_SUBE_DEF:
            if (g->bt.et_def[quien] < 6) g->bt.et_def[quien]++;
            snprintf(l3, sizeof(l3), "%s", _("SUBE SU DEFENSA."));
            break;
        case EF_QUEMA:
            g->bt.quema[!quien] = 4;
            snprintf(l3, sizeof(l3), "%s", _("QUEDA RECALENTADO."));
            break;
        case EF_CORTO:
            g->bt.corto[!quien] = 3;
            snprintf(l3, sizeof(l3), "%s", _("QUEDA EN CORTOCIRCUITO."));
            break;
        case EF_DRENA: {
            int cura = d / 2;
            at->vida = (int16_t)(at->vida + cura);
            if (at->vida > at->vida_max) at->vida = at->vida_max;
            snprintf(l3, sizeof(l3), _("ABSORBE %d DE VIDA."), cura);
            break;
        }
        case EF_CARGA:
            at->ene = at->ene_max;
            snprintf(l3, sizeof(l3), "%s", _("RECARGA LA BATERIA."));
            break;
        case EF_REPARA: {
            int cura = at->vida_max / 2;
            at->vida = (int16_t)(at->vida + cura);
            if (at->vida > at->vida_max) at->vida = at->vida_max;
            snprintf(l3, sizeof(l3), _("SE REPARA %d."), cura);
            break;
        }
        default:
            break;
        }
    }

    msg(g, l1, l2, l3);
    return df->vida <= 0;
}

/* --------------------------------------------------------------------------
 * The opponent's choice
 *
 * It is not pure chance: it looks at the effectiveness against your type and
 * weights the attack that would do the most damage more heavily. There is
 * still a roll, so it is not predictable, but an opponent that ignores
 * weaknesses does not force you to think about the combination of parts, which
 * is what the game is about.
 * -------------------------------------------------------------------------- */

static int elegir_rival(ch_t *g)
{
    ch_robot_t *r = &g->bt.rival;
    int mejor = 0, mejor_p = -1;

    for (int i = 0; i < r->nmov; i++) {
        const ch_move_t *m = &ch_moves[r->mov[i] % MOVES];
        int p;

        if (r->ene < m->costo) continue;

        if (!m->poder) {
            /* Status ones are worth it early, not when it is about to fall. */
            p = (r->vida * 3 > r->vida_max * 2) ? 20 : 5;
            if (m->efecto == EF_REPARA && r->vida * 3 < r->vida_max) p = 90;
        } else {
            p = m->poder * ch_efectividad(m->tipo, g->s.yo.tipo) / 8;
            if (m->tipo == r->tipo) p = p * 5 / 4;
            p = p * m->precision / 100;
        }
        p += ch_rnd(&g->rng, 25);
        if (p > mejor_p) { mejor_p = p; mejor = i; }
    }
    return r->mov[mejor];
}

/* --------------------------------------------------------------------------
 * End of the fight
 * -------------------------------------------------------------------------- */

static void subir_nivel(ch_t *g)
{
    while (g->s.yo.nivel < 60 &&
           g->s.yo.exp >= ch_exp_nivel(g->s.yo.nivel + 1)) {
        g->s.yo.nivel++;
        ch_robot_stats(&g->s.yo);
        g->s.yo.vida = g->s.yo.vida_max;      /* levelling up repairs        */
        g->s.yo.ene  = g->s.yo.ene_max;
        ch_sfx(1400, 90);
    }
}

static void victoria(ch_t *g)
{
    ch_robot_t *r = &g->bt.rival;
    char l2[40], l3[40];
    int exp = r->nivel * r->nivel * 3 / 2 + 12;
    int cred = r->nivel * 8 + ch_rnd(&g->rng, 20);
    int nv_antes = g->s.yo.nivel;

    if (g->bt.jefe) { exp *= 3; cred *= 3; }

    g->s.victorias++;
    g->s.yo.exp += (uint32_t)exp;
    g->s.creditos = (uint16_t)(g->s.creditos + cred > 9999 ? 9999
                                                          : g->s.creditos + cred);
    subir_nivel(g);

    /* The part that can be torn off. It is the game's real prize, so the
     * chance is not negligible but nor is it certain: without that, in three
     * fights your bag would be full and the workshop would stop mattering. */
    l3[0] = 0;
    int chance = g->bt.jefe ? 100 : 28;
    if (g->s.obj[IT_IMAN]) { chance += 30; g->s.obj[IT_IMAN]--; }
    g->bt.premio_pieza = 0xFF;
    if (ch_rnd(&g->rng, 100) < chance) {
        int cat = ch_rnd(&g->rng, P_CATS);
        uint8_t id = PIEZA_ID(cat, r->pieza[cat]);
        bool entro = false;
        for (int i = 0; i < MOCHILA; i++) {
            if (g->s.piezas[i] == 0xFF) {
                g->s.piezas[i] = id; g->bt.premio_pieza = id; entro = true; break;
            }
        }
        /* The bag has twelve slots and it fills up. The part used to be lost
         * SILENTLY, which is the worst way for it to happen: you think you
         * were unlucky. */
        if (!entro) g->bt.premio_pieza = 0xFE;
    }

    /* The room's creature is defeated for good, and drops whatever it carries.
     * The sub-boss's pass comes from here: it is a field of the world's table,
     * not a special case in the code. */
    if (g->bt.origen != 0xFF && g->bt.origen < g->nmov) {
        const ch_room_t *sala = &ch_salas[g->s.sala % ch_nsalas];
        const ch_ent_t *e = &sala->ents[g->mov[g->bt.origen].idx];
        ch_flag_set(&g->s, e->p2);
        g->mov[g->bt.origen].vivo = 0;
        if (e->premio && e->premio < ITEMS && g->s.obj[e->premio] < 99) {
            g->s.obj[e->premio]++;
            g->bt.premio_pieza = 0xFF;      /* the item rules the panel      */
            snprintf(l3, sizeof(l3), _("TE DEJA: %s"), _(ch_items[e->premio].nombre));
        }
    }

    snprintf(l2, sizeof(l2), _("GANAS %d EXP Y %d CRED."), exp, cred);
    if (l3[0]) {
        /* the sub-boss's prize already filled it */
    } else if (g->bt.premio_pieza == 0xFE) {
        snprintf(l3, sizeof(l3), "%s", _("MOCHILA LLENA!"));
    } else if (g->bt.premio_pieza != 0xFF) {
        snprintf(l3, sizeof(l3), _("ARRANCAS: %s"),
                 _(ch_partes[g->bt.premio_pieza].nombre));
    } else if (g->s.yo.nivel > nv_antes) {
        snprintf(l3, sizeof(l3), _("SUBISTE AL NIVEL %d!"), g->s.yo.nivel);
    } else {
        l3[0] = 0;
    }

    g->bt.pend = CB_FIN;
    /* The world's last room is the summit: winning there ends the game and
     * deserves different music. It comes from comparing against ch_nsalas and
     * not from a special flag, so it still holds if a ninth zone is added
     * tomorrow. */
    ch_snd_melodia(g, (g->bt.jefe && g->s.sala == ch_nsalas - 1)
                      ? CH_MEL_FINAL : CH_MEL_VICTORIA);
    msg(g, _("GANASTE EL COMBATE!"), l2, l3);
}

static void derrota(ch_t *g)
{
    int perdido = g->s.creditos / 4;
    g->s.creditos = (uint16_t)(g->s.creditos - perdido);
    g->bt.pend = CB_FIN;
    g->bt.huir = 2;                 /* 2 = you crawled back to the workshop  */
    {
        char l2[30];
        snprintf(l2, sizeof(l2), _("PIERDES %d CREDITOS."), perdido);
        msg(g, _("TU ROBOT SE APAGO..."), l2, _("TE LLEVAN AL TALLER."));
    }
    ch_snd_melodia(g, CH_MEL_DERROTA);
}

/* --------------------------------------------------------------------------
 * The turn
 * -------------------------------------------------------------------------- */

static void fin_de_turno(ch_t *g);
void ch_bt_animar(ch_t *g, int quien, int tipo, int de_estado);

/* Resolves the second half of the turn or closes. Called on closing each
 * message, which is what gives the combat its rhythm. */
static void seguir(ch_t *g)
{
    switch (g->bt.pend) {
    case CB_MENU:
        g->bt.fase = CB_MENU;
        g->bt.sel = 0;
        g->rehacer_fondo = 1;
        break;

    case CB_ACCION:
        /* the second half: whoever did not attack first replies */
        g->bt.pend = CB_MENSAJE;
        if (g->bt.turno == 0) {
            if (atacar(g, 1, g->bt.mov_r)) { derrota(g); return; }
        } else {
            if (atacar(g, 0, g->bt.mov_j)) { victoria(g); return; }
        }
        g->bt.pend = CB_MENSAJE;
        g->bt.turno = 2;            /* mark: both have played                */
        break;

    case CB_MENSAJE:
        fin_de_turno(g);
        break;

    case CB_FIN:
        /* Beating the champion opens the closing screen. It is told apart by
         * the room, like the music: it is the world's last. */
        if (g->bt.jefe && g->s.sala == ch_nsalas - 1 &&
            ch_flag(&g->s, 0) == false && g->bt.rival.vida <= 0) {
            g->modo = MODO_FINAL;
            g->rehacer_fondo = 1;
            break;
        }
        g->modo = MODO_MAPA;
        ch_snd_melodia(g, CH_MEL_NADA);
        if (g->bt.huir == 2) {
            ch_robot_curar(&g->s.yo);
            ch_map_entrar(g, 0, 11, 14);        /* your house                */
        }
        g->rehacer_fondo = 1;
        g->hud_sucio = 1;
        break;

    default:
        g->bt.fase = CB_MENU;
        g->rehacer_fondo = 1;
        break;
    }
}

static void fin_de_turno(ch_t *g)
{
    /* Overheating hits when the turn closes. */
    for (int q = 0; q < 2; q++) {
        if (!g->bt.quema[q]) continue;
        ch_robot_t *r = q ? &g->bt.rival : &g->s.yo;
        int d = r->vida_max / 16;
        if (d < 1) d = 1;
        r->vida = (int16_t)(r->vida - d);
        if (r->vida < 0) r->vida = 0;
        g->bt.quema[q]--;
        {
            char l1[30], l2[30];
            snprintf(l1, sizeof(l1), _("%s SE RECALIENTA"),
                 q ? _("EL RIVAL") : _("TU ROBOT"));
            snprintf(l2, sizeof(l2), _("Y PIERDE %d DE VIDA."), d);
            g->bt.pend = CB_MENSAJE;
            msg(g, l1, l2, "");
        }
        if (g->s.yo.vida <= 0)      { derrota(g);  return; }
        if (g->bt.rival.vida <= 0)  { victoria(g); return; }
        return;                     /* one message at a time                 */
    }

    g->bt.pend = CB_MENU;
    g->bt.fase = CB_MENU;
    g->bt.sel = 0;
    g->rehacer_fondo = 1;
}

static void jugar_turno(ch_t *g, int mv)
{
    g->bt.mov_j = (uint8_t)mv;
    g->bt.mov_r = (uint8_t)elegir_rival(g);

    int vj = con_etapa(g->s.yo.vel, 0);
    int vr = con_etapa(g->bt.rival.vel, 0);
    g->bt.turno = (uint8_t)((vr > vj || (vr == vj && ch_rnd(&g->rng, 2))) ? 1 : 0);

    g->bt.pend = CB_ACCION;
    if (g->bt.turno == 0) {
        if (atacar(g, 0, g->bt.mov_j)) { victoria(g); return; }
    } else {
        if (atacar(g, 1, g->bt.mov_r)) { derrota(g); return; }
    }
    g->bt.pend = CB_ACCION;
}

/* --------------------------------------------------------------------------
 * Items in combat
 * -------------------------------------------------------------------------- */

static void usar_objeto(ch_t *g, int it)
{
    const ch_item_t *d = &ch_items[it];
    char l2[30];

    if (!g->s.obj[it] || !d->combate) return;
    g->s.obj[it]--;

    switch (it) {
    case IT_ACEITE:
    case IT_ACEITE2: {
        int cura = d->valor;
        if (g->s.yo.vida + cura > g->s.yo.vida_max) {
            cura = g->s.yo.vida_max - g->s.yo.vida;
        }
        g->s.yo.vida = (int16_t)(g->s.yo.vida + cura);
        ch_bt_animar(g, 1, TIPO_ACIDO, 0);   /* green particles over you     */
        g->bt.dmg_val = (int16_t)cura;
        g->bt.dmg_t = 26;
        g->bt.dmg_quien = 0;
        snprintf(l2, sizeof(l2), _("REPARA %d DE VIDA."), cura);
        break;
    }
    case IT_BATERIA:
    case IT_BATERIA2: {
        int c = d->valor;
        if (g->s.yo.ene + c > g->s.yo.ene_max) c = g->s.yo.ene_max - g->s.yo.ene;
        g->s.yo.ene = (int16_t)(g->s.yo.ene + c);
        snprintf(l2, sizeof(l2), _("RECUPERA %d DE ENERGIA."), c);
        break;
    }
    case IT_IMAN:
        g->s.obj[it]++;                 /* not spent here, spent on winning */
        snprintf(l2, sizeof(l2), "%s", _("LO USARAS AL GANAR."));
        break;
    default:
        snprintf(l2, sizeof(l2), "%s", _("NO PASA NADA."));
        break;
    }

    /* Using an item IS your turn: the opponent replies. */
    g->bt.mov_r = (uint8_t)elegir_rival(g);
    g->bt.turno = 1;
    g->bt.pend = CB_ACCION;
    {
        char l1[30];
        snprintf(l1, sizeof(l1), _("USAS %s"), _(d->nombre));
        msg(g, l1, l2, "");
    }
    ch_sfx(900, 60);
}

/* --------------------------------------------------------------------------
 * Running away
 * -------------------------------------------------------------------------- */

static void huir(ch_t *g)
{
    if (g->bt.jefe) {
        g->bt.pend = CB_MENSAJE;
        msg(g, _("NO SE PUEDE HUIR"), _("DE UN COMBATE ASI."), "");
        return;
    }
    if (ch_rnd(&g->rng, 100) < 55 + g->s.yo.vel - g->bt.rival.vel) {
        g->bt.huir = 1;
        g->bt.pend = CB_FIN;
        msg(g, _("TE ESCAPASTE."), "", "");
    } else {
        g->bt.mov_r = (uint8_t)elegir_rival(g);
        g->bt.turno = 1;
        g->bt.pend = CB_ACCION;
        msg(g, _("NO PUDISTE ESCAPAR!"), "", "");
    }
}

/* --------------------------------------------------------------------------
 * The touch
 * -------------------------------------------------------------------------- */

static int boton_en(int bx, int by)
{
    for (int i = 0; i < 4; i++) {
        if (bx >= BOT_X[i] && bx < BOT_X[i] + BOT_W &&
            by >= BOT_Y[i] && by < BOT_Y[i] + BOT_H) {
            return i;
        }
    }
    return -1;
}

void ch_bt_toque(ch_t *g, int bx, int by)
{
    int b;

    switch (g->bt.fase) {
    case CB_MENSAJE:
        seguir(g);
        return;

    case CB_MENU:
        b = boton_en(bx, by);
        if (b < 0) return;
        ch_sfx(1100, 25);
        if (b == 0)      { g->bt.fase = CB_ATAQUES; g->rehacer_fondo = 1; }
        else if (b == 1) { g->bt.fase = CB_OBJETOS; g->rehacer_fondo = 1; }
        else if (b == 2) {
            char l1[30], l2[30], l3[40];
            const ch_robot_t *r = &g->bt.rival;
            snprintf(l1, sizeof(l1), _("%s  NV %d"), ch_robot_nombre(r), r->nivel);
            snprintf(l2, sizeof(l2), _("TIPO %s   A%d D%d V%d"),
                     _(ch_tipo_nombre[r->tipo % TIPOS]), r->atk, r->def, r->vel);
            /* 40 and not 30: for GCC a %d takes up to eleven characters and it
             * does not care that here it is a two-digit stat. With -Werror
             * that stops the board's build, and on the Mac it says nothing. */
            /* The attacks you have seen it use, not just the stats: that is
             * what really decides how to fight it. */
            {
                int k = 0;
                l3[0] = 0;
                for (int i = 0; i < r->nmov && k < 28; i++) {
                    const char *nm = _(ch_moves[r->mov[i] % MOVES].nombre);
                    k += snprintf(l3 + k, sizeof(l3) - (size_t)k, "%s%s",
                                  k ? " " : "", nm);
                }
            }
            g->bt.pend = CB_MENU;
            msg(g, l1, l2, l3);
        } else {
            huir(g);
        }
        return;

    case CB_ATAQUES:
        b = boton_en(bx, by);
        if (b < 0 || b >= g->s.yo.nmov) return;
        ch_sfx(1100, 25);
        jugar_turno(g, g->s.yo.mov[b]);
        return;

    case CB_OBJETOS: {
        static const uint8_t LISTA[4] = { IT_ACEITE, IT_ACEITE2, IT_BATERIA, IT_IMAN };
        b = boton_en(bx, by);
        if (b < 0) return;
        if (!g->s.obj[LISTA[b]]) { ch_sfx(220, 40); return; }
        ch_sfx(1100, 25);
        usar_objeto(g, LISTA[b]);
        return;
    }

    default:
        return;
    }
}

/* The back gesture: from a sub-list to the menu. */
bool ch_bt_atras(ch_t *g)
{
    if (g->bt.fase == CB_ATAQUES || g->bt.fase == CB_OBJETOS) {
        g->bt.fase = CB_MENU;
        g->rehacer_fondo = 1;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * The combat's background
 * -------------------------------------------------------------------------- */

static void panel_robot(ch_t *g, int x, int y, const ch_robot_t *r, bool mio)
{
    ch_buf_t *b = &g->bg;
    char t[24];

    ch_panel(b, x, y, PAN_W, mio ? PAN_YO_H : PAN_R_H, ch_rgb(0x8A93AB));
    ch_text(b, x + 4, y + 3, ch_robot_nombre(r), ch_rgb(0xFFFFFF));
    snprintf(t, sizeof(t), _("N%d"), r->nivel);
    ch_text(b, x + PAN_W - 4 - ch_text_w(t), y + 3, t, ch_rgb(0xFFE45E));

    /* Only the bar's SLOT. The fill and the numbers are drawn by
     * ch_bt_dibujar() per frame, because they go down gradually: leaving them
     * in the background forced a full rebuild for every point of health. */
    ch_barra(b, x + 4, y + 14, PAN_W - 8, 0, 1, ch_rgb(0x4ADE80));
    if (mio) {
        ch_barra(b, x + 4, y + 32, PAN_W - 8, 0, 1, ch_rgb(0x4A9DF5));
    }
    (void)t;
}

static void boton(ch_t *g, int i, const char *txt, bool activo)
{
    ch_buf_t *b = &g->bg;
    uint16_t fondo = activo ? ch_rgb(0x2B3145) : ch_rgb(0x171B29);
    uint16_t borde = activo ? ch_rgb(0x8A93AB) : ch_rgb(0x3D465F);
    uint16_t tinta = activo ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85);

    ch_rect(b, BOT_X[i], BOT_Y[i], BOT_W, BOT_H, fondo);
    ch_frame(b, BOT_X[i], BOT_Y[i], BOT_W, BOT_H, borde);
    ch_text_center(b, BOT_X[i] + BOT_W / 2, BOT_Y[i] + 6, txt, tinta,
                   ch_rgb(0x05060C));
}

void ch_bt_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;

    /* The arena: sky, horizon and floor. It is the only decorative part and
     * that is why it can afford a gradient: it is drawn once per phase. */
    ch_vgrad(b, 0, 0, CH_W, 96, ch_rgb(0x1B2340), ch_rgb(0x3B3A62));
    ch_vgrad(b, 0, 96, CH_W, 176, ch_rgb(0x4A4162), ch_rgb(0x241E33));
    ch_rect(b, 0, 94, CH_W, 2, ch_rgb(0x6A5F8C));

    /* Two platforms, one per robot. They go in the BACKGROUND and not with the
     * robot: they do not move, so there is no reason for them to pay a dirty
     * rectangle per frame. */
    ch_disc(b, RIVAL_CX, RIVAL_Y + 80, 30, ch_rgb(0x5A5178));
    ch_disc(b, RIVAL_CX, RIVAL_Y + 78, 28, ch_rgb(0x6E648F));
    ch_disc(b, YO_CX, YO_Y + 82, 36, ch_rgb(0x5A5178));
    ch_disc(b, YO_CX, YO_Y + 80, 34, ch_rgb(0x6E648F));

    panel_robot(g, PAN_R_X,  PAN_R_Y,  &g->bt.rival, false);
    panel_robot(g, PAN_YO_X, PAN_YO_Y, &g->s.yo,     true);

    /* The box at the bottom */
    ch_panel(b, CAJA_X, CAJA_Y, CAJA_W, CAJA_H, ch_rgb(0x8A93AB));

    switch (g->bt.fase) {
    case CB_MENU:
        for (int i = 0; i < 4; i++) boton(g, i, _(MENU_PPAL[i]), true);
        break;

    case CB_ATAQUES:
        for (int i = 0; i < 4; i++) {
            if (i < g->s.yo.nmov) {
                const ch_move_t *m = &ch_moves[g->s.yo.mov[i] % MOVES];
                boton(g, i, _(m->nombre), g->s.yo.ene >= m->costo);
                /* the cost, small in the corner */
                if (m->costo) {
                    char t[8];
                    snprintf(t, sizeof(t), "%d", m->costo);
                    ch_text(b, BOT_X[i] + BOT_W - 4 - ch_text_w(t),
                            BOT_Y[i] + BOT_H - 8, t,
                            ch_rgb(ch_tipo_color[m->tipo % TIPOS]));
                }
                /* THE EFFECTIVENESS, ON THE BUTTON.
                 *
                 * The game has six types and a cross table, and until now the
                 * only way to learn it was losing fights and remembering. That
                 * is not difficulty, that is having to take notes. Two little
                 * arrows tell it without explaining anything. */
                if (m->poder) {
                    int ef = ch_efectividad(m->tipo, g->bt.rival.tipo);
                    if (ef != 8) {
                        uint16_t c = ef > 8 ? ch_rgb(0x4ADE80) : ch_rgb(0xFF4A3D);
                        int ax = BOT_X[i] + BOT_W - 8, ay = BOT_Y[i] + 4;
                        for (int k = 0; k < 3; k++) {
                            int w = 5 - k * 2;
                            ch_rect(b, ax - w / 2,
                                    ef > 8 ? ay + k : ay + 2 - k, w, 1, c);
                        }
                    }
                }
            } else {
                boton(g, i, "-", false);
            }
        }
        break;

    case CB_OBJETOS: {
        static const uint8_t LISTA[4] = { IT_ACEITE, IT_ACEITE2, IT_BATERIA, IT_IMAN };
        for (int i = 0; i < 4; i++) {
            char t[26];
            snprintf(t, sizeof(t), "%s x%d", _(ch_items[LISTA[i]].nombre),
                     g->s.obj[LISTA[i]]);
            boton(g, i, t, g->s.obj[LISTA[i]] > 0);
            /* How much it heals, on the button itself: in the middle of a
             * fight nobody remembers whether the oil is 40 or 120. */
            if (ch_items[LISTA[i]].valor) {
                char v[10];
                snprintf(v, sizeof(v), "+%d", ch_items[LISTA[i]].valor);
                ch_text(b, BOT_X[i] + BOT_W - 4 - ch_text_w(v),
                        BOT_Y[i] + BOT_H - 8, v, ch_rgb(0x4ADE80));
            }
        }
        break;
    }

    default: {                      /* CB_MENSAJE and the rest: text         */
        /* If you tore a part off, it is DRAWN beside the text. It is the whole
         * game's prize and until now it was a line of text like any other. */
        bool premio = (g->bt.pend == CB_FIN && g->bt.premio_pieza < PIEZAS);
        int ancho = premio ? CAJA_W - 40 : CAJA_W;

        for (int i = 0; i < 3; i++) {
            if (g->bt.linea[i][0]) {
                char corte[30];
                snprintf(corte, sizeof(corte), "%.*s",
                         (ancho - 12) / CH_FADV, g->bt.linea[i]);
                ch_text(b, CAJA_X + 6, CAJA_Y + 5 + i * 12, corte,
                        ch_rgb(0xFFFFFF));
            }
        }
        if (premio) {
            int id = g->bt.premio_pieza;
            ch_rect(b, CAJA_X + CAJA_W - 38, CAJA_Y + 4, 34, CAJA_H - 8,
                    ch_rgb(0x0E111A));
            ch_frame(b, CAJA_X + CAJA_W - 38, CAJA_Y + 4, 34, CAJA_H - 8,
                     ch_rgb(0xFFE45E));
            ch_part_draw(b, PIEZA_CAT(id), PIEZA_VAR(id),
                         CAJA_X + CAJA_W - 21, CAJA_Y + CAJA_H / 2 + 2, 1,
                         (int)g->s.yo.skin);
        }
        /* the little "carry on" arrow */
        ch_rect(b, CAJA_X + CAJA_W - 10, CAJA_Y + CAJA_H - 8, 5, 2,
                ch_rgb(0xFFE45E));
        ch_rect(b, CAJA_X + CAJA_W - 9, CAJA_Y + CAJA_H - 6, 3, 2,
                ch_rgb(0xFFE45E));
        ch_rect(b, CAJA_X + CAJA_W - 8, CAJA_Y + CAJA_H - 4, 1, 2,
                ch_rgb(0xFFE45E));
        break;
    }
    }

}

/* --------------------------------------------------------------------------
 * THE HIT ANIMATION
 *
 * Twenty-two frames, some 730 ms, running while the message panel is being
 * read. That is what makes it cheap in game time: the turn already stopped
 * there waiting for the touch.
 *
 *     0.. 5   the attacker steps forward
 *     6..13   the projectile travels (or the punch lands and that is that)
 *    14       impact: the particles are born, shake and flash
 *    14..21   the particles fly and fade
 *
 * The particles go in QUARTER PIXELS with integers. With float we would have
 * to drag __divsf3 and __extendsfdf2 into the .so, and it buys nothing: at
 * this scale a quarter pixel is more resolution than can be seen.
 * -------------------------------------------------------------------------- */

#define ANIM_LARGO   22
#define ANIM_IMPACTO 8          /* frames remaining when it lands            */
#define NPART        ((int)(sizeof(((ch_batalla_t *)0)->part) / \
                            sizeof(((ch_batalla_t *)0)->part[0])))

/* The ANIMATION's colour is not always the type's. IMPACTO is light grey -it
 * works for the text label- and over a light robot the hit's ring disappeared:
 * you could not see that anything had happened. What is drawn on top of the
 * robots needs a colour of its own, chosen against THEM and not against the
 * background. */
static uint16_t color_anim(int tipo)
{
    return ch_rgb(tipo == TIPO_IMPACTO ? 0xFFE45E
                                       : ch_tipo_color[tipo % TIPOS]);
}

/* Where each robot's chest is, which is what the hits aim at. */
static void punto(int quien, int *x, int *y)
{
    *x = quien ? RIVAL_CX : YO_CX;
    *y = quien ? RIVAL_Y + 34 : YO_Y + 34;
}

void ch_bt_animar(ch_t *g, int quien, int tipo, int de_estado)
{
    g->bt.anim = ANIM_LARGO;
    g->bt.anim_tipo = (uint8_t)tipo;
    g->bt.anim_dir = (uint8_t)quien;
    g->bt.anim_estado = (uint8_t)de_estado;
    for (int i = 0; i < NPART; i++) g->bt.part[i].vida = 0;
}

/* Born on impact. Each type throws its own differently, which is what makes a
 * flamethrower not look the same as a lightning bolt. */
static void soltar_particulas(ch_t *g)
{
    int cx, cy;
    punto(!g->bt.anim_dir, &cx, &cy);

    for (int i = 0; i < NPART; i++) {
        int ang = i * (256 / NPART) + ch_rnd(&g->rng, 24);
        int vel;

        switch (g->bt.anim_tipo) {
        case TIPO_FUEGO:  vel = 5 + ch_rnd(&g->rng, 7); break;  /* rises     */
        case TIPO_CRIO:   vel = 9 + ch_rnd(&g->rng, 4); break;  /* shards    */
        case TIPO_VOLT:   vel = 12 + ch_rnd(&g->rng, 8); break; /* sparks    */
        case TIPO_ACIDO:  vel = 4 + ch_rnd(&g->rng, 6); break;  /* drips     */
        default:          vel = 7 + ch_rnd(&g->rng, 8); break;
        }
        g->bt.part[i].x  = (int16_t)(cx * 4);
        g->bt.part[i].y  = (int16_t)(cy * 4);
        g->bt.part[i].vx = (int16_t)(ch_cos(ang) * vel / 64);
        g->bt.part[i].vy = (int16_t)(ch_sin(ang) * vel / 64);
        if (g->bt.anim_tipo == TIPO_FUEGO) g->bt.part[i].vy -= 4;
        g->bt.part[i].vida = (uint8_t)(6 + ch_rnd(&g->rng, 6));
        g->bt.part[i].col  = (uint8_t)(i & 1);
    }
}

void ch_bt_tick(ch_t *g)
{
    if (g->bt.entrada) g->bt.entrada--;
    if (g->bt.sacude) g->bt.sacude--;
    if (g->bt.flash)  g->bt.flash--;
    if (g->bt.dmg_t)  g->bt.dmg_t--;

    if (g->bt.anim) {
        if (g->bt.anim == ANIM_IMPACTO && !g->bt.anim_estado) {
            soltar_particulas(g);
        }
        g->bt.anim--;
    }

    for (int i = 0; i < NPART; i++) {
        if (!g->bt.part[i].vida) continue;
        g->bt.part[i].x = (int16_t)(g->bt.part[i].x + g->bt.part[i].vx);
        g->bt.part[i].y = (int16_t)(g->bt.part[i].y + g->bt.part[i].vy);
        g->bt.part[i].vy = (int16_t)(g->bt.part[i].vy + 1);   /* gravity     */
        g->bt.part[i].vida--;
    }

    /* The bars approach the real health instead of jumping. An eighth of the
     * difference per frame, with a minimum of one: that way a small hit is
     * still seen to move and a large one does not take a second. */
    for (int q = 0; q < 2; q++) {
        int real = q ? g->bt.rival.vida : g->s.yo.vida;
        int dif = real - g->bt.hp_ver[q];
        if (dif) {
            int paso = dif / 8;
            if (!paso) paso = dif > 0 ? 1 : -1;
            g->bt.hp_ver[q] = (int16_t)(g->bt.hp_ver[q] + paso);
        }
    }
    g->cuadro++;
}

/* The projectile. Each type travels differently, and that is half of what
 * makes a flamethrower not be confused with a lightning bolt. */
static void proyectil(ch_t *g, int t256)
{
    ch_buf_t *b = &g->fb;
    int ox, oy, dx, dy, x, y;
    uint16_t c = color_anim(g->bt.anim_tipo);

    punto(g->bt.anim_dir, &ox, &oy);
    punto(!g->bt.anim_dir, &dx, &dy);
    x = ox + (dx - ox) * t256 / 256;
    y = oy + (dy - oy) * t256 / 256;

    switch (g->bt.anim_tipo) {
    case TIPO_VOLT:
        /* The bolt does not travel: it appears whole, jagged, and flickers. */
        for (int i = 0; i < 6; i++) {
            int x0 = ox + (dx - ox) * i / 6, y0 = oy + (dy - oy) * i / 6;
            int x1 = ox + (dx - ox) * (i + 1) / 6, y1 = oy + (dy - oy) * (i + 1) / 6;
            int j = (i & 1) ? 5 : -5;
            ch_line(b, x0, y0 + (i ? j : 0), x1, y1 + j, c);
        }
        break;
    case TIPO_FUEGO:
        ch_disc(b, x, y, 5, c);
        ch_disc(b, x - (dx > ox ? 5 : -5), y + 1, 3, ch_rgb(0xC05A00));
        ch_disc(b, x, y - 1, 2, ch_rgb(0xFFE45E));
        break;
    case TIPO_CRIO:
        for (int i = 0; i < 4; i++) {
            ch_rect(b, x - 4 + i, y - 4 + i * 2, 8 - i * 2, 1, c);
        }
        ch_rect(b, x - 1, y - 5, 2, 10, ch_rgb(0xFFFFFF));
        break;
    case TIPO_ACIDO:
        ch_disc(b, x, y + ch_sin(t256 & 255) / 40, 4, c);
        ch_disc(b, x - 4, y + 3, 2, ch_rgb(0x1E7A3C));
        break;
    case TIPO_PLASMA:
        ch_disc(b, x, y, 4, ch_rgb(0xFFFFFF));
        ch_glow(b, x, y, 7, c, 10);
        ch_disc(b, x - (dx > ox ? 6 : -6), y, 2, c);
        break;
    default:
        /* IMPACTO has no projectile: the hit is the robot itself. */
        break;
    }
}

void ch_bt_dibujar(ch_t *g)
{
    ch_buf_t *b = &g->fb;
    int w, h, cx, cy;
    int bob = ((g->cuadro >> 4) & 1) ? 1 : 0;
    int sac_r = 0, sac_y = 0, emb_r = 0, emb_y = 0;
    int paso = ANIM_LARGO - g->bt.anim;      /* 0..21 while it lasts         */
    bool animando = g->bt.anim || g->bt.dmg_t;
    uint16_t ctipo = color_anim(g->bt.anim_tipo);

    ch_robot_box(2, &w, &h);
    ch_clip(b, 0, 0, CH_W, CAJA_Y);

    /* The attacker steps forward and back. With IMPACTO it steps further,
     * because the hit IS the robot going all the way to the other one.
     *
     * The cap of 10 is not aesthetic: the robot is 52 wide and the panels
     * start at x=88 and end at x=96. With 26 the player's robot was drawn ON
     * TOP of its own health panel, because the clip is a rectangle and cannot
     * dodge the boxes. */
    if (g->bt.anim) {
        int lejos = g->bt.anim_tipo == TIPO_IMPACTO ? 10 : 5;
        int d = paso < 6 ? paso * lejos / 6
              : (paso < 14 ? lejos : (ANIM_LARGO - paso) * lejos / 8);
        if (g->bt.anim_dir) emb_r = -d; else emb_y = d;
    }
    if (g->bt.sacude) {
        int d = (g->bt.sacude & 1) ? 3 : -3;
        if (g->bt.sacude_quien) sac_y = d; else sac_r = d;
    }

    ch_robot_draw(b, RIVAL_CX + sac_r + emb_r, RIVAL_Y + bob, &g->bt.rival,
                  2, true, g->bt.anim && g->bt.anim_dir ? 1 : 0);
    ch_robot_draw(b, YO_CX + sac_y + emb_y, YO_Y + (bob ^ 1), &g->s.yo,
                  2, false, g->bt.anim && !g->bt.anim_dir ? 1 : 0);

    if (g->bt.anim && !g->bt.anim_estado) {
        if (paso >= 6 && g->bt.anim > ANIM_IMPACTO) {
            proyectil(g, (paso - 6) * 256 / 8);
        }
        if (g->bt.anim <= ANIM_IMPACTO) {
            /* The impact: two rings opening from the chest. */
            int r = (ANIM_IMPACTO - g->bt.anim) * 5 + 4;
            punto(!g->bt.anim_dir, &cx, &cy);
            ch_wave(b, cx, cy, r, 3, ctipo, 14 - r / 5);
            ch_wave(b, cx, cy, r / 2, 2, ch_rgb(0xFFFFFF), 12 - r / 6);
        }
    }

    for (int i = 0; i < NPART; i++) {
        if (!g->bt.part[i].vida) continue;
        ch_disc(b, g->bt.part[i].x / 4, g->bt.part[i].y / 4,
                g->bt.part[i].vida > 6 ? 2 : 1,
                g->bt.part[i].col ? ctipo : ch_rgb(0xFFFFFF));
    }

    /* The hit's flash: it lightens the whole arena for a frame or two. */
    if (g->bt.flash) {
        ch_shade(b, 0, 0, CH_W, CAJA_Y, g->bt.flash);
    }

    ch_clip_none(b);

    /* The bars, which come down by themselves, and the numbers. */
    ch_barra(b, PAN_R_X + 4, PAN_R_Y + 14, PAN_W - 8, g->bt.hp_ver[1],
             g->bt.rival.vida_max, ch_rgb(0x4ADE80));
    ch_barra(b, PAN_YO_X + 4, PAN_YO_Y + 14, PAN_W - 8, g->bt.hp_ver[0],
             g->s.yo.vida_max, ch_rgb(0x4ADE80));
    ch_barra(b, PAN_YO_X + 4, PAN_YO_Y + 32, PAN_W - 8, g->s.yo.ene,
             g->s.yo.ene_max, ch_rgb(0x4A9DF5));
    {
        char t[20];
        snprintf(t, sizeof(t), "%d/%d", g->bt.hp_ver[0], g->s.yo.vida_max);
        ch_rect(b, PAN_YO_X + 4, PAN_YO_Y + 22, PAN_W - 8, 8, ch_rgb(0x141824));
        ch_text(b, PAN_YO_X + 4, PAN_YO_Y + 23, t, ch_rgb(0xD5DCEB));
    }
    /* The statuses, as two little squares beside the name. Before, you only
     * found out by reading the panel on the turn they appeared, and three
     * turns later nobody remembered whether it was still burning. */
    for (int q = 0; q < 2; q++) {
        int x = (q ? PAN_R_X : PAN_YO_X) + PAN_W - 26;
        int y = (q ? PAN_R_Y : PAN_YO_Y) + 3;
        if (g->bt.quema[q]) {
            ch_rect(b, x, y, 5, 6, ch_rgb(0xFF6A0A));
            ch_rect(b, x + 1, y + 1, 3, 2, ch_rgb(0xFFE45E));
            x -= 7;
        }
        if (g->bt.corto[q]) {
            ch_rect(b, x, y, 5, 6, ch_rgb(0x1F4FBF));
            ch_rect(b, x + 2, y + 1, 1, 4, ch_rgb(0xFFE45E));
        }
    }
    ch_dirty_add(&g->d_cur, PAN_R_X + 3, PAN_R_Y + 2, PAN_W - 6, 19);
    ch_dirty_add(&g->d_cur, PAN_YO_X + 3, PAN_YO_Y + 2, PAN_W - 6, 39);

    /* The damage number, rising from the chest of whoever took it. */
    if (g->bt.dmg_t) {
        char t[10];
        int sube = (26 - g->bt.dmg_t) / 2;
        punto(g->bt.dmg_quien, &cx, &cy);
        int tw, tx, ty;
        snprintf(t, sizeof(t), "%d", g->bt.dmg_val);
        tw = ch_text_w(t);
        tx = cx - tw / 2;
        ty = cy - 18 - sube;
        /* With a dark plate behind. A white number with a shadow over the body
         * of a light robot cannot be read, and it is precisely the fact you
         * want to catch out of the corner of your eye. */
        ch_round(b, tx - 3, ty - 2, tw + 6, 11, 1, ch_rgb(0x05060C));
        ch_text(b, tx, ty, t, ch_rgb(0xFFE45E));
        ch_dirty_add(&g->d_cur, cx - 22, cy - 22 - sube, 44, 14);
    }

    /* The opening curtain: two black bands opening from the edge. A fight that
     * appears out of nowhere reads as a screen change; with the curtain it
     * reads as SOMETHING HAPPENED. */
    if (g->bt.entrada) {
        int alto = g->bt.entrada * (CAJA_Y / 2) / 12;
        ch_rect(b, 0, 0, CH_W, alto, ch_rgb(0x05060C));
        ch_rect(b, 0, CAJA_Y - alto, CH_W, alto, ch_rgb(0x05060C));
        animando = true;
    }

    /* While anything moves about the arena the whole arena is dirtied: that is
     * 24 thousand pixels, 14% of the screen, and it avoids carrying fourteen
     * particle rectangles that would overflow the dirty list. */
    if (animando) {
        ch_dirty_add(&g->d_cur, 0, 0, CH_W, CAJA_Y);
    } else {
        ch_dirty_add(&g->d_cur, RIVAL_CX - w / 2 - 6, RIVAL_Y - 12, w + 12, h + 16);
        ch_dirty_add(&g->d_cur, YO_CX - w / 2 - 6, YO_Y - 12, w + 12, h + 16);
    }
}
