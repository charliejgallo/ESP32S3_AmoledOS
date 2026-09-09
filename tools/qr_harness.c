/*
 * AmoledOS - bench for the WiFi QR's text.
 *
 * It links against components/aos_ui/aos_wifi_qr.c, which has not one line of
 * LVGL or of the HAL, and tests it on the Mac. Same division of labour as
 * cz_harness.c: the maths on one side, the drawing on the other.
 *
 *   cc -Wall -Wextra -I components/aos_ui/include tools/qr_harness.c \
 *      components/aos_ui/aos_wifi_qr.c -o /tmp/qr && /tmp/qr
 *
 * Why it exists: a malformed QR DRAWS perfectly. There is no symptom. The
 * phone reads it, sees a network that does not exist and nothing happens, and
 * from the screen that looks exactly like "the scan did not work". An
 * unescaped semicolon in the network's name is enough for that.
 */
#include <stdio.h>
#include <string.h>

#include "aos_wifi_qr.h"

static int pruebas, fallos;

static void ok(const char *que, int cond, const char *detalle)
{
    pruebas++;
    printf("  %s  %-46s %s\n", cond ? "OK " : "MAL", que, detalle ? detalle : "");
    if (!cond) fallos++;
}

static void esperar(const char *que, const char *ssid, const char *pass,
                    const char *esperado)
{
    char buf[160];
    bool r = aos_wifi_qr_text(buf, sizeof(buf), ssid, pass);
    int bien = r && strcmp(buf, esperado) == 0;
    ok(que, bien, bien ? buf : (r ? buf : "(fallo)"));
    if (!bien && r) {
        printf("       esperaba: %s\n", esperado);
    }
}

int main(void)
{
    printf("\nTexto del QR de WiFi\n\n");

    esperar("caso normal", "AmoledOS-5IM", "amoledos",
            "WIFI:T:WPA;S:AmoledOS-5IM;P:amoledos;;");

    esperar("clave generada", "AmoledOS-5IM", "h3jLL2syVF",
            "WIFI:T:WPA;S:AmoledOS-5IM;P:h3jLL2syVF;;");

    esperar("espacios en el nombre", "Reloj de Charlie", "amoledos",
            "WIFI:T:WPA;S:Reloj de Charlie;P:amoledos;;");

    /* The five that give the format its structure, one by one and all
     * together. */
    esperar("punto y coma en el nombre", "A;B", "amoledos",
            "WIFI:T:WPA;S:A\\;B;P:amoledos;;");
    esperar("dos puntos en el nombre", "A:B", "amoledos",
            "WIFI:T:WPA;S:A\\:B;P:amoledos;;");
    esperar("barra invertida en el nombre", "A\\B", "amoledos",
            "WIFI:T:WPA;S:A\\\\B;P:amoledos;;");
    esperar("coma en el nombre", "A,B", "amoledos",
            "WIFI:T:WPA;S:A\\,B;P:amoledos;;");
    esperar("comillas en el nombre", "A\"B", "amoledos",
            "WIFI:T:WPA;S:A\\\"B;P:amoledos;;");
    esperar("los cinco juntos", "A;B:C\\D,E\"F", "amoledos",
            "WIFI:T:WPA;S:A\\;B\\:C\\\\D\\,E\\\"F;P:amoledos;;");
    esperar("clave con separadores", "casa", "cla;ve:rara",
            "WIFI:T:WPA;S:casa;P:cla\\;ve\\:rara;;");

    /* With no password the type changes: with T:WPA and an empty P there are
     * phones that ask for a password that does not exist. */
    esperar("red abierta", "abierta", "",
            "WIFI:T:nopass;S:abierta;P:;;");

    char buf[160];
    ok("sin nombre no hay QR",
       !aos_wifi_qr_text(buf, sizeof(buf), "", "amoledos") && buf[0] == 0, NULL);
    ok("nombre NULL no hay QR",
       !aos_wifi_qr_text(buf, sizeof(buf), NULL, "amoledos") && buf[0] == 0, NULL);

    /* A half QR is a VALID QR leading to another network: cutting silently is
     * worse than drawing nothing. */
    char chico[20];
    ok("si no entra, no devuelve nada a medias",
       !aos_wifi_qr_text(chico, sizeof(chico), "un-nombre-larguisimo", "clave") &&
       chico[0] == 0, NULL);

    /* The exact limit: 33 of SSID + 63 of password, both with every character
     * escaped, is the real worst case. */
    char ssid_max[33], pass_max[64];
    memset(ssid_max, ';', sizeof(ssid_max) - 1); ssid_max[32] = 0;
    memset(pass_max, ';', sizeof(pass_max) - 1); pass_max[63] = 0;
    char grande[256];
    bool cabe = aos_wifi_qr_text(grande, sizeof(grande), ssid_max, pass_max);
    ok("el peor caso entra en 256 bytes", cabe && strlen(grande) == 13 + 64 + 3 + 126 + 2,
       cabe ? NULL : "no entro");

    printf("\n%d pruebas, %d fallos\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
