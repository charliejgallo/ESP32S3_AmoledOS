/*
 * MONSTER HOP - reading a level (see mh_level.h for the layout)
 */
#include "mh_level.h"
#include "mh_gfx.h"

#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool mh_level_parse(mh_level_t *lv, const uint8_t *b, uint32_t len)
{
    memset(lv, 0, sizeof(*lv));
    if (len < 20 || memcmp(b, "MHLV", 4) != 0 || b[4] != MH_LV_VERSION) return false;
    lv->zone = b[5];
    lv->w = b[6];
    lv->h = b[7];
    lv->start_x = b[8];
    lv->start_y = b[9];
    lv->start_dir = b[10];
    lv->flags = b[11];
    lv->time_s = rd16(b + 12);
    lv->par_s = rd16(b + 14);
    lv->n_assets = rd16(b + 16);
    lv->n_ents = rd16(b + 18);
    lv->n_paths = rd16(b + 20);
    lv->n_points = rd16(b + 22);
    if (lv->w == 0 || lv->h == 0 || lv->w > MH_LV_MAXW || lv->h > MH_LV_MAXH ||
        lv->n_assets > MH_LV_MAXASSET || lv->n_ents > MH_LV_MAXENT ||
        lv->n_paths > MH_LV_MAXPATH || lv->n_points > MH_LV_MAXPT || lv->zone >= ZONE_N) return false;
    size_t need = 24 + (size_t)lv->n_assets * 32 + (size_t)lv->w * lv->h * 8 + (size_t)lv->n_ents * 12 +
                  (size_t)lv->n_paths * 4 + (size_t)lv->n_points * 2;
    if (need > len) return false;
    const uint8_t *p = b + 24;
    for (int i = 0; i < lv->n_assets; i++, p += 32) {
        memcpy(lv->asset[i], p, 32);
        lv->asset[i][31] = 0;
    }
    lv->cell = (mh_cell_t *)mh_malloc((size_t)lv->w * lv->h * sizeof(mh_cell_t));
    if (!lv->cell) return false;
    memcpy(lv->cell, p, (size_t)lv->w * lv->h * 8);
    p += (size_t)lv->w * lv->h * 8;
    for (int i = 0; i < lv->w * lv->h; i++) {
        mh_cell_t *c = &lv->cell[i];
        if (c->top >= lv->n_assets) c->top = 0;
        if (c->fill >= lv->n_assets) c->fill = 0;
        if (c->prop >= lv->n_assets) c->prop = 0;
        if (c->surf >= lv->n_assets) c->surf = 0;
        if (c->deck >= lv->n_assets) c->deck = 0;
        if (c->h > 3) c->h = 3;
    }
    for (int i = 0; i < lv->n_ents; i++, p += 12) {
        mh_ent_def_t *e = &lv->ent[i];
        e->type = p[0];
        e->x = p[1];
        e->y = p[2];
        e->z = p[3];
        e->dir = p[4];
        e->a = p[5];
        e->b = p[6];
        e->c = p[7];
        e->p0 = rd16(p + 8);
        e->p1 = rd16(p + 10);
    }
    for (int i = 0; i < lv->n_paths; i++, p += 4) {
        lv->path[i].first = rd16(p);
        lv->path[i].n = p[2];
        lv->path[i].flags = p[3];
        if (lv->path[i].first + lv->path[i].n > lv->n_points) lv->path[i].n = 0;
    }
    for (int i = 0; i < lv->n_points; i++, p += 2) {
        lv->pt[i][0] = p[0];
        lv->pt[i][1] = p[1];
    }
    return true;
}

void mh_level_free(mh_level_t *lv)
{
    free(lv->cell);
    lv->cell = NULL;
}
