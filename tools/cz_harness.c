/*
 * AmoledOS - bench for the exchange rates reader (#45).
 *
 * It links against apps/cotiz/main/cz_api.c, which has not one line of LVGL,
 * and tests it on the Mac. It is the same division of labour as af_dsp.c /
 * af_harness.c: the maths on one side, the drawing on the other, and the maths
 * is tested without the board.
 *
 *   cc -Wall -Wextra -I apps/cotiz/main tools/cz_harness.c \
 *      apps/cotiz/main/cz_api.c -o /tmp/cz && /tmp/cz
 *
 * The last test queries the real API and needs the internet; the rest do not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cz_api.h"

static int pruebas, fallos;

static void ok(const char *que, int cond, const char *detalle)
{
    pruebas++;
    printf("  %s  %-44s %s\n", cond ? "OK " : "BAD", que, detalle ? detalle : "");
    if (!cond) fallos++;
}

/* A clipping of the real response of 2026-09-03, with the values exactly as
 * they arrived. Deliberately fixed: if the API changes shape tomorrow, this
 * test goes on saying whether the READER works, which is its job. */
static const char *MUESTRA_DOLARES =
"[{\"moneda\":\"USD\",\"casa\":\"oficial\",\"nombre\":\"Oficial\",\"compra\":1480,"
"\"venta\":1530,\"fechaActualizacion\":\"2026-09-03T17:00:00.000Z\"},"
"{\"moneda\":\"USD\",\"casa\":\"blue\",\"nombre\":\"Blue\",\"compra\":1525,"
"\"venta\":1545,\"fechaActualizacion\":\"2026-09-03T20:01:00.000Z\"},"
"{\"moneda\":\"USD\",\"casa\":\"bolsa\",\"nombre\":\"Bolsa\",\"compra\":1524.4,"
"\"venta\":1531.9,\"fechaActualizacion\":\"2026-09-03T20:01:00.000Z\"},"
"{\"moneda\":\"USD\",\"casa\":\"contadoconliqui\",\"nombre\":\"Contado con liquidación\","
"\"compra\":1587.9,\"venta\":1590.3,\"fechaActualizacion\":\"2026-09-03T20:01:00.000Z\"},"
"{\"moneda\":\"USD\",\"casa\":\"tarjeta\",\"nombre\":\"Tarjeta\",\"compra\":1924,"
"\"venta\":1989,\"fechaActualizacion\":\"2026-09-03T20:01:00.000Z\"}]";

static const char *MUESTRA_MONEDAS =
"[{\"moneda\":\"USD\",\"casa\":\"oficial\",\"nombre\":\"Dólar\",\"compra\":1480,"
"\"venta\":1530,\"fechaActualizacion\":\"2026-09-03T16:58:00.000Z\"},"
"{\"moneda\":\"EUR\",\"casa\":\"oficial\",\"nombre\":\"Euro\",\"compra\":1742.2877,"
"\"venta\":1756.5184,\"fechaActualizacion\":\"2026-09-03T16:58:00.000Z\"},"
"{\"moneda\":\"BRL\",\"casa\":\"oficial\",\"nombre\":\"Real Brasileño\","
"\"compra\":296.7322,\"venta\":296.9052,\"fechaActualizacion\":\"2026-09-03T16:54:00.000Z\"}]";

int main(void)
{
    char d[64], e[160];

    printf("\n=== 1. the table of instruments ===\n");
    {
        ok("blue existe", cz_indice("blue") == 1, NULL);
        ok("eur existe", cz_indice("eur") == 7, NULL);
        ok("a made-up key does not", cz_indice("dogecoin") < 0, NULL);
        ok("an empty string does not", cz_indice("") < 0, NULL);
        ok("NULL does not blow up", cz_indice(NULL) < 0, NULL);

        /* The keys cannot repeat, not even between houses and currencies: the
         * preference is a flat list of keys and does not distinguish the two
         * namespaces. */
        int repes = 0;
        for (int i = 0; i < CZ_MAX; i++)
            for (int j = i + 1; j < CZ_MAX; j++)
                if (strcmp(CZ_ESPECIES[i].key, CZ_ESPECIES[j].key) == 0) repes++;
        ok("ninguna key repetida", repes == 0, NULL);

        /* The names are drawn with Montserrat, which brings ASCII. */
        int no_ascii = 0;
        for (int i = 0; i < CZ_MAX; i++)
            for (const unsigned char *p = (const unsigned char *)CZ_ESPECIES[i].nombre; *p; p++)
                if (*p < 0x20 || *p > 0x7E) no_ascii++;
        ok("every name is ASCII", no_ascii == 0, "(Montserrat has no accents)");
    }

    printf("\n=== 2. reading /v1/dolares ===\n");
    cz_datos_t datos;
    memset(&datos, 0, sizeof(datos));
    {
        int n = cz_parse(MUESTRA_DOLARES, (int)strlen(MUESTRA_DOLARES), &datos);
        snprintf(e, sizeof(e), "filled %d", n);
        ok("five houses", n == 5, e);

        int i = cz_indice("blue");
        snprintf(e, sizeof(e), "venta=%ld compra=%ld",
                 (long)datos.v[i].venta_cent, (long)datos.v[i].compra_cent);
        ok("blue: integers with no decimals", datos.v[i].venta_cent == 154500 &&
                                          datos.v[i].compra_cent == 152500, e);

        i = cz_indice("bolsa");
        snprintf(e, sizeof(e), "venta=%ld (1531.9 -> 153190)", (long)datos.v[i].venta_cent);
        ok("bolsa: one decimal is padded out", datos.v[i].venta_cent == 153190, e);

        i = cz_indice("tarjeta");
        snprintf(e, sizeof(e), "venta=%ld", (long)datos.v[i].venta_cent);
        ok("tarjeta: the largest value", datos.v[i].venta_cent == 198900, e);

        i = cz_indice("mayorista");
        ok("a house that did not arrive stays NOT ok", !datos.v[i].ok, NULL);
    }

    printf("\n=== 3. reading /v1/cotizaciones without trampling the previous data ===\n");
    {
        int n = cz_parse(MUESTRA_MONEDAS, (int)strlen(MUESTRA_MONEDAS), &datos);
        snprintf(e, sizeof(e), "filled %d (the array's USD lands in 'oficial')", n);
        ok("two currencies + the dollar", n == 3, e);

        int i = cz_indice("brl");
        snprintf(e, sizeof(e), "venta=%ld (296.9052 -> 29690, truncated)",
                 (long)datos.v[i].venta_cent);
        ok("real: four decimals are truncated", datos.v[i].venta_cent == 29690, e);

        i = cz_indice("eur");
        snprintf(e, sizeof(e), "venta=%ld", (long)datos.v[i].venta_cent);
        ok("euro", datos.v[i].venta_cent == 175651, e);

        /* What really matters: the second query does NOT wipe the first. */
        i = cz_indice("blue");
        ok("blue is still there from the previous pass", datos.v[i].ok &&
                                                       datos.v[i].venta_cent == 154500, NULL);
        i = cz_indice("clp");
        ok("a currency that did not arrive stays NOT ok", !datos.v[i].ok, NULL);
    }

    printf("\n=== 4. price formatting ===\n");
    {
        ok("coma decimal", strcmp(cz_precio(152440, d, sizeof(d)), "1524,40") == 0,
           cz_precio(152440, d, sizeof(d)));
        ok("cents with a leading zero", strcmp(cz_precio(29605, d, sizeof(d)), "296,05") == 0,
           cz_precio(29605, d, sizeof(d)));
        ok("redondo", strcmp(cz_precio(148000, d, sizeof(d)), "1480,00") == 0,
           cz_precio(148000, d, sizeof(d)));
        ok("less than one peso", strcmp(cz_precio(7, d, sizeof(d)), "0,07") == 0,
           cz_precio(7, d, sizeof(d)));
    }

    printf("\n=== 5. the date comes in UTC, not in the board's time ===\n");
    {
        /* 2026-09-03T20:01:00Z is 1788465660 epoch. If the reader used
         * mktime() this would come out differently depending on the time zone
         * of whoever compiles it, which is precisely the bug we set out to
         * avoid. */
        int i = cz_indice("blue");
        cz_datos_t sola; memset(&sola, 0, sizeof(sola));
        cz_parse(MUESTRA_DOLARES, (int)strlen(MUESTRA_DOLARES), &sola);
        snprintf(e, sizeof(e), "epoch=%lld", (long long)sola.v[i].fecha);
        ok("epoch UTC exacto", sola.v[i].fecha == 1788465660LL, e);

        cz_antiguedad(1788465660LL, 1788465660LL + 30, d, sizeof(d));
        ok("30 s -> recien", strcmp(d, "recien") == 0, d);
        cz_antiguedad(1788465660LL, 1788465660LL + 600, d, sizeof(d));
        ok("10 min", strcmp(d, "hace 10 min") == 0, d);
        cz_antiguedad(1788465660LL, 1788465660LL + 7200, d, sizeof(d));
        ok("2 h", strcmp(d, "hace 2 h") == 0, d);
        cz_antiguedad(1788465660LL, 1788465660LL - 300, d, sizeof(d));
        ok("board running behind: it says nothing", d[0] == 0, "(instead of 'hace -5 min')");
        cz_antiguedad(0, 1788465660LL, d, sizeof(d));
        ok("no date: it says nothing", d[0] == 0, NULL);
    }

    printf("\n=== 6. rubbish without blowing up ===\n");
    {
        cz_datos_t z; memset(&z, 0, sizeof(z));
        /* The lengths ALWAYS come from strlen and never by hand. The first
         * version counted them by eye, got the last case wrong by two and
         * AddressSanitizer caught it reading past the end of the string. The
         * bug was the bench's, not the reader's —cz_parse trusts the length it
         * is given, and in the app that length comes from aos_hal_http_len(),
         * which does not lie— but counting characters by hand in a test has no
         * advantage at all and this is the price. */
        #define PARSE(lit) cz_parse((lit), (int)strlen(lit), &z)
        ok("NULL", cz_parse(NULL, 100, &z) == 0, NULL);
        ok("empty", PARSE("") == 0, NULL);
        ok("not JSON", PARSE("hello there") == 0, NULL);
        ok("empty array", PARSE("[]") == 0, NULL);
        ok("unclosed object", PARSE("[{\"casa\":\"blue\"") == 0, NULL);
        ok("known house with no venta",
           PARSE("[{\"moneda\":\"USD\",\"casa\":\"blue\",\"compra\":1}]") == 0,
           "(with no venta there is nothing to show)");
        ok("a house this app does not know",
           PARSE("[{\"moneda\":\"USD\",\"casa\":\"futuro\",\"venta\":1}]") == 0,
           "(it is ignored, nothing breaks)");
        /* A brace inside a string cannot cut the object short. */
        int n = PARSE("[{\"moneda\":\"USD\",\"casa\":\"blue\",\"nombre\":\"a}b\","
                      "\"venta\":10}]");
        ok("a brace inside a string", n == 1 && z.v[1].venta_cent == 1000, NULL);
        #undef PARSE
    }

    printf("\n=== 7. against the real API (needs internet) ===\n");
    {
        char cmd[256], *buf = malloc(CZ_BUF_BYTES);
        int total = 0;
        snprintf(cmd, sizeof(cmd), "curl -s --max-time 20 '%s'", cz_url_dolares());
        FILE *f = popen(cmd, "r");
        if (f) {
            total = (int)fread(buf, 1, CZ_BUF_BYTES - 1, f);
            pclose(f);
        }
        buf[total > 0 ? total : 0] = 0;

        snprintf(e, sizeof(e), "%d bytes (the ceiling is %d)", total, CZ_BUF_BYTES);
        ok("the answer fits in the buffer", total > 100 && total < CZ_BUF_BYTES - 512, e);

        cz_datos_t viva; memset(&viva, 0, sizeof(viva));
        int n = cz_parse(buf, total, &viva);
        snprintf(e, sizeof(e), "%d especies", n);
        ok("the dollar's seven houses are read", n == 7, e);

        int i = cz_indice("blue");
        if (viva.v[i].ok) {
            char p[32], a[32];
            cz_precio(viva.v[i].venta_cent, p, sizeof(p));
            cz_antiguedad(viva.v[i].fecha, (int64_t)time(NULL), a, sizeof(a));
            snprintf(e, sizeof(e), "blue vende a %s, %s", p, a);
            /* A deliberately wide range: the test is that the number is
             * plausible, not what the dollar is worth today. */
            ok("blue has a believable value", viva.v[i].venta_cent > 10000 &&
                                                 viva.v[i].venta_cent < 100000000, e);
        } else {
            ok("blue arrived", 0, "it was not read");
        }
        free(buf);
    }

    printf("\n---------------------------------------------\n");
    printf("%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
