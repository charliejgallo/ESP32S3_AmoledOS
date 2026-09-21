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
#define PAN_R_H      40         /* +14: la fila de piezas conocidas del rival */
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

/* --------------------------------------------------------------------------
 * THE MAIN MENU IS SIX CELLS, NOT FOUR
 *
 * With a team of three there is a fifth thing to do in a turn -bring another
 * robot out- and no room for it among four buttons without losing one. Three
 * columns of 56 give six cells of the same height as before, which is what
 * matters: the row got no shorter and the finger did not get any smaller.
 *
 * The sixth cell is NOT a button. It is the team: three lamps with the health
 * of each robot, in the one place you are already looking when you decide what
 * to do. The sub-lists -attacks, items, the team- keep the wide 2x2, because
 * there an attack's name has to fit.
 * -------------------------------------------------------------------------- */
#define B6_W         56
static const int B6_X[6] = { CAJA_X +  2, CAJA_X + 60, CAJA_X + 118,
                             CAJA_X +  2, CAJA_X + 60, CAJA_X + 118 };
static const int B6_Y[6] = { CAJA_Y +  2, CAJA_Y +  2, CAJA_Y +   2,
                             CAJA_Y + 23, CAJA_Y + 23, CAJA_Y +  23 };

enum { MP_ATACAR = 0, MP_OBJETO, MP_CAMBIAR, MP_ANALIZAR, MP_HUIR, MP_EQUIPO };

static const char *const MENU_PPAL[5] = { N_("ATACAR"), N_("OBJETO"),
                                          N_("CAMBIAR"), N_("ANALIZAR"),
                                          N_("HUIR") };

/* --------------------------------------------------------------------------
 * THE COMBAT'S DICE
 *
 * Against the machine they are the game's own, `g->rng`, as they always were.
 * Against another watch they have to come out of a generator SEEDED BY THE
 * HOST and read by nothing else: both watches run this same engine over the
 * same two choices, so if they roll the same numbers they reach the same
 * result and neither has to send one.
 *
 * Which is why only the five rolls that DECIDE something go through here -
 * damage spread, the critical, the burn, the miss, the side effect. The
 * particles and the prizes keep rolling on `g->rng`: the particles because
 * they change nothing and the prizes because they are worked out by the winner
 * alone, and a draw one side makes and the other does not is exactly how a
 * shared sequence comes apart.
 * -------------------------------------------------------------------------- */

static void sacar(ch_t *g, int slot, char *dst, size_t n);
static void sacar_rival(ch_t *g, int slot);

static int bt_rnd(ch_t *g, int n)
{
    return ch_rnd(g->bt.enlace ? &g->rng_bt : &g->rng, n);
}

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
    g->bt.truco = 0;
    g->bt.truco_usado = 0;
    g->bt.turnos = 0;
    if (jefe && zona >= 1 && zona <= ZONAS) {
        g->bt.truco = ch_trucos[zona - 1];
        /* Los dos que actuan AL ENTRAR lo hacen antes del primer turno: el
         * jugador tiene que ver que empezo en desventaja y decidir con eso. */
        if (g->bt.truco == TRUCO_QUEMA) g->bt.quema[0] = 5;
        if (g->bt.truco == TRUCO_CORTO) g->bt.corto[0] = 4;
    }
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

    base = base * (85 + bt_rnd(g, 16)) / 100;

    /* Critical: one in ten. It exists because turn-based combat with nearly
     * fixed damage turns into a sum and not a fight; the critical is what
     * makes the extra turn worth trying when you are losing. */
    g->bt.critico = 0;
    if (bt_rnd(g, 100) < 10) {
        base = base * 3 / 2;
        g->bt.critico = 1;
    }

    /* LA DIFICULTAD, en el unico lugar donde se puede tocar de una vez.
     *
     * No cambia estadisticas ni niveles -eso desincroniza el registro y el
     * juego de piezas-: cambia lo que DUELE. En facil pegas un 20% mas y te
     * pegan un 25% menos; en duro, al reves. Con eso una pelea que no sale se
     * vuelve ganable sin que el jugador tenga que entender por que. */
    /* LA FURIA: el subjefe pega mas cuanto menos vida le queda. Se nota sin
     * anunciarse, que es justo lo que se quiere de un jefe acorralado. */
    if (g->bt.truco == TRUCO_FURIA && at == &g->bt.rival &&
        at->vida * 3 <= at->vida_max) {
        base = base * 3 / 2;
    }
    {
        int d = ch_dificultad(&g->s);
        bool me_pegan = (at != &g->s.yo);
        if (d == DIF_FACIL) base = me_pegan ? base * 3 / 4 : base * 6 / 5;
        else if (d == DIF_DURO) base = me_pegan ? base * 5 / 4 : base * 5 / 6;
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

    /* 0xFF is "this side did not attack": it brought another robot out, and a
     * swap costs the whole turn. Saying it here -instead of with a flag and an
     * `if` around each half of the turn- means the rest of the engine never
     * learns that a turn can be empty. */
    if (mv == 0xFF) return false;

    l1[0] = l2[0] = l3[0] = 0;
    snprintf(l1, sizeof(l1), _("%s USA %s"),
             quien ? _("EL RIVAL") : _("TU ROBOT"), _(m->nombre));

    /* Short circuit: the turn is lost. */
    if (g->bt.corto[quien]) {
        g->bt.corto[quien]--;
        if (bt_rnd(g, 100) < 45) {
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

    if (bt_rnd(g, 100) >= m->precision) {
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

    /* EL TRUCO QUE SE RESUELVE AL GOLPEAR: el Capataz se cura con lo que
     * hace, y la Administradora pega dos veces cada tres turnos. */
    if (quien == 1 && d > 0) {
        if (g->bt.truco == TRUCO_DRENA) {
            int cura = d / 3;
            at->vida = (int16_t)(at->vida + cura);
            if (at->vida > at->vida_max) at->vida = at->vida_max;
            if (cura) snprintf(l3, sizeof(l3), _("SE CURA %d."), cura);
        } else if (g->bt.truco == TRUCO_DOBLE && (g->bt.turnos % 3) == 0) {
            int extra = d / 2;
            df->vida = (int16_t)(df->vida - extra);
            if (df->vida < 0) df->vida = 0;
            snprintf(l3, sizeof(l3), _("Y OTRA VEZ: %d MAS."), extra);
        }
    }

    /* The effect, if there is one and if it lands. */
    if (m->efecto != EF_NADA && bt_rnd(g, 100) < m->prob) {
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

/* The first standing robot on the other watch's bench, or -1. Both sides can
 * work this out -the bench never takes damage- which is what lets the
 * replacement happen without a message and without a choice. */
static int rival_de_reserva(const ch_t *g)
{
    for (int i = 1; i < g->lk.nequipo_e; i++) {
        if (g->lk.equipo_e[i].vida > 0) return i;
    }
    return -1;
}

static void victoria(ch_t *g)
{
    ch_robot_t *r = &g->bt.rival;
    char l2[40], l3[40];
    int exp = r->nivel * r->nivel * 3 / 2 + 12;
    int cred = r->nivel * 8 + ch_rnd(&g->rng, 20);
    int nv_antes = g->s.yo.nivel;

    /* Against another watch: their robot went down, but their team may not
     * have. The replacement is AUTOMATIC and the same on both watches -the
     * first one standing- because a choice here would be a message in the
     * middle of a turn, and the whole design of this battle is that nothing
     * travels but the two choices. */
    if (g->bt.enlace) {
        int slot = rival_de_reserva(g);
        if (slot > 0) {
            char l1[30];
            snprintf(l1, sizeof(l1), _("%s SE APAGO!"), ch_robot_nombre(r));
            sacar_rival(g, slot);
            snprintf(l2, sizeof(l2), _("SACAN A %s!"),
                     ch_robot_nombre(&g->bt.rival));
            g->bt.pend = CB_MENU;
            msg(g, l1, l2, "");
            ch_sfx(1500, 70);
            return;
        }
        g->s.victorias++;
        g->s.yo.exp += (uint32_t)exp;
        subir_nivel(g);
        g->bt.premio_pieza = 0xFF;
        snprintf(l2, sizeof(l2), _("GANAS %d EXP."), exp);
        g->bt.pend = CB_FIN;
        ch_snd_melodia(g, CH_MEL_VICTORIA);
        msg(g, _("GANASTE EL COMBATE!"), l2,
            g->s.yo.nivel > nv_antes ? _("SUBISTE DE NIVEL!") : "");
        return;
    }

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
        for (int i = 0; i < ch_mochila(&g->s); i++) {
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

/* --------------------------------------------------------------------------
 * ONE ROBOT FALLS, THE TEAM DOES NOT
 *
 * Until the team existed, your robot running out of health was the end of the
 * fight. Now it is the end of THAT ROBOT: if there is another one standing you
 * choose which comes out, and the screen that asks has no way back -
 * `forzado`- because there is nothing else you could be doing.
 *
 * It is only a loss when the three of them are down, and that is the branch
 * that keeps the old punishment: a quarter of the credits and the walk back to
 * the workshop.
 * -------------------------------------------------------------------------- */

static void relevo(ch_t *g)
{
    g->bt.fase   = CB_CAMBIO;
    g->bt.forzado = 1;
    g->bt.pend   = CB_MENU;
    g->rehacer_fondo = 1;
}

static void derrota(ch_t *g)
{
    int perdido;

    /* Against another watch nothing is lost and nobody is carried anywhere:
     * it is a friendly, and a friendly that charged you a quarter of your
     * credits would be played exactly once. */
    if (g->bt.enlace) {
        g->bt.pend = CB_FIN;
        g->bt.huir = 0;
        msg(g, _("TU EQUIPO SE APAGO."), _("GANARON ELLOS."), "");
        ch_snd_melodia(g, CH_MEL_DERROTA);
        return;
    }

    perdido = g->s.creditos / 4;
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

/* Your active robot just went down. If another one is standing, the fight goes
 * on with it; the loss is only when there is nothing left to send out. */
static void caer(ch_t *g)
{
    /* Over the link the replacement is automatic, for the same reason it is on
     * the other side: a choice in the middle of a turn would be a message the
     * lockstep does not have. Alone, you pick - there is nobody to keep in
     * step with. */
    if (g->bt.enlace) {
        int slot = ch_eq_otro_vivo(&g->s);
        if (slot > 0) {
            char l1[30], l2[30];
            snprintf(l1, sizeof(l1), _("%s SE APAGO!"), ch_robot_nombre(&g->s.yo));
            sacar(g, slot, l2, sizeof(l2));
            g->bt.pend = CB_MENU;
            msg(g, l1, l2, "");
            return;
        }
        derrota(g);
        return;
    }

    if (ch_eq_otro_vivo(&g->s) >= 0) {
        char l1[30];
        snprintf(l1, sizeof(l1), _("%s SE APAGO!"), ch_robot_nombre(&g->s.yo));
        g->bt.pend = CB_CAMBIO;
        msg(g, l1, _("SACA OTRO ROBOT."), "");
        ch_sfx(150, 200);
        return;
    }
    derrota(g);
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

    case CB_CAMBIO:
        relevo(g);
        return;

    case CB_ACCION:
        /* the second half: whoever did not attack first replies */
        g->bt.pend = CB_MENSAJE;
        if (g->bt.turno == 0) {
            if (atacar(g, 1, g->bt.mov_r)) { caer(g); return; }
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
        if (g->bt.enlace) { ch_lk_combate_fin(g); break; }
        /* Beating the champion opens the closing screen. It is told apart by
         * the room, like the music: it is the world's last. */
        if (g->bt.jefe && g->s.sala == ch_nsalas - 1 &&
            ch_flag(&g->s, 0) == false && g->bt.rival.vida <= 0) {
            g->modo = MODO_FINAL;
            g->rehacer_fondo = 1;
            break;
        }
        g->modo = MODO_MAPA;
        /* Y de vuelta al tema de la zona, que es de donde se venia: parar la
         * musica al salir del combate dejaba el mapa mudo otra vez. */
        g->mel_mapa = 0;
        if (g->bt.huir == 2) {
            ch_eq_curar(&g->s);         /* the workshop repairs the three   */
            ch_map_entrar(g, 0, 11, 14);        /* your house                */
        }
        ch_map_musica(g);
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
        if (g->s.yo.vida <= 0)      { caer(g);     return; }
        if (g->bt.rival.vida <= 0)  { victoria(g); return; }
        return;                     /* one message at a time                 */
    }

    g->bt.pend = CB_MENU;
    g->bt.fase = CB_MENU;
    g->bt.sel = 0;
    g->rehacer_fondo = 1;
}

/* Who moves first. A tie cannot be settled with "does the rival go first?":
 * that is the OPPOSITE question on the two watches and the same coin would
 * have both of them answering yes. Over the link the coin decides whether the
 * HOST goes first, which means the same thing on both sides, and each one
 * turns that into its own answer. */
static int orden(ch_t *g, int vj, int vr)
{
    if (vr != vj) return vr > vj;
    if (!g->bt.enlace) return ch_rnd(&g->rng, 2);
    return ch_rnd(&g->rng_bt, 2) ? !g->lk.host : (int)g->lk.host;
}

/* Brings out the robot in `slot`. Shared by the swap you choose and the one
 * you are forced into when yours falls. */
static void sacar(ch_t *g, int slot, char *dst, size_t n)
{
    ch_eq_activar(&g->s, slot);
    g->bt.et_atk[0] = g->bt.et_def[0] = 0;   /* the stages belong to the robot */
    g->bt.quema[0] = g->bt.corto[0] = 0;
    g->bt.hp_ver[0] = g->s.yo.vida;
    g->bt.forzado = 0;
    if (dst && n) snprintf(dst, n, _("SALE %s!"), ch_robot_nombre(&g->s.yo));
    ch_sfx(1500, 70);
}

/* The same, on the other watch's side of the arena. Their team is mirrored
 * here whole, so their swap is an index and this side already knows what came
 * out - the live health of the robot leaving goes back to the mirror first,
 * or it would come back later as good as new. */
static void sacar_rival(ch_t *g, int slot)
{
    if (slot <= 0 || slot >= g->lk.nequipo_e) return;
    g->lk.equipo_e[0] = g->bt.rival;
    {
        ch_robot_t t = g->lk.equipo_e[0];
        g->lk.equipo_e[0] = g->lk.equipo_e[slot];
        g->lk.equipo_e[slot] = t;
    }
    g->bt.rival = g->lk.equipo_e[0];
    g->bt.et_atk[1] = g->bt.et_def[1] = 0;
    g->bt.quema[1] = g->bt.corto[1] = 0;
    g->bt.hp_ver[1] = g->bt.rival.vida;
}

/* EL TRUCO, ANTES DEL TURNO.
 *
 * Los tres que dependen del estado -repararse, blindarse, pegar dos veces-
 * se resuelven aca y AVISAN por el panel: un jefe que hace algo raro y no lo
 * dice es un jefe que parece roto. El cuarto (furia) y el quinto (drena)
 * viven dentro del calculo del dano, que es donde se notan.
 */
static bool truco_antes(ch_t *g)
{
    ch_robot_t *r = &g->bt.rival;

    g->bt.turnos++;
    switch (g->bt.truco) {
    case TRUCO_REPARA:
        if (!g->bt.truco_usado && r->vida * 2 <= r->vida_max) {
            g->bt.truco_usado = 1;
            r->vida = (int16_t)(r->vida + r->vida_max / 2);
            if (r->vida > r->vida_max) r->vida = r->vida_max;
            msg(g, _("SE REPARA SOLO!"),
                        _("VUELVE A EMPEZAR."), "");
            return true;
        }
        break;
    case TRUCO_ESCUDO:
        if ((g->bt.turnos % 3) == 0 && g->bt.et_def[1] < 4) {
            g->bt.et_def[1]++;
            msg(g, _("SE BLINDA."), _("SUBE SU DEFENSA."), "");
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static void jugar_turno(ch_t *g, int mv)
{
    if (truco_antes(g)) {
        /* El truco se come el turno del rival: el jugador ve lo que hizo y
         * sigue jugando, que es lo contrario de un jefe que hace dos cosas a
         * la vez y no se entiende ninguna. */
        g->bt.pend = CB_MENU;
        return;
    }
    g->bt.mov_j = (uint8_t)mv;
    g->bt.mov_r = (uint8_t)elegir_rival(g);

    int vj = con_etapa(g->s.yo.vel, 0);
    int vr = con_etapa(g->bt.rival.vel, 0);
    g->bt.turno = (uint8_t)orden(g, vj, vr);

    g->bt.pend = CB_ACCION;
    if (g->bt.turno == 0) {
        if (atacar(g, 0, g->bt.mov_j)) { victoria(g); return; }
    } else {
        if (atacar(g, 1, g->bt.mov_r)) { caer(g); return; }
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

static int boton6_en(int bx, int by)
{
    for (int i = 0; i < 6; i++) {
        if (bx >= B6_X[i] && bx < B6_X[i] + B6_W &&
            by >= B6_Y[i] && by < B6_Y[i] + BOT_H) {
            return i;
        }
    }
    return -1;
}

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

    case CB_CAMBIO: {
        int n = ch_eq_n(&g->s);
        b = boton_en(bx, by);
        if (b < 0) return;
        if (b == 3 && n <= 3) {                 /* the VOLVER cell           */
            if (g->bt.forzado) { ch_sfx(220, 40); return; }
            ch_sfx(700, 30);
            g->bt.fase = CB_MENU;
            g->rehacer_fondo = 1;
            return;
        }
        if (b >= n || b == 0) { ch_sfx(220, 40); return; }
        {
            const ch_robot_t *r = ch_eq(&g->s, b);
            if (!r || r->vida <= 0) { ch_sfx(220, 40); return; }
        }
        ch_sfx(1100, 25);
        if (g->bt.forzado) {
            /* Forced: the robot that fell is replaced and the turn goes on
             * without the rival getting a free hit for it. */
            char l1[30];
            sacar(g, b, l1, sizeof(l1));
            g->bt.fase = CB_MENSAJE;
            g->bt.pend = CB_MENU;
            msg(g, l1, "", "");
            /* Over the link, the other watch has to be told which one came
             * out, or its mirror of this team stops matching. */
            if (g->bt.enlace) ch_lk_elegir(g, (uint8_t)(0x20 + b));
            return;
        }
        if (g->bt.enlace) { ch_lk_elegir(g, (uint8_t)(0x20 + b)); return; }
        sacar(g, b, NULL, 0);
        jugar_turno(g, 0xFF);                   /* the swap IS the turn      */
        return;
    }

    case CB_ESPERA:
        return;                                  /* the other watch's turn   */

    case CB_MENU:
        b = boton6_en(bx, by);
        if (b < 0 || b == MP_EQUIPO) return;
        if (b == MP_OBJETO && g->bt.enlace) { ch_sfx(220, 40); return; }
        if (b == MP_CAMBIAR && ch_eq_otro_vivo(&g->s) < 0) { ch_sfx(220, 40); return; }
        ch_sfx(1100, 25);
        if (b == MP_ATACAR)  { g->bt.fase = CB_ATAQUES; g->rehacer_fondo = 1; }
        else if (b == MP_OBJETO) { g->bt.fase = CB_OBJETOS; g->rehacer_fondo = 1; }
        else if (b == MP_CAMBIAR) {
            g->bt.fase = CB_CAMBIO;
            g->bt.forzado = 0;
            g->rehacer_fondo = 1;
        }
        else if (b == MP_ANALIZAR) {
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
        } else if (g->bt.enlace) {
            ch_sfx(220, 40);
        } else {
            huir(g);
        }
        return;

    case CB_ATAQUES:
        b = boton_en(bx, by);
        if (b < 0 || b >= g->s.yo.nmov) return;
        ch_sfx(1100, 25);
        /* Over the link nothing is resolved on the tap: the choice goes out
         * and the turn happens when both are known. A side that resolved its
         * own move on the tap would be a turn ahead of the other half the
         * time - Truco's lesson, and the reason its guest applies nothing. */
        if (g->bt.enlace) { ch_lk_elegir(g, (uint8_t)b); return; }
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

/* --------------------------------------------------------------------------
 * A TURN OF A BATTLE BETWEEN TWO WATCHES
 *
 * `mio` and `suyo` are the two choices of the turn, in the one byte each of
 * them travels as. Both watches call this with the same pair -the host after
 * hearing the guest, the guest after the host's echo- and from here on the
 * engine is the one that has always been there.
 *
 * Swaps resolve FIRST and both at once, then whoever is left attacks in order
 * of speed. It is the order the game this one comes from uses, and it is the
 * only one where choosing to swap is not a free turn of being hit.
 * -------------------------------------------------------------------------- */
void ch_bt_aplicar_enlace(ch_t *g, uint8_t mio, uint8_t suyo)
{
    int mv_j = 0xFF, mv_r = 0xFF;
    int vj, vr;
    char cambio[30];

    g->bt.nturno++;
    g->bt.eleccion   = 0xFF;
    g->bt.eleccion_e = 0xFF;
    cambio[0] = 0;

    if ((suyo & 0xF0) == 0x20) sacar_rival(g, suyo & 0x0F);
    if ((mio  & 0xF0) == 0x20) sacar(g, mio & 0x0F, cambio, sizeof(cambio));

    if (mio  < 0x10 && mio  < g->s.yo.nmov)     mv_j = g->s.yo.mov[mio];
    if (suyo < 0x10 && suyo < g->bt.rival.nmov) mv_r = g->bt.rival.mov[suyo];

    g->bt.mov_j = (uint8_t)mv_j;
    g->bt.mov_r = (uint8_t)mv_r;

    if (mv_j == 0xFF && mv_r == 0xFF) {         /* both swapped: nobody hits */
        g->bt.turno = 2;
        g->bt.pend  = CB_MENSAJE;
        msg(g, cambio[0] ? cambio : _("CAMBIAN DE ROBOT."),
            _("NADIE ATACA ESTE TURNO."), "");
        return;
    }

    vj = con_etapa(g->s.yo.vel, 0);
    vr = con_etapa(g->bt.rival.vel, 0);
    g->bt.turno = (uint8_t)orden(g, vj, vr);
    g->bt.pend  = CB_ACCION;
    if (g->bt.turno == 0) {
        if (atacar(g, 0, g->bt.mov_j)) { victoria(g); return; }
    } else {
        if (atacar(g, 1, g->bt.mov_r)) { caer(g); return; }
    }
    g->bt.pend = CB_ACCION;
}

/* The back gesture: from a sub-list to the menu. */
bool ch_bt_atras(ch_t *g)
{
    /* A forced swap has no way back: there is nothing else you could be doing
     * with a robot that has just gone down. */
    if (g->bt.fase == CB_CAMBIO && g->bt.forzado) return true;
    if (g->bt.fase == CB_ESPERA) return true;
    if (g->bt.fase == CB_ATAQUES || g->bt.fase == CB_OBJETOS ||
        g->bt.fase == CB_CAMBIO) {
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

    /* LAS PIEZAS DEL RIVAL, las que ya viste.
     *
     * El registro anotaba cada pieza vista y no servia para nada mas que para
     * mirarlo. Aca dice de que esta hecho el que tenes enfrente: cuatro
     * casillas con la letra de la categoria y el color de su tipo, y una
     * interrogacion donde todavia no viste esa pieza. Con el juego de piezas
     * -las del tipo del torso suman- eso es exactamente lo que hay que saber
     * antes de elegir el golpe, y de paso le da un para que al registro. */
    if (!mio) {
        static const char *const L[P_CATS] = { "C", "T", "B", "P" };
        for (int c = 0; c < P_CATS; c++) {
            uint8_t id = PIEZA_ID(c, r->pieza[c] % PVAR);
            const ch_part_t *p = &ch_partes[id];
            bool visto = ch_visto(&g->s, id);
            int px = x + 4 + c * 13, py = y + PAN_R_H - 13;

            ch_rect(b, px, py, 11, 10,
                    visto ? ch_rgb(ch_tipo_color[p->tipo % TIPOS])
                          : ch_rgb(0x232B41));
            ch_frame(b, px, py, 11, 10, ch_rgb(0x05060C));
            /* Letra blanca con sombra negra SIEMPRE: la tinta oscura sobre
             * el color del tipo se lee en los claros y desaparece en los
             * oscuros, y hay tipos de los dos. */
            ch_text_center(b, px + 5, py + 2, visto ? L[c] : "?",
                           visto ? ch_rgb(0xFFFFFF) : ch_rgb(0x8A93AB),
                           ch_rgb(0x05060C));
        }
        {   /* y si tiene juego, que es el +30% que no se ve venir */
            int n = ch_robot_juego(r);
            if (n >= 2) {
                char j[16];
                snprintf(j, sizeof(j), "x%d", n);
                ch_text(b, x + PAN_W - 4 - ch_text_w(j), y + PAN_R_H - 11, j,
                        ch_rgb(n >= 4 ? 0xFF4A3D : 0xFFE45E));
            }
        }
    }

    /* Only the bar's SLOT. The fill and the numbers are drawn by
     * ch_bt_dibujar() per frame, because they go down gradually: leaving them
     * in the background forced a full rebuild for every point of health. */
    ch_barra(b, x + 4, y + 14, PAN_W - 8, 0, 1, ch_rgb(0x4ADE80));
    if (mio) {
        ch_barra(b, x + 4, y + 32, PAN_W - 8, 0, 1, ch_rgb(0x4A9DF5));
    }
    (void)t;
}

static void boton_en_xy(ch_t *g, int x, int y, int w, const char *txt,
                        bool activo)
{
    ch_buf_t *b = &g->bg;
    uint16_t fondo = activo ? ch_rgb(0x2B3145) : ch_rgb(0x171B29);
    uint16_t borde = activo ? ch_rgb(0x8A93AB) : ch_rgb(0x3D465F);
    uint16_t tinta = activo ? ch_rgb(0xFFFFFF) : ch_rgb(0x606B85);

    ch_rect(b, x, y, w, BOT_H, fondo);
    ch_frame(b, x, y, w, BOT_H, borde);
    ch_text_center(b, x + w / 2, y + 6, txt, tinta, ch_rgb(0x05060C));
}

static void boton(ch_t *g, int i, const char *txt, bool activo)
{
    boton_en_xy(g, BOT_X[i], BOT_Y[i], BOT_W, txt, activo);
}

static void boton6(ch_t *g, int i, const char *txt, bool activo)
{
    boton_en_xy(g, B6_X[i], B6_Y[i], B6_W, txt, activo);
}

/* The team, in the sixth cell: one lamp per robot, as long as its health.
 * Green the one that is out, grey a reserve, red a wreck. */
static void celda_equipo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    int x = B6_X[MP_EQUIPO], y = B6_Y[MP_EQUIPO];
    int n = ch_eq_n(&g->s);

    ch_rect(b, x, y, B6_W, BOT_H, ch_rgb(0x171B29));
    ch_frame(b, x, y, B6_W, BOT_H, ch_rgb(0x3D465F));
    for (int i = 0; i < n; i++) {
        const ch_robot_t *r = ch_eq(&g->s, i);
        int by = y + 3 + i * 5;
        int v = r && r->vida_max ? r->vida * (B6_W - 12) / r->vida_max : 0;
        uint16_t c = !r || r->vida <= 0 ? ch_rgb(0xE05252)
                   : (i == 0 ? ch_rgb(0x4ADE80) : ch_rgb(0x8A93AB));
        ch_rect(b, x + 5, by, B6_W - 12, 3, ch_rgb(0x2B3145));
        if (v > 0) ch_rect(b, x + 5, by, v, 3, c);
    }
}

/* --------------------------------------------------------------------------
 * THE ARENA, ONE PER ZONE
 *
 * The combat used to happen in the same purple dusk everywhere, which meant
 * that after two hours the port, the foundry and the ice valley all looked
 * like the same fight. Eight arenas is a TABLE of eight rows plus one painter
 * per kind of scenery, and every one of them is drawn ONCE, into the
 * background, when the phase changes.
 *
 * That last sentence is the whole licence for this section. The background is
 * the only place in this engine where detail is free: what costs a frame is
 * the dirty rectangles pushed on top of it, and the arena pushes none. So it
 * can afford gradients, a skyline, two hundred dots of snow - things that
 * would be unthinkable if they had to be redrawn thirty times a second.
 *
 * The scenery is drawn from a FIXED seed, so the same zone always looks the
 * same: it is a place you recognise, not noise that reshuffles every fight.
 * -------------------------------------------------------------------------- */

enum {
    DECO_NADA = 0,
    DECO_ESTRELLAS,     /* the first town, at dusk                           */
    DECO_OLAS,          /* the port                                          */
    DECO_CIRCUITO,      /* Alto Voltio: a grid with lit nodes                */
    DECO_BRASAS,        /* the foundry                                       */
    DECO_NIEVE,         /* Criovalle                                         */
    DECO_TORRES,        /* Ciudad Malla: a skyline                           */
    DECO_AGUJAS,        /* Villa Oxido: rusted spires                        */
    DECO_CRISTALES,     /* Prisma                                            */
};

typedef struct {
    uint32_t cielo_a, cielo_b;      /* sky, top and bottom                   */
    uint32_t suelo_a, suelo_b;      /* ground, near the horizon and far down */
    uint32_t horizonte;
    uint32_t plato, plato_alto;     /* the two discs each robot stands on    */
    uint8_t  deco;
    uint32_t deco_c;
} ch_arena_t;

/* Indexed by zone MINUS ONE; a battle with no zone (the phone booth falls back
 * to the room you are standing in) lands on row 0. */
static const ch_arena_t ARENAS[ZONAS] = {
    /* 1 Villa Tuerca - dusk over the fields                                 */
    { 0x1B2340, 0x3B3A62, 0x4A4162, 0x241E33, 0x6A5F8C,
      0x5A5178, 0x6E648F, DECO_ESTRELLAS, 0xD5DCEB },
    /* 2 Puerto Bujia - sea and a low sun                                    */
    { 0x0E2A4A, 0x2E6E96, 0x1C4E63, 0x0A2030, 0x7FD4E8,
      0x2A5A70, 0x3E7A92, DECO_OLAS, 0x9FE3F2 },
    /* 3 Alto Voltio - the substation at night                               */
    { 0x160B2E, 0x3A1E63, 0x2A1A46, 0x120A22, 0x9A6CF0,
      0x3A2A5E, 0x54407E, DECO_CIRCUITO, 0x7BE9FF },
    /* 4 Fundicion - the furnace                                             */
    { 0x2A0E06, 0x7A2A0C, 0x5A2008, 0x260A04, 0xFF9F0A,
      0x5E2A14, 0x7E3E1E, DECO_BRASAS, 0xFF6A1E },
    /* 5 Criovalle - the one daylit arena, but not so bright that a pale grey
     * robot disappears into it: the sky is a cold mid blue and the snow does
     * the lifting. */
    { 0x2E5A80, 0x6E9CBC, 0x7E9EB2, 0x3E5468, 0xDCEEF8,
      0x4E6E86, 0x7EA2BC, DECO_NIEVE, 0xFFFFFF },
    /* 6 Ciudad Malla - a skyline of masts                                   */
    { 0x08201E, 0x104A44, 0x0E3A36, 0x061A18, 0x2AF0C8,
      0x125248, 0x1E7A6C, DECO_TORRES, 0x2AF0C8 },
    /* 7 Villa Oxido - dust and rust                                         */
    { 0x3A2410, 0x8A5E28, 0x6E4A20, 0x2E1E0C, 0xC9A96A,
      0x5E4018, 0x7E5A28, DECO_AGUJAS, 0xC06B2E },
    /* 8 Prisma - the summit                                                 */
    { 0x14082E, 0x4A1E7E, 0x32155A, 0x160828, 0xE0A8FF,
      0x3E2068, 0x5E3A92, DECO_CRISTALES, 0xB072F0 },
};

/* A generator of its own, seeded per zone: the scenery is the same every time
 * you fight there. Using the game's `rng` would reshuffle the skyline on every
 * battle, and worse, would pull the combat's dice along with it. */
static void arena_deco(ch_t *g, const ch_arena_t *a, int zona)
{
    ch_buf_t *b = &g->bg;
    uint32_t r = 0x5EED0000u + (uint32_t)zona * 2654435761u;
    uint16_t c = ch_rgb(a->deco_c);

    switch (a->deco) {

    case DECO_ESTRELLAS:
        for (int i = 0; i < 40; i++) {
            int x = ch_rnd(&r, CH_W), y = ch_rnd(&r, 84);
            ch_px(b, x, y, ch_tone(c, 40 + ch_rnd(&r, 60)));
        }
        break;

    case DECO_OLAS:
        /* Four crests, flatter and paler the further away they are. */
        for (int k = 0; k < 4; k++) {
            int y = 62 + k * 8;
            uint16_t cc = ch_tone(c, 30 + k * 18);
            for (int x = 0; x < CH_W; x += 2) {
                int d = ((x / 2 + k * 3) % 8 < 4) ? 0 : 1;
                ch_rect(b, x, y + d, 2, 1, cc);
            }
        }
        break;

    case DECO_CIRCUITO:
        for (int x = 8; x < CH_W; x += 16) ch_vline(b, x, 0, 94, ch_tone(c, 22));
        for (int y = 12; y < 94; y += 16) ch_hline(b, 0, y, CH_W, ch_tone(c, 22));
        for (int i = 0; i < 12; i++) {
            int x = 8 + ch_rnd(&r, 11) * 16, y = 12 + ch_rnd(&r, 6) * 16;
            ch_rect(b, x - 1, y - 1, 3, 3, c);
        }
        break;

    case DECO_BRASAS:
        ch_glow(b, CH_W / 2, 96, 60, ch_rgb(0xFF9F0A), 70);
        for (int i = 0; i < 34; i++) {
            int x = ch_rnd(&r, CH_W), y = 20 + ch_rnd(&r, 74);
            int s = 1 + (ch_rnd(&r, 10) == 0);
            ch_rect(b, x, y, s, s, ch_tone(c, 50 + ch_rnd(&r, 50)));
        }
        break;

    case DECO_NIEVE:
        for (int i = 0; i < 70; i++) {
            int x = ch_rnd(&r, CH_W), y = ch_rnd(&r, 94);
            ch_px(b, x, y, ch_tone(c, 50 + ch_rnd(&r, 50)));
        }
        break;

    case DECO_TORRES: {
        /* A skyline: the masts go BEHIND the horizon line, which is what
         * makes them read as far away instead of as bars on the floor. */
        int x = -4;
        while (x < CH_W) {
            int w = 6 + ch_rnd(&r, 10);
            int h = 18 + ch_rnd(&r, 44);
            ch_rect(b, x, 94 - h, w, h, ch_tone(c, 16 + ch_rnd(&r, 14)));
            for (int y = 94 - h + 4; y < 92; y += 7) {
                if (ch_rnd(&r, 3)) ch_rect(b, x + 2, y, 2, 2, ch_tone(c, 70));
            }
            x += w + 2 + ch_rnd(&r, 6);
        }
        break;
    }

    case DECO_AGUJAS:
        for (int i = 0; i < 9; i++) {
            int x = 6 + ch_rnd(&r, CH_W - 12);
            int h = 16 + ch_rnd(&r, 40);
            uint16_t cc = ch_tone(c, 24 + ch_rnd(&r, 22));
            for (int k = 0; k < h; k++) {
                int w = 1 + (h - k) / 12;
                ch_rect(b, x - w / 2, 94 - h + k, w, 1, cc);
            }
        }
        for (int i = 0; i < 26; i++) {           /* dust                     */
            ch_px(b, ch_rnd(&r, CH_W), 30 + ch_rnd(&r, 60), ch_tone(c, 40));
        }
        break;

    case DECO_CRISTALES:
        for (int i = 0; i < 11; i++) {
            int x = 4 + ch_rnd(&r, CH_W - 8);
            int h = 20 + ch_rnd(&r, 46);
            int w = 4 + ch_rnd(&r, 7);
            uint16_t cc = ch_tone(c, 26 + ch_rnd(&r, 30));
            for (int k = 0; k < h; k++) {
                int ww = w * (h - k) / h + 1;
                ch_rect(b, x - ww / 2, 94 - h + k, ww, 1, cc);
            }
            ch_vline(b, x, 94 - h + 2, h - 4, ch_tone(c, 85));
        }
        break;

    default:
        break;
    }
}

void ch_bt_fondo(ch_t *g)
{
    ch_buf_t *b = &g->bg;
    const ch_arena_t *a = &ARENAS[(g->bt.zona ? g->bt.zona - 1 : 0) % ZONAS];

    /* The arena: sky, scenery, horizon and floor. It is the only decorative
     * part and that is why it can afford all this: it is drawn once per phase
     * and pays not one dirty rectangle afterwards. */
    ch_vgrad(b, 0, 0, CH_W, 96, ch_rgb(a->cielo_a), ch_rgb(a->cielo_b));
    arena_deco(g, a, g->bt.zona);
    ch_vgrad(b, 0, 96, CH_W, 176, ch_rgb(a->suelo_a), ch_rgb(a->suelo_b));
    ch_rect(b, 0, 94, CH_W, 2, ch_rgb(a->horizonte));

    /* Two platforms, one per robot. They go in the BACKGROUND and not with the
     * robot: they do not move, so there is no reason for them to pay a dirty
     * rectangle per frame. */
    ch_disc(b, RIVAL_CX, RIVAL_Y + 80, 30, ch_rgb(a->plato));
    ch_disc(b, RIVAL_CX, RIVAL_Y + 78, 28, ch_rgb(a->plato_alto));
    ch_disc(b, YO_CX, YO_Y + 82, 36, ch_rgb(a->plato));
    ch_disc(b, YO_CX, YO_Y + 80, 34, ch_rgb(a->plato_alto));

    panel_robot(g, PAN_R_X,  PAN_R_Y,  &g->bt.rival, false);
    panel_robot(g, PAN_YO_X, PAN_YO_Y, &g->s.yo,     true);

    /* The box at the bottom */
    ch_panel(b, CAJA_X, CAJA_Y, CAJA_W, CAJA_H, ch_rgb(0x8A93AB));

    switch (g->bt.fase) {
    case CB_MENU:
        /* Swapping needs a reserve standing; running away is not an option
         * against a boss or against another watch. A button that cannot be
         * used says so by being grey, which is cheaper than a message
         * explaining it after the tap. */
        boton6(g, MP_ATACAR,   _(MENU_PPAL[MP_ATACAR]),   true);
        boton6(g, MP_OBJETO,   _(MENU_PPAL[MP_OBJETO]),   !g->bt.enlace);
        boton6(g, MP_CAMBIAR,  _(MENU_PPAL[MP_CAMBIAR]),
               ch_eq_otro_vivo(&g->s) >= 0);
        boton6(g, MP_ANALIZAR, _(MENU_PPAL[MP_ANALIZAR]), true);
        boton6(g, MP_HUIR,     _(MENU_PPAL[MP_HUIR]),     !g->bt.jefe && !g->bt.enlace);
        celda_equipo(g);
        break;

    case CB_CAMBIO:
        for (int i = 0; i < 4; i++) {
            const ch_robot_t *r = i < ch_eq_n(&g->s) ? ch_eq(&g->s, i) : NULL;
            char t[26];
            if (!r) { boton(g, i, i == 3 ? _("VOLVER") : "-", i == 3 && !g->bt.forzado); continue; }
            snprintf(t, sizeof(t), "%s %d/%d", ch_robot_nombre(r),
                     r->vida, r->vida_max);
            boton(g, i, t, i != 0 && r->vida > 0);
        }
        break;

    case CB_ESPERA:
        boton(g, 0, _("ESPERANDO..."), false);
        boton(g, 1, "", false);
        boton(g, 2, "", false);
        boton(g, 3, "", false);
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
    /* THE IDLE.
     *
     * This used to be a one-pixel square wave, and the measurement is what
     * justifies replacing it: in combat the engine pushes 40 % of the screen
     * EVERY frame, idle included, because the two robot boxes are dirtied
     * whether or not anything moved. That budget is already spent. Everything
     * drawn inside those boxes is therefore free, and what was being bought
     * with it was one pixel of bob.
     *
     * So: a three-step breath of about a second, the two robots out of phase
     * so they do not look like one object, the shadow narrowing underneath
     * (ch_robot_draw's `flota`), and a robot under a quarter of its health
     * throwing sparks - which is not decoration, it is the one thing in this
     * screen that says "this is about to end" without reading a number. */
    static const int8_t RESPIRO[8] = { 0, 1, 2, 2, 2, 1, 0, 0 };
    int fase_r = RESPIRO[(g->cuadro >> 3) & 7];
    int fase_y = RESPIRO[((g->cuadro >> 3) + 4) & 7];
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

    /* While a hit is playing the robot is being thrown about; breathing on
     * top of that just makes it jitter. */
    if (g->bt.anim || g->bt.sacude) fase_r = fase_y = 0;

    ch_robot_draw(b, RIVAL_CX + sac_r + emb_r, RIVAL_Y - fase_r, &g->bt.rival,
                  2, true, g->bt.anim && g->bt.anim_dir ? 1 : 0, fase_r);
    ch_robot_draw(b, YO_CX + sac_y + emb_y, YO_Y - fase_y, &g->s.yo,
                  2, false, g->bt.anim && !g->bt.anim_dir ? 1 : 0, fase_y);

    /* A robot below a quarter of its health throws sparks out of its chest.
     * The dice are the GAME's and never the combat's: over the link both
     * watches must roll the same numbers for the same reasons, and a spark is
     * not one of them. */
    for (int q = 0; q < 2; q++) {
        const ch_robot_t *r = q ? &g->bt.rival : &g->s.yo;
        if (r->vida <= 0 || r->vida * 4 >= r->vida_max) continue;
        punto(q, &cx, &cy);
        for (int i = 0; i < 2; i++) {
            if (ch_rnd(&g->rng, 3)) continue;
            int sx = cx - 8 + ch_rnd(&g->rng, 17);
            int sy = cy - 6 + ch_rnd(&g->rng, 13);
            ch_rect(b, sx, sy, 1, 1 + (int)ch_rnd(&g->rng, 2),
                    ch_rnd(&g->rng, 2) ? ch_rgb(0xFFE45E) : ch_rgb(0xFF8A3D));
        }
        /* and every so often the whole body browns out for a frame */
        if (((g->cuadro + q * 17) % 47) == 0) {
            ch_shade(b, cx - 26, cy - 24, 52, 48, -3);
        }
    }

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
    /* LAS ETAPAS DE ATAQUE Y DEFENSA.
     *
     * Subir o bajar una etapa es +-33% de dano, o sea la mitad de la pelea, y
     * lo unico que lo decia era una linea de texto en el turno en que pasaba:
     * tres turnos despues nadie se acuerda de si sigue con el ataque bajo. Va
     * al lado de las barras, en el color del signo, y solo cuando no es cero.
     * Mismo criterio que la quemadura, que ya estaba resuelta asi. */
    for (int q = 0; q < 2; q++) {
        char t[16];
        int a = g->bt.et_atk[q], d2 = g->bt.et_def[q];
        int x = (q ? PAN_R_X : PAN_YO_X) + 4;
        int y = q ? PAN_R_Y + 21 : PAN_YO_Y + 23;

        if (!q) x += 44;                        /* el mio, al lado del 39/39 */
        if (a) {
            snprintf(t, sizeof(t), "A%+d", a);
            ch_text(b, x, y, t, a > 0 ? ch_rgb(0x4ADE80) : ch_rgb(0xFF4A3D));
            x += ch_text_w(t) + 5;
        }
        if (d2) {
            snprintf(t, sizeof(t), "D%+d", d2);
            ch_text(b, x, y, t, d2 > 0 ? ch_rgb(0x4ADE80) : ch_rgb(0xFF4A3D));
        }
    }
    ch_dirty_add(&g->d_cur, PAN_R_X + 3, PAN_R_Y + 2, PAN_W - 6, 24);
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
        /* ONLY WHAT CHANGED.
         *
         * Measured on the board: 29.4 fps on the map, where 2 % of the screen
         * is pushed, against 22.4 in combat, where 40 % is. And that 40 % is
         * two robot boxes repainted every frame whether or not a single pixel
         * of them differs - which, between two steps of a breath that moves
         * every eighth frame, it does not.
         *
         * So each robot carries a signature of everything that can change how
         * it looks, and its box is dirtied when the signature moves. A robot
         * throwing sparks changes every frame by definition and says so.
         *
         * The frame AFTER a change still pushes the box, because `d_prev` is
         * what gets restored and pushed next time round: that is not a leak,
         * it is how the engine erases what it drew. */
        static const int BX[2] = { YO_CX, RIVAL_CX };
        static const int BY[2] = { YO_Y,  RIVAL_Y  };
        for (int q = 0; q < 2; q++) {
            const ch_robot_t *r = q ? &g->bt.rival : &g->s.yo;
            bool chispea = r->vida > 0 && r->vida * 4 < r->vida_max;
            uint32_t f = (uint32_t)((q ? fase_r : fase_y) & 7)
                       | (uint32_t)((q ? sac_r : sac_y) + 8) << 4
                       | (uint32_t)((q ? emb_r : emb_y) + 16) << 10
                       /* all four parts, so a robot coming out in place of
                        * another is never mistaken for the same one */
                       | (uint32_t)((r->pieza[0] ^ (r->pieza[1] * 3) ^
                                     (r->pieza[2] * 7) ^ (r->pieza[3] * 11))
                                    & 15) << 17
                       | (uint32_t)(chispea ? g->cuadro : 0) << 21;
            if (f == g->bt.firma[q]) continue;
            g->bt.firma[q] = f;
            ch_dirty_add(&g->d_cur, BX[q] - w / 2 - 6, BY[q] - 12, w + 12, h + 16);
        }
    }
}
