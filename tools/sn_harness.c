/*
 * AmoledOS - bench for the sensor panel (#46).
 *
 *   cc -Wall -Wextra -I apps/sensores/main tools/sn_harness.c \
 *      apps/sensores/main/sn_api.c -o /tmp/sn && /tmp/sn
 *
 * It needs neither the network nor the board: sn_api.c talks to nobody, it
 * only turns text into numbers. What is tested is precisely that, which is
 * where the mistakes that cost an afternoon on the board are.
 */
#include <stdio.h>
#include <string.h>

#include "sn_api.h"

static int pruebas, fallos;

static void ok(const char *que, int cond, const char *detalle)
{
    pruebas++;
    printf("  %s  %-46s %s\n", cond ? "OK " : "MAL", que, detalle ? detalle : "");
    if (!cond) fallos++;
}

int main(void)
{
    char d[128];
    sn_sensor_t s[SN_MAX];

    printf("\n=== 1. leer la configuracion del portal ===\n");
    {
        int n = sn_parse_config(
            "sensor.power|Workshop power|W;"
            "sensor.bed|Printer bed|C;"
            "sensor.pressure|Pressure|hPa", s, SN_MAX);
        snprintf(d, sizeof(d), "%d sensores", n);
        ok("tres registros", n == 3, d);
        ok("entidad", strcmp(s[0].entity, "sensor.power") == 0, s[0].entity);
        ok("nombre", strcmp(s[0].nombre, "Workshop power") == 0, s[0].nombre);
        ok("unidad", strcmp(s[0].unidad, "W") == 0, s[0].unidad);
        ok("el ultimo sin ';' al final", strcmp(s[2].entity, "sensor.pressure") == 0, s[2].entity);
    }
    {
        int n = sn_parse_config("", s, SN_MAX);
        ok("vacio", n == 0, NULL);
        ok("NULL", sn_parse_config(NULL, s, SN_MAX) == 0, NULL);

        /* The case that hung the first version: a record without the unit
         * field left the pointer on the ';' and the loop never advanced. */
        n = sn_parse_config("sensor.a|A;sensor.b|B|W;sensor.c", s, SN_MAX);
        snprintf(d, sizeof(d), "%d (a, b, c)", n);
        ok("registros a los que les faltan campos", n == 3, d);
        ok("el que no tenia nombre usa la entidad",
           strcmp(s[2].nombre, "sensor.c") == 0, s[2].nombre);

        n = sn_parse_config("a|A|W;b|B|W;c|C|W;d|D|W;e|E|W;f|F|W", s, SN_MAX);
        snprintf(d, sizeof(d), "%d (el tope es %d)", n, SN_MAX);
        ok("no pasa del tope", n == SN_MAX, d);
    }

    printf("\n=== 2. la plantilla, una sola para todos ===\n");
    {
        int n = sn_parse_config("sensor.a|A|W;sensor.b|B|C", s, SN_MAX);
        char tpl[512];
        ok("se arma", sn_build_template(s, n, tpl, sizeof(tpl)), NULL);
        ok("un solo pedido con los dos adentro",
           strcmp(tpl, "{\"template\":\"{{states('sensor.a')}}|{{states('sensor.b')}}\"}") == 0,
           tpl);
        ok("sin sensores no arma nada", !sn_build_template(s, 0, tpl, sizeof(tpl)), NULL);
        char chico[20];
        ok("si no entra, avisa", !sn_build_template(s, n, chico, sizeof(chico)), NULL);
    }

    printf("\n=== 3. repartir los valores ===\n");
    {
        int n = sn_parse_config("sensor.a|A|W;sensor.b|B|C;sensor.c|C|%", s, SN_MAX);
        const char *r = "83.8218307495117|21.8|50";
        int b = sn_push_values(r, (int)strlen(r), s, n);
        snprintf(d, sizeof(d), "%d de 3", b);
        ok("los tres traen numero", b == 3, d);
        snprintf(d, sizeof(d), "%ld", (long)s[0].ultimo);
        ok("los decimales de mas se truncan", s[0].ultimo == 8382, d);
        ok("un decimal", s[1].ultimo == 2180, NULL);
        ok("entero", s[2].ultimo == 5000, NULL);
        ok("cada uno tiene una muestra", s[0].n == 1 && s[1].n == 1 && s[2].n == 1, NULL);
    }
    {
        /* What really matters: a sensor that has gone down cannot put a zero
         * in. */
        int n = sn_parse_config("sensor.a|A|W;sensor.b|B|C", s, SN_MAX);
        const char *r1 = "100|20";
        sn_push_values(r1, (int)strlen(r1), s, n);
        const char *r2 = "unavailable|21";
        int b = sn_push_values(r2, (int)strlen(r2), s, n);
        snprintf(d, sizeof(d), "%d de 2, y el anillo de A quedo en %d", b, s[0].n);
        ok("'unavailable' no entra al anillo", b == 1 && s[0].n == 1, d);
        ok("y queda marcado como no ok", !s[0].ok && s[1].ok, NULL);
        ok("el que si vino sigue creciendo", s[1].n == 2, NULL);

        sn_push_values("unknown|22", 10, s, n);
        ok("'unknown' tampoco", s[0].n == 1, NULL);
        sn_push_values("|23", 3, s, n);
        ok("campo vacio tampoco", s[0].n == 1, NULL);
        sn_push_values("on|24", 5, s, n);
        ok("un estado de texto tampoco", s[0].n == 1, NULL);
    }
    {
        /* Fewer fields than expected: the template may come back short. */
        int n = sn_parse_config("sensor.a|A|W;sensor.b|B|C;sensor.c|C|%", s, SN_MAX);
        int b = sn_push_values("10", 2, s, n);
        snprintf(d, sizeof(d), "%d de 3", b);
        ok("respuesta mas corta que la lista", b == 1 && s[1].n == 0 && s[2].n == 0, d);
    }

    printf("\n=== 4. el anillo, cuando da la vuelta ===\n");
    {
        int n = sn_parse_config("sensor.a|A|W", s, SN_MAX);
        for (int i = 0; i < SN_POINTS + 25; i++) {
            char v[16];
            snprintf(v, sizeof(v), "%d", i);
            sn_push_values(v, (int)strlen(v), s, n);
        }
        snprintf(d, sizeof(d), "n=%d (tope %d)", s[0].n, SN_POINTS);
        ok("no pasa del tope", s[0].n == SN_POINTS, d);

        /* The oldest one left is 25, and the newest SN_POINTS+24. */
        snprintf(d, sizeof(d), "la mas vieja es %ld, se esperaba 2500",
                 (long)sn_at(&s[0], 0));
        ok("la mas vieja quedo bien", sn_at(&s[0], 0) == 2500, d);
        snprintf(d, sizeof(d), "la mas nueva es %ld", (long)sn_at(&s[0], s[0].n - 1));
        ok("la mas nueva quedo bien",
           sn_at(&s[0], s[0].n - 1) == (SN_POINTS + 24) * 100, d);
        ok("van en orden creciente",
           sn_at(&s[0], 5) < sn_at(&s[0], 6) && sn_at(&s[0], 6) < sn_at(&s[0], 7), NULL);
        ok("fuera de rango devuelve 0",
           sn_at(&s[0], -1) == 0 && sn_at(&s[0], SN_POINTS) == 0, NULL);

        int32_t lo, hi;
        ok("rango", sn_range(&s[0], &lo, &hi) && lo == 2500 &&
                    hi == (SN_POINTS + 24) * 100, NULL);
    }
    {
        sn_sensor_t z;
        memset(&z, 0, sizeof(z));
        int32_t lo, hi;
        ok("rango sin muestras", !sn_range(&z, &lo, &hi), NULL);

        /* A flat sensor: without opening the range, LVGL draws the line flush
         * against the edge or divides by zero when scaling. */
        int n = sn_parse_config("sensor.a|A|W", s, SN_MAX);
        for (int i = 0; i < 10; i++) {
            sn_push_values("21.8", 4, s, n);
        }
        ok("un sensor que no se movio abre el rango",
           sn_range(&s[0], &lo, &hi) && lo < hi, NULL);
        snprintf(d, sizeof(d), "lo=%ld hi=%ld", (long)lo, (long)hi);
        ok("y queda centrado en el valor", lo < 2180 && hi > 2180, d);
    }

    printf("\n=== 5. como se escribe el numero ===\n");
    {
        ok("un decimal",   strcmp(sn_valor(2180, d, sizeof(d)), "21,8") == 0, sn_valor(2180, d, sizeof(d)));
        ok("sin decimal cuando es cero", strcmp(sn_valor(5000, d, sizeof(d)), "50") == 0, sn_valor(5000, d, sizeof(d)));
        ok("miles",        strcmp(sn_valor(100990, d, sizeof(d)), "1009,9") == 0, sn_valor(100990, d, sizeof(d)));
        ok("negativo",     strcmp(sn_valor(-57510, d, sizeof(d)), "-575,1") == 0, sn_valor(-57510, d, sizeof(d)));
        ok("negativo chico", strcmp(sn_valor(-50, d, sizeof(d)), "-0,5") == 0, sn_valor(-50, d, sizeof(d)));
        ok("cero",         strcmp(sn_valor(0, d, sizeof(d)), "0") == 0, sn_valor(0, d, sizeof(d)));
    }

    printf("\n---------------------------------------------\n");
    printf("%d pruebas, %d fallos\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
