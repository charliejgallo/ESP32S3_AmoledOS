/*
 * CHATARRA - the parts, the attacks and the robot that comes from adding them
 *
 * Here is the game's central idea: a robot is FOUR parts -head, torso, arms
 * and legs- and each one contributes shape, stats, type and attacks. Sixteen
 * variants per category give 65,536 robots and not one of them is written down
 * anywhere.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE ARE NO SPRITES
 * ---------------------------------------------------------------------------
 *
 * The obvious temptation is to draw 64 ASCII sprites and blit them. It is not
 * done, for two reasons that reinforce each other:
 *
 *   1. The COLOUR has to be an argument. An opponent and the player's robot
 *      may wear the same head in two different schemes; with sprites you would
 *      need a copy per colour or you would have to invent tinting. It is the
 *      same reasoning as cjump's costumes (apps/cjump/main/cj_skins.c).
 *   2. So does the SCALE. The same robot is drawn x1 on the workshop's card
 *      and x2 in combat. With primitives that is a multiplier; with sprites,
 *      two sets of data.
 *
 * So each part is a style number (0..15) and a few trait tables -shape,
 * detail, finish- that the drawing functions interpret. Each table is 16 bytes
 * and lives in .rodata, that is, in PSRAM: free. The code that reads them is
 * four functions and is paid for once.
 *
 * ---------------------------------------------------------------------------
 * THE PEN
 * ---------------------------------------------------------------------------
 *
 * Everything is drawn in units of the 26x40 box and the pen multiplies by the
 * scale. No writing "esc *" on every call: that is where the one-pixel errors
 * come from that on the board show up as a trail stuck on the screen.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Random numbers
 *
 * There is no rand() in the firmware's symbol table, so we bring our own.
 * xorshift32: a few instructions and more than enough for an RPG.
 * -------------------------------------------------------------------------- */

uint32_t ch_rand(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

int ch_rnd(uint32_t *rng, int n)
{
    return n > 0 ? (int)(ch_rand(rng) % (uint32_t)n) : 0;
}

/* --------------------------------------------------------------------------
 * Elemental types
 *
 * Six, in two crossed triangles. The table is in EIGHTHS: 8 is normal, 16
 * double, 4 half. In eighths and not in percent so the damage calculation ends
 * in a shift and not in a division.
 * -------------------------------------------------------------------------- */

const char *const ch_tipo_nombre[TIPOS] = {
    N_("IMPACTO"), N_("PLASMA"), N_("FUEGO"),
    N_("CRIO"),    N_("VOLT"),   N_("ACIDO"),
};

const uint32_t ch_tipo_color[TIPOS] = {
    0xD5DCEB, 0x18A6D8, 0xFF6A0A, 0x7BE9FF, 0xFFE45E, 0x4ADE80,
};

/* Rows: attacker. Columns: defender. */
static const uint8_t EFEC[TIPOS][TIPOS] = {
    /*             IMP  PLA  FUE  CRI  VOL  ACI */
    /* IMPACTO */ {  8,   4,   8,  16,   8,  16 },
    /* PLASMA  */ { 16,   8,   8,   4,  16,   8 },
    /* FUEGO   */ {  8,   8,   8,  16,   4,  16 },
    /* CRIO    */ {  4,  16,   4,   8,  16,   8 },
    /* VOLT    */ { 16,   4,  16,   8,   8,   4 },
    /* ACIDO   */ {  4,  16,   4,   8,  16,   8 },
};

uint8_t ch_efectividad(int a, int d)
{
    if (a < 0 || a >= TIPOS || d < 0 || d >= TIPOS) {
        return 8;
    }
    return EFEC[a][d];
}

/* --------------------------------------------------------------------------
 * The attacks
 *
 * Number 0 is the basic hit and every robot has it even if its parts
 * contribute nothing: that way there is never a case of having nothing to
 * choose. Which is why the parts use 0 as "this part contributes no attack".
 * -------------------------------------------------------------------------- */

const ch_move_t ch_moves[MOVES] = {
    /*  name                 type          pow cst acc effect      prob */
    { N_("Golpe"),           TIPO_IMPACTO,  35,  0, 100, EF_NADA,      0 },

    /* impact */
    { N_("Martillazo"),      TIPO_IMPACTO,  70,  6,  85, EF_NADA,      0 },
    { N_("Embestida"),       TIPO_IMPACTO,  55,  4,  95, EF_NADA,      0 },
    { N_("Doble Puno"),      TIPO_IMPACTO,  45,  4, 100, EF_BAJA_DEF, 25 },
    { N_("Aplastar"),        TIPO_IMPACTO,  85,  9,  80, EF_NADA,      0 },
    { N_("Barrido"),         TIPO_IMPACTO,  40,  3, 100, EF_BAJA_ATK, 30 },
    { N_("Pisoton"),         TIPO_IMPACTO,  60,  5,  90, EF_NADA,      0 },
    { N_("Cabezazo"),        TIPO_IMPACTO,  50,  4,  95, EF_CORTO,    15 },

    /* plasma */
    { N_("Rayo Plasma"),     TIPO_PLASMA,   50,  5, 100, EF_NADA,      0 },
    { N_("Lanza Ionica"),    TIPO_PLASMA,   75,  8,  90, EF_NADA,      0 },
    { N_("Pulso"),           TIPO_PLASMA,   40,  3, 100, EF_NADA,      0 },
    { N_("Sobrecarga"),      TIPO_PLASMA,   95, 12,  75, EF_BAJA_ATK, 40 },
    { N_("Haz Guia"),        TIPO_PLASMA,   45,  4, 100, EF_NADA,      0 },
    { N_("Absorber Carga"),  TIPO_PLASMA,   50,  6, 100, EF_DRENA,   100 },

    /* fire */
    { N_("Lanzallamas"),     TIPO_FUEGO,    65,  7,  90, EF_QUEMA,    20 },
    { N_("Chispa Termica"),  TIPO_FUEGO,    35,  2, 100, EF_QUEMA,    10 },
    { N_("Fundicion"),       TIPO_FUEGO,    85, 10,  80, EF_BAJA_DEF, 30 },
    { N_("Soplete"),         TIPO_FUEGO,    50,  5,  95, EF_QUEMA,    35 },
    { N_("Horno"),           TIPO_FUEGO,    75,  9,  85, EF_NADA,      0 },

    /* cryo */
    { N_("Escarcha"),        TIPO_CRIO,     40,  3, 100, EF_NADA,      0 },
    { N_("Nitrogeno"),       TIPO_CRIO,     70,  7,  90, EF_CORTO,    20 },
    { N_("Congelador"),      TIPO_CRIO,     85, 10,  80, EF_NADA,      0 },
    { N_("Viento Polar"),    TIPO_CRIO,     50,  4,  95, EF_BAJA_ATK, 25 },
    { N_("Rocio Helado"),    TIPO_CRIO,     60,  6,  90, EF_NADA,      0 },

    /* volt */
    { N_("Descarga"),        TIPO_VOLT,     55,  5,  95, EF_CORTO,    20 },
    { N_("Chispazo"),        TIPO_VOLT,     35,  2, 100, EF_CORTO,    10 },
    { N_("Tormenta"),        TIPO_VOLT,     80,  9,  80, EF_CORTO,    30 },
    { N_("Electroiman"),     TIPO_VOLT,     45,  4, 100, EF_BAJA_DEF, 30 },
    { N_("Cortocircuito"),   TIPO_VOLT,     65,  7,  85, EF_CORTO,    40 },

    /* acid */
    { N_("Salpicon"),        TIPO_ACIDO,    40,  3, 100, EF_NADA,      0 },
    { N_("Corrosion"),       TIPO_ACIDO,    65,  6,  90, EF_BAJA_DEF, 35 },
    { N_("Bidon Acido"),     TIPO_ACIDO,    85, 10,  80, EF_NADA,      0 },
    { N_("Oxido"),           TIPO_ACIDO,     0,  4, 100, EF_BAJA_DEF,100 },
    { N_("Parasito"),        TIPO_ACIDO,    45,  5, 100, EF_DRENA,   100 },
    { N_("Vapor Toxico"),    TIPO_ACIDO,    55,  5,  95, EF_QUEMA,    25 },

    /* status */
    { N_("Diagnostico"),     TIPO_IMPACTO,   0,  3, 100, EF_SUBE_ATK,100 },
    { N_("Blindaje"),        TIPO_IMPACTO,   0,  3, 100, EF_SUBE_DEF,100 },
    { N_("Recarga"),         TIPO_PLASMA,    0,  0, 100, EF_CARGA,   100 },
    { N_("Autoreparar"),     TIPO_IMPACTO,   0, 10, 100, EF_REPARA,  100 },
    { N_("Mira Laser"),      TIPO_PLASMA,    0,  3, 100, EF_SUBE_ATK,100 },
    { N_("Placa Extra"),     TIPO_IMPACTO,   0,  4, 100, EF_SUBE_DEF,100 },
    { N_("Refrigerar"),      TIPO_CRIO,      0,  3, 100, EF_SUBE_DEF,100 },
    { N_("Turbo"),           TIPO_VOLT,      0,  4, 100, EF_SUBE_ATK,100 },
    { N_("Purga"),           TIPO_ACIDO,     0,  5, 100, EF_BAJA_ATK,100 },
};

/* --------------------------------------------------------------------------
 * The items
 * -------------------------------------------------------------------------- */

const ch_item_t ch_items[ITEMS] = {
    /*  name               description                        price cbt val */
    { N_(""),             N_(""),                                 0, 0,   0 },
    { N_("Aceite"),       N_("Repara 40 de vida."),              60, 1,  40 },
    { N_("Aceite Puro"),  N_("Repara 120 de vida."),            180, 1, 120 },
    { N_("Bateria"),      N_("Devuelve 30 de energia."),         80, 1,  30 },
    { N_("Bateria Densa"),    N_("Devuelve toda la energia."),      250, 1, 200 },
    { N_("Soldador"),     N_("Revive con media vida."),         400, 1,   0 },
    { N_("Chip de Datos"),    N_("Suma 300 de experiencia."),       300, 0,   0 },
    { N_("Iman de Rescate"),  N_("Mas chance de arrancar una pieza."),  150, 1,   0 },
    { N_("Llave Oxidada"),    N_("Abre una puerta trabada."),         0, 0,   0 },
    { N_("Pase de Sector"),   N_("Permite cruzar un control."),       0, 0,   0 },

    /* The errand items. They are not sold and not used: they exist so a
     * character recognises them. Mind this table: it is declared [ITEMS] and C
     * fills the gaps with ZEROS, so adding an IT_ to the enum and forgetting
     * the row here gives no error at all -it leaves a NULL name and blows up
     * on being drawn-. It happened: seven items were left without a row.
     * ch_map_check() now checks for it. */
    { N_("Caja de Tornillos"), N_("Alguien la esta buscando."),       0, 0,   0 },
    { N_("Ancla Oxidada"),  N_("Pesa como para hundir un barco."),    0, 0,   0 },
    { N_("Fusible Grueso"), N_("Aguanta lo que le tires."),           0, 0,   0 },
    { N_("Molde de Acero"), N_("Sigue caliente."),                    0, 0,   0 },
    { N_("Termo"),          N_("Todavia tiene agua caliente."),       0, 0,   0 },
    { N_("Clave Maestra"),  N_("Abre lo que no deberia."),            0, 0,   0 },
    { N_("Engranaje Grande"), N_("De una maquina que ya no existe."), 0, 0,   0 },
};

/* --------------------------------------------------------------------------
 * Colour schemes
 *
 * Each robot picks one. The parts carry no colour: they receive it. Twelve
 * schemes over sixteen head shapes give nearly two hundred heads
 * distinguishable at a glance, which is the whole point.
 * -------------------------------------------------------------------------- */

const ch_skin_t ch_skins[SKINS] = {
    /*  light     mid       dark      highlight (visors and cores) */
    { 0x9CC8F7, 0x4A7BC8, 0x24406E, 0x7BE9FF },   /* blue       */
    { 0xA8E890, 0x4FA85C, 0x235C30, 0xC8FF8A },   /* green      */
    { 0xF7B25C, 0xC8792A, 0x6E4014, 0xFFE45E },   /* amber      */
    { 0xF29A94, 0xC04A40, 0x6E221C, 0xFF9F8A },   /* red        */
    { 0xCDB4F0, 0x8158C0, 0x452C6E, 0xE0A8FF },   /* violet     */
    { 0xD5DCEB, 0x8A93AB, 0x454C60, 0xFFFFFF },   /* steel      */
    { 0x8EE6DC, 0x2FA396, 0x125650, 0x6FFFEE },   /* turquoise  */
    { 0xEBC9A0, 0xA07A4E, 0x513A22, 0xFFDFA8 },   /* bronze     */
    { 0xF4A8CE, 0xC0407E, 0x6B1F44, 0xFFC0E0 },   /* pink       */
    { 0xA6ADBE, 0x555C70, 0x272C3A, 0xB0F0FF },   /* graphite   */
    { 0xE8E17A, 0xB0A428, 0x5C540E, 0xFFF9A0 },   /* lime       */
    { 0xC08A62, 0x7E4E2C, 0x412514, 0xFFB07A },   /* rust       */
};

/* --------------------------------------------------------------------------
 * THE PARTS
 *
 * 'nivel' is not a requirement for using it: it is the zone from which it
 * starts appearing in opponents and in chests. A level 8 part found early can
 * be fitted all the same, and that is deliberate: finding one is the prize.
 *
 * The attacks are indices into ch_moves. 0 means "contributes none".
 * -------------------------------------------------------------------------- */

const ch_part_t ch_partes[PIEZAS] = {
    /* ---- HEADS: energy and accuracy ---------------------------------- */
    /*  name           hp  atk def spd ene  type          lvl sty  movA movB */
    { N_("Ojo Simple"),   8,  6,  6,  8, 14, TIPO_IMPACTO,  1,  0,   0,  0 },
    { N_("Visor"),        6,  8,  5, 10, 12, TIPO_PLASMA,   1,  1,  10,  0 },
    { N_("Casco"),       12,  7, 10,  7, 14, TIPO_IMPACTO,  2,  2,   7,  0 },
    { N_("Cupula"),       9, 10,  7, 10, 18, TIPO_PLASMA,   2,  3,  12,  0 },
    { N_("Antena"),       8, 12,  7, 12, 24, TIPO_VOLT,     3,  4,  25,  0 },
    { N_("Radar"),       10,  9, 11, 11, 22, TIPO_PLASMA,   3,  5,  40, 12 },
    { N_("Ciclope"),     11, 16,  9, 12, 20, TIPO_FUEGO,    4,  6,  15,  0 },
    { N_("Faro"),        10, 13, 10, 15, 26, TIPO_PLASMA,   4,  7,   8, 38 },
    { N_("Craneo"),      14, 18, 12, 13, 22, TIPO_ACIDO,    5,  8,  29,  7 },
    { N_("Tri-Ojo"),     12, 15, 11, 18, 28, TIPO_PLASMA,   5,  9,  12, 40 },
    { N_("Yelmo"),       18, 16, 18, 12, 24, TIPO_IMPACTO,  6, 10,  36, 41 },
    { N_("Sensor"),      13, 17, 13, 20, 32, TIPO_CRIO,     6, 11,  19, 42 },
    { N_("Farola"),      15, 21, 14, 18, 30, TIPO_FUEGO,    7, 12,  17,  0 },
    { N_("Mascara"),     17, 20, 17, 16, 26, TIPO_ACIDO,    7, 13,  32,  0 },
    { N_("Torreta"),     16, 26, 15, 17, 30, TIPO_VOLT,     8, 14,  26, 43 },
    { N_("Nucleo"),      20, 22, 20, 20, 38, TIPO_PLASMA,   8, 15,  11, 37 },

    /* ---- TORSOS: health and defence. The torso gives the robot's TYPE - */
    { N_("Caja"),        22,  6, 10,  4,  8, TIPO_IMPACTO,  1,  0,   2,  0 },
    { N_("Barril"),      26,  5,  9,  3,  8, TIPO_IMPACTO,  1,  1,   2,  0 },
    { N_("Coraza"),      30,  7, 14,  3, 10, TIPO_IMPACTO,  2,  2,  37,  0 },
    { N_("Reactor"),     24,  9, 10,  6, 18, TIPO_PLASMA,   2,  3,   8,  0 },
    { N_("Panel"),       28, 10, 13,  6, 14, TIPO_VOLT,     3,  4,  25,  0 },
    { N_("Blindado"),    36,  8, 18,  2, 10, TIPO_IMPACTO,  3,  5,  41,  6 },
    { N_("Cofre"),       34, 11, 16,  5, 14, TIPO_ACIDO,    4,  0,  30,  0 },
    { N_("Nucleo V"),    30, 14, 13,  8, 20, TIPO_PLASMA,   4,  3,   9, 38 },
    { N_("Chasis"),      32, 15, 15,  9, 16, TIPO_IMPACTO,  5,  4,   4,  0 },
    { N_("Fragua"),      38, 16, 17,  5, 14, TIPO_FUEGO,    5,  2,  18, 14 },
    { N_("Celda"),       34, 17, 16, 10, 26, TIPO_VOLT,     6,  3,  28, 43 },
    { N_("Boveda"),      46, 13, 24,  4, 14, TIPO_IMPACTO,  6,  5,  36, 41 },
    { N_("Turbina"),     36, 20, 18, 13, 22, TIPO_CRIO,     7,  1,  23, 42 },
    { N_("Placa Madre"), 40, 19, 21,  9, 24, TIPO_PLASMA,   7,  4,  11, 39 },
    { N_("Motor"),       44, 24, 20, 12, 22, TIPO_FUEGO,    8,  2,  16,  4 },
    { N_("Prisma"),      42, 22, 23, 14, 30, TIPO_CRIO,     8,  3,  21, 39 },

    /* ---- ARMS: attack. They are the ones bringing the heavy hits ------ */
    { N_("Garras"),       6, 14,  5,  6,  4, TIPO_IMPACTO,  1,  0,   3,  0 },
    { N_("Pinzas"),       7, 12,  7,  5,  4, TIPO_IMPACTO,  1,  3,   5,  0 },
    { N_("Martillos"),    8, 20,  6,  3,  4, TIPO_IMPACTO,  2,  5,   1,  0 },
    { N_("Canones"),      6, 18,  5,  6,  8, TIPO_PLASMA,   2,  4,   8,  0 },
    { N_("Muelles"),      7, 17,  7,  9,  6, TIPO_IMPACTO,  3,  6,   3,  0 },
    { N_("Tenazas"),      9, 21,  8,  5,  6, TIPO_ACIDO,    3,  2,  30,  0 },
    { N_("Sierras"),      7, 26,  6,  8,  6, TIPO_IMPACTO,  4,  2,   5,  1 },
    { N_("Puas"),         8, 24,  9,  6,  6, TIPO_ACIDO,    4,  0,  29,  0 },
    { N_("Guantes"),     11, 25, 12,  7,  8, TIPO_IMPACTO,  5,  1,   6,  3 },
    { N_("Taladros"),     9, 30,  8,  6,  8, TIPO_IMPACTO,  5,  5,   4,  0 },
    { N_("Latigos"),      8, 28, 10, 14,  8, TIPO_VOLT,     6,  6,  24, 27 },
    { N_("Escudos"),     14, 20, 22,  4,  8, TIPO_IMPACTO,  6,  7,  36, 41 },
    { N_("Lanzas"),      10, 34, 11,  9, 10, TIPO_CRIO,     7,  2,  20, 22 },
    { N_("Imanes"),      11, 30, 14, 10, 14, TIPO_VOLT,     7,  4,  27, 13 },
    { N_("Sopletes"),    10, 38, 12, 10, 12, TIPO_FUEGO,    8,  4,  14, 16 },
    { N_("Rotores"),     12, 34, 15, 16, 12, TIPO_PLASMA,   8,  6,   9, 11 },

    /* ---- LEGS: speed ------------------------------------------------- */
    { N_("Patas"),       10,  4,  6, 14,  4, TIPO_IMPACTO,  1,  0,   0,  0 },
    { N_("Zancos"),       8,  5,  5, 17,  4, TIPO_IMPACTO,  1,  1,   0,  0 },
    { N_("Orugas"),      18,  6, 14,  8,  4, TIPO_IMPACTO,  2,  2,   6,  0 },
    { N_("Rueda"),       10,  5,  7, 20,  6, TIPO_IMPACTO,  2,  3,   2,  0 },
    { N_("Triciclo"),    14,  6, 10, 18,  6, TIPO_IMPACTO,  3,  4,   2,  0 },
    { N_("Flotador"),    11,  7,  8, 24,  8, TIPO_PLASMA,   3,  5,  10,  0 },
    { N_("Aracnidas"),   16,  9, 13, 16,  6, TIPO_ACIDO,    4,  6,  33,  0 },
    { N_("Resortes"),    13,  8, 10, 22,  6, TIPO_IMPACTO,  4,  7,   6,  0 },
    { N_("Botas"),       20, 10, 16, 17,  8, TIPO_IMPACTO,  5,  0,   6, 37 },
    { N_("Cadenas"),     24, 11, 19, 12,  8, TIPO_ACIDO,    5,  2,  31,  0 },
    { N_("Monociclo"),   15, 12, 12, 28,  8, TIPO_VOLT,     6,  3,  24,  0 },
    { N_("Cuadrupedas"), 26, 13, 20, 16,  8, TIPO_IMPACTO,  6,  6,   4, 41 },
    { N_("Cohetes"),     18, 15, 14, 32, 12, TIPO_FUEGO,    7,  5,  17, 43 },
    { N_("Garras Sup."), 22, 16, 18, 24, 10, TIPO_CRIO,     7,  6,  22,  0 },
    { N_("Pistones"),    28, 18, 22, 22, 10, TIPO_IMPACTO,  8,  7,   4,  1 },
    { N_("Deslizador"),  24, 17, 19, 34, 12, TIPO_CRIO,     8,  5,  19, 42 },
};

/* --------------------------------------------------------------------------
 * Drawing traits
 *
 * Each table is 16 bytes. Changing a part's look is changing a number, not
 * touching code.
 * -------------------------------------------------------------------------- */

/* HEAD: silhouette, eyes, top finish */
static const uint8_t CAB_FORMA[PVAR] = { 0,1,0,2,1,3,4,2,5,4,0,3,6,5,6,2 };
static const uint8_t CAB_OJOS [PVAR] = { 0,1,0,6,0,1,2,5,7,3,4,1,2,4,5,6 };
static const uint8_t CAB_ANT  [PVAR] = { 0,0,1,0,2,3,0,1,5,2,4,3,1,5,4,3 };

/* TORSO: silhouette, chest, shoulders */
static const uint8_t TOR_FORMA[PVAR] = { 0,1,0,2,3,0,4,2,3,0,2,5,1,3,5,4 };
static const uint8_t TOR_PECHO[PVAR] = { 6,2,2,0,3,6,5,1,3,4,0,5,1,4,0,1 };
static const uint8_t TOR_HOMB [PVAR] = { 0,0,1,0,1,2,1,0,3,2,1,3,2,1,3,3 };

/* ARMS: type and thickness */
static const uint8_t BRA_TIPO[PVAR]  = { 2,3,5,4,6,2,7,7,1,5,6,8,4,4,4,1 };
static const uint8_t BRA_GRUE[PVAR]  = { 3,3,4,3,3,4,4,4,5,4,3,5,4,4,4,5 };

/* LEGS: type */
static const uint8_t PIE_TIPO[PVAR]  = { 0,1,2,3,4,5,6,7,0,2,3,6,5,6,0,5 };

/* --------------------------------------------------------------------------
 * The pen
 * -------------------------------------------------------------------------- */

typedef struct {
    ch_buf_t *b;
    int       ox, oy, e;
    uint16_t  cl, md, os, br, kk;
} pen_t;

static void R(const pen_t *p, int x, int y, int w, int h, uint16_t c)
{
    ch_rect(p->b, p->ox + x * p->e, p->oy + y * p->e, w * p->e, h * p->e, c);
}

/* Rectangle with a dark outline: it is what gives the pixel art its
 * readability. */
static void RB(const pen_t *p, int x, int y, int w, int h, uint16_t c)
{
    R(p, x, y, w, h, p->kk);
    if (w > 2 && h > 2) {
        R(p, x + 1, y + 1, w - 2, h - 2, c);
    }
}

static void DSC(const pen_t *p, int cx, int cy, int r, uint16_t c)
{
    /* The disc is drawn at real scale so it does not come out blocky. */
    ch_disc(p->b, p->ox + cx * p->e, p->oy + cy * p->e, r * p->e, c);
}

/* --------------------------------------------------------------------------
 * The box
 *
 * 26 x 40 units. Anything a part draws has to fit inside it: it is the
 * rectangle the combat dirties and restores, and whatever runs outside leaves
 * a trail stuck on the screen (the classic dirty-rectangle trap).
 *
 *      y  0..11   head
 *      y 11..27   torso, with the arms at the sides
 *      y 26..40   legs
 * -------------------------------------------------------------------------- */

#define BOX_W   26
#define BOX_H   40
#define BOX_CX  13

void ch_robot_box(int esc, int *w, int *h)
{
    if (w) *w = BOX_W * esc;
    if (h) *h = BOX_H * esc;
}

/* --------------------------------------------------------------------------
 * Head
 * -------------------------------------------------------------------------- */

static void draw_cabeza(const pen_t *p, int var, bool izq)
{
    int forma = CAB_FORMA[var], ojos = CAB_OJOS[var], ant = CAB_ANT[var];
    int x, w, y = 1, h = 10;

    /* silhouette */
    switch (forma) {
    case 1:  w = 12; h = 11; y = 0; break;      /* tall                  */
    case 2:  w = 12; h =  9; y = 2; break;      /* dome                  */
    case 3:  w = 14; h =  9; y = 2; break;      /* wide                  */
    case 4:  w = 10; h = 10; y = 1; break;      /* narrow                */
    case 5:  w = 13; h = 10; y = 1; break;      /* hexagonal             */
    case 6:  w = 12; h = 10; y = 1; break;      /* double box            */
    default: w = 12; h = 10; y = 1; break;      /* box                   */
    }
    x = BOX_CX - w / 2;

    RB(p, x, y, w, h, p->cl);
    /* shadow on the side away from the light */
    R(p, izq ? x + 1 : x + w - 3, y + 1, 2, h - 2, p->md);

    if (forma == 2) {                            /* dome: bitten corners     */
        R(p, x, y, 2, 2, p->kk);
        R(p, x + w - 2, y, 2, 2, p->kk);
        R(p, x + 1, y - 1, w - 2, 1, p->kk);
        R(p, x + 2, y, w - 4, 1, p->cl);
    } else if (forma == 5) {                     /* hexagonal                */
        R(p, x, y, 1, 2, p->kk);
        R(p, x + w - 1, y, 1, 2, p->kk);
        R(p, x, y + h - 2, 1, 2, p->kk);
        R(p, x + w - 1, y + h - 2, 1, 2, p->kk);
    } else if (forma == 6) {                     /* separate jaw             */
        R(p, x + 2, y + h - 3, w - 4, 1, p->os);
    }

    /* eyes */
    switch (ojos) {
    case 1:                                      /* visor side to side       */
        R(p, x + 2, y + 3, w - 4, 3, p->kk);
        R(p, x + 3, y + 4, w - 6, 1, p->br);
        break;
    case 2:                                      /* cyclops                  */
        DSC(p, BOX_CX, y + h / 2, 2, p->kk);
        DSC(p, BOX_CX, y + h / 2, 1, p->br);
        break;
    case 3:                                      /* three eyes               */
        R(p, x + 2, y + 4, 2, 2, p->br);
        R(p, BOX_CX - 1, y + 3, 2, 2, p->br);
        R(p, x + w - 4, y + 4, 2, 2, p->br);
        break;
    case 4:                                      /* angry                    */
        R(p, x + 2, y + 3, 4, 1, p->kk);
        R(p, x + w - 6, y + 3, 4, 1, p->kk);
        R(p, x + 3, y + 4, 2, 2, p->br);
        R(p, x + w - 5, y + 4, 2, 2, p->br);
        break;
    case 5:                                      /* slotted visor            */
        R(p, x + 2, y + 3, w - 4, 4, p->os);
        R(p, x + 3, y + 4, w - 6, 1, p->br);
        R(p, x + 3, y + 6, w - 6, 1, p->kk);
        break;
    case 6:                                      /* two large eyes           */
        R(p, x + 2, y + 3, 3, 3, p->kk);
        R(p, x + w - 5, y + 3, 3, 3, p->kk);
        R(p, x + 3, y + 4, 1, 1, p->br);
        R(p, x + w - 4, y + 4, 1, 1, p->br);
        break;
    case 7:                                      /* crossed blades           */
        R(p, x + 2, y + 3, 4, 1, p->br);
        R(p, x + 3, y + 4, 2, 2, p->br);
        R(p, x + w - 6, y + 3, 4, 1, p->br);
        R(p, x + w - 5, y + 4, 2, 2, p->br);
        break;
    default:                                     /* two dots                 */
        R(p, x + 3, y + 4, 2, 2, p->br);
        R(p, x + w - 5, y + 4, 2, 2, p->br);
        break;
    }

    /* mouth grille: it gives a face to nearly every shape */
    if (h >= 10 && ojos != 5) {
        R(p, BOX_CX - 3, y + h - 3, 6, 2, p->os);
        R(p, BOX_CX - 2, y + h - 3, 1, 2, p->kk);
        R(p, BOX_CX, y + h - 3, 1, 2, p->kk);
        R(p, BOX_CX + 2, y + h - 3, 1, 2, p->kk);
    }

    /* top finish */
    switch (ant) {
    case 1:                                      /* antenna with a ball      */
        R(p, BOX_CX, y - 4, 1, 4, p->os);
        DSC(p, BOX_CX, y - 5, 1, p->br);
        break;
    case 2:                                      /* two antennae             */
        R(p, x + 2, y - 3, 1, 3, p->os);
        R(p, x + w - 3, y - 3, 1, 3, p->os);
        R(p, x + 2, y - 4, 1, 1, p->br);
        R(p, x + w - 3, y - 4, 1, 1, p->br);
        break;
    case 3:                                      /* dish                     */
        R(p, BOX_CX, y - 3, 1, 3, p->os);
        R(p, BOX_CX - 4, y - 5, 9, 2, p->md);
        R(p, BOX_CX - 3, y - 4, 7, 1, p->cl);
        break;
    case 4:                                      /* fin                      */
        R(p, BOX_CX - 1, y - 4, 3, 4, p->md);
        R(p, BOX_CX, y - 5, 1, 5, p->br);
        break;
    case 5:                                      /* horns                    */
        R(p, x + 1, y - 3, 2, 3, p->md);
        R(p, x + w - 3, y - 3, 2, 3, p->md);
        R(p, x + 1, y - 4, 1, 1, p->cl);
        R(p, x + w - 2, y - 4, 1, 1, p->cl);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Torso
 * -------------------------------------------------------------------------- */

#define TOR_Y   11
#define TOR_H   16

static void draw_torso(const pen_t *p, int var, bool izq)
{
    int forma = TOR_FORMA[var], pecho = TOR_PECHO[var], hom = TOR_HOMB[var];
    int w = 16, x, y = TOR_Y, h = TOR_H;

    switch (forma) {
    case 1: w = 18; break;                       /* barrel                   */
    case 2: w = 14; break;                       /* narrow                   */
    case 3: w = 17; break;                       /* trapezoid                */
    case 4: w = 16; h = 15; break;               /* short                    */
    case 5: w = 19; break;                       /* solid                    */
    default: break;
    }
    x = BOX_CX - w / 2;

    RB(p, x, y, w, h, p->cl);
    R(p, izq ? x + 1 : x + w - 3, y + 1, 2, h - 2, p->md);

    if (forma == 1) {                            /* bitten corners           */
        R(p, x, y, 1, 2, p->kk);
        R(p, x + w - 1, y, 1, 2, p->kk);
        R(p, x, y + h - 2, 1, 2, p->kk);
        R(p, x + w - 1, y + h - 2, 1, 2, p->kk);
    } else if (forma == 3) {                     /* tapers downwards         */
        R(p, x, y + h - 3, 2, 3, p->kk);
        R(p, x + w - 2, y + h - 3, 2, 3, p->kk);
    }

    /* neck */
    R(p, BOX_CX - 2, y - 1, 4, 2, p->os);

    /* the chest */
    int py = y + 4;
    switch (pecho) {
    case 0:                                      /* circular core            */
        DSC(p, BOX_CX, py + 2, 3, p->os);
        DSC(p, BOX_CX, py + 2, 2, p->br);
        break;
    case 1:                                      /* rhombus                  */
        R(p, BOX_CX - 1, py, 2, 6, p->br);
        R(p, BOX_CX - 2, py + 1, 4, 4, p->br);
        R(p, BOX_CX - 3, py + 2, 6, 2, p->br);
        break;
    case 2:                                      /* grille                   */
        for (int i = 0; i < 4; i++) {
            R(p, x + 3, py + i * 2, w - 6, 1, p->os);
        }
        break;
    case 3:                                      /* panel with lights        */
        R(p, x + 3, py, w - 6, 6, p->os);
        R(p, x + 4, py + 1, 2, 2, p->br);
        R(p, x + 7, py + 1, 2, 2, p->md);
        R(p, x + 4, py + 4, w - 8, 1, p->md);
        break;
    case 4:                                      /* screen                   */
        R(p, x + 3, py - 1, w - 6, 7, p->kk);
        R(p, x + 4, py, w - 8, 5, p->br);
        R(p, x + 5, py + 2, w - 10, 1, p->os);
        break;
    case 5:                                      /* cross                    */
        R(p, BOX_CX - 1, py, 2, 7, p->os);
        R(p, x + 3, py + 2, w - 6, 2, p->os);
        R(p, BOX_CX - 1, py + 2, 2, 2, p->br);
        break;
    default:                                     /* riveted plates           */
        R(p, x + 2, py + 1, 2, 2, p->os);
        R(p, x + w - 4, py + 1, 2, 2, p->os);
        R(p, x + 2, py + 5, 2, 2, p->os);
        R(p, x + w - 4, py + 5, 2, 2, p->os);
        R(p, x + 4, py + 3, w - 8, 1, p->md);
        break;
    }

    /* waist */
    R(p, x + 2, y + h - 2, w - 4, 2, p->os);

    /* shoulders */
    switch (hom) {
    case 1:
        RB(p, x - 2, y, 4, 5, p->md);
        RB(p, x + w - 2, y, 4, 5, p->md);
        break;
    case 2:                                      /* spikes                   */
        RB(p, x - 2, y, 4, 4, p->md);
        RB(p, x + w - 2, y, 4, 4, p->md);
        R(p, x - 3, y - 2, 2, 3, p->cl);
        R(p, x + w + 1, y - 2, 2, 3, p->cl);
        break;
    case 3:                                      /* rounded                  */
        DSC(p, x, y + 2, 3, p->kk);
        DSC(p, x, y + 2, 2, p->md);
        DSC(p, x + w, y + 2, 3, p->kk);
        DSC(p, x + w, y + 2, 2, p->md);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Arms
 *
 * Both are drawn, mirrored. 'lado' is -1 for the left and +1 for the right;
 * 'alza' raises the front arm when the robot attacks.
 * -------------------------------------------------------------------------- */

static void draw_un_brazo(const pen_t *p, int var, int lado, int alza)
{
    int tipo = BRA_TIPO[var], gr = BRA_GRUE[var];
    int borde = (lado < 0) ? (BOX_CX - 8) : (BOX_CX + 8);
    int x = (lado < 0) ? borde - gr : borde;
    int y = TOR_Y + 2 - alza;

    /* the shoulder */
    RB(p, x, y, gr, 4, p->cl);

    switch (tipo) {
    case 1:                                      /* long straight            */
        RB(p, x, y + 3, gr, 11, p->md);
        RB(p, x - (lado < 0 ? 1 : 0), y + 12, gr + 1, 3, p->cl);
        break;
    case 2:                                      /* claw                     */
        RB(p, x, y + 3, gr, 8, p->md);
        R(p, x, y + 11, 1, 4, p->cl);
        R(p, x + gr - 1, y + 11, 1, 4, p->cl);
        R(p, x, y + 14, gr, 1, p->os);
        break;
    case 3:                                      /* pincer                   */
        RB(p, x, y + 3, gr, 7, p->md);
        RB(p, x - (lado < 0 ? 1 : 0), y + 10, gr + 1, 3, p->cl);
        R(p, x + (lado < 0 ? -1 : gr - 1), y + 13, 2, 2, p->os);
        break;
    case 4:                                      /* cannon                   */
        RB(p, x, y + 3, gr, 6, p->md);
        RB(p, x - 1, y + 9, gr + 2, 6, p->cl);
        R(p, x, y + 14, gr, 1, p->br);
        break;
    case 5:                                      /* hammer                   */
        RB(p, x, y + 3, gr - 1, 8, p->md);
        RB(p, x - 2, y + 11, gr + 3, 5, p->cl);
        R(p, x - 1, y + 12, gr + 1, 1, p->br);
        break;
    case 6:                                      /* spring                   */
        for (int i = 0; i < 4; i++) {
            R(p, x, y + 4 + i * 2, gr, 1, p->md);
            R(p, x + (lado < 0 ? 0 : gr - 1), y + 5 + i * 2, 1, 1, p->os);
        }
        RB(p, x - (lado < 0 ? 1 : 0), y + 12, gr + 1, 4, p->cl);
        break;
    case 7:                                      /* spikes                   */
        RB(p, x, y + 3, gr, 10, p->md);
        R(p, x + (lado < 0 ? -1 : gr), y + 5, 1, 2, p->cl);
        R(p, x + (lado < 0 ? -1 : gr), y + 9, 1, 2, p->cl);
        RB(p, x, y + 12, gr, 3, p->cl);
        break;
    case 8:                                      /* shield                   */
        RB(p, x, y + 3, gr, 7, p->md);
        RB(p, x + (lado < 0 ? -2 : 0), y + 6, gr + 2, 9, p->cl);
        R(p, x + (lado < 0 ? -1 : 1), y + 8, gr, 1, p->br);
        break;
    default:                                     /* straight                 */
        RB(p, x, y + 3, gr, 9, p->md);
        RB(p, x, y + 11, gr, 4, p->cl);
        break;
    }
}

/* --------------------------------------------------------------------------
 * Legs
 * -------------------------------------------------------------------------- */

#define PIE_Y   26
#define PIE_H   14

static void draw_piernas(const pen_t *p, int var, int paso)
{
    int tipo = PIE_TIPO[var];
    int y = PIE_Y, h = PIE_H;

    switch (tipo) {
    case 1:                                      /* stilts                   */
        RB(p, BOX_CX - 6, y + 1, 3, h - 4, p->md);
        RB(p, BOX_CX + 3, y + 1, 3, h - 4, p->md);
        RB(p, BOX_CX - 8, y + h - 3, 6, 3, p->cl);
        RB(p, BOX_CX + 2, y + h - 3, 6, 3, p->cl);
        break;
    case 2:                                      /* tracks                   */
        RB(p, BOX_CX - 10, y + 4, 20, h - 4, p->os);
        for (int i = 0; i < 5; i++) {
            DSC(p, BOX_CX - 7 + i * 4, y + 4 + (h - 4) / 2, 2, p->md);
        }
        R(p, BOX_CX - 9, y + 5 + ((paso >> 1) & 1), 18, 1, p->cl);
        break;
    case 3:                                      /* single wheel             */
        DSC(p, BOX_CX, y + 7, 7, p->kk);
        DSC(p, BOX_CX, y + 7, 6, p->os);
        DSC(p, BOX_CX, y + 7, 3, p->md);
        DSC(p, BOX_CX, y + 7, 1, p->br);
        R(p, BOX_CX - 1, y, 2, 3, p->md);
        break;
    case 4:                                      /* tricycle                 */
        RB(p, BOX_CX - 8, y + 2, 16, 4, p->md);
        DSC(p, BOX_CX - 6, y + 9, 4, p->os);
        DSC(p, BOX_CX - 6, y + 9, 2, p->md);
        DSC(p, BOX_CX + 6, y + 9, 4, p->os);
        DSC(p, BOX_CX + 6, y + 9, 2, p->md);
        break;
    case 5:                                      /* hover                    */
        RB(p, BOX_CX - 7, y + 1, 14, 5, p->md);
        R(p, BOX_CX - 5, y + 7, 10, 2, p->br);
        R(p, BOX_CX - 3 + ((paso >> 1) & 1), y + 10, 6, 1, p->br);
        break;
    case 6:                                      /* arachnid                 */
        RB(p, BOX_CX - 5, y + 1, 10, 4, p->md);
        for (int i = 0; i < 2; i++) {
            int dx = 5 + i * 3;
            R(p, BOX_CX - dx - 2, y + 4, 3, 2, p->md);
            R(p, BOX_CX - dx - 3, y + 6, 2, h - 7, p->os);
            R(p, BOX_CX + dx - 1, y + 4, 3, 2, p->md);
            R(p, BOX_CX + dx + 1, y + 6, 2, h - 7, p->os);
        }
        break;
    case 7:                                      /* springs                  */
        for (int i = 0; i < 4; i++) {
            R(p, BOX_CX - 6, y + 2 + i * 2, 4, 1, p->md);
            R(p, BOX_CX + 2, y + 2 + i * 2, 4, 1, p->md);
        }
        RB(p, BOX_CX - 7, y + h - 3, 6, 3, p->cl);
        RB(p, BOX_CX + 1, y + h - 3, 6, 3, p->cl);
        break;
    default: {                                   /* two legs                 */
        int a = (paso & 2) ? 1 : 0;
        RB(p, BOX_CX - 6, y + 1, 4, h - 4 - a, p->md);
        RB(p, BOX_CX + 2, y + 1, 4, h - 4 + a, p->md);
        RB(p, BOX_CX - 7, y + h - 3 - a, 6, 3, p->cl);
        RB(p, BOX_CX + 1, y + h - 3 + a, 6, 3, p->cl);
        break;
    }
    }
}

/* --------------------------------------------------------------------------
 * The whole robot
 *
 * pose: 0 still, 1 attacking (it leans and raises the arm), 2 hit.
 * -------------------------------------------------------------------------- */

void ch_robot_draw(ch_buf_t *b, int cx, int y, const ch_robot_t *r,
                   int esc, bool mirando_izq, int pose)
{
    const ch_skin_t *s = &ch_skins[r->skin % SKINS];
    pen_t p;
    int lean = (pose == 1) ? (mirando_izq ? -2 : 2) : (pose == 2 ? -1 : 0);

    p.b  = b;
    p.e  = esc;
    p.ox = cx - BOX_CX * esc + lean * esc;
    p.oy = y;
    p.cl = ch_rgb(s->claro);
    p.md = ch_rgb(s->medio);
    p.os = ch_rgb(s->oscuro);
    p.br = ch_rgb(s->brillo);
    p.kk = ch_rgb(0x05060C);

    /* The shadow on the ground, which is what rests it there. It goes FLAT and
     * inside the 26x40 box, not as a disc: a disc centred on the base sticks
     * out below by its whole radius, and in combat that was drawn on top of
     * the text panel and ate half a word. Anything running outside the box
     * does not fit in the dirty rectangle either, so it also stays stuck. */
    {
        uint16_t so = ch_rgb(0x1A1E2A);
        ch_rect(b, p.ox + (BOX_CX - 8) * esc, p.oy + (BOX_H - 3) * esc,
                16 * esc, esc, so);
        ch_rect(b, p.ox + (BOX_CX - 9) * esc, p.oy + (BOX_H - 2) * esc,
                18 * esc, esc, so);
        ch_rect(b, p.ox + (BOX_CX - 7) * esc, p.oy + (BOX_H - 1) * esc,
                14 * esc, esc, so);
    }

    /* Order: legs, back arm, torso, head, front arm. The front arm goes last
     * so it covers the torso when attacking. */
    draw_piernas(&p, r->pieza[P_PIERNAS], 0);
    draw_un_brazo(&p, r->pieza[P_BRAZOS], mirando_izq ? +1 : -1, 0);
    draw_torso(&p, r->pieza[P_TORSO], mirando_izq);
    draw_cabeza(&p, r->pieza[P_CABEZA], mirando_izq);
    draw_un_brazo(&p, r->pieza[P_BRAZOS], mirando_izq ? -1 : +1,
                  pose == 1 ? 3 : 0);
}

/* --------------------------------------------------------------------------
 * The map figure
 *
 * 12x16, four directions and two steps. It is not the combat robot shrunk -at
 * that scale nothing would read- but it DOES carry the colour and the shape of
 * the head and legs you are wearing, which is what makes the player recognise
 * their robot walking.
 * -------------------------------------------------------------------------- */

void ch_mini_draw(ch_buf_t *b, int x, int y, const ch_robot_t *r,
                  int dir, int paso)
{
    const ch_skin_t *s = &ch_skins[r->skin % SKINS];
    uint16_t cl = ch_rgb(s->claro), md = ch_rgb(s->medio);
    uint16_t os = ch_rgb(s->oscuro), br = ch_rgb(s->brillo);
    uint16_t kk = ch_rgb(0x05060C);
    int ant = CAB_ANT[r->pieza[P_CABEZA]];
    int rueda = PIE_TIPO[r->pieza[P_PIERNAS]];
    int a = (paso & 2) ? 1 : 0;

    /* shadow */
    ch_rect(b, x + 2, y + 15, 8, 1, ch_rgb(0x1A1E2A));

    /* legs or wheels */
    if (rueda == 2 || rueda == 3 || rueda == 4 || rueda == 5) {
        ch_rect(b, x + 1, y + 11, 10, 4, kk);
        ch_rect(b, x + 2, y + 12, 8, 2, os);
        ch_rect(b, x + 3 + ((paso >> 1) & 1) * 2, y + 12, 2, 2, md);
    } else {
        ch_rect(b, x + 3, y + 11, 2, 4 - a, kk);
        ch_rect(b, x + 7, y + 11, 2, 4 + a - 1, kk);
    }

    /* torso */
    ch_rect(b, x + 2, y + 6, 8, 6, kk);
    ch_rect(b, x + 3, y + 7, 6, 4, cl);
    ch_rect(b, x + 3, y + 7, 2, 4, md);

    /* arms */
    ch_rect(b, x, y + 6, 2, 4, md);
    ch_rect(b, x + 10, y + 6, 2, 4, md);

    /* head */
    ch_rect(b, x + 2, y + 1, 8, 6, kk);
    ch_rect(b, x + 3, y + 2, 6, 4, cl);

    /* the face looks where it walks */
    switch (dir) {
    case 1:                                      /* from behind              */
        ch_rect(b, x + 4, y + 3, 4, 2, md);
        break;
    case 2:                                      /* to the left              */
        ch_rect(b, x + 3, y + 3, 2, 2, br);
        break;
    case 3:                                      /* to the right             */
        ch_rect(b, x + 7, y + 3, 2, 2, br);
        break;
    default:                                     /* front on                 */
        ch_rect(b, x + 4, y + 3, 1, 2, br);
        ch_rect(b, x + 7, y + 3, 1, 2, br);
        break;
    }

    /* the head's finish travels too: it is what shows most from a distance */
    if (ant == 1 || ant == 4) {
        ch_rect(b, x + 6, y - 1, 1, 2, os);
        ch_rect(b, x + 6, y - 2, 1, 1, br);
    } else if (ant == 2 || ant == 5) {
        ch_rect(b, x + 3, y, 1, 2, os);
        ch_rect(b, x + 8, y, 1, 2, os);
    } else if (ant == 3) {
        ch_rect(b, x + 4, y - 1, 4, 1, md);
    }
}

/* --------------------------------------------------------------------------
 * Stats
 *
 * The Pokemon shape, with integers: the base comes from adding the four parts
 * and the level scales it. No floating point, which on the S3 drags in
 * __divsf3 and fattens the .so needlessly.
 * -------------------------------------------------------------------------- */

uint32_t ch_exp_nivel(int nivel)
{
    if (nivel < 1) nivel = 1;
    return (uint32_t)nivel * (uint32_t)nivel * (uint32_t)nivel;
}

const char *ch_robot_nombre(const ch_robot_t *r)
{
    /* The robot is named after its torso: it is the part that defines its type
     * and the one that shows most. Changing torso is changing robot, and that
     * is right. */
    return _(ch_partes[PIEZA_ID(P_TORSO, r->pieza[P_TORSO] % PVAR)].nombre);
}

void ch_robot_visto(ch_save_t *s, const ch_robot_t *r)
{
    for (int c = 0; c < P_CATS; c++) {
        ch_ver(s, PIEZA_ID(c, r->pieza[c] % PVAR));
    }
}

/* --------------------------------------------------------------------------
 * A loose part
 *
 * The same pen and the same four functions that draw a whole robot, but
 * centring the part on the point they are given. It is what lets the register
 * show all 64 without a single new sprite: if the drawing were in bitmaps,
 * this screen would have cost 64 images.
 * -------------------------------------------------------------------------- */
void ch_part_draw(ch_buf_t *b, int cat, int var, int cx, int cy, int esc,
                  int skin)
{
    const ch_skin_t *s = &ch_skins[(skin < 0 ? 0 : skin) % SKINS];
    pen_t p;

    p.b = b;
    p.e = esc;
    p.ox = cx - BOX_CX * esc;
    p.kk = ch_rgb(0x05060C);
    if (skin < 0) {
        /* Silhouette: the one you have not seen yet. You recognise the shape
         * and nothing else, which is exactly what should happen. */
        p.cl = p.md = p.os = p.br = ch_rgb(0x2B3145);
    } else {
        p.cl = ch_rgb(s->claro);  p.md = ch_rgb(s->medio);
        p.os = ch_rgb(s->oscuro); p.br = ch_rgb(s->brillo);
    }

    switch (cat) {
    case P_CABEZA:  p.oy = cy - 6 * esc;  draw_cabeza(&p, var % PVAR, false); break;
    case P_TORSO:   p.oy = cy - 19 * esc; draw_torso(&p, var % PVAR, false);  break;
    case P_BRAZOS:  p.oy = cy - 20 * esc;
                    draw_un_brazo(&p, var % PVAR, -1, 0);
                    draw_un_brazo(&p, var % PVAR, +1, 0);                     break;
    default:        p.oy = cy - 33 * esc; draw_piernas(&p, var % PVAR, 0);    break;
    }
}

void ch_robot_stats(ch_robot_t *r)
{
    int bv = 0, ba = 0, bd = 0, bs = 0, be = 0;
    int nv = r->nivel < 1 ? 1 : r->nivel;

    for (int c = 0; c < P_CATS; c++) {
        const ch_part_t *p = &ch_partes[PIEZA_ID(c, r->pieza[c] % PVAR)];
        bv += p->vida;  ba += p->atk;  bd += p->def;
        bs += p->vel;   be += p->energia;
    }

    int vmax = (bv * 3 * nv) / 100 + nv * 2 + 20;
    int emax = 24 + be / 2 + nv / 2;

    r->atk = (int16_t)((ba * 2 * nv) / 100 + 6);
    r->def = (int16_t)((bd * 2 * nv) / 100 + 6);
    r->vel = (int16_t)((bs * 2 * nv) / 100 + 6);
    r->tipo = ch_partes[PIEZA_ID(P_TORSO, r->pieza[P_TORSO] % PVAR)].tipo;

    /* On levelling up the maximum health grows and the current one rises with
     * it; on changing a part, likewise. What cannot happen is the current one
     * ending up above the new maximum. */
    if (r->vida_max != vmax) {
        int dif = vmax - r->vida_max;
        r->vida_max = (int16_t)vmax;
        if (dif > 0 && r->vida > 0) r->vida = (int16_t)(r->vida + dif);
    }
    r->ene_max = (int16_t)emax;
    if (r->vida > r->vida_max) r->vida = r->vida_max;
    if (r->ene  > r->ene_max)  r->ene  = r->ene_max;
    if (r->vida < 0) r->vida = 0;

    /* The attacks: the basic hit always, and then whatever each part
     * contributes without repeating. Four is the cap, and the upper parts
     * (head and torso) win because they are the ones that define the robot. */
    r->mov[0] = 0;
    r->nmov = 1;
    for (int c = 0; c < P_CATS && r->nmov < 4; c++) {
        const ch_part_t *p = &ch_partes[PIEZA_ID(c, r->pieza[c] % PVAR)];
        uint8_t cand[2] = { p->mov_a, p->mov_b };
        for (int k = 0; k < 2 && r->nmov < 4; k++) {
            if (!cand[k] || cand[k] >= MOVES) continue;
            bool rep = false;
            for (int i = 0; i < r->nmov; i++) {
                if (r->mov[i] == cand[k]) { rep = true; break; }
            }
            if (!rep) r->mov[r->nmov++] = cand[k];
        }
    }
}

void ch_robot_curar(ch_robot_t *r)
{
    ch_robot_stats(r);
    r->vida = r->vida_max;
    r->ene  = r->ene_max;
}

/* --------------------------------------------------------------------------
 * A random opponent
 *
 * It picks among the parts whose zone level does not exceed the current zone's,
 * with a small chance of one from a zone above slipping in: that is what makes
 * a creature worth taking apart turn up now and then.
 * -------------------------------------------------------------------------- */

void ch_robot_random(ch_robot_t *r, uint32_t *rng, int nv, int zona)
{
    memset(r, 0, sizeof(*r));
    if (zona < 1) zona = 1;
    if (zona > 8) zona = 8;

    for (int c = 0; c < P_CATS; c++) {
        int tope = zona + (ch_rnd(rng, 100) < 12 ? 1 : 0);
        uint8_t opciones[PVAR];
        int n = 0;
        for (int v = 0; v < PVAR; v++) {
            if (ch_partes[PIEZA_ID(c, v)].nivel <= tope) {
                opciones[n++] = (uint8_t)v;
            }
        }
        r->pieza[c] = n ? opciones[ch_rnd(rng, n)] : 0;
    }

    r->skin  = (uint8_t)ch_rnd(rng, SKINS);
    r->nivel = (uint8_t)(nv < 1 ? 1 : (nv > 60 ? 60 : nv));
    r->exp   = ch_exp_nivel(r->nivel);
    ch_robot_curar(r);
}
