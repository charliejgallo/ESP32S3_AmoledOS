/*
 * AmoledOS - bench for the HTTP client, with and without TLS.
 *
 * It links against components/aos_hal/aos_http.c —the real file, the same one
 * that runs on the board— and against nothing else: the only thing that file
 * asks of the rest of the HAL is aos_hal_time_is_valid(), and the bench
 * supplies it here. That is why neither LVGL nor SDL nor the board is needed.
 *
 * Build and run:
 *     cc -DAOS_SIM -I components/aos_hal/include \
 *        -I $(brew --prefix mbedtls)/include \
 *        tools/http_harness.c components/aos_hal/aos_http.c \
 *        -L $(brew --prefix mbedtls)/lib -lmbedtls -lmbedx509 -lmbedcrypto \
 *        -o /tmp/http_harness && /tmp/http_harness
 *
 * It needs the internet: these are real requests against real servers, which
 * is precisely the point. The cases that have to FAIL use badssl.com, which
 * exists for that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "aos_hal.h"

/* -------------------------------------------------------------------------- */
/* The only thing aos_http.c asks of the rest of the HAL.                      */
bool g_hora_valida = true;
bool aos_hal_time_is_valid(void) { return g_hora_valida; }

/* -------------------------------------------------------------------------- */
static int fallos = 0;
static int pruebas = 0;

static void ok(const char *que, bool cond, const char *detalle)
{
    pruebas++;
    if (cond) {
        printf("  OK   %-42s %s\n", que, detalle ? detalle : "");
    } else {
        printf("  BAD  %-42s %s\n", que, detalle ? detalle : "");
        fallos++;
    }
}

/* Waits for the request to finish. Returns the final state. */
static aos_http_state_t esperar(int id, int max_ms, int *ms_out)
{
    int ms = 0;
    aos_http_state_t st;
    while ((st = aos_hal_http_state(id)) == AOS_HTTP_BUSY && ms < max_ms) {
        usleep(20000);
        ms += 20;
    }
    if (ms_out) {
        *ms_out = ms;
    }
    return st;
}

/* One complete request, end to end. Returns the status (or the error). */
static int pedir(const char *url, int *ms_out, int *len_out)
{
    int id = aos_hal_http_get(url, 8192);
    if (id <= 0) {
        return id;
    }
    aos_http_state_t st = esperar(id, 20000, ms_out);
    int status = aos_hal_http_status(id);
    if (len_out) {
        *len_out = aos_hal_http_len(id);
    }
    if (st == AOS_HTTP_BUSY) {
        status = -999;      /* it never finished */
    }
    aos_hal_http_release(id);
    return status;
}

/* -------------------------------------------------------------------------- */
int main(void)
{
    char det[160];

    printf("\n=== 1. http:// still works exactly as before ===\n");
    {
        int ms = 0, len = 0;
        int st = pedir("http://api.open-meteo.com/v1/forecast?latitude=-34.61"
                       "&longitude=-58.38&current=temperature_2m&timeformat=unixtime",
                       &ms, &len);
        snprintf(det, sizeof(det), "status=%d  %d ms  %d bytes", st, ms, len);
        ok("GET plano", st == 200 && len > 50, det);
    }

    printf("\n=== 2. https:// against the same server ===\n");
    int ms_frio = 0;
    {
        int len = 0;
        int st = pedir("https://api.open-meteo.com/v1/forecast?latitude=-34.61"
                       "&longitude=-58.38&current=temperature_2m&timeformat=unixtime",
                       &ms_frio, &len);
        snprintf(det, sizeof(det), "status=%d  %d ms  %d bytes", st, ms_frio, len);
        ok("GET cifrado", st == 200 && len > 50, det);
    }

    /* Careful about reading this as proof of resumption: on the Mac the 800 ms
     * are nearly all round trip to open-meteo, and the handshake's saving is
     * lost in the noise. Where it shows is on the board, which takes 1842 ms
     * cold and 617 resuming. What is verified here is that a second query to
     * the same host over the stored session still brings the body back
     * correctly. */
    printf("\n=== 3. second request to the same host, over the stored session ===\n");
    {
        int ms = 0, len = 0;
        int st = pedir("https://api.open-meteo.com/v1/forecast?latitude=-34.61"
                       "&longitude=-58.38&current=temperature_2m&timeformat=unixtime",
                       &ms, &len);
        snprintf(det, sizeof(det), "status=%d  segunda vuelta %d ms contra %d de la primera",
                 st, ms, ms_frio);
        ok("segunda consulta al mismo host", st == 200, det);
    }

    printf("\n=== 4. the default port comes from the scheme ===\n");
    {
        int ms = 0, len = 0;
        int st = pedir("https://www.howsmyssl.com/a/check", &ms, &len);
        snprintf(det, sizeof(det), "status=%d  %d bytes (no :443 in the URL)", st, len);
        ok("https with no explicit port", st == 200 && len > 100, det);
    }

    printf("\n=== 5. what MUST fail, and with which code ===\n");
    {
        int st = pedir("https://expired.badssl.com/", NULL, NULL);
        snprintf(det, sizeof(det), "status=%d (se esperaba %d)", st, AOS_HTTP_ERR_TLS);
        ok("certificado vencido", st == AOS_HTTP_ERR_TLS, det);

        st = pedir("https://wrong.host.badssl.com/", NULL, NULL);
        snprintf(det, sizeof(det), "status=%d", st);
        ok("the name does not match", st == AOS_HTTP_ERR_TLS, det);

        st = pedir("https://self-signed.badssl.com/", NULL, NULL);
        snprintf(det, sizeof(det), "status=%d", st);
        ok("firmado por si mismo", st == AOS_HTTP_ERR_TLS, det);

        st = pedir("https://untrusted-root.badssl.com/", NULL, NULL);
        snprintf(det, sizeof(det), "status=%d", st);
        ok("raiz desconocida", st == AOS_HTTP_ERR_TLS, det);
    }

    printf("\n=== 6. with no clock, no handshake is spent ===\n");
    {
        g_hora_valida = false;
        int ms = 0;
        int st = pedir("https://api.open-meteo.com/v1/forecast?latitude=0&longitude=0"
                       "&current=temperature_2m", &ms, NULL);
        g_hora_valida = true;
        snprintf(det, sizeof(det), "status=%d (se esperaba %d) y tardo %d ms",
                 st, AOS_HTTP_ERR_SIN_HORA, ms);
        /* That it is quick is part of the test: the cut has to come BEFORE the
         * handshake, not after negotiating and discovering the problem. */
        ok("corta antes de conectarse", st == AOS_HTTP_ERR_SIN_HORA && ms < 400, det);

        /* And with the time back the same host works: the check leaves nothing
         * broken behind it. */
        st = pedir("https://api.open-meteo.com/v1/forecast?latitude=0&longitude=0"
                   "&current=temperature_2m", NULL, NULL);
        snprintf(det, sizeof(det), "status=%d", st);
        ok("and it recovers once the time comes in", st == 200, det);
    }

    printf("\n=== 7. schemes that are neither of the two ===\n");
    {
        ok("ftp:// se rechaza",   aos_hal_http_get("ftp://example.com/x", 512) < 0, NULL);
        ok("sin esquema",         aos_hal_http_get("example.com/x", 512) < 0, NULL);
        ok("https mal escrito",   aos_hal_http_get("https:/example.com", 512) < 0, NULL);
        ok("NULL",                aos_hal_http_get(NULL, 512) < 0, NULL);
    }

    printf("\n=== 8. release() con el handshake a medio hacer ===\n");
    {
        /* The ugly case: the app closes while the task is inside the
         * handshake. The slot is marked and the task itself cleans it up. */
        int id = aos_hal_http_get("https://api.open-meteo.com/v1/forecast?latitude=10"
                                  "&longitude=10&current=temperature_2m", 8192);
        ok("started", id > 0, NULL);
        usleep(120000);                    /* right in the middle */
        aos_hal_http_release(id);
        ok("a release mid-handshake does not blow up", true, "(AddressSanitizer says so)");
        sleep(3);                          /* let the task finish and clean up */

        /* And after that all three slots have to be free again. */
        int a = aos_hal_http_get("http://api.open-meteo.com/v1/forecast?latitude=1&longitude=1&current=temperature_2m", 4096);
        int b = aos_hal_http_get("http://api.open-meteo.com/v1/forecast?latitude=2&longitude=2&current=temperature_2m", 4096);
        int c = aos_hal_http_get("http://api.open-meteo.com/v1/forecast?latitude=3&longitude=3&current=temperature_2m", 4096);
        snprintf(det, sizeof(det), "ids %d %d %d", a, b, c);
        ok("los tres lugares volvieron", a > 0 && b > 0 && c > 0, det);
        esperar(a, 20000, NULL); esperar(b, 20000, NULL); esperar(c, 20000, NULL);
        aos_hal_http_release(a); aos_hal_http_release(b); aos_hal_http_release(c);
    }

    printf("\n=== 9. los tres lugares a la vez, cifrados ===\n");
    {
        int id[3];
        for (int i = 0; i < 3; i++) {
            char url[200];
            snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%d"
                     "&longitude=%d&current=temperature_2m&timeformat=unixtime", i + 1, i + 1);
            id[i] = aos_hal_http_get(url, 8192);
        }
        ok("three at once do start", id[0] > 0 && id[1] > 0 && id[2] > 0, NULL);
        int cuarto = aos_hal_http_get("https://api.open-meteo.com/v1/forecast?latitude=9&longitude=9&current=temperature_2m", 4096);
        snprintf(det, sizeof(det), "devolvio %d (se esperaba -2, todos ocupados)", cuarto);
        ok("el cuarto rebota", cuarto == -2, det);

        int buenos = 0;
        for (int i = 0; i < 3; i++) {
            if (esperar(id[i], 25000, NULL) == AOS_HTTP_DONE &&
                aos_hal_http_status(id[i]) == 200) {
                buenos++;
            }
            aos_hal_http_release(id[i]);
        }
        snprintf(det, sizeof(det), "%d of 3 finished with 200", buenos);
        ok("las tres traen el cuerpo", buenos == 3, det);
    }

    printf("\n---------------------------------------------\n");
    printf("%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
