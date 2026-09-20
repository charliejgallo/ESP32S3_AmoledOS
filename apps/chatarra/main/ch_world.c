/*
 * CHATARRA - the world
 *
 * Cells, decorations, rooms, entities and dialogue. All of this is .rodata,
 * that is, PSRAM, that is, free: adding a whole town adds not one line of code
 * and not one byte to the 48 KB reservation. It is why the world is described
 * with tables and not with functions.
 *
 * ---------------------------------------------------------------------------
 * THE ROOM
 * ---------------------------------------------------------------------------
 *
 * The world is split into rooms of ONE screen: 23 x 22 cells of 8 px. The
 * camera never moves. That is not a limitation that was accepted: it is what
 * makes the game run at 30 fps on this board. With a moving camera the 165
 * thousand pixels would have to be upscaled and invalidated on every frame
 * -the 15 fps measured in 2043- and the whole dirty-rectangle technique falls
 * apart.
 *
 * A room is three things:
 *
 *   suelo   ROWS rows of COLS characters. Each character is a cell and is
 *           drawn with an 8x8 pattern from the table below.
 *   props   large decorations -trees, houses, machines- painted INTO the
 *           background, occupying several cells.
 *   ents    what can be touched: characters, chests, doors, signs, enemies.
 *
 * ---------------------------------------------------------------------------
 * THE GRASS'S VARIATION HAS TO BE DETERMINISTIC
 * ---------------------------------------------------------------------------
 *
 * The grass and the earth alternate between two patterns according to the
 * cell, so they are not stamped. That choice is made with a function of the
 * coordinate and NOT with the random number generator. The reason is the
 * engine: the background is restored by rectangles every time something moves
 * over it, so grass rolled on the fly would give a different picture on every
 * repaint and the map would "boil" under the player. It is the same reason
 * arkanos's stars are in a table.
 */
#include "chatarra.h"

#include "aos_i18n.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * The cells
 * -------------------------------------------------------------------------- */

#define T_SOLIDO    0x01
#define T_ENCUENTRO 0x02

typedef struct {
    char               c;
    uint8_t            flags;
    const char *const *px;      /* 8 rows of 8 palette characters            */
    const char *const *px2;     /* variant, or NULL                          */
    /* --- edge tiles ---------------------------------------------------
     * `prio` is which ground spills over which, and `borde` is what it looks
     * like spilling. Both are at the END of the structure on purpose: the
     * thirty-eight rows of TILES that say nothing about them get zero and
     * NULL, which reads exactly as "this ground has no fringe and gives way
     * to everything", and that is the right default. */
    uint8_t            prio;
    const char *const *borde;
} tile_t;

static const char *const PX_PASTO[TILE] = {
    "eeeeeeeeeeee", "eeeeeeeEeeee", "eeeeeeeEeeee", "eeEeeeeeeeee",
    "eeEeeeeeeeEe", "eeeeeeeeeeEe", "eeeeeeeeeeee", "eeeeeEeeeeee",
    "eeeeeEeeeeee", "eeeeeeeeeeee", "eEeeeeeeEeee", "eeeeeeeeEeee",
};
static const char *const PX_PASTO2[TILE] = {
    "eeeeeeeeeeee", "eEeeeeeeeeee", "eEeeeeeeeeee", "eeeeeeeeeEee",
    "eeeeeEeeeEee", "eeeeeEeeeeee", "eeeeeeeeeeee", "eeeeeeeeEeee",
    "eeeEeeeeEeee", "eeeEeeeeeeee", "eeeeeeeeeeee", "eeeeeeEeeeee",
};
static const char *const PX_TIERRA[TILE] = {
    "hhhhhhhhhhhh", "hhhHhhhhhhhh", "hhhhhhhhHhhh", "hHhhhhhhhhhh",
    "hhhhhhHhhhhh", "hhhhhhhhhhHh", "hHhhhhhhhhhh", "hhhhHhhhhhhh",
    "hhhhhhhhhHhh", "hhHhhhhhhhhh", "hhhhhhhHhhhh", "hhhhhhhhhhhh",
};
static const char *const PX_TIERRA2[TILE] = {
    "hhhhhhhhhhhh", "hhhhhhhHhhhh", "hhHhhhhhhhhh", "hhhhhhhhhhHh",
    "hhhhHhhhhhhh", "hHhhhhhhhhhh", "hhhhhhhhHhhh", "hhhhhhhhhhhh",
    "hhhHhhhhhHhh", "hhhhhhHhhhhh", "hHhhhhhhhhhh", "hhhhhhhhhhhh",
};
static const char *const PX_ALTO[TILE] = {
    "EEEEEEEEEEEE", "EfEEEfEEEfEE", "EffEEffEEffE", "EffEEffEEffE",
    "fffEfffEfffE", "ffffffffffff", "fFffffFfffFf", "ffffffffffff",
    "ffFfffffFfff", "ffffffffffff", "fFffffFfffFf", "ffffffffffff",
};
static const char *const PX_AGUA[TILE] = {
    "llllllllllll", "lLllllLlllll", "llllllllllll", "LllllllllLll",
    "llllllllllll", "llLllllllllL", "llllllllllll", "lllLllllLlll",
    "llllllllllll", "LllllllllLll", "llllllllllll", "llllLlllllll",
};
static const char *const PX_CERCA[TILE] = {
    "eeeeeeeeeeee", "eeeeeeeeeeee", "eeJeeeeeJeee", "jjjjjjjjjjjj",
    "JJJJJJJJJJJJ", "eeJeeeeeJeee", "eeJeeeeeJeee", "jjjjjjjjjjjj",
    "JJJJJJJJJJJJ", "eeJeeeeeJeee", "eeJeeeeeJeee", "eeeeeeeeeeee",
};
static const char *const PX_PIEDRA[TILE] = {
    "iiiiiiiiiiii", "iiiiiiiiiiii", "iiiiiiiiiiii", "IIIIIIIIIIII",
    "iiiiiIiiiiii", "iiiiiIiiiiii", "iiiiiIiiiiii", "IIIIIIIIIIII",
    "iiIiiiiiiiIi", "iiIiiiiiiiIi", "iiIiiiiiiiIi", "IIIIIIIIIIII",
};
static const char *const PX_LADRILLO[TILE] = {
    "aaaaaaaaaaaa", "aaaaaAaaaaaa", "AAAAAAAAAAAA", "aaAaaaaaaaAa",
    "aaAaaaaaaaAa", "AAAAAAAAAAAA", "aaaaaAaaaaaa", "aaaaaAaaaaaa",
    "AAAAAAAAAAAA", "aaAaaaaaaaAa", "aaAaaaaaaaAa", "AAAAAAAAAAAA",
};
static const char *const PX_MADERA[TILE] = {
    "jjjjjjjjjjjJ", "jjjjjjjjjjjJ", "JJJJJJJJJJJJ", "jjjjjjjjjjjJ",
    "jjjjjjjjjjjJ", "jjjjjjjjjjjJ", "JJJJJJJJJJJJ", "jjjjjjjjjjjJ",
    "jjjjjjjjjjjJ", "jjjjjjjjjjjJ", "JJJJJJJJJJJJ", "jjjjjjjjjjjJ",
};
static const char *const PX_PARED[TILE] = {
    "QQQQQQQQQQQQ", "QQQQQQQQQQQQ", "QQQQQQQQQQQQ", "QQQQQQQQQQQQ",
    "QQQQQQQQQQQQ", "jjjjjjjjjjjj", "JJJJJJJJJJJJ", "QQQQQQQQQQQQ",
    "QQQQQQQQQQQQ", "QQQQQQQQQQQQ", "QQQQQQQQQQQQ", "jjjjjjjjjjjj",
};
static const char *const PX_ALFOMBRA[TILE] = {
    "AAAAAAAAAAAA", "AaAAAAAAaAAA", "AAAAAAAAAAAA", "AAAAaAAAAAAA",
    "AAAAAAAAAAAA", "AaAAAAAAaAAA", "AAAAAAAAAAAA", "AAAAaAAAAAAA",
    "AAAAAAAAAAAA", "AaAAAAAAaAAA", "AAAAAAAAAAAA", "AAAAaAAAAAAA",
};
static const char *const PX_MOSTRADOR[TILE] = {
    "GGGGGGGGGGGG", "GGGGGGGGGGGG", "gggggggggggg", "jjjjjjjjjjjj",
    "jjjjjjjjjjjj", "JJJJJJJJJJJJ", "jjjjjjjjjjjj", "jjjjjjjjjjjj",
    "JJJJJJJJJJJJ", "jjjjjjjjjjjj", "jjjjjjjjjjjj", "JJJJJJJJJJJJ",
};
static const char *const PX_METAL[TILE] = {
    "dddddddddddd", "dgdddddddgdd", "dddddddddddd", "dddddddddddd",
    "dddddddddddd", "dddddddddddd", "dddddddddddd", "dddddddddddd",
    "dgdddddddgdd", "dddddddddddd", "dddddddddddd", "dddddddddddd",
};
static const char *const PX_METAL2[TILE] = {
    "dddddddddddd", "dddddddddddd", "ddDDDDDDDDdd", "ddDddddddDdd",
    "ddDddddddDdd", "ddDddddddDdd", "ddDddddddDdd", "ddDddddddDdd",
    "ddDDDDDDDDdd", "dddddddddddd", "dddddddddddd", "dddddddddddd",
};
static const char *const PX_MURO[TILE] = {
    "xxxxxxxxxxxx", "xKKKKKKKKKKx", "xKddddddddKx", "xKddddddddKx",
    "xKddddddddKx", "xKddddddddKx", "xKddddddddKx", "xKddddddddKx",
    "xKddddddddKx", "xKddddddddKx", "xKKKKKKKKKKx", "xxxxxxxxxxxx",
};
/* Scrap: plating and rust over the metal floor. The pieces are deliberately
 * LARGE. The first version had loose pixels and from two cells away it read as
 * snow, not as broken metal: in an 8x8 pattern that repeats, fine detail
 * disappears and only the texture is left. */
/* ONE piece per cell and not scattered pixels. With the detail spread out,
 * nine cells of scrap in a row read as noise and not as a pile of metal: in an
 * 8x8 pattern that repeats, the fine stuff turns into texture and only what
 * takes up several pixels together survives. */
static const char *const PX_CHATARRA[TILE] = {
    "dddddddddddd", "ddddggDDdddd", "dddgDDggDddd", "dddDggggDddd",
    "ddddDDDDdddd", "dddddUUUdddd", "ddddUUUUUddd", "dddddUUUdddd",
    "dddddddddddd", "ddddddddgddd", "dddddddddddd", "dddddddddddd",
};
/* A puddle has to JOIN UP with the puddle beside it. With the blot centred on
 * the cell and clean edges, three cells in a row read as three links of a
 * chain instead of as one long puddle. */
static const char *const PX_ACEITE[TILE] = {
    "dKKKKKKKKKKd", "KKKKKKKKKKKK", "KKKKKKKKKKKK", "KKKKKPKKKKKK",
    "KKKKPPPKKKKK", "KKKKKPKKKKKK", "KKKKKKKKKKKK", "KKKKKKKKKKKK",
    "KKKKKKKKKKKK", "KKKKKKKKKKKK", "KKKKKKKKKKKK", "dKKKKKKKKKKd",
};
static const char *const PX_ROCA[TILE] = {
    "QQQQQQQQQQQQ", "QQQQIIIIQQQQ", "QQQIiiiiIQQQ", "QQIiiiiiiIQQ",
    "QIiiiiiiiiIQ", "QIiiiiiiiiIQ", "QIiiiiiiiiIQ", "QQIiiiiiiIQQ",
    "QQQIiiiiIQQQ", "QQQQIIIIQQQQ", "QQQQQQQQQQQQ", "QQQQQQQQQQQQ",
};
static const char *const PX_REJILLA[TILE] = {
    "dddddddddddd", "KdKdKdKdKdKd", "dddddddddddd", "KdKdKdKdKdKd",
    "dddddddddddd", "KdKdKdKdKdKd", "dddddddddddd", "KdKdKdKdKdKd",
    "dddddddddddd", "KdKdKdKdKdKd", "dddddddddddd", "KdKdKdKdKdKd",
};
static const char *const PX_BALDOSA[TILE] = {
    "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gGGGGGGGGGGG",
    "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gGGGGGGGGGGG",
    "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gGGGGGGGGGGG", "gggggggggggg",
};
static const char *const PX_NEGRO[TILE] = {
    "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
    "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
    "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
};
static const char *const PX_PUENTE[TILE] = {
    "jjjjjjjjjjjj", "JJJJJJJJJJJJ", "jjjjjjjjjjjj", "jjjjjjjjjjjj",
    "JJJJJJJJJJJJ", "jjjjjjjjjjjj", "jjjjjjjjjjjj", "JJJJJJJJJJJJ",
    "jjjjjjjjjjjj", "jjjjjjjjjjjj", "JJJJJJJJJJJJ", "jjjjjjjjjjjj",
};
static const char *const PX_ARBUSTO[TILE] = {
    "eeeeeeeeeeee", "eeeefffeeeee", "eeefFFFfeeee", "eefFFFFFfeee",
    "efFFfFFFFfee", "efFFFFFFFfee", "efFFFFFFFfee", "eefFFFFFfeee",
    "eeefFFFfeeee", "eeeeFFFeeeee", "eeeeeeeeeeee", "eeeeeeeeeeee",
};

/* --------------------------------------------------------------------------
 * Cells of zones 2 to 8
 *
 * The same rule as the scrap: ONE motif per cell and not loose pixels. Fine
 * detail disappears on repeating and only texture is left.
 * -------------------------------------------------------------------------- */

/* The floor does NOT go pure white. Two reasons: the snow particles are white
 * and over white they disappear -so the whole zone's ambience is lost- and on
 * an AMOLED full white leaves no headroom for a highlight on top. White is
 * kept for what falls. */
static const char *const PX_NIEVE[TILE] = {
    "GGGGGGGGGGGG", "GGwGGGGGGGGG", "GGGGGGGwGGGG", "GwGGGGGGGGGG",
    "GGGGGGwGGGGG", "GGGGGGGGGGwG", "GwGGGGGGGGGG", "GGGGwGGGGGGG",
    "GGGGGGGGGwGG", "GGwGGGGGGGGG", "GGGGGGGwGGGG", "GGGGGGGGGGGG",
};
/* Snowy scrub: the SAME silhouette as the tall grass and the dry scrub. All
 * three hide creatures, so all three have to be recognised the same way. */
static const char *const PX_NEVADO[TILE] = {
    "GGGGGGGGGGGG", "GwGGGwGGGwGG", "GwwGGwwGGwwG", "GwwGGwwGGwwG",
    "wwwGwwwGwwwG", "wwwwwwwwwwww", "wCwwwwCwwwCw", "wwwwwwwwwwww",
    "wwCwwwwwCwww", "wwwwwwwwwwww", "wCwwwwCwwwCw", "wwwwwwwwwwww",
};
static const char *const PX_HIELO[TILE] = {
    "cccccccccccc", "ccCccccccccc", "cCcccCcccccc", "cccccccccCcc",
    "ccccCccccccc", "cCcccccccccc", "cccccCcccccc", "ccccccccCccc",
    "cccccccccccc", "ccCccccccCcc", "cccccccccccc", "cccCcccccccc",
};
static const char *const PX_LAVA[TILE] = {
    "OOOOOOOOOOOO", "OoOOOoOOOoOO", "oooOoooOoooO", "OoooOoooOooo",
    "OOoOOOoOOOoO", "oOOOooOOoOOO", "OOOoOOOOOoOO", "OoOOOOoOOOOo",
    "oooOoooOoooO", "OOoOOOoOOOoO", "OoOOOOOOoOOO", "OOOoOOOoOOOO",
};
static const char *const PX_VOLCAN[TILE] = {
    "SSSSSSSSSSSS", "SsSSSSSSSsSS", "SSSSSUSSSSSS", "SSsSSSSSSSSS",
    "SSSSSSSUSSSS", "SSSSSSSSSSSS", "SsSSSSSSSsSS", "SSSSuSSSSSSS",
    "SSSSSSSSSSSS", "SSSSSSSsSSSS", "SSuSSSSSSSSS", "SSSSSSSSSSSS",
};
static const char *const PX_ARENA[TILE] = {
    "qqqqqqqqqqqq", "qqQqqqqqqqqq", "qqqqqqqQqqqq", "qQqqqqqqqqqq",
    "qqqqqQqqqqqq", "qqqqqqqqqqQq", "qQqqqqqqqqqq", "qqqqQqqqqqqq",
    "qqqqqqqqqQqq", "qqQqqqqqqqqq", "qqqqqqqQqqqq", "qqqqqqqqqqqq",
};
static const char *const PX_MUELLE[TILE] = {
    "jjjJjjjjJjjj", "JJJJJJJJJJJJ", "jjjJjjjjJjjj", "jjjJjjjjJjjj",
    "JJJJJJJJJJJJ", "jjjJjjjjJjjj", "jjjJjjjjJjjj", "JJJJJJJJJJJJ",
    "jjjJjjjjJjjj", "jjjJjjjjJjjj", "JJJJJJJJJJJJ", "jjjJjjjjJjjj",
};
static const char *const PX_VADO[TILE] = {
    "llllllllllll", "lcllllclllll", "llllllllllll", "cllllcllllcl",
    "llllllllllll", "llclllllcccl", "llllllllllll", "lllllcllllll",
    "llccllllcccl", "llllllllllll", "lclllllcllll", "llllllllllll",
};
/* The floor goes VERY dark and the wall BRIGHT. The first version had both at
 * the same contrast and on screen you could not see where the walls were: the
 * room looked like a flat board. In a dungeon that is not an aesthetic detail,
 * it is being unable to play it. */
static const char *const PX_CIRCUITO[TILE] = {
    "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkKkkkkkkk",
    "kkkkKkkkkkkk", "kkkkKkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
    "kkkkkkkkKkkk", "kkkkkkkkKkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
};
/* The track only appears on one cell in four: with the mark on ALL of them the
 * floor was as busy as the wall and the earlier problem came back. */
static const char *const PX_CIRCUITO2[TILE] = {
    "kkkkkkkkkkkk", "kkNkkkkkkkkk", "kkNNNkkkkkkk", "kkkkNkkkkkkk",
    "kkkkNNkkkkkk", "kkkkkNkkkkkk", "kkkkkNNNkkkk", "kkkkkkkNkkkk",
    "kkkkkkkNkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk", "kkkkkkkkkkkk",
};
static const char *const PX_MURO_CIRC[TILE] = {
    "NNNNNNNNNNNN", "NKKKKKKKKKKN", "NKnnnnnnnnKN", "NKnnnnnnnnKN",
    "NKnnnnnnnnKN", "NKnnnnnnnnKN", "NKnnnnnnnnKN", "NKnnnnnnnnKN",
    "NKnnnnnnnnKN", "NKnnnnnnnnKN", "NKKKKKKKKKKN", "NNNNNNNNNNNN",
};
/* Dry scrub. It has the SAME silhouette as the tall grass and not that of a
 * mottled floor, because the two cells do the same thing -they hide creatures-
 * and the player has to recognise them at a glance. The colour changes, not
 * the shape. */
/* Cracked earth of the wasteland. It exists because 'Q' was being used as a
 * cell throughout zone 7 WITHOUT being in the table: buscar() returns the
 * first entry when it does not find the character, so the whole wasteland was
 * drawn as green grass and nobody said a word. */
static const char *const PX_SECO[TILE] = {
    "QQQQQQQQQQQQ", "QQQQQQQQQQQQ", "QQQUQQQQQQQQ", "QQUQQQQQQQQQ",
    "QQQQQQQQQQQQ", "QQQQQQQUQQQQ", "QQQQQUQQQQQQ", "QQQQQQQQQQQQ",
    "QQQQQQQQQUQQ", "QQUQQQQQQQQQ", "QQQQQQQQQQQQ", "QQQQQQQQQQQQ",
};
static const char *const PX_PARAMO[TILE] = {
    "QQQQQQQQQQQQ", "QVQQQVQQQVQQ", "QVVQQVVQQVVQ", "QVVQQVVQQVVQ",
    "VVVQVVVQVVVQ", "VVVVVVVVVVVV", "VEVVVVEVVVEV", "VVVVVVVVVVVV",
    "VVEVVVVVEVVV", "VVVVVVVVVVVV", "VEVVVVEVVVEV", "VVVVVVVVVVVV",
};
static const char *const PX_GRAVA[TILE] = {
    "IIIIIIIIIIII", "IiIIIiIIIiII", "IIIiIIIIIIII", "IiIIIIiIIIiI",
    "IIIIiIIIIIII", "IIiIIIIIIiII", "IIIIIIiIIIII", "IiIIIIIIIIiI",
    "IIIiIIIiIIII", "IIIIIIIIIIII", "IiIIIiIIIIII", "IIIIIiIIIIII",
};
static const char *const PX_CIUDAD[TILE] = {
    "DDDDDDDDDDDD", "DGGGGGGGGGGD", "DGGGGGGGGGGD", "DGGGGGGGGGGD",
    "DGGGGGGGGGGD", "DGGGGGGGGGGD", "DGGGGGGGGGGD", "DGGGGGGGGGGD",
    "DGGGGGGGGGGD", "DGGGGGGGGGGD", "DGGGGGGGGGGD", "DDDDDDDDDDDD",
};
static const char *const PX_MURO_CIU[TILE] = {
    "gggggggggggg", "gGGGGGGGGGGg", "gGDDDDDDDDGg", "gGDDDDDDDDGg",
    "gGDDDDDDDDGg", "gGDDDDDDDDGg", "gGDDDDDDDDGg", "gGDDDDDDDDGg",
    "gGDDDDDDDDGg", "gGDDDDDDDDGg", "gGGGGGGGGGGg", "gggggggggggg",
};
static const char *const PX_CRISTAL[TILE] = {
    "cccccccccccc", "cwcccccccccc", "ccwccccccccc", "cccwcccccccc",
    "ccccwccccccc", "cccccwcccccc", "ccccccwccccc", "cccccccwcccc",
    "ccccccccwccc", "cccccccccwcc", "ccccccccccwc", "cccccccccccc",
};

static const char *const BR_PASTO[TILE] = {
    "eeeeeeeeeeee", "eEeeeeeeeEee", "ee.ee.ee.eee", ".e...e...e..",
    "..e.....e...", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_TIERRA[TILE] = {
    "hhhhhhhhhhhh", "hHhhhhhhhHhh", "hh.hh.hh.hhh", ".h...h...h..",
    "............", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_ARENA[TILE] = {
    "qqqqqqqqqqqq", "qQqqqqqqqQqq", "qq.qq.qq.qqq", ".q...q...q..",
    "..q.....q...", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_NIEVE[TILE] = {
    "GGGGGGGGGGGG", "GwGGGGGGGwGG", "GG.GG.GG.GGG", ".G...G...G..",
    "..G.....G...", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_GRAVA[TILE] = {
    "IIIIIIIIIIII", "IiIIIIIIIiII", "II.II.II.III", ".I...I...I..",
    "............", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_CIUDAD[TILE] = {
    "pppppppppppp", "pPpppppppPpp", "pp.pp.pp.ppp", "............",
    "............", "............", "............", "............",
    "............", "............", "............", "............",
};
static const char *const BR_VOLCAN[TILE] = {
    "SSSSSSSSSSSS", "SsSSSSSSSsSS", "SS.SS.SS.SSS", ".S...S...S..",
    "............", "............", "............", "............",
    "............", "............", "............", "............",
};

static const tile_t TILES[] = {
    { ' ', 0,                       PX_PASTO,     PX_PASTO2,  5, BR_PASTO  },
    { '.', 0,                       PX_TIERRA,    PX_TIERRA2, 3, BR_TIERRA },
    { '"', T_ENCUENTRO,             PX_ALTO,      NULL       },
    { '~', T_SOLIDO,                PX_AGUA,      NULL       },
    { '^', 0,                       PX_PUENTE,    NULL, 9       },
    { '=', T_SOLIDO,                PX_CERCA,     NULL, 9       },
    { '#', T_SOLIDO,                PX_PIEDRA,    NULL, 9       },
    { '%', T_SOLIDO,                PX_LADRILLO,  NULL, 9       },
    { '_', 0,                       PX_MADERA,    NULL, 9       },
    { '|', T_SOLIDO,                PX_PARED,     NULL, 9       },
    { 'o', 0,                       PX_ALFOMBRA,  NULL, 9       },
    { '-', T_SOLIDO,                PX_MOSTRADOR, NULL, 9       },
    { 'M', 0,                       PX_METAL,     PX_METAL2, 9  },
    { 'X', T_SOLIDO,                PX_MURO,      NULL, 9       },
    { 'x', T_ENCUENTRO,             PX_CHATARRA,  NULL       },
    { '*', T_ENCUENTRO,             PX_ACEITE,    NULL       },
    { 'R', T_SOLIDO,                PX_ROCA,      NULL, 9       },
    { ':', 0,                       PX_REJILLA,   NULL, 9       },
    { '+', 0,                       PX_BALDOSA,   NULL, 9       },
    { '0', T_SOLIDO,                PX_NEGRO,     NULL, 9       },
    { 'B', T_SOLIDO,                PX_ARBUSTO,   NULL, 9       },

    /* zones 2 to 8 */
    { 'n', 0,                       PX_NIEVE,     NULL,       5, BR_NIEVE  },
    { 'N', T_ENCUENTRO,             PX_NEVADO,    NULL       },
    { 'h', 0,                       PX_HIELO,     NULL       },
    { 'L', T_SOLIDO,                PX_LAVA,      NULL       },
    { 'r', 0,                       PX_VOLCAN,    NULL,       3, BR_VOLCAN },
    { 'S', 0,                       PX_ARENA,     NULL,       4, BR_ARENA  },
    { 'w', 0,                       PX_MUELLE,    NULL, 9       },
    { 'z', 0,                       PX_VADO,      NULL       },
    { 'c', 0,                       PX_CIRCUITO,  PX_CIRCUITO2 },
    { 'C', T_SOLIDO,                PX_MURO_CIRC, NULL, 9       },
    { 'd', T_ENCUENTRO,             PX_PARAMO,    NULL       },
    { 'Q', 0,                       PX_SECO,      NULL       },
    { 'G', 0,                       PX_GRAVA,     NULL,       3, BR_GRAVA  },
    { 'p', 0,                       PX_CIUDAD,    NULL,       3, BR_CIUDAD },
    { 'P', T_SOLIDO,                PX_MURO_CIU,  NULL, 9       },
    { 'V', T_SOLIDO,                PX_CRISTAL,   NULL, 9       },
};

#define NTILES  ((int)(sizeof(TILES) / sizeof(TILES[0])))

static const tile_t *buscar(char c)
{
    for (int i = 0; i < NTILES; i++) {
        if (TILES[i].c == c) return &TILES[i];
    }
    return &TILES[0];
}

bool ch_tile_solido(char t)
{
    return (buscar(t)->flags & T_SOLIDO) != 0;
}

bool ch_tile_encuentro(char t)
{
    return (buscar(t)->flags & T_ENCUENTRO) != 0;
}

/* --------------------------------------------------------------------------
 * GROUND THAT FLOWS
 *
 * Water and lava are animated by ROLLING their own eight-row pattern, not by
 * drawing anything new: row `fase` becomes the top one and the rest follow,
 * wrapping. For water the speckles drift downwards and it reads as a current;
 * for lava the same motion reads as a slow churn, which is exactly what those
 * two patterns already were, only still.
 *
 * Not one byte of new art, and the tile stays a table.
 * -------------------------------------------------------------------------- */

bool ch_tile_corre(char t)
{
    return t == '~' || t == 'L';
}

void ch_tile_anim(ch_buf_t *b, char t, int tx, int ty, int fase)
{
    const tile_t *d = buscar(t);
    const char *rot[TILE];

    if (!ch_tile_corre(t)) { ch_tile_draw(b, t, tx, ty); return; }
    for (int i = 0; i < TILE; i++) rot[i] = d->px[(i + fase) % TILE];
    ch_blit(b, tx * TILE, ty * TILE, rot, TILE);
}


/* --------------------------------------------------------------------------
 * EDGE TILES: WHERE ONE GROUND SPILLS OVER ANOTHER
 *
 * Until now grass met a path in a perfectly straight step eight pixels long,
 * and a map made of those reads as a spreadsheet with a robot on it. What
 * fixes that is not more tiles: it is that the two terrains OVERLAP by two or
 * three pixels, raggedly, wherever they touch.
 *
 * Two decisions keep it cheap and keep the tables small:
 *
 *   1. ONE pattern per terrain, written for its NORTH edge, and the other
 *      three sides are that pattern TURNED (borde_dibujar below). Twelve
 *      patterns per terrain -four sides, four outer and four inner corners-
 *      is what a full autotiler needs and it is why most of them are
 *      generated rather than drawn. One and a rotation is a tenth of the art
 *      and, at eight pixels, looks the same.
 *
 *   2. It is drawn ONCE, into the background, when the room is built. It is
 *      the same licence the combat arena has: the background is the one place
 *      in this engine where detail costs nothing per frame.
 *
 * Which terrain spills over which is a PRIORITY, not a special case: grass
 * grows over a path, sand lies on stone, snow covers everything. The one that
 * matters most is the lowest - water and lava have priority zero, so whatever
 * surrounds a pond hangs into it, which is what gives a pond a shore instead
 * of a rim.
 * -------------------------------------------------------------------------- */

/* The pattern turned: 0 north (as written), 1 south, 2 west, 3 east. Reading
 * it rotated costs nothing and saves three patterns per terrain. */
static void borde_dibujar(ch_buf_t *b, int tx, int ty,
                          const char *const *fr, int lado)
{
    for (int r = 0; r < TILE; r++) {
        for (int c = 0; c < TILE; c++) {
            char k;
            uint16_t col;
            switch (lado) {
            case 1:  k = fr[TILE - 1 - r][c];  break;   /* south             */
            case 2:  k = fr[c][r];             break;   /* west              */
            case 3:  k = fr[TILE - 1 - c][r];  break;   /* east              */
            default: k = fr[r][c];             break;   /* north             */
            }
            if (ch_pal(k, &col)) ch_px(b, tx * TILE + c, ty * TILE + r, col);
        }
    }
}

void ch_tile_borde(ch_buf_t *b, const ch_room_t *r, int tx, int ty)
{
    static const int8_t DX[4] = {  0,  0, -1, +1 };
    static const int8_t DY[4] = { -1, +1,  0,  0 };
    const tile_t *mio = buscar(r->suelo[ty][tx]);

    for (int lado = 0; lado < 4; lado++) {
        int nx = tx + DX[lado], ny = ty + DY[lado];
        const tile_t *otro;
        if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
        otro = buscar(r->suelo[ny][nx]);
        /* Only a HIGHER ground spills, and only onto a different one: without
         * the second test every tile would fringe itself at the seams of its
         * own variant. */
        if (!otro->borde || otro->prio <= mio->prio) continue;
        borde_dibujar(b, tx, ty, otro->borde, lado);
    }
}

void ch_tile_draw(ch_buf_t *b, char t, int tx, int ty)
{
    const tile_t *d = buscar(t);
    const char *const *px = d->px;

    /* Deterministic on purpose: see the comment above. */
    if (d->px2 && (((tx * 7) ^ (ty * 13)) & 3) == 0) {
        px = d->px2;
    }
    ch_blit(b, tx * TILE, ty * TILE, px, TILE);
}

/* --------------------------------------------------------------------------
 * The decorations
 *
 * A decoration occupies a rectangle 'cw' cells wide by 'rows/8' tall, and only
 * the 'solidas' BOTTOM rows block the way. That way you walk behind a tree's
 * canopy and in front of its trunk, which is what makes a flat map look as if
 * it had depth.
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *const *px;
    uint8_t            rows;    /* height in pixels                          */
    uint8_t            cw;      /* width in cells                            */
    uint8_t            solidas; /* rows of solid cells, from the bottom      */
    /* Which grid the ART is drawn on: 12 once it has been redrawn for v2, 8
     * while it is still v1's and gets stretched. Scaffolding, and the only
     * way the two batches can live in the same table while the redraw is
     * half done. It goes when the last 8 does. */
    uint8_t            grid;
} propdef_t;

static const char *const SP_ARBOL[36] = {
    ".........ffffff.........", ".......ffFFFFFFff.......",
    ".....fFFFFFFFFFFFFf.....", "....fFFFFFFFFFFFFFFf....",
    "...fFFFFFvvFFFFFFFFFf...", "..fFFFFFvvvvFFFFFFFFFf..",
    "..fFFFFvvvvvvFFFFFFFFf..", ".fFFFFFvvvvvvvFFFFFFFFf.",
    ".fFFFFFFvvvvvFFFFFFFFFf.", "fFFFFFFFFvvvFFFFFFFFFFFf",
    "fFFFFFFFFFFFFFFFFFFFFFFf", "fFFFFFFFFFFFFFFFFFFFFFFf",
    "fFFFFFFFFFFFFFFFFFFFFFFf", ".fFFFFFFFFFFFFFFFFFFFFf.",
    ".fFFFFFFFFFFFFFFFFFFFFf.", "..fFFFFFFFFFFFFFFFFFFf..",
    "..fFFFFFFFFFFFFFFFFFFf..", "...fFFFFFFFFFFFFFFFFf...",
    "....fFFFFFFFFFFFFFFf....", ".....ffFFFFFFFFFFff.....",
    ".......ffFFFFFFff.......", ".........ffffff.........",
    "..........jjjj..........", ".........jjJJjj.........",
    ".........jJJJJj.........", ".........jJJJJj.........",
    ".........jJJJJj.........", ".........jJJJJj.........",
    ".........jJJJJj.........", ".........jJJJJj.........",
    "........jjJJJJjj........", ".......jJJJJJJJJj.......",
    "......jJJJJJJJJJJj......", "....EEJJJJJJJJJJJJEE....",
    "..EEEEEEEEEEEEEEEEEEEE..", "........................",
};

static const char *const SP_CASA[36] = {
    "............aaaaaaaaaaaaaaaaaaaa................", ".........aaaaaaaaaaaaaaaaaaaaaaaaaa.............",
    "......aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa..........", "...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.........",
    ".aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.......", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA......",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA......", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA......",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......", ".QQQjjjjjjQQQQQQQQQQQQQQQQjjjjjjQQQQQQQQQ.......",
    ".QQQjccccjQQQQQQQQQQQQQQQQjccccjQQQQQQQQQ.......", ".QQQjccccjQQQQQQQQQQQQQQQQjccccjQQQQQQQQQ.......",
    ".QQQjccccjQQQQQQQQQQQQQQQQjccccjQQQQQQQQQ.......", ".QQQjjjjjjQQQQQQQQQQQQQQQQjjjjjjQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjjjjjjjjQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQjJJyyJJjQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......",
    ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......", ".QQQQQQQQQQQQQQQjJJJJJJjQQQQQQQQQQQQQQQQQ.......",
    ".JJJJJJJJJJJJJJJjJJJJJJjJJJJJJJJJJJJJJJJJ.......", ".JJJJJJJJJJJJJJJjJJJJJJjJJJJJJJJJJJJJJJJJ.......",
    "......EEEEEE.......hhhhhhhh.......EEEEEE........", ".......EEEE.........hhhh...........EEEE.........",
};

static const char *const SP_TALLER[36] = {
    "...DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.........", "..DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD........",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.......", "DDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDD......",
    "DDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDdDDD......", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx......",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx......", ".dddddddddddddddddddddddddddddddddddddddd.......",
    ".dddddddddddddddddddddddddddddddddddddddd.......", ".dddDDDDDDdddddddddddddddddDDDDDDdddddddd.......",
    ".dddDccccDdddddddddddddddddDccccDddddddddd......", ".dddDccccDdddddddddddddddddDccccDddddddddd......",
    ".dddDDDDDDdddddddddddddddddDDDDDDddddddddd......", ".dddddddddddddddddddddddddddddddddddddddd.......",
    ".dddddddddddddddddddddddddddddddddddddddd.......", ".dddddddddddddddddddddddddddddddddddddddd.......",
    ".ddddddddoooooooooooooooooooooddddddddddd.......", ".ddddddddoOOOOOOOOOOOOOOOOOoddddddddddddd.......",
    ".ddddddddoOxxxxxxxxxxxxxxxxxOoddddddddddd.......", ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......",
    ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......", ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......",
    ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......", ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......",
    ".ddddddddoOxDDDDDDDDDDDDDxOoddddddddddddd.......", ".ddddddddoOxxxxxxxxxxxxxxxOoddddddddddddd.......",
    ".ddddddddoOOOOOOOOOOOOOOOOOoddddddddddddd.......", ".ddddddddoooooooooooooooooooddddddddddddd.......",
    ".dddddddddddddddddddddddddddddddddddddddd.......", ".dddddddddddddddddddddddddddddddddddddddd.......",
    ".dddddddddddddddddddddddddddddddddddddddd.......", ".dddddddddddddddddddddddddddddddddddddddd.......",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK.......", ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK.......",
    "......IIIIII.......IIIIIIII.......IIIIII........", ".......IIII.........IIII...........IIII.........",
};

static const char *const SP_FUENTE[22] = {
    ".........IIIIIIIIIIIIIIIII..........", ".......IIiiiiiiiiiiiiiiiiiII........",
    "....IIiiiiiiiiiiiiiiiiiiiiiiiII.....", "..IIiiiiiiiiiiiiiiiiiiiiiiiiiiiII...",
    "..IiiiiillllllllllllllllllliiiiI....", "..IiiilLLLLLLLLLLLLLLLLLLLLLliiI....",
    "..IiilLLLLLLLLLLLLLLLLLLLLLLLliI....", "..IiilLLLLLLLLcccLLLLLLLLLLLLliI....",
    "..IiilLLLLLLLLcccLLLLLLLLLLLLliI....", "..IiilLLLLLLLLcccLLLLLLLLLLLLliI....",
    "..IiilLLLLLLLLcccLLLLLLLLLLLLliI....", "..IiilLLLLLLLLLLLLLLLLLLLLLLLliI....",
    "..IiiilLLLLLLLLLLLLLLLLLLLLliiiI....", "..IiiiiillllllllllllllllllliiiiI....",
    "..IIiiiiiiiiiiiiiiiiiiiiiiiiiiiII...", "....IIiiiiiiiiiiiiiiiiiiiiiiiII.....",
    ".......IIiiiiiiiiiiiiiiiiiII........", ".........IIIIIIIIIIIIIIIII..........",
    "..........IIIIIIIIIIIIIII...........", "...........IIIIIIIIIIIII............",
    "............IIIIIIIIIII.............", "..............sssssss...............",
};

static const char *const SP_CARTEL[14] = {
    "....jjjjjjjjjjjjjj......", "...jGGGGGGGGGGGGGGj.....",
    "...jGkkkkkkkkkkkkGj.....", "...jGkGGGGGGGGGGkGj.....",
    "...jGkGGGGGGGGGGkGj.....", "...jGkkkkkkkkkkkkGj.....",
    "...jGGGGGGGGGGGGGGj.....", "....jjjjjjjjjjjjjj......",
    ".........jjjj...........", ".........jjjj...........",
    ".........jjjj...........", ".........jjjj...........",
    "........JJJJJJ..........", ".......JJJJJJJJ.........",
};

static const char *const SP_FAROLA[24] = {
    "...yyyyyy...", "..yywwwwyy..",
    ".yywwwwwwyy.", "yywwwwwwwwyy",
    "yywwwwwwwwyy", "yywwwwwwwwyy",
    ".yywwwwwwyy.", "..yyyyyyyy..",
    "....dddd....", "....dddd....",
    ".....dd.....", ".....dd.....",
    ".....dd.....", ".....dd.....",
    ".....dd.....", ".....dd.....",
    ".....dd.....", ".....dd.....",
    ".....dd.....", ".....dd.....",
    ".....dd.....", "....dddd....",
    "...dddddd...", "..dddddddd..",
};

static const char *const SP_MAQUINA[16] = {
    "xxxxxxxxxxxxxxxx",
    "xKKKKKKKKKKKKKKx",
    "xKddddddddddddKx",
    "xKdcccccccccddKx",
    "xKdcCCCCCCCcddKx",
    "xKdcCCCCCCCcddKx",
    "xKdcccccccccddKx",
    "xKddddddddddddKx",
    "xKdrrddddddggdKx",
    "xKdrrddddddggdKx",
    "xKddddddddddddKx",
    "xKdyyyyyyyyyydKx",
    "xKdYYYYYYYYYYdKx",
    "xKddddddddddddKx",
    "xKKKKKKKKKKKKKKx",
    "xxxxxxxxxxxxxxxx",
};

static const char *const SP_PILA[16] = {
    "................",
    "................",
    "................",
    "................",
    ".....ggGg.......",
    "...ggGGGGg......",
    "..gGdddddGg.uu..",
    ".gGdddddddGguUu.",
    ".gddUUUdddgguUu.",
    "gGdUUUUUddGg.u..",
    "gddUUUUUdddg....",
    "gdddddddddgg....",
    "ggdddddddgg.....",
    ".gggggggggg.....",
    "..IIIIIIII......",
    "................",
};

static const char *const SP_PINO[24] = {
    ".......ff.......",
    "......ffff......",
    "......fFFf......",
    ".....ffFFff.....",
    ".....fFFFFf.....",
    "....ffFFFFff....",
    "....fFFFFFFf....",
    "...ffFFFFFFff...",
    "...fFFFFFFFFf...",
    "..ffFFFFFFFFff..",
    "..fFFFFFFFFFFf..",
    ".ffFFFFFFFFFFff.",
    ".fFFFFFFFFFFFFf.",
    "ffFFFFFFFFFFFFff",
    "fFFFFFFFFFFFFFFf",
    ".ffFFFFFFFFFFff.",
    "..ffffffffffff..",
    "......jjjj......",
    "......jJJj......",
    "......jJJj......",
    "......jJJj......",
    ".....jjJJjj.....",
    "....GGGGGGGG....",
    "...GGGGGGGGGG...",
};

static const char *const SP_TORRE[24] = {
    "......gggg......",
    ".....g....g.....",
    ".....g....g.....",
    "....gggggggg....",
    "....g......g....",
    "....g.gggg.g....",
    "....g......g....",
    "...gggggggggg...",
    "...g........g...",
    "...g..gggg..g...",
    "...g........g...",
    "..gggggggggggg..",
    "..g..........g..",
    "..g...gggg...g..",
    "..g..........g..",
    ".gggggggggggggg.",
    ".g............g.",
    ".g...gggggg...g.",
    ".g............g.",
    "gggggggggggggggg",
    "g..............g",
    "g..............g",
    "II............II",
    "IIIIIIIIIIIIIIII",
};

static const char *const SP_ESTATUA[24] = {
    "......IIII......",
    ".....IiiiiI.....",
    ".....IiwwiI.....",
    ".....IiiiiI.....",
    "......IiiI......",
    ".....IIIIII.....",
    "....IiiiiiiI....",
    "...IiiiiiiiiI...",
    "...IiiIIIIiiI...",
    "...IiiIiiIiiI...",
    "...IiiIiiIiiI...",
    "...IiiiiiiiiI...",
    "....IiiiiiiI....",
    "....IiiIIiiI....",
    "....IiiIIiiI....",
    "....IiiIIiiI....",
    "...IIiiIIiiII...",
    "..IIIIIIIIIIII..",
    "..IiiiiiiiiiiI..",
    "..IiiiiiiiiiiI..",
    ".IIIIIIIIIIIIII.",
    ".IiiiiiiiiiiiiI.",
    "IIIIIIIIIIIIIIII",
    "IIIIIIIIIIIIIIII",
};

static const char *const SP_SERVIDOR[24] = {
    "................",
    "..kkkkkkkkkkkk..",
    "..kxxxxxxxxxxk..",
    "..kxnnnnnnnnxk..",
    "..kxnKKKKKKnxk..",
    "..kxnKvKKvKnxk..",
    "..kxnKKKKKKnxk..",
    "..kxnnnnnnnnxk..",
    "..kxxxxxxxxxxk..",
    "..kxnnnnnnnnxk..",
    "..kxnKvKKvKnxk..",
    "..kxnKKKKKKnxk..",
    "..kxnnnnnnnnxk..",
    "..kxxxxxxxxxxk..",
    "..kxnnnnnnnnxk..",
    "..kxnKKvvKKnxk..",
    "..kxnnnnnnnnxk..",
    "..kxxxxxxxxxxk..",
    "..kxxxxxxxxxxk..",
    "..kkkkkkkkkkkk..",
    "..dddddddddddd..",
    "..dddddddddddd..",
    ".dddddddddddddd.",
    "................",
};

static const char *const SP_HORNO[24] = {
    "................................",
    "......IIIIIIIIIIIIIIIIIIII......",
    ".....IIIIIIIIIIIIIIIIIIIIII.....",
    "....IIiiiiiiiiiiiiiiiiiiiiII....",
    "....IiiiiiiiiiiiiiiiiiiiiiiI....",
    "....IiixxxxxxxxxxxxxxxxxxiiI....",
    "....IiixOOOOOOOOOOOOOOOOxiiI....",
    "....IiixOooooooooooooooOxiiI....",
    "....IiixOoyyyyyyyyyyyyoOxiiI....",
    "....IiixOoyywwwwwwwwyyoOxiiI....",
    "....IiixOoyywwwwwwwwyyoOxiiI....",
    "....IiixOoyyyyyyyyyyyyoOxiiI....",
    "....IiixOooooooooooooooOxiiI....",
    "....IiixOOOOOOOOOOOOOOOOxiiI....",
    "....IiixxxxxxxxxxxxxxxxxxiiI....",
    "....IiiiiiiiiiiiiiiiiiiiiiiI....",
    "....IiiIIIIiiiiiiiiIIIIiiiiI....",
    "....IiiIuuIiiiiiiiIuuIiiiiiI....",
    "....IiiIuuIiiiiiiiIuuIiiiiiI....",
    "....IiiIIIIiiiiiiiiIIIIiiiiI....",
    "....IiiiiiiiiiiiiiiiiiiiiiiI....",
    "...IIIIIIIIIIIIIIIIIIIIIIIIII...",
    "...IIIIIIIIIIIIIIIIIIIIIIIIII...",
    "................................",
};

static const char *const SP_BARCO[24] = {
    "................................",
    "..............jj................",
    "..............jj................",
    ".............Gjj................",
    "...........GGGjj................",
    ".........GGGGGjj................",
    ".......GGGGGGGjj................",
    ".....GGGGGGGGGjj................",
    "...GGGGGGGGGGGjj................",
    "...GGGGGGGGGGGjj................",
    ".....GGGGGGGGGjj................",
    ".......GGGGGGGjj................",
    "..............jj................",
    "..............jj................",
    "..jjjjjjjjjjjjjjjjjjjjjjjjjjjj..",
    ".jJJJJJJJJJJJJJJJJJJJJJJJJJJJJj.",
    ".jJuuJJJJJJJJJJJJJJJJJJJJuuJJJj.",
    ".jJJJJJJJJJJJJJJJJJJJJJJJJJJJJj.",
    "..jJJJJJJJJJJJJJJJJJJJJJJJJJJj..",
    "...jjJJJJJJJJJJJJJJJJJJJJJJjj...",
    ".....jjjJJJJJJJJJJJJJJJJjjj.....",
    "........jjjjjjjjjjjjjjjj........",
    "................................",
    "................................",
};

static const propdef_t PROPS[] = {
    /* sprite      height width  solid rows */
    { SP_ARBOL, 36, 2, 2, 12 },   /* 0 tree: you pass behind it at the top */
    { SP_CASA, 36, 4, 3, 12 },   /* 1 house                            */
    { SP_TALLER, 36, 4, 3, 12 },   /* 2 workshop                         */
    { SP_FUENTE, 22, 3, 2, 12 },   /* 3 fountain                         */
    { SP_CARTEL, 14, 2, 1, 12 },   /* 4 sign                             */
    { SP_FAROLA, 24, 1, 1, 12 },   /* 5 street lamp                      */
    { SP_MAQUINA, 16, 2, 2, 8 },   /* 6 machine                          */
    { SP_PILA, 16, 2, 1, 8 },   /* 7 scrap pile                       */
    { SP_PINO, 24, 2, 2, 8 },   /* 8 pine                             */
    { SP_TORRE, 24, 2, 2, 8 },   /* 9 pylon                            */
    { SP_ESTATUA, 24, 2, 3, 8 },   /* 10 statue                          */
    { SP_SERVIDOR, 24, 2, 3, 8 },   /* 11 server                          */
    { SP_HORNO, 24, 4, 3, 8 },   /* 12 furnace                         */
    { SP_BARCO, 24, 4, 3, 8 },   /* 13 ship                            */
};

#define NPROPS  ((int)(sizeof(PROPS) / sizeof(PROPS[0])))

void ch_prop_draw(ch_buf_t *b, const ch_prop_t *pr)
{
    if (pr->sprite >= NPROPS) return;
    const propdef_t *d = &PROPS[pr->sprite];

    /* THE SHADOW, AND WHY IT IS FLAT AND NOT A DISC.
     *
     * Without one a house is a drawing pasted on the grass; with one it is
     * standing on it, and that is the whole of what three rows of dark pixels
     * buy. They go at the FOOT of the sprite and inside its own width: the
     * combat robot learned the same lesson the hard way, where a disc centred
     * on the base stuck out by its whole radius and was drawn over the text
     * panel. Here it would run under the neighbouring tile and be cut by the
     * next prop painted after it.
     *
     * Width follows the sprite and the rows narrow as they go down, which at
     * this size reads as an ellipse without being one. */
    {
        int w = d->cw * TILE;
        int alto = d->rows * TILE / d->grid;        /* on the panel's grid  */
        int y = pr->y * TILE + alto - 3;
        int x = pr->x * TILE;
        for (int k = 0; k < 3; k++) {
            int m = 1 + k * 2;          /* each row a little narrower        */
            /* Out of SIXTEEN: ch_shade goes through ch_mix, and 16 is solid
             * black. Seven, five, three is a shadow; forty is a hole. */
            ch_shade(b, x + m, y + k, w - m * 2, 1, -(7 - k * 2));
        }
    }

    if (d->grid == TILE) {
        ch_blit(b, pr->x * TILE, pr->y * TILE, d->px, d->rows);
    } else {
        ch_blit_esc(b, pr->x * TILE, pr->y * TILE, d->px, d->rows, TILE, d->grid);
    }
}

/* --------------------------------------------------------------------------
 * IS THERE ANYTHING DRAWN ON TOP OF THIS CELL?
 *
 * Props and entities are painted INTO THE BACKGROUND, on top of the ground.
 * The flowing water repaints its own cells into the frame buffer, and a cell
 * that has a sign standing on it gets the sign wiped - and then restored from
 * the background on the next frame, and wiped again on that run's next turn.
 * On the board that reads as a sign blinking at the edge of the pond, which is
 * exactly what it was.
 *
 * So the runs stop at anything that covers them. A few tiles of water under a
 * sign do not ripple; nobody can tell, and it is a great deal better than the
 * blinking.
 *
 * The box of an entity is taken from what ch_ent_draw() actually blits, and
 * not from a generous envelope around it. That is not fussiness: a three-by-
 * three box around the sign at the edge of the first town's pond swallowed six
 * of its twelve cells and half the pond stopped moving. The sign is fourteen
 * pixels tall drawn six above its cell, so it covers two rows, not three.
 * -------------------------------------------------------------------------- */

/* How far above its own cell each entity's sprite reaches, in cells, and how
 * wide it is. Mirrors ch_ent_draw() below: if a sprite there changes, this
 * changes with it. */
static void ent_caja(const ch_ent_t *e, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = e->x; *x1 = e->x; *y0 = e->y; *y1 = e->y;
    switch (e->tipo) {
    case E_PNJ: case E_TIENDA:          /* 16 tall, blitted at y-8           */
    case E_COFRE:                       /* 10 tall, at y-2                   */
    case E_CARTEL:                      /* 14 tall, at y-6                   */
        *x0 = e->x - 1; *x1 = e->x + 1; *y0 = e->y - 1;
        break;
    case E_CABINA:                      /* 18 tall, at y-10: two rows up     */
        *x0 = e->x - 1; *x1 = e->x + 1; *y0 = e->y - 2;
        break;
    case E_PUERTA: {                    /* as big as the opening             */
        int w, h;
        ch_puerta_caja(e, &w, &h);
        *x1 = e->x + w - 1; *y1 = e->y + h - 1;
        break;
    }
    default:                            /* the rest draw nothing, or draw
                                         * inside their own cell             */
        break;
    }
}

bool ch_celda_tapada(const ch_room_t *r, int tx, int ty)
{
    for (int i = 0; i < r->nprops; i++) {
        const ch_prop_t *pr = &r->props[i];
        if (pr->sprite >= NPROPS) continue;
        const propdef_t *d = &PROPS[pr->sprite];
        int filas = (d->rows + d->grid - 1) / d->grid;
        if (tx >= pr->x && tx < pr->x + d->cw &&
            ty >= pr->y && ty < pr->y + filas) {
            return true;
        }
    }
    for (int i = 0; i < r->nents; i++) {
        int x0, y0, x1, y1;
        ent_caja(&r->ents[i], &x0, &y0, &x1, &y1);
        if (tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1) return true;
    }
    return false;
}

bool ch_prop_solido(const ch_room_t *r, int tx, int ty)
{
    for (int i = 0; i < r->nprops; i++) {
        const ch_prop_t *pr = &r->props[i];
        if (pr->sprite >= NPROPS) continue;
        const propdef_t *d = &PROPS[pr->sprite];
        int filas = (d->rows + d->grid - 1) / d->grid;
        int y0 = pr->y + filas - d->solidas;
        if (tx >= pr->x && tx < pr->x + d->cw && ty >= y0 && ty < pr->y + filas) {
            return true;
        }
    }
    return false;
}


/* --------------------------------------------------------------------------
 * The entities' sprites
 *
 * An entity ALWAYS draws its own thing and the decorations are pure ornament:
 * that is why no decoration may land on an entity's cell, or the thing is
 * drawn twice and the one underneath pokes out at the edges. It is the kind of
 * defect that in the simulator looks like "an odd pixel" and on the board
 * stays stuck.
 * -------------------------------------------------------------------------- */

static const char *const SP_ABUELA[24] = {
    "...............", "....mmmmmmmm...",
    "..mhhhhhhhhhm..", "..mhhhhhhhhhm..",
    "..hwwwhhhwwwh..", "..hkwwhhhkwwh..",
    "..hhhhhhhhhhh..", "...hhhMMMhhh...",
    "....hhhhhhh....", "...ppppppppp...",
    "..ppppppppppp..", ".ppppppppppppp.",
    "hpppppPPPpppppH", "hpppppPPPpppppH",
    ".ppppppPPpppppp", "..ppppppppppp..",
    "...ppppppppp...", "...ppp...ppp...",
    "...ppp...ppp...", "...KKK...KKK...",
    "...KKK...KKK...", "...ddd...ddd...",
    "...ddd...ddd...", "...............",
};

static const char *const SP_CHICO[24] = {
    "...............", "....oooooooo...",
    "..ohhhhhhhhho..", "..ohhhhhhhhho..",
    "..hwwwhhhwwwh..", "..hkwwhhhkwwh..",
    "..hhhhhhhhhhh..", "...hhhrrrhhh...",
    "....hhhhhhh....", "...ccccccccc...",
    "..ccccccccccc..", ".ccccccccccccc.",
    "hcccccCCCcccccH", "hcccccCCCcccccH",
    ".ccccccCCcccccc", "..ccccccccccc..",
    "...ccccccccc...", "...ccc...ccc...",
    "...ccc...ccc...", "...bbb...bbb...",
    "...bbb...bbb...", "...KKK...KKK...",
    "...KKK...KKK...", "...............",
};

static const char *const SP_VECINO[24] = {
    "...............", "....JJJJJJJJ...",
    "..JhhhhhhhhhJ..", "..JhhhhhhhhhJ..",
    "..hwwwhhhwwwh..", "..hkwwhhhkwwh..",
    "..hhhhhhhhhhh..", "...hhhjjjhhh...",
    "....hhhhhhh....", "...vvvvvvvvv...",
    "..vvvvvvvvvvv..", ".vvvvvvvvvvvvv.",
    "hvvvvvVVVvvvvvH", "hvvvvvVVVvvvvvH",
    ".vvvvvvVVvvvvvv", "..vvvvvvvvvvv..",
    "...vvvvvvvvv...", "...jjj...jjj...",
    "...jjj...jjj...", "...JJJ...JJJ...",
    "...JJJ...JJJ...", "...KKK...KKK...",
    "...KKK...KKK...", "...............",
};

static const char *const SP_SENORA[24] = {
    "...............", "....YYYYYYYY...",
    "..YhhhhhhhhhY..", "..YhhhhhhhhhY..",
    "..hwwwhhhwwwh..", "..hkwwhhhkwwh..",
    "..hhhhhhhhhhh..", "...hhhmmmhhh...",
    "....hhhhhhh....", "...mmmmmmmmm...",
    "..mmmmmmmmmmm..", ".mmmmmmmmmmmmm.",
    "hmmmmmMMMmmmmmH", "hmmmmmMMMmmmmmH",
    ".mmmmmmMMmmmmmm", "..mmmmmmmmmmm..",
    "...mmmmmmmmm...", "...mmm...mmm...",
    "...mmm...mmm...", "...ddd...ddd...",
    "...ddd...ddd...", "...KKK...KKK...",
    "...KKK...KKK...", "...............",
};

static const char *const *const NPCS[] = {
    SP_ABUELA, SP_ABUELA, SP_CHICO, SP_VECINO, SP_SENORA,
};
#define NNPCS   ((int)(sizeof(NPCS) / sizeof(NPCS[0])))

static const char *const SP_COFRE[15] = {
    "...YYYYYYYYYYYY...", "..YyyyyyyyyyyyyY..",
    "..YyYYYYYYYYYYyY..", "..YyyyyyyyyyyyyY..",
    "..YyyyyyyyyyyyyY..", "YYYYYYYYYYYYYYYYYY",
    "YyyyyyyKKKKyyyyyyY", "YyyyyyyKkkKyyyyyyY",
    "YyyyyyyKKKKyyyyyyY", "YyyyyyyyyyyyyyyyyY",
    "YyyyyyyyyyyyyyyyyY", ".YYYYYYYYYYYYYYYY.",
    "..UUUUUUUUUUUUUU..", "..UUUUUUUUUUUUUU..",
    "..................",
};

static const char *const SP_COFRE_ABIERTO[15] = {
    "...JJJJJJJJJJJJ...", "..JkkkkkkkkkkkkJ..",
    "..JkkkkkkkkkkkkJ..", "..JkkkkkkkkkkkkJ..",
    "...JJJJJJJJJJJJ...", "YYYYYYYYYYYYYYYYYY",
    "YkkkkkkkkkkkkkkkkY", "YkkkkkkkkkkkkkkkkY",
    "YkkkkkkkkkkkkkkkkY", "YyyyyyyyyyyyyyyyyY",
    "YyyyyyyyyyyyyyyyyY", ".YYYYYYYYYYYYYYYY.",
    "..UUUUUUUUUUUUUU..", "..UUUUUUUUUUUUUU..",
    "..................",
};

static const char *const SP_SIGNO[20] = {
    "...jjjjjjjjj...", "..jGGGGGGGGGj..",
    "..jGkkkkkkkGj..", "..jGkGGGGGkGj..",
    "..jGkGGGGGkGj..", "..jGkGGGGGkGj..",
    "..jGkkkkkkkGj..", "..jGGGGGGGGGj..",
    "...jjjjjjjjj...", "......jjj......",
    "......jjj......", "......jjj......",
    "......jjj......", ".....JJJJJ.....",
    "....JJJJJJJ....", "....JJJJJJJ....",
    "...............", "...............",
    "...............", "...............",
};

/* A flat three-step arrow, pointing at 'dir' (0 down, 1 up, 2 left, 3 right).
 * It is built by stacking rectangles from the BASE: it is the only way for the
 * tip to be the narrow part. */
/* The arrow that says "you can leave here". It is drawn FROM THE CELL, not at
 * a fixed size: on the 8 px grid it was seven pixels across and four deep, and
 * left at that on the 12 it reads as a smudge in the middle of a street. Now
 * it fills the cell it is on, which is the only size that stays right the next
 * time the grid moves. */
static void flecha(ch_buf_t *b, int cx, int cy, int dir, uint16_t c)
{
    int n = TILE / 2;                       /* six rows deep on a 12 px cell */
    for (int i = 0; i < n; i++) {
        int w = (TILE - 1) - i * 2;
        if (w < 1) w = 1;
        switch (dir) {
        case 1: ch_rect(b, cx - w / 2, cy - n / 2 + i, w, 1, c); break;
        case 0: ch_rect(b, cx - w / 2, cy + n / 2 - i, w, 1, c); break;
        case 2: ch_rect(b, cx - n / 2 + i, cy - w / 2, 1, w, c); break;
        default: ch_rect(b, cx + n / 2 - i, cy - w / 2, 1, w, c); break;
        }
    }
}


/* --------------------------------------------------------------------------
 * The phone booth
 *
 * Ten wide and eighteen tall, like the characters: it sticks out upwards and
 * the cell you touch is still the one at its feet. Blue and lit, so it reads
 * as a booth at a glance in a town full of brown and green - and the aerial
 * on the roof is there to say what it is for, because nothing else on this
 * map talks to another watch.
 * -------------------------------------------------------------------------- */
static const char *const SP_CABINA[28] = {
    ".......k.......", "......kck......",
    ".......k.......", "....kkkkkkk....",
    "...kBBBBBBBk...", "...kBBBBBBBk...",
    "...kBcccccBk...", "...kBcccccBk...",
    "...kBcwwwcBk...", "...kBcwwwcBk...",
    "...kBcccccBk...", "...kBcccccBk...",
    "...kBccccdBk...", "...kBcccdddBk..",
    "...kBcdddddBk..", "...kBcccccBk...",
    "...kBcccccBk...", "...kBcccccBk...",
    "...kBcccccBk...", "...kBcccccBk...",
    "...kBBBBBBBk...", "...kBBBBBBBk...",
    "...kBBBBBBBk...", "...kkkkkkkkk...",
    "....KKKKKKK....", "....KKKKKKK....",
    "...............", "...............",
};

void ch_ent_draw(ch_buf_t *b, const ch_room_t *r, const ch_ent_t *e, bool hecho)
{
    int x = e->x * TILE, y = e->y * TILE;

    switch (e->tipo) {
    case E_PUERTA: {
        /* A DOOR HAS TO BE VISIBLE. With touch as the only control, an
         * invisible exit is an exit that does not exist: the first town's was
         * a stretch of earth identical to all the rest, and there was no way
         * to know you could leave there. The arrow goes over every cell of the
         * opening. */
        int w, h, dir = ch_puerta_lado(e);

        ch_puerta_caja(e, &w, &h);
        if (dir < 0) {
            if (r->suelo[e->y][e->x] == ':') dir = e->y < ROWS / 2 ? 1 : 0;
            else if (r->tema == TEMA_DUNGEON) {
            /* A door that does NOT lead to the edge is not an exit, it is an
             * entrance: it gets a hatch and not an arrow. An arrow pointing
             * down in the middle of a room says nothing. */
                ch_rect(b, x, y + 1, w * TILE, TILE - 2, ch_rgb(0x05060C));
                ch_frame(b, x, y + 1, w * TILE, TILE - 2, ch_rgb(0x99A3BC));
                for (int k = 0; k < w * 2; k++) {
                    ch_rect(b, x + 2 + k * 4, y + 3, 2, 1, ch_rgb(0x606B85));
                }
                break;
            }
            else return;            /* a house's door: the house already draws it */
        }

        for (int k = 0; k < w * h; k++) {
            flecha(b, x + (k % w) * TILE + TILE / 2, y + (k / w) * TILE + TILE / 2,
                   dir, ch_rgb(r->tema == TEMA_DUNGEON ? 0x7BE9FF : 0xFFE45E));
        }
        break;
    }
    case E_PNJ:
    case E_TIENDA: {
        int a = (e->tipo == E_TIENDA) ? 0 : e->p1;
        if (a >= NNPCS) a = 1;
        /* The character is 16 tall and the cell 8: it sticks out upwards, just
         * like the player's robot. That way it is seen whole without the cell
         * below ceasing to be the one you touch. */
        ch_blit(b, x - 1, y - 12, NPCS[a], 24);
        break;
    }
    case E_COFRE:
        ch_blit(b, x - 3, y - 3, hecho ? SP_COFRE_ABIERTO : SP_COFRE, 15);
        break;
    case E_CARTEL:
        ch_blit(b, x - 1, y - 8, SP_SIGNO, 20);
        break;

    case E_CABINA:
        ch_blit(b, x - 1, y - 16, SP_CABINA, 28);
        break;

    case E_BLOQUEO:
        /* Closed: a striped barrier. Open: nothing is drawn, and that is half
         * the prize of having won. */
        if (!hecho) {
            ch_rect(b, x, y + 1, TILE, TILE - 2, ch_rgb(0x232B41));
            for (int k = 0; k < 3; k++) {
                ch_rect(b, x + k * 3, y + 2, 2, TILE - 4, ch_rgb(0xFFE45E));
            }
            ch_rect(b, x, y, TILE, 1, ch_rgb(0x05060C));
            ch_rect(b, x, y + TILE - 1, TILE, 1, ch_rgb(0x05060C));
        }
        break;
    default:
        break;
    }
}
