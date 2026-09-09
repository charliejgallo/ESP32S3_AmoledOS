/* AmoledOS - WiFi QR text. See aos_wifi_qr.h. */
#include "aos_wifi_qr.h"

#include <string.h>

/* The five that give the format its structure. The password the device
 * generates uses none of them -its alphabet deliberately excludes them- but
 * the one set from the portal may carry them, and so may the network's name. */
static const char ESCAPAR[] = "\\;,:\"";

/* Copies 'src' VERBATIM. It is for the format's fixed parts, which are made of
 * exactly the characters that have to be escaped in the others: passing them
 * through anexar() would turn "WIFI:T:WPA;S:" into "WIFI\:T\:WPA\;S\:". */
static bool anexar_crudo(char *out, size_t out_len, size_t *w, const char *src)
{
    size_t largo = strlen(src);
    if (*w + largo >= out_len) {
        return false;
    }
    memcpy(out + *w, src, largo);
    *w += largo;
    out[*w] = 0;
    return true;
}

/* Copies 'src' escaped onto the end of 'out'. Returns false if it does not fit. */
static bool anexar(char *out, size_t out_len, size_t *w, const char *src)
{
    for (size_t r = 0; src[r]; r++) {
        bool especial = strchr(ESCAPAR, src[r]) != NULL;
        size_t hace_falta = especial ? 2 : 1;
        if (*w + hace_falta >= out_len) {
            return false;
        }
        if (especial) {
            out[(*w)++] = '\\';
        }
        out[(*w)++] = src[r];
    }
    out[*w] = 0;
    return true;
}

bool aos_wifi_qr_text(char *out, size_t out_len,
                      const char *ssid, const char *pass)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = 0;
    if (!ssid || !ssid[0]) {
        return false;
    }
    if (!pass) {
        pass = "";
    }

    size_t w = 0;
    /* With no password the network is open and the type changes: with "WPA"
     * and an empty P, some phones ask for a password that does not exist and
     * sit there waiting. */
    const char *cabecera = pass[0] ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:";

    if (!anexar_crudo(out, out_len, &w, cabecera) ||
        !anexar(out, out_len, &w, ssid) ||
        !anexar_crudo(out, out_len, &w, ";P:") ||
        !anexar(out, out_len, &w, pass) ||
        !anexar_crudo(out, out_len, &w, ";;")) {
        out[0] = 0;
        return false;
    }
    return true;
}
