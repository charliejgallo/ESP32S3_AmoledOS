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

    /* What a piece of furniture hides. One flag each, so it is found ONCE:
     * without it a bookcase is a machine for printing oil. */
    F_MUEBLE_1, F_MUEBLE_2, F_MUEBLE_3, F_MUEBLE_4, F_MUEBLE_5,
    F_MUEBLE_6, F_MUEBLE_7, F_MUEBLE_8,

    /* One per zone: set the first time you walk into it, and it is what the
     * map's fast travel unlocks. */
    F_VISITA_1, F_VISITA_2, F_VISITA_3, F_VISITA_4,
    F_VISITA_5, F_VISITA_6, F_VISITA_7, F_VISITA_8,

    /* TEMPORAL: la entrega de piezas de prueba, una sola vez. */
    /* Las dos de la reparticion de prueba de la v2. La funcion se fue; las
     * banderas se quedan, porque sacarlas correria todas las de abajo y un
     * guardado en curso leeria "cofre abierto" donde dice "jefe vencido". */
    F_PRUEBA_PIEZAS, F_PRUEBA_PIEZAS2,

    /* La feria paga su pieza una sola vez. */
    F_FERIA_PIEZA,

    F_ULTIMA_DEL_MUNDO,

    /* zone 8 - Torre Prisma */
    F_JEFE_PRISMA, F_FINAL,
    F_COFRE_T1, F_COFRE_T2, F_COFRE_T3,
    F_ENEMIGO_52, F_ENEMIGO_53, F_ENEMIGO_54, F_ENEMIGO_55,
    F_ENEMIGO_56, F_ENEMIGO_57,
};

/* Room identifiers. */
enum {
    S_CASA = 0,
    S_PUEBLO,                   /* Villa Tuerca, sector noroeste            */
    S_TUERCA_NE,
    S_TUERCA_SO,
    S_TUERCA_SE,
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
    S_PUERTO_E,
    S_PUERTO_INT,
    S_BODEGA1,
    S_BODEGA2,
    S_JEFE2,
    /* zone 3 */
    S_CUESTA,
    S_VOLTIO,
    S_VOLTIO_E,
    S_VOLTIO_INT,
    S_SUB1,
    S_SUB2,
    S_JEFE3,
    /* zone 4 */
    S_HUMO,
    S_FUNDICION,
    S_FUNDICION_E,
    S_FUND_INT,
    S_HORNO1,
    S_HORNO2,
    S_JEFE4,
    /* zone 5 */
    S_PASO,
    S_CRIO,
    S_CRIO_E,
    S_CRIO_INT,
    S_CUEVA1,
    S_CUEVA2,
    S_JEFE5,
    /* zone 6 */
    S_AUTOPISTA,
    S_MALLA,
    S_MALLA_E,
    S_MALLA_INT,
    S_SERV1,
    S_SERV2,
    S_JEFE6,
    /* zone 7 */
    S_LLANURA,
    S_OXIDO,
    S_OXIDO_E,
    S_OXIDO_INT,
    S_CEMENT1,
    S_CEMENT2,
    S_JEFE7,
    /* zone 8 */
    S_ULTIMO,
    S_PRISMA,
    S_PRISMA_E,
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
    "000000000000000", "0|||||||||||||0",
    "0|___________|0", "0|___________|0",
    "0|___________|0", "0|_ooooooooo_|0",
    "0|_ooooooooo_|0", "0|_ooooooooo_|0",
    "0|_ooooooooo_|0", "0|___________|0",
    "0|___________|0", "0|___________|0",
    "0||||||___||||0", "000000000000000",
};

static const ch_ent_t EN_CASA[] = {
    { E_PUERTA,  7, 12, S_PUEBLO,    3,  5, 3, NULL, NULL },
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
    { E_COFRE,  11,  3, IT_ACEITE, 2, F_COFRE_CASA, 0, NULL, NULL },
    { E_CARTEL,  3,  3, 0, 0, 0, 0,
      N_("UNA FOTO VIEJA: TU\n"
      "ABUELA JOVEN, AL LADO\n"
      "DE UN ROBOT ENORME.\n"
      "ABAJO DICE:\n"
      "CAMPEONA, ANO 12."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0,
      N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0,
      N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0,
      N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0,
      N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

_Static_assert(F_ULTIMA_DEL_MUNDO < F_CFG,
               "las banderas del mundo pisan la reserva de configuracion");

/* --------------------------------------------------------------------------
 * ROOM 1 - VILLA TUERCA
 * -------------------------------------------------------------------------- */

static const char *const M_PUEBLO[ROWS] = {
    "===============",
    "=     jjj      ",
    "=     jjj      ",
    "=     jjj      ",
    "=  ee jjj  vv  ",
    "=jjjjjjjjjjjjjj",
    "=jjjjjjjjjjjjjj",
    "=jjjjjjjjjjjjjj",
    "=  vv jjj      ",
    "= ~~~ jjj      ",
    "= ~~~ jjj      ",
    "= ~~~ jjj   ee ",
    "=  ee jjj   ee ",
    "=     jjj      ",
};

static const char *const M_TUERCA_NE[ROWS] = {
    "=====.....=====",
    "     .....     ",
    "     vv..vv    ",
    "      jjj      ",
    "   ee jjj  ee  ",
    "jjjjjjjjjjjjjj=",
    "jjjjjjjjjjjjjj=",
    "jjjjjjjjjjjjjj=",
    "      jjj  vv =",
    "  ee  jjj     =",
    "  ee  jjj     =",
    "      jjj     =",
    "      jjj  vv =",
    "      jjj     =",
};

static const char *const M_TUERCA_SO[ROWS] = {
    "=     jjj      ",
    "=     jjj      ",
    "=     jjj      ",
    "=  vv jjj   ee ",
    "=jjjjjjjjjjjjjj",
    "=jjjjjjjjjjjjjj",
    "=jjjjjjjjjjjjjj",
    "=  ee jjj      ",
    "=     jjj  vv  ",
    "=     jjj      ",
    "=  vv jjj      ",
    "=     jjj   ee ",
    "=     jjj      ",
    "===============",
};

static const char *const M_TUERCA_SE[ROWS] = {
    "      jjj     =",
    "      jjj     =",
    "   ee jjj  vv =",
    "      jjj     =",
    "jjjjjjjjjjjjjj=",
    "jjjjjjjjjjjjjj=",
    "jjjjjjjjjjjjjj=",
    "   vv jjj     =",
    "      jjj  ee =",
    "      jjj     =",
    "   ee jjj     =",
    "      jjj  vv =",
    "      jjj     =",
    "===============",
};

static const ch_prop_t P_PUEBLO[] = {
    { 2,  1, PR_CASA },          /* the door lands on cell 3,4             */
    { 11, 1, PR_ARBOL },
    { 11, 9, PR_ARBOL },
    { 10, 4, PR_FAROLA },
};

static const ch_ent_t EN_PUEBLO[] = {
    /* The doors that lead to the next SECTOR of the same town sit on the
     * paths, at the edge of the map, so crossing one slides and the line you
     * were walking carries on. The house's door is in the middle of the
     * sector and fades: walking into a house is not walking east. */
    { E_PUERTA,  3,  4, S_CASA,     7, 10, 2, NULL, NULL },
    { E_PUERTA, 14,  5, S_TUERCA_NE, 2,  6, 3, NULL, NULL },
    { E_PUERTA,  6, 13, S_TUERCA_SO, 7,  2, 3, NULL, NULL },
    { E_PNJ,    13, 12, 3, F_MISION_CUMPLIDA, F_TORNILLOS, IT_SOLDADOR,
      N_("VECINO: SE ME CAYERON\n"
      "LOS TORNILLOS EN EL\n"
      "DEPOSITO DEL DESGUACE.\n"
      "ESTA AL NORTE, PASANDO\n"
      "EL SENDERO.\n"
      "SI ME LOS TRAES TE DOY\n"
      "ALGO QUE TE VA A SERVIR."),
      N_("VECINO: MIS TORNILLOS!\n"
      "TOMA, ESTE SOLDADOR ERA\n"
      "DE MI PADRE.") },
    { E_MUEBLE,  1,  8, MU_BANCO, 0, 0, 0,
      N_("UN BANCO DE PLAZA.\n"
      "MIRA AL ESTANQUE."), NULL },
    { E_MUEBLE, 13,  4, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  9,  8, MU_MACETA, 0, 0, 0,
      N_("UNA MACETA DE LA VEREDA.\n"
      "ALGUIEN LE PUSO UN\n"
      "CARTELITO: NO PISAR."), NULL },
    { E_ANIMAL, 12,  4, AN_GATO, 0, 0, 0,
      N_("UN GATO NEGRO.\n"
      "TE MIRA, BOSTEZA, Y\n"
      "SIGUE EN LO SUYO."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 2 - The grandmother's workshop
 * -------------------------------------------------------------------------- */

static const char *const M_TALLER[ROWS] = {
    "000000000000000", "0|||||||||||||0",
    "0|+++++++++++|0", "0|---------++|0",
    "0|+++++++++++|0", "0|+++++++++++|0",
    "0|++ooooooo++|0", "0|++ooooooo++|0",
    "0|++ooooooo++|0", "0|+++++++++++|0",
    "0|+++++++++++|0", "0|+++++++++++|0",
    "0||||||+++||||0", "000000000000000",
};

static const ch_prop_t P_TUERCA_NE[] = {
    { 10, 1, PR_TALLER },        /* the door lands on cell 11,4            */
    { 1,  9, PR_ARBOL },
    { 12, 8, PR_FAROLA },
};

static const ch_ent_t EN_TUERCA_NE[] = {
    { E_PUERTA,  5,  0, S_SENDERO,   7, 11, 5, NULL, NULL },
    { E_PUERTA,  0,  5, S_PUEBLO,   12,  6, 3, NULL, NULL },
    { E_PUERTA,  6, 13, S_TUERCA_SE, 7,  2, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_TALLER,    7, 10, 2, NULL, NULL },
    { E_PNJ,     3,  2, 4, 0, 0, 0,
      N_("MECANICO: EL TALLER ES\n"
      "DE LA ABUELA TUERCA.\n"
      "SI TE ROMPEN EL ROBOT,\n"
      "TE LO DEJA COMO NUEVO\n"
      "Y NO TE COBRA."), NULL },
    { E_MUEBLE,  2,  4, MU_BANCO, 0, 0, 0,
      N_("UN BANCO GASTADO.\n"
      "ACA ESPERAN LOS QUE\n"
      "DEJAN EL ROBOT EN EL\n"
      "TALLER."), NULL },
    { E_MUEBLE,  4,  8, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_ANIMAL,  3, 12, AN_PAJARO, 0, 0, 0,
      N_("UN PAJARO PICOTEA\n"
      "TORNILLOS DEL PISO.\n"
      "NO PARECE UNA BUENA\n"
      "DIETA."), NULL },
};

static const ch_prop_t P_TUERCA_SO[] = {
    { 10, 1, PR_CASA },          /* the door lands on cell 11,4            */
    { 2,  8, PR_FUENTE },
    { 12,10, PR_ARBOL },
    { 5, 10, PR_FAROLA },
};

static const ch_ent_t EN_TUERCA_SO[] = {
    { E_PUERTA,  6,  0, S_PUEBLO,    7, 11, 3, NULL, NULL },
    { E_PUERTA, 14,  4, S_TUERCA_SE, 2,  5, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_VECINO,    7, 10, 2, NULL, NULL },
    { E_CARTEL,  3, 11, 0, 0, 0, 0,
      N_("VILLA TUERCA\n"
      "POBLACION: 34 PERSONAS\n"
      "Y UNOS CUANTOS ROBOTS."), NULL },
    { E_PNJ,    11,  9, 2, 0, 0, 0,
      N_("CHICO: MI HERMANA DICE\n"
      "QUE EN EL DESGUACE HAY\n"
      "PIEZAS BUENISIMAS.\n"
      "YO NO ENTRARIA. HAY\n"
      "ROBOTS SUELTOS."), NULL },
    { E_MUEBLE,  9,  8, MU_BANCO, F_MUEBLE_2, 0, IT_ACEITE,
      N_("UN BANCO FRENTE A LA\n"
      "FUENTE.\n"
      "ABAJO HAY ALGO."), NULL },
    { E_MUEBLE,  1,  3, MU_MACETA, 0, 0, 0, NULL, NULL },
    { E_MUEBLE, 13,  7, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_ANIMAL,  7, 11, AN_GATO, 0, 0, 0,
      N_("OTRO GATO. O EL MISMO,\n"
      "QUE SE MUEVE MAS RAPIDO\n"
      "DE LO QUE PARECE."), NULL },
};

static const ch_prop_t P_TUERCA_SE[] = {
    { 2,  1, PR_ARBOL },
    { 2,  9, PR_MAQUINA },
    { 10, 2, PR_FAROLA },
};

static const ch_ent_t EN_TUERCA_SE[] = {
    { E_PUERTA,  6,  0, S_TUERCA_NE, 7, 11, 3, NULL, NULL },
    { E_PUERTA,  0,  4, S_TUERCA_SO, 12, 5, 3, NULL, NULL },
    /* The booth: the other watch. One per town, always at the side of the
     * square, because it is a place you go to and not a menu you open. */
    { E_CABINA, 11,  9, 0, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  4,  8, MU_BANCO, 0, 0, 0,
      N_("UN BANCO AL LADO DE LA\n"
      "CABINA.\n"
      "PARA ESPERAR TURNO."), NULL },
    { E_MUEBLE, 10, 11, MU_MACETA, 0, 0, 0, NULL, NULL },
    { E_ANIMAL, 12,  2, AN_PAJARO, 0, 0, 0,
      N_("UN PAJARO EN EL CANTERO.\n"
      "SE VA APENAS TE ACERCAS."), NULL },
    { E_CHATARRERO,  2, 11, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const ch_prop_t P_TALLER_INT[] = {
    { 2,   2, PR_MAQUINA },
    { 8,   8, PR_MAQUINA },      /* the workbench: touched from below */
};

static const ch_ent_t EN_TALLER[] = {
    { E_PUERTA,  7, 12, S_TUERCA_NE, 11, 5, 3, NULL, NULL },
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
    { E_PNJ,    11,  4, 1, 0, 0, 0,
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
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0,
      N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0,
      N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0,
      N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0,
      N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 3 - The neighbour's house (for now entered from the town later on)
 * -------------------------------------------------------------------------- */

static const char *const M_VECINO[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|WWWWWWWWWWW|0",
    "0|WWWWWWWWWWW|0",
    "0|WWWWWWWWWWW|0",
    "0|WWooooooWWW|0",
    "0|WWooooooWWW|0",
    "0|WWooooooWWW|0",
    "0|WWWWWWWWWWW|0",
    "0|WWWWWWWWWWW|0",
    "0|WWWWWWWWWWW|0",
    "0|WWWWWWWWWWW|0",
    "0||||||WWW||||0",
    "000000000000000",
};

static const ch_ent_t EN_VECINO[] = {
    { E_PUERTA,  7, 12, S_TUERCA_SO, 11,  5, 3, NULL, NULL },
    { E_MUEBLE,  3,  2, MU_CUADRO, 0, 0, 0,
      N_("UN CUADRO DE UN LAGO.\n"
      "LA FIRMA DICE: TUERCA,\n"
      "ANO 9."), NULL },
    { E_MUEBLE,  6,  2, MU_CUADRO, 0, 0, 0,
      N_("LA FOTO DE UN ROBOT\n"
      "VIEJO, CON UNA MEDALLA.\n"
      "ATRAS ALGUIEN ESCRIBIO:\n"
      "SIEMPRE VOLVIA."), NULL },
    { E_MUEBLE,  9,  4, MU_ESTANTE, F_MUEBLE_1, 0, IT_ACEITE,
      N_("UNA BIBLIOTECA LLENA DE\n"
      "MANUALES.\n"
      "ATRAS DE UN TOMO HAY\n"
      "ALGO GUARDADO."), NULL },
    { E_MUEBLE,  2,  3, MU_COMPU, 0, 0, 0,
      N_("UNA TERMINAL VIEJA.\n"
      "EN LA PANTALLA PARPADEA\n"
      "UNA PARTIDA DE AJEDREZ\n"
      "SIN TERMINAR."), NULL },
    { E_MUEBLE, 12,  4, MU_VASIJA, 0, 0, 0,
      N_("UNA VASIJA DE BARRO.\n"
      "ADENTRO HAY TORNILLOS\n"
      "DE TODAS LAS MEDIDAS."), NULL },
    { E_MUEBLE,  4,  6, MU_MESA, 0, 0, 0,
      N_("LA MESA, PUESTA PARA\n"
      "DOS.\n"
      "HACE RATO QUE ESPERA."), NULL },
    { E_MUEBLE,  3,  6, MU_SILLA, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  6,  6, MU_SILLA, 0, 0, 0, NULL, NULL },
    { E_MUEBLE, 10,  9, MU_CAMA, 0, 0, 0,
      N_("LA CAMA, TENDIDA.\n"
      "MEJOR NO."), NULL },
    { E_MUEBLE,  2, 10, MU_PLANTA, 0, 0, 0,
      N_("UNA PLANTA DE INTERIOR.\n"
      "ALGUIEN LA RIEGA TODOS\n"
      "LOS DIAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 4 - Northern path
 * -------------------------------------------------------------------------- */

static const char *const M_SENDERO[ROWS] = {
    "=====.....=====",
    "     .....     ",
    "      ...      ",
    "  \"\"\" ... \"\"\"  ",
    "  \"\"\" ... \"\"\"  ",
    "      ...      ",
    "~~~~~~^^^~~~~~~",
    "      .........",
    "      .........",
    "  \"\"\" ... \"\"\"  ",
    "      ...      ",
    "      ...      ",
    "     .....     ",
    "=====.....=====",
};

static const ch_prop_t P_SENDERO[] = {
    { 0,  1, PR_PINO },
    { 13, 1, PR_PINO },
    { 0, 10, PR_PINO },
    { 13,10, PR_PINO },
};

static const ch_ent_t EN_SENDERO[] = {
    { E_PUERTA,  5, 13, S_TUERCA_NE, 7,  2, 5, NULL, NULL },
    { E_PUERTA,  5,  0, S_DESGUACE1, 7, 11, 5, NULL, NULL },
    { E_PUERTA, 14,  7, S_COSTA,     2,  5, 2, NULL, NULL },
    /* The gate to zone 2. It is a pair of BLOCKS and not a closed door: a
     * door that refuses is indistinguishable from a door that is broken. */
    { E_BLOQUEO,12,  7, F_JEFE_DESGUACE, 0, 0, 0,
      N_("UN CONTROL BAJADO.\n"
      "CARTEL: PROHIBIDO PASAR AL\n"
      "ESTE SIN PASE DE SECTOR.\n"
      "EL PASE LO DA EL GUARDIAN\n"
      "DEL DESGUACE."),
      N_("EL CONTROL ESTA LEVANTADO.\n"
      "AL ESTE: PUERTO BUJIA.") },
    { E_BLOQUEO,12,  8, F_JEFE_DESGUACE, 0, 0, 0,
      N_("UN CONTROL BAJADO.\n"
      "AL ESTE NO SE PASA SIN\n"
      "PASE DE SECTOR."),
      N_("EL CONTROL ESTA LEVANTADO."), },
    { E_CARTEL, 11,  4, 0, 0, 0, 0,
      N_("SENDERO NORTE\n"
      "AL DESGUACE.\n"
      "CUIDADO: HAY ROBOTS\n"
      "SUELTOS ENTRE LOS\n"
      "YUYOS."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 5 - The scrapyard, entrance
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE1[ROWS] = {
    "XXXXXX:::XXXXXX",
    "XMMMMM:::MMMMMX",
    "XMMxxxMMMMMxxxX",
    "XMMxxxMMMMMxxxX",
    "XMMMMMMMMMMMMMX",
    "XMM***MMM***MMX",
    "XMM***MMM***MMX",
    "XMMMMMMMMMMMMMX",
    "XMMxxxMMMMMxxxX",
    "XMMxxxMMMMMxxxX",
    "XMMMMMMMMMMMMMX",
    "XMMMMM:::MMMMMX",
    "XMMMMM:::MMMMMX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_DESGUACE1[] = {
    { 1,  4, PR_PILA },
    { 12, 7, PR_PILA },
    { 11,11, PR_MAQUINA },
};

static const ch_ent_t EN_DESGUACE1[] = {
    { E_PUERTA,  6, 13, S_SENDERO,    7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_DESGUACE2,  7, 11, 3, NULL, NULL },
    { E_COFRE,   2,  7, IT_ACEITE, 1, F_COFRE_DESGUACE1, 0, NULL, NULL },
    { E_ENEMIGO,11,  3, 1, F_ENEMIGO_3, 5, 0, NULL, NULL },
    { E_ENEMIGO, 4,  9, 1, F_ENEMIGO_4, 5, 0, NULL, NULL },
    { E_CARTEL,  7,  6, 0, 0, 0, 0,
      N_("UN CARTEL TORCIDO:\n"
      "DESGUACE MUNICIPAL\n"
      "NO ALIMENTE A LOS\n"
      "ROBOTS."), NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 6 - The scrapyard, corridor
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE2[ROWS] = {
    "XXXXXX:::XXXXXX",
    "XMMMMM:::MMMMMX",
    "XMMMMMMMMMMMMMX",
    "XMxxxMMMMMxxxMX",
    "XMxxxMMMMMxxxMX",
    "::MMMMMMMMMMMMX",
    "::MMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMX",
    "XMM***MMM***MMX",
    "XMM***MMM***MMX",
    "XMMMMMMMMMMMMMX",
    "XMMMMM:::MMMMMX",
    "XMMMMM:::MMMMMX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_DESGUACE2[] = {
    { 1,  9, PR_MAQUINA },
    { 12, 9, PR_PILA },
};

static const ch_ent_t EN_DESGUACE2[] = {
    { E_PUERTA,  6, 13, S_DESGUACE1,  7, 11, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE,       7, 11, 3, NULL, NULL },
    { E_PUERTA,  0,  5, S_DESGUACE3, 12,  5, 2, NULL, NULL },
    { E_COFRE,  12,  4, IT_BATERIA, 2, F_COFRE_DESGUACE2, 0, NULL, NULL },
    { E_ENEMIGO, 3,  9, 1, F_ENEMIGO_5, 6, 0, NULL, NULL },
    { E_ENEMIGO,11,  2, 1, F_ENEMIGO_6, 6, 0, NULL, NULL },
    { E_ENEMIGO, 7,  9, 1, F_ENEMIGO_7, 7, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 7 - The store (the neighbour's screws)
 * -------------------------------------------------------------------------- */

static const char *const M_DESGUACE3[ROWS] = {
    "XXXXXXXXXXXXXXX",
    "XMMMMMMMMMMMMMX",
    "XMxxxxxxxxxxxMX",
    "XMxxxxxxxxxxxMX",
    "XMMMMMMMMMMMMMX",
    "XMM:::::::::MM:",
    "XMM:::::::::MM:",
    "XMMMMMMMMMMMMMX",
    "XMxxxxxxxxxxxMX",
    "XMxxxxxxxxxxxMX",
    "XMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMX",
    "XMMMMMMMMMMMMMX",
    "XXXXXXXXXXXXXXX",
};

static const ch_prop_t P_DESGUACE3[] = {
    { 2, 11, PR_PILA },
    { 11,11, PR_PILA },
};

static const ch_ent_t EN_DESGUACE3[] = {
    { E_PUERTA, 14,  5, S_DESGUACE2,  2,  5, 2, NULL, NULL },
    { E_COFRE,   6,  1, IT_LLAVE, 1, F_COFRE_DESGUACE3, 0, NULL, NULL },
    { E_COFRE,   6, 11, IT_TORNILLOS, 1, F_TORNILLOS, 0, NULL, NULL },
    { E_ENEMIGO,10,  8, 1, F_ENEMIGO_8, 7, 0, NULL, NULL },
};

/* --------------------------------------------------------------------------
 * ROOM 8 - The sub-boss
 * -------------------------------------------------------------------------- */

static const char *const M_JEFE[ROWS] = {
    "XXXXXXXXXXXXXXX",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "X:::::::::::::X",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_JEFE[] = {
    { 2,  1, PR_MAQUINA },
    { 11, 1, PR_MAQUINA },
};

static const ch_ent_t EN_JEFE[] = {
    { E_PUERTA,  6, 13, S_DESGUACE2,  7, 11, 3, NULL, NULL },
    { E_JEFE,    7,  4, 3, F_JEFE_DESGUACE, 12, IT_PASE,
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
    "###############",
    "## ddd   ddd ##",
    "## ddd   ddd ##",
    "##           ##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "##           ##",
    "## ddd ddddd ##",
    "## ddd ddddd ##",
    "SSSSSSSSSSSSSSS",
    "SSSSSSSSSSSSSSS",
    "zzzzzzzzzzzzzzz",
    "~~~~~~~~~~~~~~~",
};

static const ch_prop_t P_COSTA[] = {
    { 2,  1, PR_PINO },
    { 11, 1, PR_PINO },
    { 5, 10, PR_BARCO },
    { 2,  8, PR_PILA },
};

static const ch_ent_t EN_COSTA[] = {
    { E_PUERTA,  0,  4, S_SENDERO,  11,  7, 3, NULL, NULL },
    { E_PUERTA, 14,  4, S_PUERTO,    2,  5, 3, NULL, NULL },
    { E_CARTEL,  3, 11, 0, 0, 0, 0,
      N_("PLAYA DEL CANGREJO.\n"
      "NO SE PERMITE OXIDARSE\n"
      "EN LA ARENA."), NULL },
    { E_COFRE,  12, 11, IT_ACEITE2, 1, F_COFRE_P1, 0, NULL, NULL },
    { E_ENEMIGO, 4,  2, 0, F_ENEMIGO_9,  10, 0, NULL, NULL },
    { E_ENEMIGO,11,  8, 0, F_ENEMIGO_10, 11, 0, NULL, NULL },
    { E_ENEMIGO, 4,  9, 0, F_ENEMIGO_11, 12, 0, NULL, NULL },
};

static const char *const M_PUERTO[ROWS] = {
    "######GGG######",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "###############",
};

static const ch_prop_t P_PUERTO[] = {
    { 1,  1, PR_CASA },
    { 10, 1, PR_TALLER },        /* the door lands on cell 11,4            */
    { 1,  9, PR_CASA },
    { 10, 9, PR_ESTATUA },
    { 13, 3, PR_FAROLA },
};

static const ch_ent_t EN_PUERTO[] = {
    { E_PUERTA,  0,  5, S_COSTA,    12,  5, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_PUERTO_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_BODEGA1,   7, 11, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_PUERTO_INT, 7, 10, 2, NULL, NULL },
    { E_CARTEL,  3, 12, 0, 0, 0, 0,
      N_("PUERTO BUJIA\n"
      "AQUI SE DESCARGA TODO LO\n"
      "QUE SE OXIDA DESPUES.\n"
      "AL NORTE: LA BODEGA."), NULL },
    { E_PNJ,    12, 12, 2, 0, 0, 0,
      N_("CHICO: EN LA BODEGA HAY\n"
      "AGUA HASTA LAS RODILLAS\n"
      "Y ROBOTS QUE NO SALEN\n"
      "NUNCA DE AHI."), NULL },
};

static const char *const M_PUERTO_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const char *const M_PUERTO_E[ROWS] = {
    "###############",
    "#pppppppppppp##",
    "#pppppppppppp##",
    "#pppppppppppp##",
    "#pppppppppppp##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#pppppppppppp##",
    "#pppppwwwwwww##",
    "#Sppppwwwwwww##",
    "#SSSSSzzzzzzz##",
    "#SSSSS~~~~~~~##",
    "###############",
};

static const ch_prop_t P_PUERTO_E[] = {
    { 7,  9, PR_BARCO },
    { 11, 1, PR_PILA },
    { 3,  3, PR_FAROLA },
};

static const ch_ent_t EN_PUERTO_E[] = {
    { E_PUERTA,  0,  5, S_PUERTO,   12,  5, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_CUESTA,    2,  5, 3, NULL, NULL },
    { E_BLOQUEO,12,  5, F_JEFE_PUERTO, 0, 0, 0,
      N_("CONTROL DEL PUERTO.\n"
      "AL ESTE SUBE LA CUESTA\n"
      "HACIA ALTO VOLTIO.\n"
      "NO SE PASA SIN PASE."),
      N_("EL CONTROL ESTA ABIERTO.\n"
      "AL ESTE: LA CUESTA.") },
    { E_BLOQUEO,12,  6, F_JEFE_PUERTO, 0, 0, 0,
      N_("CONTROL DEL PUERTO.\n"
      "AL ESTE SUBE LA CUESTA."),
      N_("EL CONTROL ESTA ABIERTO."), },
    { E_BLOQUEO,12,  7, F_JEFE_PUERTO, 0, 0, 0,
      N_("CONTROL DEL PUERTO.\n"
      "AL ESTE SUBE LA CUESTA."),
      N_("EL CONTROL ESTA ABIERTO."), },
    { E_PNJ,     4, 10, 4, 0, 0, 0,
      N_("PESCADORA: EL AGUA SALADA\n"
      "SE COME LOS CIRCUITOS.\n"
      "POR ESO ACA TODO PEGA\n"
      "CON ACIDO."), NULL },
    { E_PNJ,     3,  1, 3, F_MISION_ANCLA, F_ANCLA, IT_BATERIA2,
      N_("CAPITAN: PERDI EL ANCLA\n"
      "DE MI BARCO EN LA BODEGA.\n"
      "TRAEMELA Y TE DOY LA\n"
      "MEJOR BATERIA QUE TENGO."),
      N_("CAPITAN: EL ANCLA! AHORA\n"
      "SI PUEDO SALIR A PESCAR.\n"
      "TOMA, TE LA GANASTE.") },
    { E_CABINA,  3,  8, 0, 0, 0, 0, NULL, NULL },
    /* LA FERIA DEL PUERTO. Va en los muelles y no en el pueblo inicial a
     * proposito: cuando llegas aca ya sabes que es una pieza y para que sirve
     * un credito, que es lo que hace que el premio signifique algo. */
    { E_FERIA,   4,  2, 0, 0, 0, 0,
      N_("FERIANTE: PASA, PASA.\n"
      "LA CINTA TRAE CHATARRA\n"
      "DE TODO EL PUERTO.\n"
      "AGARRA LAS VERDES Y\n"
      "DEJA PASAR LAS OXIDADAS.\n"
      "TE PAGO POR PIEZA."), NULL },
    { E_CHATARRERO, 11,  1, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const ch_prop_t P_PUERTO_INT[] = {
    { 2,   2, PR_MAQUINA },
    { 8,   8, PR_MAQUINA },
};

static const ch_ent_t EN_PUERTO_INT[] = {
    { E_PUERTA,  7, 12, S_PUERTO,   11,  5, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0,
      N_("EL BANCO DEL PUERTO.\n"
      "HUELE A SALITRE PERO\n"
      "DEJA EL ROBOT COMO NUEVO."), NULL },
    { E_TIENDA,  5,  4, 0, 0, 0, 0,
      N_("MECANICO: TENGO DE TODO,\n"
      "Y TODO UN POCO OXIDADO.\n"
      "MIRA TRANQUILO."), NULL },
    { E_PNJ,    11,  6, 1, 0, 0, 0,
      N_("ABUELA TUERCA: TE SEGUI\n"
      "HASTA ACA PARA DECIRTE\n"
      "UNA COSA.\n"
      "EL TIPO DEL TORSO MANDA:\n"
      "SI PEGAS CON TU PROPIO\n"
      "TIPO, HACES MAS DANO.\n"
      "ACA TODOS PEGAN CON\n"
      "ACIDO. LLEVA ALGO QUE\n"
      "RESISTA."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0,
      N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0,
      N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0,
      N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0,
      N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_BODEGA1[ROWS] = {
    "XXXXXX:::XXXXXX",
    "XMMMMM:::MMMMMX",
    "XMMMMMMMMMMMMMX",
    "XMzzzMMMMMzzzMX",
    "XMzzzMMMMMzzzMX",
    "XMMMMMMMMMMMMMX",
    "XMMMXXXXXXXMMMX",
    "XMMMXzzzzzXMMMX",
    "XMMMXzzzzzXMMMX",
    "XMMMXXX:XXXMMMX",
    "XMMMMMMMMMMMMMX",
    "XMMMMM:::MMMMMX",
    "XMMMMM:::MMMMMX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_BODEGA1[] = {
    { 1,  3, PR_PILA },
    { 12,10, PR_PILA },
    { 12, 2, PR_MAQUINA },
};

static const ch_ent_t EN_BODEGA1[] = {
    { E_PUERTA,  6, 13, S_PUERTO,    7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_BODEGA2,   7, 11, 3, NULL, NULL },
    { E_COFRE,   7,  7, IT_CHIP, 1, F_COFRE_P3, 0, NULL, NULL },
    { E_ENEMIGO, 3,  3, 0, F_ENEMIGO_12, 13, 0, NULL, NULL },
    { E_ENEMIGO,11, 11, 0, F_ENEMIGO_13, 13, 0, NULL, NULL },
    { E_CARTEL,  3,  2, 0, 0, 0, 0,
      N_("BODEGA MUNICIPAL\n"
      "NIVEL DEL AGUA: ALTO.\n"
      "NO RESPONDEMOS POR\n"
      "CIRCUITOS MOJADOS."), NULL },
};

static const char *const M_BODEGA2[ROWS] = {
    "XXXXXX:::XXXXXX",
    "XMMMMM:::MMMMMX",
    "XMMMMMMMMMMMMMX",
    "XzzzzzzzzzzzzzX",
    "XzXXXzzzzzXXXzX",
    "XzXxxzzzzzxxXzX",
    "XzXXXzzzzzXXXzX",
    "XzzzzzzzzzzzzzX",
    "XzzzzzzzzzzzzzX",
    "XMMMMMMMMMMMMMX",
    "XMzzzMMMMMzzzMX",
    "XMMMMM:::MMMMMX",
    "XMMMMM:::MMMMMX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_BODEGA2[] = {
    { 1, 10, PR_MAQUINA },
    { 12, 2, PR_PILA },
};

static const ch_ent_t EN_BODEGA2[] = {
    { E_PUERTA,  6, 13, S_BODEGA1,   7, 11, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE2,     7, 11, 3, NULL, NULL },
    { E_COFRE,   3,  5, IT_SOLDADOR, 1, F_COFRE_P2, 0, NULL, NULL },
    { E_COFRE,  10,  5, IT_ANCLA, 1, F_ANCLA, 0, NULL, NULL },
    { E_ENEMIGO, 2,  7, 0, F_ENEMIGO_14, 14, 0, NULL, NULL },
    { E_ENEMIGO,12,  7, 0, F_ENEMIGO_15, 14, 0, NULL, NULL },
    { E_ENEMIGO, 7,  8, 0, F_ENEMIGO_16, 15, 0, NULL, NULL },
};

static const char *const M_JEFE2[ROWS] = {
    "XXXXXXXXXXXXXXX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "XwwwwwwwwwwwwwX",
    "Xwwwww:::wwwwwX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_JEFE2[] = {
    { 2,  1, PR_PILA },
    { 11, 1, PR_PILA },
};

static const ch_ent_t EN_JEFE2[] = {
    { E_PUERTA,  6, 13, S_BODEGA2,   7, 11, 3, NULL, NULL },
    { E_JEFE,    7,  4, 7, F_JEFE_PUERTO, 18, IT_PASE,
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
    "###############",
    "##ddd#####ddd##",
    "##ddd#####ddd##",
    "##GGGGGGGGGGG##",
    "GGGGGGGG####d##",
    "GGGGGGGG####d##",
    "GGGGGGGG####d##",
    "##d#####GGGGGGG",
    "##d#####GGGGGGG",
    "##ddd###GGGGGGG",
    "##ddd###ddddd##",
    "##ddd###ddddd##",
    "##ddd###ddddd##",
    "###############",
};

static const ch_prop_t P_CUESTA[] = {
    {  2,  1, PR_TORRE },
    { 11,  1, PR_TORRE },
    {  2, 10, PR_TORRE },
    {  9, 10, PR_PINO },
};

static const ch_ent_t EN_CUESTA[] = {
    { E_PUERTA,  0,  4, S_PUERTO_E, 11,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  7, S_VOLTIO,  2,  6, 3, NULL, NULL },
    { E_CARTEL,  4, 10, 0, 0, 0, 0, N_("LA CUESTA.\n"
      "ARRIBA: ALTO VOLTIO.\n"
      "NO TOQUE LOS CABLES\n"
      "CON LAS MANOS."), NULL },
    { E_COFRE, 12, 11, IT_BATERIA2, 1, F_COFRE_V1, 0, NULL, NULL },
    { E_ENEMIGO,  5,  4, 0, F_ENEMIGO_17, 16, 0, NULL, NULL },
    { E_ENEMIGO, 12,  5, 0, F_ENEMIGO_18, 17, 0, NULL, NULL },
    { E_ENEMIGO, 10,  8, 0, F_ENEMIGO_19, 18, 0, NULL, NULL },
};

static const char *const M_VOLTIO[ROWS] = {
    "######GGG######",
    "#ppppppppppppp#",
    "#ppppppppppppp#",
    "#pp#######pppp#",
    "#pp#GGGGG#pppp#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#pp#GGGGG#pppp#",
    "#pp#######pppp#",
    "#ppppppppppppp#",
    "#ppppppppppppp#",
    "#ppppppppppppp#",
    "###############",
};

static const ch_prop_t P_VOLTIO[] = {
    {  1,  1, PR_CASA },
    { 10,  1, PR_TALLER },
    {  1, 10, PR_TORRE },
    { 11, 10, PR_TORRE },
};

static const ch_ent_t EN_VOLTIO[] = {
    { E_PUERTA,  0,  5, S_CUESTA, 12,  8, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_SUB1,  7, 11, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_VOLTIO_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_VOLTIO_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("ALTO VOLTIO\n"
      "ALTURA: 900 METROS\n"
      "CONSUMO: TODO EL QUE\n"
      "HAGA FALTA."), NULL },
    { E_PNJ,  5, 11, 2, 0, 0, 0, N_("CHICO: SI TOCAS UN CABLE\n"
      "PELADO se te frien los\n"
      "CIRCUITOS.\n"
      "A MI HERMANO LE PASO."), NULL },
    { E_PNJ, 13,  4, 4, 0, 0, 0, N_("SENORA: LA SUBESTACION\n"
      "SE LLENO DE ROBOTS Y\n"
      "NADIE PUEDE ENTRAR.\n"
      "SIN ELLA NO HAY LUZ."), NULL },
    { E_PNJ,  2,  8, 3, F_MISION_FUSIBLE, F_FUSIBLE, IT_CHIP, N_("TECNICO: NECESITO UN\n"
      "FUSIBLE GRUESO DE LOS\n"
      "QUE HAY EN LA SUBESTACION.\n"
      "SI ME LO TRAES TE DOY\n"
      "UN CHIP DE DATOS."), N_("TECNICO: EL FUSIBLE!\n"
      "AHORA SI PUEDO ARREGLAR\n"
      "EL TABLERO. TOMA.") },
};

static const char *const M_VOLTIO_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_VOLTIO_INT[] = {
    {  2,  2, PR_MAQUINA },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_VOLTIO_INT[] = {
    { E_PUERTA,  7, 12, S_VOLTIO,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL BANCO DE ALTO VOLTIO.\n"
      "Aca la corriente sobra."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("TENDERA: BATERIAS,\n"
      "ACEITE Y LO DE SIEMPRE.\n"
      "TODO CON GARANTIA DE\n"
      "TRES DIAS."), NULL },
    { E_PNJ, 11,  6, 1, 0, 0, 0, N_("ABUELA TUERCA: PRESTA\n"
      "ATENCION A LA VELOCIDAD.\n"
      "EL QUE PEGA PRIMERO PEGA\n"
      "DOS VECES, PORQUE EL OTRO\n"
      "PUEDE NO LLEGAR A PEGAR.\n"
      "UNAS PIERNAS BUENAS VALEN\n"
      "MAS QUE UN BRAZO CARO."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_SUB1[ROWS] = {
    "CCCCCC:::CCCCCC",
    "Cccccc:::cccccC",
    "CccCCCcccCCCccC",
    "CccCccccccccCcc",
    "CccCcccccccCccC",
    "CccCCCcccCCCccC",
    "CcccccccccccccC",
    "CCCCCcccccCCCCC",
    "CcccccccccccccC",
    "CccCCCCCCCCCccC",
    "CcccccccccccccC",
    "Cccccc:::cccccC",
    "Cccccc:::cccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_SUB1[] = {
    {  4,  3, PR_SERVIDOR },
    {  9,  3, PR_SERVIDOR },
};

static const ch_ent_t EN_SUB1[] = {
    { E_PUERTA,  6, 13, S_VOLTIO,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_SUB2,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  3, IT_ACEITE2, 2, F_COFRE_V2, 0, NULL, NULL },
    { E_COFRE, 12, 10, IT_FUSIBLE, 1, F_FUSIBLE, 0, NULL, NULL },
    { E_ENEMIGO,  2,  6, 0, F_ENEMIGO_20, 19, 0, NULL, NULL },
    { E_ENEMIGO, 12,  6, 0, F_ENEMIGO_21, 19, 0, NULL, NULL },
    { E_CARTEL,  7,  8, 0, 0, 0, 0, N_("TABLERO GENERAL.\n"
      "NO OPERAR SIN GUANTES.\n"
      "(ALGUIEN TACHO LO DE\n"
      "LOS GUANTES.)"), NULL },
};

static const char *const M_SUB2[ROWS] = {
    "CCCCCC:::CCCCCC",
    "Cccccc:::cccccC",
    "CcccccccccccccC",
    "CCCCCCCcCCCCCCC",
    "CcccccccccccccC",
    "CccCCCCcCCCCccC",
    "CccCccccccccccC",
    "CccCcCCCCCCCccC",
    "CccCcCcccccCccC",
    "CccCcCcCCCcCccC",
    "CccccCcccCcCccC",
    "Cccccc:::cCcccC",
    "Cccccc:::cccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_SUB2[] = {
    {  1,  2, PR_SERVIDOR },
    { 12,  2, PR_SERVIDOR },
};

static const ch_ent_t EN_SUB2[] = {
    { E_PUERTA,  6, 13, S_SUB1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE3,  7, 11, 3, NULL, NULL },
    { E_COFRE,  8,  8, IT_SOLDADOR, 1, F_COFRE_V3, 0, NULL, NULL },
    { E_ENEMIGO,  2,  6, 0, F_ENEMIGO_22, 20, 0, NULL, NULL },
    { E_ENEMIGO, 12,  6, 0, F_ENEMIGO_23, 21, 0, NULL, NULL },
};

static const char *const M_JEFE3[ROWS] = {
    "CCCCCCCCCCCCCCC",
    "Cc:::::::::::cC",
    "Cc:ccccccccc:cC",
    "Cc:c:::::::c:cC",
    "Cc:c:ccccc:c:cC",
    "Cc:c:c:::c:c:cC",
    "Cc:c:c:::c:c:cC",
    "Cc:c:c:::c:c:cC",
    "Cc:c:ccccc:c:cC",
    "Cc:c:::::::c:cC",
    "Cc:ccccccccc:cC",
    "Cc:::::::::::cC",
    "Ccccc:::ccccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_JEFE3[] = {
    {  1,  1, PR_SERVIDOR },
    { 12,  1, PR_SERVIDOR },
};

static const ch_ent_t EN_JEFE3[] = {
    { E_PUERTA,  6, 13, S_SUB2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  6, 11, F_JEFE_VOLTIO, 24, IT_PASE, N_("INGENIERA JEFA:\n"
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
    "###############",
    "##dd#######dd##",
    "##dd#######dd##",
    "GGGGGGG###GGGGG",
    "GGGGGGG###GGGGG",
    "GGGGGGGGGGGGGGG",
    "LLLLLGGGGGGLLLL",
    "LLLLLGGGGGGLLLL",
    "GGGGGGGGGGGGGGG",
    "GGGGG###GGGGGGG",
    "##dd####dd#####",
    "##dd####dd#####",
    "##dddddddd#####",
    "###############",
};

static const ch_prop_t P_HUMO[] = {
    {  2,  1, PR_PILA },
    { 11,  1, PR_PILA },
    {  2, 10, PR_MAQUINA },
};

static const ch_ent_t EN_HUMO[] = {
    { E_PUERTA,  0,  3, S_VOLTIO_E, 11,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  3, S_FUNDICION,  2,  6, 3, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("VALLE DEL HUMO.\n"
      "NO SE DETENGA.\n"
      "EL AIRE NO ES BUENO\n"
      "PARA NADIE."), NULL },
    { E_COFRE, 12,  8, IT_ACEITE2, 2, F_COFRE_F1, 0, NULL, NULL },
    { E_ENEMIGO,  4,  5, 0, F_ENEMIGO_24, 22, 0, NULL, NULL },
    { E_ENEMIGO, 12,  5, 0, F_ENEMIGO_25, 23, 0, NULL, NULL },
    { E_ENEMIGO,  7, 11, 0, F_ENEMIGO_26, 23, 0, NULL, NULL },
};

static const char *const M_FUNDICION[ROWS] = {
    "######GGG######",
    "#rrrrrGGGrrrrr#",
    "#rrrrrGGGrrrrr#",
    "#rrrrrGGGrrrrr#",
    "#rrrrrGGGrrrrr#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#rrrrrGGGrrrrr#",
    "#rrrrrGGGrrrrr#",
    "#rr#######rrrr#",
    "#rr#LLLLL#rrrr#",
    "#rr#LLLLL#rrrr#",
    "###############",
};

static const ch_prop_t P_FUNDICION[] = {
    {  1,  1, PR_HORNO },
    { 10,  1, PR_TALLER },
    {  1,  8, PR_CASA },
    { 10,  8, PR_HORNO },
};

static const ch_ent_t EN_FUNDICION[] = {
    { E_PUERTA,  0,  5, S_HUMO, 12,  4, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_HORNO1,  7, 11, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_FUNDICION_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_FUND_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  3,  4, 0, 0, 0, 0, N_("FUNDICION\n"
      "AQUI SE FUNDE TODO LO\n"
      "QUE EL PUERTO OXIDA.\n"
      "AL NORTE: EL HORNO."), NULL },
    { E_PNJ, 13,  4, 3, 0, 0, 0, N_("FUNDIDOR: EL METAL\n"
      "CALIENTE NO PERDONA.\n"
      "SI TU ROBOT ES DE HIELO,\n"
      "ACA LA VAS A PASAR MAL."), NULL },
    { E_PNJ, 12, 11, 2, 0, 0, 0, N_("CHICA: DICEN QUE EN EL\n"
      "HORNO VIVE UN ROBOT\n"
      "HECHO DE LAVA.\n"
      "YO NO LO VI, EH."), NULL },
    { E_PNJ,  2, 12, 4, F_MISION_MOLDE, F_MOLDE, IT_ACEITE2, N_("MOLDEADORA: SE ME QUEDO\n"
      "UN MOLDE ADENTRO DEL\n"
      "HORNO Y NO PIENSO IR.\n"
      "TRAEMELO Y ARREGLAMOS."), N_("MOLDEADORA: EL MOLDE!\n"
      "SOS DE FIERRO, PIBE.\n"
      "TOMA ESTO PARA EL CAMINO.") },
};

static const char *const M_FUND_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_FUND_INT[] = {
    {  2,  2, PR_HORNO },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_FUND_INT[] = {
    { E_PUERTA,  7, 12, S_FUNDICION,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL BANCO DE LA FUNDICION.\n"
      "Se suelda todo de una."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("HERRERO: LO QUE NECESITES\n"
      "PARA AGUANTAR EL CALOR."), NULL },
    { E_PNJ, 11,  6, 1, 0, 0, 0, N_("ABUELA TUERCA: ACORDATE\n"
      "DE MIRAR LOS TIPOS.\n"
      "EL FUEGO DERRITE EL HIELO\n"
      "Y CORROE EL ACIDO, PERO\n"
      "CONTRA EL VOLT NO HACE\n"
      "NADA.\n"
      "MIRA LA FICHA ANTES DE\n"
      "PEGAR."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_HORNO1[ROWS] = {
    "XXXXXX:::XXXXXX",
    "Xrrrrr:::rrrrrX",
    "XrrrrrrrrrrrrrX",
    "XrLLLLrrrLLLLrX",
    "XrLLLLrrrLLLLrX",
    "XrrrrrrrrrrrrrX",
    "XrrrLLLLLLLrrrX",
    "XrrrLLLLLLLrrrX",
    "XrrrrrrrrrrrrrX",
    "XrLLLLrrrLLLLrX",
    "XrrrrrrrrrrrrrX",
    "Xrrrrr:::rrrrrX",
    "Xrrrrr:::rrrrrX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_HORNO1[] = {
    {  1,  5, PR_HORNO },
    { 12,  9, PR_PILA },
};

static const ch_ent_t EN_HORNO1[] = {
    { E_PUERTA,  6, 13, S_FUNDICION,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_HORNO2,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  5, IT_MOLDE, 1, F_MOLDE, 0, NULL, NULL },
    { E_ENEMIGO,  2,  8, 0, F_ENEMIGO_27, 25, 0, NULL, NULL },
    { E_ENEMIGO, 12,  2, 0, F_ENEMIGO_28, 25, 0, NULL, NULL },
};

static const char *const M_HORNO2[ROWS] = {
    "XXXXXX:::XXXXXX",
    "Xrrrrr:::rrrrrX",
    "XrrrrrrrrrrrrrX",
    "XLLLLLLrLLLLLLX",
    "XLLLLLLrLLLLLLX",
    "XrrrrrrrrrrrrrX",
    "XrLLLLrrrLLLLrX",
    "XrLLLLrrrLLLLrX",
    "XrrrrrrrrrrrrrX",
    "XLLLLLLrLLLLLLX",
    "XrrrrrrrrrrrrrX",
    "Xrrrrr:::rrrrrX",
    "Xrrrrr:::rrrrrX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_HORNO2[] = {
    {  1, 10, PR_HORNO },
    { 12,  1, PR_PILA },
};

static const ch_ent_t EN_HORNO2[] = {
    { E_PUERTA,  6, 13, S_HORNO1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE4,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  8, IT_BATERIA2, 2, F_COFRE_F2, 0, NULL, NULL },
    { E_ENEMIGO,  2,  5, 0, F_ENEMIGO_29, 26, 0, NULL, NULL },
    { E_ENEMIGO, 12,  5, 0, F_ENEMIGO_30, 27, 0, NULL, NULL },
};

static const char *const M_JEFE4[ROWS] = {
    "XXXXXXXXXXXXXXX",
    "XrrrrrrrrrrrrrX",
    "XrLLLLLLLLLLLrX",
    "XrLrrrrrrrrrLrX",
    "XrLrrrrrrrrrLrX",
    "XrLrrrrrrrrrLrX",
    "XrLrrrrrrrrrLrX",
    "XrLrrrrrrrrrLrX",
    "XrLrrrrrrrrrLrX",
    "XrLLLLLrLLLLLrX",
    "XrrrrrrrrrrrrrX",
    "Xrrrrr:::rrrrrX",
    "Xrrrrr:::rrrrrX",
    "XXXXXX:::XXXXXX",
};

static const ch_prop_t P_JEFE4[] = {
    {  1,  1, PR_HORNO },
};

static const ch_ent_t EN_JEFE4[] = {
    { E_PUERTA,  6, 13, S_HORNO2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  5, 10, F_JEFE_FUNDICION, 30, IT_PASE, N_("MAESTRO FUNDIDOR:\n"
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
    "###############",
    "##NN#######NN##",
    "##NN#######NN##",
    "##NNN#####NNN##",
    "GGGGGG###GGGGGG",
    "GGGGGG###GGGGGG",
    "GGGGGGGGGGGGGGG",
    "###GGGGGGGGG###",
    "##NNNN###NNNN##",
    "##NNNN###NNNN##",
    "##NNNN###NNNN##",
    "##nnnnnnnnnnn##",
    "##nnnnnnnnnnn##",
    "###############",
};

static const ch_prop_t P_PASO[] = {
    {  2,  1, PR_PINO },
    { 11,  1, PR_PINO },
    {  2,  8, PR_PINO },
    { 11,  8, PR_PINO },
};

static const ch_ent_t EN_PASO[] = {
    { E_PUERTA,  0,  4, S_FUNDICION_E, 11,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  4, S_CRIO,  2,  6, 3, NULL, NULL },
    { E_CARTEL,  6, 11, 0, 0, 0, 0, N_("PASO HELADO.\n"
      "SI SE LE CONGELAN LOS\n"
      "SERVOS, NO INSISTA.\n"
      "ESPERE AL DESHIELO."), NULL },
    { E_COFRE, 12, 11, IT_ACEITE2, 2, F_COFRE_C1, 0, NULL, NULL },
    { E_ENEMIGO,  4,  6, 0, F_ENEMIGO_31, 28, 0, NULL, NULL },
    { E_ENEMIGO, 10,  6, 0, F_ENEMIGO_32, 29, 0, NULL, NULL },
    { E_ENEMIGO,  7,  7, 0, F_ENEMIGO_33, 29, 0, NULL, NULL },
};

static const char *const M_CRIO[ROWS] = {
    "######GGG######",
    "#nnnnnGGGnnnnn#",
    "#nnnnnGGGnnnnn#",
    "#nnnnnGGGnnnnn#",
    "#nnnnnGGGnnnnn#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#nnnhhhhhhhnnn#",
    "#nnnhhhhhhhnnn#",
    "#nnnhhhhhhhnnn#",
    "#nnnnnnnnnnnnn#",
    "#nnnnnnnnnnnnn#",
    "###############",
};

static const ch_prop_t P_CRIO[] = {
    {  1,  1, PR_CASA },
    { 10,  1, PR_TALLER },
    {  1, 10, PR_PINO },
    { 12, 10, PR_PINO },
};

static const ch_ent_t EN_CRIO[] = {
    { E_PUERTA,  0,  5, S_PASO, 12,  5, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_CUEVA1,  7,  9, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_CRIO_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_CRIO_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("CRIOVALLE\n"
      "TEMPERATURA MEDIA: -12.\n"
      "LOS CIRCUITOS DURAN MAS\n"
      "PERO LAS BATERIAS MENOS."), NULL },
    { E_PNJ,  4, 12, 2, 0, 0, 0, N_("CHICO: EN LA CUEVA EL\n"
      "PISO ES DE HIELO PURO.\n"
      "MI PAPA DICE QUE ADENTRO\n"
      "HAY ALGO QUE NO SE\n"
      "DERRITE NUNCA."), NULL },
    { E_PNJ, 13,  4, 3, 0, 0, 0, N_("LENADOR: LOS BICHOS DE\n"
      "ACA PEGAN CON CRIO.\n"
      "SI LLEVAS ALGO DE FUEGO\n"
      "LA VAS A PASAR MEJOR."), NULL },
    { E_PNJ,  2,  8, 4, F_MISION_TERMO, F_TERMO, IT_SOLDADOR, N_("ABUELA DEL VALLE: SE ME\n"
      "QUEDO EL TERMO EN LA\n"
      "CUEVA Y SIN EL NO HAY\n"
      "MATE.\n"
      "TRAEMELO, SI?"), N_("ABUELA DEL VALLE: MI\n"
      "TERMO! GRACIAS, HIJO.\n"
      "TOMA ESTE SOLDADOR QUE\n"
      "ERA DE MI MARIDO.") },
};

static const char *const M_CRIO_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_CRIO_INT[] = {
    {  2,  2, PR_MAQUINA },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_CRIO_INT[] = {
    { E_PUERTA,  7, 12, S_CRIO,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL BANCO DE CRIOVALLE.\n"
      "Hay que soplarle el hielo\n"
      "antes de usarlo."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("TENDERO: ACEITE QUE NO\n"
      "SE CONGELA. LO DEMAS SI."), NULL },
    { E_PNJ, 11,  6, 1, 0, 0, 0, N_("ABUELA TUERCA: TE VOY A\n"
      "DECIR LO ULTIMO Y DESPUES\n"
      "TE DEJO EN PAZ.\n"
      "NO TE ENAMORES DE UNA\n"
      "PIEZA. LA MEJOR CABEZA\n"
      "DEL MUNDO NO SIRVE SI EL\n"
      "TORSO NO LE DA ENERGIA.\n"
      "MIRA EL CONJUNTO."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_CUEVA1[ROWS] = {
    "######:::######",
    "#nnnnn:::nnnnn#",
    "#nnnnnnnnnnnnn#",
    "#nRRnnnnnnnRRn#",
    "#nRRnnhhhnnRRn#",
    "#nnnnnhhhnnnnn#",
    "#nnnnnhhhnnnnn#",
    "#nRRnnnnnnnRRn#",
    "#nRRnnnnnnnRRn#",
    "#nnnnnnnnnnnnn#",
    "#nnnRRRnRRRnnn#",
    "#nnnnn:::nnnnn#",
    "#nnnnn:::nnnnn#",
    "######:::######",
};

static const ch_prop_t P_CUEVA1[] = {
    {  1,  3, PR_PINO },
    { 12,  7, PR_PINO },
};

static const ch_ent_t EN_CUEVA1[] = {
    { E_PUERTA,  6, 13, S_CRIO,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_CUEVA2,  7, 10, 3, NULL, NULL },
    { E_COFRE,  7,  5, IT_TERMO, 1, F_TERMO, 0, NULL, NULL },
    { E_ENEMIGO,  4,  6, 0, F_ENEMIGO_34, 31, 0, NULL, NULL },
    { E_ENEMIGO, 10,  9, 0, F_ENEMIGO_35, 31, 0, NULL, NULL },
};

static const char *const M_CUEVA2[ROWS] = {
    "######:::######",
    "#nnnnn:::nnnnn#",
    "#nnnnnnnnnnnnn#",
    "#RRRRRnnnRRRRR#",
    "#nnnnnnnnnnnnn#",
    "#nhhhhnnnhhhhn#",
    "#nhhhhnnnhhhhn#",
    "#nhhhhnnnhhhhn#",
    "#nnnnnnnnnnnnn#",
    "#RRRnRRRRRnRRR#",
    "#nnnnnnnnnnnnn#",
    "#nnnnn:::nnnnn#",
    "#nnnnn:::nnnnn#",
    "######:::######",
};

static const ch_prop_t P_CUEVA2[] = {
    {  1,  5, PR_PINO },
};

static const ch_ent_t EN_CUEVA2[] = {
    { E_PUERTA,  6, 13, S_CUEVA1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE5,  7, 11, 3, NULL, NULL },
    { E_COFRE,  3,  6, IT_BATERIA2, 2, F_COFRE_C2, 0, NULL, NULL },
    { E_COFRE, 11,  6, IT_CHIP, 1, F_COFRE_C3, 0, NULL, NULL },
    { E_ENEMIGO,  7,  8, 0, F_ENEMIGO_36, 32, 0, NULL, NULL },
    { E_ENEMIGO,  4, 10, 0, F_ENEMIGO_37, 33, 0, NULL, NULL },
};

static const char *const M_JEFE5[ROWS] = {
    "###############",
    "#nnnnnnnnnnnnn#",
    "#nhhhhhhhhhhhn#",
    "#nhnnnnnnnnnhn#",
    "#nhnhhhhhhhnhn#",
    "#nhnhnnnnnhnhn#",
    "#nhnhnnnnnhnhn#",
    "#nhnhnnnnnhnhn#",
    "#nhnhhhhhhhnhn#",
    "#nhnnnnnnnnnhn#",
    "#nhhhhhhhhhhhn#",
    "#nnnnn:::nnnnn#",
    "#nnnnn:::nnnnn#",
    "######:::######",
};

static const ch_prop_t P_JEFE5[] = {
    {  1,  1, PR_PINO },
    { 12,  1, PR_PINO },
};

static const ch_ent_t EN_JEFE5[] = {
    { E_PUERTA,  6, 13, S_CUEVA2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  6, 13, F_JEFE_CRIO, 36, IT_PASE, N_("GUARDABOSQUE:\n"
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
    "PPPPPPPPPPPPPPP",
    "PddPddPddPddPdP",
    "PddPddPddPddPdP",
    "PPPPPPPPPPPPPPP",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "PPPPPPPPPPPPPPP",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "PPPPPPPPPPPPPPP",
    "PddPddPddPddPdP",
    "PddPddPddPddPdP",
    "PddPddPddPddPdP",
    "PPPPPPPPPPPPPPP",
};

static const ch_prop_t P_AUTOPISTA[] = {
    {  1,  1, PR_TORRE },
    { 10,  1, PR_TORRE },
    {  1, 10, PR_SERVIDOR },
    { 10, 10, PR_SERVIDOR },
};

static const ch_ent_t EN_AUTOPISTA[] = {
    { E_PUERTA,  0,  4, S_CRIO_E, 11,  6, 2, NULL, NULL },
    { E_PUERTA, 14,  4, S_MALLA,  2,  6, 2, NULL, NULL },
    { E_CARTEL,  6, 11, 0, 0, 0, 0, N_("AUTOPISTA A MALLA.\n"
      "VELOCIDAD MAXIMA: LA QUE\n"
      "TE DEN LAS PIERNAS."), NULL },
    { E_COFRE, 13, 11, IT_ACEITE2, 2, F_COFRE_M1, 0, NULL, NULL },
    { E_ENEMIGO,  4,  4, 0, F_ENEMIGO_38, 34, 0, NULL, NULL },
    { E_ENEMIGO, 10,  5, 0, F_ENEMIGO_39, 35, 0, NULL, NULL },
    { E_ENEMIGO,  7,  8, 0, F_ENEMIGO_40, 35, 0, NULL, NULL },
};

static const char *const M_MALLA[ROWS] = {
    "######ppp######",
    "#pppppppppppppp",
    "#pp#pp#pp#pp#p#",
    "#pp#pp#pp#pp#p#",
    "#ppppppppppppp#",
    "ppppppppppppppp",
    "ppppppppppppppp",
    "ppppppppppppppp",
    "#ppppppppppppp#",
    "#pp#pp#pp#pp#p#",
    "#pp#pp#pp#pp#p#",
    "#ppppppppppppp#",
    "#ppppppppppppp#",
    "###############",
};

static const ch_prop_t P_MALLA[] = {
    {  1,  1, PR_SERVIDOR },
    { 10,  1, PR_TALLER },
    {  1,  9, PR_SERVIDOR },
    { 13,  9, PR_FAROLA },
};

static const ch_ent_t EN_MALLA[] = {
    { E_PUERTA,  0,  5, S_AUTOPISTA, 12,  5, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_SERV1,  7, 11, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_MALLA_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_MALLA_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("CIUDAD MALLA\n"
      "AQUI NO SE FABRICA NADA.\n"
      "SOLO SE PIENSA."), NULL },
    { E_PNJ,  4, 11, 2, 0, 0, 0, N_("CHICA: TODO EL PUEBLO\n"
      "CORRE SOBRE EL SERVIDOR\n"
      "DE ABAJO.\n"
      "SI SE APAGA, SE APAGA\n"
      "TODO."), NULL },
    { E_PNJ, 13,  4, 3, 0, 0, 0, N_("PROGRAMADOR: LOS BICHOS\n"
      "DE ACA PEGAN CON PLASMA.\n"
      "EL PLASMA NO PUEDE CON\n"
      "EL CRIO. PENSALO."), NULL },
    { E_PNJ,  3,  8, 4, F_MISION_CLAVE, F_CLAVE, IT_CHIP, N_("ARCHIVISTA: PERDI LA\n"
      "CLAVE MAESTRA EN EL\n"
      "SERVIDOR.\n"
      "SIN ELLA NO PUEDO ENTRAR\n"
      "A MI PROPIA OFICINA."), N_("ARCHIVISTA: LA CLAVE!\n"
      "TOMA UN CHIP, TE LO\n"
      "GANASTE.") },
};

static const char *const M_MALLA_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_MALLA_INT[] = {
    {  2,  2, PR_SERVIDOR },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_MALLA_INT[] = {
    { E_PUERTA,  7, 12, S_MALLA,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL BANCO DE MALLA.\n"
      "Te lo diagnostica solo."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("TENDERA: TODO CARO, TODO\n"
      "BUENO. Es una ciudad."), NULL },
    { E_PNJ, 11,  6, 3, 0, 0, 0, N_("TECNICO: DEJAME VER TU\n"
      "ROBOT... MIRA, LE SOBRA\n"
      "ATAQUE Y LE FALTA VIDA.\n"
      "UN TORSO GRANDE TE\n"
      "SALVARIA MAS DE UN\n"
      "COMBATE."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_SERV1[ROWS] = {
    "CCCCCC:::CCCCCC",
    "Cccccc:::cccccC",
    "CcccccccccccccC",
    "CCCcCCCcCCCcCCC",
    "CcccccccccccccC",
    "CcCCCcCCCcCCCcC",
    "CcccccccccccccC",
    "CCCcCCCcCCCcCCC",
    "CcccccccccccccC",
    "CcCCCcCCCcCCCcC",
    "CcccccccccccccC",
    "Cccccc:::cccccC",
    "Cccccc:::cccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_SERV1[] = {
    {  1,  4, PR_SERVIDOR },
    { 12,  8, PR_SERVIDOR },
};

static const ch_ent_t EN_SERV1[] = {
    { E_PUERTA,  6, 13, S_MALLA,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_SERV2,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  6, IT_CLAVE, 1, F_CLAVE, 0, NULL, NULL },
    { E_ENEMIGO,  3,  8, 0, F_ENEMIGO_41, 37, 0, NULL, NULL },
    { E_ENEMIGO, 11,  4, 0, F_ENEMIGO_42, 37, 0, NULL, NULL },
};

static const char *const M_SERV2[ROWS] = {
    "CCCCCC:::CCCCCC",
    "Cccccc:::cccccC",
    "CcccccccccccccC",
    "CcCCCCCCCCCCCcC",
    "CcCcccccccccCcC",
    "CcCcCCCCCCCcCcC",
    "CcCcCcccccCcCcC",
    "CcCcCcCCCcCcCcC",
    "CcCcCccccccCccC",
    "CcCcCCCCCCCcCcC",
    "CcCcccccccccCcC",
    "CcCCCC:::CCCCcC",
    "Cccccc:::cccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_SERV2[] = {
    { 12,  1, PR_SERVIDOR },
};

static const ch_ent_t EN_SERV2[] = {
    { E_PUERTA,  6, 13, S_SERV1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE6,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  6, IT_SOLDADOR, 2, F_COFRE_M2, 0, NULL, NULL },
    { E_COFRE,  2,  6, IT_BATERIA2, 2, F_COFRE_M3, 0, NULL, NULL },
    { E_ENEMIGO, 12,  6, 0, F_ENEMIGO_43, 38, 0, NULL, NULL },
    { E_ENEMIGO, 10, 10, 0, F_ENEMIGO_44, 39, 0, NULL, NULL },
};

static const char *const M_JEFE6[ROWS] = {
    "CCCCCCCCCCCCCCC",
    "CcccccccccccccC",
    "CccCCCCCCCCCccC",
    "CccCcccccccCccC",
    "CccCc:::::cCccC",
    "CccCc:ccc:cCccC",
    "CccCc:ccc:cCccC",
    "CccCc:ccc:cCccC",
    "CccCc:::::cCccC",
    "CccCcccccccCccC",
    "CccCCCCCCCCCccC",
    "Cccccc:::cccccC",
    "Cccccc:::cccccC",
    "CCCCCC:::CCCCCC",
};

static const ch_prop_t P_JEFE6[] = {
    {  1,  1, PR_SERVIDOR },
    { 12,  1, PR_SERVIDOR },
};

static const ch_ent_t EN_JEFE6[] = {
    { E_PUERTA,  6, 13, S_SERV2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  6, 14, F_JEFE_MALLA, 42, IT_PASE, N_("ADMINISTRADORA:\n"
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
    "###############",
    "#dddddddddddd##",
    "#ddRRddddRRdd##",
    "#ddRRddddRRdd##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#ddddRRRRdddd##",
    "#ddddRRRRdddd##",
    "GGGGGGGGGGGGGGG",
    "#ddRRdddddddd##",
    "#ddRRddddRRdd##",
    "#ddddddddRRdd##",
    "#dddddddddddd##",
    "###############",
};

static const ch_prop_t P_LLANURA[] = {
    {  1,  1, PR_PILA },
    { 11,  1, PR_PILA },
    {  1,  9, PR_PILA },
    { 11, 12, PR_TORRE },
};

static const ch_ent_t EN_LLANURA[] = {
    { E_PUERTA,  0,  4, S_MALLA_E, 11,  6, 2, NULL, NULL },
    { E_PUERTA, 14,  4, S_OXIDO,  2,  6, 2, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("EL PARAMO.\n"
      "ACA TERMINA TODO LO QUE\n"
      "NO SIRVE.\n"
      "USTED TAMBIEN, SI SE\n"
      "DESCUIDA."), NULL },
    { E_COFRE, 12,  8, IT_SOLDADOR, 1, F_COFRE_O1, 0, NULL, NULL },
    { E_ENEMIGO,  7,  2, 0, F_ENEMIGO_45, 40, 0, NULL, NULL },
    { E_ENEMIGO,  4,  8, 0, F_ENEMIGO_46, 41, 0, NULL, NULL },
    { E_ENEMIGO,  8, 10, 0, F_ENEMIGO_47, 41, 0, NULL, NULL },
};

static const char *const M_OXIDO[ROWS] = {
    "######GGG######",
    "#QQQQQGGGQQQQQ#",
    "#QQQQQGGGQQQ#Q#",
    "#QQ#QQGGGQQQ#Q#",
    "#QQ#QQGGGQQQQQ#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#QQQQQGGGQ#QQQ#",
    "#QQ#QQGGGQ#QQQ#",
    "#QQ#QQQQQQQQQQ#",
    "#QQQQQQQQQQQQQ#",
    "#QQQQQQQQQQQQQ#",
    "###############",
};

static const ch_prop_t P_OXIDO[] = {
    {  1,  1, PR_CASA },
    { 10,  1, PR_TALLER },
    {  1, 11, PR_PILA },
    { 11, 11, PR_PILA },
};

static const ch_ent_t EN_OXIDO[] = {
    { E_PUERTA,  0,  5, S_LLANURA, 12,  5, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_CEMENT1,  7, 10, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_OXIDO_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_OXIDO_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  6, 12, 0, 0, 0, 0, N_("VILLA OXIDO\n"
      "EL PUEBLO MAS VIEJO Y EL\n"
      "MAS CERCANO A LA TORRE.\n"
      "NADIE SABE CUAL DE LAS\n"
      "DOS COSAS ES PEOR."), NULL },
    { E_PNJ,  4, 12, 3, 0, 0, 0, N_("VIEJO: YO SUBI A LA TORRE\n"
      "UNA VEZ. HACE MUCHO.\n"
      "NO TE VOY A CONTAR COMO\n"
      "ME FUE."), NULL },
    { E_PNJ, 13,  4, 2, 0, 0, 0, N_("CHICO: EN EL CEMENTERIO\n"
      "LOS ROBOTS SE ARMAN SOLOS\n"
      "CON LO QUE ENCUENTRAN.\n"
      "POR ESO SON TAN RAROS."), NULL },
    { E_PNJ,  2,  8, 4, F_MISION_ENGRANAJE, F_ENGRANAJE, IT_BATERIA2, N_("CHATARRERA: BUSCO UN\n"
      "ENGRANAJE GRANDE, DE LOS\n"
      "VIEJOS. EN EL CEMENTERIO\n"
      "TIENE QUE HABER.\n"
      "TE PAGO BIEN."), N_("CHATARRERA: MIRA ESO.\n"
      "HACIA VEINTE ANOS QUE NO\n"
      "VEIA UNO ENTERO.\n"
      "TOMA, TE LO GANASTE.") },
};

static const char *const M_OXIDO_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_OXIDO_INT[] = {
    {  2,  2, PR_MAQUINA },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_OXIDO_INT[] = {
    { E_PUERTA,  7, 12, S_OXIDO,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL BANCO DE VILLA OXIDO.\n"
      "Viejo pero impecable."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("CHATARRERO: LO QUE VES\n"
      "es lo que hay. Alcanza."), NULL },
    { E_PNJ, 11,  6, 1, 0, 0, 0, N_("ABUELA TUERCA: LLEGASTE\n"
      "LEJOS, EH.\n"
      "ARRIBA DE LA TORRE ESTA\n"
      "EL CAMPEON. NO TE VOY A\n"
      "MENTIR: SU ROBOT ES MEJOR\n"
      "QUE EL TUYO.\n"
      "PERO EL NO ARMO EL SUYO.\n"
      "SE LO ARMARON."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_CEMENT1[ROWS] = {
    "######:::######",
    "#QQQQQ:::QQQQQ#",
    "#QQQQQQQQQQQQQ#",
    "#RRQRRQQQRRQRR#",
    "#RRQRRQQQRRQRR#",
    "#QQQQQQQQQQQQQ#",
    "#RRQRRQQQRRQRR#",
    "#RRQRRQQQRRQRR#",
    "#QQQQQQQQQQQQQ#",
    "#RRQRRQQQRRQRR#",
    "#QQQQQQQQQQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "######:::######",
};

static const ch_prop_t P_CEMENT1[] = {
    {  1,  5, PR_PILA },
    { 12,  5, PR_PILA },
    {  1, 10, PR_PILA },
};

static const ch_ent_t EN_CEMENT1[] = {
    { E_PUERTA,  6, 13, S_OXIDO,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_CEMENT2,  7, 10, 3, NULL, NULL },
    { E_COFRE,  7,  5, IT_ENGRANAJE, 1, F_ENGRANAJE, 0, NULL, NULL },
    { E_ENEMIGO,  3,  8, 0, F_ENEMIGO_48, 43, 0, NULL, NULL },
    { E_ENEMIGO, 11,  2, 0, F_ENEMIGO_49, 43, 0, NULL, NULL },
};

static const char *const M_CEMENT2[ROWS] = {
    "######:::######",
    "#QQQQQ:::QQQQQ#",
    "#QQQQQQQQQQQQQ#",
    "#QRRRRRQRRRRRQ#",
    "#QRQQQRQRQQQRQ#",
    "#QRQRQRQRQRQRQ#",
    "#QRQRQQQQQRQRQ#",
    "#QRQRRRRRRRQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRRRRRQRRRRRQ#",
    "#QQQQQQQQQQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "######:::######",
};

static const ch_prop_t P_CEMENT2[] = {
    {  1,  1, PR_PILA },
    { 12,  1, PR_PILA },
};

static const ch_ent_t EN_CEMENT2[] = {
    { E_PUERTA,  6, 13, S_CEMENT1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_JEFE7,  7, 11, 3, NULL, NULL },
    { E_COFRE,  5,  5, IT_CHIP, 2, F_COFRE_O2, 0, NULL, NULL },
    { E_COFRE,  9,  5, IT_ACEITE2, 3, F_COFRE_O3, 0, NULL, NULL },
    { E_ENEMIGO,  7,  6, 0, F_ENEMIGO_50, 44, 0, NULL, NULL },
    { E_ENEMIGO,  3, 10, 0, F_ENEMIGO_51, 45, 0, NULL, NULL },
};

static const char *const M_JEFE7[ROWS] = {
    "###############",
    "#QQQQQQQQQQQQQ#",
    "#QRRRRRRRRRRRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRQQQQQQQQQRQ#",
    "#QRRRRRQRRRRRQ#",
    "#QQQQQQQQQQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "#QQQQQ:::QQQQQ#",
    "######:::######",
};

static const ch_prop_t P_JEFE7[] = {
    {  1,  1, PR_PILA },
    { 12,  1, PR_PILA },
};

static const ch_ent_t EN_JEFE7[] = {
    { E_PUERTA,  6, 13, S_CEMENT2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  5, 12, F_JEFE_PARAMO, 48, IT_PASE, N_("CHATARRERO MAYOR:\n"
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
    "VVVVVVVVVVVVVVV",
    "VddVVVVVVVVVddV",
    "VddVVVVVVVVVddV",
    "VdddVVVVVVVdddV",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "VVVVGGGGGGGVVVV",
    "VddVGGGGGGGVddV",
    "VddVGGGGGGGVddV",
    "VdddGGGGGGGdddV",
    "VdddddddddddddV",
    "VddVVVVVVVVVddV",
    "VddVVVVVVVVVddV",
    "VVVVVVVVVVVVVVV",
};

static const ch_prop_t P_ULTIMO[] = {
    {  1,  1, PR_ESTATUA },
    { 12,  1, PR_ESTATUA },
};

static const ch_ent_t EN_ULTIMO[] = {
    { E_PUERTA,  0,  4, S_OXIDO_E, 11,  6, 2, NULL, NULL },
    { E_PUERTA, 14,  4, S_PRISMA,  2,  6, 2, NULL, NULL },
    { E_CARTEL,  6, 10, 0, 0, 0, 0, N_("ULTIMO TRAMO.\n"
      "DE ACA NO SE VUELVE\n"
      "IGUAL QUE COMO SE VINO."), NULL },
    { E_COFRE, 12, 10, IT_BATERIA2, 3, F_COFRE_T1, 0, NULL, NULL },
    { E_ENEMIGO,  5,  7, 0, F_ENEMIGO_52, 46, 0, NULL, NULL },
    { E_ENEMIGO,  9,  7, 0, F_ENEMIGO_53, 47, 0, NULL, NULL },
};

static const char *const M_PRISMA[ROWS] = {
    "######GGG######",
    "#pppppGGGppppp#",
    "#pppppGGGppppp#",
    "#pp#ppGGGppppp#",
    "#pp#ppGGGppppp#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#pp#ppppppp#pp#",
    "#pp#ppppppp#pp#",
    "#ppppppppppppp#",
    "#pppppVVVppppp#",
    "#pppppVVVppppp#",
    "###############",
};

static const ch_prop_t P_PRISMA[] = {
    {  1,  1, PR_ESTATUA },
    { 10,  1, PR_TALLER },
    {  1,  9, PR_ESTATUA },
};

static const ch_ent_t EN_PRISMA[] = {
    { E_PUERTA,  0,  5, S_ULTIMO, 12,  5, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_TORRE1,  7, 11, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_PRISMA_E,  2,  6, 3, NULL, NULL },
    { E_PUERTA, 11,  4, S_PRISMA_INT,  7, 10, 2, NULL, NULL },
    { E_CARTEL,  4, 10, 0, 0, 0, 0, N_("PRISMA\n"
      "AL NORTE: LA TORRE.\n"
      "SUBEN MUCHOS. BAJAN\n"
      "BASTANTES MENOS."), NULL },
    { E_PNJ, 12, 10, 2, 0, 0, 0, N_("CHICO: MI HERMANA SUBIO\n"
      "AYER. TODAVIA NO BAJO.\n"
      "DICE MAMA QUE NO ME\n"
      "PREOCUPE."), NULL },
    { E_PNJ,  2,  8, 3, 0, 0, 0, N_("GUARDIA: DOS PISOS Y LA\n"
      "CUMBRE.\n"
      "REPARA ANTES DE SUBIR.\n"
      "ARRIBA NO HAY BANCO."), NULL },
};

static const char *const M_PRISMA_INT[ROWS] = {
    "000000000000000",
    "0|||||||||||||0",
    "0|+++++++++++|0",
    "0|---------++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|++ooooooo++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0|+++++++++++|0",
    "0||||||+++||||0",
    "000000000000000",
};

static const ch_prop_t P_PRISMA_INT[] = {
    {  2,  2, PR_SERVIDOR },
    {  8,  8, PR_MAQUINA },
};

static const ch_ent_t EN_PRISMA_INT[] = {
    { E_PUERTA,  7, 12, S_PRISMA,  7,  2, 3, NULL, NULL },
    { E_TALLER,  8,  9, 0, 0, 0, 0, N_("EL ULTIMO BANCO ANTES\n"
      "DE LA TORRE.\n"
      "Usalo bien."), NULL },
    { E_TIENDA,  6,  4, 0, 0, 0, 0, N_("TENDERO: LLEVA TODO LO\n"
      "QUE PUEDAS PAGAR.\n"
      "Arriba no se compra nada."), NULL },
    { E_PNJ, 11,  6, 1, 0, 0, 0, N_("ABUELA TUERCA: BUENO.\n"
      "HASTA ACA TE ACOMPANO.\n"
      "ACORDATE DE UNA COSA\n"
      "SOLA: EL ROBOT QUE VAS A\n"
      "PELEAR ES EL QUE VOS\n"
      "ARMASTE.\n"
      "NINGUNA PIEZA TE LA\n"
      "REGALARON.\n"
      "ANDA."), NULL },
    { E_MUEBLE, 11,  2, MU_ESTANTE, 0, 0, 0, N_("UNA ESTANTERIA DE\n"
      "REPUESTOS.\n"
      "TODO ETIQUETADO A MANO."), NULL },
    { E_MUEBLE,  2,  6, MU_PLANTA, 0, 0, 0, N_("UNA PLANTA EN UN RINCON.\n"
      "SOBREVIVE AL ACEITE."), NULL },
    { E_MUEBLE,  7,  2, MU_CUADRO, 0, 0, 0, N_("UN PLANO DE UN ROBOT,\n"
      "CLAVADO A LA PARED.\n"
      "TIENE CORRECCIONES DE\n"
      "TRES LETRAS DISTINTAS."), NULL },
    { E_MUEBLE, 12, 10, MU_CESTO, 0, 0, 0, NULL, NULL },
    { E_MUEBLE,  2, 10, MU_VASIJA, 0, 0, 0, N_("UNA VASIJA CON RETAZOS\n"
      "DE CHAPA."), NULL },
};

static const char *const M_TORRE1[ROWS] = {
    "VVVVVV:::VVVVVV",
    "Vccccc:::cccccV",
    "VcccccccccccccV",
    "VcVVVVVVVVVVVcV",
    "VcVcccccccccVcV",
    "VcVcVVVVVVVcVcV",
    "VcVcVcccccVcVcV",
    "VcVcVcVVVcVcVcV",
    "VcVcVcccccVcVcV",
    "VcVcVVVVVVVcVcV",
    "VcVcccccccccVcV",
    "VcVVVV:::VVVVcV",
    "Vccccc:::cccccV",
    "VVVVVV:::VVVVVV",
};

static const ch_prop_t P_TORRE1[] = {
    {  1,  1, PR_SERVIDOR },
};

static const ch_ent_t EN_TORRE1[] = {
    { E_PUERTA,  6, 13, S_PRISMA,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_TORRE2,  7, 10, 3, NULL, NULL },
    { E_COFRE,  7,  8, IT_SOLDADOR, 2, F_COFRE_T2, 0, NULL, NULL },
    { E_ENEMIGO,  2,  6, 0, F_ENEMIGO_54, 49, 0, NULL, NULL },
    { E_ENEMIGO, 12,  6, 0, F_ENEMIGO_55, 50, 0, NULL, NULL },
};

static const char *const M_TORRE2[ROWS] = {
    "VVVVVV:::VVVVVV",
    "Vccccc:::cccccV",
    "VcccccccccccccV",
    "VVVVVcccccVVVVV",
    "VcccccccccccccV",
    "VcVVVVVcVVVVVcV",
    "VcVcccccccccVcV",
    "VcVcVVVcVVVcVcV",
    "VcVcccccccccVcV",
    "VcVVVVVcVVVVVcV",
    "VcccccccccccccV",
    "Vccccc:::cccccV",
    "Vccccc:::cccccV",
    "VVVVVV:::VVVVVV",
};

static const ch_prop_t P_TORRE2[] = {
    { 12,  1, PR_SERVIDOR },
};

static const ch_ent_t EN_TORRE2[] = {
    { E_PUERTA,  6, 13, S_TORRE1,  7,  2, 3, NULL, NULL },
    { E_PUERTA,  6,  0, S_CUMBRE,  7, 11, 3, NULL, NULL },
    { E_COFRE,  7,  6, IT_BATERIA2, 3, F_COFRE_T3, 0, NULL, NULL },
    { E_ENEMIGO,  2,  6, 0, F_ENEMIGO_56, 51, 0, NULL, NULL },
    { E_ENEMIGO, 12,  6, 0, F_ENEMIGO_57, 52, 0, NULL, NULL },
};

static const char *const M_CUMBRE[ROWS] = {
    "VVVVVVVVVVVVVVV",
    "VcccccccccccccV",
    "VccVVVVVVVVVccV",
    "VccVcccccccVccV",
    "VccVc:::::cVccV",
    "VccVc:ccc:cVccV",
    "VccVc:ccc:cVccV",
    "VccVc:ccc:cVccV",
    "VccVc:::::cVccV",
    "VccVcccccccVccV",
    "VccVVVVVVVVVccV",
    "Vccccc:::cccccV",
    "Vccccc:::cccccV",
    "VVVVVV:::VVVVVV",
};

static const ch_prop_t P_CUMBRE[] = {
    {  1,  1, PR_ESTATUA },
    { 12,  1, PR_ESTATUA },
};

static const ch_ent_t EN_CUMBRE[] = {
    { E_PUERTA,  6, 13, S_TORRE2,  7,  2, 3, NULL, NULL },
    { E_JEFE,  7,  6, 16, F_JEFE_PRISMA, 55, IT_PASE, N_("CAMPEON:\n"
      "TE VI SUBIR PISO POR\n"
      "PISO. NO SALTEASTE\n"
      "NINGUNO.\n"
      "ESO YA TE HACE DISTINTO\n"
      "A CASI TODOS.\n"
      "AHORA VEAMOS EL ROBOT."), NULL },
    { E_PNJ,  2,  6, 3, F_FINAL, F_JEFE_PRISMA, 0, N_("GUARDIANA DE LA CUMBRE:\n"
      "ESE SILLON DE METAL ES\n"
      "DEL CAMPEON.\n"
      "TODAVIA NO ES TUYO."), N_("GUARDIANA: SENTATE, DALE.\n"
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

/* --------------------------------------------------------------------------
 * THE EAST SECTORS of the towns of zones 3 to 8
 *
 * Same reason as the harbour: a road crossing, a workshop, three neighbours,
 * the booth and the checkpoint out do not fit in 210 cells without going
 * back to the cramming that v2 exists to stop.
 * -------------------------------------------------------------------------- */

static const char *const M_VOLTIO_E[ROWS] = {
    "###############",
    "#ppp##ppp##ppp#",
    "#ppp##ppp##ppp#",
    "#ppp##ppp##ppp#",
    "#ppppppppppppp#",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#ppppppppppppp#",
    "#ppp##ppp##ppp#",
    "#ppp##ppp##ppp#",
    "#ppppppppppppp#",
    "#ppppppppppppp#",
    "###############",
};

static const ch_prop_t P_VOLTIO_E[] = {
    {  1,  9, PR_TORRE },
    { 11,  9, PR_TORRE },
};

static const ch_ent_t EN_VOLTIO_E[] = {
    { E_PUERTA,  0,  5, S_VOLTIO, 12,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_HUMO,  2,  4, 3, NULL, NULL },
    { E_BLOQUEO, 12,  5, F_JEFE_VOLTIO, 0, 0, 0, N_("CONTROL DE ALTO VOLTIO.\n"
      "AL ESTE BAJA AL VALLE\n"
      "DEL HUMO. CERRADO HASTA\n"
      "QUE ALGUIEN ARREGLE LA\n"
      "SUBESTACION."), N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL VALLE DEL\n"
      "HUMO.") },
    { E_BLOQUEO, 12,  6, F_JEFE_VOLTIO, 0, 0, 0, N_("CONTROL DE ALTO VOLTIO.\n"
      "CERRADO."), N_("CONTROL ABIERTO.") },
    { E_CABINA,  6, 11, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  6, 11, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const char *const M_FUNDICION_E[ROWS] = {
    "###############",
    "#rrrrrrrrrrrr##",
    "#rr######rrrr##",
    "#rr#LLLL#rrrr##",
    "#rr#LLLL#rrrr##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#rr#LLLL#rrrr##",
    "#rr#LLLL#rrrr##",
    "#rr######rrrr##",
    "#rrrrrrrrrrrr##",
    "#rrrrrrrrrrrr##",
    "###############",
};

static const ch_prop_t P_FUNDICION_E[] = {
    {  1, 11, PR_PILA },
    { 10, 11, PR_PILA },
};

static const ch_ent_t EN_FUNDICION_E[] = {
    { E_PUERTA,  0,  5, S_FUNDICION, 12,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_PASO,  2,  5, 3, NULL, NULL },
    { E_BLOQUEO, 12,  5, F_JEFE_FUNDICION, 0, 0, 0, N_("CONTROL DE LA FUNDICION.\n"
      "AL ESTE EMPIEZA EL PASO\n"
      "HELADO. NO SE PASA SIN\n"
      "PASE DE SECTOR."), N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL PASO HELADO.") },
    { E_BLOQUEO, 12,  6, F_JEFE_FUNDICION, 0, 0, 0, N_("CONTROL DE LA FUNDICION.\n"
      "CERRADO."), N_("CONTROL ABIERTO.") },
    { E_CABINA,  6, 12, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  2,  1, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const char *const M_CRIO_E[ROWS] = {
    "###############",
    "#nnnnnnnnnnnn##",
    "#nnnnnnnnnnnn##",
    "#nn#nnnnn#nnn##",
    "#nn#nnnnn#nnn##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#nn#nnnnn#nnn##",
    "#nn#nnnnn#nnn##",
    "#nnnnnnnnnnnn##",
    "#nnnhhhhhnnnn##",
    "#nnnhhhhhnnnn##",
    "###############",
};

static const ch_prop_t P_CRIO_E[] = {
    {  1,  1, PR_PINO },
    { 11,  1, PR_PINO },
};

static const ch_ent_t EN_CRIO_E[] = {
    { E_PUERTA,  0,  5, S_CRIO, 12,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_AUTOPISTA,  2,  5, 3, NULL, NULL },
    { E_BLOQUEO, 12,  5, F_JEFE_CRIO, 0, 0, 0, N_("CONTROL DE CRIOVALLE.\n"
      "AL ESTE BAJA LA AUTOPISTA\n"
      "A CIUDAD MALLA.\n"
      "CERRADO POR NIEVE."), N_("CONTROL ABIERTO.\n"
      "AL ESTE: LA AUTOPISTA.") },
    { E_BLOQUEO, 12,  6, F_JEFE_CRIO, 0, 0, 0, N_("CONTROL DE CRIOVALLE.\n"
      "CERRADO POR NIEVE."), N_("CONTROL ABIERTO.") },
    { E_CABINA,  6,  9, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  2, 12, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const char *const M_MALLA_E[ROWS] = {
    "###############",
    "#pppppppppppp##",
    "#pp#ppppp#pp###",
    "#pp#ppppp#pp###",
    "#pppppppppppp##",
    "ppppppppppppppp",
    "ppppppppppppppp",
    "ppppppppppppppp",
    "#pppppppppppp##",
    "#pp#ppppp#pp###",
    "#pp#ppppp#pp###",
    "#pppppppppppp##",
    "#pppppppppppp##",
    "###############",
};

static const ch_prop_t P_MALLA_E[] = {
    {  1,  1, PR_FAROLA },
    { 10,  9, PR_ESTATUA },
};

static const ch_ent_t EN_MALLA_E[] = {
    { E_PUERTA,  0,  5, S_MALLA, 12,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_LLANURA,  2,  5, 3, NULL, NULL },
    { E_BLOQUEO, 12,  5, F_JEFE_MALLA, 0, 0, 0, N_("CONTROL DE MALLA.\n"
      "AL ESTE EMPIEZA EL\n"
      "PARAMO. NO SE PASA SIN\n"
      "PASE DE SECTOR."), N_("CONTROL ABIERTO.\n"
      "AL ESTE: EL PARAMO.") },
    { E_BLOQUEO, 12,  6, F_JEFE_MALLA, 0, 0, 0, N_("CONTROL DE MALLA.\n"
      "CERRADO."), N_("CONTROL ABIERTO.") },
    { E_CABINA,  6, 11, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  2, 11, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const char *const M_OXIDO_E[ROWS] = {
    "###############",
    "#QQQQQQQQQQQQ##",
    "#QQ#QQQQQ#QQQ##",
    "#QQ#QQQQQ#QQQ##",
    "#QQQQQQQQQQQQ##",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "GGGGGGGGGGGGGGG",
    "#QQQQQQQQQQQQ##",
    "#QQ#QQRRQQ#QQQ#",
    "#QQ#QQRRQQ#QQQ#",
    "#QQQQQQQQQQQQ##",
    "#QQQQQQQQQQQQ##",
    "###############",
};

static const ch_prop_t P_OXIDO_E[] = {
    {  1,  1, PR_PILA },
    { 12,  1, PR_FAROLA },
};

static const ch_ent_t EN_OXIDO_E[] = {
    { E_PUERTA,  0,  5, S_OXIDO, 12,  6, 3, NULL, NULL },
    { E_PUERTA, 14,  5, S_ULTIMO,  2,  5, 3, NULL, NULL },
    { E_BLOQUEO, 12,  5, F_JEFE_PARAMO, 0, 0, 0, N_("CONTROL DE VILLA OXIDO.\n"
      "AL ESTE SOLO QUEDA LA\n"
      "TORRE PRISMA.\n"
      "NO SE PASA SIN PASE."), N_("CONTROL ABIERTO.\n"
      "AL ESTE: LA TORRE.") },
    { E_BLOQUEO, 12,  6, F_JEFE_PARAMO, 0, 0, 0, N_("CONTROL DE VILLA OXIDO.\n"
      "CERRADO."), N_("CONTROL ABIERTO.") },
    { E_CABINA,  6, 12, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  2, 12, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

static const char *const M_PRISMA_E[ROWS] = {
    "###############",
    "#pppppppppppp##",
    "#pp#ppppp#ppp##",
    "#pp#ppppp#ppp##",
    "#pppppppppppp##",
    "GGGGGGGGGGGGG##",
    "GGGGGGGGGGGGG##",
    "GGGGGGGGGGGGG##",
    "#pppppppppppp##",
    "#pp#ppppp#ppp##",
    "#pp#ppppp#ppp##",
    "#pppVVVVVpppp##",
    "#pppVVVVVpppp##",
    "###############",
};

static const ch_prop_t P_PRISMA_E[] = {
    {  1,  1, PR_FAROLA },
    { 11,  1, PR_FAROLA },
};

static const ch_ent_t EN_PRISMA_E[] = {
    { E_PUERTA,  0,  5, S_PRISMA, 12,  6, 3, NULL, NULL },
    { E_CABINA,  6,  9, 0, 0, 0, 0, NULL, NULL },
    { E_CHATARRERO,  2, 12, 0, 0, 0, 0,
      N_("CHATARRERO: TRAEME LO\n"
      "QUE NO USES.\n"
      "TE LO PAGO POR LO QUE\n"
      "PESA, NO POR LO QUE FUE."), NULL },
};

const ch_room_t ch_salas[] = {
    { N_("TU CASA"),     TEMA_INTERIOR, M_CASA,      NULL,        0,
      EN_CASA,      N(EN_CASA),      1,   0 , AMB_NADA },
    { N_("VILLA TUERCA"), TEMA_PUEBLO,  M_PUEBLO,    P_PUEBLO,    N(P_PUEBLO),
      EN_PUEBLO,    N(EN_PUEBLO),    1,   0 , AMB_NADA },
    { N_("TUERCA NORTE"), TEMA_PUEBLO, M_TUERCA_NE, P_TUERCA_NE, N(P_TUERCA_NE),
      EN_TUERCA_NE, N(EN_TUERCA_NE), 1,   0 , AMB_NADA },
    { N_("PLAZA DE LA FUENTE"), TEMA_PUEBLO, M_TUERCA_SO, P_TUERCA_SO, N(P_TUERCA_SO),
      EN_TUERCA_SO, N(EN_TUERCA_SO), 1,   0 , AMB_NADA },
    { N_("TUERCA ESTE"), TEMA_PUEBLO, M_TUERCA_SE, P_TUERCA_SE, N(P_TUERCA_SE),
      EN_TUERCA_SE, N(EN_TUERCA_SE), 1,   0 , AMB_NADA },
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
    { N_("MUELLES DE BUJIA"), TEMA_PUEBLO, M_PUERTO_E, P_PUERTO_E, N(P_PUERTO_E),
      EN_PUERTO_E,  N(EN_PUERTO_E),  2,   0 , AMB_NADA },
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
    { N_("ALTO VOLTIO ESTE"), TEMA_PUEBLO, M_VOLTIO_E, P_VOLTIO_E, N(P_VOLTIO_E),
      EN_VOLTIO_E, N(EN_VOLTIO_E), 3, 0 , AMB_NADA },
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
    { N_("PLAZA DEL HORNO"), TEMA_CUEVA, M_FUNDICION_E, P_FUNDICION_E, N(P_FUNDICION_E),
      EN_FUNDICION_E, N(EN_FUNDICION_E), 4, 0 , AMB_NADA },
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
    { N_("CRIOVALLE ESTE"), TEMA_PUEBLO, M_CRIO_E, P_CRIO_E, N(P_CRIO_E),
      EN_CRIO_E, N(EN_CRIO_E), 5, 0 , AMB_NADA },
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
    { N_("MALLA ESTE"), TEMA_PUEBLO, M_MALLA_E, P_MALLA_E, N(P_MALLA_E),
      EN_MALLA_E, N(EN_MALLA_E), 6, 0 , AMB_NADA },
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
    { N_("PLAZA DEL OXIDO"), TEMA_PUEBLO, M_OXIDO_E, P_OXIDO_E, N(P_OXIDO_E),
      EN_OXIDO_E, N(EN_OXIDO_E), 7, 0 , AMB_NADA },
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
    { N_("PRISMA ESTE"), TEMA_PUEBLO, M_PRISMA_E, P_PRISMA_E, N(P_PRISMA_E),
      EN_PRISMA_E, N(EN_PRISMA_E), 8, 0 , AMB_NADA },
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
/* The wash each zone puts over its rooms. Villa Tuerca is zero on purpose: it
 * is the light everything else is a departure from, and you cannot tell the
 * ice valley is colder than home if home is tinted too. */
const ch_aire_t ch_aire[ZONAS] = {
    { 0x000000, 0 },    /* 1 Villa Tuerca - the reference                    */
    { 0x2E6E96, 2 },    /* 2 Puerto Bujia - sea air                          */
    { 0x6A2FB5, 2 },    /* 3 Alto Voltio  - the substation hum               */
    { 0xFF6A1E, 2 },    /* 4 Fundicion    - the furnace next door            */
    { 0xAFD4EE, 3 },    /* 5 Criovalle    - cold light, and it reads coldest */
    { 0x10584E, 2 },    /* 6 Ciudad Malla - green screens                    */
    { 0xC06B2E, 2 },    /* 7 Villa Oxido  - dust in the air                  */
    { 0xB072F0, 2 },    /* 8 Prisma       - the summit                       */
};

/* LA PIEZA DE LA FERIA, una sola vez.
 *
 * Vive aca y no en ch_feria.c por lo mismo que el regalo de prueba: el enum de
 * banderas es privado de este archivo.
 *
 * Devuelve FE_PREMIO_DADA si la entrego, FE_PREMIO_LLENA si no habia lugar y
 * FE_PREMIO_NADA si no correspondia. La diferencia entre las dos primeras es
 * el aviso: la mochila llena se comia el premio en silencio y el jugador se
 * iba creyendo que veinte puntos no alcanzaban. Y la bandera NO se prende si
 * no entro, asi que la pieza sigue esperando a que hagas lugar. */
int ch_feria_premio(ch_save_t *s, uint32_t *sem)
{
    if (ch_flag(s, F_FERIA_PIEZA)) return FE_PREMIO_NADA;
    for (int i = 0; i < ch_mochila(s); i++) {
        if (s->piezas[i] != 0xFF) continue;
        s->piezas[i] = (uint8_t)PIEZA_ID(P_TORSO, ch_rnd(sem, PVAR));
        ch_flag_set(s, F_FERIA_PIEZA);
        return FE_PREMIO_DADA;
    }
    return FE_PREMIO_LLENA;
}

/* El truco de cada subjefe, en el orden de las zonas. */
const uint8_t ch_trucos[ZONAS] = {
    TRUCO_REPARA,   /* 1 Guardian del Desguace: se repara y hay que rehacerlo */
    TRUCO_DRENA,    /* 2 Capataz: cada golpe suyo lo cura un poco            */
    TRUCO_CORTO,    /* 3 Ingeniera Jefa: entras en cortocircuito             */
    TRUCO_FURIA,    /* 4 Maestro Fundidor: mientras menos vida, mas pega     */
    TRUCO_ESCUDO,   /* 5 Guardabosque: se blinda cada tres turnos            */
    TRUCO_DOBLE,    /* 6 Administradora: cada tres turnos pega dos veces     */
    TRUCO_QUEMA,    /* 7 Chatarrero Mayor: te deja recalentado de entrada    */
    TRUCO_FURIA,    /* 8 El Campeon: el ultimo no necesita un truco nuevo,
                     *   necesita que el que tiene duela */
};

const ch_zona_t ch_zonas_tab[ZONAS] = {
    /*  name              cleared           first     last       visited      home        x   y */
    { N_("VILLA TUERCA"), F_JEFE_DESGUACE,  S_CASA,   S_JEFE,   F_VISITA_1, S_PUEBLO,    7, 6 },
    { N_("PUERTO BUJIA"), F_JEFE_PUERTO,    S_COSTA,  S_JEFE2,  F_VISITA_2, S_PUERTO,    7, 6 },
    { N_("ALTO VOLTIO"),  F_JEFE_VOLTIO,    S_CUESTA, S_JEFE3,  F_VISITA_3, S_VOLTIO,    7, 6 },
    { N_("FUNDICION"),    F_JEFE_FUNDICION, S_HUMO,   S_JEFE4,  F_VISITA_4, S_FUNDICION, 7, 6 },
    { N_("CRIOVALLE"),    F_JEFE_CRIO,      S_PASO,   S_JEFE5,  F_VISITA_5, S_CRIO,      7, 6 },
    { N_("CIUDAD MALLA"), F_JEFE_MALLA,     S_AUTOPISTA, S_JEFE6, F_VISITA_6, S_MALLA,   7, 6 },
    { N_("VILLA OXIDO"),  F_JEFE_PARAMO,    S_LLANURA, S_JEFE7, F_VISITA_7, S_OXIDO,     7, 6 },
    { N_("PRISMA"),       F_JEFE_PRISMA,    S_ULTIMO, S_CUMBRE, F_VISITA_8, S_PRISMA,    7, 6 },
};
