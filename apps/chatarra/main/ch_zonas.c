/*
 * CHATARRA - the world, room by room
 *
 * There is no code here: it is the maps, the decorations, the entities and the
 * dialogue of the game's eight zones. It is all .rodata, that is, PSRAM, that
 * is, free: adding a whole town adds not a byte to the 48 KB reservation the
 * .text of ALL loaded apps comes out of.
 *
 * It is separate from ch_world.c -which has the cells, the decorations and the
 * drawing of the entities- precisely because this grows and that does not. The
 * engine is a few stable functions; this is the game.
 *
 * ---------------------------------------------------------------------------
 * THE SHAPE OF A ZONE
 * ---------------------------------------------------------------------------
 *
 * The eight zones have the same structure, and that is deliberate: the player
 * learns once how the world is traversed and after that only the scenery
 * changes.
 *
 *     a ROAD with random encounters and a control that closes it
 *     a TOWN with a workshop, a shop and people
 *     a DUNGEON of two or three rooms with chests and creatures
 *     a SUB-BOSS ROOM
 *
 * Beating the sub-boss sets its flag, and that flag opens the next road's
 * CONTROL. That is the whole progression of the game and it costs not a line
 * of code: it is two fields of the entity table.
 *
 * ---------------------------------------------------------------------------
 * RULES THAT CANNOT BE SKIPPED WHEN ADDING A ROOM
 * ---------------------------------------------------------------------------
 *
 *   - the map is EXACTLY 22 rows of 23 characters;
 *   - no decoration may land on an entity's cell (both would be drawn);
 *   - a door carries its WIDTH in 'premio', and against the edge of the screen
 *     it has to be wide: with a finger, a single cell is not hit;
 *   - a door's destination may NEVER be another door, or the first step sends
 *     you back.
 *
 * All four are checked by `CH_CHECK=1` in the simulator, room by room.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <stddef.h>

/* --------------------------------------------------------------------------
 * FLAGS
 *
 * One number for each thing the world remembers. They go into the save file,
 * so the order cannot be changed without breaking old saves: ALWAYS append at
 * the end.
 * -------------------------------------------------------------------------- */

enum {
    F_NADA = 0,
    F_ABUELA_HABLO,         /* the grandmother has already given you the robot */
    F_COFRE_CASA,
    F_COFRE_DESGUACE1,
    F_COFRE_DESGUACE2,
    F_COFRE_DESGUACE3,
    F_JEFE_DESGUACE,        /* the scrapyard's sub-boss, defeated              */
    F_MISION_TORNILLOS,     /* the neighbour asked for the screws              */
    F_TORNILLOS,            /* the box is in your bag                          */
    F_MISION_CUMPLIDA,
    F_GUARDIA_PASO,         /* the northern guard lets you through             */
    F_LLAVE_DESGUACE,
    F_ENEMIGO_1, F_ENEMIGO_2, F_ENEMIGO_3, F_ENEMIGO_4,
    F_ENEMIGO_5, F_ENEMIGO_6, F_ENEMIGO_7, F_ENEMIGO_8,

    /* zone 2 - Puerto Bujia */
    F_JEFE_PUERTO,
    F_COFRE_P1, F_COFRE_P2, F_COFRE_P3, F_COFRE_P4,
    F_PNJ_CAPITAN, F_MISION_ANCLA, F_ANCLA,
    F_ENEMIGO_9,  F_ENEMIGO_10, F_ENEMIGO_11, F_ENEMIGO_12,
    F_ENEMIGO_13, F_ENEMIGO_14, F_ENEMIGO_15, F_ENEMIGO_16,

    /* zone 3 - Alto Voltio */
    F_JEFE_VOLTIO,
    F_COFRE_V1, F_COFRE_V2, F_COFRE_V3,
    F_MISION_FUSIBLE, F_FUSIBLE,
    F_ENEMIGO_17, F_ENEMIGO_18, F_ENEMIGO_19, F_ENEMIGO_20,
    F_ENEMIGO_21, F_ENEMIGO_22, F_ENEMIGO_23,

    /* zone 4 - Fundicion */
    F_JEFE_FUNDICION,
    F_COFRE_F1, F_COFRE_F2, F_COFRE_F3,
    F_MISION_MOLDE, F_MOLDE,
    F_ENEMIGO_24, F_ENEMIGO_25, F_ENEMIGO_26, F_ENEMIGO_27,
    F_ENEMIGO_28, F_ENEMIGO_29, F_ENEMIGO_30,

    /* zone 5 - Criovalle */
    F_JEFE_CRIO,
    F_COFRE_C1, F_COFRE_C2, F_COFRE_C3,
    F_MISION_TERMO, F_TERMO,
    F_ENEMIGO_31, F_ENEMIGO_32, F_ENEMIGO_33, F_ENEMIGO_34,
    F_ENEMIGO_35, F_ENEMIGO_36, F_ENEMIGO_37,

    /* zone 6 - Ciudad Malla */
    F_JEFE_MALLA,
    F_COFRE_M1, F_COFRE_M2, F_COFRE_M3,
    F_MISION_CLAVE, F_CLAVE,
    F_ENEMIGO_38, F_ENEMIGO_39, F_ENEMIGO_40, F_ENEMIGO_41,
    F_ENEMIGO_42, F_ENEMIGO_43, F_ENEMIGO_44,

    /* zone 7 - Villa Oxido */
    F_JEFE_PARAMO,
    F_COFRE_O1, F_COFRE_O2, F_COFRE_O3,
    F_MISION_ENGRANAJE, F_ENGRANAJE,
    F_ENEMIGO_45, F_ENEMIGO_46, F_ENEMIGO_47, F_ENEMIGO_48,
    F_ENEMIGO_49, F_ENEMIGO_50, F_ENEMIGO_51,

    /* zone 8 - Torre Prisma */
    F_JEFE_PRISMA, F_FINAL,
    F_COFRE_T1, F_COFRE_T2, F_COFRE_T3,
    F_ENEMIGO_52, F_ENEMIGO_53, F_ENEMIGO_54, F_ENEMIGO_55,
    F_ENEMIGO_56, F_ENEMIGO_57,
};

/* Room identifiers. */
enum {
    S_CASA = 0,
    S_PUEBLO,
    S_TALLER,
    S_VECINO,
    S_SENDERO,
    S_DESGUACE1,
    S_DESGUACE2,
    S_DESGUACE3,
    S_JEFE,
    /* zone 2 */
    S_COSTA,
    S_PUERTO,
    S_PUERTO_INT,
    S_BODEGA1,
    S_BODEGA2,
    S_JEFE2,
    /* zone 3 */
    S_CUESTA,
    S_VOLTIO,
    S_VOLTIO_INT,
    S_SUB1,
    S_SUB2,
    S_JEFE3,
    /* zone 4 */
    S_HUMO,
    S_FUNDICION,
    S_FUND_INT,
    S_HORNO1,
    S_HORNO2,
    S_JEFE4,
    /* zone 5 */
    S_PASO,
    S_CRIO,
    S_CRIO_INT,
    S_CUEVA1,
    S_CUEVA2,
    S_JEFE5,
    /* zone 6 */
    S_AUTOPISTA,
    S_MALLA,
    S_MALLA_INT,
    S_SERV1,
    S_SERV2,
    S_JEFE6,
    /* zone 7 */
    S_LLANURA,
    S_OXIDO,
    S_OXIDO_INT,
    S_CEMENT1,
    S_CEMENT2,
    S_JEFE7,
    /* zone 8 */
    S_ULTIMO,
    S_PRISMA,
    S_PRISMA_INT,
    S_TORRE1,
    S_TORRE2,
    S_CUMBRE,
    SALAS
};

/* --------------------------------------------------------------------------
 * ROOM 0 - Your house
 * -------------------------------------------------------------------------- */

static const char *const M_CASA[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "0000|||||||||||||||0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_ooooooooo___|0000",
    "0000|_ooooooooo___|0000",
    "0000|_ooooooooo___|0000",
    "0000|_ooooooooo___|0000",
    "0000|_ooooooooo___|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000||||||___||||||0000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_ent_t EN_CASA[] = {
    { E_PUERTA, 10, 17, S_PUEBLO,   4, 10, 3, NULL, NULL },
    { E_PNJ,     7,  5, 1, F_ABUELA_HABLO, 0, 0,
      N_("ABUELA TUERCA: BUEN DIA,\n"
      "DORMILON.\n"
      "TE ARME UN ROBOT CON LO\n"
      "QUE HABIA EN EL GALPON.\n"
      "NO ES GRAN COSA, PERO\n"
      "CAMINA Y PEGA.\n"
      "ANDA AL TALLER Y TE\n"
      "EXPLICO COMO SE CAMBIAN\n"
      "LAS PIEZAS."), NULL },
    { E_COFRE,  15,  5, IT_ACEITE, 2, F_COFRE_CASA, 0, NULL, NULL },
    { E_CARTEL, 15,  3, 0, 0, 0, 0,
      N_("UNA FOTO VIEJA: TU\n"
      "ABUELA JOVEN, AL LADO\n"
      "DE UN ROBOT ENORME.\n"
      "ABAJO DICE:\n"
      "CAMPEONA, ANO 12."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 1 - VILLA TUERCA
 * -------------------------------------------------------------------------- */

static const char *const M_PUEBLO[ROWS] = {
    "==========.....========",
    "          .....        ",
    "  BB      .....    BB  ",
    "  BB     .......   BB  ",
    "         .......       ",
    " ..................... ",
    " .                   . ",
    " .                   . ",
    " .                   . ",
    " .                   . ",
    " .                   . ",
    " .        .....      . ",
    " .        .....      . ",
    " .  ~~~~  .....      . ",
    " .  ~~~~  .....      . ",
    " .  ~~~~  .....      . ",
    " .        .....      . ",
    " ..................... ",
    "          .....        ",
    "          .....        ",
    "==========.....========",
    "          .....        ",
};

static const ch_prop_t P_PUEBLO[] = {
    { 3,  6, PR_CASA },          /* the door lands on cell 4               */
    { 14, 6, PR_TALLER },        /* the door lands on cell 16              */
    { 16,12, PR_FUENTE },
    { 0,  1, PR_ARBOL },
    { 21, 1, PR_ARBOL },
    { 0, 17, PR_ARBOL },
    { 21,17, PR_ARBOL },
    { 8,  9, PR_FAROLA },
    { 18,15, PR_FAROLA },
};

static const ch_ent_t EN_PUEBLO[] = {
    /* The doors go on the cell BELOW the threshold: that is where the player
     * stands, because the house's decoration blocks the way. */
    { E_PUERTA,  4,  9, S_CASA,    11, 16, 2, NULL, NULL },
    { E_PUERTA, 16,  9, S_TALLER,  11, 16, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_SENDERO,  7, 19, 5, NULL, NULL },
    { E_PUERTA, 10,  1, S_SENDERO,  7, 19, 5, NULL, NULL },
    { E_CARTEL,  6, 16, 0, 0, 0, 0,
      N_("VILLA TUERCA\n"
      "POBLACION: 34 PERSONAS\n"
      "Y UNOS CUANTOS ROBOTS."), NULL },
    { E_PNJ,    12,  8, 2, 0, 0, 0,
      N_("CHICO: MI HERMANA DICE\n"
      "QUE EN EL DESGUACE HAY\n"
      "PIEZAS BUENISIMAS.\n"
      "YO NO ENTRARIA. HAY\n"
      "ROBOTS SUELTOS."), NULL },
    /* The town's only quest with a reward. p3 is the flag you have to bring
     * and texto2 what they say when they see it: two pointers of a table
     * instead of a state machine. */
    { E_PNJ,     5, 11, 3, F_MISION_CUMPLIDA, F_TORNILLOS, IT_SOLDADOR,
      N_("VECINO: SE ME CAYERON\n"
      "LOS TORNILLOS EN EL\n"
      "DEPOSITO DEL DESGUACE.\n"
      "ESTA AL NORTE, PASANDO\n"
      "EL SENDERO.\n"
      "SI ME LOS TRAES, TE DOY\n"
      "ALGO BUENO."),
      N_("VECINO: LOS TORNILLOS!\n"
      "SOS UN FENOMENO.\n"
      "TOMA ESTE SOLDADOR, QUE\n"
      "A MI NO ME SIRVE Y A VOS\n"
      "TE VA A SALVAR.\n"
      "Y QUEDATE CON LOS\n"
      "CREDITOS TAMBIEN.") },
    { E_PNJ,    18, 10, 4, 0, 0, 0,
      N_("SENORA: CUIDADO CON LA\n"
      "FUENTE, QUE EL AGUA Y\n"
      "LOS CIRCUITOS NO SE\n"
      "LLEVAN BIEN."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 2 - The grandmother's workshop
 * -------------------------------------------------------------------------- */

static const char *const M_TALLER[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|---------++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++++++___++++++|000",
    "000|||||||___|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_TALLER_INT[] = {
    { 15,  3, PR_MAQUINA },
    { 8,   8, PR_MAQUINA },      /* the workbench: touched from below */
};

static const ch_ent_t EN_TALLER[] = {
    { E_PUERTA, 10, 17, S_PUEBLO,  16, 10, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0,
      N_("EL BANCO DE TRABAJO DE\n"
      "LA ABUELA.\n"
      "TU ROBOT QUEDA COMO\n"
      "NUEVO."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("ABUELA TUERCA: TENGO\n"
      "ACEITE, BATERIAS Y\n"
      "ALGUNA COSA MAS.\n"
      "MIRA TRANQUILO."), NULL },
    { E_PNJ,    16,  6, 1, 0, 0, 0,
      N_("ABUELA TUERCA: UN ROBOT\n"
      "SON CUATRO PIEZAS:\n"
      "CABEZA, TORSO, BRAZOS\n"
      "Y PIERNAS.\n"
      "CADA UNA CAMBIA LAS\n"
      "ESTADISTICAS Y TRAE SUS\n"
      "PROPIOS ATAQUES.\n"
      "CUANDO GANES UN COMBATE\n"
      "A VECES PODES ARRANCARLE\n"
      "UNA PIEZA AL RIVAL.\n"
      "ESAS VAN A LA MOCHILA\n"
      "Y SE MONTAN DESDE EL\n"
      "MENU, EN TALLER."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 3 - The neighbour's house (for now entered from the town later on)
 * -------------------------------------------------------------------------- */

static const char *const M_VECINO[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "0000|||||||||||||||0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000|_____________|0000",
    "0000||||||___||||||0000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_ent_t EN_VECINO[] = {
    { E_PUERTA, 10, 17, S_PUEBLO,  11,  6, 3, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 4 - Northern path
 * -------------------------------------------------------------------------- */

static const char *const M_SENDERO[ROWS] = {
    "     .....             ",
    "  BB .....   BB        ",
    "     .....             ",
    "\"\"\"  .....  \"\"\"\"\"\"\"    ",
    "\"\"\"  .....  \"\"\"\"\"\"\"    ",
    "\"\"\"  .....  \"\"\"\"\"\"\"    ",
    "     .....             ",
    "     ..................",
    "     .....             ",
    "  \"\"\"\"\"\"\"\"\"\"\"\"\"        ",
    "  \"\"\"\"\"\"\"\"\"\"\"\"\"        ",
    "  \"\"\"\"\"\"\"\"\"\"\"\"\"        ",
    "     .....             ",
    "     .....    \"\"\"\"\"    ",
    "  BB .....    \"\"\"\"\"    ",
    "     .....    \"\"\"\"\"    ",
    "     .....             ",
    "~~~~~.....~~~~~~~~~~~~~",
    "~~~~~^^^^^~~~~~~~~~~~~~",
    "     .....             ",
    "     .....             ",
    "     .....             ",
};

static const ch_prop_t P_SENDERO[] = {
    { 0,  0, PR_ARBOL },
    { 19, 2, PR_ARBOL },
    { 0, 12, PR_ARBOL },
    { 20,14, PR_ARBOL },
};

static const ch_ent_t EN_SENDERO[] = {
    { E_PUERTA,  5, 21, S_PUEBLO,   11,  2, 5, NULL, NULL },
    { E_PUERTA,  5, 20, S_PUEBLO,   11,  2, 5, NULL, NULL },
    { E_PUERTA,  5,  0, S_DESGUACE1, 11, 17, 5, NULL, NULL },
    { E_PUERTA,  5,  1, S_DESGUACE1, 11, 17, 5, NULL, NULL },
    { E_CARTEL, 15,  6, 0, 0, 0, 0,
      N_("AL NORTE: EL DESGUACE.\n"
      "PROHIBIDO EL PASO A\n"
      "ROBOTS SIN LICENCIA."), NULL },
    /* The control the Desguace's sub-boss opens. The whole progression of the
     * game is this: one flag and two texts. */
    { E_BLOQUEO, 19,  7, F_JEFE_DESGUACE, 0, 0, 0,
      N_("UN CONTROL BAJADO.\n"
      "CARTEL: PROHIBIDO PASAR AL\n"
      "ESTE SIN PASE DE SECTOR.\n"
      "EL PASE LO DA EL GUARDIAN\n"
      "DEL DESGUACE."),
      N_("EL CONTROL ESTA LEVANTADO.\n"
      "AL ESTE: PUERTO BUJIA.") },
    { E_PUERTA, 21,  7, S_COSTA,      2,  5, 2, NULL, NULL },
    { E_ENEMIGO, 13, 10, 1, F_ENEMIGO_1, 4, 0, NULL, NULL },
    { E_ENEMIGO,  3,  4, 1, F_ENEMIGO_2, 3, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 5 - The scrapyard, entrance
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE1[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMxxxMMMMMMMMMMxxxMMMX",
    "XMMxxxMMMMMMMMMMxxxMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMM***MMMM***MMMMMX",
    "XMMMMMM***MMMM***MMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMxxxxxMMMMMMxxxxxMMMX",
    "XMMxxxxxMMMMMMxxxxxMMMX",
    "XMMxxxxxMMMMMMxxxxxMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMM:::::MMMMMMMMX",
    "XMMMMMMMM:::::MMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_DESGUACE1[] = {
    { 2,  4, PR_PILA },
    { 18, 8, PR_PILA },
    { 5, 17, PR_PILA },
    { 17,16, PR_MAQUINA },
};

static const ch_ent_t EN_DESGUACE1[] = {
    { E_PUERTA, 10, 19, S_SENDERO,    7,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_SENDERO,    7,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_DESGUACE2, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_DESGUACE2, 11, 17, 3, NULL, NULL },
    { E_COFRE,    3,  8, IT_ACEITE, 1, F_COFRE_DESGUACE1, 0, NULL, NULL },
    { E_ENEMIGO, 16,  5, 1, F_ENEMIGO_3, 5, 0, NULL, NULL },
    { E_ENEMIGO,  6, 13, 1, F_ENEMIGO_4, 5, 0, NULL, NULL },
    { E_CARTEL,  12,  8, 0, 0, 0, 0,
      N_("UN CARTEL TORCIDO:\n"
      "DESGUACE MUNICIPAL\n"
      "NO ALIMENTE A LOS\n"
      "ROBOTS."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 6 - The scrapyard, corridor
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE2[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMXXXXXXMMMMMXXXXXXMMX",
    "XMMXxxxxXMMMMMXxxxxXMMX",
    "XMMXxxxxXMMMMMXxxxxXMMX",
    "XMMXxxxxXMMMMMXxxxxXMMX",
    "XMMXXXX:XMMMMMX:XXXXMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMM*****MMMMMMMMMX",
    "XMMMMMMM*****MMMMMMMMMX",
    "XMMMMMMM*****MMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMxxxMMMMMMMMMMMxxxMMX",
    "XMMxxxMMMMMMMMMMMxxxMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_DESGUACE2[] = {
    { 2,  9, PR_MAQUINA },
    { 19,11, PR_PILA },
};

static const ch_ent_t EN_DESGUACE2[] = {
    { E_PUERTA, 10, 19, S_DESGUACE1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_DESGUACE1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE,      11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE,      11, 17, 3, NULL, NULL },
    { E_PUERTA,  5,  5, S_DESGUACE3, 11, 16, 2, NULL, NULL },
    { E_COFRE,  17,  5, IT_BATERIA, 2, F_COFRE_DESGUACE2, 0, NULL, NULL },
    { E_ENEMIGO, 4, 12, 1, F_ENEMIGO_5, 6, 0, NULL, NULL },
    { E_ENEMIGO,18, 16, 1, F_ENEMIGO_6, 6, 0, NULL, NULL },
    { E_ENEMIGO,11, 13, 1, F_ENEMIGO_7, 7, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 7 - The store (the neighbour's screws)
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE3[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "0000XXXXXXXXXXXXXXX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMxxxxxxxxxMMX0000",
    "0000XMMxxxxxxxxxMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMM:::::::::MMX0000",
    "0000XMM:::::::::MMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMxxxxxxxxxMMX0000",
    "0000XMMxxxxxxxxxMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XMMMMMMMMMMMMMX0000",
    "0000XXXXXX:::XXXXXX0000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_DESGUACE3[] = {
    { 5,  3, PR_PILA },
    { 15,14, PR_PILA },
};

static const ch_ent_t EN_DESGUACE3[] = {
    { E_PUERTA, 10, 17, S_DESGUACE2,  6,  6, 3, NULL, NULL },
    { E_COFRE,  11,  6, IT_LLAVE, 1, F_COFRE_DESGUACE3, 0, NULL, NULL },
    { E_COFRE,   7, 11, IT_TORNILLOS, 1, F_TORNILLOS, 0, NULL, NULL },
    { E_ENEMIGO,15,  9, 1, F_ENEMIGO_8, 7, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 8 - The sub-boss
 * -------------------------------------------------------------------------- */

static const char *const M_JEFE[ROWS] = {
    "00000XXXXXXXXXXXXX00000",
    "00000X:::::::::::X00000",
    "00000X:::::::::::X00000",
    "0000XX:::::::::::XX0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000X:::::::::::::X0000",
    "0000XX:::::::::::XX0000",
    "00000X:::::::::::X00000",
    "00000X:::::::::::X00000",
    "00000X:::::::::::X00000",
    "00000X:::::::::::X00000",
    "00000XXXXX:::XXXXX00000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE[] = {
    { 5,  2, PR_MAQUINA },
    { 16, 2, PR_MAQUINA },
};

static const ch_ent_t EN_JEFE[] = {
    { E_PUERTA, 10, 19, S_DESGUACE2, 11, 17, 3, NULL, NULL },
    { E_JEFE,   11,  6, 3, F_JEFE_DESGUACE, 12, IT_PASE,
      N_("GUARDIAN DEL DESGUACE:\n"
      "ALTO AHI.\n"
      "NADIE SE LLEVA CHATARRA\n"
      "DE ACA SIN PASAR POR\n"
      "ENCIMA MIO.\n"
      "A VER QUE TRAJISTE."), NULL },
};

/* ==========================================================================
 * ZONE 2 - PUERTO BUJIA
 *
 * The rusted harbour. Dominant type: ACID. It is reached along the coast from
 * the Northern Path's control, and the sub-boss -the CAPATAZ, Chest torso-
 * guards the flooded hold.
 * ========================================================================== */

static const char *const M_COSTA[ROWS] = {
    "#######################",
    "##   ddd   ##   ddd  ##",
    "##   ddd   ##   ddd  ##",
    "##         ##        ##",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "##                   ##",
    "##  ddddd    ddddddd ##",
    "##  ddddd    ddddddd ##",
    "##                   ##",
    "SSSSSSSSSSSSSSSSSSSSSSS",
    "SSSSSSSSSSSSSSSSSSSSSSS",
    "SSSSSSSSSSSSSSSSSSSSSSS",
    "SSSSSSSSSSSSSSSSSSSSSSS",
    "zzzzzzzzzzzzzzzzzzzzzzz",
    "zzzzzzzzzzzzzzzzzzzzzzz",
    "~~~~~~~~~~~~~~~~~~~~~~~",
    "~~~~~~~~~~~~~~~~~~~~~~~",
    "~~~~~~~~~~~~~~~~~~~~~~~",
    "~~~~~~~~~~~~~~~~~~~~~~~",
    "~~~~~~~~~~~~~~~~~~~~~~~",
};

static const ch_prop_t P_COSTA[] = {
    { 3,  1, PR_PINO },
    { 18, 1, PR_PINO },
    { 8, 13, PR_BARCO },
    { 1,  7, PR_PILA },
};

static const ch_ent_t EN_COSTA[] = {
    { E_PUERTA,  0,  4, S_SENDERO, 20,  7, 2, NULL, NULL },
    { E_PUERTA,  0,  5, S_SENDERO, 20,  7, 2, NULL, NULL },
    { E_PUERTA,  0,  6, S_SENDERO, 20,  7, 2, NULL, NULL },
    { E_PUERTA, 21,  4, S_PUERTO,   2, 11, 2, NULL, NULL },
    { E_PUERTA, 21,  5, S_PUERTO,   2, 11, 2, NULL, NULL },
    { E_PUERTA, 21,  6, S_PUERTO,   2, 11, 2, NULL, NULL },
    { E_CARTEL,  4, 12, 0, 0, 0, 0,
      N_("PLAYA DEL CANGREJO.\n"
      "NO SE PERMITE OXIDARSE\n"
      "EN LA ARENA."), NULL },
    { E_COFRE,  19, 13, IT_ACEITE2, 1, F_COFRE_P1, 0, NULL, NULL },
    { E_ENEMIGO, 6,  8, 0, F_ENEMIGO_9,  10, 0, NULL, NULL },
    { E_ENEMIGO,16,  9, 0, F_ENEMIGO_10, 11, 0, NULL, NULL },
    { E_ENEMIGO,11, 12, 0, F_ENEMIGO_11, 12, 0, NULL, NULL },
};

static const char *const M_PUERTO[ROWS] = {
    "##########:::##########",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppp###",
    "#pppppppppppppppppppppp",
    "#pppppppppppppppppppppp",
    "#ppppppppppppppppppp###",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "GGGGGGGGGGGGGGGGGGGGGG#",
    "#ppppppppppppppppppppp#",
    "#pppppppppppppwwwwwwww#",
    "#pppppppppppppwwwwwwww#",
    "#SSSSSSSSSSSSSzzzzzzzz#",
    "#SSSSSSSSSSSSS~~~~~~~~#",
    "#SSSSSSSSSSSSS~~~~~~~~#",
    "#~~~~~~~~~~~~~~~~~~~~~#",
    "#~~~~~~~~~~~~~~~~~~~~~#",
    "#~~~~~~~~~~~~~~~~~~~~~#",
    "#######################",
};

static const ch_prop_t P_PUERTO[] = {
    { 3,  3, PR_CASA },
    { 14, 3, PR_TALLER },
    { 3,  7, PR_CASA },
    { 9,  1, PR_FAROLA },
    { 18,12, PR_FAROLA },
    { 16,12, PR_BARCO },
    { 19, 1, PR_ESTATUA },
};

static const ch_ent_t EN_PUERTO[] = {
    { E_PUERTA,  0, 11, S_COSTA,   20,  5, 2, NULL, NULL },
    { E_BLOQUEO,20,  4, F_JEFE_PUERTO, 0, 0, 0,
      N_("CONTROL DEL PUERTO.\n"
      "AL ESTE SUBE LA CUESTA\n"
      "HACIA ALTO VOLTIO.\n"
      "NO SE PASA SIN PASE."),
      N_("EL CONTROL ESTA ABIERTO.\n"
      "AL ESTE: LA CUESTA.") },
    { E_BLOQUEO,20,  5, F_JEFE_PUERTO, 0, 0, 0,
      N_("CONTROL DEL PUERTO.\n"
      "AL ESTE SUBE LA CUESTA\n"
      "HACIA ALTO VOLTIO."),
      N_("EL CONTROL ESTA ABIERTO.") },
    { E_PUERTA, 21,  4, S_CUESTA,   2, 10, 2, NULL, NULL },
    { E_PUERTA, 21,  5, S_CUESTA,   2, 11, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_BODEGA1, 11, 17, 3, NULL, NULL },
    { E_PUERTA,  4,  6, S_PUERTO_INT, 11, 16, 2, NULL, NULL },
    { E_PUERTA, 16,  6, S_PUERTO_INT, 11, 16, 2, NULL, NULL },
    { E_CARTEL,  9, 12, 0, 0, 0, 0,
      N_("PUERTO BUJIA\n"
      "AQUI SE DESCARGA TODO LO\n"
      "QUE SE OXIDA DESPUES.\n"
      "AL NORTE: LA BODEGA."), NULL },
    { E_PNJ,     7, 13, 4, 0, 0, 0,
      N_("PESCADORA: EL AGUA SALADA\n"
      "SE COME LOS CIRCUITOS.\n"
      "POR ESO ACA TODO PEGA\n"
      "CON ACIDO."), NULL },
    { E_PNJ,    12,  9, 2, 0, 0, 0,
      N_("CHICO: EN LA BODEGA HAY\n"
      "AGUA HASTA LAS RODILLAS\n"
      "Y ROBOTS QUE NO SALEN\n"
      "NUNCA DE AHI."), NULL },
    { E_PNJ,    19,  9, 3, F_MISION_ANCLA, F_ANCLA, IT_BATERIA2,
      N_("CAPITAN: PERDI EL ANCLA\n"
      "DE MI BARCO EN LA BODEGA.\n"
      "TRAEMELA Y TE DOY LA\n"
      "MEJOR BATERIA QUE TENGO."),
      N_("CAPITAN: EL ANCLA! AHORA\n"
      "SI PUEDO SALIR A PESCAR.\n"
      "TOMA, TE LA GANASTE.") },
};

static const char *const M_PUERTO_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|------++++++++-|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++ooooooo+++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++++++___++++++|000",
    "000|||||||___|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_PUERTO_INT[] = {
    { 15,  3, PR_MAQUINA },
    { 8,   7, PR_MAQUINA },
};

static const ch_ent_t EN_PUERTO_INT[] = {
    { E_PUERTA, 10, 17, S_PUERTO, 16,  7, 3, NULL, NULL },
    { E_TALLER,  8,  8, 0, 0, 0, 0,
      N_("EL BANCO DEL PUERTO.\n"
      "HUELE A SALITRE PERO\n"
      "DEJA EL ROBOT COMO NUEVO."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("MECANICO: TENGO DE TODO,\n"
      "Y TODO UN POCO OXIDADO.\n"
      "MIRA TRANQUILO."), NULL },
    { E_PNJ,    16,  7, 1, 0, 0, 0,
      N_("ABUELA TUERCA: TE SEGUI\n"
      "HASTA ACA PARA DECIRTE\n"
      "UNA COSA.\n"
      "EL TIPO DEL TORSO MANDA:\n"
      "SI PEGAS CON TU PROPIO\n"
      "TIPO, HACES MAS DANO.\n"
      "ACA TODOS PEGAN CON\n"
      "ACIDO. LLEVA ALGO QUE\n"
      "RESISTA."), NULL },
};

static const char *const M_BODEGA1[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMzzzzMMMMMMMMMzzzzMMX",
    "XMMzzzzMMMMMMMMMzzzzMMX",
    "XMMzzzzMMMMMMMMMzzzzMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMXXXXXXXXXMMMMMMX",
    "XMMMMMMXzzzzzzzXMMMMMMX",
    "XMMMMMMXzzzzzzzXMMMMMMX",
    "XMMMMMMXzzzzzzzXMMMMMMX",
    "XMMMMMMXXXX:XXXXMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMzzzzzMMMMMMMzzzzzMMX",
    "XMMzzzzzMMMMMMMzzzzzMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_BODEGA1[] = {
    { 2,  1, PR_PILA },
    { 19,15, PR_PILA },
    { 17, 6, PR_MAQUINA },
};

static const ch_ent_t EN_BODEGA1[] = {
    { E_PUERTA, 10, 19, S_PUERTO,  11,  1, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_PUERTO,  11,  1, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_BODEGA2, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_BODEGA2, 11, 17, 3, NULL, NULL },
    { E_COFRE,  11,  9, IT_CHIP, 1, F_COFRE_P3, 0, NULL, NULL },
    { E_ENEMIGO, 4, 12, 0, F_ENEMIGO_12, 13, 0, NULL, NULL },
    { E_ENEMIGO,18, 12, 0, F_ENEMIGO_13, 13, 0, NULL, NULL },
    { E_CARTEL,  6,  2, 0, 0, 0, 0,
      N_("BODEGA MUNICIPAL\n"
      "NIVEL DEL AGUA: ALTO.\n"
      "NO RESPONDEMOS POR\n"
      "CIRCUITOS MOJADOS."), NULL },
};

static const char *const M_BODEGA2[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMzzzzzzzzzzzzzzzzzzzMX",
    "XMzzzzzzzzzzzzzzzzzzzMX",
    "XMzzXXXXXzzzzXXXXXzzzMX",
    "XMzzXxxxXzzzzXxxxXzzzMX",
    "XMzzXxxxXzzzzXxxxXzzzMX",
    "XMzzXXX:XzzzzX:XXXzzzMX",
    "XMzzzzzzzzzzzzzzzzzzzMX",
    "XMzzzzzzzzzzzzzzzzzzzMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMzzzzzMMMMMMMzzzzzMMX",
    "XMMzzzzzMMMMMMMzzzzzMMX",
    "XMMMMMMMMMMMMMMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XMMMMMMMMX:::XMMMMMMMMX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_BODEGA2[] = {
    { 2, 12, PR_MAQUINA },
    { 19, 2, PR_PILA },
};

static const ch_ent_t EN_BODEGA2[] = {
    { E_PUERTA, 10, 19, S_BODEGA1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_BODEGA1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE2,   11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE2,   11, 17, 3, NULL, NULL },
    { E_COFRE,   6,  7, IT_SOLDADOR, 1, F_COFRE_P2, 0, NULL, NULL },
    { E_COFRE,  16,  7, IT_ANCLA, 1, F_ANCLA, 0, NULL, NULL },
    { E_ENEMIGO, 4,  4, 0, F_ENEMIGO_14, 14, 0, NULL, NULL },
    { E_ENEMIGO,18,  4, 0, F_ENEMIGO_15, 14, 0, NULL, NULL },
    { E_ENEMIGO,11, 11, 0, F_ENEMIGO_16, 15, 0, NULL, NULL },
};

static const char *const M_JEFE2[ROWS] = {
    "00000XXXXXXXXXXXXX00000",
    "00000XwwwwwwwwwwwX00000",
    "00000XwwwwwwwwwwwX00000",
    "0000XXwwwwwwwwwwwXX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XwwwwwwwwwwwwwX0000",
    "0000XXwwwwwwwwwwwXX0000",
    "00000XwwwwwwwwwwwX00000",
    "00000XwwwwwwwwwwwX00000",
    "00000Xwwwww:::wwwwX0000",
    "00000Xwwwww:::wwwwX0000",
    "00000XXXXXX:::XXXXX0000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE2[] = {
    { 6,  2, PR_PILA },
    { 15, 2, PR_PILA },
};

static const ch_ent_t EN_JEFE2[] = {
    { E_PUERTA, 11, 19, S_BODEGA2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  6, 7, F_JEFE_PUERTO, 18, IT_PASE,
      N_("CAPATAZ DE LA BODEGA:\n"
      "ASI QUE VOS SOS EL QUE\n"
      "ANDA LEVANTANDO PIEZAS.\n"
      "ACA ABAJO TODO SE OXIDA,\n"
      "PIBE. TAMBIEN VOS."), NULL },
};

/* ==========================================================================
 * ZONE 3 - ALTO VOLTIO
 *
 * The plateau town, among high-tension pylons. Dominant type: VOLT. The
 * sub-boss is the INGENIERO (Cell torso) and guards the substation.
 * ========================================================================== */

static const char *const M_CUESTA[ROWS] = {
    "###########GGG#########",
    "###########GGG#########",
    "##  dddd   GGG   dddd##",
    "##  dddd   GGG   dddd##",
    "##         GGG        #",
    "##  GGGGGGGGGGGGGGG   #",
    "##  GGG        GGG    #",
    "##  GGG  dddd  GGG    #",
    "##  GGG  dddd  GGG    #",
    "##  GGG        GGG    #",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "##  GGG        GGG    #",
    "##  GGG dddddd GGG    #",
    "##  GGG dddddd GGG    #",
    "##  GGG        GGG    #",
    "##  GGGGGGGGGGGGGG    #",
    "##                    #",
    "##   dddd     dddd    #",
    "##   dddd     dddd    #",
    "#######################",
};

static const ch_prop_t P_CUESTA[] = {
    { 5,  5, PR_TORRE },
    { 16, 5, PR_TORRE },
    { 5, 13, PR_TORRE },
    { 2, 18, PR_PINO },
    { 18,18, PR_PINO },
    { 19,13, PR_PILA },
};

static const ch_ent_t EN_CUESTA[] = {
    { E_PUERTA,  0, 10, S_PUERTO,  19,  4, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_PUERTO,  19,  4, 2, NULL, NULL },
    { E_PUERTA,  0, 12, S_PUERTO,  19,  4, 2, NULL, NULL },
    { E_PUERTA, 11,  0, S_VOLTIO,  11, 18, 3, NULL, NULL },
    { E_PUERTA, 11,  1, S_VOLTIO,  11, 18, 3, NULL, NULL },
    { E_CARTEL, 10,  4, 0, 0, 0, 0,
      N_("LA CUESTA.\n"
      "ARRIBA: ALTO VOLTIO.\n"
      "NO TOQUE LOS CABLES\n"
      "CON LAS MANOS."), NULL },
    { E_COFRE,   4,  7, IT_BATERIA2, 1, F_COFRE_V1, 0, NULL, NULL },
    { E_ENEMIGO, 9, 14, 0, F_ENEMIGO_17, 16, 0, NULL, NULL },
    { E_ENEMIGO,17, 19, 0, F_ENEMIGO_18, 17, 0, NULL, NULL },
    { E_ENEMIGO, 5,  2, 0, F_ENEMIGO_19, 18, 0, NULL, NULL },
};

static const char *const M_VOLTIO[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGG###",
    "#GGGGGGGGGpppGGGGGGGppp",
    "#GGGGGGGGGpppGGGGGGGppp",
    "#GGGGGGGGGpppGGGGGGG###",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "#GGGGGGGGGpppGGGGGGGGG#",
    "##########:::##########",
    "##########:::##########",
    "00000000000000000000000",
};

static const ch_prop_t P_VOLTIO[] = {
    { 3,  2, PR_CASA },
    { 15, 2, PR_TALLER },
    { 3, 14, PR_TORRE },
    { 17,14, PR_TORRE },
    { 6, 16, PR_ESTATUA },
    { 2,  7, PR_FAROLA },
    { 18, 7, PR_FAROLA },
};

static const ch_ent_t EN_VOLTIO[] = {
    { E_PUERTA, 10,  0, S_CUESTA,  11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_CUESTA,  11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 19, S_SUB1,    11, 17, 3, NULL, NULL },
    { E_PUERTA, 10, 20, S_SUB1,    11, 17, 3, NULL, NULL },
    { E_PUERTA, 16,  5, S_VOLTIO_INT, 11, 16, 2, NULL, NULL },
    { E_BLOQUEO,20,  9, F_JEFE_VOLTIO, 0, 0, 0,
      N_("CONTROL DE ALTO VOLTIO.\n"
      "AL ESTE BAJA AL VALLE\n"
      "DEL HUMO. CERRADO HASTA\n"
      "QUE ALGUIEN ARREGLE LA\n"
      "SUBESTACION."),
      N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL VALLE DEL\n"
      "HUMO.") },
    { E_BLOQUEO,20, 10, F_JEFE_VOLTIO, 0, 0, 0,
      N_("CONTROL DE ALTO VOLTIO.\n"
      "CERRADO."),
      N_("CONTROL ABIERTO.") },
    { E_PUERTA, 21,  9, S_HUMO,     2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_HUMO,     2, 11, 2, NULL, NULL },
    { E_CARTEL, 13,  6, 0, 0, 0, 0,
      N_("ALTO VOLTIO\n"
      "ALTURA: 900 METROS\n"
      "CONSUMO: TODO EL QUE\n"
      "HAGA FALTA."), NULL },
    { E_PNJ,     6,  6, 2, 0, 0, 0,
      N_("CHICO: SI TOCAS UN CABLE\n"
      "PELADO se te frien los\n"
      "CIRCUITOS.\n"
      "A MI HERMANO LE PASO."), NULL },
    { E_PNJ,    16, 13, 4, 0, 0, 0,
      N_("SENORA: LA SUBESTACION\n"
      "SE LLENO DE ROBOTS Y\n"
      "NADIE PUEDE ENTRAR.\n"
      "SIN ELLA NO HAY LUZ."), NULL },
    { E_PNJ,     4, 12, 3, F_MISION_FUSIBLE, F_FUSIBLE, IT_CHIP,
      N_("TECNICO: NECESITO UN\n"
      "FUSIBLE GRUESO DE LOS\n"
      "QUE HAY EN LA SUBESTACION.\n"
      "SI ME LO TRAES TE DOY\n"
      "UN CHIP DE DATOS."),
      N_("TECNICO: EL FUSIBLE!\n"
      "AHORA SI PUEDO ARREGLAR\n"
      "EL TABLERO. TOMA.") },
};

static const char *const M_VOLTIO_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|+++++++++++++++|000",
    "000|-------++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++ooooooooo++++|000",
    "000|++ooooooooo++++|000",
    "000|++ooooooooo++++|000",
    "000|++ooooooooo++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++++++___++++++|000",
    "000|||||||___|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_VOLTIO_INT[] = {
    { 15,  5, PR_MAQUINA },
    { 5,  10, PR_MAQUINA },
};

static const ch_ent_t EN_VOLTIO_INT[] = {
    { E_PUERTA, 10, 17, S_VOLTIO, 16,  6, 3, NULL, NULL },
    { E_TALLER,  5, 11, 0, 0, 0, 0,
      N_("EL BANCO DE ALTO VOLTIO.\n"
      "Aca la corriente sobra."), NULL },
    { E_TIENDA,  5,  3, 0, 0, 0, 0,
      N_("TENDERA: BATERIAS,\n"
      "ACEITE Y LO DE SIEMPRE.\n"
      "TODO CON GARANTIA DE\n"
      "TRES DIAS."), NULL },
    { E_PNJ,    16,  8, 1, 0, 0, 0,
      N_("ABUELA TUERCA: PRESTA\n"
      "ATENCION A LA VELOCIDAD.\n"
      "EL QUE PEGA PRIMERO PEGA\n"
      "DOS VECES, PORQUE EL OTRO\n"
      "PUEDE NO LLEGAR A PEGAR.\n"
      "UNAS PIERNAS BUENAS VALEN\n"
      "MAS QUE UN BRAZO CARO."), NULL },
};

static const char *const M_SUB1[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CccCCCCCcccccCCCCCCCccC",
    "CccCcccCcccccCcccccCccC",
    "CccCcccCcccccCcccccCccC",
    "CccCCC:CcccccC:CCCCCccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccccccCCCCCCCCCccccccC",
    "CccccccCcccccccCccccccC",
    "CccccccCcccccccCccccccC",
    "CccccccCCCC:CCCCccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccccccccC:::CccccccccC",
    "CccccccccC:::CccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_SUB1[] = {
    { 2, 13, PR_SERVIDOR },
    { 19, 7, PR_SERVIDOR },
};

static const ch_ent_t EN_SUB1[] = {
    { E_PUERTA, 10, 19, S_VOLTIO,  11, 18, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_VOLTIO,  11, 18, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_SUB2,    11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_SUB2,    11, 17, 3, NULL, NULL },
    { E_COFRE,   5,  4, IT_ACEITE2, 2, F_COFRE_V2, 0, NULL, NULL },
    { E_COFRE,  16,  4, IT_FUSIBLE, 1, F_FUSIBLE, 0, NULL, NULL },
    { E_ENEMIGO,11, 10, 0, F_ENEMIGO_20, 19, 0, NULL, NULL },
    { E_ENEMIGO, 4,  8, 0, F_ENEMIGO_21, 19, 0, NULL, NULL },
    { E_CARTEL, 18, 14, 0, 0, 0, 0,
      N_("TABLERO GENERAL.\n"
      "NO OPERAR SIN GUANTES.\n"
      "(ALGUIEN TACHO LO DE\n"
      "LOS GUANTES.)"), NULL },
};

static const char *const M_SUB2[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccCCCCCCCCCCCCCCCCCccC",
    "CccCcccccccccccccccCccC",
    "CccCcccccccccccccccCccC",
    "CccCccCCCCCCCCCCCccCccC",
    "CccCccCcccccccccCccCccC",
    "CccCccCcccccccccCccCccC",
    "CccCccCccCCCCCccCccCccC",
    "CccCccCccC:::CccCccCccC",
    "CccCccCccCCCCCccCccCccC",
    "CccCccCcccccccccCccCccC",
    "CccCccCCCCCC:CCCCccCccC",
    "CccCcccccccccccccccCccC",
    "CccCCCCCCC:CCCCCCCCCccC",
    "CccccccccC:::CccccccccC",
    "CccccccccC:::CccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_SUB2[] = {
    { 1,  2, PR_SERVIDOR },
    { 20, 2, PR_SERVIDOR },
};

static const ch_ent_t EN_SUB2[] = {
    { E_PUERTA, 10, 19, S_SUB1,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_SUB1,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE3,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE3,  11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 11, IT_SOLDADOR, 1, F_COFRE_V3, 0, NULL, NULL },
    { E_ENEMIGO, 8, 15, 0, F_ENEMIGO_22, 20, 0, NULL, NULL },
    { E_ENEMIGO,14,  5, 0, F_ENEMIGO_23, 21, 0, NULL, NULL },
};

static const char *const M_JEFE3[ROWS] = {
    "0000CCCCCCCCCCCCCCC0000",
    "0000CcccccccccccccC0000",
    "0000CcccccccccccccC0000",
    "000CCcccccccccccccCC000",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CCcccccccccccccCC000",
    "0000CcccccccccccccC0000",
    "0000CcccccccccccccC0000",
    "0000Cccccc:::ccccccC000",
    "0000Cccccc:::ccccccC000",
    "0000CCCCCC:::CCCCCCC000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE3[] = {
    { 5,  2, PR_SERVIDOR },
    { 15, 2, PR_SERVIDOR },
};

static const ch_ent_t EN_JEFE3[] = {
    { E_PUERTA, 10, 19, S_SUB2,   11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  6, 11, F_JEFE_VOLTIO, 24, IT_PASE,
      N_("INGENIERA JEFA:\n"
      "ASI QUE VENIS A ARREGLAR\n"
      "LA SUBESTACION.\n"
      "PRIMERO ARREGLA ESTO:\n"
      "MI ROBOT CONTRA EL TUYO."), NULL },
};

/* ==========================================================================
 * ZONE 4 - FUNDICION
 *
 * The valley of smoke and the town of the furnaces. Dominant type: FIRE.
 * ========================================================================== */

static const char *const M_HUMO[ROWS] = {
    "#######################",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrLLLLrrrrrrrLLLLrrr#",
    "#rrrLLLLrrrrrrrLLLLrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrddddrrrrrrrddddrrr#",
    "#rrrddddrrrrrrrddddrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrrLLLLLLLLLLLLLrrrr#",
    "#rrrrLLLLLLLLLLLLLrrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrddddddrrrddddddrrr#",
    "#rrrddddddrrrddddddrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#######################",
};

static const ch_prop_t P_HUMO[] = {
    { 2, 19, PR_PILA },
    { 19,19, PR_PILA },
    { 10, 5, PR_MAQUINA },
};

static const ch_ent_t EN_HUMO[] = {
    { E_PUERTA,  0,  9, S_VOLTIO,  19,  9, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_VOLTIO,  19,  9, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_VOLTIO,  19, 10, 2, NULL, NULL },
    { E_PUERTA, 21,  9, S_FUNDICION, 2,  9, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_FUNDICION, 2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_FUNDICION, 2, 10, 2, NULL, NULL },
    { E_CARTEL, 11, 12, 0, 0, 0, 0,
      N_("VALLE DEL HUMO.\n"
      "NO SE DETENGA.\n"
      "EL AIRE NO ES BUENO\n"
      "PARA NADIE."), NULL },
    { E_COFRE,  19, 17, IT_ACEITE2, 2, F_COFRE_F1, 0, NULL, NULL },
    { E_ENEMIGO, 5,  6, 0, F_ENEMIGO_24, 22, 0, NULL, NULL },
    { E_ENEMIGO,17, 16, 0, F_ENEMIGO_25, 23, 0, NULL, NULL },
    { E_ENEMIGO, 6, 17, 0, F_ENEMIGO_26, 23, 0, NULL, NULL },
};

static const char *const M_FUNDICION[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "###rrrrrrrGGGrrrrrrrrr#",
    "GGGGGGGGGGGGGGGGGGGGGG#",
    "GGGGGGGGGGGGGGGGGGGGGG#",
    "###rrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrr###",
    "#GGGGGGGGGGGGGGGGGGGGGG",
    "#GGGGGGGGGGGGGGGGGGGGGG",
    "#rrrrrrrrrGGGrrrrrrr###",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrGGGrrrrrrrrr#",
    "#rrrrrrrrrrrrrrrrrrrrr#",
    "#######################",
};

static const ch_prop_t P_FUNDICION[] = {
    { 14, 2, PR_HORNO },
    { 3,  2, PR_CASA },
    { 3, 12, PR_TALLER },
    { 15,12, PR_HORNO },
    { 6, 18, PR_PILA },
    { 17,18, PR_PILA },
};

static const ch_ent_t EN_FUNDICION[] = {
    { E_PUERTA,  0,  9, S_HUMO,    20, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_HUMO,    20, 10, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_HORNO1,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_HORNO1,  11, 17, 3, NULL, NULL },
    { E_PUERTA,  4, 15, S_FUND_INT, 11, 16, 2, NULL, NULL },
    { E_BLOQUEO,20, 15, F_JEFE_FUNDICION, 0, 0, 0,
      N_("CONTROL DE LA FUNDICION.\n"
      "AL ESTE EMPIEZA EL PASO\n"
      "HELADO. NO SE PASA SIN\n"
      "PASE DE SECTOR."),
      N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL PASO HELADO.") },
    { E_BLOQUEO,20, 16, F_JEFE_FUNDICION, 0, 0, 0,
      N_("CONTROL DE LA FUNDICION.\n"
      "CERRADO."),
      N_("CONTROL ABIERTO.") },
    { E_PUERTA, 21, 15, S_PASO,     2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 16, S_PASO,     2, 11, 2, NULL, NULL },
    { E_CARTEL, 12,  9, 0, 0, 0, 0,
      N_("FUNDICION\n"
      "AQUI SE FUNDE TODO LO\n"
      "QUE EL PUERTO OXIDA.\n"
      "AL NORTE: EL HORNO."), NULL },
    { E_PNJ,     7,  6, 3, 0, 0, 0,
      N_("FUNDIDOR: EL METAL\n"
      "CALIENTE NO PERDONA.\n"
      "SI TU ROBOT ES DE HIELO,\n"
      "ACA LA VAS A PASAR MAL."), NULL },
    { E_PNJ,    17,  8, 2, 0, 0, 0,
      N_("CHICA: DICEN QUE EN EL\n"
      "HORNO VIVE UN ROBOT\n"
      "HECHO DE LAVA.\n"
      "YO NO LO VI, EH."), NULL },
    { E_PNJ,    15, 17, 4, F_MISION_MOLDE, F_MOLDE, IT_ACEITE2,
      N_("MOLDEADORA: SE ME QUEDO\n"
      "UN MOLDE ADENTRO DEL\n"
      "HORNO Y NO PIENSO IR.\n"
      "TRAEMELO Y ARREGLAMOS."),
      N_("MOLDEADORA: EL MOLDE!\n"
      "SOS DE FIERRO, PIBE.\n"
      "TOMA ESTO PARA EL CAMINO.") },
};

static const char *const M_FUND_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|_______________|000",
    "000|_______________|000",
    "000|--------_______|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|___ooooooo_____|000",
    "000|___ooooooo_____|000",
    "000|___ooooooo_____|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|______+++______|000",
    "000|||||||+++|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_FUND_INT[] = {
    { 15,  3, PR_HORNO },
    { 5,  10, PR_MAQUINA },
};

static const ch_ent_t EN_FUND_INT[] = {
    { E_PUERTA, 10, 17, S_FUNDICION, 4, 16, 3, NULL, NULL },
    { E_TALLER,  5, 11, 0, 0, 0, 0,
      N_("EL BANCO DE LA FUNDICION.\n"
      "Se suelda todo de una."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("HERRERO: LO QUE NECESITES\n"
      "PARA AGUANTAR EL CALOR."), NULL },
    { E_PNJ,    16,  8, 1, 0, 0, 0,
      N_("ABUELA TUERCA: ACORDATE\n"
      "DE MIRAR LOS TIPOS.\n"
      "EL FUEGO DERRITE EL HIELO\n"
      "Y CORROE EL ACIDO, PERO\n"
      "CONTRA EL VOLT NO HACE\n"
      "NADA.\n"
      "MIRA LA FICHA ANTES DE\n"
      "PEGAR."), NULL },
};

static const char *const M_HORNO1[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrLLLLrrrrrrrrrLLLLrrX",
    "XrrLLLLrrrrrrrrrLLLLrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrrrXXXXXXXXXrrrrrrX",
    "XrrrrrrXrrrrrrrXrrrrrrX",
    "XrrrrrrXrrrrrrrXrrrrrrX",
    "XrrrrrrXrrrrrrrXrrrrrrX",
    "XrrrrrrXXXX:XXXXrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrLLLLLrrrrrrrLLLLLrrX",
    "XrrLLLLLrrrrrrrLLLLLrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_HORNO1[] = {
    { 1, 11, PR_HORNO },
    { 18, 6, PR_PILA },
};

static const ch_ent_t EN_HORNO1[] = {
    { E_PUERTA, 10, 19, S_FUNDICION, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_FUNDICION, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_HORNO2,    11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_HORNO2,    11, 17, 3, NULL, NULL },
    { E_COFRE,  11,  8, IT_MOLDE, 1, F_MOLDE, 0, NULL, NULL },
    { E_ENEMIGO, 4, 16, 0, F_ENEMIGO_27, 25, 0, NULL, NULL },
    { E_ENEMIGO,18, 16, 0, F_ENEMIGO_28, 25, 0, NULL, NULL },
};

static const char *const M_HORNO2[ROWS] = {
    "XXXXXXXXXX:::XXXXXXXXXX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrLLLLLLLLLrLLLLLLLLLrX",
    "XrLLLLLLLLLrLLLLLLLLLrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrLLLLLrrrrrrrrrLLLLLrX",
    "XrLLLLLrrrrrrrrrLLLLLrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrrrrXXXXXXXXXXXXXrrrrX",
    "XrrrrXrrrrrrrrrrrXrrrrX",
    "XrrrrXrrrrrrrrrrrXrrrrX",
    "XrrrrXXXXXX:XXXXXXrrrrX",
    "XrrrrrrrrrrrrrrrrrrrrrX",
    "XrLLLLLLLrrrrrLLLLLLLrX",
    "XrLLLLLLLrrrrrLLLLLLLrX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XrrrrrrrrX:::XrrrrrrrrX",
    "XXXXXXXXXX:::XXXXXXXXXX",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_HORNO2[] = {
    { 1,  5, PR_HORNO },
    { 18,11, PR_PILA },
};

static const ch_ent_t EN_HORNO2[] = {
    { E_PUERTA, 10, 19, S_HORNO1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_HORNO1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE4,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE4,  11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 11, IT_BATERIA2, 2, F_COFRE_F2, 0, NULL, NULL },
    { E_ENEMIGO, 6,  6, 0, F_ENEMIGO_29, 26, 0, NULL, NULL },
    { E_ENEMIGO,16, 14, 0, F_ENEMIGO_30, 27, 0, NULL, NULL },
};

static const char *const M_JEFE4[ROWS] = {
    "0000XXXXXXXXXXXXXXX0000",
    "0000XrrrrrrrrrrrrrX0000",
    "0000XrLLLLLLLLLLLrX0000",
    "000XXrLLLLLLLLLLLrXX000",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrrrrrrrrrrrrrrrrX00",
    "000XrLLLLLLLLLLLLLLrX00",
    "000XXrLLLLLLLLLLLLrXX00",
    "0000XrrrrrrrrrrrrrX0000",
    "0000XrrrrrrrrrrrrrX0000",
    "0000Xrrrrr:::rrrrrX0000",
    "0000Xrrrrr:::rrrrrX0000",
    "0000XXXXXX:::XXXXXX0000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE4[] = {
    { 5,  5, PR_HORNO },
};

static const ch_ent_t EN_JEFE4[] = {
    { E_PUERTA, 10, 19, S_HORNO2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  8, 10, F_JEFE_FUNDICION, 30, IT_PASE,
      N_("MAESTRO FUNDIDOR:\n"
      "TREINTA ANOS METIENDO\n"
      "CHATARRA EN ESE HORNO.\n"
      "SE CUANDO UNA PIEZA\n"
      "SIRVE Y CUANDO NO.\n"
      "LA TUYA TODAVIA NO SE."), NULL },
};

/* ==========================================================================
 * ZONE 5 - CRIOVALLE
 *
 * The frozen pass and the snow town. Dominant type: CRYO. The sub-boss is the
 * GUARDABOSQUE (Turbine torso) at the back of the ice cave.
 * ========================================================================== */

static const char *const M_PASO[ROWS] = {
    "#######################",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnhhhhnnnnnnnhhhhnnn#",
    "#nnnhhhhnnnnnnnhhhhnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnNNNNnnnnnnnNNNNnnn#",
    "#nnnNNNNnnnnnnnNNNNnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnhhhhhhhhhhhhhnnnn#",
    "#nnnnhhhhhhhhhhhhhnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnNNNNNNnnnNNNNNNnnn#",
    "#nnnNNNNNNnnnNNNNNNnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#######################",
};

static const ch_prop_t P_PASO[] = {
    { 2,  1, PR_PINO },
    { 19, 1, PR_PINO },
    { 2, 19, PR_PINO },
    { 19,19, PR_PINO },
    { 10, 4, PR_PINO },
};

static const ch_ent_t EN_PASO[] = {
    { E_PUERTA,  0,  9, S_FUNDICION, 19, 15, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_FUNDICION, 19, 15, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_FUNDICION, 19, 16, 2, NULL, NULL },
    { E_PUERTA, 21,  9, S_CRIO,      2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_CRIO,      2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_CRIO,      2, 10, 2, NULL, NULL },
    { E_CARTEL, 11, 12, 0, 0, 0, 0,
      N_("PASO HELADO.\n"
      "SI SE LE CONGELAN LOS\n"
      "SERVOS, NO INSISTA.\n"
      "ESPERE AL DESHIELO."), NULL },
    { E_COFRE,   5, 19, IT_ACEITE2, 2, F_COFRE_C1, 0, NULL, NULL },
    { E_ENEMIGO, 6,  5, 0, F_ENEMIGO_31, 28, 0, NULL, NULL },
    { E_ENEMIGO,16, 18, 0, F_ENEMIGO_32, 29, 0, NULL, NULL },
    { E_ENEMIGO, 5, 15, 0, F_ENEMIGO_33, 29, 0, NULL, NULL },
};

static const char *const M_CRIO[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "###nnnnnnnGGGnnnnnnn###",
    "GGGnnnnnnnGGGnnnnnnnGGG",
    "GGGnnnnnnnGGGnnnnnnnGGG",
    "###nnnnnnnGGGnnnnnnn###",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnGGGnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#######################",
};

static const ch_prop_t P_CRIO[] = {
    { 3,  2, PR_CASA },
    { 15, 2, PR_TALLER },
    { 2, 14, PR_PINO },
    { 18,14, PR_PINO },
    { 6, 17, PR_PINO },
    { 15,17, PR_PINO },
    { 8,  4, PR_FAROLA },
    { 14, 4, PR_FAROLA },
};

static const ch_ent_t EN_CRIO[] = {
    { E_PUERTA,  0,  9, S_PASO,   19, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_PASO,   19, 10, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_CUEVA1, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_CUEVA1, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 16,  6, S_CRIO_INT, 11, 16, 2, NULL, NULL },
    { E_BLOQUEO,20,  9, F_JEFE_CRIO, 0, 0, 0,
      N_("CONTROL DE CRIOVALLE.\n"
      "AL ESTE BAJA LA AUTOPISTA\n"
      "A CIUDAD MALLA.\n"
      "CERRADO POR NIEVE."),
      N_("CONTROL ABIERTO.\n"
      "AL ESTE: LA AUTOPISTA.") },
    { E_BLOQUEO,20, 10, F_JEFE_CRIO, 0, 0, 0,
      N_("CONTROL DE CRIOVALLE.\n"
      "CERRADO POR NIEVE."),
      N_("CONTROL ABIERTO.") },
    { E_PUERTA, 21,  9, S_AUTOPISTA, 2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_AUTOPISTA, 2, 11, 2, NULL, NULL },
    { E_CARTEL, 13,  7, 0, 0, 0, 0,
      N_("CRIOVALLE\n"
      "TEMPERATURA MEDIA: -12.\n"
      "LOS CIRCUITOS DURAN MAS\n"
      "PERO LAS BATERIAS MENOS."), NULL },
    { E_PNJ,     6,  7, 2, 0, 0, 0,
      N_("CHICO: EN LA CUEVA EL\n"
      "PISO ES DE HIELO PURO.\n"
      "MI PAPA DICE QUE ADENTRO\n"
      "HAY ALGO QUE NO SE\n"
      "DERRITE NUNCA."), NULL },
    { E_PNJ,    17, 13, 3, 0, 0, 0,
      N_("LENADOR: LOS BICHOS DE\n"
      "ACA PEGAN CON CRIO.\n"
      "SI LLEVAS ALGO DE FUEGO\n"
      "LA VAS A PASAR MEJOR."), NULL },
    { E_PNJ,     5, 12, 4, F_MISION_TERMO, F_TERMO, IT_SOLDADOR,
      N_("ABUELA DEL VALLE: SE ME\n"
      "QUEDO EL TERMO EN LA\n"
      "CUEVA Y SIN EL NO HAY\n"
      "MATE.\n"
      "TRAEMELO, SI?"),
      N_("ABUELA DEL VALLE: MI\n"
      "TERMO! GRACIAS, HIJO.\n"
      "TOMA ESTE SOLDADOR QUE\n"
      "ERA DE MI MARIDO.") },
};

static const char *const M_CRIO_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______--------|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|____ooooooo____|000",
    "000|____ooooooo____|000",
    "000|____ooooooo____|000",
    "000|____ooooooo____|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|______+++______|000",
    "000|||||||+++|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CRIO_INT[] = {
    { 4,   3, PR_MAQUINA },
    { 15, 10, PR_MAQUINA },
};

static const ch_ent_t EN_CRIO_INT[] = {
    { E_PUERTA, 10, 17, S_CRIO, 16,  7, 3, NULL, NULL },
    { E_TALLER, 15, 11, 0, 0, 0, 0,
      N_("EL BANCO DE CRIOVALLE.\n"
      "Hay que soplarle el hielo\n"
      "antes de usarlo."), NULL },
    { E_TIENDA, 16,  4, 0, 0, 0, 0,
      N_("TENDERO: ACEITE QUE NO\n"
      "SE CONGELA. LO DEMAS SI."), NULL },
    { E_PNJ,     6,  6, 1, 0, 0, 0,
      N_("ABUELA TUERCA: TE VOY A\n"
      "DECIR LO ULTIMO Y DESPUES\n"
      "TE DEJO EN PAZ.\n"
      "NO TE ENAMORES DE UNA\n"
      "PIEZA. LA MEJOR CABEZA\n"
      "DEL MUNDO NO SIRVE SI EL\n"
      "TORSO NO LE DA ENERGIA.\n"
      "MIRA EL CONJUNTO."), NULL },
};

static const char *const M_CUEVA1[ROWS] = {
    "##########:::##########",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnn##",
    "#nnhhhhhnnnnnnnhhhhhnn#",
    "#nnhhhhhnnnnnnnhhhhhnn#",
    "#nnhhhhhnnnnnnnhhhhhnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnn#########nnnnnn#",
    "#nnnnnn#hhhhhhh#nnnnnn#",
    "#nnnnnn#hhhhhhh#nnnnnn#",
    "#nnnnnn####:####nnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnhhhhhhhnnnhhhhhhhnn#",
    "#nnhhhhhhhnnnhhhhhhhnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "##########:::##########",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CUEVA1[] = {
    { 1,  1, PR_PINO },
    { 19,11, PR_PINO },
};

static const ch_ent_t EN_CUEVA1[] = {
    { E_PUERTA, 10, 19, S_CRIO,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_CRIO,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_CUEVA2, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_CUEVA2, 11, 17, 3, NULL, NULL },
    { E_COFRE,  11,  8, IT_TERMO, 1, F_TERMO, 0, NULL, NULL },
    { E_ENEMIGO, 4, 12, 0, F_ENEMIGO_34, 31, 0, NULL, NULL },
    { E_ENEMIGO,18, 12, 0, F_ENEMIGO_35, 31, 0, NULL, NULL },
};

static const char *const M_CUEVA2[ROWS] = {
    "##########:::##########",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnnnnnnnnnn#",
    "#nhhhhhhhhhhhhhhhhhhhn#",
    "#nhhhhhhhhhhhhhhhhhhhn#",
    "#nhhh#############hhhn#",
    "#nhhh#nnnnnnnnnnn#hhhn#",
    "#nhhh#nnnnnnnnnnn#hhhn#",
    "#nhhh#nnn#####nnn#hhhn#",
    "#nhhh#nnn#hhh#nnn#hhhn#",
    "#nhhh#nnn#hhh#nnn#hhhn#",
    "#nhhh#nnn##:##nnn#hhhn#",
    "#nhhh#nnnnnnnnnnn#hhhn#",
    "#nhhh####:####nnn#hhhn#",
    "#nhhhhhhhhhhhhhhhhhhhn#",
    "#nhhhhhhhhhhhhhhhhhhhn#",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "#nnnnnnnn#:::#nnnnnnnn#",
    "##########:::##########",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CUEVA2[] = {
    { 1, 19, PR_PINO },
};

static const ch_ent_t EN_CUEVA2[] = {
    { E_PUERTA, 10, 19, S_CUEVA1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_CUEVA1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE5,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE5,  11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 10, IT_BATERIA2, 2, F_COFRE_C2, 0, NULL, NULL },
    { E_COFRE,   3, 15, IT_CHIP, 1, F_COFRE_C3, 0, NULL, NULL },
    { E_ENEMIGO, 8, 13, 0, F_ENEMIGO_36, 32, 0, NULL, NULL },
    { E_ENEMIGO,14,  7, 0, F_ENEMIGO_37, 33, 0, NULL, NULL },
};

static const char *const M_JEFE5[ROWS] = {
    "0000###############0000",
    "0000#hhhhhhhhhhhhh#0000",
    "0000#hhhhhhhhhhhhh#0000",
    "000##hhhhhhhhhhhhh##000",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000#hhhhhhhhhhhhhhhh#00",
    "000##hhhhhhhhhhhhh##000",
    "0000#hhhhhhhhhhhhh#0000",
    "0000#hhhhhhhhhhhhh#0000",
    "0000#hhhhh:::hhhhh#0000",
    "0000#hhhhh:::hhhhh#0000",
    "0000######:::######0000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE5[] = {
    { 5,  2, PR_PINO },
    { 15, 2, PR_PINO },
};

static const ch_ent_t EN_JEFE5[] = {
    { E_PUERTA, 10, 19, S_CUEVA2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  7, 13, F_JEFE_CRIO, 36, IT_PASE,
      N_("GUARDABOSQUE:\n"
      "TE VI SUBIR DESDE EL\n"
      "PASO. TARDASTE MENOS DE\n"
      "LO QUE PENSE.\n"
      "VEAMOS SI TU ROBOT\n"
      "AGUANTA EL FRIO."), NULL },
};

/* ==========================================================================
 * ZONE 6 - CIUDAD MALLA
 *
 * The city of data. Dominant type: PLASMA. The sub-boss is the ADMINISTRADORA
 * (Motherboard torso) deep inside the server.
 * ========================================================================== */

static const char *const M_AUTOPISTA[ROWS] = {
    "#######################",
    "#ppppppppppppppppppppp#",
    "#pdddppppppppppppdddpp#",
    "#pdddppppppppppppdddpp#",
    "#ppppppppppppppppppppp#",
    "#PPPPPPPPPPPPPPPPPPPPP#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#pppppdddddddddddppppp#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#pppppdddddddddddppppp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#PPPPPPPPPPPPPPPPPPPPP#",
    "#ppppppppppppppppppppp#",
    "#pdddppppppppppppdddpp#",
    "#pdddppppppppppppdddpp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#######################",
};

static const ch_prop_t P_AUTOPISTA[] = {
    { 8,  1, PR_TORRE },
    { 14,17, PR_TORRE },
    { 3, 12, PR_SERVIDOR },
    { 18, 6, PR_SERVIDOR },
};

static const ch_ent_t EN_AUTOPISTA[] = {
    { E_PUERTA,  0,  9, S_CRIO,   19,  9, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_CRIO,   19,  9, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_CRIO,   19, 10, 2, NULL, NULL },
    { E_PUERTA, 21,  9, S_MALLA,   2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_MALLA,   2, 11, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_MALLA,   2, 11, 2, NULL, NULL },
    { E_CARTEL, 11,  7, 0, 0, 0, 0,
      N_("AUTOPISTA A MALLA.\n"
      "VELOCIDAD MAXIMA: LA QUE\n"
      "TE DEN LAS PIERNAS."), NULL },
    { E_COFRE,   3,  2, IT_ACEITE2, 2, F_COFRE_M1, 0, NULL, NULL },
    { E_ENEMIGO, 8, 12, 0, F_ENEMIGO_38, 34, 0, NULL, NULL },
    { E_ENEMIGO,15,  8, 0, F_ENEMIGO_39, 35, 0, NULL, NULL },
    { E_ENEMIGO,18, 18, 0, F_ENEMIGO_40, 35, 0, NULL, NULL },
};

static const char *const M_MALLA[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#pppppppppcccppppppppp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pppppppppcccppppppppp#",
    "#ccccccccccccccccccccc#",
    "#ccccccccccccccccccccc#",
    "#pppppppppcccpppppp####",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#pppppppppcccpppppp####",
    "#ccccccccccccccccccccc#",
    "#ccccccccccccccccccccc#",
    "#pppppppppcccppppppppp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pPPPPPppcccccppPPPPPp#",
    "#pppppppppcccppppppppp#",
    "#ppppppppppppppppppppp#",
    "#######################",
};

static const ch_prop_t P_MALLA[] = {
    { 3, 16, PR_SERVIDOR },
    { 17,16, PR_SERVIDOR },
    { 8,  1, PR_FAROLA },
    { 14, 1, PR_FAROLA },
    { 10,20, PR_ESTATUA },
};

static const ch_ent_t EN_MALLA[] = {
    { E_PUERTA,  0, 10, S_AUTOPISTA, 19, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_AUTOPISTA, 19, 10, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_SERV1,     11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_SERV1,     11, 17, 3, NULL, NULL },
    { E_PUERTA,  4,  6, S_MALLA_INT, 11, 16, 2, NULL, NULL },
    { E_BLOQUEO,19, 10, F_JEFE_MALLA, 0, 0, 0,
      N_("CONTROL DE MALLA.\n"
      "AL ESTE EMPIEZA EL\n"
      "PARAMO. NO SE PASA SIN\n"
      "PASE DE SECTOR."),
      N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL PARAMO.") },
    { E_BLOQUEO,19, 11, F_JEFE_MALLA, 0, 0, 0,
      N_("CONTROL DE MALLA.\n"
      "CERRADO."),
      N_("CONTROL ABIERTO.") },
    { E_PUERTA, 21, 10, S_LLANURA,  2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_LLANURA,  2, 11, 2, NULL, NULL },
    { E_CARTEL, 13,  8, 0, 0, 0, 0,
      N_("CIUDAD MALLA\n"
      "AQUI NO SE FABRICA NADA.\n"
      "SOLO SE PIENSA."), NULL },
    { E_PNJ,     7,  8, 2, 0, 0, 0,
      N_("CHICA: TODO EL PUEBLO\n"
      "CORRE SOBRE EL SERVIDOR\n"
      "DE ABAJO.\n"
      "SI SE APAGA, SE APAGA\n"
      "TODO."), NULL },
    { E_PNJ,    16, 13, 3, 0, 0, 0,
      N_("PROGRAMADOR: LOS BICHOS\n"
      "DE ACA PEGAN CON PLASMA.\n"
      "EL PLASMA NO PUEDE CON\n"
      "EL CRIO. PENSALO."), NULL },
    { E_PNJ,     6, 14, 4, F_MISION_CLAVE, F_CLAVE, IT_CHIP,
      N_("ARCHIVISTA: PERDI LA\n"
      "CLAVE MAESTRA EN EL\n"
      "SERVIDOR.\n"
      "SIN ELLA NO PUEDO ENTRAR\n"
      "A MI PROPIA OFICINA."),
      N_("ARCHIVISTA: LA CLAVE!\n"
      "TOMA UN CHIP, TE LO\n"
      "GANASTE.") },
};

static const char *const M_MALLA_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|--------+++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++ooooooooo+++|000",
    "000|+++ooooooooo+++|000",
    "000|+++ooooooooo+++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++++++___++++++|000",
    "000|||||||___|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_MALLA_INT[] = {
    { 15,  3, PR_SERVIDOR },
    { 5,  11, PR_MAQUINA },
};

static const ch_ent_t EN_MALLA_INT[] = {
    { E_PUERTA, 10, 17, S_MALLA,  4,  7, 3, NULL, NULL },
    { E_TALLER,  5, 12, 0, 0, 0, 0,
      N_("EL BANCO DE MALLA.\n"
      "Te lo diagnostica solo."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("TENDERA: TODO CARO, TODO\n"
      "BUENO. Es una ciudad."), NULL },
    { E_PNJ,    16,  8, 3, 0, 0, 0,
      N_("TECNICO: DEJAME VER TU\n"
      "ROBOT... MIRA, LE SOBRA\n"
      "ATAQUE Y LE FALTA VIDA.\n"
      "UN TORSO GRANDE TE\n"
      "SALVARIA MAS DE UN\n"
      "COMBATE."), NULL },
};

static const char *const M_SERV1[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CccCCCCCCcccccCCCCCCCcC",
    "CccCcccccccccccccccccCC",
    "CccCcccCCCCCCCCCCCcccCC",
    "CccCcccCcccccccccCcccCC",
    "CccCcccCcccccccccCcccCC",
    "CccCcccCccCCCCCccCcccCC",
    "CccCcccCccCcccCccCcccCC",
    "CccCcccCccCcccCccCcccCC",
    "CccCcccCccCC:CCccCcccCC",
    "CccCcccCcccccccccCcccCC",
    "CccCcccCCCCC:CCCCCcccCC",
    "CccCcccccccccccccccccCC",
    "CccCCCCCCCCC:CCCCCCCCCC",
    "CcccccccccccccccccccccC",
    "CccccccccC:::CccccccccC",
    "CccccccccC:::CccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_SERV1[] = {
    { 1,  2, PR_SERVIDOR },
    { 19,16, PR_SERVIDOR },
};

static const ch_ent_t EN_SERV1[] = {
    { E_PUERTA, 10, 19, S_MALLA,  11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_MALLA,  11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_SERV2,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_SERV2,  11, 17, 3, NULL, NULL },
    { E_COFRE,  12,  9, IT_CLAVE, 1, F_CLAVE, 0, NULL, NULL },
    { E_ENEMIGO, 5, 12, 0, F_ENEMIGO_41, 37, 0, NULL, NULL },
    { E_ENEMIGO,19,  8, 0, F_ENEMIGO_42, 37, 0, NULL, NULL },
};

static const char *const M_SERV2[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CcCCCCCCCCCCCCCCCCCCCcC",
    "CcCcccccccccccccccccCcC",
    "CcCcCCCCCCCCCCCCCCCcCcC",
    "CcCcCcccccccccccccCcCcC",
    "CcCcCcCCCCCCCCCCCcCcCcC",
    "CcCcCcCcccccccccCcCcCcC",
    "CcCcCcCcCCCCCCcCcCcCcCC",
    "CcCcCcCcCcccccCcCcCcCcC",
    "CcCcCcCcCcCCCcCcCcCcCcC",
    "CcCcCcCcCcccccCcCcCcCcC",
    "CcCcCcCcCCCCCCcCcCcCcCC",
    "CcCcCcCcccccccccCcCcCcC",
    "CcCcCcCCCCCCCCCCCcCcCcC",
    "CcCcCccccccccccccccCcCC",
    "CcCcCCCCCC:::CCCCCCCcCC",
    "CcccccccccC:CCccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_SERV2[] = {
    { 1,  2, PR_SERVIDOR },
};

static const ch_ent_t EN_SERV2[] = {
    { E_PUERTA, 10, 19, S_SERV1,  11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE6,  11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_JEFE6,  11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 11, IT_SOLDADOR, 2, F_COFRE_M2, 0, NULL, NULL },
    { E_COFRE,   3, 16, IT_BATERIA2, 2, F_COFRE_M3, 0, NULL, NULL },
    { E_ENEMIGO, 9, 14, 0, F_ENEMIGO_43, 38, 0, NULL, NULL },
    { E_ENEMIGO,17,  6, 0, F_ENEMIGO_44, 39, 0, NULL, NULL },
};

static const char *const M_JEFE6[ROWS] = {
    "0000CCCCCCCCCCCCCCC0000",
    "0000CcccccccccccccC0000",
    "0000CcccccccccccccC0000",
    "000CCcccccccccccccCC000",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CccccccccccccccccC00",
    "000CCcccccccccccccCC000",
    "0000CcccccccccccccC0000",
    "0000CcccccccccccccC0000",
    "0000Cccccc:::ccccccC000",
    "0000Cccccc:::ccccccC000",
    "0000CCCCCC:::CCCCCCC000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE6[] = {
    { 5,  2, PR_SERVIDOR },
    { 15, 2, PR_SERVIDOR },
};

static const ch_ent_t EN_JEFE6[] = {
    { E_PUERTA, 10, 19, S_SERV2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  7, 14, F_JEFE_MALLA, 42, IT_PASE,
      N_("ADMINISTRADORA:\n"
      "TE ESTUVE MIRANDO DESDE\n"
      "QUE ENTRASTE AL PUEBLO.\n"
      "SE QUE PIEZAS LLEVAS Y\n"
      "SE COMO PELEAS.\n"
      "IGUAL VENIS, CLARO."), NULL },
};

/* ==========================================================================
 * ZONE 7 - VILLA OXIDO
 *
 * The wasteland and the robot graveyard. Dominant type: IMPACT. The sub-boss
 * is the CHATARRERO MAYOR (Vault torso), the toughest robot in the game before
 * the tower.
 * ========================================================================== */

static const char *const M_LLANURA[ROWS] = {
    "#######################",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQdddQQQQQQQQQdddQQQQ#",
    "#QQdddQQQQQQQQQdddQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQRRRRRRRQQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQdddddddQQQdddddddQQ#",
    "#QQdddddddQQQdddddddQQ#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQRRRRRRRRRRRQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQdddddQQQQQQQdddddQQ#",
    "#QQdddddQQQQQQQdddddQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQdddQQQQQQQQQdddQQQQ#",
    "#QQdddQQQQQQQQQdddQQQQ#",
    "#######################",
};

static const ch_prop_t P_LLANURA[] = {
    { 3, 12, PR_PILA },
    { 18, 4, PR_PILA },
    { 9, 18, PR_PILA },
    { 16,12, PR_TORRE },
};

static const ch_ent_t EN_LLANURA[] = {
    { E_PUERTA,  0,  9, S_MALLA,  18, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_MALLA,  18, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_MALLA,  18, 11, 2, NULL, NULL },
    { E_PUERTA, 21,  9, S_OXIDO,   2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_OXIDO,   2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_OXIDO,   2, 10, 2, NULL, NULL },
    { E_CARTEL, 11,  6, 0, 0, 0, 0,
      N_("EL PARAMO.\n"
      "ACA TERMINA TODO LO QUE\n"
      "NO SIRVE.\n"
      "USTED TAMBIEN, SI SE\n"
      "DESCUIDA."), NULL },
    { E_COFRE,   4, 19, IT_SOLDADOR, 1, F_COFRE_O1, 0, NULL, NULL },
    { E_ENEMIGO, 6,  7, 0, F_ENEMIGO_45, 40, 0, NULL, NULL },
    { E_ENEMIGO,17, 15, 0, F_ENEMIGO_46, 41, 0, NULL, NULL },
    { E_ENEMIGO, 5, 15, 0, F_ENEMIGO_47, 41, 0, NULL, NULL },
};

static const char *const M_OXIDO[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "###QQQQQQQGGGQQQQQQ####",
    "GGGQQQQQQQGGGQQQQQQGGGG",
    "GGGQQQQQQQGGGQQQQQQGGGG",
    "###QQQQQQQGGGQQQQQQ####",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#GGGGGGGGGGGGGGGGGGGGG#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQGGGQQQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#######################",
};

static const ch_prop_t P_OXIDO[] = {
    { 3,  2, PR_CASA },
    { 15, 2, PR_TALLER },
    { 3, 15, PR_PILA },
    { 16,15, PR_PILA },
    { 6, 18, PR_PILA },
    { 8,  4, PR_FAROLA },
};

static const ch_ent_t EN_OXIDO[] = {
    { E_PUERTA,  0,  9, S_LLANURA, 19, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_LLANURA, 19, 10, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_CEMENT1, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_CEMENT1, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 16,  6, S_OXIDO_INT, 11, 16, 2, NULL, NULL },
    { E_BLOQUEO,18,  9, F_JEFE_PARAMO, 0, 0, 0,
      N_("CONTROL DE VILLA OXIDO.\n"
      "AL ESTE SOLO QUEDA LA\n"
      "TORRE PRISMA.\n"
      "NO SE PASA SIN PASE."),
      N_("CONTROL ABIERTO.\n"
      "AL ESTE: LA TORRE.") },
    { E_BLOQUEO,18, 10, F_JEFE_PARAMO, 0, 0, 0,
      N_("CONTROL DE VILLA OXIDO.\n"
      "CERRADO."),
      N_("CONTROL ABIERTO.") },
    { E_PUERTA, 21,  9, S_ULTIMO,  2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_ULTIMO,  2, 11, 2, NULL, NULL },
    { E_CARTEL, 13,  7, 0, 0, 0, 0,
      N_("VILLA OXIDO\n"
      "EL PUEBLO MAS VIEJO Y EL\n"
      "MAS CERCANO A LA TORRE.\n"
      "NADIE SABE CUAL DE LAS\n"
      "DOS COSAS ES PEOR."), NULL },
    { E_PNJ,     6,  7, 3, 0, 0, 0,
      N_("VIEJO: YO SUBI A LA TORRE\n"
      "UNA VEZ. HACE MUCHO.\n"
      "NO TE VOY A CONTAR COMO\n"
      "ME FUE."), NULL },
    { E_PNJ,    17, 13, 2, 0, 0, 0,
      N_("CHICO: EN EL CEMENTERIO\n"
      "LOS ROBOTS SE ARMAN SOLOS\n"
      "CON LO QUE ENCUENTRAN.\n"
      "POR ESO SON TAN RAROS."), NULL },
    { E_PNJ,     5, 13, 4, F_MISION_ENGRANAJE, F_ENGRANAJE, IT_BATERIA2,
      N_("CHATARRERA: BUSCO UN\n"
      "ENGRANAJE GRANDE, DE LOS\n"
      "VIEJOS. EN EL CEMENTERIO\n"
      "TIENE QUE HABER.\n"
      "TE PAGO BIEN."),
      N_("CHATARRERA: MIRA ESO.\n"
      "HACIA VEINTE ANOS QUE NO\n"
      "VEIA UNO ENTERO.\n"
      "TOMA, TE LO GANASTE.") },
};

static const char *const M_OXIDO_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|_______________|000",
    "000|--------_______|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|__ooooooooooo__|000",
    "000|__ooooooooooo__|000",
    "000|__ooooooooooo__|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|_______________|000",
    "000|______+++______|000",
    "000|||||||+++|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_OXIDO_INT[] = {
    { 15,  3, PR_MAQUINA },
    { 5,  11, PR_MAQUINA },
};

static const ch_ent_t EN_OXIDO_INT[] = {
    { E_PUERTA, 10, 17, S_OXIDO, 16,  7, 3, NULL, NULL },
    { E_TALLER,  5, 12, 0, 0, 0, 0,
      N_("EL BANCO DE VILLA OXIDO.\n"
      "Viejo pero impecable."), NULL },
    { E_TIENDA,  5,  3, 0, 0, 0, 0,
      N_("CHATARRERO: LO QUE VES\n"
      "es lo que hay. Alcanza."), NULL },
    { E_PNJ,    16,  8, 1, 0, 0, 0,
      N_("ABUELA TUERCA: LLEGASTE\n"
      "LEJOS, EH.\n"
      "ARRIBA DE LA TORRE ESTA\n"
      "EL CAMPEON. NO TE VOY A\n"
      "MENTIR: SU ROBOT ES MEJOR\n"
      "QUE EL TUYO.\n"
      "PERO EL NO ARMO EL SUYO.\n"
      "SE LO ARMARON."), NULL },
};

static const char *const M_CEMENT1[ROWS] = {
    "##########:::##########",
    "#QQQQQQQQ#:::#QQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQRRQQQQQQQQQQQQQRRQQ#",
    "#QQRRQQQdddddddQQQRRQQ#",
    "#QQQQQQQdddddddQQQQQQQ#",
    "#QQQQQQQdddddddQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQRRRQQQQQQQQQQQRRRQQ#",
    "#QQRRRQQQQQQQQQQQRRRQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQdddddddddQQQQQQ#",
    "#QQQQQQdddddddddQQQQQQ#",
    "#QQQQQQdddddddddQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQRRQQQQQQQQQQQQQRRQQ#",
    "#QQRRQQQQQQQQQQQQQRRQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQQQ#:::#QQQQQQQQ#",
    "##########:::##########",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CEMENT1[] = {
    { 2,  1, PR_PILA },
    { 19, 1, PR_PILA },
    { 7, 16, PR_PILA },
};

static const ch_ent_t EN_CEMENT1[] = {
    { E_PUERTA, 10, 19, S_OXIDO,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_OXIDO,   11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_CEMENT2, 11, 17, 3, NULL, NULL },
    { E_COFRE,  11,  5, IT_ENGRANAJE, 1, F_ENGRANAJE, 0, NULL, NULL },
    { E_ENEMIGO, 5, 12, 0, F_ENEMIGO_48, 43, 0, NULL, NULL },
    { E_ENEMIGO,17, 12, 0, F_ENEMIGO_49, 43, 0, NULL, NULL },
};

static const char *const M_CEMENT2[ROWS] = {
    "##########:::##########",
    "#QQQQQQQQ#:::#QQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QRRRRRRRRRRRRRRRRRRRQ#",
    "#QRQQQQQQQQQQQQQQQQQRQ#",
    "#QRQRRRRRRRRRRRRRRRQRQ#",
    "#QRQRQQQQQQQQQQQQQRQRQ#",
    "#QRQRQRRRRRRRRRRRQRQRQ#",
    "#QRQRQRddddddddRQRQRQQ#",
    "#QRQRQRddddddddRQRQRQQ#",
    "#QRQRQRRRRRRRRRRQRQRQQ#",
    "#QRQRQQQQQQQQQQQQRQRQQ#",
    "#QRQRRRRRRRRRRRRRRQRQQ#",
    "#QRQQQQQQQQQQQQQQQQRQQ#",
    "#QRRRRRRRRRRRRRRRRRRQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQQQQQQQQQQQQQQQQ#",
    "#QQQQQQQQ#:::#QQQQQQQQ#",
    "#QQQQQQQQ#:::#QQQQQQQQ#",
    "##########:::##########",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CEMENT2[] = {
    { 1, 16, PR_PILA },
    { 19,16, PR_PILA },
};

static const ch_ent_t EN_CEMENT2[] = {
    { E_PUERTA, 10, 19, S_CEMENT1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_CEMENT1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_JEFE7,   11, 17, 3, NULL, NULL },
    { E_COFRE,  10,  8, IT_CHIP, 2, F_COFRE_O2, 0, NULL, NULL },
    { E_COFRE,  13,  8, IT_ACEITE2, 3, F_COFRE_O3, 0, NULL, NULL },
    { E_ENEMIGO, 3,  2, 0, F_ENEMIGO_50, 44, 0, NULL, NULL },
    { E_ENEMIGO,18,  2, 0, F_ENEMIGO_51, 45, 0, NULL, NULL },
};

static const char *const M_JEFE7[ROWS] = {
    "0000###############0000",
    "0000#QQQQQQQQQQQQQ#0000",
    "0000#QQQQQQQQQQQQQ#0000",
    "000##QQQQQQQQQQQQQ##000",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000#QQQQQQQQQQQQQQQQ#00",
    "000##QQQQQQQQQQQQQ##000",
    "0000#QQQQQQQQQQQQQ#0000",
    "0000#QQQQQQQQQQQQQ#0000",
    "0000#QQQQQ:::QQQQQ#0000",
    "0000#QQQQQ:::QQQQQ#0000",
    "0000######:::######0000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_JEFE7[] = {
    { 5,  2, PR_PILA },
    { 15, 2, PR_PILA },
};

static const ch_ent_t EN_JEFE7[] = {
    { E_PUERTA, 10, 19, S_CEMENT2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  7, 12, F_JEFE_PARAMO, 48, IT_PASE,
      N_("CHATARRERO MAYOR:\n"
      "TODO ESTO ERAN ROBOTS\n"
      "COMO EL TUYO.\n"
      "CADA UNO CREYO QUE ERA\n"
      "DISTINTO.\n"
      "A VER VOS."), NULL },
};

/* ==========================================================================
 * ZONE 8 - TORRE PRISMA
 *
 * The end. You climb the tower and at the top is the CAMPEON (Prism torso).
 * Beating him sets F_JEFE_PRISMA, and with that flag the summit's throne tells
 * the ending: the same two-texts-and-two-flags mechanism as the quests.
 * ========================================================================== */

static const char *const M_ULTIMO[ROWS] = {
    "#######################",
    "#ppppppppppppppppppppp#",
    "#pVVVpppppppppppVVVppp#",
    "#pVVVpppppppppppVVVppp#",
    "#ppppppppppppppppppppp#",
    "#pppppddddddddddppppppp",
    "#pppppddddddddddpppppp#",
    "#ppppppppppppppppppppp#",
    "#pVVVVVpppppppVVVVVppp#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#pVVVVVpppppppVVVVVppp#",
    "#ppppppppppppppppppppp#",
    "#pppppddddddddddpppppp#",
    "#pppppddddddddddpppppp#",
    "#ppppppppppppppppppppp#",
    "#pVVVpppppppppppVVVppp#",
    "#pVVVpppppppppppVVVppp#",
    "#ppppppppppppppppppppp#",
    "#ppppppppppppppppppppp#",
    "#######################",
};

static const ch_prop_t P_ULTIMO[] = {
    { 9,  1, PR_ESTATUA },
    { 12,17, PR_ESTATUA },
};

static const ch_ent_t EN_ULTIMO[] = {
    { E_PUERTA,  0,  9, S_OXIDO,  17, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 10, S_OXIDO,  17, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_OXIDO,  17, 10, 2, NULL, NULL },
    { E_PUERTA, 21,  9, S_PRISMA,  2, 10, 2, NULL, NULL },
    { E_PUERTA, 21, 10, S_PRISMA,  2, 11, 2, NULL, NULL },
    { E_PUERTA, 21, 11, S_PRISMA,  2, 11, 2, NULL, NULL },
    { E_CARTEL, 11,  7, 0, 0, 0, 0,
      N_("ULTIMO TRAMO.\n"
      "DE ACA NO SE VUELVE\n"
      "IGUAL QUE COMO SE VINO."), NULL },
    { E_COFRE,   3, 19, IT_BATERIA2, 3, F_COFRE_T1, 0, NULL, NULL },
    { E_ENEMIGO, 7,  5, 0, F_ENEMIGO_52, 46, 0, NULL, NULL },
    { E_ENEMIGO,16, 14, 0, F_ENEMIGO_53, 47, 0, NULL, NULL },
};

static const char *const M_PRISMA[ROWS] = {
    "##########:::##########",
    "##########:::##########",
    "#pppppppppcccppppppppp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pppppppppcccppppppppp#",
    "#ccccccccccccccccccccc#",
    "#ccccccccccccccccccccc#",
    "#pppppppppcccppppppppp#",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGGGGGGGGGG",
    "#pppppppppcccppppppppp#",
    "#ccccccccccccccccccccc#",
    "#ccccccccccccccccccccc#",
    "#pppppppppcccppppppppp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pVVVVVVppcccppVVVVVVp#",
    "#pppppppppcccppppppppp#",
    "#ppppppppppppppppppppp#",
    "#######################",
};

static const ch_prop_t P_PRISMA[] = {
    { 3, 20, PR_ESTATUA },
    { 18,20, PR_ESTATUA },
    { 8,  1, PR_FAROLA },
    { 14, 1, PR_FAROLA },
};

static const ch_ent_t EN_PRISMA[] = {
    { E_PUERTA,  0, 10, S_ULTIMO, 19, 10, 2, NULL, NULL },
    { E_PUERTA,  0, 11, S_ULTIMO, 19, 10, 2, NULL, NULL },
    { E_PUERTA, 10,  0, S_TORRE1, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_TORRE1, 11, 17, 3, NULL, NULL },
    { E_PUERTA,  4,  7, S_PRISMA_INT, 11, 16, 2, NULL, NULL },
    { E_CARTEL, 13, 11, 0, 0, 0, 0,
      N_("PRISMA\n"
      "AL NORTE: LA TORRE.\n"
      "SUBEN MUCHOS. BAJAN\n"
      "BASTANTES MENOS."), NULL },
    { E_PNJ,     7, 13, 2, 0, 0, 0,
      N_("CHICO: MI HERMANA SUBIO\n"
      "AYER. TODAVIA NO BAJO.\n"
      "DICE MAMA QUE NO ME\n"
      "PREOCUPE."), NULL },
    { E_PNJ,    16,  8, 3, 0, 0, 0,
      N_("GUARDIA: DOS PISOS Y LA\n"
      "CUMBRE.\n"
      "REPARA ANTES DE SUBIR.\n"
      "ARRIBA NO HAY BANCO."), NULL },
};

static const char *const M_PRISMA_INT[ROWS] = {
    "00000000000000000000000",
    "00000000000000000000000",
    "000|||||||||||||||||000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|-------++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++ooooooooooo++|000",
    "000|++ooooooooooo++|000",
    "000|++ooooooooooo++|000",
    "000|++ooooooooooo++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|+++++++++++++++|000",
    "000|++++++___++++++|000",
    "000|||||||___|||||||000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_PRISMA_INT[] = {
    { 15,  3, PR_SERVIDOR },
    { 5,  13, PR_MAQUINA },
};

static const ch_ent_t EN_PRISMA_INT[] = {
    { E_PUERTA, 10, 17, S_PRISMA, 4,  8, 3, NULL, NULL },
    { E_TALLER,  5, 14, 0, 0, 0, 0,
      N_("EL ULTIMO BANCO ANTES\n"
      "DE LA TORRE.\n"
      "Usalo bien."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("TENDERO: LLEVA TODO LO\n"
      "QUE PUEDAS PAGAR.\n"
      "Arriba no se compra nada."), NULL },
    { E_PNJ,    16,  9, 1, 0, 0, 0,
      N_("ABUELA TUERCA: BUENO.\n"
      "HASTA ACA TE ACOMPANO.\n"
      "ACORDATE DE UNA COSA\n"
      "SOLA: EL ROBOT QUE VAS A\n"
      "PELEAR ES EL QUE VOS\n"
      "ARMASTE.\n"
      "NINGUNA PIEZA TE LA\n"
      "REGALARON.\n"
      "ANDA."), NULL },
};

static const char *const M_TORRE1[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CccVVVVVcccccVVVVVVVccC",
    "CccVcccVcccccVcccccVccC",
    "CccVcccVcccccVcccccVccC",
    "CccVVV:VcccccV:VVVVVccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccccccVVVVVVVVVccccccC",
    "CccccccVcccccccVccccccC",
    "CccccccVcccccccVccccccC",
    "CccccccVVVV:VVVVccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccccccccC:::CccccccccC",
    "CccccccccC:::CccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_TORRE1[] = {
    { 1,  2, PR_SERVIDOR },
};

static const ch_ent_t EN_TORRE1[] = {
    { E_PUERTA, 10, 19, S_PRISMA, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_PRISMA, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_TORRE2, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_TORRE2, 11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 11, IT_SOLDADOR, 2, F_COFRE_T2, 0, NULL, NULL },
    { E_ENEMIGO, 5, 15, 0, F_ENEMIGO_54, 49, 0, NULL, NULL },
    { E_ENEMIGO,18,  8, 0, F_ENEMIGO_55, 50, 0, NULL, NULL },
};

static const char *const M_TORRE2[ROWS] = {
    "CCCCCCCCCC:::CCCCCCCCCC",
    "CccccccccC:::CccccccccC",
    "CcccccccccccccccccccccC",
    "CcccccccccccccccccccccC",
    "CccVVVVVVVVVVVVVVVVVccC",
    "CccVcccccccccccccccVccC",
    "CccVcccccccccccccccVccC",
    "CccVccVVVVVVVVVVVccVccC",
    "CccVccVcccccccccVccVccC",
    "CccVccVcccccccccVccVccC",
    "CccVccVccVVVVVccVccVccC",
    "CccVccVccV:::VccVccVccC",
    "CccVccVccVVVVVccVccVccC",
    "CccVccVcccccccccVccVccC",
    "CccVccVVVVVV:VVVVccVccC",
    "CccVcccccccccccccccVccC",
    "CccVVVVVVV:VVVVVVVVVccC",
    "CccccccccC:::CccccccccC",
    "CccccccccC:::CccccccccC",
    "CCCCCCCCCC:::CCCCCCCCCC",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_TORRE2[] = {
    { 20, 2, PR_SERVIDOR },
};

static const ch_ent_t EN_TORRE2[] = {
    { E_PUERTA, 10, 19, S_TORRE1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10, 18, S_TORRE1, 11,  2, 3, NULL, NULL },
    { E_PUERTA, 10,  0, S_CUMBRE, 11, 17, 3, NULL, NULL },
    { E_PUERTA, 10,  1, S_CUMBRE, 11, 17, 3, NULL, NULL },
    { E_COFRE,  11, 11, IT_BATERIA2, 3, F_COFRE_T3, 0, NULL, NULL },
    { E_ENEMIGO,11,  3, 0, F_ENEMIGO_56, 51, 0, NULL, NULL },
    { E_ENEMIGO, 3, 16, 0, F_ENEMIGO_57, 52, 0, NULL, NULL },
};

static const char *const M_CUMBRE[ROWS] = {
    "0000VVVVVVVVVVVVVVV0000",
    "0000VcccccccccccccV0000",
    "0000VcccccccccccccV0000",
    "000VVcccccccccccccVV000",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VccccccccccccccccV00",
    "000VVcccccccccccccVV000",
    "0000VcccccccccccccV0000",
    "0000VcccccccccccccV0000",
    "0000Vccccc:::cccccV0000",
    "0000Vccccc:::cccccV0000",
    "0000VVVVVV:::VVVVVV0000",
    "00000000000000000000000",
    "00000000000000000000000",
};

static const ch_prop_t P_CUMBRE[] = {
    { 5,  2, PR_ESTATUA },
    { 15, 2, PR_ESTATUA },
};

static const ch_ent_t EN_CUMBRE[] = {
    { E_PUERTA, 10, 19, S_TORRE2, 11,  2, 3, NULL, NULL },
    { E_JEFE,   11,  8, 16, F_JEFE_PRISMA, 55, IT_PASE,
      N_("CAMPEON:\n"
      "TE VI SUBIR PISO POR\n"
      "PISO. NO SALTEASTE\n"
      "NINGUNO.\n"
      "ESO YA TE HACE DISTINTO\n"
      "A CASI TODOS.\n"
      "AHORA VEAMOS EL ROBOT."), NULL },
    /* The guardian tells the ending. Before you win she says something else:
     * it is the same two-texts-and-two-flags mechanism as the town's quests,
     * without a line of code of its own. */
    { E_PNJ,     8,  4, 3, F_FINAL, F_JEFE_PRISMA, 0,
      N_("GUARDIANA DE LA CUMBRE:\n"
      "ESE SILLON DE METAL ES\n"
      "DEL CAMPEON.\n"
      "TODAVIA NO ES TUYO."),
      N_("GUARDIANA: SENTATE, DALE.\n"
      "TE LO GANASTE.\n"
      "DESDE ACA SE VE TODO:\n"
      "EL PARAMO, LA CIUDAD,\n"
      "EL VALLE, EL PUERTO.\n"
      "Y ALLA LEJOS, CHIQUITO,\n"
      "VILLA TUERCA.\n"
      "TU ROBOT NO SE PARECE EN\n"
      "NADA AL QUE TE ARMO LA\n"
      "ABUELA.\n"
      "NI UNA PIEZA.\n"
      "FIN.") },
};

/* --------------------------------------------------------------------------
 * THE ROOM TABLE
 * -------------------------------------------------------------------------- */

#define N(a)    ((uint8_t)(sizeof(a) / sizeof((a)[0])))

const ch_room_t ch_salas[] = {
    { N_("TU CASA"),     TEMA_INTERIOR, M_CASA,      NULL,        0,
      EN_CASA,      N(EN_CASA),      1,   0 , AMB_NADA },
    { N_("VILLA TUERCA"), TEMA_PUEBLO,  M_PUEBLO,    P_PUEBLO,    N(P_PUEBLO),
      EN_PUEBLO,    N(EN_PUEBLO),    1,   0 , AMB_NADA },
    { N_("TALLER"),      TEMA_INTERIOR, M_TALLER,    P_TALLER_INT, N(P_TALLER_INT),
      EN_TALLER,    N(EN_TALLER),    1,   0 , AMB_NADA },
    { N_("CASA VECINA"), TEMA_INTERIOR, M_VECINO,    NULL,        0,
      EN_VECINO,    N(EN_VECINO),    1,   0 , AMB_NADA },
    { N_("SENDERO NORTE"), TEMA_PUEBLO, M_SENDERO,   P_SENDERO,   N(P_SENDERO),
      EN_SENDERO,   N(EN_SENDERO),   1,  22 , AMB_NADA },
    { N_("DESGUACE"),    TEMA_DUNGEON,  M_DESGUACE1, P_DESGUACE1, N(P_DESGUACE1),
      EN_DESGUACE1, N(EN_DESGUACE1), 2,  26 , AMB_POLVO },
    { N_("DESGUACE -2"), TEMA_DUNGEON,  M_DESGUACE2, P_DESGUACE2, N(P_DESGUACE2),
      EN_DESGUACE2, N(EN_DESGUACE2), 2,  30 , AMB_POLVO },
    { N_("DEPOSITO"),    TEMA_DUNGEON,  M_DESGUACE3, P_DESGUACE3, N(P_DESGUACE3),
      EN_DESGUACE3, N(EN_DESGUACE3), 2,  30 , AMB_POLVO },
    { N_("SALA DEL GUARDIAN"), TEMA_DUNGEON, M_JEFE,  P_JEFE,     N(P_JEFE),
      EN_JEFE,      N(EN_JEFE),      2,   0 , AMB_POLVO },

    /* zone 2 */
    { N_("COSTA DEL CANGREJO"), TEMA_PUEBLO, M_COSTA, P_COSTA, N(P_COSTA),
      EN_COSTA,     N(EN_COSTA),     2,  24 , AMB_NADA },
    { N_("PUERTO BUJIA"), TEMA_PUEBLO,  M_PUERTO,  P_PUERTO,  N(P_PUERTO),
      EN_PUERTO,    N(EN_PUERTO),    2,   0 , AMB_NADA },
    { N_("TALLER DEL PUERTO"), TEMA_INTERIOR, M_PUERTO_INT, P_PUERTO_INT,
      N(P_PUERTO_INT), EN_PUERTO_INT, N(EN_PUERTO_INT), 2, 0 , AMB_NADA },
    { N_("LA BODEGA"), TEMA_DUNGEON, M_BODEGA1, P_BODEGA1, N(P_BODEGA1),
      EN_BODEGA1,   N(EN_BODEGA1),   3,  28 , AMB_GOTERAS },
    { N_("BODEGA -2"), TEMA_DUNGEON, M_BODEGA2, P_BODEGA2, N(P_BODEGA2),
      EN_BODEGA2,   N(EN_BODEGA2),   3,  30 , AMB_GOTERAS },
    { N_("MUELLE HUNDIDO"), TEMA_DUNGEON, M_JEFE2, P_JEFE2, N(P_JEFE2),
      EN_JEFE2,     N(EN_JEFE2),     3,   0 , AMB_GOTERAS },

    /* zone 3 */
    { N_("LA CUESTA"), TEMA_PUEBLO, M_CUESTA, P_CUESTA, N(P_CUESTA),
      EN_CUESTA,    N(EN_CUESTA),    3,  26 , AMB_NADA },
    { N_("ALTO VOLTIO"), TEMA_PUEBLO, M_VOLTIO, P_VOLTIO, N(P_VOLTIO),
      EN_VOLTIO,    N(EN_VOLTIO),    3,   0 , AMB_NADA },
    { N_("TALLER DE ALTO VOLTIO"), TEMA_INTERIOR, M_VOLTIO_INT, P_VOLTIO_INT,
      N(P_VOLTIO_INT), EN_VOLTIO_INT, N(EN_VOLTIO_INT), 3, 0 , AMB_NADA },
    { N_("LA SUBESTACION"), TEMA_DUNGEON, M_SUB1, P_SUB1, N(P_SUB1),
      EN_SUB1,      N(EN_SUB1),      4,  28 , AMB_POLVO },
    { N_("SUBESTACION -2"), TEMA_DUNGEON, M_SUB2, P_SUB2, N(P_SUB2),
      EN_SUB2,      N(EN_SUB2),      4,  30 , AMB_POLVO },
    { N_("SALA DE BARRAS"), TEMA_DUNGEON, M_JEFE3, P_JEFE3, N(P_JEFE3),
      EN_JEFE3,     N(EN_JEFE3),     4,   0 , AMB_POLVO },

    /* zone 4 */
    { N_("VALLE DEL HUMO"), TEMA_CUEVA, M_HUMO, P_HUMO, N(P_HUMO),
      EN_HUMO,      N(EN_HUMO),      4,  28 , AMB_BRASAS },
    { N_("FUNDICION"), TEMA_CUEVA, M_FUNDICION, P_FUNDICION, N(P_FUNDICION),
      EN_FUNDICION, N(EN_FUNDICION), 4,   0 , AMB_BRASAS },
    { N_("TALLER DE FUNDICION"), TEMA_INTERIOR, M_FUND_INT, P_FUND_INT,
      N(P_FUND_INT), EN_FUND_INT, N(EN_FUND_INT), 4, 0 , AMB_NADA },
    { N_("EL HORNO"), TEMA_DUNGEON, M_HORNO1, P_HORNO1, N(P_HORNO1),
      EN_HORNO1,    N(EN_HORNO1),    5,  30 , AMB_BRASAS },
    { N_("HORNO -2"), TEMA_DUNGEON, M_HORNO2, P_HORNO2, N(P_HORNO2),
      EN_HORNO2,    N(EN_HORNO2),    5,  30 , AMB_BRASAS },
    { N_("BOCA DE COLADA"), TEMA_DUNGEON, M_JEFE4, P_JEFE4, N(P_JEFE4),
      EN_JEFE4,     N(EN_JEFE4),     5,   0 , AMB_BRASAS },

    /* zone 5 */
    { N_("PASO HELADO"), TEMA_PUEBLO, M_PASO, P_PASO, N(P_PASO),
      EN_PASO,      N(EN_PASO),      5,  28 , AMB_NIEVE },
    { N_("CRIOVALLE"), TEMA_PUEBLO, M_CRIO, P_CRIO, N(P_CRIO),
      EN_CRIO,      N(EN_CRIO),      5,   0 , AMB_NIEVE },
    { N_("TALLER DE CRIOVALLE"), TEMA_INTERIOR, M_CRIO_INT, P_CRIO_INT,
      N(P_CRIO_INT), EN_CRIO_INT, N(EN_CRIO_INT), 5, 0 , AMB_NADA },
    { N_("CUEVA DE HIELO"), TEMA_CUEVA, M_CUEVA1, P_CUEVA1, N(P_CUEVA1),
      EN_CUEVA1,    N(EN_CUEVA1),    6,  30 , AMB_NIEVE },
    { N_("CUEVA -2"), TEMA_CUEVA, M_CUEVA2, P_CUEVA2, N(P_CUEVA2),
      EN_CUEVA2,    N(EN_CUEVA2),    6,  30 , AMB_NIEVE },
    { N_("EL NUCLEO HELADO"), TEMA_CUEVA, M_JEFE5, P_JEFE5, N(P_JEFE5),
      EN_JEFE5,     N(EN_JEFE5),     6,   0 , AMB_NIEVE },

    /* zone 6 */
    { N_("AUTOPISTA"), TEMA_PUEBLO, M_AUTOPISTA, P_AUTOPISTA, N(P_AUTOPISTA),
      EN_AUTOPISTA, N(EN_AUTOPISTA), 6,  28 , AMB_NADA },
    { N_("CIUDAD MALLA"), TEMA_PUEBLO, M_MALLA, P_MALLA, N(P_MALLA),
      EN_MALLA,     N(EN_MALLA),     6,   0 , AMB_NADA },
    { N_("TALLER DE MALLA"), TEMA_INTERIOR, M_MALLA_INT, P_MALLA_INT,
      N(P_MALLA_INT), EN_MALLA_INT, N(EN_MALLA_INT), 6, 0 , AMB_NADA },
    { N_("EL SERVIDOR"), TEMA_DUNGEON, M_SERV1, P_SERV1, N(P_SERV1),
      EN_SERV1,     N(EN_SERV1),     7,  30 , AMB_NADA },
    { N_("SERVIDOR -2"), TEMA_DUNGEON, M_SERV2, P_SERV2, N(P_SERV2),
      EN_SERV2,     N(EN_SERV2),     7,  30 , AMB_NADA },
    { N_("SALA DE MAQUINAS"), TEMA_DUNGEON, M_JEFE6, P_JEFE6, N(P_JEFE6),
      EN_JEFE6,     N(EN_JEFE6),     7,   0 , AMB_NADA },

    /* zone 7 */
    { N_("LLANURA MUERTA"), TEMA_PUEBLO, M_LLANURA, P_LLANURA, N(P_LLANURA),
      EN_LLANURA,   N(EN_LLANURA),   7,  30 , AMB_POLVO },
    { N_("VILLA OXIDO"), TEMA_PUEBLO, M_OXIDO, P_OXIDO, N(P_OXIDO),
      EN_OXIDO,     N(EN_OXIDO),     7,   0 , AMB_POLVO },
    { N_("TALLER DE VILLA OXIDO"), TEMA_INTERIOR, M_OXIDO_INT, P_OXIDO_INT,
      N(P_OXIDO_INT), EN_OXIDO_INT, N(EN_OXIDO_INT), 7, 0 , AMB_NADA },
    { N_("CEMENTERIO DE ROBOTS"), TEMA_PUEBLO, M_CEMENT1, P_CEMENT1, N(P_CEMENT1),
      EN_CEMENT1,   N(EN_CEMENT1),   8,  32 , AMB_POLVO },
    { N_("CEMENTERIO -2"), TEMA_PUEBLO, M_CEMENT2, P_CEMENT2, N(P_CEMENT2),
      EN_CEMENT2,   N(EN_CEMENT2),   8,  32 , AMB_POLVO },
    { N_("LA FOSA"), TEMA_PUEBLO, M_JEFE7, P_JEFE7, N(P_JEFE7),
      EN_JEFE7,     N(EN_JEFE7),     8,   0 , AMB_POLVO },

    /* zone 8 */
    { N_("ULTIMO TRAMO"), TEMA_PUEBLO, M_ULTIMO, P_ULTIMO, N(P_ULTIMO),
      EN_ULTIMO,    N(EN_ULTIMO),    8,  30 , AMB_NADA },
    { N_("PRISMA"), TEMA_PUEBLO, M_PRISMA, P_PRISMA, N(P_PRISMA),
      EN_PRISMA,    N(EN_PRISMA),    8,   0 , AMB_NADA },
    { N_("TALLER DE PRISMA"), TEMA_INTERIOR, M_PRISMA_INT, P_PRISMA_INT,
      N(P_PRISMA_INT), EN_PRISMA_INT, N(EN_PRISMA_INT), 8, 0 , AMB_NADA },
    { N_("LA TORRE"), TEMA_DUNGEON, M_TORRE1, P_TORRE1, N(P_TORRE1),
      EN_TORRE1,    N(EN_TORRE1),    8,  32 , AMB_NADA },
    { N_("TORRE -2"), TEMA_DUNGEON, M_TORRE2, P_TORRE2, N(P_TORRE2),
      EN_TORRE2,    N(EN_TORRE2),    8,  32 , AMB_NADA },
    { N_("LA CUMBRE"), TEMA_DUNGEON, M_CUMBRE, P_CUMBRE, N(P_CUMBRE),
      EN_CUMBRE,    N(EN_CUMBRE),    8,   0 , AMB_NIEVE },
};

const uint8_t ch_nsalas = (uint8_t)(sizeof(ch_salas) / sizeof(ch_salas[0]));

/* --------------------------------------------------------------------------
 * The eight zones
 *
 * The only thing needed for the world map: what each one is called, which flag
 * says you have cleared it and which rooms make it up.
 * -------------------------------------------------------------------------- */
const ch_zona_t ch_zonas_tab[ZONAS] = {
    { N_("VILLA TUERCA"),  F_JEFE_DESGUACE,  S_CASA,   S_JEFE   },
    { N_("PUERTO BUJIA"),  F_JEFE_PUERTO,    S_COSTA,  S_JEFE2  },
    { N_("ALTO VOLTIO"),   F_JEFE_VOLTIO,    S_CUESTA, S_JEFE3  },
    { N_("FUNDICION"),     F_JEFE_FUNDICION, S_HUMO,   S_JEFE4  },
    { N_("CRIOVALLE"),     F_JEFE_CRIO,      S_PASO,   S_JEFE5  },
    { N_("CIUDAD MALLA"),  F_JEFE_MALLA,     S_AUTOPISTA, S_JEFE6 },
    { N_("VILLA OXIDO"),   F_JEFE_PARAMO,    S_LLANURA, S_JEFE7 },
    { N_("PRISMA"),        F_JEFE_PRISMA,    S_ULTIMO, S_CUMBRE },
};
