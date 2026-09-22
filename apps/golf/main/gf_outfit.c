/*
 * GOLF - the wardrobe and the shop's catalogue (see gf_outfit.h)
 */
#include "gf_outfit.h"

#include <string.h>

#ifndef N_
#define N_(s) s
#endif

static const gf_item_t SHIRTS[] = {
    { N_("Polo blanco"),            0,   0, 0xF2F2F0, 0xE6E6E6, 0 },
    { N_("Polo azul marino"),       0,   0, 0x1E2E55, 0x2A3E70, 0 },
    { N_("Polo verde"),             80,  0, 0x2E8B57, 0x3AA36A, 0 },
    { N_("Polo amarillo"),          80,  0, 0xF2C230, 0xF6D35A, 0 },
    { N_("Polo rosa"),              100, 0, 0xF28DB2, 0xF6A9C6, 0 },
    { N_("Rayas rojas"),            150, 0, 0xC8302C, 0xF2F2F0, 0 },
    { N_("Rayas lila"),             150, 0, 0x7B5EA7, 0xE8E0F0, 0 },
    { N_("Celeste y blanco"),       250, 0, 0x6CB4EE, 0xF6F6F6, 0 },
    { N_("Naranja fuego"),          220, 0, 0xF26A1B, 0x2A2A2A, 0 },
    { N_("Negro y dorado"),         400, 0, 0x1A1A1E, 0xD4AF37, 0 },
};

static const gf_item_t PANTS[] = {
    { N_("Pantalón caqui"),         0,   0, 0xC8B48A, 0xB8A47A, 0x4A3222 },
    { N_("Pantalón blanco"),        60,  0, 0xEDEDE8, 0xE0E0DA, 0x2A2A2A },
    { N_("Pantalón azul"),          60,  0, 0x22325A, 0x2C3E6C, 0x1A1A1A },
    { N_("Pantalón gris"),          60,  0, 0x6E7278, 0x80848A, 0x1A1A1A },
    { N_("Pantalón negro"),         80,  0, 0x1E1E22, 0x2A2A30, 0x6A4A2A },
    { N_("Escocés rojo"),           300, 0, 0xB02A2A, 0x1E4A2A, 0x1A1A1A },
    { N_("Escocés azul"),           300, 0, 0x2A4A8A, 0xE0C040, 0x1A1A1A },
};

static const gf_item_t HATS[] = {
    { N_("Sin gorro"),              0,   -1, 0, 0, 0 },
    { N_("Gorra blanca"),           0,   0, 0xF2F2F0, 0x1E2E55, 0 },
    { N_("Gorra roja"),             50,  0, 0xC8302C, 0xF2F2F0, 0 },
    { N_("Visera azul"),            120, 1, 0x2A5BD7, 0xF2F2F0, 0 },
    { N_("Piluso"),                 180, 2, 0xD8CBA8, 0x6E5A3A, 0 },
    { N_("Gorro de lana"),          220, 3, 0x2E6B3A, 0xF2F2F0, 0 },
    { N_("Boina"),                  260, 4, 0x6A6A70, 0x4A4A50, 0 },
    { N_("Sombrero de ala"),        350, 5, 0x8A5A30, 0x3A2A1A, 0 },
    { N_("Gorra dorada"),           600, 0, 0xD4AF37, 0x1A1A1E, 0 },
};

static const gf_item_t SHOES[] = {
    { N_("Zapatos blancos"),        0,   0, 0xF2F2F0, 0x1A1A1A, 0 },
    { N_("Negros y blancos"),       80,  0, 0x1A1A1E, 0xF2F2F0, 0 },
    { N_("Marrones clásicos"),      100, 0, 0x6A4022, 0xE8E0D0, 0 },
    { N_("Rojos"),                  150, 0, 0xC8302C, 0xF2F2F0, 0 },
    { N_("Dorados"),                500, 0, 0xD4AF37, 0xF2F2F0, 0 },
};

static const gf_item_t CLUBS[] = {
    { N_("Palos de acero"),         0,   0, 0xC8CCD2, 0x9A9EA6, 0x1A1A1A },
    { N_("Palos negros"),           200, 0, 0x2A2A30, 0x3A3A40, 0xC8302C },
    { N_("Palos azules"),           250, 0, 0x3A6AD0, 0x2A2A30, 0xF2F2F0 },
    { N_("Palos dorados"),          800, 0, 0xE0BC48, 0xD4AF37, 0x1A1A1A },
};

/* c1 = skin, c3 = hair */
static const gf_item_t LOOKS[] = {
    { N_("Piel clara, pelo castaño"),   0, 0, 0xF0C8A8, 0, 0x5A3A22 },
    { N_("Piel clara, pelo rubio"),     0, 0, 0xF2CCAE, 0, 0xD8B060 },
    { N_("Piel trigueña, pelo negro"),  0, 0, 0xC89468, 0, 0x1E1A18 },
    { N_("Piel morena, pelo negro"),    0, 0, 0x8A5A3A, 0, 0x141210 },
    { N_("Piel clara, pelo colorado"),  0, 0, 0xF2C8AA, 0, 0xB0482A },
    { N_("Canoso"),                     0, 0, 0xE6BC9C, 0, 0xC8C8C8 },
};

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

static const gf_item_t *const gf_items[CAT_N] = { SHIRTS, PANTS, HATS, SHOES, CLUBS, LOOKS };
static const uint8_t gf_item_count[CAT_N] = { COUNT(SHIRTS), COUNT(PANTS), COUNT(HATS), COUNT(SHOES), COUNT(CLUBS), COUNT(LOOKS) };
static const char *const gf_cat_name[CAT_N] = {
    N_("Remeras"), N_("Pantalones"), N_("Gorros"), N_("Zapatos"), N_("Palos"), N_("Aspecto"),
};

const gf_item_t *gf_item(int cat, int item)
{
    return &gf_items[cat][item];
}

int gf_item_n(int cat)
{
    return gf_item_count[cat];
}

const char *gf_cat(int cat)
{
    return gf_cat_name[cat];
}

void gf_wardrobe_default(gf_wardrobe_t *w)
{
    memset(w, 0, sizeof(*w));
    for (int c = 0; c < CAT_N; c++) {
        for (int i = 0; i < gf_item_count[c]; i++) {
            if (gf_items[c][i].price == 0) {
                w->own[c] |= 1u << i;
            }
        }
    }
    w->eq[CAT_SHIRT] = 1;           /* navy polo  */
    w->eq[CAT_HAT] = 1;             /* white cap  */
}

bool gf_owns(const gf_wardrobe_t *w, int cat, int item)
{
    return (w->own[cat] >> item) & 1u;
}

void gf_outfit_palette(const uint8_t eq[CAT_N], uint32_t pal[RG_N], int *hat)
{
    const gf_item_t *sh = &SHIRTS[eq[CAT_SHIRT] % COUNT(SHIRTS)];
    const gf_item_t *pa = &PANTS[eq[CAT_PANTS] % COUNT(PANTS)];
    const gf_item_t *ha = &HATS[eq[CAT_HAT] % COUNT(HATS)];
    const gf_item_t *so = &SHOES[eq[CAT_SHOES] % COUNT(SHOES)];
    const gf_item_t *cl = &CLUBS[eq[CAT_CLUBS] % COUNT(CLUBS)];
    const gf_item_t *lk = &LOOKS[eq[CAT_LOOK] % COUNT(LOOKS)];
    pal[RG_NONE]     = 0;
    pal[RG_SKIN]     = lk->c1;
    pal[RG_HAIR]     = lk->c3;
    pal[RG_SHIRT_A]  = sh->c1;
    pal[RG_SHIRT_B]  = sh->c2;
    pal[RG_PANTS_A]  = pa->c1;
    pal[RG_PANTS_B]  = pa->c2;
    pal[RG_SHOES_A]  = so->c1;
    pal[RG_SHOES_B]  = so->c2;
    pal[RG_BELT]     = pa->c3;
    pal[RG_GLOVE]    = 0xF4F4F0;
    pal[RG_CLUBHEAD] = cl->c1;
    pal[RG_SHAFT]    = cl->c2;
    pal[RG_GRIP]     = cl->c3;
    pal[RG_HAT_A]    = ha->c1;
    pal[RG_HAT_B]    = ha->c2;
    *hat = ha->hat;
}

void gf_outfit_guest(int player, uint8_t eq[CAT_N])
{
    static const uint8_t G[4][CAT_N] = {
        { 1, 0, 1, 0, 0, 0 },
        { 5, 2, 2, 1, 0, 2 },       /* red stripes, blue trousers, red cap  */
        { 2, 1, 3, 2, 0, 1 },       /* green, white trousers, blue visor    */
        { 3, 3, 4, 0, 0, 3 },       /* yellow, grey, bucket hat             */
    };
    memcpy(eq, G[player & 3], CAT_N);
}
