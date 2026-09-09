/*
 * CHATARRA - turn-based robot RPG
 *
 * The game's model. Nothing here knows LVGL, the HAL or the preferences exist:
 * that all lives in chatarra.c, as in cjump and arkanos. That way the world,
 * the combat and the workshop can be tested without a screen.
 *
 * ---------------------------------------------------------------------------
 * THE RULE THAT ORDERS THE WHOLE DESIGN: .text is the scarce resource
 * ---------------------------------------------------------------------------
 *
 * Dynamic apps take their code from a 48 KB reservation shared by ALL loaded
 * apps (components/aos_dynapp/aos_dynapp.c). The .rodata, by contrast, is sent
 * by the loader to PSRAM, which is 8 MB. In other words:
 *
 *     one line of code costs         ~8 bytes of a 48 KB pool
 *     one kilobyte of table costs     nothing
 *
 * Three decisions follow from that, repeated across every file:
 *
 *   1. The 64 robot parts are NOT 64 sprites or 64 functions. They are 64
 *      DESCRIPTORS of a handful of bytes and four drawing functions that
 *      interpret them (ch_parts.c). The colour goes in as an argument, like
 *      cjump's costumes.
 *   2. The maps, the dialogue, the items and the attacks are const tables.
 *      Adding a whole town adds not one line of code.
 *   3. Whatever is drawn the same is drawn by the same function. A sign, a
 *      chest and an NPC go through the same blit.
 *
 * ---------------------------------------------------------------------------
 * THE SCREEN
 * ---------------------------------------------------------------------------
 *
 * The buffer is 184x224 and is upscaled x2 to the board's 368x448. Inside:
 *
 *     y   0..175   the map: 23 x 22 cells of 8 px   (real   0..351)
 *     y 176..223   the HUD                          (real 352..447)
 *
 * The cut at 176 is not aesthetic: the board's touch panel does NOT reliably
 * report anything below a real y=354 (see the CST816 trap in
 * docs/HANDOFF-APPS.md). Everything touchable lives above that line and the
 * HUD is read-only. That is why the menu opens with the PHYSICAL BUTTON or by
 * touching the robot itself, and not with a button at the bottom.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ch_pixel.h"

/* --------------------------------------------------------------------------
 * Geometry
 * -------------------------------------------------------------------------- */

#define TILE        8
#define COLS        23                  /* 23 * 8 = 184 = CH_W               */
#define ROWS        22                  /* 22 * 8 = 176                      */
#define MAP_H       (ROWS * TILE)       /* 176                               */
#define HUD_Y       MAP_H
#define HUD_H       (CH_H - MAP_H)      /* 48 px that cannot be touched      */

#define NCELLS      (COLS * ROWS)

/* Steps of 2 px: one cell is four frames, ~133 ms at 30 fps. */
#define WALK_STEP   2

/* The dialogue panel. It lives here and not in ch_ui.c because the map has to
 * know where it starts so as not to draw the robot ON TOP of the text. */
#define DLG_Y       112

/* --------------------------------------------------------------------------
 * Elemental types
 *
 * Six, with a 36-byte effectiveness table in eighths (8 = normal, 16 = double,
 * 4 = half). In eighths and not in percent so the damage calculation is a
 * shift and not a division.
 * -------------------------------------------------------------------------- */

enum {
    TIPO_IMPACTO = 0,
    TIPO_PLASMA,
    TIPO_FUEGO,
    TIPO_CRIO,
    TIPO_VOLT,
    TIPO_ACIDO,
    TIPOS
};

extern const char *const ch_tipo_nombre[TIPOS];
extern const uint32_t    ch_tipo_color[TIPOS];   /* 0xRRGGBB */
/* multiplier in eighths of attack 'a' against defender 'd' */
uint8_t ch_efectividad(int a, int d);

/* --------------------------------------------------------------------------
 * Attacks
 * -------------------------------------------------------------------------- */

enum {
    EF_NADA = 0,
    EF_BAJA_DEF,        /* -1 stage of the opponent's defence                */
    EF_BAJA_ATK,
    EF_SUBE_ATK,        /* +1 own stage                                      */
    EF_SUBE_DEF,
    EF_QUEMA,           /* damage per turn                                   */
    EF_CORTO,           /* loses the turn sometimes (short circuit)          */
    EF_DRENA,           /* returns half the damage as health                 */
    EF_CARGA,           /* recovers energy                                   */
    EF_REPARA,          /* heals half of maximum health                      */
};

typedef struct {
    const char *nombre;         /* N_(): translated when drawn               */
    uint8_t     tipo;
    uint8_t     poder;          /* 0 = status attack                         */
    uint8_t     costo;          /* energy                                    */
    uint8_t     precision;      /* percentage                                */
    uint8_t     efecto;
    uint8_t     prob;           /* chance of the effect, in percent          */
} ch_move_t;

#define MOVES 44
extern const ch_move_t ch_moves[MOVES];

/* --------------------------------------------------------------------------
 * Parts
 *
 * Four categories of sixteen. Each part contributes stats, a type and up to
 * two attacks; the ROBOT is the sum of its four parts, so there are 65,536
 * combinations and not one of them is written down anywhere.
 *
 * 'estilo' is the number the drawing functions in ch_parts.c interpret. It is
 * not an index into a sprite: it is the shape descriptor.
 * -------------------------------------------------------------------------- */

enum { P_CABEZA = 0, P_TORSO, P_BRAZOS, P_PIERNAS, P_CATS };

#define PVAR    16                      /* variants per category             */
#define PIEZAS  (P_CATS * PVAR)

/* A part identifier is category * PVAR + variant. */
#define PIEZA_ID(cat, var)  ((uint8_t)((cat) * PVAR + (var)))
#define PIEZA_CAT(id)       ((id) / PVAR)
#define PIEZA_VAR(id)       ((id) % PVAR)

typedef struct {
    const char *nombre;         /* N_()                                      */
    uint8_t     vida;           /* contribution to maximum health            */
    uint8_t     atk;
    uint8_t     def;
    uint8_t     vel;
    uint8_t     energia;
    uint8_t     tipo;
    uint8_t     nivel;          /* 1..8: which zone it starts appearing in   */
    uint8_t     estilo;         /* shape drawn by ch_parts.c                 */
    uint8_t     mov_a, mov_b;   /* attacks it contributes (0 = none)         */
} ch_part_t;

extern const ch_part_t ch_partes[PIEZAS];

/* --------------------------------------------------------------------------
 * Colour schemes
 *
 * A part's drawing carries no colour inside: it receives one of these. That is
 * what lets the same sixteen shapes give hundreds of different robots.
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t claro, medio, oscuro, brillo;
} ch_skin_t;

#define SKINS 12
extern const ch_skin_t ch_skins[SKINS];

/* --------------------------------------------------------------------------
 * A robot
 * -------------------------------------------------------------------------- */

typedef struct {
    uint8_t  pieza[P_CATS];     /* variant of each category                  */
    uint8_t  skin;
    uint8_t  nivel;
    uint32_t exp;

    int16_t  vida, vida_max;
    int16_t  ene,  ene_max;

    /* derived, recomputed with ch_robot_stats() */
    int16_t  atk, def, vel;
    uint8_t  mov[4];            /* available attacks (index into ch_moves)   */
    uint8_t  nmov;
    uint8_t  tipo;              /* the dominant one, the torso's             */
} ch_robot_t;

void ch_robot_stats(ch_robot_t *r);         /* recomputes everything derived */
void ch_robot_curar(ch_robot_t *r);         /* health and energy to maximum  */
uint32_t ch_exp_nivel(int nivel);           /* exp accumulated for that level */
const char *ch_robot_nombre(const ch_robot_t *r);   /* the torso's          */

/* Drawing. 'esc' is 1 or 2: the same robot for combat and for the data card. */
void ch_robot_draw(ch_buf_t *b, int cx, int y, const ch_robot_t *r,
                   int esc, bool mirando_izq, int pose);
void ch_robot_box(int esc, int *w, int *h);
/* Draws ONE loose part. The register uses it; combat draws the whole robot. A
 * negative 'skin' draws it as a silhouette, which is how the ones you have not
 * come across yet are shown. */
void ch_part_draw(ch_buf_t *b, int cat, int var, int cx, int cy, int esc,
                  int skin);

/* The little figure on the map: 12x14, with the head and legs you are wearing.
 * It is not the combat robot shrunk, it is a separate drawing. */
#define MINI_W  12
#define MINI_H  16
void ch_mini_draw(ch_buf_t *b, int x, int y, const ch_robot_t *r,
                  int dir, int paso);

/* Generates an opponent of level 'nv' from the seed, with parts whose zone
 * requirement does not exceed 'zona'. */
void ch_robot_random(ch_robot_t *r, uint32_t *rng, int nv, int zona);

/* --------------------------------------------------------------------------
 * Items
 * -------------------------------------------------------------------------- */

enum {
    IT_NADA = 0,
    IT_ACEITE,          /* +40 health                                        */
    IT_ACEITE2,         /* +120                                              */
    IT_BATERIA,         /* +30 energy                                        */
    IT_BATERIA2,
    IT_SOLDADOR,        /* revives with half health                          */
    IT_CHIP,            /* +200 exp                                          */
    IT_IMAN,            /* raises the chance of tearing a part off the rival */
    IT_LLAVE,           /* opens the boss's door                             */
    IT_PASE,            /* story key                                         */
    IT_TORNILLOS,       /* the neighbour's errand                            */
    IT_ANCLA,           /* the harbour captain's errand                       */
    IT_FUSIBLE,         /* the Alto Voltio technician's errand                */
    IT_MOLDE,           /* the Fundicion moulder's errand                     */
    IT_TERMO,           /* the Criovalle grandmother's errand                 */
    IT_CLAVE,           /* the Malla archivist's errand                       */
    IT_ENGRANAJE,       /* the Paramo scrap dealer's errand                   */
    ITEMS
};

typedef struct {
    const char *nombre;         /* N_()                                      */
    const char *desc;           /* N_()                                      */
    uint16_t    precio;         /* 0 = not for sale                          */
    uint8_t     combate;        /* usable in combat                          */
    uint8_t     valor;
} ch_item_t;

extern const ch_item_t ch_items[ITEMS];

/* --------------------------------------------------------------------------
 * The world
 * -------------------------------------------------------------------------- */

/* Entity types. The entity is what you touch; the ground and the decorations
 * are part of the background and are not touched. */
enum {
    E_NADA = 0,
    E_PNJ,              /* p1 = appearance, p2 = flag, p3 = alternative dialogue */
    E_CARTEL,
    E_COFRE,            /* p1 = item, p2 = quantity, p3 = flag               */
    E_PUERTA,           /* p1 = room, p2/p3 = destination cell               */
    E_ENEMIGO,          /* p1 = zone, p2 = flag, p3 = level                  */
    E_JEFE,             /* the same, but fixed and with dialogue             */
    E_TALLER,           /* repairs for free                                  */
    E_TIENDA,           /* p1 = stock list                                   */
    E_ROCA,             /* p1 = flag: it stands aside if you have the upgrade */
    E_BLOQUEO,          /* p1 = flag that opens it, p2 = item required       */
};

/* On the three parameters and the two texts:
 *
 * An NPC with an errand needs to say TWO different things -the request and the
 * thanks- and to know when to move from one to the other. That is two pointers
 * and two flags, that is, four fields of a const table: free. Solving it with
 * code -a function per character, or a quest state machine- would cost .text,
 * which is the only thing in short supply. That is why this game's quests are
 * flags and texts, and not logic.
 *
 *   E_PNJ    p1 appearance, p2 own flag, p3 REQUIRED flag (0 = none)
 *   E_COFRE  p1 item,       p2 quantity, p3 flag
 *   E_PUERTA p1 room,       p2/p3 destination cell,  premio = WIDTH in cells
 *   E_ENEMIGO p1 unused, p2 defeated flag, p3 level
 *   E_JEFE    p1 TORSO+1 (0 = random), p2 flag, p3 level, premio = item
 *   E_BLOQUEO p1 flag that opens it; texto closed, texto2 open
 *
 * The door's WIDTH is not a luxury. With touch as the only control, a door IS
 * a target you have to aim at, and a one-cell door against the top edge is
 * 16x16 real pixels against the edge of the glass: in the simulator the mouse
 * hits it every time and with a finger you never do. The first town's exit was
 * exactly that. With the width, the door is described in ONE table row instead
 * of repeating the entity for every cell.
 */
typedef struct {
    uint8_t     tipo;
    uint8_t     x, y;           /* in cells                                  */
    uint8_t     p1, p2, p3;
    uint8_t     premio;         /* item handed over when it is resolved      */
    const char *texto;          /* N_(), or NULL                             */
    const char *texto2;         /* what it says AFTERWARDS, or NULL          */
} ch_ent_t;

/* The decoration catalogue. It lives in the header because both files use it:
 * ch_world.c draws it and ch_zonas.c places it. ALWAYS append at the end. */
enum { PR_ARBOL = 0, PR_CASA, PR_TALLER, PR_FUENTE, PR_CARTEL, PR_FAROLA,
       PR_MAQUINA, PR_PILA,
       PR_PINO, PR_TORRE, PR_ESTATUA, PR_SERVIDOR, PR_HORNO, PR_BARCO,
       PR_PROPS };

/* A decoration: a large sprite drawn into the background, with its solid box. */
typedef struct {
    uint8_t x, y;               /* cell of the top-left corner               */
    uint8_t sprite;
} ch_prop_t;

enum { TEMA_PUEBLO = 0, TEMA_INTERIOR, TEMA_DUNGEON, TEMA_CUEVA, TEMAS };

/* Ambience: the particles falling in the room. It is a NEW field at the end of
 * the structure on purpose, so rooms that do not set it stay at 0 (no
 * ambience) without having to touch all 51 rows. */
enum { AMB_NADA = 0, AMB_NIEVE, AMB_BRASAS, AMB_POLVO, AMB_GOTERAS };

typedef struct {
    const char        *nombre;      /* N_()                                  */
    uint8_t            tema;
    const char *const *suelo;       /* ROWS rows of COLS characters          */
    const ch_prop_t   *props;
    uint8_t            nprops;
    const ch_ent_t    *ents;
    uint8_t            nents;
    uint8_t            zona;        /* level of the creatures that appear    */
    uint8_t            encuentros;  /* 0..255: probability per cell          */
    uint8_t            ambiente;    /* AMB_*, 0 = none                       */
} ch_room_t;

extern const ch_room_t ch_salas[];
extern const uint8_t   ch_nsalas;

/* The eight zones, for the world map. It is a table and not a computation over
 * ch_salas because the distribution of rooms into zones is a design decision,
 * not a consequence of the enum's order. */
typedef struct {
    const char *nombre;         /* N_()                                      */
    uint8_t     bandera;        /* the sub-boss's: set = zone cleared          */
    uint8_t     sala0, sala1;   /* range of rooms, to know where you are      */
} ch_zona_t;

#define ZONAS 8
extern const ch_zona_t ch_zonas_tab[ZONAS];

/* Querying the ground and the decorations: used by the path finder. */
bool ch_tile_solido(char t);
bool ch_tile_encuentro(char t);

/* Drawing the world. The cells' patterns and the decorations' sprites are
 * private to ch_world.c: the map asks for them to be drawn and does not know
 * what they look like. The cell receives its coordinate because the pattern
 * varies with it -so the grass is not stamped- and that variation has to be
 * DETERMINISTIC: the background is repainted by rectangles and grass drawn at
 * random on the fly would give a different picture every time. */
void ch_tile_draw(ch_buf_t *b, char t, int tx, int ty);
void ch_prop_draw(ch_buf_t *b, const ch_prop_t *pr);
/* An entity ALWAYS draws its own thing; the decorations are pure ornament and
 * cannot land on an entity's cell. 'hecho' is the chest already opened or the
 * character with their business settled. */
void ch_ent_draw(ch_buf_t *b, const ch_room_t *r, const ch_ent_t *e, bool hecho);
/* Height in cells of the decoration, and whether it blocks that cell. */
bool ch_prop_solido(const ch_room_t *r, int tx, int ty);

/* --------------------------------------------------------------------------
 * Save state
 * -------------------------------------------------------------------------- */

#define BANDERAS    256                 /* chests, NPCs, bosses, quests      */
#define MOCHILA     12                  /* stored parts                      */

/* --------------------------------------------------------------------------
 * THE SAVE FILE CANNOT DEPEND ON THE ENUMS
 * --------------------------------------------------------------------------
 *
 * The arrays in the saved structure have a FIXED size, larger than what the
 * game uses today. That is not paranoia: it is the fix for a bug that reached
 * the board.
 *
 * This used to say `uint8_t obj[ITEMS]`, with ITEMS coming from the items
 * enum. On adding the six errand items for zones 2 to 7, ITEMS went from 11 to
 * 17 and the array grew SIX BYTES IN THE MIDDLE of the structure, shifting
 * everything that came after. The loader -which copies the old save verbatim
 * and pads the end with zeros- then read the first bytes of `piezas[]`, which
 * is initialised with 0xFF, as if they were item quantities: six errands with
 * 255 units each, in a freshly started game.
 *
 * The only thing that stops that happening again is the array's size not
 * depending on how many items the game has. With these caps, adding an item, a
 * part in the bag or a flag moves not a single offset.
 *
 * What STILL holds: new fields go at the end, never in the middle.
 * -------------------------------------------------------------------------- */

#define CH_MAX_OBJ      32              /* cap of the saved array            */
#define CH_MAX_MOCHILA  16
#define CH_MAX_PIEZAS   64

typedef struct {
    ch_robot_t yo;
    uint8_t    sala;
    uint8_t    x, y;                    /* cell                              */
    uint8_t    dir;                     /* 0 down, 1 up, 2 left, 3 right     */
    uint16_t   creditos;
    uint8_t    obj[CH_MAX_OBJ];         /* how many of each item             */
    uint8_t    piezas[CH_MAX_MOCHILA];  /* loose parts (id or 0xFF)          */
    uint8_t    bandera[BANDERAS / 8];
    uint16_t   victorias;
    uint32_t   pasos;
    uint8_t    visto[CH_MAX_PIEZAS / 8];/* parts you have ever seen          */
} ch_save_t;

/* If they are ever exceeded, the compiler says so HERE and not the board later. */
_Static_assert(ITEMS   <= CH_MAX_OBJ,     "ITEMS no entra en el guardado");
_Static_assert(MOCHILA <= CH_MAX_MOCHILA, "MOCHILA no entra en el guardado");
_Static_assert(PIEZAS  <= CH_MAX_PIEZAS,  "PIEZAS no entra en el guardado");

static inline bool ch_visto(const ch_save_t *s, int id)
{
    return id >= 0 && id < PIEZAS && (s->visto[id >> 3] & (1u << (id & 7)));
}
static inline void ch_ver(ch_save_t *s, int id)
{
    if (id >= 0 && id < PIEZAS) s->visto[id >> 3] |= (uint8_t)(1u << (id & 7));
}
/* Marks a robot's four parts: called on coming across an opponent. */
void ch_robot_visto(ch_save_t *s, const ch_robot_t *r);

static inline bool ch_flag(const ch_save_t *s, int f)
{
    return f > 0 && f < BANDERAS && (s->bandera[f >> 3] & (1u << (f & 7)));
}
static inline void ch_flag_set(ch_save_t *s, int f)
{
    if (f > 0 && f < BANDERAS) s->bandera[f >> 3] |= (uint8_t)(1u << (f & 7));
}

/* --------------------------------------------------------------------------
 * Modes
 *
 * Only one active. Each one's drawing is in its own file and they all write
 * into the same two buffers.
 * -------------------------------------------------------------------------- */

enum {
    MODO_MAPA = 0,
    MODO_DIALOGO,
    MODO_MENU,
    MODO_TALLER,
    MODO_OBJETOS,
    MODO_FICHA,
    MODO_TIENDA,
    MODO_REGISTRO,
    MODO_MAPAMUNDI,
    MODO_AYUDA,
    MODO_FINAL,
    MODO_COMBATE,
    MODO_TITULO,
};

/* --------------------------------------------------------------------------
 * Combat
 * -------------------------------------------------------------------------- */

enum {
    CB_ENTRAR = 0,      /* entrance animation                                */
    CB_MENU,            /* the player chooses                                */
    CB_ATAQUES,
    CB_OBJETOS,
    CB_MENSAJE,         /* showing text, waiting for the touch               */
    CB_ACCION,          /* resolving a turn                                  */
    CB_FIN,
};

typedef struct {
    ch_robot_t rival;
    uint8_t    fase;
    uint8_t    turno;           /* 0 player, 1 opponent                      */
    uint8_t    jefe;
    uint8_t    zona;
    int8_t     et_atk[2], et_def[2];    /* stages from -6 to +6              */
    uint8_t    quema[2], corto[2];
    uint8_t    sel;             /* chosen option                             */
    uint8_t    pend;            /* what to do when the message closes        */
    uint8_t    mov_j, mov_r;
    uint8_t    huir;
    uint8_t    origen;          /* the room's creature that started it, 0xFF if
                                   it was a random encounter                 */
    uint8_t    premio_pieza;    /* part that can be torn off, 0xFF if not    */
    uint16_t   premio_exp;
    uint16_t   premio_cred;
    uint8_t    critico;         /* the last hit was critical                 */
    uint8_t    entrada;         /* frames of the opening curtain             */
    uint8_t    sacude;          /* frames of shake for whoever is hit        */
    uint8_t    sacude_quien;
    uint8_t    flash;

    /* --- The hit animation ---------------------------------------------
     * It runs WHILE the message panel is being read, so it does not lengthen
     * the turn by a single frame: the time was already there, waiting for the
     * touch. That is why it can be slotted in without touching the combat's
     * state machine.
     *
     * The health bars and the numbers came out of the BACKGROUND and are now
     * drawn per frame. Every point of health used to force a rebuild of the
     * whole background; now the background carries the EMPTY bar and the fill
     * is an 84x4 dirty rectangle. */
    uint8_t    anim;            /* frames remaining, 0 = still               */
    uint8_t    anim_tipo;       /* elemental type of the hit                 */
    uint8_t    anim_dir;        /* 0 = from the player to the opponent       */
    uint8_t    anim_estado;     /* the hit does no damage: another animation */
    int16_t    hp_ver[2];       /* the health the bar SHOWS                  */
    int16_t    dmg_val;         /* floating number                           */
    uint8_t    dmg_t, dmg_quien;
    struct {
        int16_t x, y, vx, vy;   /* in 1/4 of a pixel: no floating point      */
        uint8_t vida;
        uint8_t col;
    } part[14];
    char       linea[3][30];    /* the panel's text, in upper case           */
} ch_batalla_t;

/* --------------------------------------------------------------------------
 * The whole game
 * -------------------------------------------------------------------------- */

#define MAX_MOV     8           /* creatures moving about a room             */
#define RUTA_MAX    64

typedef struct {
    /* buffers */
    ch_buf_t   fb, bg;
    ch_dirty_t d_prev, d_cur, d_push;

    ch_save_t  s;

    uint8_t    modo;
    uint8_t    modo_prev;
    uint8_t    rehacer_fondo;   /* request to repaint the whole background   */
    uint8_t    hud_sucio;
    uint32_t   rng;
    uint32_t   cuadro;

    /* the player's movement */
    int16_t    px, py;          /* pixel, top-left corner of the little figure */
    uint8_t    andando;
    uint8_t    paso;            /* animation                                 */
    uint8_t    ruta[RUTA_MAX];  /* pending directions                        */
    uint8_t    nruta, iruta;
    uint8_t    destino_ent;     /* entity being walked to, 0xFF if none      */

    /* mobile entities of the room (the enemies that patrol) */
    struct {
        uint8_t    idx;         /* index into the room's table               */
        int16_t    px, py;
        uint8_t    x, y, dir, paso, timer;
        uint8_t    casa_x, casa_y;  /* they do not stray from where they were born */
        uint8_t    vivo;
        uint8_t    alerta;      /* it saw you: it comes for you (dungeons only) */
        ch_robot_t bot;         /* generated on entering: the one you see IS
                                   the one you fight, not another rolled later */
    } mov[MAX_MOV];
    uint8_t    nmov;

    /* Ambience particles. SIX and no more: each one pays for a dirty
     * rectangle, and with the player and the room's creatures the list of 24
     * fills up. Past that cap ch_dirty_add merges rectangles far apart from
     * each other and ends up pushing half the screen. */
    struct { int16_t x, y, vx, vy; } amb[6];
    /* Glints on the water and the lava: three at a time, each on a randomly
     * chosen cell. They are not an animated water system -that would force a
     * background repaint- but three dots born and dying on top. */
    struct { uint8_t x, y, t; } brillo[3];
    uint8_t    cofre_t, cofre_x, cofre_y;   /* the chest's glint     */
    uint8_t    trans;           /* frames of the room transition             */

    /* dialogue */
    const char *dlg;
    uint8_t     dlg_pag;
    uint8_t     dlg_pags;
    uint8_t     dlg_ent;        /* entity that triggered it                  */
    uint8_t     dlg_chars;      /* letters of the page revealed              */
    uint8_t     dlg_luego;      /* mode to return to                         */

    /* menus */
    uint8_t    sel;
    uint8_t    sel2;
    uint8_t    scroll;

    /* combat */
    ch_batalla_t bt;
    uint8_t    bt_pendiente;    /* creature+1 to fight when the boss's
                                   dialogue closes; 0 = none                  */

    /* short notices above the HUD */
    char       aviso[26];
    uint8_t    aviso_t;

    /* music: which melody is playing, which note it is on and how long it has left */
    uint8_t    mel_id, mel_i, mel_t;

    /* things the LVGL layer has to deal with */
    uint8_t    quiere_salir;
    uint8_t    quiere_guardar;
    uint8_t    pitido;          /* pending note                              */
    uint16_t   pitido_hz;
} ch_t;

/* --------------------------------------------------------------------------
 * API between files
 * -------------------------------------------------------------------------- */

extern const ch_rect_t ch_entera;
extern const ch_rect_t ch_mapa_rect;

uint32_t ch_rand(uint32_t *rng);
int      ch_rnd(uint32_t *rng, int n);          /* 0..n-1                    */

/* ch_map.c */
void ch_map_entrar(ch_t *g, int sala, int x, int y);
void ch_map_fondo(ch_t *g);                     /* repaints the whole bg     */
void ch_map_dibujar(ch_t *g);                   /* what moves                */
void ch_map_tick(ch_t *g);
void ch_map_toque(ch_t *g, int bx, int by);
void ch_map_interactuar(ch_t *g, int idx);
void ch_map_dialogo_cerrado(ch_t *g);
#ifdef AOS_SIM_BUILTIN
int  ch_map_check(void);        /* world test bench, CH_CHECK=1 */
#endif

/* ch_ui.c */
void ch_ui_hud(ch_t *g);
void ch_ui_fondo(ch_t *g);                      /* rebuilds bg for the mode  */
void ch_ui_dibujar(ch_t *g);                    /* what moves on top         */
bool ch_ui_atras(ch_t *g);                      /* back gesture              */
void ch_ui_toque(ch_t *g, int bx, int by);
void ch_ui_dialogo(ch_t *g, const char *texto, int ent, int luego);
void ch_ui_aviso(ch_t *g, const char *texto);
void ch_ui_menu(ch_t *g);
/* Drawing helpers shared by every mode. */
void ch_panel(ch_buf_t *b, int x, int y, int w, int h, uint16_t borde);
int  ch_wrap(const char *s, int ancho, char dst[][30], int max);
void ch_barra(ch_buf_t *b, int x, int y, int w, int v, int vmax, uint16_t c);

/* ch_battle.c */
void ch_bt_empezar(ch_t *g, const ch_robot_t *rival, int jefe, int zona);
void ch_bt_fondo(ch_t *g);
void ch_bt_dibujar(ch_t *g);
void ch_bt_animar(ch_t *g, int quien, int tipo, int de_estado);
bool ch_bt_atras(ch_t *g);
void ch_bt_tick(ch_t *g);
void ch_bt_toque(ch_t *g, int bx, int by);

/* --------------------------------------------------------------------------
 * Sound
 * -------------------------------------------------------------------------- */

enum {
    CH_MEL_NADA = 0,
    CH_MEL_TITULO,
    CH_MEL_COMBATE,
    CH_MEL_JEFE,
    CH_MEL_VICTORIA,
    CH_MEL_NIVEL,
    CH_MEL_DERROTA,
    CH_MEL_FINAL,
};

void ch_snd_melodia(ch_t *g, int id);
void ch_snd_tick(ch_t *g);

/* implemented by chatarra.c: the game knows neither the HAL nor the preferences */
void ch_sfx(int freq_hz, int ms);       /* effect: plays with sonido >= 1    */
void ch_tono(int freq_hz, int ms);      /* music: plays with sonido == 2     */
int  ch_sonido_get(void);               /* 0 mute, 1 effects, 2 everything   */
void ch_sonido_set(int v);
