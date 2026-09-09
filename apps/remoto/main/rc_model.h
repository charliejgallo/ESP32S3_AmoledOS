/*
 * Remoto - configuration model
 *
 * What each button, each gesture and each dial does comes from here, not from
 * the code. The profile is edited by the portal's /remoto page and stored as
 * JSON on the microSD; this unit reads it and leaves it in flat structures.
 *
 * Why a file and not preferences: in NVS a string has a ceiling of a few
 * kilobytes and this profile passes ten with six full pages. Besides, a file
 * can be copied, versioned and edited by hand, which for something the user
 * will be touching often is worth more than the convenience of a key.
 *
 * NVS keeps only the two things the firmware needs to know: Home Assistant's
 * base URL and the token (see rc_ha.h).
 *
 * It deliberately does not depend on LVGL: it compiles with plain 'cc' for the
 * test bench in tools/.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RC_MAX_PAGES     8
#define RC_MAX_BUTTONS   9
#define RC_MAX_STATES    40

#define RC_LEN_LABEL    20
#define RC_LEN_SERVICE  40
#define RC_LEN_ENTITY   64
#define RC_LEN_DATA     96
#define RC_LEN_ATTR     24
#define RC_LEN_UNIT      8
#define RC_LEN_VALUE    24

/* -------------------------------------------------------------------------- */
/* Actions                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    RC_ACT_NONE = 0,
    RC_ACT_SERVICE,     /* POST /api/services/<domain>/<service> */
    RC_ACT_PAGE,        /* internal navigation, does not touch the network */
} rc_act_type_t;

typedef struct {
    uint8_t type;
    char    service[RC_LEN_SERVICE];    /* "light.toggle" */
    char    entity[RC_LEN_ENTITY];      /* empty = the service takes no entity */
    char    data[RC_LEN_DATA];          /* extra JSON WITHOUT braces:
                                           "\"brightness_pct\":40" */
    int16_t page;                       /* destination of RC_ACT_PAGE */
} rc_action_t;

/* -------------------------------------------------------------------------- */
/* Buttons                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    RC_ST_NONE = 0,
    RC_ST_ONOFF,        /* "on"/"off": the button lights up or goes dark */
    RC_ST_VALUE,        /* the state is written under the label          */
    RC_ST_ATTR,         /* the same, but from an attribute               */
} rc_state_mode_t;

typedef struct {
    char        label[RC_LEN_LABEL];
    uint32_t    color;                  /* 0xRRGGBB */
    char        icon[12];               /* optional LVGL glyph */
    rc_action_t tap;
    rc_action_t hold;

    uint8_t     st_mode;                /* rc_state_mode_t */
    char        st_entity[RC_LEN_ENTITY];
    char        st_attr[RC_LEN_ATTR];
    char        unit[RC_LEN_UNIT];
    int8_t      slot;                   /* index into the state table, -1 */
} rc_button_t;

/* -------------------------------------------------------------------------- */
/* Tilt dial                                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    char     service[RC_LEN_SERVICE];   /* "light.turn_on" */
    char     entity[RC_LEN_ENTITY];
    char     field[RC_LEN_ATTR];        /* "brightness_pct" */
    int16_t  min, max;                  /* usable travel of the value */
    uint16_t span_deg;                  /* degrees of wrist from min to max */
    uint8_t  invert;
    uint16_t live_ms;                   /* 0 = only sends on release */

    char     st_entity[RC_LEN_ENTITY];  /* where the current value is read from */
    char     st_attr[RC_LEN_ATTR];      /* "brightness" (0..255) */
    int16_t  st_full;                   /* full scale of the attribute:
                                           255 for brightness, 100 for pct */
    int8_t   slot;
} rc_dial_t;

/* -------------------------------------------------------------------------- */
/* Pages                                                                       */
/* -------------------------------------------------------------------------- */

typedef enum {
    RC_PAGE_GRID = 0,
    RC_PAGE_DIAL,
} rc_page_kind_t;

typedef struct {
    char        name[RC_LEN_LABEL];
    uint8_t     kind;
    uint8_t     cols, rows;
    uint8_t     count;                  /* buttons used out of btn[] */
    rc_button_t btn[RC_MAX_BUTTONS];
    rc_dial_t   dial;
} rc_page_t;

/* -------------------------------------------------------------------------- */
/* Accelerometer gestures                                                      */
/* -------------------------------------------------------------------------- */

typedef enum {
    RC_G_SHAKE = 0,
    RC_G_TILT_L,        /* the left edge drops       */
    RC_G_TILT_R,        /* the right edge drops      */
    RC_G_TILT_F,        /* the board tilts forwards (screen towards the floor) */
    RC_G_TILT_B,        /* backwards (screen towards the ceiling) */
    RC_G_FLIP,          /* face down, held           */
    RC_G_COUNT
} rc_gesture_t;

const char *rc_gesture_name(int g);

typedef struct {
    uint8_t  enabled;
    int16_t  shake_mg;      /* total variation that counts as a shake */
    int16_t  tilt_deg;      /* degrees from the rest position */
    uint16_t hold_ms;       /* how long the face-down has to be held */
    uint16_t cool_ms;       /* dead time after firing */
} rc_gcfg_t;

/* -------------------------------------------------------------------------- */
/* Profile                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    int         version;
    uint16_t    poll_s;                 /* how often the states are re-read */
    uint8_t     n_pages;
    rc_page_t  *pages;                  /* n_pages elements */

    rc_action_t gest[RC_G_COUNT];
    rc_gcfg_t   gcfg;

    /* State table: the union of everything Home Assistant has to be asked
     * about, without duplicates. It is queried in one go with a template (see
     * rc_ha.h), so the fewer entries, the shorter the request. */
    uint8_t     n_states;
    char        st_entity[RC_MAX_STATES][RC_LEN_ENTITY];
    char        st_attr[RC_MAX_STATES][RC_LEN_ATTR];
    char        st_value[RC_MAX_STATES][RC_LEN_VALUE];
    bool        st_valid;               /* at least one reading has arrived */
} rc_profile_t;

/* -------------------------------------------------------------------------- */

/* Parses a profile already read into memory. Returns NULL and writes the
 * reason into 'err' if the JSON is no good. The input buffer is NOT kept. */
rc_profile_t *rc_parse(const char *json, int len, char *err, int errlen);

/* Reads the file and parses it. */
rc_profile_t *rc_load(const char *path, char *err, int errlen);

void rc_free(rc_profile_t *p);

/* Builds the Jinja template that brings every state in one go:
 *   {{states('light.a')}}|{{state_attr('light.b','brightness')}}
 * Returns a malloc()ed buffer, or NULL if there is nothing to query. */
char *rc_build_template(const rc_profile_t *p);

/* Distributes the template's response into the state table. */
void rc_apply_template(rc_profile_t *p, const char *body, int len);

/* Current value of a slot; an empty string if nothing has arrived yet. */
const char *rc_state_value(const rc_profile_t *p, int slot);

/* true if the slot is "on" (or any synonym of switched on). */
bool rc_state_is_on(const rc_profile_t *p, int slot);

/* Builds an action's JSON body: {"entity_id":"...","brillo":50}. Returns the
 * number of bytes written. */
int rc_action_body(const rc_action_t *a, char *out, int outlen);

/* "light.toggle" -> domain "light", service "toggle". false if it has no dot
 * or either half is empty. */
bool rc_split_service(const char *service, char *domain, int dlen,
                      char *name, int nlen);
