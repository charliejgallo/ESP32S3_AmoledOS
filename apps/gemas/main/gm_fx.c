/*
 * GEMAS - effects (see gm_fx.h)
 */
#include "gm_fx.h"
#include "aos_theme.h"

#include <string.h>

/* Every LVGL 9 object is born clickable and would eat the board's touch. */
static lv_obj_t *fx_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void gm_fx_init(gm_fx_t *fx, lv_obj_t *parent)
{
    memset(fx, 0, sizeof(*fx));
    fx->parent = parent;

    fx->flash = fx_obj(parent);
    lv_obj_set_size(fx->flash, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(fx->flash, 0, 0);
    lv_obj_set_style_bg_color(fx->flash, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(fx->flash, LV_OPA_TRANSP, 0);
}

void gm_fx_clear(gm_fx_t *fx)
{
    for (int i = 0; i < GM_FX_PARTS; i++) {
        fx->part[i].life = 0;
        if (fx->part[i].obj) {
            lv_obj_add_flag(fx->part[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < GM_FX_RINGS; i++) {
        fx->ring[i].life = 0;
        if (fx->ring[i].obj) {
            lv_obj_add_flag(fx->ring[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < GM_FX_BEAMS; i++) {
        fx->beam[i].life = 0;
        if (fx->beam[i].obj) {
            lv_obj_add_flag(fx->beam[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < GM_FX_TEXTS; i++) {
        fx->text[i].life = 0;
        if (fx->text[i].obj) {
            lv_obj_add_flag(fx->text[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    fx->flash_life = 0;
    fx->shake = 0;
    lv_obj_add_flag(fx->flash, LV_OBJ_FLAG_HIDDEN);
}

/* --------------------------------------------------------------------------
 * Sparks
 * -------------------------------------------------------------------------- */

void gm_fx_burst(gm_fx_t *fx, int x, int y, uint32_t color, int count,
                 int speed, bool gravity)
{
    static uint32_t seed = 0x1234567u;

    for (int k = 0; k < count; k++) {
        gm_part_t *p = NULL;
        for (int i = 0; i < GM_FX_PARTS; i++) {
            if (fx->part[i].life == 0) {
                p = &fx->part[i];
                break;
            }
        }
        if (!p) {
            return;             /* no slot: better to lose a spark */
        }

        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        int ang = (int)(seed % 256u);
        int mag = speed / 2 + (int)((seed >> 8) % (uint32_t)(speed + 1));

        /* sine and cosine by quarters, without tables and without libm: a
         * triangular approximation is enough because they are sparks */
        int s, c;
        {
            int a = ang & 255;
            int tri = (a < 64) ? a * 4 : (a < 192 ? 512 - a * 4 : a * 4 - 1024);
            s = tri;
            a = (ang + 64) & 255;
            tri = (a < 64) ? a * 4 : (a < 192 ? 512 - a * 4 : a * 4 - 1024);
            c = tri;
        }

        if (!p->obj) {
            p->obj = fx_obj(fx->parent);
            lv_obj_set_style_radius(p->obj, LV_RADIUS_CIRCLE, 0);
        }

        p->x = (int16_t)(x * 16);
        p->y = (int16_t)(y * 16);
        p->vx = (int16_t)(c * mag / 256);
        p->vy = (int16_t)(s * mag / 256);
        p->life0 = (uint8_t)(10 + (int)((seed >> 16) % 8u));
        p->life = p->life0;
        p->size = (uint8_t)(4 + (int)((seed >> 20) % 5u));
        p->grav = gravity ? 1 : 0;

        lv_obj_set_style_bg_color(p->obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(p->obj, LV_OPA_COVER, 0);
        lv_obj_set_size(p->obj, p->size, p->size);
        lv_obj_set_pos(p->obj, x - p->size / 2, y - p->size / 2);
        lv_obj_remove_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --------------------------------------------------------------------------
 * Shock waves
 * -------------------------------------------------------------------------- */

void gm_fx_ring(gm_fx_t *fx, int x, int y, uint32_t color, int r0, int r1,
                int frames, int width)
{
    gm_ring_t *g = NULL;
    for (int i = 0; i < GM_FX_RINGS; i++) {
        if (fx->ring[i].life == 0) {
            g = &fx->ring[i];
            break;
        }
    }
    if (!g) {
        return;
    }
    if (!g->obj) {
        g->obj = fx_obj(fx->parent);
        lv_obj_set_style_radius(g->obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g->obj, LV_OPA_TRANSP, 0);
    }

    g->cx = (int16_t)x;
    g->cy = (int16_t)y;
    g->r0 = (int16_t)r0;
    g->r1 = (int16_t)r1;
    g->life0 = (uint8_t)frames;
    g->life = (uint8_t)frames;
    g->width = (uint8_t)width;

    lv_obj_set_style_border_color(g->obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(g->obj, width, 0);
    lv_obj_set_style_border_opa(g->obj, LV_OPA_COVER, 0);
    lv_obj_set_size(g->obj, r0 * 2, r0 * 2);
    lv_obj_set_pos(g->obj, x - r0, y - r0);
    lv_obj_remove_flag(g->obj, LV_OBJ_FLAG_HIDDEN);
}

/* --------------------------------------------------------------------------
 * The star's beams
 * -------------------------------------------------------------------------- */

void gm_fx_beam(gm_fx_t *fx, int x, int y, bool horizontal, uint32_t color,
                int frames)
{
    gm_beam_t *b = NULL;
    for (int i = 0; i < GM_FX_BEAMS; i++) {
        if (fx->beam[i].life == 0) {
            b = &fx->beam[i];
            break;
        }
    }
    if (!b) {
        return;
    }
    if (!b->obj) {
        b->obj = fx_obj(fx->parent);
        lv_obj_set_style_radius(b->obj, 6, 0);
    }

    b->life0 = (uint8_t)frames;
    b->life = (uint8_t)frames;

    lv_obj_set_style_bg_color(b->obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_grad_color(b->obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(b->obj, horizontal ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(b->obj, LV_OPA_COVER, 0);

    if (horizontal) {
        lv_obj_set_size(b->obj, LV_HOR_RES, 6);
        lv_obj_set_pos(b->obj, 0, y - 3);
    } else {
        lv_obj_set_size(b->obj, 6, LV_VER_RES);
        lv_obj_set_pos(b->obj, x - 3, 0);
    }
    lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
}

/* --------------------------------------------------------------------------
 * Flying labels (+150, COMBO x3, ...)
 * -------------------------------------------------------------------------- */

void gm_fx_text(gm_fx_t *fx, int x, int y, const char *txt, uint32_t color,
                bool big)
{
    gm_text_t *t = NULL;
    for (int i = 0; i < GM_FX_TEXTS; i++) {
        if (fx->text[i].life == 0) {
            t = &fx->text[i];
            break;
        }
    }
    if (!t) {
        t = &fx->text[0];       /* the oldest gives up its slot */
    }
    if (!t->obj) {
        t->obj = lv_label_create(fx->parent);
        lv_obj_remove_flag(t->obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(t->obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(t->obj, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_label_set_text(t->obj, txt);
    lv_obj_set_style_text_font(t->obj, big ? aos_font_title : aos_font_body, 0);
    lv_obj_set_style_text_color(t->obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(t->obj, LV_OPA_COVER, 0);
    lv_obj_update_layout(t->obj);

    int w = lv_obj_get_width(t->obj);
    t->x = (int16_t)(x - w / 2);
    t->y = (int16_t)(y * 16);
    t->vy = (int16_t)(big ? -22 : -16);
    t->life0 = (uint8_t)(big ? 30 : 22);
    t->life = t->life0;

    lv_obj_set_pos(t->obj, t->x, y);
    lv_obj_remove_flag(t->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(t->obj);
}

/* -------------------------------------------------------------------------- */

void gm_fx_flash(gm_fx_t *fx, int opa, int frames)
{
    fx->flash_opa   = (uint8_t)opa;
    fx->flash_life0 = (uint8_t)frames;
    fx->flash_life  = (uint8_t)frames;
    lv_obj_set_style_bg_opa(fx->flash, (lv_opa_t)opa, 0);
    lv_obj_remove_flag(fx->flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(fx->flash);
}

void gm_fx_shake(gm_fx_t *fx, int frames)
{
    if (frames > fx->shake) {
        fx->shake = (int8_t)frames;
    }
}

/* --------------------------------------------------------------------------
 * One frame
 * -------------------------------------------------------------------------- */

void gm_fx_step(gm_fx_t *fx)
{
    for (int i = 0; i < GM_FX_PARTS; i++) {
        gm_part_t *p = &fx->part[i];
        if (p->life == 0) {
            continue;
        }
        p->x = (int16_t)(p->x + p->vx);
        p->y = (int16_t)(p->y + p->vy);
        if (p->grav) {
            p->vy = (int16_t)(p->vy + 5);
        }
        p->vx = (int16_t)(p->vx * 15 / 16);

        p->life--;
        if (p->life == 0) {
            lv_obj_add_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        int size = p->size * p->life / p->life0 + 1;
        lv_obj_set_size(p->obj, size, size);
        lv_obj_set_pos(p->obj, p->x / 16 - size / 2, p->y / 16 - size / 2);
        lv_obj_set_style_bg_opa(p->obj, (lv_opa_t)(255 * p->life / p->life0), 0);
    }

    for (int i = 0; i < GM_FX_RINGS; i++) {
        gm_ring_t *g = &fx->ring[i];
        if (g->life == 0) {
            continue;
        }
        g->life--;
        if (g->life == 0) {
            lv_obj_add_flag(g->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int t = (g->life0 - g->life) * 256 / g->life0;      /* 0..256 */
        int r = g->r0 + (g->r1 - g->r0) * t / 256;
        lv_obj_set_size(g->obj, r * 2, r * 2);
        lv_obj_set_pos(g->obj, g->cx - r, g->cy - r);
        lv_obj_set_style_border_opa(g->obj, (lv_opa_t)(255 - 255 * t / 256), 0);
        lv_obj_set_style_border_width(g->obj, 1 + g->width * (256 - t) / 256, 0);
    }

    for (int i = 0; i < GM_FX_BEAMS; i++) {
        gm_beam_t *b = &fx->beam[i];
        if (b->life == 0) {
            continue;
        }
        b->life--;
        if (b->life == 0) {
            lv_obj_add_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int t = (b->life0 - b->life) * 256 / b->life0;
        lv_obj_set_style_bg_opa(b->obj, (lv_opa_t)(255 - 255 * t / 256), 0);
    }

    for (int i = 0; i < GM_FX_TEXTS; i++) {
        gm_text_t *t = &fx->text[i];
        if (t->life == 0) {
            continue;
        }
        t->y = (int16_t)(t->y + t->vy);
        t->vy = (int16_t)(t->vy * 15 / 16);
        t->life--;
        if (t->life == 0) {
            lv_obj_add_flag(t->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(t->obj, t->x, t->y / 16);
        int fade = t->life * 3;
        lv_obj_set_style_text_opa(t->obj, (lv_opa_t)(fade > 255 ? 255 : fade), 0);
    }

    if (fx->flash_life > 0) {
        fx->flash_life--;
        if (fx->flash_life == 0) {
            lv_obj_add_flag(fx->flash, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_bg_opa(fx->flash,
                (lv_opa_t)(fx->flash_opa * fx->flash_life / fx->flash_life0), 0);
        }
    }

    if (fx->shake > 0) {
        fx->shake--;
    }
}
