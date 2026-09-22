/*
 * GOLF - what the golfer wears, and the shop that sells it
 *
 * The golfer is rendered once in Blender with every region tagged (skin,
 * hair, shirt, trousers, shoes, club...: tools/blender/SPEC.md), so an
 * outfit is just a colour per region plus a hat model. Items are bought with
 * the coins rounds earn; owned items and the one worn in each category live
 * in preferences.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* region ids of the renders */
enum {
    RG_NONE = 0, RG_SKIN, RG_HAIR, RG_SHIRT_A, RG_SHIRT_B, RG_PANTS_A, RG_PANTS_B,
    RG_SHOES_A, RG_SHOES_B, RG_BELT, RG_GLOVE, RG_CLUBHEAD, RG_SHAFT, RG_GRIP,
    RG_HAT_A, RG_HAT_B, RG_N,
};

enum {
    CAT_SHIRT = 0,
    CAT_PANTS,
    CAT_HAT,
    CAT_SHOES,
    CAT_CLUBS,
    CAT_LOOK,           /* skin tone and hair: free, a choice not a purchase */
    CAT_N,
};

typedef struct {
    const char *name;       /* N_() marked                                    */
    uint16_t    price;      /* 0 = owned from the start                       */
    int8_t      hat;        /* CAT_HAT: model 0..5, -1 = none                 */
    uint32_t    c1, c2;     /* the two colours of the category's regions      */
    uint32_t    c3;         /* CAT_CLUBS: the grip; CAT_PANTS: the belt;
                               CAT_LOOK: the hair                             */
} gf_item_t;

/* Tables are static and reached through functions, never as extern data:
 * the firmware's .so loader drops the addend of R_XTENSA_GLOB_DAT, so code
 * in another file reading "table + 4" gets "table + 0" and every field comes
 * out shifted (measured: carries of 24,848 yards). A static table is only
 * touched by R_XTENSA_RELATIVE, which the loader does right. */
const gf_item_t *gf_item(int cat, int item);
int         gf_item_n(int cat);
const char *gf_cat(int cat);                        /* N_() marked           */

typedef struct {
    uint8_t  eq[CAT_N];         /* the item worn in each category             */
    uint32_t own[CAT_N];        /* bit per item                               */
    int32_t  coins;
} gf_wardrobe_t;

void gf_wardrobe_default(gf_wardrobe_t *w);
bool gf_owns(const gf_wardrobe_t *w, int cat, int item);

/* The 16 region colours and the hat model an outfit gives. 'eq' may differ
 * from the wardrobe's (the shop previews before buying). */
void gf_outfit_palette(const uint8_t eq[CAT_N], uint32_t pal[RG_N], int *hat);

/* Outfits for the other players of a game on one watch, so they can be told
 * apart: player 1..3. */
void gf_outfit_guest(int player, uint8_t eq[CAT_N]);
