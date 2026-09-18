/*
 * BLACKJACK - the court figures
 *
 * Pixel art, half a figure each, 23 x 20 source pixels drawn at x2. The
 * other half is the same drawing turned 180 degrees, as on a real deck, so
 * the card reads the same from either end. The king holds a sword, the queen
 * a rose and the jack a halberd; the clothes change colour with the suit.
 *
 * Designed with a previewer that renders the rows eight times larger, and
 * checked on the sheet tools/bj_harness.c lays out. One character is one
 * pixel:
 *
 *   .  transparent      k  outline           s S  skin, its shadow
 *   y Y  gold           a A  first colour    b B  second colour
 *   w  white            g  fur shadow        h H  hair
 *   e  eyes             r  lips, cheeks      m M  steel
 *   c  leaf green
 */
#include "bj_art.h"

static const char *const JACK[BJ_COURT_H] = {
    "............kwk......k.",
    "...........kwk......kmk",
    "......kbbbbwbbbbk.kmmmk",
    ".....kbbbbbbbbbbbkkmmHk",
    ".....kyyyyyyyyyyykkmMHk",
    ".....khhssssssshhk.kkHk",
    ".....khssesssesshk..kHk",
    ".....khsrssSssrshk..kHk",
    ".....khssssssssshk..kHk",
    ".....khsssrrrssshk..kHk",
    "......khssssssshk...kHk",
    ".......kkssssskk....kHk",
    "...kwgwgwgwgwgwgwgwkkHk",
    "..kbbbbbbaaaaabbbbbbkHk",
    "..kbbybbbaayaabbbybkssk",
    "..kBbbbbbaaaaabbbbbkkHk",
    "..kBbbybbaayaabbybbBkHk",
    "..kBbbbbbaaaaabbbbbBkHk",
    "..kBbbbbbayyyabbbbbBkHk",
    "..kBbbbbbaaaaabbbbbBkHk",
};

static const char *const QUEEN[BJ_COURT_H] = {
    ".........k.k.k.........",
    "........kykykyk........",
    ".......kyyyyyyyk.......",
    ".......kyayByayk.......",
    "......khYYYYYYYhk......",
    "....khhssssssssshhk.kk.",
    "....khhssesssesshhkkaak",
    "....khhsrssSssrshhkkAyk",
    "....khhssssrsssshhk.kk.",
    "....khhhssssssshhhk..c.",
    "....khhhhkssskhhhhk.cc.",
    "...khhaayysysyyaahhkc..",
    "..khaaaayssyssyaaakssk.",
    "..kaaaaaaysysyaaaakkkk.",
    "..kAaaaaaayyyaaaaaaAk..",
    "..kAaawaaaayaaaawaaAk..",
    "..kAaaaaaayyyaaaaaaAk..",
    "..kAabaaaayyyaaaabaAk..",
    "..kAaaaaaayyyaaaaaaAk..",
    "..kAaaaaaayyyaaaaaaAk..",
};

static const char *const KING[BJ_COURT_H] = {
    "...k...k...k...k.......",
    "..kmk.kyk.kyk.kyk......",
    "..kmk.kyykyyykyyk......",
    "..kmk.kyyyyyyyyyk......",
    "..kmk.kyayyByyayk......",
    "..kmk.kYYYYYYYYYk......",
    "..kmkkhhssssssshhk.....",
    "..kmkkhssesssesshk.....",
    "..kmkkhsrssSssrshk.....",
    "..kmkkhswwwwwwwshk.....",
    "..kmkkgwwwwrwwwwgk.....",
    ".kyyykkgwwwwwwwgk......",
    "..kHkkyyykgwgkyyyk.....",
    ".kssskaayykgkyyaaaaak..",
    "..kkkaaaayyyyyaaaaaak..",
    "..kAaawaayybyyaawaaAk..",
    "..kAaaaaayyyyyaaaaaAk..",
    "..kAawaaayybyyaaawaAk..",
    "..kAaaaaayyyyyaaaaaAk..",
    "..kAaaaaayyyyyaaaaaAk..",
};
/* The two colours of the clothes, by suit: spades, hearts, diamonds, clubs. */
static const uint32_t SUIT_A[4][2] = {
    { 0x2D5DA8, 0x1B3B70 }, { 0xC8323C, 0x8A1C24 },
    { 0xC8323C, 0x8A1C24 }, { 0x2E8B57, 0x1D5C39 },
};
static const uint32_t SUIT_B[4][2] = {
    { 0xC8323C, 0x8A1C24 }, { 0x2D5DA8, 0x1B3B70 },
    { 0x2E8B57, 0x1D5C39 }, { 0x2D5DA8, 0x1B3B70 },
};
/* hair by figure: jack, queen, king */
static const uint32_t HAIR[3][2] = {
    { 0x6B3E1E, 0x42240F }, { 0xC0772E, 0x87501C }, { 0xB9BAC6, 0x80818F },
};

static bool color_of(char ch, int suit, int fig, uint32_t *out)
{
    switch (ch) {
    case 'k': case 'e': *out = 0x1B1B24; return true;
    case 's': *out = 0xF4CFA6; return true;
    case 'S': *out = 0xD69E73; return true;
    case 'y': *out = 0xF2C23B; return true;
    case 'Y': *out = 0xB5831A; return true;
    case 'a': *out = SUIT_A[suit][0]; return true;
    case 'A': *out = SUIT_A[suit][1]; return true;
    case 'b': *out = SUIT_B[suit][0]; return true;
    case 'B': *out = SUIT_B[suit][1]; return true;
    case 'w': *out = 0xFFFFFF; return true;
    case 'g': *out = 0xC6CAD4; return true;
    case 'h': *out = HAIR[fig][0]; return true;
    case 'H': *out = HAIR[fig][1]; return true;
    case 'r': *out = 0xD9534F; return true;
    case 'm': *out = 0xD5DCE4; return true;
    case 'M': *out = 0x7D8793; return true;
    case 'c': *out = 0x3E9E5A; return true;
    default:  return false;
    }
}

void bj_court_draw(bj_img_t *img, int suit, int rank, int x0, int y0)
{
    int fig = rank - 10;                /* 0 jack, 1 queen, 2 king */
    const char *const *rows = fig == 0 ? JACK : (fig == 1 ? QUEEN : KING);
    for (int y = 0; y < BJ_COURT_H * 2; y++) {
        for (int x = 0; x < BJ_COURT_W; x++) {
            /* the lower half is the upper one turned around */
            char ch = y < BJ_COURT_H ? rows[y][x]
                                     : rows[BJ_COURT_H * 2 - 1 - y][BJ_COURT_W - 1 - x];
            uint32_t c;
            if (!color_of(ch, suit, fig, &c)) {
                continue;
            }
            int px = x0 + x * 2, py = y0 + y * 2;
            for (int j = 0; j < 2; j++) {
                if (py + j < 0 || py + j >= img->h) {
                    continue;
                }
                for (int i = 0; i < 2; i++) {
                    if (px + i >= 0 && px + i < img->w) {
                        img->px[(py + j) * img->w + px + i] = 0xFF000000u | c;
                    }
                }
            }
        }
    }
}
