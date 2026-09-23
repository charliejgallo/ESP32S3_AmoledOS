/*
 * MILA - the shop's catalogue (see ml_shop.h)
 */
#include "ml_shop.h"

#include "aos_i18n.h"

#include <string.h>

static const ml_item_t s_items[] = {
    { "hat_bow",      N_("Moño"),                CAT_HAT, 60, true },
    { "hat_party",    N_("Gorrito de fiesta"),   CAT_HAT, 80, true },
    { "hat_beanie",   N_("Gorro de lana"),       CAT_HAT, 90, true },
    { "hat_flower",   N_("Flor"),                CAT_HAT, 70, true },
    { "hat_beret",    N_("Boina"),               CAT_HAT, 100, true },
    { "hat_bunny",    N_("Orejas de conejo"),    CAT_HAT, 120, true },
    { "hat_witch",    N_("Sombrero de bruja"),   CAT_HAT, 150, true },
    { "hat_crown",    N_("Corona"),              CAT_HAT, 300, false },
    { "neck_bell",    N_("Collar con cascabel"), CAT_NECK, 0, true },
    { "neck_fish",    N_("Collar con pescadito"), CAT_NECK, 70, true },
    { "neck_bow",     N_("Moño al cuello"),      CAT_NECK, 80, true },
    { "neck_bandana", N_("Pañuelo"),             CAT_NECK, 90, true },
    { "neck_dots",    N_("Pañuelo a lunares"),   CAT_NECK, 110, true },
    { "neck_pearls",  N_("Collar de perlas"),    CAT_NECK, 160, false },
    { "toy_mouse",    N_("Ratoncito"),           CAT_TOY, 40, false },
    { "toy_yarn",     N_("Ovillo"),              CAT_TOY, 50, false },
    { "toy_ball",     N_("Pelota con cascabel"), CAT_TOY, 60, false },
    { "toy_feather",  N_("Caña con pluma"),      CAT_TOY, 70, false },
    { "toy_box",      N_("Caja de cartón"),      CAT_TOY, 80, false },
    { "toy_catnip",   N_("Plantita de catnip"),  CAT_TOY, 100, false },
    { "toy_tunnel",   N_("Túnel"),               CAT_TOY, 120, false },
    { "toy_post",     N_("Rascador"),            CAT_TOY, 150, false },
    { "toy_hammock",  N_("Hamaca de ventana"),   CAT_TOY, 180, false },
    { "toy_fishbowl", N_("Pecera"),              CAT_TOY, 200, false },
};

#define NITEMS ((int)(sizeof s_items / sizeof s_items[0]))

int ml_shop_count(int cat)
{
    int n = 0;
    for (int i = 0; i < NITEMS; i++)
        if (s_items[i].cat == cat) n++;
    return n;
}

const ml_item_t *ml_shop_item(int cat, int k)
{
    for (int i = 0; i < NITEMS; i++) {
        if (s_items[i].cat != cat) continue;
        if (k-- == 0) return &s_items[i];
    }
    return NULL;
}

const ml_item_t *ml_shop_find(const char *id)
{
    for (int i = 0; i < NITEMS; i++)
        if (!strcmp(s_items[i].id, id)) return &s_items[i];
    return NULL;
}

uint32_t ml_shop_colour(int i)
{
    static const uint32_t c[ML_NCOLOURS] = {
        0xF0648C,   /* pink    */
        0xE0282C,   /* red     */
        0xFF9A3A,   /* orange  */
        0xFFD24A,   /* yellow  */
        0x5AD7A5,   /* mint    */
        0x3C6EF0,   /* blue    */
        0x9A55E6,   /* purple  */
        0xF4F2EE,   /* white   */
    };
    return c[(unsigned)i % ML_NCOLOURS];
}
