/*
 * AmoledOS - Application icons.
 *
 * Every icon is a circle with a gradient and, on top, a glyph from the font or
 * a handful of shapes made of LVGL objects. Zero bitmaps: it takes ~0 flash and
 * scales to any size without looking pixelated.
 *
 * The shapes are DATA: a blob of AIC ops (aos_icon_ops.h) that walk_ops()
 * turns into objects. The firmware's own icons are the tables in
 * aos_icon_tables.c; a dynamic app hands over its own blob with
 * aos_icon_set_ops(); a file on the card overrides either. Until F4 of
 * docs/ICONS.md they were 36 switch cases of hand-written LVGL calls in this
 * file - 25.7 KB of code; tools/aic_gen.py derived the tables from what
 * those cases drew, and tools/icon_golden/ keeps their pixels.
 */
#include "aos_theme.h"
#include "aos_ui.h"
#include "aos_icon_ops.h"
#include "aos_hal.h"
#ifndef AOS_SIM
#include "esp_heap_caps.h"
#endif
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static lv_obj_t *ring(lv_obj_t *parent, int32_t size, int32_t border,
                      lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_center(obj);
    return obj;
}

/* --------------------------------------------------------------------------
 * Icons as data (aos_icon_ops.h, docs/ICONS.md)
 *
 * The interpreter. Every coordinate is a percent of the size, computed as
 * 'size * pct / 100' with int32 arithmetic - the very expression the old
 * hand-written cases used - so a ported drawing lands on the same pixels; a
 * negative dimension is 'size / n' for the hairlines those cases wrote as
 * 's / 26'.
 * -------------------------------------------------------------------------- */


const uint8_t *aos_icon_ops_builtin(aos_icon_id_t id, size_t *len)
{
    if (id <= AOS_ICON_NONE || id >= AOS_ICON_COUNT || !aos_icon_tables[id].ops) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = aos_icon_tables[id].len;
    return aos_icon_tables[id].ops;
}

/* The palette, in the order of aos_icon_ops.h. Append-only. */
static const uint32_t AIC_PALETTE[AIC_C_COUNT] = {
    0xFFFFFF, 0x000000, 0x1C1C1E, 0x2C2C2E, 0x8E8E93, 0x0A84FF, 0x30D158,
    0xFF453A, 0xFF9F0A, 0xFFD60A, 0xBF5AF2, 0xFF375F, 0x40C8E0,
};

/* A reader over the blob. 'bad' is set at the first fault and every read
 * after that yields zeros, so the caller checks once at the end of each op
 * instead of after every byte. */
typedef struct {
    const uint8_t *p;
    size_t         len;
    size_t         at;
    bool           bad;
} aic_rd_t;

static uint8_t rd_u8(aic_rd_t *r)
{
    if (r->bad || r->at >= r->len) {
        r->bad = true;
        return 0;
    }
    return r->p[r->at++];
}

static int8_t rd_i8(aic_rd_t *r)
{
    return (int8_t)rd_u8(r);
}

static int16_t rd_i16(aic_rd_t *r)
{
    uint16_t lo = rd_u8(r);
    uint16_t hi = rd_u8(r);
    return (int16_t)(lo | (hi << 8));
}

static lv_color_t rd_color(aic_rd_t *r)
{
    uint8_t idx = rd_u8(r);
    if (idx == AIC_C_LITERAL) {
        uint32_t cr = rd_u8(r), cg = rd_u8(r), cb = rd_u8(r);
        return lv_color_make((uint8_t)cr, (uint8_t)cg, (uint8_t)cb);
    }
    if (idx >= AIC_C_COUNT) {
        r->bad = true;
        return lv_color_hex(0xFF00FF);   /* loud, so a bad index is seen */
    }
    return lv_color_hex(AIC_PALETTE[idx]);
}

/* 'size * pct / 100'. Offsets only. */
static int32_t pct(int32_t size, int8_t p)
{
    return size * p / 100;
}

/* A dimension: percent when positive, 'size / n' when negative (AIC_DIV).
 * Both truncate like the C the drawings were written in. Something that was
 * asked for but rounds to nothing is drawn at 1 px rather than vanishing. */
static int32_t dim(int32_t size, int8_t p)
{
    if (p == 0) return 0;
    int32_t v = p > 0 ? size * p / 100 : size / -p;
    return v < 1 ? 1 : v;
}

static int32_t radius_px(int32_t size, uint8_t r)
{
    if (r == AIC_CIRCLE) return LV_RADIUS_CIRCLE;
    int8_t p = (int8_t)r;
    if (p == 0) return 0;
    return p > 0 ? size * p / 100 : size / -p;
}

static bool aic_header_ok(const uint8_t *ops, size_t len)
{
    return ops && len >= 4 && len <= AOS_ICON_OPS_MAX &&
           ops[0] == 'A' && ops[1] == 'I' && ops[2] == 'C' && ops[3] == AIC_VERSION;
}

#define AIC_DEPTH   4

/* Walks the blob. With 'base' it draws; with NULL it only validates, which
 * is how aos_icon_ops_check() shares the one parser instead of keeping two
 * in step. Returns the shape count, or -1 with *bad_at at the fault. */
static int walk_ops(lv_obj_t *base, const uint8_t *ops, size_t len,
                    int32_t size, size_t *bad_at)
{
    if (!aic_header_ok(ops, len)) {
        if (bad_at) *bad_at = 0;
        return -1;
    }

    aic_rd_t r = { .p = ops, .len = len, .at = 4, .bad = false };
    lv_obj_t *stack[AIC_DEPTH];
    int       depth = 0;
    lv_obj_t *parent = base;
    lv_obj_t *last = NULL;
    int shapes = 0;
    const bool draw = base != NULL;

    for (;;) {
        size_t op_at = r.at;
        uint8_t op = rd_u8(&r);
        if (r.bad) {
            /* ran off the end without an END */
            if (bad_at) *bad_at = op_at;
            return -1;
        }

        switch (op) {
        case AIC_OP_END:
            return shapes;

        case AIC_OP_RECT: {
            uint8_t align = rd_u8(&r);
            int8_t x = rd_i8(&r), y = rd_i8(&r), w = rd_i8(&r), h = rd_i8(&r);
            uint8_t rad = rd_u8(&r);
            lv_color_t c = rd_color(&r);
            uint8_t opa = rd_u8(&r);
            if (r.bad || align > LV_ALIGN_CENTER) { r.bad = true; break; }
            if (draw) {
                lv_obj_t *o = lv_obj_create(parent);
                lv_obj_remove_style_all(o);
                lv_obj_set_size(o, dim(size, w), dim(size, h));
                lv_obj_set_style_radius(o, radius_px(size, rad), 0);
                lv_obj_set_style_bg_color(o, c, 0);
                lv_obj_set_style_bg_opa(o, opa, 0);
                lv_obj_align(o, (lv_align_t)align, pct(size, x), pct(size, y));
                last = o;
            }
            shapes++;
            break;
        }

        case AIC_OP_RING: {
            int8_t d = rd_i8(&r), b = rd_i8(&r);
            lv_color_t c = rd_color(&r);
            uint8_t opa = rd_u8(&r);
            if (r.bad) break;
            if (draw) {
                last = ring(parent, dim(size, d), dim(size, b), c);
                lv_obj_set_style_border_opa(last, opa, 0);
            }
            shapes++;
            break;
        }

        case AIC_OP_ARC: {
            uint8_t align = rd_u8(&r);
            int8_t x = rd_i8(&r), y = rd_i8(&r);
            int8_t d = rd_i8(&r), wt = rd_i8(&r), wi = rd_i8(&r);
            int16_t bs = rd_i16(&r), be = rd_i16(&r), is = rd_i16(&r), ie = rd_i16(&r);
            int16_t rot = rd_i16(&r);
            lv_color_t ct = rd_color(&r);
            uint8_t ot = rd_u8(&r);
            lv_color_t ci = rd_color(&r);
            uint8_t oi = rd_u8(&r);
            if (r.bad || align > LV_ALIGN_CENTER) { r.bad = true; break; }
            if (draw) {
                lv_obj_t *arc = lv_arc_create(parent);
                lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
                lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_size(arc, dim(size, d), dim(size, d));
                lv_arc_set_rotation(arc, rot);
                lv_arc_set_bg_angles(arc, bs, be);
                lv_arc_set_angles(arc, is, ie);
                lv_obj_set_style_arc_width(arc, dim(size, wt), LV_PART_MAIN);
                lv_obj_set_style_arc_width(arc, dim(size, wi), LV_PART_INDICATOR);
                lv_obj_set_style_arc_color(arc, ct, LV_PART_MAIN);
                lv_obj_set_style_arc_opa(arc, ot, LV_PART_MAIN);
                lv_obj_set_style_arc_color(arc, ci, LV_PART_INDICATOR);
                lv_obj_set_style_arc_opa(arc, oi, LV_PART_INDICATOR);
                lv_obj_align(arc, (lv_align_t)align, pct(size, x), pct(size, y));
                last = arc;
            }
            shapes++;
            break;
        }

        case AIC_OP_HAND: {
            int8_t w = rd_i8(&r), l = rd_i8(&r);
            int16_t angle = rd_i16(&r);
            lv_color_t c = rd_color(&r);
            if (r.bad) break;
            if (draw) {
                last = aos_hand_create(parent, dim(size, w), dim(size, l), c);
                aos_hand_set_angle(last, angle);
            }
            shapes++;
            break;
        }

        case AIC_OP_TEXT: {
            uint8_t font = rd_u8(&r);
            uint8_t n = rd_u8(&r);
            size_t text_at = r.at;
            for (uint8_t i = 0; i < n; i++) rd_u8(&r);
            if (r.bad || n == 0 || n > 15) { r.bad = true; break; }
            if (draw) {
                char text[16];
                memcpy(text, ops + text_at, n);
                text[n] = '\0';
                lv_obj_t *glyph = lv_label_create(parent);
                lv_label_set_text(glyph, text);
                lv_obj_set_style_text_color(glyph, AOS_C_TEXT, 0);
                lv_obj_set_style_text_font(glyph, font == AIC_FONT_TITLE ? aos_font_title
                                                                         : aos_font_body, 0);
                lv_obj_center(glyph);
                last = glyph;
            }
            shapes++;
            break;
        }

        case AIC_OP_ROT: {
            int16_t angle = rd_i16(&r);
            if (r.bad) break;
            if (draw && last) {
                lv_obj_set_style_transform_rotation(last, angle, 0);
                lv_obj_set_style_transform_pivot_x(last, lv_pct(50), 0);
                lv_obj_set_style_transform_pivot_y(last, lv_pct(50), 0);
            }
            break;
        }

        case AIC_OP_BORDER: {
            int8_t w = rd_i8(&r);
            lv_color_t c = rd_color(&r);
            uint8_t opa = rd_u8(&r);
            if (r.bad) break;
            if (draw && last) {
                lv_obj_set_style_border_width(last, dim(size, w), 0);
                lv_obj_set_style_border_color(last, c, 0);
                lv_obj_set_style_border_opa(last, opa, 0);
            }
            break;
        }

        case AIC_OP_GRAD: {
            lv_color_t c = rd_color(&r);
            uint8_t dir = rd_u8(&r);
            if (r.bad || dir > AIC_GRAD_HOR) { r.bad = true; break; }
            if (draw && last) {
                lv_obj_set_style_bg_grad_color(last, c, 0);
                lv_obj_set_style_bg_grad_dir(last, (lv_grad_dir_t)dir, 0);
            }
            break;
        }

        case AIC_OP_INTO:
            if (depth >= AIC_DEPTH || (draw && !last)) { r.bad = true; break; }
            stack[depth++] = parent;
            if (draw) parent = last;
            break;

        case AIC_OP_OUT:
            if (depth == 0) { r.bad = true; break; }
            parent = stack[--depth];
            break;

        default:
            r.bad = true;
            break;
        }

        if (r.bad) {
            if (bad_at) *bad_at = op_at;
            return -1;
        }
    }
}

int aos_icon_ops_check(const uint8_t *ops, size_t len, size_t *bad_at)
{
    return walk_ops(NULL, ops, len, 100, bad_at);
}

/* --------------------------------------------------------------------------
 * Icons brought by the apps themselves (aos_icon_set_ops)
 *
 * One entry per app id, the blob copied. Only dynamic apps use this today
 * (the built-in ones have their tables in flash), so it can never need more
 * than the launcher holds. It was 40 while the loader took 48, the same
 * drift as every other ceiling here; since v0.5.0 it is AOS_MAX_APPS.
 *
 * At that size the blob can no longer sit inside the entry: 256 entries of
 * 300 bytes, twice (this table and the files'), was 150 KB of PSRAM for the
 * sixteen icons a full card brings. So an entry is 48 bytes and its buffer
 * is taken the first time the entry is used, at the full AOS_ICON_OPS_MAX,
 * and then kept for good. Never freed on purpose: the portal's task reads
 * aos_icon_ops_for() while the UI task may be rescanning, and a buffer that
 * stays put can at worst show the old icon, never freed memory.
 * -------------------------------------------------------------------------- */

#define ICON_REG_MAX    AOS_MAX_APPS
#define ICON_REG_ID_MAX 40      /* as dynapp_t.id */

typedef struct {
    char     id[ICON_REG_ID_MAX];
    uint16_t len;               /* 0 = free entry; a blob is never shorter than its header */
    uint8_t *ops;               /* AOS_ICON_OPS_MAX bytes once taken, PSRAM */
} icon_reg_t;

/* The entry's buffer, taken on first use. NULL only if PSRAM is gone. */
static uint8_t *entry_buf(icon_reg_t *e)
{
    if (!e->ops) {
#ifdef AOS_SIM
        e->ops = malloc(AOS_ICON_OPS_MAX);
#else
        e->ops = heap_caps_malloc(AOS_ICON_OPS_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    }
    return e->ops;
}

AOS_BSS_PSRAM static icon_reg_t s_reg[ICON_REG_MAX];

static icon_reg_t *reg_find(const char *id)
{
    if (!id || !id[0]) {
        return NULL;
    }
    for (int i = 0; i < ICON_REG_MAX; i++) {
        if (s_reg[i].len && strcmp(s_reg[i].id, id) == 0) {
            return &s_reg[i];
        }
    }
    return NULL;
}

bool aos_icon_set_ops(const aos_app_t *app, const uint8_t *ops, size_t len)
{
    const char *id = app ? app->desc.id : NULL;
    if (!id || !id[0]) {
        aos_hal_log("icon", "aos_icon_set_ops: set desc.id before the icon");
        return false;
    }
    if (strlen(id) >= ICON_REG_ID_MAX) {
        aos_hal_log("icon", "%s: id too long for the icon table", id);
        return false;
    }
    size_t bad_at = 0;
    int shapes = aos_icon_ops_check(ops, len, &bad_at);
    if (shapes < 0) {
        aos_hal_log("icon", "%s: icon blob refused at byte %u of %u",
                    id, (unsigned)bad_at, (unsigned)len);
        return false;
    }

    icon_reg_t *e = reg_find(id);
    if (!e) {
        for (int i = 0; i < ICON_REG_MAX; i++) {
            if (s_reg[i].len == 0) {
                e = &s_reg[i];
                break;
            }
        }
    }
    if (!e) {
        aos_hal_log("icon", "%s: no room for its icon (%d apps already)", id, ICON_REG_MAX);
        return false;
    }
    if (!entry_buf(e)) {
        aos_hal_log("icon", "%s: no memory for its icon", id);
        return false;
    }
    snprintf(e->id, sizeof(e->id), "%s", id);
    memcpy(e->ops, ops, len);
    e->len = (uint16_t)len;
    aos_hal_log("icon", "%s brought its icon: %u bytes, %d shapes", id, (unsigned)len, shapes);
    return true;
}

void aos_icon_clear_ops(const char *id)
{
    icon_reg_t *e = reg_find(id);
    if (e) {
        e->len = 0;             /* the buffer stays with the entry */
        e->id[0] = '\0';
    }
}

/* --------------------------------------------------------------------------
 * Icons from files (docs/ICONS.md 4.2)
 *
 * A second table, same shape, filled by aos_icon_scan_files() from
 * <id>.aic files. Separate from the apps' table on purpose: deleting the
 * file has to bring back what the app registered, so the two must not
 * overwrite each other. A file can override any app, built-in ones
 * included, so AOS_MAX_APPS entries, with their buffers taken as above.
 * -------------------------------------------------------------------------- */

#define ICON_FILES_MAX  AOS_MAX_APPS
#define ICON_FILE_EXT   ".aic"

AOS_BSS_PSRAM static icon_reg_t s_files[ICON_FILES_MAX];

static icon_reg_t *files_find(const char *id)
{
    if (!id || !id[0]) {
        return NULL;
    }
    for (int i = 0; i < ICON_FILES_MAX; i++) {
        if (s_files[i].len && strcmp(s_files[i].id, id) == 0) {
            return &s_files[i];
        }
    }
    return NULL;
}

int aos_icon_scan_files(void)
{
    for (int i = 0; i < ICON_FILES_MAX; i++) {
        s_files[i].len = 0;     /* buffers kept: see entry_buf() */
    }

    const char *dir_path = aos_hal_path_icons();
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;       /* no directory is the normal case, not an error */
    }

    int loaded = 0, refused = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t n = strlen(name);
        size_t ext = strlen(ICON_FILE_EXT);
        if (n <= ext || strcasecmp(name + n - ext, ICON_FILE_EXT) != 0) {
            continue;
        }
        size_t id_len = n - ext;
        if (id_len >= ICON_REG_ID_MAX) {
            aos_hal_log("icon", "%s: name too long for an app id, skipped", name);
            refused++;
            continue;
        }
        if (loaded >= ICON_FILES_MAX) {
            aos_hal_log("icon", "%s: no room, %d icon files already", name, ICON_FILES_MAX);
            refused++;
            continue;
        }

        char path[192];
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;
        }
        /* One byte past the cap: a file of 257 bytes is refused as too long
         * instead of being read as a valid 256-byte prefix. */
        uint8_t buf[AOS_ICON_OPS_MAX + 1];
        size_t got = fread(buf, 1, sizeof(buf), f);
        fclose(f);

        size_t bad_at = 0;
        int shapes = got > AOS_ICON_OPS_MAX ? -1 : aos_icon_ops_check(buf, got, &bad_at);
        if (shapes < 0) {
            aos_hal_log("icon", "%s: refused (%u bytes, fault at %u)", name,
                        (unsigned)got, (unsigned)bad_at);
            refused++;
            continue;
        }

        icon_reg_t *e = &s_files[loaded];
        if (!entry_buf(e)) {
            aos_hal_log("icon", "%s: no memory for it", name);
            refused++;
            continue;
        }
        loaded++;
        memcpy(e->id, name, id_len);
        e->id[id_len] = '\0';
        memcpy(e->ops, buf, got);
        e->len = (uint16_t)got;
    }
    closedir(dir);

    if (loaded || refused) {
        aos_hal_log("icon", "%d icon file(s) from %s%s", loaded, dir_path,
                    refused ? " (some refused, see above)" : "");
    }
    return loaded;
}

aos_icon_source_t aos_icon_source(const char *id)
{
    if (files_find(id)) return AOS_ICON_SRC_FILE;
    if (reg_find(id))   return AOS_ICON_SRC_APP;
    return AOS_ICON_SRC_NONE;
}

const uint8_t *aos_icon_ops_for(const char *id, size_t *len)
{
    icon_reg_t *e = files_find(id);
    if (!e) {
        e = reg_find(id);
    }
    if (!e) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = e->len;
    return e->ops;
}

/* The circle with the gradient every icon sits on, shared by both builders. */
static lv_obj_t *make_base(lv_obj_t *parent, const aos_app_desc_t *desc, int32_t size)
{
    lv_obj_t *base = lv_obj_create(parent);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, size, size);
    lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(desc->color_a), 0);
    lv_obj_set_style_bg_grad_color(base, lv_color_hex(desc->color_b ? desc->color_b
                                                                   : desc->color_a), 0);
    lv_obj_set_style_bg_grad_dir(base, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_CLICKABLE);
    return base;
}

lv_obj_t *aos_icon_create_ops(lv_obj_t *parent, const aos_app_desc_t *desc,
                              const uint8_t *ops, size_t len, int32_t size)
{
    lv_obj_t *base = make_base(parent, desc, size);
    size_t bad_at = 0;
    if (walk_ops(base, ops, len, size, &bad_at) < 0) {
        aos_hal_log("icon", "%s: bad icon blob at byte %u of %u",
                    desc->id ? desc->id : "?", (unsigned)bad_at, (unsigned)len);
    }
    aos_make_decorative(base);
    return base;
}

lv_obj_t *aos_icon_create(lv_obj_t *parent, const aos_app_desc_t *desc, int32_t size)
{
    lv_obj_t *base = make_base(parent, desc, size);

    /* Precedence (docs/ICONS.md 4.3): the blob the app registered, then a
     * built-in table, then the glyph. */
    size_t ops_len = 0;
    const uint8_t *ops = aos_icon_ops_for(desc->id, &ops_len);
    if (!ops && desc->icon_vec != AOS_ICON_NONE) {
        ops = aos_icon_ops_builtin(desc->icon_vec, &ops_len);
    }

    if (ops) {
        size_t bad_at = 0;
        if (walk_ops(base, ops, ops_len, size, &bad_at) < 0) {
            aos_hal_log("icon", "%s: bad icon blob at byte %u",
                        desc->id ? desc->id : "?", (unsigned)bad_at);
        }
    } else if (desc->icon && desc->icon[0]) {
        lv_obj_t *glyph = lv_label_create(base);
        lv_label_set_text(glyph, desc->icon);
        lv_obj_set_style_text_color(glyph, AOS_C_TEXT, 0);
        lv_obj_set_style_text_font(glyph, size >= 64 ? aos_font_title : aos_font_body, 0);
        lv_obj_center(glyph);
    }

    aos_make_decorative(base);
    return base;
}
