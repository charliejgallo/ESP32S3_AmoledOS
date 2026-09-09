/*
 * Claudito - the character
 *
 * It is not a fixed sprite but a figure made of parts: body, legs, arms, eyes
 * and mouth are drawn on every frame according to the pose. It comes out
 * smaller than storing thirty bitmaps and it allows squashing, stretching and
 * leaning, which is where nearly all the critter's life comes from.
 *
 * The origin (x, y) is the floor between the legs: that way resting it on a rug
 * or on the grass means setting y = the floor's row.
 */
#pragma once

#include "cl_pixel.h"

#define CL_PET_BODY_W   28
#define CL_PET_BODY_H   18
#define CL_PET_LEG_H    4

typedef enum {
    CL_EYE_OPEN = 0,
    CL_EYE_BLINK,       /* a little line: blinking */
    CL_EYE_HAPPY,       /* ^ ^ */
    CL_EYE_SLEEP,       /* u u */
    CL_EYE_WIDE,        /* surprise */
    CL_EYE_LOOK_L,
    CL_EYE_LOOK_R,
    CL_EYE_SQUINT,      /* stifled laugh */
    CL_EYE_LOVE,        /* hearts */
    CL_EYE_DIZZY,
} cl_eye_t;

typedef enum {
    CL_MOUTH_SMILE = 0,
    CL_MOUTH_TOOTH,     /* the smile with the tooth from the original drawing */
    CL_MOUTH_OPEN,
    CL_MOUTH_BIG,       /* bite */
    CL_MOUTH_CHEW,
    CL_MOUTH_FLAT,
    CL_MOUTH_SAD,
    CL_MOUTH_LAUGH,
    CL_MOUTH_OH,
} cl_mouth_t;

typedef struct {
    int        x, y;        /* floor between the legs, on the stage */
    int        squash;      /* + squashed (falling), - stretched (jumping) */
    int        lean;        /* the body shifts relative to the legs */
    int        step;        /* phase of the legs */
    cl_eye_t   eye;
    cl_mouth_t mouth;
    int        arm_l;       /* 0 down, 1 halfway, 2 up */
    int        arm_r;
    int        face_dx;     /* it looks sideways without turning the body */
    bool       blush;
    bool       shadow;
    int        dirt;        /* 0..3 blotches of dirt */
} cl_pet_t;

void cl_pet_init(cl_pet_t *p);
void cl_pet_draw(cl_buf_t *b, const cl_pet_t *p);

/* Useful points for attaching things: the mouth (food) and the hands (toys) */
void cl_pet_mouth_at(const cl_pet_t *p, int *x, int *y);
void cl_pet_hand_at(const cl_pet_t *p, int right, int *x, int *y);
/* Box of the body, to know whether a touch landed on it */
void cl_pet_bbox(const cl_pet_t *p, int *x, int *y, int *w, int *h);
