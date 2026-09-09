/*
 * Remoto's test bench, without LVGL, without the HAL and without the board.
 *
 *   cc -O1 -Wall -Wextra -I../main -I../../../components/aos_ui/include \
 *      -I../../../components/aos_hal/include -o /tmp/rc_harness rc_harness.c \
 *      ../main/rc_model.c ../main/rc_tilt.c -lm && /tmp/rc_harness
 *
 * It checks three things that on the board would be discovered late and badly:
 *
 *  1. That the angles come out of the FOUR POSTURES MEASURED on the board on
 *     2026-08-28 (docs/DECISIONES.md). A reversed sign in rc_tilt.c breaks
 *     nothing: it makes the dial turn the other way and "to the right" switch
 *     on the thing on the left, which is exactly what you cannot bring
 *     yourself to believe is wrong.
 *  2. That each gesture is recognised by a plausible movement and -more
 *     important- that the noise of walking with the board in your hand does
 *     NOT fire any of them. A remote that switches lights on by itself is
 *     worse than one that does not work.
 *  3. That the JSON reader takes out of the profile the same thing the web
 *     page put in, odd cases included: accents, missing fields, extra pages.
 */
#include "rc_model.h"
#include "rc_tilt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* rc_model.c wraps its error messages in _(), which is aos_tr(). Nothing here
 * translates anything, and pulling in the runtime just for that would drag
 * LVGL along; the identity is what aos_tr() itself returns with no pack
 * loaded, so the bench sees exactly what the board sees in Spanish. */
const char *aos_tr(const char *es)                  { return es; }
const char *aos_trc(const char *ctx, const char *es) { (void)ctx; return es; }

static int fallos;

static void ok(const char *what, int cond)
{
    printf("  %s %s\n", cond ? "ok  " : "FALLA", what);
    if (!cond) {
        fallos++;
    }
}

static void near(const char *what, float got, float want, float tol)
{
    int cond = fabsf(got - want) <= tol;
    printf("  %s %-44s %+8.2f (esperado %+.2f +-%.2f)\n",
           cond ? "ok  " : "FALLA", what, (double)got, (double)want, (double)tol);
    if (!cond) {
        fallos++;
    }
}

/* -------------------------------------------------------------------------- */
/* 1. The axes, against the four measured postures                             */
/* -------------------------------------------------------------------------- */

static void test_ejes(void)
{
    puts("\n== angulos contra las posturas MEDIDAS en la placa ==");

    /* docs/DECISIONES.md, table of 2026-08-28 */
    struct { const char *name; float ax, ay, az; } m[] = {
        { "plana, pantalla arriba",       +0.026f, +0.002f, -1.022f },
        { "de pie, derecha",              +1.021f, +0.014f, +0.033f },
        { "acostada sobre su lado derecho", +0.025f, -0.988f, -0.067f },
        { "de pie, de cabeza",            -0.978f, -0.003f, +0.021f },
    };

    printf("  postura                            giro   cabeceo  giro? cabeceo?\n");
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        printf("  %-32s %+7.1f  %+7.1f    %d      %d\n", m[i].name,
               (double)rc_roll_deg(m[i].ax, m[i].ay),
               (double)rc_pitch_deg(m[i].ax, m[i].az),
               rc_roll_usable(m[i].ax, m[i].ay),
               rc_pitch_usable(m[i].ax, m[i].az));
    }
    puts("");

    /* Standing up facing you: both angles are zero. It is the remote's rest
     * position. */
    near("de pie: giro = 0", rc_roll_deg(m[1].ax, m[1].ay), 0.0f, 3.0f);
    near("de pie: cabeceo = 0", rc_pitch_deg(m[1].ax, m[1].az), 0.0f, 3.0f);

    /* Lying on its right side means having turned it 90 degrees clockwise: the
     * right edge ended up at the bottom. The dial has to read +90. */
    near("lado derecho abajo: giro = +90",
         rc_roll_deg(m[2].ax, m[2].ay), 90.0f, 3.0f);

    /* Resting face up was reached by tilting it 90 degrees backwards. */
    near("apoyada boca arriba: cabeceo = +90",
         rc_pitch_deg(m[0].ax, m[0].az), 90.0f, 3.0f);

    /* And there the roll no longer means anything: there is no gravity in that
     * plane. */
    ok("apoyada: el giro se declara inutilizable",
       !rc_roll_usable(m[0].ax, m[0].ay));
    /* Resting face UP the pitch does make sense (it reads +90), but the roll
     * does not, and without a roll there is no rest position for the pitch to
     * hang off: in practice, resting there are no gestures. What does have to
     * be excluded is the other side, because there the pitch and the face-down
     * are the same movement. */
    ok("apoyada boca abajo: el cabeceo no sirve",
       !rc_pitch_usable(0.02f, +1.00f));
    ok("de pie: los dos angulos sirven",
       rc_roll_usable(m[1].ax, m[1].ay) && rc_pitch_usable(m[1].ax, m[1].az));

    /* The dial's sign: turning the wrist clockwise has to go UP. */
    float a = rc_roll_deg(1.0f, 0.0f);
    float b = rc_roll_deg(0.87f, -0.5f);        /* 30 degrees clockwise */
    near("giro horario de 30 grados = +30", rc_angle_delta(b, a), 30.0f, 2.0f);

    /* And the +-180 wrap cannot give a jump of 350 degrees. */
    near("delta 170 -> -170 son 20 grados",
         rc_angle_delta(-170.0f, 170.0f), 20.0f, 0.1f);
}

/* -------------------------------------------------------------------------- */
/* 2. The gestures                                                             */
/* -------------------------------------------------------------------------- */

static rc_gcfg_t cfg_default(void)
{
    rc_gcfg_t c = { .enabled = 1, .shake_mg = 2000, .tilt_deg = 40,
                    .hold_ms = 700, .cool_ms = 1500 };
    return c;
}

/* Holds the board still in one posture for 'ms', 10 samples a second, with a
 * hand tremor of +-8 milli-g. Returns the last gesture. */
static int hold(rc_tilt_t *t, uint32_t *now, int ms,
                float ax, float ay, float az, int *count)
{
    int last = -1;
    for (int i = 0; i < ms / 100; i++) {
        float n = 0.008f * (float)((i * 37 % 11) - 5);
        int g = rc_tilt_feed(t, ax + n, ay - n, az + n * 0.5f, *now);
        if (g >= 0) {
            last = g;
            if (count) {
                (*count)++;
            }
        }
        *now += 100;
    }
    return last;
}

/* Moves the board from one posture to another in 'ms', at 10 samples a second
 * and passing through the middle. It is the difference between testing the
 * remote and testing a teleportation: jumping straight from vertical to face
 * down puts a variation of 2 g into a single sample, which -rightly- is
 * recognised as a shake. A hand takes about half a second to turn the board
 * over. */
static int move(rc_tilt_t *t, uint32_t *now, int ms,
                float ax0, float ay0, float az0,
                float ax1, float ay1, float az1, int *count)
{
    int steps = ms / 100;
    int last = -1;
    for (int i = 1; i <= steps; i++) {
        float k = (float)i / (float)steps;
        float x = ax0 + (ax1 - ax0) * k;
        float y = ay0 + (ay1 - ay0) * k;
        float z = az0 + (az1 - az0) * k;
        float m = sqrtf(x * x + y * y + z * z);     /* gravity does not change */
        if (m > 0.01f) {
            x /= m; y /= m; z /= m;
        }
        int g = rc_tilt_feed(t, x, y, z, *now);
        if (g >= 0) {
            last = g;
            if (count) {
                (*count)++;
            }
        }
        *now += 100;
    }
    return last;
}

/* A unit vector rotated 'deg' clockwise from the vertical. */
static void roll_pose(float deg, float *ax, float *ay)
{
    float r = deg / 57.2957795f;
    *ax = cosf(r);
    *ay = -sinf(r);
}

static void test_gestos(void)
{
    puts("\n== gestos ==");
    rc_gcfg_t cfg = cfg_default();
    rc_tilt_t t;
    uint32_t now = 100000;
    int n;

    /* --- tilt right --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);       /* vertical rest position */
    float ax, ay;
    roll_pose(55.0f, &ax, &ay);
    n = 0;
    int g = hold(&t, &now, 600, ax, ay, 0.0f, &n);
    ok("inclinar a la derecha da TILT_R", g == RC_G_TILT_R);
    ok("y dispara una sola vez, no una por muestra", n == 1);

    /* Holding it tilted does not fire again: the rest position follows it. */
    n = 0;
    hold(&t, &now, 5000, ax, ay, 0.0f, &n);
    ok("sostenerla inclinada no repite", n == 0);

    /* And returning to the vertical from there does not either: the rest
     * position has already moved, but the return is 55 degrees and the
     * threshold is 40. This is the awkward case, and the right thing is for it
     * to fire TILT_L: as far as the remote is concerned the board comes back
     * tilted to the left relative to where you had been holding it. */
    n = 0;
    g = hold(&t, &now, 800, 1.0f, 0.0f, 0.0f, &n);
    printf("     (volver a la vertical: %s)\n",
           g < 0 ? "sin gesto" : rc_gesture_name(g));

    /* --- tilt left --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    roll_pose(-55.0f, &ax, &ay);
    g = hold(&t, &now, 600, ax, ay, 0.0f, NULL);
    ok("inclinar a la izquierda da TILT_L", g == RC_G_TILT_L);

    /* --- pitch --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    /* backwards 50 degrees: the screen ends up facing the ceiling */
    g = hold(&t, &now, 600, cosf(50 / 57.2958f), 0.0f, -sinf(50 / 57.2958f), NULL);
    ok("inclinar hacia atras da TILT_B", g == RC_G_TILT_B);

    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    g = hold(&t, &now, 600, cosf(50 / 57.2958f), 0.0f, +sinf(50 / 57.2958f), NULL);
    ok("inclinar hacia adelante da TILT_F", g == RC_G_TILT_F);

    /* --- face down ---
     * Only this gesture configured: it is the normal case, and it is the one
     * the remote has to get right. With the pitch configured as well things
     * change, and that is tested separately, below. */
    rc_tilt_reset(&t, &cfg, RC_G_BIT(RC_G_FLIP));
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    n = 0;
    g = move(&t, &now, 500, 1.0f, 0.0f, 0.0f, 0.05f, 0.0f, 1.0f, &n);
    printf("     (darla vuelta en medio segundo: %s)\n",
           g < 0 ? "sin gesto" : rc_gesture_name(g));
    n = 0;
    g = hold(&t, &now, 400, 0.05f, 0.0f, 1.0f, &n);
    ok("recien dada vuelta todavia no dispara FLIP", g != RC_G_FLIP);
    g = hold(&t, &now, 900, 0.05f, 0.0f, 1.0f, &n);
    ok("boca abajo sostenido da FLIP", g == RC_G_FLIP);
    n = 0;
    hold(&t, &now, 5000, 0.05f, 0.0f, 1.0f, &n);
    ok("y dejarla boca abajo no repite", n == 0);

    /* Tilting forwards and turning the board over are the same movement at
     * different depths. With BOTH gestures configured the first to arrive
     * wins, which is the pitch, and the face-down is masked by the dead time.
     * It is not a defect that can be fixed: it is what they mean. It is tested
     * so it is written down which of the two wins, and so the web page can say
     * so. */
    rc_tilt_reset(&t, &cfg, RC_G_BIT(RC_G_TILT_F) | RC_G_BIT(RC_G_FLIP));
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    n = 0;
    g = move(&t, &now, 700, 1.0f, 0.0f, 0.0f, 0.05f, 0.0f, 1.0f, &n);
    printf("     (girarla despacio hasta boca abajo: primero %s)\n",
           g < 0 ? "nada" : rc_gesture_name(g));
    ok("bajando hasta boca abajo gana el cabeceo hacia adelante",
       g == RC_G_TILT_F);
    n = 0;
    hold(&t, &now, 1200, 0.05f, 0.0f, 1.0f, &n);
    ok("y el boca abajo queda tapado por el tiempo muerto", n == 0);

    /* --- shake --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    g = -1;
    for (int i = 0; i < 8 && g < 0; i++) {
        float s = (i & 1) ? 1.0f : -1.0f;
        g = rc_tilt_feed(&t, 1.0f + s * 0.9f, s * 0.7f, 0.0f, now);
        now += 100;
    }
    ok("sacudirla da SHAKE", g == RC_G_SHAKE);

    /* --- the case that matters: walking fires nothing --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    n = 0;
    for (int i = 0; i < 600; i++) {                 /* a minute of walking */
        float ph = (float)i * 0.62f;
        float wx = 1.0f + 0.16f * sinf(ph) + 0.05f * sinf(ph * 2.3f);
        float wy = 0.12f * sinf(ph * 0.7f);
        float wz = 0.10f * cosf(ph * 1.1f);
        if (rc_tilt_feed(&t, wx, wy, wz, now) >= 0) {
            n++;
        }
        now += 100;
    }
    printf("  %s caminar un minuto con la placa en la mano: %d disparos\n",
           n == 0 ? "ok  " : "FALLA", n);
    if (n) {
        fallos++;
    }

    /* --- and slowly turning it to look at it does not either --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    n = 0;
    for (int i = 0; i < 200; i++) {                 /* 20 s turning 60 degrees */
        roll_pose((float)i * 0.3f, &ax, &ay);
        if (rc_tilt_feed(&t, ax, ay, 0.0f, now) >= 0) {
            n++;
        }
        now += 100;
    }
    printf("  %s girarla despacio 60 grados en 20 s: %d disparos\n",
           n == 0 ? "ok  " : "FALLA", n);
    if (n) {
        fallos++;
    }

    /* --- resting on the table fires nothing --- */
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    n = 0;
    hold(&t, &now, 10000, 0.026f, 0.002f, -1.022f, &n);
    ok("diez segundos apoyada boca arriba: ni un disparo", n == 0);

    /* --- switched off nothing happens --- */
    cfg.enabled = 0;
    rc_tilt_reset(&t, &cfg, RC_G_ALL);
    hold(&t, &now, 2000, 1.0f, 0.0f, 0.0f, NULL);
    roll_pose(80.0f, &ax, &ay);
    n = 0;
    hold(&t, &now, 2000, ax, ay, 0.0f, &n);
    ok("con los gestos apagados no dispara nada", n == 0);
}

static void test_dial(void)
{
    puts("\n== dial ==");

    /* 1..100 over 140 degrees of wrist: two thirds of a degree per point. */
    const int MIN = 1, MAX = 100, SPAN = 140;

    ok("sin girar, no se mueve",
       rc_dial_value(50, 0.0f, SPAN, MIN, MAX, 0) == 50);
    ok("+140 grados desde el minimo llega al maximo",
       rc_dial_value(MIN, 140.0f, SPAN, MIN, MAX, 0) == MAX);
    ok("-140 grados desde el maximo llega al minimo",
       rc_dial_value(MAX, -140.0f, SPAN, MIN, MAX, 0) == MIN);
    ok("medio giro desde el medio suma medio recorrido",
       rc_dial_value(50, 70.0f, SPAN, MIN, MAX, 0) == 100);
    ok("se recorta en el maximo",
       rc_dial_value(90, 200.0f, SPAN, MIN, MAX, 0) == MAX);
    ok("y en el minimo",
       rc_dial_value(10, -200.0f, SPAN, MIN, MAX, 0) == MIN);
    ok("invertido va para el otro lado",
       rc_dial_value(50, 35.0f, SPAN, MIN, MAX, 1) ==
       rc_dial_value(50, -35.0f, SPAN, MIN, MAX, 0));
    ok("un grado ya mueve el dial",
       rc_dial_value(50, 1.0f, SPAN, MIN, MAX, 0) == 51);
    ok("un recorrido de largo cero no rompe nada",
       rc_dial_value(5, 90.0f, SPAN, 10, 10, 0) == 10);
    ok("span cero no divide por cero",
       rc_dial_value(50, 10.0f, 0, MIN, MAX, 0) == MAX);

    /* And what motivated accumulating instead of subtracting: turning the
     * wrist a little at a time, passing through the +-180 wrap, has to go on
     * adding up. */
    float acc = 0.0f, prev = 0.0f, total = 0.0f;
    for (int i = 1; i <= 200; i++) {            /* 200 degrees one at a time */
        float roll = (float)i;
        while (roll >  180.0f) roll -= 360.0f;  /* as it comes out of atan2 */
        total = rc_dial_turn(&acc, &prev, roll);
    }
    near("girar 200 grados de a uno acumula 200", total, 200.0f, 0.5f);
    ok("y el valor se va al maximo, no al minimo",
       rc_dial_value(50, total, SPAN, MIN, MAX, 0) == MAX);

    /* The case that used to fail, written as what it is: comparing against the
     * latch's angle gives -160 instead of +200. */
    near("restando contra el enganche daria -160",
         rc_angle_delta(-160.0f, 0.0f), -160.0f, 0.5f);

    /* Turning back the other way has to undo it exactly. */
    for (int i = 199; i >= 0; i--) {
        float roll = (float)i;
        while (roll >  180.0f) roll -= 360.0f;
        total = rc_dial_turn(&acc, &prev, roll);
    }
    near("y volver deja el acumulado en cero", total, 0.0f, 0.5f);
}

/* -------------------------------------------------------------------------- */
/* 3. The profile                                                              */
/* -------------------------------------------------------------------------- */

static const char *PERFIL =
"{\"v\":1,\"poll\":6,"
" \"gcfg\":{\"on\":1,\"shake\":1800,\"tilt\":35,\"hold\":600,\"cool\":1200},"
" \"gestures\":{"
"   \"shake\":{\"t\":\"svc\",\"s\":\"light.toggle\",\"e\":\"light.lamp\"},"
"   \"flip\":{\"t\":\"svc\",\"s\":\"media_player.media_pause\",\"e\":\"media_player.tv\"},"
"   \"tiltr\":{\"t\":\"page\",\"p\":1}},"
" \"pages\":["
"  {\"n\":\"Living room\",\"k\":\"grid\",\"c\":3,\"r\":2,\"b\":["
"    {\"l\":\"Ceiling\",\"c\":16766474,\"st\":\"light.ceiling\",\"sm\":1,"
"     \"a\":{\"t\":\"svc\",\"s\":\"light.toggle\",\"e\":\"light.ceiling\"},"
"     \"h\":{\"t\":\"svc\",\"s\":\"light.turn_on\",\"e\":\"light.ceiling\","
"           \"d\":\"\\\"brightness_pct\\\":100\"}},"
"    {\"l\":\"Temp\",\"sm\":2,\"st\":\"sensor.room_temp\",\"u\":\"C\"},"
"    {\"l\":\"Dimmer\",\"sm\":3,\"st\":\"light.ceiling\",\"sa\":\"brightness\"},"
"    {\"l\":\"More\",\"a\":{\"t\":\"page\",\"p\":1}},"
"    {\"l\":\"Away\",\"a\":{\"t\":\"page\",\"p\":9}},"
"    {\"l\":\"None\"}]},"
"  {\"n\":\"Dimmer\",\"k\":\"dial\",\"dl\":{\"s\":\"light.turn_on\","
"    \"e\":\"light.ceiling\",\"f\":\"brightness_pct\",\"mn\":1,\"mx\":100,"
"    \"sp\":140,\"st\":\"light.ceiling\",\"sa\":\"brightness\",\"sf\":255}}"
" ]}";

static void test_perfil(void)
{
    puts("\n== perfil ==");
    char err[96];
    rc_profile_t *p = rc_parse(PERFIL, (int)strlen(PERFIL), err, sizeof(err));
    if (!p) {
        printf("  FALLA no parseo: %s\n", err);
        fallos++;
        return;
    }
    ok("dos paginas", p->n_pages == 2);
    ok("poll = 6", p->poll_s == 6);
    ok("umbral de sacudida = 1800", p->gcfg.shake_mg == 1800);
    ok("pagina 0 es grilla 3x2", p->pages[0].kind == RC_PAGE_GRID &&
                                 p->pages[0].cols == 3 && p->pages[0].rows == 2);
    ok("seis botones", p->pages[0].count == 6);
    ok("etiqueta del primero", strcmp(p->pages[0].btn[0].label, "Ceiling") == 0);
    ok("color del primero", p->pages[0].btn[0].color == 0xFFD60Au);
    ok("toque = light.toggle sobre light.ceiling",
       p->pages[0].btn[0].tap.type == RC_ACT_SERVICE &&
       strcmp(p->pages[0].btn[0].tap.service, "light.toggle") == 0 &&
       strcmp(p->pages[0].btn[0].tap.entity, "light.ceiling") == 0);
    ok("mantenido lleva datos extra",
       strcmp(p->pages[0].btn[0].hold.data, "\"brightness_pct\":100") == 0);
    ok("un boton sin accion queda en NONE",
       p->pages[0].btn[5].tap.type == RC_ACT_NONE);
    ok("una pagina que no existe se recorta",
       p->pages[0].btn[4].tap.type == RC_ACT_NONE);
    ok("navegar a la pagina 1 sobrevive",
       p->pages[0].btn[3].tap.type == RC_ACT_PAGE &&
       p->pages[0].btn[3].tap.page == 1);

    ok("gesto de sacudida cargado",
       p->gest[RC_G_SHAKE].type == RC_ACT_SERVICE &&
       strcmp(p->gest[RC_G_SHAKE].entity, "light.lamp") == 0);
    ok("gesto de giro a la derecha navega",
       p->gest[RC_G_TILT_R].type == RC_ACT_PAGE);
    ok("un gesto sin configurar queda en NONE",
       p->gest[RC_G_TILT_L].type == RC_ACT_NONE);

    ok("pagina 1 es dial", p->pages[1].kind == RC_PAGE_DIAL);
    ok("dial: recorrido y campo",
       p->pages[1].dial.min == 1 && p->pages[1].dial.max == 100 &&
       strcmp(p->pages[1].dial.field, "brightness_pct") == 0 &&
       p->pages[1].dial.span_deg == 140 && p->pages[1].dial.st_full == 255);

    /* The state table merges duplicates: light.ceiling appears in button 0
     * (state), in button 2 (attribute) and in the dial (attribute). That is
     * two entries, not three, plus sensor.room_temp. */
    printf("  tabla de estados: %d entradas\n", p->n_states);
    for (int i = 0; i < p->n_states; i++) {
        printf("     %d  %s%s%s\n", i, p->st_entity[i],
               p->st_attr[i][0] ? " . " : "", p->st_attr[i]);
    }
    ok("tres entradas, sin repetir light.ceiling", p->n_states == 3);
    ok("el dial comparte lugar con el boton de brillo",
       p->pages[1].dial.slot == p->pages[0].btn[2].slot);

    char *tpl = rc_build_template(p);
    ok("plantilla armada", tpl != NULL);
    if (tpl) {
        printf("  plantilla: %s\n", tpl);
        ok("consulta el estado con states()", strstr(tpl, "{{states('light.ceiling')}}") != NULL);
        ok("y el atributo con state_attr()",
           strstr(tpl, "{{state_attr('light.ceiling','brightness')}}") != NULL);
        free(tpl);
    }

    rc_apply_template(p, "on|21.4|128\n", 12);
    ok("estado repartido: on", strcmp(rc_state_value(p, 0), "on") == 0);
    ok("estado repartido: 21.4", strcmp(rc_state_value(p, 1), "21.4") == 0);
    ok("estado repartido: 128, sin el salto de linea",
       strcmp(rc_state_value(p, 2), "128") == 0);
    ok("y se reconoce como encendido", rc_state_is_on(p, 0));

    char body[160];
    rc_action_body(&p->pages[0].btn[0].tap, body, sizeof(body));
    ok("cuerpo simple", strcmp(body, "{\"entity_id\":\"light.ceiling\"}") == 0);
    rc_action_body(&p->pages[0].btn[0].hold, body, sizeof(body));
    ok("cuerpo con datos extra",
       strcmp(body, "{\"entity_id\":\"light.ceiling\",\"brightness_pct\":100}") == 0);

    char dom[16], name[32];
    ok("light.toggle se parte bien",
       rc_split_service("light.toggle", dom, sizeof(dom), name, sizeof(name)) &&
       strcmp(dom, "light") == 0 && strcmp(name, "toggle") == 0);
    ok("un servicio sin punto se rechaza",
       !rc_split_service("toggle", dom, sizeof(dom), name, sizeof(name)));

    rc_free(p);
}

static void test_perfil_roto(void)
{
    puts("\n== perfiles rotos ==");
    char err[96];
    struct { const char *name, *json; } bad[] = {
        { "vacio",              "" },
        { "no es un objeto",    "[1,2,3]" },
        { "sin paginas",        "{\"v\":1}" },
        { "paginas vacias",     "{\"pages\":[]}" },
        { "llave sin cerrar",   "{\"pages\":[{\"n\":\"a\"" },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rc_profile_t *p = rc_parse(bad[i].json, (int)strlen(bad[i].json),
                                   err, sizeof(err));
        printf("  %s %-20s -> %s\n", p ? "FALLA" : "ok  ", bad[i].name,
               p ? "acepto un perfil invalido" : err);
        if (p) {
            fallos++;
            rc_free(p);
        }
    }

    /* Accents and anything non-ASCII are dropped: the firmware's font is
     * Montserrat with ASCII and an accent would be drawn as a square. */
    const char *acentos =
        "{\"pages\":[{\"n\":\"Sal\\u00f3n\",\"b\":[{\"l\":\"Ba\\u00f1o\"}]}]}";
    rc_profile_t *p = rc_parse(acentos, (int)strlen(acentos), err, sizeof(err));
    if (p) {
        printf("  nombre  '%s'   etiqueta '%s'\n",
               p->pages[0].name, p->pages[0].btn[0].label);
        ok("los acentos se caen sin romper el resto",
           strcmp(p->pages[0].name, "Saln") == 0 &&
           strcmp(p->pages[0].btn[0].label, "Bao") == 0);
        rc_free(p);
    } else {
        printf("  FALLA no parseo con acentos: %s\n", err);
        fallos++;
    }

    /* More pages than the ceiling: the first ones are taken and it does not
     * fall over. */
    char big[4096];
    int w = snprintf(big, sizeof(big), "{\"pages\":[");
    for (int i = 0; i < 20; i++) {
        w += snprintf(big + w, sizeof(big) - w, "%s{\"n\":\"p%d\"}", i ? "," : "", i);
    }
    snprintf(big + w, sizeof(big) - w, "]}");
    p = rc_parse(big, (int)strlen(big), err, sizeof(err));
    if (p) {
        ok("20 paginas se recortan al techo", p->n_pages == RC_MAX_PAGES);
        rc_free(p);
    } else {
        printf("  FALLA 20 paginas: %s\n", err);
        fallos++;
    }
}

/* With one argument, instead of the tests it reads that file and reports on
 * it. It is for verifying that what the web page wrote is what the firmware is
 * going to understand, without going through the board or the simulator:
 *
 *     rc_harness sim/sim_fs/data/remoto.json
 */
static int volcar(const char *path)
{
    char err[128];
    rc_profile_t *p = rc_load(path, err, sizeof(err));
    if (!p) {
        printf("no pude leer %s: %s\n", path, err);
        return 1;
    }
    printf("%s\n  %d paginas, refresco cada %d s, %d estados\n",
           path, p->n_pages, p->poll_s, p->n_states);
    printf("  gestos: umbral %d grados, sacudida %d, espera %d ms, %s\n",
           p->gcfg.tilt_deg, p->gcfg.shake_mg, p->gcfg.cool_ms,
           p->gcfg.enabled ? "activos" : "apagados");
    for (int g = 0; g < RC_G_COUNT; g++) {
        if (p->gest[g].type == RC_ACT_SERVICE) {
            printf("    %-16s %s %s\n", rc_gesture_name(g),
                   p->gest[g].service, p->gest[g].entity);
        } else if (p->gest[g].type == RC_ACT_PAGE) {
            printf("    %-16s ir a la pagina %d\n", rc_gesture_name(g),
                   p->gest[g].page + 1);
        }
    }
    for (int i = 0; i < p->n_pages; i++) {
        const rc_page_t *pg = &p->pages[i];
        if (pg->kind == RC_PAGE_DIAL) {
            printf("  %d. %-14s dial  %s %s  %s  %d..%d en %d grados\n",
                   i + 1, pg->name, pg->dial.service, pg->dial.entity,
                   pg->dial.field, pg->dial.min, pg->dial.max,
                   pg->dial.span_deg);
            continue;
        }
        printf("  %d. %-14s %dx%d, %d botones\n", i + 1, pg->name,
               pg->cols, pg->rows, pg->count);
        for (int b = 0; b < pg->count; b++) {
            const rc_button_t *bt = &pg->btn[b];
            char toque[80] = "-";
            if (bt->tap.type == RC_ACT_SERVICE) {
                snprintf(toque, sizeof(toque), "%s %s",
                         bt->tap.service, bt->tap.entity);
            } else if (bt->tap.type == RC_ACT_PAGE) {
                snprintf(toque, sizeof(toque), "-> pagina %d", bt->tap.page + 1);
            }
            printf("       %-12s #%06lX  %-42s%s%s\n", bt->label,
                   (unsigned long)bt->color, toque,
                   bt->hold.type != RC_ACT_NONE ? "  [mantenido]" : "",
                   bt->st_mode ? "  [estado]" : "");
        }
    }
    char *tpl = rc_build_template(p);
    if (tpl) {
        printf("  plantilla (%d bytes): %s\n", (int)strlen(tpl), tpl);
        free(tpl);
    }
    rc_free(p);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        return volcar(argv[1]);
    }
    puts("banco de pruebas de Remoto");
    test_ejes();
    test_gestos();
    test_dial();
    test_perfil();
    test_perfil_roto();

    printf("\n%s\n", fallos ? "HAY FALLAS" : "todo bien");
    return fallos ? 1 : 0;
}
