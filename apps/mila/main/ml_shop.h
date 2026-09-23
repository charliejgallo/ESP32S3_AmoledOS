/*
 * MILA - the shop's catalogue: hats, neck items, toys
 *
 * Item ids are the art's names (hat_party, neck_bell, toy_post) and what
 * the saves keep (ml_prog.own). A world's gift (the worlds table) is not for
 * sale: it comes with finishing that world. Colours cost nothing once the
 * item is owned.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { CAT_HAT = 0, CAT_NECK, CAT_TOY, CAT_N };

typedef struct {
    const char *id;
    const char *name;           /* Spanish, N_(): translate with _()        */
    uint8_t     cat;
    int16_t     price;
    bool        colour;         /* region 1 can be recoloured              */
} ml_item_t;

#define ML_NCOLOURS 8

int  ml_shop_count(int cat);
const ml_item_t *ml_shop_item(int cat, int i);
const ml_item_t *ml_shop_find(const char *id);
uint32_t ml_shop_colour(int i);         /* 0xRRGGBB                       */
