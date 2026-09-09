#include "at_levels.h"

/* Generated with tools/gen_levels.py (not versioned: it is a single-use
 * script) and verified with at_harness.c. Each one's minimum move count comes
 * from the BFS, not from guesswork. */
static const at_level_t at_levels[] = {
    /* level  1 -  1 move  minimum,  1 car  */
    { {
        "......",
        "......",
        "AA....",
        "......",
        "......",
        "......",
    } },
    /* level  2 -  2 moves minimum,  2 cars */
    { {
        "......",
        "......",
        "AA..B.",
        "....B.",
        "....B.",
        "......",
    } },
    /* level  3 -  2 moves minimum,  2 cars */
    { {
        "......",
        "...B..",
        "AA.B..",
        "...B..",
        "......",
        "......",
    } },
    /* level  4 -  3 moves minimum,  3 cars */
    { {
        "....B.",
        "....B.",
        "AA..B.",
        "...CC.",
        "......",
        "......",
    } },
    /* level  5 -  3 moves minimum,  4 cars */
    { {
        "......",
        "....BC",
        "DAA.BC",
        "D.....",
        "D.....",
        "......",
    } },
    /* level  6 -  4 moves minimum,  4 cars */
    { {
        "......",
        "..D..C",
        "AADB.C",
        "...B..",
        "......",
        "......",
    } },
    /* level  7 -  4 moves minimum,  5 cars */
    { {
        "......",
        "..D..B",
        "AAD.CB",
        "....CB",
        "EE..C.",
        "......",
    } },
    /* level  8 -  5 moves minimum,  5 cars */
    { {
        "......",
        "...B..",
        "AA.B..",
        "C..B..",
        "C...D.",
        "CEEED.",
    } },
    /* level  9 -  6 moves minimum,  6 cars */
    { {
        "......",
        ".....B",
        "AACE.B",
        "..CE.B",
        "...EDD",
        "FFF...",
    } },
    /* level 10 -  6 moves minimum,  7 cars */
    { {
        "......",
        "....D.",
        ".AACDB",
        "...CDB",
        ".F.G..",
        ".F.GEE",
    } },
    /* level 11 -  7 moves minimum,  6 cars */
    { {
        "...C..",
        ".EEC..",
        "AABC.D",
        "..B..D",
        "..BFFD",
        "......",
    } },
    /* level 12 -  7 moves minimum,  7 cars */
    { {
        "...EEE",
        "..C.B.",
        "AACDB.",
        "..CD..",
        "....G.",
        ".FFFG.",
    } },
    /* level 13 -  7 moves minimum,  8 cars */
    { {
        "...B.C",
        "..HB.C",
        "AAHBDC",
        "....D.",
        "..GGE.",
        ".FFFE.",
    } },
    /* level 14 -  8 moves minimum,  9 cars */
    { {
        "...CH.",
        "II.CHB",
        "AA.CHB",
        "DFF...",
        "D..EEE",
        "..GG..",
    } },
    /* level 15 -  8 moves minimum, 10 cars */
    { {
        "I..EEE",
        "I.BFCD",
        "AABFCD",
        "...F..",
        "GH....",
        "GHJJJ.",
    } },
    /* level 16 -  9 moves minimum, 14 cars */
    { {
        "JNKK..",
        "JNGGG.",
        "AAEDBC",
        "FFEDBC",
        "LMI.BC",
        "LMI.HH",
    } },
    /* level 17 - 10 moves minimum, 11 cars */
    { {
        "H.GG..",
        "HIIIC.",
        "AADECB",
        "KKDE.B",
        "JJ.F.B",
        "...F..",
    } },
    /* level 18 - 10 moves minimum, 14 cars */
    { {
        ".KK.M.",
        "JJ..M.",
        "AACDBE",
        ".ICDBE",
        ".INNHH",
        "GGLLFF",
    } },
    /* level 19 - 10 moves minimum, 14 cars */
    { {
        "FMC.KK",
        "FMCGG.",
        "AACEDB",
        ".HHEDB",
        ".IINN.",
        "LL..JJ",
    } },
    /* level 20 - 11 moves minimum,  9 cars */
    { {
        "....E.",
        "..CDE.",
        "AACDEB",
        "G.CHHB",
        "G....F",
        "II...F",
    } },
    /* level 21 - 11 moves minimum, 12 cars */
    { {
        "FG.JJ.",
        "FG..E.",
        "AACBED",
        "LLCB.D",
        "KHHIID",
        "K.....",
    } },
    /* level 22 - 13 moves minimum, 10 cars */
    { {
        "..EJJ.",
        "..E.CD",
        "AAEBCD",
        "...BGI",
        ".FF.GI",
        ".HHH.I",
    } },
    /* level 23 - 13 moves minimum, 13 cars */
    { {
        "K..LL.",
        "K..BMM",
        "AAEBDC",
        ".IE.DC",
        ".IFFFJ",
        "HHGG.J",
    } },
    /* level 24 - 18 moves minimum, 11 cars */
    { {
        ".GGJ.C",
        "..BJDC",
        "AABEDC",
        "IHFE..",
        "IHF.KK",
        "......",
    } },
    /* level 25 - 20 moves minimum, 11 cars */
    { {
        "K..II.",
        "K.EDBC",
        "AAEDBC",
        "GGG...",
        "HHHFFJ",
        ".....J",
    } },
};

int at_level_count(void)
{
    return (int)(sizeof(at_levels) / sizeof(at_levels[0]));
}

const at_level_t *at_level_get(int index)
{
    int n = at_level_count();
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    return &at_levels[index];
}
