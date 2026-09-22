/*
 * TURBO - the race (see tb_game.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "tb_game.h"
#include "tb_gfx.h"

#include <math.h>
#include <string.h>

#define CAR_HW          0.95f       /* half the player's width                */
#define CAR_LEN         4.4f
#define BRAKE_DECEL     26.0f
#define STEER_MAX       13.0f       /* m/s sideways at full lock and speed    */
#define CENTRIFUGAL     0.0135f     /* sideways m/s per (m/s)^2 per curve     */
#define OFF_LIMIT       14.0f       /* how far off the road the car may go    */

static const tb_car_spec_t *specs(void)
{
    /* static behind a function: see tb_track.c on GLOB_DAT */
    static const tb_car_spec_t s[CAR_N] = {
        { "Stiletto",  86.0f,  9.8f, 1.00f, 0.45f, 0.30f,    0 },
        { "Bulldog",   80.0f, 12.6f, 0.90f, 0.50f, 0.35f,  600 },
        { "Rallye",    77.0f, 11.0f, 1.30f, 0.72f, 0.35f,  900 },
        { "Mule",      72.0f,  9.6f, 1.05f, 0.88f, 0.60f, 1200 },
    };
    return s;
}

const tb_car_spec_t *tb_car_spec(int car)
{
    if (car < 0 || car >= CAR_N) car = 0;
    return &specs()[car];
}

float tb_road_hw(const tb_seg_t *s)
{
    return s->lanes == 2 ? 4.2f : TB_ROAD_HW;
}

static float lane_x(int lanes, int lane)
{
    if (lanes == 2) return lane ? 1.9f : -1.9f;
    return (float)(lane - 1) * TB_LANE_W;
}

float tb_track_y(const tb_track_t *t, float z)
{
    float fs = z / TB_SEG_LEN;
    int i = tb_ifloor(fs);
    float f = fs - (float)i;
    float y0 = tb_seg(t, i)->y, y1 = tb_seg(t, i + 1)->y;
    return y0 + (y1 - y0) * f;
}

int tb_game_kmh(const tb_game_t *g)
{
    return (int)(g->v * 3.6f + 0.5f);
}

float tb_game_progress(const tb_game_t *g)
{
    float fin = (float)g->trk->cp_seg[g->trk->ncp - 1] * TB_SEG_LEN;
    float p = g->z / fin;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
}

/* --------------------------------------------------------------------------
 * Traffic
 * -------------------------------------------------------------------------- */

static int traffic_count(int diff)
{
    return diff == DIFF_EASY ? 10 : (diff == DIFF_NORMAL ? 14 : TB_TRAFFIC);
}

static void traffic_spawn(tb_game_t *g, tb_traffic_t *c, float z)
{
    const tb_seg_t *s = tb_seg(g->trk, (int)(z / TB_SEG_LEN));
    int lanes = s->lanes;
    c->z = z;
    c->lane = (uint8_t)(tb_rand(&g->rng) % (uint32_t)lanes);
    c->x = c->tx = lane_x(lanes, c->lane);
    uint32_t r = tb_rand(&g->rng) % 100u;
    /* mostly cars; trucks and vans are slower and in the right lanes */
    if (r < 30) c->model = VH_SEDAN;
    else if (r < 55) c->model = VH_COMPACT;
    else if (r < 72) c->model = VH_VAN;
    else if (r < 86) c->model = VH_TRUCK;
    else c->model = (uint8_t)(tb_rand(&g->rng) % CAR_N);
    float base = c->model == VH_TRUCK ? 22.0f : (c->model == VH_VAN ? 26.0f : 30.0f);
    if (c->model < CAR_N) base = 40.0f;
    c->v = base + tb_randf(&g->rng) * 10.0f + (g->diff == DIFF_HARD ? 4.0f : 0.0f);
    c->paint = (uint8_t)(tb_rand(&g->rng) % 16u);
    c->braking = false;
}

static void traffic_init(tb_game_t *g)
{
    g->ntraffic = traffic_count(g->diff);
    for (int i = 0; i < g->ntraffic; i++) {
        /* none near the start line: the first ones a little way up the road */
        float z = 120.0f + (float)i * (760.0f / (float)g->ntraffic) + tb_randf(&g->rng) * 30.0f;
        traffic_spawn(g, &g->traffic[i], z);
    }
}

static void traffic_step(tb_game_t *g, float dt)
{
    float fin = (float)g->trk->nreal * TB_SEG_LEN;
    for (int i = 0; i < g->ntraffic; i++) {
        tb_traffic_t *c = &g->traffic[i];
        const tb_seg_t *s = tb_seg(g->trk, (int)(c->z / TB_SEG_LEN));
        if (c->lane >= s->lanes) {
            c->lane = (uint8_t)(s->lanes - 1);
            c->tx = lane_x(s->lanes, c->lane);
        }
        /* keep behind the car in front in the same lane */
        float want = c->v;
        c->braking = false;
        for (int j = 0; j < g->ntraffic; j++) {
            if (j == i) continue;
            tb_traffic_t *o = &g->traffic[j];
            float dz = o->z - c->z;
            if (dz > 0 && dz < 22.0f && fabsf(o->x - c->x) < 2.2f && o->v < c->v) {
                /* change lane if the next one is free, else slow down */
                int nl = c->lane + ((tb_rand(&g->rng) & 1) ? 1 : -1);
                if (nl < 0) nl = 1;
                if (nl >= s->lanes) nl = s->lanes - 2;
                bool free_lane = true;
                float nx = lane_x(s->lanes, nl);
                for (int k = 0; k < g->ntraffic; k++) {
                    if (k != i && fabsf(g->traffic[k].x - nx) < 2.0f && fabsf(g->traffic[k].z - c->z) < 18.0f) free_lane = false;
                }
                if (free_lane && fabsf(c->x - c->tx) < 0.3f) {
                    c->lane = (uint8_t)nl;
                    c->tx = nx;
                } else {
                    want = o->v;
                    c->braking = true;
                }
            }
        }
        float step = want * dt;
        c->z += step;
        float d = c->tx - c->x;
        float lat = 2.2f * dt;
        c->x += d > lat ? lat : (d < -lat ? -lat : d);
        /* left behind, or too far ahead: back into the pool, up the road */
        if (c->z < g->z - 60.0f || c->z > g->z + 1100.0f) {
            float nz = g->z + 780.0f + tb_randf(&g->rng) * 250.0f;
            if (nz > fin + 400.0f) nz = g->z - 200.0f;      /* past the finish: park it behind */
            traffic_spawn(g, c, nz);
        }
    }
}

/* --------------------------------------------------------------------------
 * The car
 * -------------------------------------------------------------------------- */

void tb_game_start(tb_game_t *g, const tb_track_t *trk, int car, int diff, uint32_t seed)
{
    bool rival = g->rival_on;
    memset(g, 0, sizeof(*g));
    g->rival_on = rival;
    g->trk = trk;
    g->car = car;
    g->diff = diff;
    g->rng = seed ? seed : 1;
    g->z = 8.0f * TB_SEG_LEN - 7.0f;       /* just behind the start gantry   */
    g->x = 0;
    g->gear = 1;
    g->state = RS_COUNTDOWN;
    float k = diff == DIFF_EASY ? 1.25f : (diff == DIFF_HARD ? 0.86f : 1.0f);
    g->time_left = trk->start_time * k;
    g->last_tick = 4;
    traffic_init(g);
}

static void hit_props(tb_game_t *g)
{
    const tb_track_t *t = g->trk;
    int s0 = (int)(g->z / TB_SEG_LEN);
    for (int si = s0; si <= s0 + 1; si++) {
        const tb_seg_t *s = tb_seg(t, si);
        for (int p = 0; p < s->nprop; p++) {
            const tb_prop_t *pr = &t->prop[s->prop0 + p];
            if (!(pr->flags & PF_SOLID)) continue;
            float px = (float)pr->x10 * 0.1f;
            float hw = tb_prop_halfw(pr->kind);
            float pz = (float)si * TB_SEG_LEN;
            if (pz < g->z || pz > g->z + CAR_LEN) continue;
            if (fabsf(px - g->x) > hw + CAR_HW) continue;
            /* a hit: most of the speed is gone, the car is thrown back towards the road */
            const tb_car_spec_t *sp = tb_car_spec(g->car);
            if (g->v > 12.0f) {
                g->v *= sp->tough;
                g->crash = g->v > 8.0f ? 1.2f : 0.6f;
                g->crashes++;
                g->events |= EV_CRASH;
            } else {
                g->v *= 0.5f;
                g->events |= EV_BUMP;
            }
            g->bump = 1.0f;
            g->x += px > g->x ? -(hw + CAR_HW - fabsf(px - g->x) + 0.4f) : (hw + CAR_HW - fabsf(px - g->x) + 0.4f);
            return;
        }
    }
}

static void hit_traffic(tb_game_t *g)
{
    for (int i = 0; i < g->ntraffic; i++) {
        tb_traffic_t *c = &g->traffic[i];
        float len = c->model == VH_TRUCK ? 7.5f : 4.5f;
        float hw = c->model == VH_TRUCK ? 1.25f : 0.95f;
        float dz = c->z - g->z;                     /* its rear minus ours     */
        if (dz > CAR_LEN || dz < -len) continue;
        float dx = c->x - g->x;
        if (fabsf(dx) > hw + CAR_HW) continue;
        if (dz > CAR_LEN - 1.6f && g->v > c->v) {
            /* ran into its back */
            float hard = g->v - c->v;
            g->v = c->v * 0.85f;
            g->z = c->z - CAR_LEN;
            if (hard > 20.0f) {
                g->crash = 0.8f;
                g->crashes++;
                g->events |= EV_CRASH;
            } else {
                g->events |= EV_BUMP;
            }
            g->bump = hard > 20.0f ? 1.0f : 0.6f;
            c->v += hard * 0.3f;
        } else {
            /* side by side: pushed apart */
            float push = hw + CAR_HW - fabsf(dx) + 0.1f;
            g->x += dx > 0 ? -push : push;
            g->v *= 0.94f;
            g->bump = 0.5f;
            g->events |= EV_BUMP;
        }
    }
}

static void step_car(tb_game_t *g, float dt)
{
    const tb_car_spec_t *sp = tb_car_spec(g->car);
    const tb_track_t *t = g->trk;
    const tb_seg_t *s = tb_seg(t, (int)((g->z + CAR_LEN * 0.5f) / TB_SEG_LEN));
    float hw = tb_road_hw(s);
    bool racing = g->state == RS_RACING;
    bool gas = racing && g->in_gas && g->crash <= 0.3f;
    bool brake = g->in_brake || g->state == RS_FINISHED || g->state == RS_TIMEUP;

    bool off = fabsf(g->x) > hw + TB_RUMBLE_W * 0.5f;
    if (off && !g->offroad) g->events |= EV_OFFROAD;
    g->offroad = off;
    float vmax = sp->vmax * (off ? sp->offroad : 1.0f);

    /* speed */
    if (brake) {
        g->v -= BRAKE_DECEL * (g->state == RS_RACING ? 1.0f : 0.5f) * dt;
    } else if (gas) {
        float r = g->v / vmax;
        float a = sp->accel * (1.0f - r * r);
        if (g->v > vmax) a = -12.0f;
        g->v += a * dt;
    } else {
        g->v -= (1.2f + g->v * 0.03f) * dt;             /* rolling, and the air */
    }
    if (g->v > vmax && !brake) g->v -= (off ? 22.0f : 6.0f) * dt;
    if (g->v < 0) g->v = 0;
    if (g->crash > 0) {
        g->crash -= dt;
        g->spin += dt * 9.0f * (g->crash > 0 ? 1.0f : 0.0f);
    } else {
        g->spin = 0;
    }

    /* sideways: the player steers, the bend pushes out */
    float vk = g->v / 30.0f;
    if (vk > 1.0f) vk = 1.0f;
    float steer = g->in_steer;
    if (!racing && g->state != RS_FINISHED) steer = 0;
    float lateral = steer * STEER_MAX * sp->grip * vk;
    float push = CENTRIFUGAL * g->v * g->v * s->curve;
    if (off) push *= 0.8f;
    g->x += (lateral - push) * dt;
    g->slide = push / (STEER_MAX * sp->grip + 0.01f);
    if (g->x > hw + OFF_LIMIT) g->x = hw + OFF_LIMIT;
    if (g->x < -hw - OFF_LIMIT) g->x = -hw - OFF_LIMIT;

    /* the visual yaw: where the nose points, eased */
    float want = steer * 0.8f * (g->v > 2.0f ? 1.0f : 0.0f) + g->slide * 0.35f;
    if (want > 1) want = 1;
    if (want < -1) want = -1;
    g->yaw += (want - g->yaw) * (dt * 8.0f > 1.0f ? 1.0f : dt * 8.0f);

    float oldz = g->z;
    g->z += g->v * dt;
    if (g->bump > 0) g->bump -= dt * 2.5f;
    if (g->bump < 0) g->bump = 0;
    if (off && g->v > 10.0f && g->bump < 0.25f) g->bump = 0.25f;

    /* the camera follows the car sideways with a little lag */
    float lag = dt * 6.0f;
    if (lag > 1) lag = 1;
    g->cam_x += (g->x - g->cam_x) * lag;

    /* gears and revs, for the sound and the dial */
    float gv = g->v / sp->vmax;
    static const float up[5] = { 0.22f, 0.40f, 0.58f, 0.76f, 2.0f };
    int gear = 1;
    while (gear < 5 && gv > up[gear - 1]) gear++;
    if (gear != g->gear) {
        g->gear = gear;
        g->events |= EV_GEAR;
    }
    float lo = gear == 1 ? 0.0f : up[gear - 2], hi = up[gear - 1] > 1.0f ? 1.05f : up[gear - 1];
    g->rpm = 0.25f + 0.75f * (gv - lo) / (hi - lo);
    if (g->rpm > 1.0f) g->rpm = 1.0f;
    if (g->state == RS_COUNTDOWN && g->in_gas) g->rpm = 0.85f;

    /* overtaking */
    for (int i = 0; i < g->ntraffic; i++) {
        tb_traffic_t *c = &g->traffic[i];
        if (oldz + CAR_LEN * 0.5f < c->z && g->z + CAR_LEN * 0.5f >= c->z && fabsf(c->x - g->x) < 4.0f) {
            g->passes++;
            g->events |= EV_PASS;
        }
    }

    hit_traffic(g);
    hit_props(g);
}

void tb_game_step(tb_game_t *g, float dt)
{
    const tb_track_t *t = g->trk;
    while (dt > 0) {
        float h = dt > (1.0f / 60.0f) ? (1.0f / 60.0f) : dt;
        dt -= h;
        g->t_state += h;
        switch (g->state) {
        case RS_COUNTDOWN: {
            int left = 3 - (int)g->t_state;
            if (left < g->last_tick && left >= 1) {
                g->last_tick = left;
                g->events |= EV_COUNT;
            }
            if (g->t_state >= 3.0f) {
                g->state = RS_RACING;
                g->t_state = 0;
                g->events |= EV_GO;
                g->last_tick = 11;
            }
            break;
        }
        case RS_RACING: {
            g->elapsed += h;
            g->time_left -= h;
            int tick = (int)g->time_left + 1;
            if (g->time_left < 10.0f && tick < g->last_tick) {
                g->last_tick = tick;
                g->events |= EV_LOW_TIME;
            }
            if (g->time_left <= 0) {
                g->time_left = 0;
                g->state = RS_TIMEUP;
                g->t_state = 0;
                g->events |= EV_TIMEUP;
            }
            break;
        }
        default:
            break;
        }
        step_car(g, h);
        traffic_step(g, h);
        if (g->v * 3.6f > g->top_speed) g->top_speed = g->v * 3.6f;

        /* checkpoints: crossed by the car's nose */
        if (g->state == RS_RACING && g->next_cp < t->ncp) {
            float cz = (float)t->cp_seg[g->next_cp] * TB_SEG_LEN;
            if (g->z + CAR_LEN >= cz) {
                g->split[g->next_cp] = g->elapsed;
                if (g->next_cp == t->ncp - 1) {
                    g->state = RS_FINISHED;
                    g->t_state = 0;
                    g->events |= EV_FINISH;
                } else {
                    float k = g->diff == DIFF_EASY ? 1.2f : (g->diff == DIFF_HARD ? 0.85f : 1.0f);
                    g->cp_added = t->cp_time[g->next_cp] * k;
                    g->time_left += g->cp_added;
                    g->events |= EV_CHECKPOINT;
                    g->last_tick = 11;
                }
                g->next_cp++;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * The bot (the test bench, and the simulator's TB_AUTO)
 * -------------------------------------------------------------------------- */

/* a bot: steers towards a lane, brakes before sharp bends and cars ahead */
void tb_game_bot(tb_game_t *g)
{
    const tb_track_t *t = g->trk;
    int si = (int)(g->z / TB_SEG_LEN);
    float ahead = 0;
    for (int k = 4; k < 30; k++) ahead += tb_seg(t, si + k)->curve;
    ahead /= 26.0f;                                   /* m per segment^2 */
    /* the lane with the most room ahead */
    int lanes = tb_seg(t, si)->lanes;
    float best = -1, target = 0;
    for (int l = 0; l < lanes; l++) {
        float lx = lanes == 2 ? (l ? 1.9f : -1.9f) : (float)(l - 1) * TB_LANE_W;
        float room = 200;
        for (int i = 0; i < g->ntraffic; i++) {
            const tb_traffic_t *c = &g->traffic[i];
            float dz = c->z - g->z;
            if (dz > -5 && dz < room && fabsf(c->x - lx) < 2.4f) room = dz;
        }
        room -= fabsf(lx - g->x) * 2.0f;
        if (room > best) { best = room; target = lx; }
    }
    /* feed forward what the bend pushes, and close on the lane */
    float here = tb_seg(t, si + 2)->curve;
    float vk = g->v / 30.0f > 1.0f ? 1.0f : g->v / 30.0f;
    float ff = 0.0135f * g->v * g->v * here / (13.0f * tb_car_spec(g->car)->grip * (vk > 0.1f ? vk : 0.1f));
    g->in_steer = ff + (target - g->x) * 0.35f;
    if (g->in_steer > 1) g->in_steer = 1;
    if (g->in_steer < -1) g->in_steer = -1;
    float lim = 90.0f - fabsf(ahead) * 500.0f;
    g->in_gas = g->v < lim;
    g->in_brake = g->v > lim + 8.0f;
}

