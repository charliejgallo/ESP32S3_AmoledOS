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

static const char *const PX_PASTO[8] = {
    "eeeeeeee", "eeeEeeee", "eeeeeeee", "eEeeeeEe",
    "eeeeeeee", "eeeeEeee", "eEeeeeee", "eeeeeeee",
};
static const char *const PX_PASTO2[8] = {
    "eeeeeeee", "eeEEeeee", "eEeeEeee", "eeeeeeEe",
    "eEeeeEEe", "eeeeeeee", "eeEeeeee", "eeeeeeee",
};
static const char *const PX_TIERRA[8] = {
    "hhhhhhhh", "hhHhhhhh", "hhhhhhHh", "hHhhhhhh",
    "hhhhhHhh", "hhhhhhhh", "hHhhhhhh", "hhhhHhhh",
};
static const char *const PX_TIERRA2[8] = {
    "hhhhhhhh", "hhhhHhhh", "hHhhhhhh", "hhhhhhhH",
    "hhHhhhhh", "hhhhhhHh", "hhhhhhhh", "hHhhhHhh",
};
static const char *const PX_ALTO[8] = {
    "EEEEEEEE", "EfEEEfEE", "EffEEffE", "EffEEffE",
    "fffEfffE", "ffffffff", "fFffffFf", "ffffffff",
};
static const char *const PX_AGUA[8] = {
    "llllllll", "lLlllLll", "llllllll", "LlllllLl",
    "llllllll", "llLlllll", "llllllLl", "llllllll",
};
static const char *const PX_CERCA[8] = {
    "eeeeeeee", "eeeeeeee", "jjjjjjjj", "JJJJJJJJ",
    "eejjeeee", "jjjjjjjj", "JJJJJJJJ", "eejjeeee",
};
static const char *const PX_PIEDRA[8] = {
    "iiiiiiii", "iiiiiiii", "iiiiiiii", "IIIIIIII",
    "iiiIiiii", "iiiIiiii", "iiiIiiii", "IIIIIIII",
};
static const char *const PX_LADRILLO[8] = {
    "aaaaaaaa", "aaaAaaaa", "aaaAaaaa", "AAAAAAAA",
    "aaaaaaaa", "aaaaaaaA", "aaaaaaaA", "AAAAAAAA",
};
static const char *const PX_MADERA[8] = {
    "jjjjjjjj", "jjjjjjjJ", "JJJJJJJJ", "jjjjjjjJ",
    "jjjjjjjJ", "jjjjjjjJ", "JJJJJJJJ", "jjjjjjjJ",
};
static const char *const PX_PARED[8] = {
    "QQQQQQQQ", "QQQQQQQQ", "QQQQQQQQ", "jjjjjjjj",
    "QQQQQQQQ", "QQQQQQQQ", "QQQQQQQQ", "jjjjjjjj",
};
static const char *const PX_ALFOMBRA[8] = {
    "AAAAAAAA", "AaAAAAaA", "AAAAAAAA", "AAAaAAAA",
    "AAAAAAAA", "AaAAAAaA", "AAAAAAAA", "AAAaAAAA",
};
static const char *const PX_MOSTRADOR[8] = {
    "GGGGGGGG", "GGGGGGGG", "jjjjjjjj", "jjjjjjjj",
    "JJJJJJJJ", "jjjjjjjj", "jjjjjjjj", "JJJJJJJJ",
};
static const char *const PX_METAL[8] = {
    "dddddddd", "dgddddgd", "dddddddd", "dddddddd",
    "dddddddd", "dddddddd", "dgddddgd", "dddddddd",
};
static const char *const PX_METAL2[8] = {
    "dddddddd", "dddddddd", "ddDDDDdd", "ddDddDdd",
    "ddDddDdd", "ddDDDDdd", "dddddddd", "dddddddd",
};
static const char *const PX_MURO[8] = {
    "xxxxxxxx", "xKKKKKKx", "xKddddKx", "xKddddKx",
    "xKddddKx", "xKddddKx", "xKKKKKKx", "xxxxxxxx",
};
/* Scrap: plating and rust over the metal floor. The pieces are deliberately
 * LARGE. The first version had loose pixels and from two cells away it read as
 * snow, not as broken metal: in an 8x8 pattern that repeats, fine detail
 * disappears and only the texture is left. */
/* ONE piece per cell and not scattered pixels. With the detail spread out,
 * nine cells of scrap in a row read as noise and not as a pile of metal: in an
 * 8x8 pattern that repeats, the fine stuff turns into texture and only what
 * takes up several pixels together survives. */
static const char *const PX_CHATARRA[8] = {
    "dddddddd", "ddgDDddd", "ddDggDdd", "dddDDddd",
    "ddddUUdd", "dddUUddd", "dddddddd", "dddddddd",
};
/* A puddle has to JOIN UP with the puddle beside it. With the blot centred on
 * the cell and clean edges, three cells in a row read as three links of a
 * chain instead of as one long puddle. */
static const char *const PX_ACEITE[8] = {
    "dKKKKKKd", "KKKKKKKK", "KKKKKKKK", "KKKKPKKK",
    "KKKKKKKK", "KKKKKKKK", "KKKKKKKK", "dKKKKKKd",
};
static const char *const PX_ROCA[8] = {
    "QQQQQQQQ", "QQIIIQQQ", "QIiiiIQQ", "IiiiiiIQ",
    "IiiiiiIQ", "QIiiiIQQ", "QQIIIQQQ", "QQQQQQQQ",
};
static const char *const PX_REJILLA[8] = {
    "dddddddd", "KdKdKdKd", "dddddddd", "KdKdKdKd",
    "dddddddd", "KdKdKdKd", "dddddddd", "KdKdKdKd",
};
static const char *const PX_BALDOSA[8] = {
    "gGGGGGGG", "gGGGGGGG", "gGGGGGGG", "gGGGGGGG",
    "gGGGGGGG", "gGGGGGGG", "gGGGGGGG", "gggggggg",
};
static const char *const PX_NEGRO[8] = {
    "kkkkkkkk", "kkkkkkkk", "kkkkkkkk", "kkkkkkkk",
    "kkkkkkkk", "kkkkkkkk", "kkkkkkkk", "kkkkkkkk",
};
static const char *const PX_PUENTE[8] = {
    "jjjjjjjj", "JJJJJJJJ", "jjjjjjjj", "jjjjjjjj",
    "JJJJJJJJ", "jjjjjjjj", "jjjjjjjj", "JJJJJJJJ",
};
static const char *const PX_ARBUSTO[8] = {
    "eeeeeeee", "eefffeee", "efFFFfee", "fFFfFFfe",
    "fFFFFFfe", "efFFFfee", "eefffeee", "eeeeeeee",
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
static const char *const PX_NIEVE[8] = {
    "GGGGGGGG", "GGwGGGGG", "GGGGGGwG", "GwGGGGGG",
    "GGGGGwGG", "GGGGGGGG", "GwGGGGGG", "GGGGwGGG",
};
/* Snowy scrub: the SAME silhouette as the tall grass and the dry scrub. All
 * three hide creatures, so all three have to be recognised the same way. */
static const char *const PX_NEVADO[8] = {
    "GGGGGGGG", "GwGGGwGG", "GwwGGwwG", "GwwGGwwG",
    "wwwGwwwG", "wwwwwwww", "wCwwwwCw", "wwwwwwww",
};
static const char *const PX_HIELO[8] = {
    "cccccccc", "ccCccccc", "cCcccCcc", "cccccccc",
    "ccccCccc", "cCcccccc", "cccccCcc", "cccccccc",
};
static const char *const PX_LAVA[8] = {
    "OOOOOOOO", "OoOOOoOO", "oooOoooO", "OoooOooo",
    "OOoOOOoO", "oOOOooOO", "OOOoOOOO", "OoOOOOoO",
};
static const char *const PX_VOLCAN[8] = {
    "SSSSSSSS", "SsSSSSSS", "SSSSSUSS", "SSsSSSSS",
    "SSSSSSSU", "SSSSSSSS", "SsSSSSSS", "SSSSuSSS",
};
static const char *const PX_ARENA[8] = {
    "qqqqqqqq", "qqQqqqqq", "qqqqqqQq", "qQqqqqqq",
    "qqqqqQqq", "qqqqqqqq", "qQqqqqqq", "qqqqQqqq",
};
static const char *const PX_MUELLE[8] = {
    "jjjJjjjJ", "JJJJJJJJ", "jjjJjjjJ", "jjjJjjjJ",
    "JJJJJJJJ", "jjjJjjjJ", "jjjJjjjJ", "JJJJJJJJ",
};
static const char *const PX_VADO[8] = {
    "llllllll", "lcllllcl", "llllllll", "cllllcll",
    "llllllll", "llclllll", "lllllcll", "llllllll",
};
/* The floor goes VERY dark and the wall BRIGHT. The first version had both at
 * the same contrast and on screen you could not see where the walls were: the
 * room looked like a flat board. In a dungeon that is not an aesthetic detail,
 * it is being unable to play it. */
static const char *const PX_CIRCUITO[8] = {
    "kkkkkkkk", "kkkkkkkk", "kkkkkkkk", "kkkKkkkk",
    "kkkKkkkk", "kkkkkkkk", "kkkkkkkk", "kkkkkkkk",
};
/* The track only appears on one cell in four: with the mark on ALL of them the
 * floor was as busy as the wall and the earlier problem came back. */
static const char *const PX_CIRCUITO2[8] = {
    "kkkkkkkk", "kkNkkkkk", "kkNNNkkk", "kkkkNkkk",
    "kkkkNNkk", "kkkkkkkk", "kkkkkkkk", "kkkkkkkk",
};
static const char *const PX_MURO_CIRC[8] = {
    "NNNNNNNN", "NKKKKKKN", "NKnnnnKN", "NKnnnnKN",
    "NKnnnnKN", "NKnnnnKN", "NKKKKKKN", "NNNNNNNN",
};
/* Dry scrub. It has the SAME silhouette as the tall grass and not that of a
 * mottled floor, because the two cells do the same thing -they hide creatures-
 * and the player has to recognise them at a glance. The colour changes, not
 * the shape. */
/* Cracked earth of the wasteland. It exists because 'Q' was being used as a
 * cell throughout zone 7 WITHOUT being in the table: buscar() returns the
 * first entry when it does not find the character, so the whole wasteland was
 * drawn as green grass and nobody said a word. */
static const char *const PX_SECO[8] = {
    "QQQQQQQQ", "QQQQQQQQ", "QQQUQQQQ", "QQUQQQQQ",
    "QQQQQQQQ", "QQQQQQUQ", "QQQQQUQQ", "QQQQQQQQ",
};
static const char *const PX_PARAMO[8] = {
    "QQQQQQQQ", "QVQQQVQQ", "QVVQQVVQ", "QVVQQVVQ",
    "VVVQVVVQ", "VVVVVVVV", "VEVVVVEV", "VVVVVVVV",
};
static const char *const PX_GRAVA[8] = {
    "IIIIIIII", "IiIIIiII", "IIIiIIII", "IiIIIIiI",
    "IIIIiIII", "IIiIIIII", "IIIIIIiI", "IiIIIIII",
};
static const char *const PX_CIUDAD[8] = {
    "DDDDDDDD", "DGGGGGGD", "DGGGGGGD", "DGGGGGGD",
    "DGGGGGGD", "DGGGGGGD", "DGGGGGGD", "DDDDDDDD",
};
static const char *const PX_MURO_CIU[8] = {
    "gggggggg", "gGGGGGGg", "gGDDDDGg", "gGDDDDGg",
    "gGDDDDGg", "gGDDDDGg", "gGGGGGGg", "gggggggg",
};
static const char *const PX_CRISTAL[8] = {
    "cccccccc", "cwcccccc", "ccwccccc", "cccwcccc",
    "ccccwccc", "cccccwcc", "ccccccwc", "cccccccc",
};

static const char *const BR_PASTO[8] = {
    "eeeeeeee", "eEeeeeEe", "ee.ee.ee", ".e...e..",
    "..e.....", "........", "........", "........",
};
static const char *const BR_TIERRA[8] = {
    "hhhhhhhh", "hHhhhhHh", "hh.hh.hh", ".h...h..",
    "........", "........", "........", "........",
};
static const char *const BR_ARENA[8] = {
    "qqqqqqqq", "qQqqqqQq", "qq.qq.qq", ".q...q..",
    "..q.....", "........", "........", "........",
};
static const char *const BR_NIEVE[8] = {
    "GGGGGGGG", "GwGGGGwG", "GG.GG.GG", ".G...G..",
    "..G.....", "........", "........", "........",
};
static const char *const BR_GRAVA[8] = {
    "GGGGGGGG", "GIGGGGIG", "GG.GG.GG", ".G...G..",
    "........", "........", "........", "........",
};
static const char *const BR_CIUDAD[8] = {
    "pppppppp", "pPpppppP", "pp.pp.pp", "........",
    "........", "........", "........", "........",
};
static const char *const BR_VOLCAN[8] = {
    "SSSSSSSS", "SsSSSSsS", "SS.SS.SS", ".S...S..",
    "........", "........", "........", "........",
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
    const char *rot[8];

    if (!ch_tile_corre(t)) { ch_tile_draw(b, t, tx, ty); return; }
    for (int i = 0; i < 8; i++) rot[i] = d->px[(i + fase) & 7];
    ch_blit(b, tx * TILE, ty * TILE, rot, 8);
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
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char k;
            uint16_t col;
            switch (lado) {
            case 1:  k = fr[7 - r][c];     break;   /* south                 */
            case 2:  k = fr[c][r];         break;   /* west                  */
            case 3:  k = fr[7 - c][r];     break;   /* east                  */
            default: k = fr[r][c];         break;   /* north                 */
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
    ch_blit(b, tx * TILE, ty * TILE, px, 8);
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
    uint8_t            rows;    /* height in pixels (multiple of 8)          */
    uint8_t            cw;      /* width in cells                            */
    uint8_t            solidas; /* rows of solid cells, from the bottom      */
} propdef_t;

static const char *const SP_ARBOL[24] = {
    "......ffff......",
    "....ffFFFFff....",
    "...fFFFFFFFFf...",
    "..fFFFFFFFFFFf..",
    ".fFFFFvvFFFFFFf.",
    ".fFFFvvvvFFFFFf.",
    "fFFFFvvvvvFFFFFf",
    "fFFFvvvvvvvFFFFf",
    "fFFFFvvvvvFFFFFf",
    "fFFFFFvvvFFFFFFf",
    ".fFFFFFFFFFFFFf.",
    ".fFFFFFFFFFFFFf.",
    "..fFFFFFFFFFFf..",
    "...ffFFFFFFff...",
    "....ffFFFFff....",
    "......fjjf......",
    "......jjjj......",
    "......jJJj......",
    "......jJJj......",
    "......jJJj......",
    ".....jjJJjj.....",
    "....jJJJJJJj....",
    "...EJJJJJJJJE...",
    "..EEEEEEEEEEEE..",
};

static const char *const SP_CASA[24] = {
    "........aaaaaaaaaaaaaa..........",
    "......aaaaaaaaaaaaaaaaaa........",
    "....aaaaaaaaaaaaaaaaaaaaaa......",
    "..aaaaaaaaaaaaaaaaaaaaaaaaaa....",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA..",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA..",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQ...",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQ...",
    ".QQQjjjjQQQQQQQQQQQjjjjQQQQQQ...",
    ".QQQjccjQQQQQQQQQQQjccjQQQQQQ...",
    ".QQQjccjQQQQQQQQQQQjccjQQQQQQ...",
    ".QQQjjjjQQQQQQQQQQQjjjjQQQQQQ...",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQ...",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQ...",
    ".QQQQQQQQQQQQQQQQQQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjjjjjjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJJJjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJJJjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJyJjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJJJjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJJJjQQQQQQQQQQQ...",
    ".QQQQQQQQQQQjJJJJjQQQQQQQQQQQ...",
    ".JJJJJJJJJJJjJJJJjJJJJJJJJJJJ...",
    "....EEEE....hhhhhh....EEEE......",
};

static const char *const SP_TALLER[24] = {
    "..IIIIIIIIIIIIIIIIIIIIIIIIII....",
    ".IIIIIIIIIIIIIIIIIIIIIIIIIIII...",
    "IIIIIIIIIIIIIIIIIIIIIIIIIIIIII..",
    "iiiiiiiiiiiiiiiiiiiiiiiiiiiiii..",
    "iiiiiiiiiiiiiiiiiiiiiiiiiiiiii..",
    ".dddddddddddddddddddddddddddd...",
    ".dyyyyyyyyyyyyyyyyyyyyyyyyyyd...",
    ".dyyyyyyyyyyyyyyyyyyyyyyyyyyd...",
    ".dddddddddddddddddddddddddddd...",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDD...",
    ".DDDDccccDDDDDDDDDDDDccccDDDD...",
    ".DDDDccccDDDDDDDDDDDDccccDDDD...",
    ".DDDDccccDDDDDDDDDDDDccccDDDD...",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDD...",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDD...",
    ".DDDDDDDDDDDKKKKKKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddddKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddddKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddoiKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddddKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddddKDDDDDDDDDDD...",
    ".DDDDDDDDDDDKddddKDDDDDDDDDDD...",
    ".xxxxxxxxxxxKddddKxxxxxxxxxxx...",
    "....IIII....dddddd....IIII......",
};

static const char *const SP_FUENTE[14] = {
    "......IIIIIIIIIIII......",
    "....IIiiiiiiiiiiiiII....",
    "..IIiiiiiiiiiiiiiiiiII..",
    "..IiiillllllllllllliiI..",
    "..IiilLLLLLLLLLLLLLliI..",
    "..IiilLLLLLccLLLLLLliI..",
    "..IiilLLLLLccLLLLLLliI..",
    "..IiilLLLLLccLLLLLLliI..",
    "..IiilLLLLLLLLLLLLLliI..",
    "..IiiillllllllllllliiI..",
    "..IIiiiiiiiiiiiiiiiiII..",
    "....IIiiiiiiiiiiiiII....",
    "......IIIIIIIIIIII......",
    ".......IIIIIIIIII.......",
};

static const char *const SP_CARTEL[10] = {
    "...jjjjjjjjjj...",
    "..jGGGGGGGGGGj..",
    "..jGkkkkkkkkGj..",
    "..jGkGGGGGGkGj..",
    "..jGkkkkkkkkGj..",
    "..jGGGGGGGGGGj..",
    "...jjjjjjjjjj...",
    ".......jj.......",
    ".......jj.......",
    "......JJJJ......",
};

static const char *const SP_FAROLA[16] = {
    "..yyyy..",
    ".yywwyy.",
    "yywwwwyy",
    "yywwwwyy",
    ".yywwyy.",
    "..dddd..",
    "...dd...",
    "...dd...",
    "...dd...",
    "...dd...",
    "...dd...",
    "...dd...",
    "...dd...",
    "...dd...",
    "..dddd..",
    ".dddddd.",
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
    { SP_ARBOL,      24,   2,   2 },   /* 0 tree: you pass behind it at the top */
    { SP_CASA,       24,   4,   3 },   /* 1 house                            */
    { SP_TALLER,     24,   4,   3 },   /* 2 workshop                         */
    { SP_FUENTE,     14,   3,   2 },   /* 3 fountain                         */
    { SP_CARTEL,     10,   2,   1 },   /* 4 sign                             */
    { SP_FAROLA,     16,   1,   1 },   /* 5 street lamp                      */
    { SP_MAQUINA,    16,   2,   2 },   /* 6 machine                          */
    { SP_PILA,       16,   2,   1 },   /* 7 scrap pile                       */
    { SP_PINO,       24,   2,   2 },   /* 8 pine                             */
    { SP_TORRE,      24,   2,   2 },   /* 9 pylon                            */
    { SP_ESTATUA,    24,   2,   3 },   /* 10 statue                          */
    { SP_SERVIDOR,   24,   2,   3 },   /* 11 server                          */
    { SP_HORNO,      24,   4,   3 },   /* 12 furnace                         */
    { SP_BARCO,      24,   4,   3 },   /* 13 ship                            */
};

#define NPROPS  ((int)(sizeof(PROPS) / sizeof(PROPS[0])))

void ch_prop_draw(ch_buf_t *b, const ch_prop_t *pr)
{
    if (pr->sprite >= NPROPS) return;
    const propdef_t *d = &PROPS[pr->sprite];
    ch_blit(b, pr->x * TILE, pr->y * TILE, d->px, d->rows);
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
    case E_PUERTA:                      /* as wide as the opening            */
        *x1 = e->x + (e->premio ? e->premio - 1 : 0);
        break;
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
        int filas = (d->rows + TILE - 1) / TILE;
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
        int filas = (d->rows + TILE - 1) / TILE;
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

static const char *const SP_ABUELA[16] = {
    "..GGGGGG..",
    ".GGGGGGGG.",
    ".GwwGGwwG.",
    ".GkwGGkwG.",
    ".GGGGGGGG.",
    "..GGmmGG..",
    "..pppppp..",
    ".ppppppppp",
    "GppPPPPppG",
    ".ppPPPPpp.",
    ".ppPPPPpp.",
    "..pppppp..",
    "..pp..pp..",
    "..dd..dd..",
    "..KK..KK..",
    "..........",
};

static const char *const SP_CHICO[16] = {
    "..........",
    "..oooooo..",
    ".ohhhhhho.",
    ".hwwhhwwh.",
    ".hkwhhkwh.",
    ".hhhhhhhh.",
    "..hhrrhh..",
    "..cccccc..",
    ".cccccccc.",
    "hcccCCcccH",
    ".cccCCccc.",
    "..cccccc..",
    "..cc..cc..",
    "..bb..bb..",
    "..KK..KK..",
    "..........",
};

static const char *const SP_VECINO[16] = {
    "..........",
    "..jjjjjj..",
    ".jjjjjjjj.",
    ".hwwhhwwh.",
    ".hkwhhkwh.",
    ".hhhhhhhh.",
    "..hhTThh..",
    "..vvvvvv..",
    ".vvvvvvvv.",
    "hvvVVVVvvH",
    ".vvVVVVvv.",
    ".vvvvvvvv.",
    "..vv..vv..",
    "..JJ..JJ..",
    "..KK..KK..",
    "..........",
};

static const char *const SP_SENORA[16] = {
    "..........",
    "..YYYYYY..",
    ".YYYYYYYY.",
    ".YwwYYwwY.",
    ".YkwYYkwY.",
    ".YYYYYYYY.",
    "..YYmmYY..",
    "..mmmmmm..",
    ".mmmmmmmm.",
    "YmmMMMMmmY",
    ".mmMMMMmm.",
    ".mmmmmmmm.",
    ".mmmmmmmm.",
    "..dd..dd..",
    "..KK..KK..",
    "..........",
};

static const char *const *const NPCS[] = {
    SP_ABUELA, SP_ABUELA, SP_CHICO, SP_VECINO, SP_SENORA,
};
#define NNPCS   ((int)(sizeof(NPCS) / sizeof(NPCS[0])))

static const char *const SP_COFRE[10] = {
    "..YYYYYYYY..",
    ".YyyyyyyyyY.",
    ".YyYYYYYYyY.",
    ".YyyyyyyyyY.",
    "YYYYYYYYYYYY",
    "YyyyyKKyyyyY",
    "YyyyyKKyyyyY",
    "YyyyyyyyyyyY",
    ".YYYYYYYYYY.",
    "..UUUUUUUU..",
};

static const char *const SP_COFRE_ABIERTO[10] = {
    "..JJJJJJJJ..",
    ".JkkkkkkkkJ.",
    ".JkkkkkkkkJ.",
    "..JJJJJJJJ..",
    "YYYYYYYYYYYY",
    "YkkkkkkkkkkY",
    "YkkkkkkkkkkY",
    "YyyyyyyyyyyY",
    ".YYYYYYYYYY.",
    "..UUUUUUUU..",
};

static const char *const SP_SIGNO[14] = {
    "..jjjjjj..",
    ".jGGGGGGj.",
    ".jGkkkkGj.",
    ".jGkGGkGj.",
    ".jGkkkkGj.",
    ".jGGGGGGj.",
    "..jjjjjj..",
    "....jj....",
    "....jj....",
    "....jj....",
    "....JJ....",
    "...JJJJ...",
    "..........",
    "..........",
};

/* A flat three-step arrow, pointing at 'dir' (0 down, 1 up, 2 left, 3 right).
 * It is built by stacking rectangles from the BASE: it is the only way for the
 * tip to be the narrow part. */
static void flecha(ch_buf_t *b, int cx, int cy, int dir, uint16_t c)
{
    for (int i = 0; i < 4; i++) {
        int w = 7 - i * 2;
        switch (dir) {
        case 1: ch_rect(b, cx - w / 2, cy - 2 + i, w, 1, c); break;
        case 0: ch_rect(b, cx - w / 2, cy + 2 - i, w, 1, c); break;
        case 2: ch_rect(b, cx - 2 + i, cy - w / 2, 1, w, c); break;
        default: ch_rect(b, cx + 2 - i, cy - w / 2, 1, w, c); break;
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
static const char *const SP_CABINA[18] = {
    "....k.....",
    "...kck....",
    "....k.....",
    "..kkkkkk..",
    ".kBBBBBBk.",
    ".kBccccBk.",
    ".kBcwwcBk.",
    ".kBccccBk.",
    ".kBcccdBk.",
    ".kBcdddBk.",
    ".kBccccBk.",
    ".kBccccBk.",
    ".kBccccBk.",
    ".kBBBBBBk.",
    ".kBBBBBBk.",
    ".kkkkkkkk.",
    "..KKKKKK..",
    "..........",
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
        int w = e->premio ? e->premio : 1;
        int dir;

        if (e->y <= 1)            dir = 1;
        else if (e->y >= ROWS - 3) dir = 0;
        else if (e->x <= 1)        dir = 2;
        else if (e->x >= COLS - 2) dir = 3;
        else if (r->suelo[e->y][e->x] == ':') dir = e->y < ROWS / 2 ? 1 : 0;
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
        else return;                /* a house's door: the house already draws it */

        for (int k = 0; k < w; k++) {
            flecha(b, x + k * TILE + TILE / 2, y + TILE / 2, dir,
                   ch_rgb(r->tema == TEMA_DUNGEON ? 0x7BE9FF : 0xFFE45E));
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
        ch_blit(b, x - 1, y - 8, NPCS[a], 16);
        break;
    }
    case E_COFRE:
        ch_blit(b, x - 2, y - 2, hecho ? SP_COFRE_ABIERTO : SP_COFRE, 10);
        break;
    case E_CARTEL:
        ch_blit(b, x - 1, y - 6, SP_SIGNO, 14);
        break;

    case E_CABINA:
        ch_blit(b, x - 1, y - 10, SP_CABINA, 18);
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
