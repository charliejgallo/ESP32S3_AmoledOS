/*
 * MONSTER HOP - two players on one screen (the desktop port)
 *
 * The key race of two watches (mh_link.c), with both Tommies here. Each
 * player has a game of their own over the same level and the same seed, so
 * the monsters, the cars and the logs are the same in both; the two are
 * stepped together, and after every step each is told what the other did,
 * exactly what the radio carries between watches: the keys it took (and
 * when, by the level's clock), the levers, the crates, the chests, the exit
 * and where its Tommy is. Each sees the other as the race's ghost.
 *
 * The screen is split down the middle: player 1 on the left, player 2 on the
 * right, each half a view of its own with its own camera, cache and HUD.
 * MH_W is the view's width while the worker draws a half; the frame is
 * a->fw wide.
 */
#ifdef MH_DESKTOP

#include "mh_app.h"
#include "mh_audio.h"
#include "mh_ui.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- player two's clothes: player one's, in other colours ---- */

static void outfit2(app_t *a)
{
    memcpy(a->eq2, a->prog.eq, CAT_N);
    int n = mh_shop_count(CAT_SHIRT);
    if (n > 1) a->eq2[CAT_SHIRT] = (int8_t)((a->prog.eq[CAT_SHIRT] + n / 2) % n);
    /* a cap of the same style (its layer is the one loaded), other colours */
    const mh_item_t *mine = mh_shop_item(CAT_CAP, a->prog.eq[CAT_CAP]);
    if (mine) {
        int nc = mh_shop_count(CAT_CAP);
        for (int k = 1; k < nc; k++) {
            int j = (a->prog.eq[CAT_CAP] + k) % nc;
            const mh_item_t *it = mh_shop_item(CAT_CAP, j);
            if (it && it->style == mine->style) {
                a->eq2[CAT_CAP] = (int8_t)j;
                break;
            }
        }
    }
    mh_wear_t w;
    mh_shop_apply(a->eq2, &a->outfit2, &w, &a->fx2, &a->trail2);
}

/* ---- the lobby ---- */

static void lobby_fill(app_t *a)
{
    char buf[200];
    snprintf(buf, sizeof buf, "%s\n%s", _("Dos jugadores, una pantalla."), mh_desktop_controls());
    mh_ui_lobby_fill(a, buf, mha_level_title(a->link_level), true);
}

void mhs_begin(app_t *a)
{
    a->split = true;
    a->split_go = false;
    if (a->link_level < 0 || !mha_level_open(a, a->link_level)) a->link_level = 0;
    a->link_state = LK_LOBBY;
    lobby_fill(a);
    mha_set_state(a, ST_LOBBY);
}

void mhs_end(app_t *a)
{
    a->split = false;
    a->split_go = false;
    a->link_state = LK_OFF;
}

void mhs_pick(app_t *a, int delta)
{
    int i = a->link_level;
    for (int k = 0; k < MH_LEVELS; k++) {
        i = (i + delta + MH_LEVELS) % MH_LEVELS;
        if (mha_level_open(a, i)) break;
    }
    if (mha_level_open(a, i)) a->link_level = i;
    lobby_fill(a);
}

void mhs_go(app_t *a)
{
    a->link_seed = ((uint32_t)aos_hal_uptime_ms() * 747796405u) | 1u;
    a->split_go = true;
    a->lk_in_r = a->lk_in_w = 0;
    a->lk_out_r = a->lk_out_w = 0;
    mha_level_start(a, a->link_level);
}

/* ---- the level: the second view (the worker, after the first) ---- */

bool mhs_load(app_t *a)
{
    if (!mh_world_init_twin(&a->world2, &a->world)) return false;
    size_t n = (size_t)a->fw * a->fh;
    if (!a->split_px) a->split_px = (uint16_t *)mh_malloc(n * 2);
    if (!a->split_px) return false;
    outfit2(a);
    mh_game_init(&a->game2, &a->lv, DIFF_NORMAL, a->link_seed);
    a->game.link = a->game2.link = true;
    a->game.host = true;
    a->game2.host = false;
    mh_scene_init(&a->scene2, &a->world2, &a->game2, &a->outfit2, a->fx2);
    mh_scene_trail(&a->scene2, a->trail2);
    mh_scene_rival(&a->scene, &a->outfit2.body);
    mh_scene_rival(&a->scene2, &a->outfit.body);
    memset(&a->hs2, 0, sizeof a->hs2);
    a->hs2.msg = -1;
    a->in_hop2 = 0;
    a->in_action2 = false;
    a->exit_told[0] = a->exit_told[1] = false;
    return true;
}

void mhs_free(app_t *a)
{
    mh_world_free(&a->world2);
}

/* ---- the race: what one did, told to the other ---- */

static void tell(app_t *a, mh_game_t *from, mh_game_t *to, int who)
{
    for (int k = 0; k < from->n_out; k++) {
        int kind = from->out[k] >> 8, i = from->out[k] & 0xFF;
        switch (kind) {
        case OUT_KEY:
            if (i < from->n_pick) mh_game_rival_key(to, i, from->pick[i].at);
            break;
        case OUT_LEVER:
            if (i < from->n_lever) mh_game_rival_lever(to, i, from->lever[i].on);
            break;
        case OUT_CRATE:
            if (i < from->n_crate) {
                const mh_crate_t *c = &from->crate[i];
                mh_game_rival_crate(to, i, c->x, c->y, c->z, c->sunk);
            }
            break;
        case OUT_CHEST:
            mh_game_rival_chest(to, i);
            break;
        default:
            break;
        }
    }
    from->n_out = 0;
    if (from->state == GS_WON && !a->exit_told[who]) {
        a->exit_told[who] = true;
        mh_game_rival_exit(to, from->t);
    }
    const mh_hero_t *h = &from->h;
    to->rival = true;
    to->rx = h->x;
    to->ry = h->y;
    to->rz = h->z;
    to->rf = h->dur > 0 ? h->t / h->dur : 0;
    to->rdir = h->dir;
    to->rstate = h->state;
}

static void take_input(mh_game_t *g, volatile int *hop, volatile bool *act)
{
    int h = *hop;
    if (h) {
        *hop = 0;
        mh_game_hop(g, h - 1);
    }
    if (*act) {
        *act = false;
        mh_game_action(g);
    }
}

static void dirty(mh_world_t *w, mh_game_t *g)
{
    for (int k = 0; k < g->n_dirty; k++) mh_world_invalidate_cell(w, g->dirty[k][0], g->dirty[k][1]);
    g->n_dirty = 0;
}

static void events(app_t *a, mh_game_t *g, mh_hud_state_t *hs, float dt)
{
    uint32_t ev = g->events;
    g->events = 0;
    mh_hud_events(hs, ev, dt);
    if (ev) {
        a->ev_ring[a->ev_w & 15] = ev;
        a->ev_w++;
    }
}

/* ---- a half of the screen ---- */

static void view(app_t *a, int v, mh_world_t *w, mh_game_t *g, mh_scene_t *s, mh_dlist_t *dl, mh_hud_state_t *hs,
                 int *px, int *py)
{
    int dirx = s->icam_x > *px ? 1 : s->icam_x < *px ? -1 : 0;
    int diry = s->icam_y > *py ? 1 : s->icam_y < *py ? -1 : 0;
    *px = s->icam_x;
    *py = s->icam_y;
    mh_world_prepare(w, s->icam_x, s->icam_y, dirx, diry, 2);
    hs->pause_icon = false;
    hs->show_title = false;
    mh_img_t im;
    mh_img_init(&im, a->split_px + (size_t)v * MH_W, a->fw, MH_H);
    mh_img_clip(&im, 0, 0, MH_W, MH_H);
    mh_render_band(w, &im, s->icam_x, s->icam_y, 0, MH_H, dl);
    mh_scene_bits_draw(s, w, &im, s->icam_x, s->icam_y);
    mh_hud_draw(&a->hud, &im, g, hs, w, s->icam_x, s->icam_y);
}

void mhs_frame(app_t *a, uint16_t *fb, float dt, float run)
{
    static int px[2], py[2];
    int full = mh_view_w;
    mh_view_w = a->fw / 2;
    mh_game_t *g1 = &a->game, *g2 = &a->game2;
    if (a->playing && !a->frozen) {
        take_input(g1, &a->in_hop, &a->in_action);
        take_input(g2, &a->in_hop2, &a->in_action2);
        /* steps of at most 50 ms, the two together, each told of the other's */
        while (run > 0.0005f) {
            float st = run > 0.05f ? 0.05f : run;
            tell(a, g1, g2, 0);
            tell(a, g2, g1, 1);
            mh_game_step(g1, st);
            mh_game_step(g2, st);
            run -= st;
        }
        tell(a, g1, g2, 0);
        tell(a, g2, g1, 1);
        events(a, g1, &a->hs, dt);
        events(a, g2, &a->hs2, dt);
        dirty(&a->world, g1);
        dirty(&a->world2, g2);
        mh_scene_build(&a->scene, &a->world, g1, &a->cast, &a->dl, dt);
        mh_scene_build(&a->scene2, &a->world2, g2, &a->cast, &a->dl2, dt);
    } else {
        mh_hud_events(&a->hs, 0, dt);
        mh_hud_events(&a->hs2, 0, dt);
        mh_scene_build(&a->scene, &a->world, g1, &a->cast, &a->dl, 0);
        mh_scene_build(&a->scene2, &a->world2, g2, &a->cast, &a->dl2, 0);
    }
    view(a, 0, &a->world, g1, &a->scene, &a->dl, &a->hs, &px[0], &py[0]);
    view(a, 1, &a->world2, g2, &a->scene2, &a->dl2, &a->hs2, &px[1], &py[1]);
    /* the seam */
    for (int y = 0; y < a->fh; y++) {
        uint16_t *row = a->split_px + (size_t)y * a->fw + MH_W - 1;
        row[0] = row[1] = mh_hex(0x0C0A14);
    }
    mh_view_w = full;
    mh_copy_swap(fb, a->split_px, (size_t)a->fw * a->fh);
}

/* ---- the LVGL timer: both are loaded, the race starts ---- */

void mhs_tick(app_t *a)
{
    if (!a->split || a->link_state != LK_LOADING || a->state != ST_LOADING || !a->lk_loaded) return;
    a->link_state = LK_PLAY;
    a->hs2.msg = MSG_GO;
    a->hs2.msg_t = 0;
    mhl_start_play(a);
}

#endif /* MH_DESKTOP */
