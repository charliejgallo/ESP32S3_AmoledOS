/*
 * AmoledOS - Icons as data ("AIC").
 *
 * Every launcher icon is a circle with a gradient and, on top, a handful of
 * shapes. aos_icon.c drew them with C code, one switch case per icon, and a
 * dynamic app could only pick one of those by number: adding an icon meant
 * flashing the firmware. This header describes the same drawings as a short
 * blob of bytes that one interpreter turns into the same LVGL objects, so a
 * .so can carry its icon - or a file on the card can - and the firmware does
 * not need to know it. The study is in docs/ICONS.md.
 *
 * The blob is written in C with the macros below, in the app's source:
 *
 *     static const uint8_t MY_ICON[] = {
 *         AIC_HEADER,
 *         AIC_RECT(AIC_CENTER,  0, -4, 44, 52, 22,   AIC_C_TEXT,          255),
 *         AIC_INTO,                                    // children of the last shape
 *         AIC_RECT(AIC_TOP_MID, -8, 12,  6,  8, AIC_CIRCLE, AIC_C_LIT(0x000000), 255),
 *         AIC_OUT,
 *         AIC_END
 *     };
 *
 * Every coordinate and dimension is a PERCENT of the icon size, as a signed
 * byte: it is exactly the 's * N / 100' the hand-written drawings used, so
 * the same blob draws at 66, 74 and 82 px. A NEGATIVE dimension (width,
 * height, radius, border, diameter, length - never an offset) means
 * 'size / N' instead: AIC_DIV(26) is the 's / 26' border the old drawings
 * used for a hairline, which no percent reproduces at all three sizes.
 * Angles are int16 in tenths of a degree, as LVGL takes them. A dimension
 * that comes out at 0 px but was not written as 0 is drawn at 1 px.
 *
 * Colours are one byte: an index into the palette below, or AIC_C_LIT(rgb)
 * for a literal, which expands to four bytes. The palette is APPEND-ONLY:
 * a .so has the indexes baked in, the same way it has the enum values baked
 * in. That lesson is written at the top of aos_icon_id_t; here it is again.
 */
#pragma once

#include "lvgl.h"
#include "aos_app.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIC_VERSION         1
#define AOS_ICON_OPS_MAX    256     /* bytes per icon, header included */

/* ---- opcodes --------------------------------------------------------------
 * Byte layout after the opcode. 'c' is a colour (1 or 4 bytes), 'i16' two
 * bytes little-endian.
 */
#define AIC_OP_END      0x00    /*                                          */
#define AIC_OP_RECT     0x01    /* align x y w h radius c opa               */
#define AIC_OP_RING     0x02    /* d border c opa                           */
#define AIC_OP_ARC      0x03    /* align x y d w_track w_ind bg_start bg_end ind_start
                                 * ind_end rot (i16 degrees) c_track opa_track c_ind opa_ind */
#define AIC_OP_HAND     0x04    /* w len angle(i16, tenths) c               */
#define AIC_OP_TEXT     0x05    /* font n utf8[n]                           */
#define AIC_OP_ROT      0x06    /* angle(i16, tenths)  -> on the last shape */
#define AIC_OP_BORDER   0x07    /* width c opa         -> on the last shape */
#define AIC_OP_GRAD     0x08    /* c dir               -> on the last shape */
#define AIC_OP_INTO     0x09    /* next shapes are children of the last one */
#define AIC_OP_OUT      0x0A    /* back to the previous parent              */

/* ---- palette (append-only) ----------------------------------------------- */
#define AIC_C_TEXT      0       /* AOS_C_TEXT   0xFFFFFF */
#define AIC_C_BG        1       /* AOS_C_BG     0x000000 */
#define AIC_C_CARD      2       /* AOS_C_CARD   0x1C1C1E */
#define AIC_C_CARD2     3       /* AOS_C_CARD2  0x2C2C2E */
#define AIC_C_DIM       4       /* AOS_C_DIM    0x8E8E93 */
#define AIC_C_ACCENT    5       /* AOS_C_ACCENT 0x0A84FF */
#define AIC_C_GREEN     6       /* AOS_C_GREEN  0x30D158 */
#define AIC_C_RED       7       /* AOS_C_RED    0xFF453A */
#define AIC_C_ORANGE    8       /* AOS_C_ORANGE 0xFF9F0A */
#define AIC_C_YELLOW    9       /* AOS_C_YELLOW 0xFFD60A */
#define AIC_C_PURPLE    10      /* AOS_C_PURPLE 0xBF5AF2 */
#define AIC_C_PINK      11      /* AOS_C_PINK   0xFF375F */
#define AIC_C_TEAL      12      /* AOS_C_TEAL   0x40C8E0 */
#define AIC_C_COUNT     13
#define AIC_C_LITERAL   0xFF    /* followed by R, G, B */

#define AIC_C_LIT(rgb)  AIC_C_LITERAL, (uint8_t)(((rgb) >> 16) & 0xFF), \
                        (uint8_t)(((rgb) >> 8) & 0xFF), (uint8_t)((rgb) & 0xFF)

/* ---- values ---------------------------------------------------------------- */
#define AIC_CIRCLE      0xFF    /* radius: LV_RADIUS_CIRCLE                 */
#define AIC_FONT_BODY   0       /* aos_font_body  (~20 px)                  */
#define AIC_FONT_TITLE  1       /* aos_font_title (~28 px)                  */
#define AIC_GRAD_VER    1       /* LV_GRAD_DIR_VER                          */
#define AIC_GRAD_HOR    2       /* LV_GRAD_DIR_HOR                          */

/* Alignment of a shape inside its parent: LVGL's own lv_align_t values. */
#define AIC_CENTER      LV_ALIGN_CENTER
#define AIC_TOP_MID     LV_ALIGN_TOP_MID
#define AIC_BOTTOM_MID  LV_ALIGN_BOTTOM_MID
#define AIC_LEFT_MID    LV_ALIGN_LEFT_MID
#define AIC_RIGHT_MID   LV_ALIGN_RIGHT_MID
#define AIC_TOP_LEFT    LV_ALIGN_TOP_LEFT
#define AIC_TOP_RIGHT   LV_ALIGN_TOP_RIGHT
#define AIC_BOTTOM_LEFT LV_ALIGN_BOTTOM_LEFT
#define AIC_BOTTOM_RIGHT LV_ALIGN_BOTTOM_RIGHT

/* ---- authoring macros -------------------------------------------------------- */
#define AIC_I8(v)       ((uint8_t)((v) & 0xFF))
#define AIC_I16(v)      ((uint8_t)((v) & 0xFF)), ((uint8_t)(((v) >> 8) & 0xFF))

#define AIC_HEADER      'A', 'I', 'C', AIC_VERSION
#define AIC_END         AIC_OP_END

/* Rounded rectangle, 'align'ed in its parent with an (x, y) offset. */
#define AIC_RECT(align, x, y, w, h, radius, color, opa) \
        AIC_OP_RECT, (uint8_t)(align), AIC_I8(x), AIC_I8(y), AIC_I8(w), AIC_I8(h), \
        (uint8_t)(radius), color, (uint8_t)(opa)

/* Circle of diameter 'd' with a 'border' and no fill, centred. */
#define AIC_RING(d, border, color, opa) \
        AIC_OP_RING, AIC_I8(d), AIC_I8(border), color, (uint8_t)(opa)

/* A dimension as 'size / n' rather than a percent (see the top of the file). */
#define AIC_DIV(n)      (-(n))

/* Arc of diameter 'd', 'align'ed like a RECT. The track runs bg_start..bg_end
 * and the lit part ind_start..ind_end, degrees clockwise from three o'clock
 * plus 'rot'; each has its own width, colour and opacity. The outer activity
 * ring of the old drawings is AIC_ARC(AIC_CENTER, 0, 0, 76, 6, 6, 0, 360, 0, 281,
 * 270, AIC_C_LIT(0x202020), 128, AIC_C_PINK, 255): a dim full track and 78 % of
 * it lit from twelve o'clock. */
#define AIC_ARC(align, x, y, d, w_track, w_ind, bg_start, bg_end, ind_start, ind_end, rot, \
                c_track, opa_track, c_ind, opa_ind) \
        AIC_OP_ARC, (uint8_t)(align), AIC_I8(x), AIC_I8(y), AIC_I8(d), AIC_I8(w_track), \
        AIC_I8(w_ind), AIC_I16(bg_start), AIC_I16(bg_end), AIC_I16(ind_start), \
        AIC_I16(ind_end), AIC_I16(rot), c_track, (uint8_t)(opa_track), c_ind, (uint8_t)(opa_ind)

/* Clock hand: pivot at the parent's centre, 'angle' in tenths of a degree,
 * 0 = twelve o'clock. */
#define AIC_HAND(w, len, angle, color) \
        AIC_OP_HAND, AIC_I8(w), AIC_I8(len), AIC_I16(angle), color

/* A glyph or short text, centred. 'n' is the byte count of the UTF-8 that
 * follows, written out by hand: { AIC_OP_TEXT, AIC_FONT_TITLE, 3, 0xEF, 0xA0, 0x81 } */
#define AIC_TEXT(font, n)   AIC_OP_TEXT, (uint8_t)(font), (uint8_t)(n)

#define AIC_ROT(angle)              AIC_OP_ROT, AIC_I16(angle)
#define AIC_BORDER(width, color, opa) AIC_OP_BORDER, AIC_I8(width), color, (uint8_t)(opa)
#define AIC_GRAD(color, dir)        AIC_OP_GRAD, color, (uint8_t)(dir)
#define AIC_INTO                    AIC_OP_INTO
#define AIC_OUT                     AIC_OP_OUT

/* ---- runtime ---------------------------------------------------------------- */

/* An app hands the runtime its icon, from init(), AFTER setting desc.id:
 *
 *     app->desc.id = "demo.topos";
 *     aos_icon_set_ops(app, MY_ICON, sizeof MY_ICON);
 *
 * The bytes are validated and COPIED into a table of the runtime's own keyed
 * by desc.id, so the blob may live in the .so's rodata, which is gone once
 * the boot probe closes the module - the same reason aos_dynapp copies id
 * and name. init() runs twice for a dynamic app (probe, then open) and the
 * second call overwrites the entry with the same bytes. Returns false, with
 * a log line, for a blob the interpreter would refuse; the app then falls
 * back to desc.icon_vec / desc.icon as before.
 *
 * Why a call and not a field in aos_app_desc_t: a field bumps
 * AOS_ABI_VERSION and every .so on the card stops loading until rebuilt; a
 * call is one new symbol in the firmware's table, and a .so that uses it on
 * an older firmware fails loudly at load instead of silently losing its icon
 * (docs/ICONS.md, 4.1). */
bool aos_icon_set_ops(const aos_app_t *app, const uint8_t *ops, size_t len);

/* Forgets the icon of an app that is being unregistered. */
void aos_icon_clear_ops(const char *id);

/* The blob that will be drawn for this id: a file from the card first, then
 * what the app registered with aos_icon_set_ops(). NULL if neither. */
const uint8_t *aos_icon_ops_for(const char *id, size_t *len);

/* Icon files on the card (or SPIFFS), <desc.id>.aic under
 * aos_hal_path_icons(). Reads them all again, replacing whatever the last
 * scan found; a file that fails validation is logged and skipped. Called at
 * boot and after the portal uploads or deletes one. Returns how many
 * loaded. */
int aos_icon_scan_files(void);

/* Where an id's icon comes from, for the portal's listing. */
typedef enum {
    AOS_ICON_SRC_NONE = 0,      /* icon_vec / glyph from the descriptor */
    AOS_ICON_SRC_APP,           /* aos_icon_set_ops() from the app       */
    AOS_ICON_SRC_FILE,          /* a file under aos_hal_path_icons()     */
} aos_icon_source_t;

aos_icon_source_t aos_icon_source(const char *id);

/* Circular icon with the descriptor's gradient and, on top, the shapes in
 * 'ops'. Same object as aos_icon_create() builds, drawn from data instead of
 * from the switch. A malformed blob stops where the fault is, with a log
 * line, and what was drawn so far stays. */
lv_obj_t *aos_icon_create_ops(lv_obj_t *parent, const aos_app_desc_t *desc,
                              const uint8_t *ops, size_t len, int32_t size);

/* Checks the header and walks the ops without drawing. Returns the number of
 * shapes, or -1 with the offending offset in *bad_at (may be NULL). */
int aos_icon_ops_check(const uint8_t *ops, size_t len, size_t *bad_at);

/* The firmware's own icons, one blob per aos_icon_id_t, generated into
 * aos_icon_tables.c by tools/aic_gen.py from what the old switch drew
 * (docs/ICONS.md, F4). An id with nothing to draw has an empty blob. */
typedef struct {
    const uint8_t *ops;
    uint16_t       len;
} aos_icon_table_t;

extern const aos_icon_table_t aos_icon_tables[AOS_ICON_COUNT];

/* The blob for a built-in id, or NULL for AOS_ICON_NONE / out of range. */
const uint8_t *aos_icon_ops_builtin(aos_icon_id_t id, size_t *len);

#ifdef __cplusplus
}
#endif
