/*
 * MONSTER HOP - the shop (see mh_shop.h)
 */
#include "mh_shop.h"

#include "aos_i18n.h"

#include <string.h>

/* the catalogue, as a function: a table read from another file would go
 * through R_XTENSA_GLOB_DAT (APP-GUIDE, "the platform") */
static const mh_item_t *items(int cat, int *n)
{
    static const mh_item_t caps[] = {
        { CAT_CAP, CAP_CAP, 0, { 0xE62E2E, 0xB81E22, 0xFFD040, 0x5A1A1A }, N_("Gorra roja") },
        { CAT_CAP, CAP_CAP, 40, { 0x2E6AE8, 0x1E48B0, 0xFFFFFF, 0x1A2A5A }, N_("Gorra azul") },
        { CAT_CAP, CAP_CAP, 40, { 0x3AB04A, 0x248A34, 0xFFFFFF, 0x1A4A2A }, N_("Gorra verde") },
        { CAT_CAP, CAP_BACK, 60, { 0x2A2A30, 0xE83434, 0xFFFFFF, 0x101014 }, N_("Gorra al revés negra") },
        { CAT_CAP, CAP_BACK, 60, { 0xF08A20, 0x2A2A2A, 0xFFFFFF, 0x5A2A08 }, N_("Gorra al revés naranja") },
        { CAT_CAP, CAP_BEANIE, 80, { 0x8A4AE0, 0xF5F5F5, 0xFFD040, 0x3A1A6A }, N_("Gorro de lana violeta") },
        { CAT_CAP, CAP_BEANIE, 80, { 0x4AD0A8, 0xFFFFFF, 0xF07090, 0x1A5A4A }, N_("Gorro de lana menta") },
        { CAT_CAP, CAP_BUCKET, 100, { 0xC8B070, 0x8A7040, 0x5A4A30, 0x4A3A20 }, N_("Piluso caqui") },
        { CAT_CAP, CAP_BUCKET, 100, { 0xF08AC0, 0xFFFFFF, 0x8A3A6A, 0x5A2A4A }, N_("Piluso rosa") },
        { CAT_CAP, CAP_PROPELLER, 150, { 0xE83434, 0x2E7CF0, 0xFFD040, 0x5A1A1A }, N_("Gorrito con hélice") },
        { CAT_CAP, CAP_CROWN, 400, { 0xF0C030, 0xD09820, 0xE83458, 0x6A4A10 }, N_("Corona") },
    };
    static const mh_item_t shirts[] = {
        { CAT_SHIRT, 0, 0, { 0x2E7CF0, 0xF5F5F5, 0xFFC830, 0 }, N_("Rayas azules") },
        { CAT_SHIRT, 0, 30, { 0x3AB04A, 0x3AB04A, 0xFFFFFF, 0 }, N_("Remera verde") },
        { CAT_SHIRT, 0, 30, { 0xE83434, 0xF5F5F5, 0xFFD040, 0 }, N_("Remera roja") },
        { CAT_SHIRT, 0, 50, { 0xFFD040, 0xFFFFFF, 0xE83434, 0 }, N_("Remera amarilla") },
        { CAT_SHIRT, 0, 50, { 0xF08AC0, 0xFFFFFF, 0x8A4AE0, 0 }, N_("Rayas rosa") },
        { CAT_SHIRT, 0, 60, { 0x8A4AE0, 0x6A2AC0, 0xFFD040, 0 }, N_("Violeta con estrella") },
        { CAT_SHIRT, 0, 60, { 0x2A2A34, 0x3A3A48, 0xF0E8C0, 0 }, N_("Negra con luna") },
        { CAT_SHIRT, 0, 80, { 0xF08A20, 0x2A2A2A, 0x3AB04A, 0 }, N_("Calabaza") },
        { CAT_SHIRT, 0, 120, { 0x1A1A5A, 0x8A4AE0, 0x60F0FF, 0 }, N_("Galaxia") },
    };
    static const mh_item_t backs[] = {
        { CAT_BACK, BACK_BACKPACK, 120, { 0x3A8A4A, 0x2A2A2A, 0xC8C8C8, 0x60F0FF }, N_("Mochila") },
        { CAT_BACK, BACK_CAPE, 160, { 0xE83434, 0x5A1A6A, 0xFFD040, 0xFFFFFF }, N_("Capa de héroe") },
        { CAT_BACK, BACK_TANK, 250, { 0x8A8A90, 0xE8C040, 0xC8C8C8, 0x60F0FF }, N_("Mochila cazafantasmas") },
        { CAT_BACK, BACK_WINGS, 220, { 0x4A2A6A, 0x8A4AE0, 0xC8C8C8, 0xFF5AA0 }, N_("Alas de murciélago") },
    };
    static const mh_item_t hands[] = {
        { CAT_HAND, HAND_FLASHLIGHT, 100, { 0xE8C040, 0x2A2A2A, 0xC8C8C8, 0xFFF4B0 }, N_("Linterna") },
        { CAT_HAND, HAND_TORCH, 120, { 0x8A5A30, 0x5A3A1A, 0xC8C8C8, 0xFFB040 }, N_("Antorcha") },
        { CAT_HAND, HAND_BALLOON, 80, { 0xE83434, 0xFFFFFF, 0xC8C8C8, 0xFFFFFF }, N_("Globo") },
        { CAT_HAND, HAND_BUCKET, 90, { 0xF08A20, 0x2A2A2A, 0x3AB04A, 0xFFE080 }, N_("Balde de golosinas") },
    };
    static const mh_item_t pets[] = {
        { CAT_PET, PET_DOG, 0, { 0xC89A60, 0xF5E8D0, 0xE83434, 0xFFD040 }, N_("Perrito") },
        { CAT_PET, PET_CAT, 180, { 0x2A2A30, 0x4A4A58, 0x8A4AE0, 0xFFD040 }, N_("Gatito negro") },
        { CAT_PET, PET_BAT, 220, { 0x5A3A7A, 0xC080E0, 0xE83434, 0xFFD040 }, N_("Murcielaguito") },
    };
    static const mh_item_t trails[] = {
        { CAT_TRAIL, TRAIL_SPARKLES, 100, { 0xFFE070, 0xFFFFFF, 0, 0 }, N_("Chispas") },
        { CAT_TRAIL, TRAIL_STEPS, 80, { 0x3A3028, 0, 0, 0 }, N_("Huellas") },
        { CAT_TRAIL, TRAIL_CONFETTI, 120, { 0xFF5A5A, 0x5AD0FF, 0xFFD040, 0x8AF08A }, N_("Confeti") },
        { CAT_TRAIL, TRAIL_HEARTS, 150, { 0xFF5A8A, 0xFFB0C8, 0, 0 }, N_("Corazones") },
    };
    static const mh_item_t skins[] = {
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0xFFDFC4 }, N_("Piel muy clara") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0xF5C9A6 }, N_("Piel clara") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0xE8B48A }, N_("Piel media") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0xD29B6C }, N_("Piel trigueña") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0xB07A4E }, N_("Piel morena") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0x8D5A36 }, N_("Piel oscura") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0x6A4028 }, N_("Piel muy oscura") },
        { CAT_SKIN, SKIN_FX_NONE, 0, { 0x4A2C1C }, N_("Piel ébano") },
        { CAT_SKIN, SKIN_FX_NONE, 200, { 0x7AE05A }, N_("Marciano") },
        { CAT_SKIN, SKIN_FX_GHOST, 300, { 0xE8F4FF }, N_("Fantasma") },
        { CAT_SKIN, SKIN_FX_NONE, 200, { 0x92AE7C }, N_("Zombi") },
        { CAT_SKIN, SKIN_FX_NONE, 200, { 0xF08A20 }, N_("Calabaza") },
        { CAT_SKIN, SKIN_FX_NONE, 300, { 0xB8BCC8 }, N_("Robot") },
        { CAT_SKIN, SKIN_FX_LAVA, 350, { 0xFF5A20 }, N_("Lava") },
        { CAT_SKIN, SKIN_FX_RAINBOW, 500, { 0xFF4040 }, N_("Arcoíris") },
    };
    static const mh_item_t hairs[] = {
        { CAT_HAIR, 0, 0, { 0x2A1E18 }, N_("Pelo negro") },
        { CAT_HAIR, 0, 0, { 0x5A341C }, N_("Pelo castaño oscuro") },
        { CAT_HAIR, 0, 0, { 0x8A5A30 }, N_("Pelo castaño") },
        { CAT_HAIR, 0, 0, { 0xE8C060 }, N_("Pelo rubio") },
        { CAT_HAIR, 0, 0, { 0xC8502A }, N_("Pelo colorado") },
        { CAT_HAIR, 0, 0, { 0x3A7AE8 }, N_("Pelo azul") },
        { CAT_HAIR, 0, 0, { 0xE870B0 }, N_("Pelo rosa") },
        { CAT_HAIR, 0, 0, { 0x4AC070 }, N_("Pelo verde") },
    };
#define N(a) (int)(sizeof(a) / sizeof(a[0]))
    switch (cat) {
    case CAT_CAP: *n = N(caps); return caps;
    case CAT_SHIRT: *n = N(shirts); return shirts;
    case CAT_BACK: *n = N(backs); return backs;
    case CAT_HAND: *n = N(hands); return hands;
    case CAT_PET: *n = N(pets); return pets;
    case CAT_TRAIL: *n = N(trails); return trails;
    case CAT_SKIN: *n = N(skins); return skins;
    case CAT_HAIR: *n = N(hairs); return hairs;
    default: *n = 0; return NULL;
    }
#undef N
}

int mh_shop_count(int cat)
{
    int n;
    items(cat, &n);
    return n;
}

const mh_item_t *mh_shop_item(int cat, int i)
{
    int n;
    const mh_item_t *t = items(cat, &n);
    return t && i >= 0 && i < n ? &t[i] : NULL;
}

const char *mh_shop_cat_name(int cat)
{
    switch (cat) {
    case CAT_CAP: return _("Gorras");
    case CAT_SHIRT: return _("Remeras");
    case CAT_BACK: return _("Espalda");
    case CAT_HAND: return _("En la mano");
    case CAT_PET: return _("Mascotas");
    case CAT_TRAIL: return _("Estelas");
    case CAT_SKIN: return _("Piel");
    case CAT_HAIR: return _("Pelo");
    default: return "";
    }
}

void mh_shop_apply(const int8_t eq[CAT_N], mh_outfit_t *o, mh_wear_t *w, int *skin_fx, int *trail)
{
    memset(o, 0, sizeof(*o));
    mh_pal_t *b = &o->body;
    const mh_item_t *it;
    it = mh_shop_item(CAT_SKIN, eq[CAT_SKIN]);
    b->c[1] = it ? it->c[0] : 0xF5C9A6;
    *skin_fx = it ? it->style : SKIN_FX_NONE;
    it = mh_shop_item(CAT_HAIR, eq[CAT_HAIR]);
    b->c[2] = it ? it->c[0] : 0x5A341C;
    b->c[3] = 0x1A1414;
    b->c[4] = 0xFFFFFF;
    it = mh_shop_item(CAT_SHIRT, eq[CAT_SHIRT]);
    if (!it) it = mh_shop_item(CAT_SHIRT, 0);
    b->c[5] = it->c[0];
    b->c[6] = it->c[1];
    b->c[7] = it->c[2];
    b->c[8] = 0x2E3A6A;
    b->c[9] = 0xE83434;
    b->c[10] = 0xF2F2F2;
    b->c[11] = 0xFFFFFF;
    b->c[12] = *skin_fx == SKIN_FX_NONE && eq[CAT_SKIN] < 8 ? 0xF09090 : b->c[1];
    it = mh_shop_item(CAT_CAP, eq[CAT_CAP]);
    w->cap = it ? it->style : -1;
    if (it) for (int k = 0; k < 4; k++) o->cap.c[1 + k] = it->c[k];
    it = mh_shop_item(CAT_BACK, eq[CAT_BACK]);
    w->back = it ? it->style : -1;
    if (it) for (int k = 0; k < 4; k++) o->back.c[1 + k] = it->c[k];
    it = mh_shop_item(CAT_HAND, eq[CAT_HAND]);
    w->hand = it ? it->style : -1;
    if (it) for (int k = 0; k < 4; k++) o->hand.c[1 + k] = it->c[k];
    it = mh_shop_item(CAT_PET, eq[CAT_PET]);
    w->pet = it ? it->style : -1;
    if (it) {
        o->pet.c[1] = it->c[0];
        o->pet.c[2] = it->c[1];
        o->pet.c[3] = 0x201818;
        o->pet.c[4] = 0xFFFFFF;
        o->pet.c[5] = it->c[2];
        o->pet.c[6] = it->c[3];
    }
    it = mh_shop_item(CAT_TRAIL, eq[CAT_TRAIL]);
    *trail = it ? it->style : TRAIL_NONE;
}
