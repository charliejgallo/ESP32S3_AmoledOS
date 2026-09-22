/*
 * MONSTER HOP - the shop's catalogue and what Tommy wears
 *
 * Everything is palette over the same renders: a cap is a style (a layer
 * rendered once) and four colours; a shirt is three colours on the body's
 * shirt regions; the odd skins (a ghost, lava, a rainbow...) are colours
 * plus an effect the renderer adds. Normal skin tones and hair colours are
 * free; the rest is bought with the coins of the levels.
 */
#pragma once

#include "mh_art.h"
#include "mh_cast.h"
#include "mh_scene.h"

#include <stdbool.h>
#include <stdint.h>

enum { CAT_CAP = 0, CAT_SHIRT, CAT_BACK, CAT_HAND, CAT_PET, CAT_TRAIL, CAT_SKIN, CAT_HAIR, CAT_N };

/* the renderer's effects for a skin */
enum { SKIN_FX_NONE = 0, SKIN_FX_GHOST, SKIN_FX_LAVA, SKIN_FX_RAINBOW };
/* trails */
enum { TRAIL_NONE = 0, TRAIL_SPARKLES, TRAIL_STEPS, TRAIL_CONFETTI, TRAIL_HEARTS };

typedef struct {
    uint8_t     cat;
    int8_t      style;          /* cap / back / hand / pet style, trail, skin effect */
    uint16_t    price;          /* 0 = free, owned from the start            */
    uint32_t    c[4];           /* the colours it brings                     */
    const char *name;           /* Spanish, translated with _() when shown   */
} mh_item_t;

int mh_shop_count(int cat);
const mh_item_t *mh_shop_item(int cat, int i);
const char *mh_shop_cat_name(int cat);  /* translated */

/* eq[cat] = the equipped item of each category (-1 = none where allowed):
 * builds the palettes and the layers to load */
void mh_shop_apply(const int8_t eq[CAT_N], mh_outfit_t *o, mh_wear_t *w, int *skin_fx, int *trail);
