/*
 * EXCHANGE RATES (#45) - instruments, URLs and reading the JSON.
 *
 * The reader is the same homespun one 'clima' uses: it builds no tree, it
 * looks for "key": inside a byte range. The difference is that here the
 * response is an ARRAY of objects all with the same keys, so each object {...}
 * is bounded first and the search happens inside it. Looking for "compra" in
 * the whole document would always return the first one.
 *
 * Prices are stored in whole cents. See the comment on cz_valor_t.
 */
#include "cz_api.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* The fixed list. THIS table's order is the drawing order, no matter what
 * order they are stored in in the preference: that way the screen stays stable
 * and does not reorder itself when an instrument is added from the portal. */
const cz_especie_t CZ_ESPECIES[CZ_MAX] = {
    /* key                match              name             dollar */
    { "oficial",          "oficial",         N_("Oficial"),        true  },
    { "blue",             "blue",            N_("Blue"),           true  },
    { "bolsa",            "bolsa",           N_("Bolsa (MEP)"),    true  },
    { "contadoconliqui",  "contadoconliqui", N_("Contado c/liqui"),true  },
    { "mayorista",        "mayorista",       N_("Mayorista"),      true  },
    { "cripto",           "cripto",          N_("Cripto"),         true  },
    { "tarjeta",          "tarjeta",         N_("Tarjeta"),        true  },
    { "eur",              "EUR",             N_("Euro"),           false },
    { "brl",              "BRL",             N_("Real"),           false },
    { "clp",              "CLP",             N_("Peso chileno"),   false },
    { "uyu",              "UYU",             N_("Peso uruguayo"),  false },
};

int cz_indice(const char *key)
{
    if (!key || !key[0]) {
        return -1;
    }
    for (int i = 0; i < CZ_MAX; i++) {
        if (strcmp(CZ_ESPECIES[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

const char *cz_url_dolares(void) { return "https://dolarapi.com/v1/dolares"; }
const char *cz_url_monedas(void) { return "https://dolarapi.com/v1/cotizaciones"; }

/* -------------------------------------------------------------------------- */
/* Reader                                                                      */
/* -------------------------------------------------------------------------- */

/* First character of "key"'s value within [from, end). */
static const char *j_find(const char *from, const char *end, const char *key)
{
    char pat[32];
    int n = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (n <= 0 || n >= (int)sizeof(pat) || !from || from >= end) {
        return NULL;
    }
    for (const char *p = from; p + n <= end; p++) {
        if (memcmp(p, pat, (size_t)n) == 0) {
            const char *v = p + n;
            while (v < end && (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')) {
                v++;
            }
            return (v < end) ? v : NULL;
        }
    }
    return NULL;
}

/* Copies a JSON string (the pointer arrives on the opening quote). */
static bool j_str(const char *v, const char *end, char *out, int max)
{
    if (!v || v >= end || *v != '"') {
        return false;
    }
    v++;
    int w = 0;
    while (v < end && *v != '"' && w < max - 1) {
        out[w++] = *v++;
    }
    out[w] = 0;
    return (v < end && *v == '"');
}

/* A decimal number into cents. "1480" -> 148000, "1524.4" -> 152440,
 * "296.7322" -> 29673.
 *
 * The spare decimals are truncated rather than rounded, and that is
 * deliberate: a rate rounded up reads as a price nobody offered. Truncating
 * always leaves a number that existed. */
static bool j_cent(const char *v, const char *end, int32_t *out)
{
    if (!v || v >= end) {
        return false;
    }
    bool neg = false;
    if (*v == '-') { neg = true; v++; }
    if (v >= end || *v < '0' || *v > '9') {
        return false;
    }
    int64_t entero = 0;
    while (v < end && *v >= '0' && *v <= '9') {
        entero = entero * 10 + (*v++ - '0');
        if (entero > 100000000LL) {         /* something came badly wrong */
            return false;
        }
    }
    int32_t dec = 0;
    if (v < end && *v == '.') {
        v++;
        for (int i = 0; i < 2; i++) {
            dec *= 10;
            if (v < end && *v >= '0' && *v <= '9') {
                dec += *v++ - '0';
            }
        }
        while (v < end && *v >= '0' && *v <= '9') {   /* the rest is thrown away */
            v++;
        }
    }
    int64_t cent = entero * 100 + dec;
    *out = (int32_t)(neg ? -cent : cent);
    return true;
}

/* Days since 1970-01-01 for a civil date. It is Howard Hinnant's algorithm,
 * and it goes in by hand because 'timegm' does not exist in the symbol table
 * and 'mktime' —which does— interprets the date as LOCAL: the API's arrives in
 * UTC (it ends in Z), so mktime would shift it by the time zone and the rate
 * would look hours old depending on where the board is. */
static int64_t dias_civiles(int y, int m, int d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                  /* 0..399 */
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* 0..365 */
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* "2026-09-03T20:01:00.000Z" -> UTC epoch. 0 on failure. */
static int64_t iso_epoch(const char *s)
{
    int y, mo, d, h, mi, se;
    if (!s || strlen(s) < 19) {
        return 0;
    }
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return 0;
    }
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) {
        return 0;
    }
    return dias_civiles(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + se;
}

/* End of the object starting at '{', counting braces and skipping any that
 * fall inside a string. */
static const char *obj_end(const char *p, const char *end)
{
    int hondo = 0;
    bool en_str = false;
    for (; p < end; p++) {
        if (en_str) {
            if (*p == '\\') { p++; }
            else if (*p == '"') { en_str = false; }
            continue;
        }
        if (*p == '"') { en_str = true; }
        else if (*p == '{') { hondo++; }
        else if (*p == '}' && --hondo == 0) { return p + 1; }
    }
    return NULL;
}

int cz_parse(const char *json, int len, cz_datos_t *out)
{
    if (!json || len < 8 || !out) {
        return 0;
    }
    const char *end = json + len;
    int llenadas = 0;

    for (const char *p = json; p < end; ) {
        /* By hand and not with memchr(): memchr is NOT in the symbol table the
         * firmware exports to the .so files, and adding it would mean forcing
         * a reflash over three lines. */
        const char *ini = NULL;
        for (const char *q = p; q < end; q++) {
            if (*q == '{') { ini = q; break; }
        }
        if (!ini) {
            break;
        }
        const char *fin = obj_end(ini, end);
        if (!fin) {
            break;
        }
        p = fin;

        /* Which instrument it is. In /v1/dolares every object is
         * "moneda":"USD" and what distinguishes them is the "casa"; in
         * /v1/cotizaciones it is the other way round, every one is
         * "casa":"oficial" and the "moneda" distinguishes them. That is why
         * each instrument says which of the two it is compared against. */
        char casa[24] = {0}, moneda[8] = {0};
        j_str(j_find(ini, fin, "casa"),   fin, casa,   sizeof(casa));
        j_str(j_find(ini, fin, "moneda"), fin, moneda, sizeof(moneda));

        int idx = -1;
        for (int i = 0; i < CZ_MAX; i++) {
            const char *m = CZ_ESPECIES[i].match;
            if (CZ_ESPECIES[i].dolar) {
                if (strcmp(moneda, "USD") == 0 && strcmp(casa, m) == 0) { idx = i; break; }
            } else {
                if (strcmp(moneda, m) == 0) { idx = i; break; }
            }
        }
        if (idx < 0) {
            continue;                   /* a new house this app does not know */
        }

        int32_t compra = 0, venta = 0;
        bool hay_v = j_cent(j_find(ini, fin, "venta"), fin, &venta);
        bool hay_c = j_cent(j_find(ini, fin, "compra"), fin, &compra);
        if (!hay_v) {
            continue;                   /* with no sell price there is nothing to show */
        }

        char fecha[32] = {0};
        j_str(j_find(ini, fin, "fechaActualizacion"), fin, fecha, sizeof(fecha));

        out->v[idx].ok          = true;
        out->v[idx].venta_cent  = venta;
        out->v[idx].compra_cent = hay_c ? compra : 0;
        out->v[idx].fecha       = iso_epoch(fecha);
        llenadas++;
    }

    out->leidas = llenadas;
    return llenadas;
}

/* -------------------------------------------------------------------------- */

char *cz_precio(int32_t cent, char *out, int max)
{
    /* The decimal comma, not the full stop: it is the rate of an Argentine
     * peso and it is read in Spanish. The thousands separator is deliberately
     * left out —"1.524,40" in 368 px competes with the instrument's name for
     * the width, and the number is understood just as well—. */
    snprintf(out, max, "%ld,%02d", (long)(cent / 100), (int)(cent % 100));
    return out;
}

void cz_antiguedad(int64_t fecha, int64_t ahora, char *out, int max)
{
    out[0] = 0;
    if (fecha <= 0 || ahora <= 0) {
        return;
    }
    int64_t seg = ahora - fecha;
    if (seg < 0) {
        /* The board's clock runs behind the server's. Saying "-2 min ago" is
         * worse than saying nothing. */
        return;
    }
    if (seg < 90) {
        snprintf(out, max, "recien");
    } else if (seg < 5400) {
        snprintf(out, max, "hace %d min", (int)(seg / 60));
    } else if (seg < 172800) {
        snprintf(out, max, "hace %d h", (int)(seg / 3600));
    } else {
        snprintf(out, max, "hace %d dias", (int)(seg / 86400));
    }
}
