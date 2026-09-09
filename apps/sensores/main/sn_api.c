/*
 * HA SENSORS (#46) - configuration, template, values and ring.
 * The reason for sampling ourselves is in sn_api.h.
 */
#include "sn_api.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

/* A signed decimal into hundredths. "21.8" -> 2180, "-575.1" -> -57510.
 * Returns false for "unavailable", "unknown", "" and anything that does not
 * start with a number, which is exactly what Home Assistant sends when a
 * sensor goes down. */
static bool a_centi(const char *p, int32_t *out)
{
    if (!p) {
        return false;
    }
    while (*p == ' ') {
        p++;
    }
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    else if (*p == '+') { p++; }
    if (*p < '0' || *p > '9') {
        return false;
    }
    int64_t entero = 0;
    while (*p >= '0' && *p <= '9') {
        entero = entero * 10 + (*p++ - '0');
        if (entero > 20000000LL) {      /* more than that does not fit in hundredths */
            return false;
        }
    }
    int32_t dec = 0;
    if (*p == '.') {
        p++;
        for (int i = 0; i < 2; i++) {
            dec *= 10;
            if (*p >= '0' && *p <= '9') {
                dec += *p++ - '0';
            }
        }
        /* The rest is thrown away. Home Assistant sends things like
         * "83.8218307495117" and hundredths are more than enough. */
    }
    int64_t c = entero * 100 + dec;
    *out = (int32_t)(neg ? -c : c);
    return true;
}

/* Copies up to EITHER of the separators. Returns the pointer to the separator
 * (or to the end of the string).
 *
 * Two separators and not one: with '|' alone, a record missing its unit
 * ("sensor.a|A;sensor.b|B|W") made the name eat the ';' and half of the next
 * record —the name came out as "A;sensor.b"— and of three records two were
 * read. tools/sn_harness.c found it before the board was touched. */
static const char *campo(const char *p, const char *seps, char *out, int max)
{
    int w = 0;
    while (*p && !strchr(seps, *p)) {
        if (w < max - 1) {
            out[w++] = *p;
        }
        p++;
    }
    out[w] = 0;
    return p;
}

int sn_parse_config(const char *txt, sn_sensor_t *out, int max)
{
    if (!txt || !out || max <= 0) {
        return 0;
    }
    int n = 0;
    const char *p = txt;
    while (*p && n < max) {
        sn_sensor_t *s = &out[n];
        memset(s, 0, sizeof(*s));

        p = campo(p, "|;", s->entity, sizeof(s->entity));
        if (*p == '|') {
            p = campo(p + 1, "|;", s->nombre, sizeof(s->nombre));
        }
        if (*p == '|') {
            p = campo(p + 1, "|;", s->unidad, sizeof(s->unidad));
        }
        /* Skip to the next record. It is needed when the unit field was
         * missing: without this, a short line leaves p on the previous one's
         * ';' and the loop never advances. */
        while (*p && *p != ';') {
            p++;
        }
        if (*p == ';') {
            p++;
        }

        /* With no entity there is nothing to ask about. With no name the
         * entity is used, which is ugly but readable; the portal always sends
         * a name. */
        if (!s->entity[0]) {
            continue;
        }
        if (!s->nombre[0]) {
            /* %.25s and not %s: the entity is 63 characters and the name 25,
             * and the apps compile with -Werror=format-truncation. Clipping it
             * by hand is what stops the compiler being right to complain. */
            snprintf(s->nombre, sizeof(s->nombre), "%.25s", s->entity);
        }
        n++;
    }
    return n;
}

bool sn_build_template(const sn_sensor_t *s, int n, char *out, int max)
{
    if (!s || !out || n <= 0) {
        return false;
    }
    /* A single template with every sensor inside it, separated by bars. It is
     * Remoto's lesson, measured at the time: one request over wifi takes close
     * to a second on this board, so four requests would be four seconds and
     * here it has to refresh every twenty.
     *
     * states() is used and not states.sensor.x.state because states() returns
     * 'unknown' instead of blowing up the whole template when the entity does
     * not exist: with one misspelt sensor, the rest goes on working. */
    int w = snprintf(out, max, "{\"template\":\"");
    for (int i = 0; i < n; i++) {
        w += snprintf(out + w, max - w, "%s{{states('%s')}}",
                      i ? "|" : "", s[i].entity);
        if (w >= max - 16) {
            return false;
        }
    }
    w += snprintf(out + w, max - w, "\"}");
    return (w > 0 && w < max);
}

int sn_push_values(const char *resp, int len, sn_sensor_t *s, int n)
{
    if (!resp || len <= 0 || !s) {
        return 0;
    }
    /* The response is plain text, not JSON: HA resolves the template. */
    char buf[SN_BUF_BYTES];
    int copiar = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, resp, (size_t)copiar);
    buf[copiar] = 0;

    int buenos = 0;
    const char *p = buf;
    for (int i = 0; i < n; i++) {
        char campo_txt[40];
        p = campo(p, "|", campo_txt, sizeof(campo_txt));
        if (*p == '|') {
            p++;
        }

        int32_t v;
        if (!a_centi(campo_txt, &v)) {
            /* A sensor that has gone down does NOT put a zero in the ring: a
             * zero is a value, and it would draw a dip to the chart's floor as
             * if consumption had dropped to zero. It is marked not-ok and the
             * ring stays as it was. */
            s[i].ok = false;
            continue;
        }
        s[i].ok     = true;
        s[i].ultimo = v;
        s[i].v[s[i].head] = v;
        s[i].head = (s[i].head + 1) % SN_POINTS;
        if (s[i].n < SN_POINTS) {
            s[i].n++;
        }
        buenos++;
    }
    return buenos;
}

int32_t sn_at(const sn_sensor_t *s, int i)
{
    if (!s || i < 0 || i >= s->n) {
        return 0;
    }
    /* head points at the next one to write; the oldest of the n remaining is n
     * positions back. */
    int idx = (s->head - s->n + i) % SN_POINTS;
    if (idx < 0) {
        idx += SN_POINTS;
    }
    return s->v[idx];
}

bool sn_range(const sn_sensor_t *s, int32_t *min, int32_t *max)
{
    if (!s || s->n <= 0) {
        return false;
    }
    int32_t lo = sn_at(s, 0), hi = lo;
    for (int i = 1; i < s->n; i++) {
        int32_t v = sn_at(s, i);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    /* A sensor that has not moved leaves lo == hi, and with a zero range LVGL
     * draws the line flush against an edge or divides by zero outright when
     * scaling. It is opened up a little on each side. */
    if (lo == hi) {
        int32_t margen = (lo == 0) ? 100 : (lo > 0 ? lo / 20 : -lo / 20);
        if (margen < 1) {
            margen = 1;
        }
        lo -= margen;
        hi += margen;
    }
    *min = lo;
    *max = hi;
    return true;
}

char *sn_valor(int32_t centi, char *out, int max)
{
    int32_t entero = centi / 100;
    int32_t resto  = centi % 100;
    if (resto < 0) {
        resto = -resto;
    }
    /* One decimal, and none when the decimal part contributes nothing: "50 %"
     * reads better than "50,0 %", and "1010 hPa" better than "1009,9 hPa" does
     * not —there the decimal does matter—. The rule is the value, not the
     * sensor: the decimal is shown unless it is zero.
     *
     * The sign is lost if the integer part is zero (-0.5 would give "0,5"), so
     * it is put in by hand. */
    const char *signo = (centi < 0 && entero == 0) ? "-" : "";
    int dec = resto / 10;                   /* a single decimal digit */
    if (dec == 0) {
        snprintf(out, max, "%s%ld", signo, (long)entero);
    } else {
        snprintf(out, max, "%s%ld,%d", signo, (long)entero, dec);
    }
    return out;
}
