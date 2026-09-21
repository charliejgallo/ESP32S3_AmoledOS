/*
 * CHATARRA - the room engine
 *
 * Paints the background, moves the player and the creatures, finds the path
 * when a cell is touched and decides what happens on arrival.
 *
 * ---------------------------------------------------------------------------
 * WHAT GOES INTO THE BACKGROUND AND WHAT IS REDRAWN
 * ---------------------------------------------------------------------------
 *
 * The background (bg) carries the cells, the decorations and the stationary
 * entities -signs, chests, characters-. It is rebuilt WHOLE on entering a room
 * and when something in the background changes (a chest being opened). It is
 * the only expensive frame and it happens once every few seconds.
 *
 * The frame on screen (fb) starts as a copy of the background and only the
 * things that move are drawn on top: the player's robot and the creatures on
 * patrol. Each one records ONE rectangle enclosing its old position and its
 * new one; anything that moves and does not record it leaves a trail stuck on
 * the screen.
 *
 * ---------------------------------------------------------------------------
 * YOU TOUCH A CELL AND THE ROBOT GOES
 * ---------------------------------------------------------------------------
 *
 * There is no d-pad and no joystick: you touch where you want to go. The path
 * comes from a breadth-first search over the 506 cells, which at this scale is
 * instantaneous and also gives FREE what is needed to touch something you
 * cannot stand on: if the destination is blocked -a character, a chest, the
 * shop counter- you walk to the nearest reachable cell and interact from
 * there. With a "pure" path finder that case would have to be solved
 * separately; with the breadth-first search it is already answered, because it
 * visits cells in order of distance.
 */
#include "chatarra.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

const ch_rect_t ch_entera    = { 0, 0, CH_W, CH_H };
const ch_rect_t ch_mapa_rect = { 0, 0, CH_W, MAP_H };

static const int8_t DX[4] = {  0,  0, -1,  1 };
static const int8_t DY[4] = {  1, -1,  0,  0 };

static void empezar_combate(ch_t *g, const ch_robot_t *rival, int jefe, int origen);
static void lanzar_bicho(ch_t *g, int m);

#define NAMB    ((int)(sizeof(((ch_t *)0)->amb) / sizeof(((ch_t *)0)->amb[0])))
static void amb_nace(ch_t *g, int i, bool arriba);
static void amb_tick(ch_t *g);
static void brillo_tick(ch_t *g);
static void flujo_buscar(ch_t *g);
static void aire_zona(ch_t *g, ch_buf_t *b, int x, int y, int w, int h);

/* --------------------------------------------------------------------------
 * Terrain queries
 * -------------------------------------------------------------------------- */

static char suelo(const ch_room_t *r, int x, int y)
{
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return '0';
    return r->suelo[y][x];
}

/* The entity in a cell, or -1. */
static int ent_en(const ch_room_t *r, int x, int y)
{
    for (int i = 0; i < r->nents; i++) {
        if (r->ents[i].x == x && r->ents[i].y == y) return i;
    }
    return -1;
}

/* Doors are looked up separately because they are the only things occupying
 * SEVERAL cells: 'premio' carries the width. A single-cell door against the
 * edge of the screen cannot be touched with a finger. */
/* Which edge of the map this door sits on, or -1 if it is not on one. The
 * tests are the SAME ones ch_ent_draw() uses to decide between an arrow and a
 * hatch, and they have to stay the same: a door that is drawn as an arrow out
 * of the room and then fades like a doorway is a door that lies. */
int ch_puerta_lado(const ch_ent_t *e)
{
    if (e->y <= 1)        return 1;         /* up    */
    if (e->y >= ROWS - 3) return 0;         /* down  */
    if (e->x <= 1)        return 2;         /* left  */
    if (e->x >= COLS - 2) return 3;         /* right */
    return -1;
}

/* 'premio' is how many cells the opening is, and it runs ALONG THE EDGE the
 * door is on: across for the top and bottom ones, DOWN for the side ones. It
 * used to always run in x, which silently capped every side door at a single
 * cell -24x24 real pixels against the bezel, which is exactly the thing the
 * comment above warns about.
 *
 * And the opening is PUERTA_HONDO cells deep, always growing INWARDS from the
 * edge, because the outermost row of the map is not touchable. The audit's
 * envelope (APP-GUIDE) is y 24..410, x 16..352 of the 368x448 real panel, and
 * a 12 px cell is 24 real: row 0 lives at y 0..23, which is ENTIRELY outside
 * it. That is why you could walk south between sectors and never back north.
 * Column 0 has 8 usable pixels out of 24 and column 14 has 17, which is why
 * the sideways ones "worked" and felt stiff. */
#define PUERTA_HONDO 2

void ch_puerta_caja(const ch_ent_t *e, int *x0, int *y0, int *w, int *h)
{
    int n = e->premio ? e->premio : 1;
    int f = PUERTA_HONDO;

    *x0 = e->x; *y0 = e->y;
    switch (ch_puerta_lado(e)) {
    case 1: *w = n; *h = f;                       break;  /* top:    y .. y+1 */
    case 0: *w = n; *h = f; *y0 = e->y - f + 1;   break;  /* bottom: y-1 .. y */
    case 2: *w = f; *h = n;                       break;  /* left:   x .. x+1 */
    case 3: *w = f; *h = n; *x0 = e->x - f + 1;   break;  /* right:  x-1 .. x */
    default: *w = n; *h = 1;                      break;  /* a door inside a room */
    }
}

static int puerta_en(const ch_room_t *r, int x, int y)
{
    for (int i = 0; i < r->nents; i++) {
        const ch_ent_t *e = &r->ents[i];
        int x0, y0, w, h;
        if (e->tipo != E_PUERTA) continue;
        ch_puerta_caja(e, &x0, &y0, &w, &h);
        if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) {
            return i;
        }
    }
    return -1;
}

/* A defeated enemy stops existing: it neither blocks the way, nor is drawn,
 * nor is touched. An opened chest GOES ON existing -it is furniture- and only
 * changes its picture; that is why a chest's flag is not consulted here. */
static bool ent_viva(const ch_t *g, const ch_ent_t *e)
{
    if (e->tipo == E_ENEMIGO || e->tipo == E_JEFE) {
        return !ch_flag(&g->s, e->p2);
    }
    return true;
}

static bool ent_solida(const ch_t *g, const ch_ent_t *e)
{
    switch (e->tipo) {
    case E_PUERTA:
        return false;                   /* it is walked on, and walking on it crosses it */
    case E_MUEBLE:
        return ch_mueble_solido(e->p1);
    case E_ANIMAL:
        /* It is not where the table says any more: it walks. What blocks the
         * way is where it IS, and that is ocupada_por_bicho(). */
        return false;
    case E_BLOQUEO:
        /* A control blocks the way until its flag is set. That is the whole
         * progression of the game: beating one zone's sub-boss opens the next
         * one's control, and that is two fields of a table. */
        return !ch_flag(&g->s, e->p1);
    default:
        return true;
    }
}

static bool ocupada_por_bicho(const ch_t *g, int x, int y)
{
    for (int i = 0; i < g->nmov; i++) {
        if (g->mov[i].vivo && g->mov[i].x == x && g->mov[i].y == y) return true;
    }
    return false;
}

static bool bloqueado(const ch_t *g, const ch_room_t *r, int x, int y)
{
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return true;
    if (ch_tile_solido(suelo(r, x, y)))           return true;
    if (ch_prop_solido(r, x, y))                  return true;

    int i = ent_en(r, x, y);
    if (i >= 0 && ent_viva(g, &r->ents[i]) && ent_solida(g, &r->ents[i])) {
        return true;
    }
    return ocupada_por_bicho(g, x, y);
}

/* --------------------------------------------------------------------------
 * Background
 * -------------------------------------------------------------------------- */

void ch_map_fondo(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    ch_clip(&g->bg, 0, 0, CH_W, MAP_H);

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            ch_tile_draw(&g->bg, r->suelo[y][x], x, y);
        }
    }
    /* The fringes go in a SECOND pass, after every tile is down: a fringe
     * drawn in the first one would be painted over by the neighbour that comes
     * after it, and half the seams of the room would be missing. */
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            ch_tile_borde(&g->bg, r, x, y);
        }
    }
    for (int i = 0; i < r->nprops; i++) {
        ch_prop_draw(&g->bg, &r->props[i]);
    }
    for (int i = 0; i < r->nents; i++) {
        const ch_ent_t *e = &r->ents[i];
        if (e->tipo == E_ENEMIGO || e->tipo == E_JEFE) continue;  /* they move */
        bool hecho = (e->tipo == E_COFRE)   ? ch_flag(&g->s, e->p3)
                   : (e->tipo == E_BLOQUEO) ? ch_flag(&g->s, e->p1)
                   : false;
        ch_ent_draw(&g->bg, r, e, hecho);
    }

    /* The zone's air goes LAST, over everything: the tiles, the props, the
     * signs. A wash that only covered the ground would make the houses look
     * like they had been cut out of another room and pasted in. */
    aire_zona(g, &g->bg, 0, 0, CH_W, MAP_H);

    ch_clip_none(&g->bg);
}

/* --------------------------------------------------------------------------
 * Entering a room
 * -------------------------------------------------------------------------- */

/* The wash of the room's zone, over a rectangle of a buffer. Anything that
 * repaints a piece of the finished background -the flowing water- has to put
 * it back, or that piece is the only part of the room without air in it. */
static void aire_zona(ch_t *g, ch_buf_t *b, int x, int y, int w, int h)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int z = r->zona;

    if (z < 1 || z > ZONAS) return;
    if (!ch_aire[z - 1].fuerza) return;
    ch_tint(b, x, y, w, h, ch_rgb(ch_aire[z - 1].color), ch_aire[z - 1].fuerza);
}

static void colocar(ch_t *g)
{
    g->px = (int16_t)(g->s.x * TILE - 2);
    g->py = (int16_t)(g->s.y * TILE - 8);
}

void ch_map_entrar(ch_t *g, int sala, int x, int y)
{
    const ch_room_t *r;

    if (sala < 0 || sala >= ch_nsalas) sala = 0;
    /* Clamped into the room, always. A destination outside it is not a
     * theoretical worry: v2 shrank the map from 23x22 cells to 15x14, so every
     * coordinate written for v1 -a save, a door, a starting position- can now
     * point off the edge, and a player standing outside the map cannot walk
     * back into it. */
    if (x < 0) x = 0; else if (x >= COLS) x = COLS - 1;
    if (y < 0) y = 0; else if (y >= ROWS) y = ROWS - 1;
    /* AND IT HAS TO BE A CELL YOU CAN STAND ON.
     *
     * Clamping into the map is not enough: (11,13) is inside a 15x14 room and
     * is also the wall of the house. A player standing in a wall cannot walk
     * out of it -the path finder will not start from a blocked cell- and the
     * game is over without a message. That happened on the board with a
     * position that came from a v1 save.
     *
     * So the arrival looks outwards for the nearest cell that is free. It
     * costs nothing (it happens once per room and nearly always finds the
     * cell it was given) and it turns a class of silent trap into a step
     * sideways. */
    {
        const ch_room_t *dst = &ch_salas[sala];
        if (bloqueado(g, dst, x, y)) {
            for (int rad = 1; rad < COLS; rad++) {
                int hallado = 0;
                for (int dy = -rad; dy <= rad && !hallado; dy++) {
                    for (int dx = -rad; dx <= rad && !hallado; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
                        if (bloqueado(g, dst, nx, ny)) continue;
                        x = nx; y = ny; hallado = 1;
                    }
                }
                if (hallado) break;
            }
        }
    }
    g->s.sala = (uint8_t)sala;
    g->s.x = (uint8_t)x;
    g->s.y = (uint8_t)y;
    r = &ch_salas[sala];

    g->nruta = g->iruta = 0;
    g->andando = 0;
    g->destino_ent = 0xFF;
    colocar(g);
    flujo_buscar(g);

    /* Being here is what unlocks the map's fast travel to here. It is marked
     * on ARRIVAL and not on clearing the zone: the point of going back is
     * usually the workshop, which is open from the first minute. */
    for (int z = 0; z < ZONAS; z++) {
        if (g->s.sala >= ch_zonas_tab[z].sala0 &&
            g->s.sala <= ch_zonas_tab[z].sala1) {
            ch_flag_set(&g->s, ch_zonas_tab[z].visita);
            break;
        }
    }
    ch_map_musica(g);

    /* The room's creatures. The robot is generated HERE and stored: the one
     * you see walking is exactly the one you fight. Rolling it again when the
     * fight starts would be fighting a different one, and it shows. */
    g->nmov = 0;
    for (int i = 0; i < r->nents && g->nmov < MAX_MOV; i++) {
        const ch_ent_t *e = &r->ents[i];
        if (e->tipo != E_ENEMIGO && e->tipo != E_JEFE && e->tipo != E_ANIMAL)
            continue;
        if (e->tipo != E_ANIMAL && ch_flag(&g->s, e->p2)) continue;  /* beaten */

        int m = g->nmov++;
        g->mov[m].idx  = (uint8_t)i;
        g->mov[m].x    = e->x;
        g->mov[m].y    = e->y;
        g->mov[m].casa_x = e->x;
        g->mov[m].casa_y = e->y;
        g->mov[m].px   = (int16_t)(e->x * TILE - 2);
        g->mov[m].py   = (int16_t)(e->y * TILE - 8);
        g->mov[m].dir  = 0;
        g->mov[m].paso = 0;
        g->mov[m].vivo = 1;
        g->mov[m].timer = (uint8_t)(20 + ch_rnd(&g->rng, 40));

        /* An animal has no robot: it walks and that is all it does. Giving it
         * the creatures' loop instead of a loop of its own is what makes it
         * cost nothing -it is already a dirty rectangle in a list that was
         * being drawn anyway- and it is the whole reason a town feels
         * inhabited rather than laid out. */
        if (e->tipo == E_ANIMAL) {
            memset(&g->mov[m].bot, 0, sizeof(g->mov[m].bot));
            continue;
        }

        /* Seed fixed by room and entity: the same creature, always the same. */
        uint32_t semilla = 0x5BD1u + (uint32_t)sala * 977u + (uint32_t)i * 31u;
        ch_robot_random(&g->mov[m].bot, &semilla, e->p3 ? e->p3 : r->zona * 3,
                        e->tipo == E_JEFE ? r->zona + 1 : r->zona);
        /* Seeing it walking already counts: the register is of what you have
         * SEEN, not what you have fought. */
        ch_robot_visto(&g->s, &g->mov[m].bot);

        /* A sub-boss carries a fixed TORSO, which is what gives it its name
         * and elemental type. Without that the eight sub-bosses come out
         * randomly and none of them is distinguishable from the creature you
         * just passed in the corridor. */
        if (e->tipo == E_JEFE && e->p1) {
            g->mov[m].bot.pieza[P_TORSO] = (uint8_t)((e->p1 - 1) % PVAR);
            g->mov[m].bot.skin = (uint8_t)((sala * 5 + 3) % SKINS);
            ch_robot_curar(&g->mov[m].bot);
        }
    }

    /* Autosave. Changing room is the natural point: it happens often, never in
     * the middle of a fight, and it is cheap. A twenty-hour RPG where you have
     * to remember to save by hand loses games. */
    g->quiere_guardar = 1;

    /* The transition covers the room change. Without it the cut is abrupt and
     * you notice they are two different screens and not a world. */
    g->trans = TRANS_N;
    g->trans_dir = 0xFF;            /* a fade unless the caller says otherwise */
    for (int i = 0; i < NAMB; i++) amb_nace(g, i, false);

    g->rehacer_fondo = 1;
    g->hud_sucio = 1;
}

/* --------------------------------------------------------------------------
 * Breadth-first search
 *
 * Returns the length of the route and leaves it in g->ruta as a list of
 * directions. If the destination is blocked, it walks as close as it can:
 * 'tope' is the distance at which it is considered good enough (0 = you have
 * to stand on it).
 * -------------------------------------------------------------------------- */

static uint8_t s_prev[NCELLS];      /* arrival direction + 1, 0 = unseen */
static uint16_t s_cola[NCELLS];

static int buscar_ruta(ch_t *g, int gx, int gy, int tope)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int cab = 0, cola = 0;
    int inicio = g->s.y * COLS + g->s.x;
    int mejor = inicio;
    int mejor_d = (gx > g->s.x ? gx - g->s.x : g->s.x - gx) +
                  (gy > g->s.y ? gy - g->s.y : g->s.y - gy);

    memset(s_prev, 0, sizeof(s_prev));
    s_prev[inicio] = 5;                 /* visited mark with no direction     */
    s_cola[cola++] = (uint16_t)inicio;

    while (cab < cola) {
        int c = s_cola[cab++];
        int cx = c % COLS, cy = c / COLS;

        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
            int n = ny * COLS + nx;
            if (s_prev[n]) continue;
            if (bloqueado(g, r, nx, ny)) continue;

            s_prev[n] = (uint8_t)(d + 1);
            s_cola[cola++] = (uint16_t)n;

            int dist = (gx > nx ? gx - nx : nx - gx) + (gy > ny ? gy - ny : ny - gy);
            if (dist < mejor_d) { mejor_d = dist; mejor = n; }
        }
    }

    if (mejor_d > tope || mejor == inicio) {
        g->nruta = g->iruta = 0;
        return mejor_d <= tope ? 0 : -1;    /* we are already there, or it cannot be reached */
    }

    /* Unwind the path backwards and turn it round. */
    uint8_t tmp[RUTA_MAX];
    int n = 0, c = mejor;
    while (c != inicio && n < RUTA_MAX) {
        int d = s_prev[c] - 1;
        tmp[n++] = (uint8_t)d;
        c = (c / COLS - DY[d]) * COLS + (c % COLS - DX[d]);
    }
    if (c != inicio) { g->nruta = g->iruta = 0; return -1; }   /* it did not fit */

    for (int i = 0; i < n; i++) {
        g->ruta[i] = tmp[n - 1 - i];
    }
    g->nruta = (uint8_t)n;
    g->iruta = 0;
    return n;
}

/* --------------------------------------------------------------------------
 * The touch
 * -------------------------------------------------------------------------- */

void ch_map_toque(ch_t *g, int bx, int by)
{
    g->quieto = 0;
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int tx = bx / TILE, ty = by / TILE;

    if (tx < 0 || tx >= COLS || ty < 0 || ty >= ROWS) return;

    /* Touching yourself opens the menu. It is the way out of the problem that
     * this board's touch panel does not reach right to the bottom and a button
     * bar cannot be put along the bottom edge. */
    if (tx == g->s.x && ty == g->s.y) {
        ch_ui_menu(g);
        return;
    }

    /* A creature: you walk up beside it and fight. If we are already beside
     * it, the route comes out zero-length and the fight has to be fired on the
     * spot: the loop consuming 'destino_ent' only runs when there was a
     * route. */
    for (int i = 0; i < g->nmov; i++) {
        if (!g->mov[i].vivo || g->mov[i].x != tx || g->mov[i].y != ty) continue;
        if (buscar_ruta(g, tx, ty, r->ents[g->mov[i].idx].tipo == E_ANIMAL
                                  ? 2 : 1) >= 0) {
            if (g->nruta == 0) {
                lanzar_bicho(g, i);
            } else {
                g->destino_ent = (uint8_t)(0x80 | i);
            }
        }
        return;
    }

    int i = ent_en(r, tx, ty);
    if (i >= 0 && ent_viva(g, &r->ents[i]) && r->ents[i].tipo != E_PUERTA) {
        /* Up to two cells: enough to talk to somebody standing behind a
         * counter, which is the one case where you cannot get beside them. */
        if (buscar_ruta(g, tx, ty, 2) >= 0) {
            g->destino_ent = (uint8_t)i;
            if (g->nruta == 0) {
                ch_map_interactuar(g, i);
                g->destino_ent = 0xFF;
            }
        }
        return;
    }

    g->destino_ent = 0xFF;
    buscar_ruta(g, tx, ty, 0);
}

/* --------------------------------------------------------------------------
 * What happens on stepping into a cell
 * -------------------------------------------------------------------------- */

static void empezar_combate(ch_t *g, const ch_robot_t *rival, int jefe, int origen)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    g->nruta = g->iruta = 0;
    g->andando = 0;
    g->destino_ent = 0xFF;
    ch_bt_empezar(g, rival, jefe, r->zona);
    g->bt.origen = (uint8_t)origen;     /* after the start: it sets it to 0 */
}

static void al_llegar(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int i = puerta_en(r, g->s.x, g->s.y);

    if (i >= 0) {
        const ch_ent_t *e = &r->ents[i];
        int lado = ch_puerta_lado(e);
        ch_map_entrar(g, e->p1, e->p2, e->p3);
        if (lado >= 0) g->trans_dir = (uint8_t)lado;
        return;
    }

    g->s.pasos++;

    /* Random encounter: only on cells that allow it and only when a cell has
     * just been crossed, not on every frame. */
    if (r->encuentros && ch_tile_encuentro(suelo(r, g->s.x, g->s.y)) &&
        ch_rnd(&g->rng, 256) < r->encuentros) {
        ch_robot_t rival;
        int nv = g->s.yo.nivel + 1 - ch_rnd(&g->rng, 3);
        if (nv < 2) nv = 2;
        ch_robot_random(&rival, &g->rng, nv, r->zona);
        empezar_combate(g, &rival, 0, 0xFF);
    }
}

/* Starts the fight against the room's creature 'm'. If it is a boss, it speaks
 * first: a sub-boss that appears and attacks without saying anything is
 * indistinguishable from any other creature, and that is precisely its only
 * difference. */
static void lanzar_bicho(ch_t *g, int m)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    const ch_ent_t *e = &r->ents[g->mov[m].idx];

    if (e->tipo == E_ANIMAL) {
        if (e->texto) ch_ui_dialogo(g, _(e->texto), g->mov[m].idx, MODO_MAPA);
        ch_sfx(e->p1 == AN_PAJARO ? 1800 : 520, 70);
        return;
    }
    if (e->tipo == E_JEFE && e->texto) {
        g->bt_pendiente = (uint8_t)(m + 1);
        ch_ui_dialogo(g, _(e->texto), g->mov[m].idx, MODO_MAPA);
        return;
    }
    empezar_combate(g, &g->mov[m].bot, e->tipo == E_JEFE, m);
}

/* Called by ch_ui.c when a dialogue closes: the boss's fight starts there. */
void ch_map_dialogo_cerrado(ch_t *g)
{
    /* El puesto de la feria: el texto se lee y despues empieza el juego, por
     * la misma razon que el jefe pelea al CERRAR su dialogo y no al abrirlo. */
    if (g->feria_pend) {
        g->feria_pend = 0;
        ch_fe_entrar(g);
        return;
    }
    if (!g->bt_pendiente) return;
    int m = g->bt_pendiente - 1;
    g->bt_pendiente = 0;
    if (m < g->nmov && g->mov[m].vivo) {
        empezar_combate(g, &g->mov[m].bot, 1, m);
    }
}

/* --------------------------------------------------------------------------
 * Interacting
 * -------------------------------------------------------------------------- */

void ch_map_interactuar(ch_t *g, int idx)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    if (idx < 0 || idx >= r->nents) return;
    const ch_ent_t *e = &r->ents[idx];

    switch (e->tipo) {
    case E_CARTEL:
        if (e->texto) ch_ui_dialogo(g, _(e->texto), idx, MODO_MAPA);
        break;

    case E_FERIA:
        /* El puesto: el texto primero y el juego despues, para que se sepa
         * que se esta por empezar. El dialogo se encarga de llamarnos. */
        if (e->texto) {
            g->feria_pend = 1;
            ch_ui_dialogo(g, _(e->texto), idx, MODO_MAPA);
        } else {
            ch_fe_entrar(g);
        }
        break;

    case E_MUEBLE: {
        /* A piece of furniture always says something, and CAN hide one thing,
         * once: p2 is the flag that remembers it and premio the item. Without
         * the flag it would be a machine for printing potions. */
        bool cobrado = e->p2 && ch_flag(&g->s, e->p2);

        if (e->p2 && !cobrado && e->premio && e->premio < ITEMS) {
            static char linea[96];
            int n = g->s.obj[e->premio] + 1;
            ch_flag_set(&g->s, e->p2);
            g->s.obj[e->premio] = (uint8_t)(n > 99 ? 99 : n);
            ch_sfx(1200, 70);
            g->hud_sucio = 1;
            snprintf(linea, sizeof(linea), _("%s\nENCONTRASTE %s!"),
                     e->texto ? _(e->texto) : "",
                     _(ch_items[e->premio].nombre));
            ch_ui_dialogo(g, linea, idx, MODO_MAPA);
            break;
        }
        if (e->texto) ch_ui_dialogo(g, _(e->texto), idx, MODO_MAPA);
        break;
    }

    case E_PNJ: {
        /* A character with an errand says two things and decides between them
         * with two flags: p3 is what you have to bring them and p2 what they
         * have already collected. The whole quest logic of the game is
         * this. */
        bool listo = (e->p3 == 0) || ch_flag(&g->s, e->p3);
        bool cobrado = e->p2 && ch_flag(&g->s, e->p2);
        const char *txt = (listo && e->texto2) ? e->texto2 : e->texto;

        if (listo && !cobrado) {
            if (e->p2) ch_flag_set(&g->s, e->p2);
            if (e->premio && e->premio < ITEMS) {
                int n = g->s.obj[e->premio] + 1;
                g->s.obj[e->premio] = (uint8_t)(n > 99 ? 99 : n);
                g->s.creditos = (uint16_t)(g->s.creditos + 200 > 9999
                                           ? 9999 : g->s.creditos + 200);
                ch_sfx(1300, 90);
                g->hud_sucio = 1;
            }
        }
        if (txt) ch_ui_dialogo(g, _(txt), idx, MODO_MAPA);
        break;
    }

    case E_COFRE:
        if (ch_flag(&g->s, e->p3)) break;
        ch_flag_set(&g->s, e->p3);
        if (e->p1 < ITEMS) {
            int n = g->s.obj[e->p1] + e->p2;
            g->s.obj[e->p1] = (uint8_t)(n > 99 ? 99 : n);
        }
        g->rehacer_fondo = 1;
        g->cofre_t = 14;
        g->cofre_x = e->x;
        g->cofre_y = e->y;
        ch_sfx(1200, 60);
        {
            static char linea[80];
            snprintf(linea, sizeof(linea), _("ENCONTRASTE %s!"),
                     e->p1 < ITEMS ? _(ch_items[e->p1].nombre) : "");
            ch_ui_dialogo(g, linea, idx, MODO_MAPA);
        }
        break;

    case E_TALLER:
        /* The whole team, not only the one that is out. Walking into town with
         * two wrecks in the bag and being handed back one would be mean for no
         * reason: the workshop is this game's health centre. */
        ch_eq_curar(&g->s);
        g->hud_sucio = 1;
        ch_sfx(880, 80);
        ch_ui_dialogo(g, e->texto ? _(e->texto) :
                      (ch_eq_n(&g->s) > 1 ? _("TU EQUIPO QUEDO COMO NUEVO.")
                                          : _("TU ROBOT QUEDO COMO NUEVO.")),
                      idx, MODO_MAPA);
        break;

    case E_CABINA:
        /* The booth is a place you walk to: from here the watch goes on the
         * air and the other one can be reached. ch_link.c does the rest. */
        ch_sfx(1200, 60);
        ch_lk_entrar(g);
        break;

    case E_BLOQUEO: {
        const char *t = ch_flag(&g->s, e->p1) ? (e->texto2 ? e->texto2 : e->texto)
                                              : e->texto;
        if (t) ch_ui_dialogo(g, _(t), idx, MODO_MAPA);
        break;
    }

    case E_TIENDA:
        g->sel = 0;
        g->scroll = 0;
        g->modo = MODO_TIENDA;
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The passing of time
 * -------------------------------------------------------------------------- */

static void andar_bichos(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    for (int i = 0; i < g->nmov; i++) {
        if (!g->mov[i].vivo) continue;

        if (g->mov[i].paso) {
            /* halfway between two cells */
            g->mov[i].px = (int16_t)(g->mov[i].px + DX[g->mov[i].dir]);
            g->mov[i].py = (int16_t)(g->mov[i].py + DY[g->mov[i].dir]);
            if (--g->mov[i].paso == 0) {
                g->mov[i].x = (uint8_t)(g->mov[i].x + DX[g->mov[i].dir]);
                g->mov[i].y = (uint8_t)(g->mov[i].y + DY[g->mov[i].dir]);
                g->mov[i].px = (int16_t)(g->mov[i].x * TILE - 2);
                g->mov[i].py = (int16_t)(g->mov[i].y * TILE - 8);
            }
            continue;
        }

        /* THEY NOTICE YOU ARE THERE.
         *
         * Only in the dungeons. In the towns and on the roads the creature
         * goes on strolling: if they chased you everywhere there would not be
         * a single place to stop and look at the map, and a game that does not
         * let you breathe tires before it is difficult. In a dungeon, on the
         * other hand, it is precisely what makes you watch where you walk. */
        int dist = (g->s.x > g->mov[i].x ? g->s.x - g->mov[i].x
                                         : g->mov[i].x - g->s.x) +
                   (g->s.y > g->mov[i].y ? g->s.y - g->mov[i].y
                                         : g->mov[i].y - g->s.y);
        bool caza = (r->tema == TEMA_DUNGEON) && dist <= 5 &&
                    r->ents[g->mov[i].idx].tipo != E_JEFE;
        if (caza && !g->mov[i].alerta) {
            g->mov[i].alerta = 1;
            g->mov[i].timer = 12;           /* a moment of surprise          */
            ch_sfx(1500, 40);
            continue;
        }
        if (!caza) g->mov[i].alerta = 0;

        /* It caught you: fight. */
        if (caza && dist <= 1) {
            lanzar_bicho(g, i);
            return;
        }

        if (g->mov[i].timer) { g->mov[i].timer--; continue; }
        g->mov[i].timer = (uint8_t)(caza ? 6 : 25 + ch_rnd(&g->rng, 50));

        /* Bosses do not patrol: they wait. */
        if (r->ents[g->mov[i].idx].tipo == E_JEFE) continue;

        int d;
        if (caza) {
            /* It moves along the axis on which it is furthest away: that reads
             * as chasing you and not as strolling. */
            int dx = g->s.x - g->mov[i].x, dy = g->s.y - g->mov[i].y;
            if ((dx > 0 ? dx : -dx) >= (dy > 0 ? dy : -dy)) d = dx > 0 ? 3 : 2;
            else                                            d = dy > 0 ? 0 : 1;
        } else {
            d = ch_rnd(&g->rng, 4);
        }
        int nx = g->mov[i].x + DX[d], ny = g->mov[i].y + DY[d];
        int lejos = (nx > g->mov[i].casa_x ? nx - g->mov[i].casa_x
                                           : g->mov[i].casa_x - nx) +
                    (ny > g->mov[i].casa_y ? ny - g->mov[i].casa_y
                                           : g->mov[i].casa_y - ny);
        if (!caza && lejos > 3) continue;
        if (bloqueado(g, r, nx, ny)) continue;
        if (nx == g->s.x && ny == g->s.y) continue;

        g->mov[i].dir = (uint8_t)d;
        g->mov[i].paso = TILE;              /* one pixel per frame           */
    }
}

/* EL TEMA DE LA ZONA.
 *
 * El mapa estaba mudo -sonaba el titulo y sonaba el combate- y ocho zonas con
 * ocho cielos y ocho paletas sonaban todas igual, o sea a nada. Se pide al
 * entrar a cada sala, pero solo se cambia si la zona cambio: volver a mandar
 * la misma melodia la reiniciaria en cada puerta, y ocho salas de una zona son
 * ocho reinicios del mismo compas. */
void ch_map_musica(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    uint8_t z = r->zona < 1 ? 1 : (r->zona > ZONAS ? ZONAS : r->zona);
    uint8_t mel = (uint8_t)(CH_MEL_ZONA + z - 1);

    if (g->mel_mapa == mel) return;
    g->mel_mapa = mel;
    ch_snd_melodia(g, mel);
}

void ch_map_tick(ch_t *g)
{
    g->cuadro++;
    if (g->andando || g->nruta) g->quieto = 0;
    else if (g->quieto < 60000)  g->quieto++;
    if (g->trans) g->trans--;
    if (g->cofre_t) g->cofre_t--;
    amb_tick(g);
    brillo_tick(g);

    if (g->andando) {
        int d = g->ruta[g->iruta];
        g->px = (int16_t)(g->px + DX[d] * WALK_STEP);
        g->py = (int16_t)(g->py + DY[d] * WALK_STEP);
        g->paso++;
        g->s.dir = (uint8_t)d;

        if (--g->andando == 0) {
            g->s.x = (uint8_t)(g->s.x + DX[d]);
            g->s.y = (uint8_t)(g->s.y + DY[d]);
            colocar(g);
            g->iruta++;
            al_llegar(g);
            if (g->modo != MODO_MAPA) return;   /* a fight was opened        */
        }
    }

    if (!g->andando && g->iruta < g->nruta) {
        const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
        int d = g->ruta[g->iruta];
        /* A creature may have moved into the way while we were walking. */
        if (bloqueado(g, r, g->s.x + DX[d], g->s.y + DY[d])) {
            g->nruta = g->iruta = 0;
        } else {
            g->andando = TILE / WALK_STEP;
        }
    }

    if (!g->andando && g->iruta >= g->nruta && g->nruta) {
        g->nruta = g->iruta = 0;
        if (g->destino_ent != 0xFF) {
            uint8_t d = g->destino_ent;
            g->destino_ent = 0xFF;
            if (d & 0x80) {
                int m = d & 0x7F;
                if (m < g->nmov && g->mov[m].vivo) {
                    lanzar_bicho(g, m);
                    return;
                }
            } else {
                ch_map_interactuar(g, d);
            }
        }
    }

    andar_bichos(g);
}

/* --------------------------------------------------------------------------
 * Ambience
 *
 * Six particles crossing the room. They do nothing -they are not a weather
 * system and they do not affect the game- but they are the only thing
 * separating "a drawn map" from "a place": the snow falling in Criovalle and
 * the embers rising in the Fundicion are noticed more than any new cell.
 *
 * Each one pays a 4x4 dirty rectangle, that is, 96 pixels between the six:
 * less than 0.1% of the screen.
 * -------------------------------------------------------------------------- */

static void amb_nace(ch_t *g, int i, bool arriba)
{
    int amb = ch_salas[g->s.sala % ch_nsalas].ambiente;

    g->amb[i].x = (int16_t)(ch_rnd(&g->rng, CH_W) * 4);
    switch (amb) {
    case AMB_BRASAS:
        g->amb[i].y  = (int16_t)((arriba ? MAP_H : ch_rnd(&g->rng, MAP_H)) * 4);
        g->amb[i].vx = (int16_t)(ch_rnd(&g->rng, 3) - 1);
        g->amb[i].vy = (int16_t)(-3 - ch_rnd(&g->rng, 3));
        break;
    case AMB_GOTERAS:
        g->amb[i].y  = (int16_t)((arriba ? 0 : ch_rnd(&g->rng, MAP_H)) * 4);
        g->amb[i].vx = 0;
        g->amb[i].vy = (int16_t)(8 + ch_rnd(&g->rng, 6));
        break;
    case AMB_POLVO:
        g->amb[i].y  = (int16_t)((arriba ? 0 : ch_rnd(&g->rng, MAP_H)) * 4);
        g->amb[i].vx = (int16_t)(1 + ch_rnd(&g->rng, 2));
        g->amb[i].vy = (int16_t)(1 + ch_rnd(&g->rng, 2));
        break;
    default:                    /* snow */
        g->amb[i].y  = (int16_t)((arriba ? 0 : ch_rnd(&g->rng, MAP_H)) * 4);
        g->amb[i].vx = (int16_t)(-1 - ch_rnd(&g->rng, 2));
        g->amb[i].vy = (int16_t)(2 + ch_rnd(&g->rng, 3));
        break;
    }
}

static void amb_tick(ch_t *g)
{
    if (!ch_salas[g->s.sala % ch_nsalas].ambiente) return;

    for (int i = 0; i < NAMB; i++) {
        g->amb[i].x = (int16_t)(g->amb[i].x + g->amb[i].vx);
        g->amb[i].y = (int16_t)(g->amb[i].y + g->amb[i].vy);
        if (g->amb[i].y < 0 || g->amb[i].y > MAP_H * 4 ||
            g->amb[i].x < 0 || g->amb[i].x > CH_W * 4) {
            amb_nace(g, i, true);
        }
    }
}

/* Glints on the water and the lava. Three dots born on a randomly chosen cell
 * and fading. They do not animate the background -that would force a repaint-
 * but they are enough for the water to stop looking like a drawing. */
/* --------------------------------------------------------------------------
 * The runs of flowing ground
 *
 * Found once, on entering the room, and never again: the map is const. Long
 * stretches are cut into pieces of FLUJO_MAX so that no single rectangle is
 * wide enough to drag half a row through the flush.
 * -------------------------------------------------------------------------- */
static void flujo_buscar(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    g->nflujo = 0;
    g->flujo_i = 0;
    for (int y = 0; y < ROWS && g->nflujo < CH_FLUJOS; y++) {
        int x = 0;
        while (x < COLS && g->nflujo < CH_FLUJOS) {
            if (!ch_tile_corre(suelo(r, x, y)) || ch_celda_tapada(r, x, y)) {
                x++;
                continue;
            }
            int largo = 0;
            char t = suelo(r, x, y);
            /* A run stops at anything drawn over the ground, or repainting it
             * would wipe what stands there and the thing would blink. */
            while (x + largo < COLS && largo < FLUJO_MAX &&
                   suelo(r, x + largo, y) == t &&
                   !ch_celda_tapada(r, x + largo, y)) {
                largo++;
            }
            g->flujo[g->nflujo].x   = (uint8_t)x;
            g->flujo[g->nflujo].y   = (uint8_t)y;
            g->flujo[g->nflujo].len = (uint8_t)largo;
            g->nflujo++;
            x += largo;
        }
    }
}

/* Repaints FLUJO_POR_CUADRO runs, taking turns. They all read the SAME phase,
 * so a run repainted a frame or two late still lines up with its neighbours:
 * what is staggered is the work, not the picture. */
static void flujo_dibujar(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];
    int fase = (int)((g->cuadro / 6) % TILE);   /* a full cycle is TILE rows */

    if (!g->nflujo) return;
    for (int k = 0; k < FLUJO_POR_CUADRO && k < g->nflujo; k++) {
        int fx = g->flujo[g->flujo_i].x;
        int fy = g->flujo[g->flujo_i].y;
        int fl = g->flujo[g->flujo_i].len;
        for (int i = 0; i < fl; i++) {
            ch_tile_anim(&g->fb, suelo(r, fx + i, fy), fx + i, fy, fase);
            /* and its fringe back on top, or the shore of a pond would blink
             * exactly the way the sign at its edge used to */
            ch_tile_borde(&g->fb, r, fx + i, fy);
        }
        aire_zona(g, &g->fb, fx * TILE, fy * TILE, fl * TILE, TILE);
        ch_dirty_add(&g->d_cur, fx * TILE, fy * TILE, fl * TILE, TILE);
        g->flujo_i = (uint8_t)((g->flujo_i + 1) % g->nflujo);
    }
}

static void brillo_tick(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    for (int i = 0; i < 3; i++) {
        if (g->brillo[i].t) { g->brillo[i].t--; continue; }
        int x = ch_rnd(&g->rng, COLS), y = ch_rnd(&g->rng, ROWS);
        char c = suelo(r, x, y);
        if (c == '~' || c == 'L' || c == 'z' || c == 'h') {
            g->brillo[i].x = (uint8_t)x;
            g->brillo[i].y = (uint8_t)y;
            g->brillo[i].t = (uint8_t)(10 + ch_rnd(&g->rng, 14));
        }
    }
}

static void brillo_dibujar(ch_t *g)
{
    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    for (int i = 0; i < 3; i++) {
        if (g->brillo[i].t < 4) continue;
        int x = g->brillo[i].x * TILE + 3, y = g->brillo[i].y * TILE + 3;
        char c = suelo(r, g->brillo[i].x, g->brillo[i].y);
        uint16_t col = (c == 'L') ? ch_rgb(0xFFE45E) : ch_rgb(0xFFFFFF);
        ch_rect(&g->fb, x, y, 2, 1, col);
        ch_rect(&g->fb, x, y - 1, 1, 3, col);
        ch_dirty_add(&g->d_cur, x - 2, y - 3, 6, 7);
    }
}

static void amb_dibujar(ch_t *g)
{
    int amb = ch_salas[g->s.sala % ch_nsalas].ambiente;
    uint16_t c;
    int r;

    if (!amb) return;
    switch (amb) {
    case AMB_BRASAS:  c = ch_rgb(0xFF9F0A); r = 1; break;
    case AMB_GOTERAS: c = ch_rgb(0x7BE9FF); r = 1; break;
    case AMB_POLVO:   c = ch_rgb(0xC9A96A); r = 1; break;
    default:          c = ch_rgb(0xFFFFFF); r = 1; break;
    }
    for (int i = 0; i < NAMB; i++) {
        int x = g->amb[i].x / 4, y = g->amb[i].y / 4;
        ch_disc(&g->fb, x, y, r, c);
        ch_dirty_add(&g->d_cur, x - 2, y - 2, 5, 5);
    }
}

/* --------------------------------------------------------------------------
 * Drawing what moves
 * -------------------------------------------------------------------------- */

/* ONE rectangle per thing per frame. Two are not needed -the old and the new-
 * because the engine pushes the union of what it dirtied this frame with what
 * it dirtied the previous one, and the previous one already carries the old
 * position. */
static void sucio_mini(ch_t *g, int x, int y)
{
    ch_dirty_add(&g->d_cur, x - 1, y - 1, MINI_W + 2, MINI_H + 2);
}

/* A room's creature is not just its figure: the level hangs nine pixels above
 * it and the alert mark sticks out to the right. The rectangle has to GROW to
 * cover them.
 *
 * It used to be `sucio_mini(g, px, py - 9)`, which MOVED it up nine instead,
 * and left the bottom eight rows of the sprite outside it - so they were never
 * restored from the background. Walking through tall grass a creature left its
 * own legs behind, because there the ground underneath is a different colour
 * and the leftovers show. On plain grass they had been invisible for weeks. */
static void sucio_bicho(ch_t *g, int x, int y)
{
    ch_dirty_add(&g->d_cur, x - 1, y - 10, MINI_W + 2 + 8, MINI_H + 2 + 9);
}

void ch_map_dibujar(ch_t *g)
{
    /* With the dialogue panel open, the clip goes up to where the panel
     * starts. Without this the robot is drawn ON TOP of the text -the panel is
     * part of the background and the moving things come afterwards- and it
     * eats half a word. */
    int bot = (g->modo == MODO_DIALOGO) ? DLG_Y : MAP_H;

    const ch_room_t *r = &ch_salas[g->s.sala % ch_nsalas];

    ch_clip(&g->fb, 0, 0, CH_W, bot);

    /* The ground first: the runs of water and lava go UNDER everything that
     * walks on them, or a robot standing at the water's edge would be sliced
     * by its own scenery a frame later. */
    flujo_dibujar(g);

    for (int i = 0; i < g->nmov; i++) {
        if (!g->mov[i].vivo) continue;
        if (r->ents[g->mov[i].idx].tipo == E_ANIMAL) {
            ch_animal_draw(&g->fb, g->mov[i].px, g->mov[i].py,
                           r->ents[g->mov[i].idx].p1, g->mov[i].dir, g->cuadro);
            continue;
        }
        ch_mini_draw(&g->fb, g->mov[i].px, g->mov[i].py, &g->mov[i].bot,
                     g->mov[i].dir, g->mov[i].paso);
        {
            /* The creature's level, on top. It is the difference between
             * choosing who to fight and finding out by losing. */
            char t[8];
            snprintf(t, sizeof(t), "%d", g->mov[i].bot.nivel);
            ch_text_sh(&g->fb, g->mov[i].px + MINI_W / 2 - ch_text_w(t) / 2,
                       g->mov[i].py - 8, t,
                       g->mov[i].bot.nivel > g->s.yo.nivel + 3
                           ? ch_rgb(0xFF4A3D) : ch_rgb(0xD5DCEB),
                       ch_rgb(0x05060C));
        }
        if (g->mov[i].alerta) {
            /* The exclamation mark. Without it, a creature coming for you is
             * indistinguishable from one strolling, and chasing without
             * warning is unfair. */
            int x = g->mov[i].px + MINI_W / 2 + 6, y = g->mov[i].py - 8;
            ch_rect(&g->fb, x, y, 2, 4, ch_rgb(0xFFE45E));
            ch_rect(&g->fb, x, y + 5, 2, 2, ch_rgb(0xFFE45E));
        }
        sucio_bicho(g, g->mov[i].px, g->mov[i].py);
    }

    /* LA ESPERA. Pasados cinco segundos sin caminar, el robot mira a un lado
     * y al otro y se balancea un pixel. No es una animacion nueva: es la
     * direccion que ya se dibuja, cambiada cada segundo y medio, mas un
     * pixel de alto. Cuesta lo mismo que estar quieto -el rectangulo sucio
     * del jugador ya se empuja todos los cuadros- y es lo que separa un
     * pueblo vivo de una captura de pantalla. */
    {
        int dir = g->s.dir, py = g->py;
        if (g->quieto > QUIETO_ESPERA) {
            uint16_t t = (uint16_t)(g->quieto - QUIETO_ESPERA);
            static const uint8_t MIRA[4] = { 2, 0, 3, 0 };
            dir = MIRA[(t / 45) & 3];
            if (((t / 8) & 3) == 0) py -= 1;
        }
        ch_mini_draw(&g->fb, g->px, py, &g->s.yo, dir, g->paso);
        sucio_mini(g, g->px, py - 1);
    }

    amb_dibujar(g);
    brillo_dibujar(g);

    /* The glint of an opening chest. Fourteen frames and it is gone. */
    if (g->cofre_t) {
        int cx = g->cofre_x * TILE + 4, cy = g->cofre_y * TILE + 4;
        int r = (14 - g->cofre_t) * 2 + 2;
        ch_wave(&g->fb, cx, cy, r, 2, ch_rgb(0xFFE45E), 14 - r / 2);
        ch_dirty_add(&g->d_cur, cx - r - 2, cy - r - 2, r * 2 + 5, r * 2 + 5);
    }

    /* The fade in, for the doors that are doorways. The ones at the edge of
     * the map slide instead, and that is composed in chatarra.c because it
     * needs the frame that is leaving, which the model does not keep. */
    if (g->trans && g->trans_dir == 0xFF) {
        ch_shade(&g->fb, 0, 0, CH_W, MAP_H, -(int)g->trans * 2);
        ch_dirty_add(&g->d_cur, 0, 0, CH_W, MAP_H);
    }

    ch_clip_none(&g->fb);
}

#ifdef AOS_SIM_BUILTIN
/* --------------------------------------------------------------------------
 * World test bench
 *
 * Walks EVERY room and checks the three things that break when a town is added
 * and give no error at all: a door leading to a blocked cell, a door leaving
 * you standing ON TOP of the return door -so the first step sends you back-
 * and a decoration sitting on an entity's cell, which gets drawn twice.
 *
 * It exists because the bug that motivated it -the first town's exit was a
 * single cell against the top edge- could not be seen in the simulator: there
 * the mouse hits a 16x16 target every time. What CAN be checked without the
 * board is everything below.
 *
 * It does not go into the board's binary: it lives entirely inside the #ifdef.
 * -------------------------------------------------------------------------- */
int ch_map_check(void)
{
    int malos = 0;

    for (int si = 0; si < ch_nsalas; si++) {
        const ch_room_t *r = &ch_salas[si];
        ch_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.s.sala = (uint8_t)si;

        for (int i = 0; i < r->nprops; i++) {
            const ch_prop_t *pr = &r->props[i];
            if (pr->x < 0 || pr->x >= COLS || pr->y < 0 || pr->y >= ROWS) {
                aos_hal_log("chatarra", "%s: the decoration at %d,%d is off the map",
                            r->nombre, pr->x, pr->y);
                malos++;
            }
        }

        for (int i = 0; i < r->nents; i++) {
            const ch_ent_t *e = &r->ents[i];

            if (e->tipo == E_PUERTA) {
                int px, py, w, h;
                ch_puerta_caja(e, &px, &py, &w, &h);

                if (e->p1 >= ch_nsalas) {
                    aos_hal_log("chatarra", "room %d: door to a room that does not exist", si);
                    malos++;
                    continue;
                }
                const ch_room_t *d = &ch_salas[e->p1];
                ch_t td;
                memset(&td, 0, sizeof(td));
                td.s.sala = e->p1;

                /* every cell of the opening has to be walkable */
                for (int k = 0; k < w * h; k++) {
                    int cx = px + k % w, cy = py + k / w;
                    if (bloqueado(&tmp, r, cx, cy)) {
                        aos_hal_log("chatarra", "%s: the doorway at %d,%d is blocked",
                                    r->nombre, cx, cy);
                        malos++;
                    }
                }
                /* the destination has to be walkable... */
                if (bloqueado(&td, d, e->p2, e->p3)) {
                    aos_hal_log("chatarra", "%s -> %s: destination %d,%d is blocked",
                                r->nombre, d->nombre, e->p2, e->p3);
                    malos++;
                }
                /* ...and CANNOT be another door, or the first step sends you back */
                if (puerta_en(d, e->p2, e->p3) >= 0) {
                    aos_hal_log("chatarra", "%s -> %s: you land ON TOP of a door at %d,%d",
                                r->nombre, d->nombre, e->p2, e->p3);
                    malos++;
                }
            }

            /* OUTSIDE THE MAP. The v1 -> v2 shrink left a workshop machine
             * at x=15 of a 15-wide map: it drew clipped against the edge and
             * nothing said a word, because until now the check only looked at
             * doors. A coordinate that does not exist is the cheapest kind of
             * bug to find and the most annoying to see. */
            if (e->x < 0 || e->x >= COLS || e->y < 0 || e->y >= ROWS) {
                aos_hal_log("chatarra", "%s: the entity at %d,%d is off the map",
                            r->nombre, e->x, e->y);
                malos++;
                continue;
            }

            /* a decoration on top of an entity is drawn twice */
            if (e->tipo != E_PUERTA && ch_prop_solido(r, e->x, e->y) &&
                e->tipo != E_TALLER) {
                aos_hal_log("chatarra", "%s: prop on top of the entity at %d,%d",
                            r->nombre, e->x, e->y);
                malos++;
            }
        }
    }

    /* And the const tables declared by the enum's cap: C fills the gaps with
     * zeros and says nothing. A NULL name blows up on being drawn, and the
     * symptom only appears when the player picks that item up. */
    for (int i = 1; i < ITEMS; i++) {
        if (!ch_items[i].nombre || !ch_items[i].nombre[0]) {
            aos_hal_log("chatarra", "item %d has no row in ch_items", i);
            malos++;
        }
    }
    for (int i = 0; i < PIEZAS; i++) {
        if (!ch_partes[i].nombre) {
            aos_hal_log("chatarra", "part %d has no row", i);
            malos++;
        }
    }
    for (int i = 0; i < MOVES; i++) {
        if (!ch_moves[i].nombre) {
            aos_hal_log("chatarra", "attack %d has no row", i);
            malos++;
        }
    }

    aos_hal_log("chatarra", "world check: %d problems", malos);
    return malos;
}
#endif
