/*
 * 2043 - sprites
 *
 * All the small art is ASCII (see the palette in gx_pixel.c). The bosses are
 * not here: they are drawn with primitives in gx_world.c because they have
 * parts that move and rotate, and a bitmap would not do for that.
 *
 * Convention: the enemy ships face downwards, that is, with the nose on the
 * last row, just as on screen.
 */
#pragma once

#include "gx_pixel.h"

typedef struct {
    const char *const *rows;
    int16_t n;      /* rows   */
    int16_t w;      /* width  */
} gx_sprite_t;

/* Enemy types. The order rules: it indexes the sprite table and gx_world.c's
 * toughness/score table. */
typedef enum {
    EN_DRONE = 0,   /* straight down, the most common          */
    EN_WEAVER,      /* zigzag                                  */
    EN_DIVER,       /* hangs at the top and then dives         */
    EN_GUNNER,      /* stops and fires aimed shots             */
    EN_MINE,        /* floats and bursts into shrapnel         */
    EN_HEAVY,       /* takes punishment, fires in a fan        */
    EN_TURRET,      /* descends with the terrain, fires upwards */
    EN_COUNT,
} gx_enemy_kind_t;

extern const gx_sprite_t gx_art_ship;       /* the player's ship, 15x13 */
extern const gx_sprite_t gx_art_ship_icon;  /* the score's thumbnail    */
extern const gx_sprite_t gx_art_enemy[EN_COUNT];

/* Belt debris: three rocks that double as scenery. */
extern const gx_sprite_t gx_art_rock[3];
