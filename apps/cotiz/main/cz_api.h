/*
 * EXCHANGE RATES (#45) - the part that does not draw: instruments, URLs and
 * reading the JSON. Without a single line of LVGL, so it can be tested on the
 * Mac with tools/cz_harness.c just like af_dsp.c with af_harness.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* How many instruments there are in total (7 dollar houses + 4 currencies). */
#define CZ_MAX          11

/* Ceiling of the body requested. The two responses MEASURED on 2026-09-03 are
 * 1223 and 895 bytes; 4096 leaves plenty of room for the API to grow without
 * anybody having to touch this, and it comes out of PSRAM. */
#define CZ_BUF_BYTES    4096

/* One instrument from the fixed list. The name that is drawn comes from HERE
 * and not from the JSON on purpose: the API sends "Contado con liquidación"
 * and "Real Brasileño", with accents, and the compiled font is Montserrat with
 * ASCII —little boxes would come out—. Besides, the API's names do not fit in
 * 368 px. */
typedef struct {
    const char *key;        /* how it is stored in the preference: "blue", "eur" */
    const char *match;      /* what to look for in the JSON: the "casa" or the "moneda" */
    const char *nombre;     /* ASCII, what is drawn */
    bool        dolar;      /* true = it comes from /v1/dolares; false, from /v1/cotizaciones */
} cz_especie_t;

extern const cz_especie_t CZ_ESPECIES[CZ_MAX];

/* One value as read. Prices go in CENTS and are integers: there is not a
 * single float in the app. The ESP32-S3 has a single-precision FPU, but a
 * double in the loop means calls into the software emulation, and besides
 * 'strtod' and '__adddf3' are not in the symbol table (the same thing the
 * tuner learned). In cents, 1524.40 is 152440 and the highest value seen
 * —tarjeta, ~1989— is 198900: plenty of room in an int32. */
typedef struct {
    bool     ok;
    int32_t  compra_cent;
    int32_t  venta_cent;
    int64_t  fecha;         /* UTC epoch of "fechaActualizacion", 0 if it did not arrive */
} cz_valor_t;

/* The complete table, indexed the same as CZ_ESPECIES. */
typedef struct {
    cz_valor_t v[CZ_MAX];
    int        leidas;      /* how many were filled in on the last pass */
} cz_datos_t;

/* Index of a key in CZ_ESPECIES, or -1. */
int cz_indice(const char *key);

/* The two URLs. They are https:// since the HAL learned TLS: dolarapi does not
 * serve over plain text, and even if it did, a rate used to decide something
 * should not be changeable by anybody along the way. */
const char *cz_url_dolares(void);
const char *cz_url_monedas(void);

/* Reads a response and fills in whatever instruments it finds, WITHOUT wiping
 * the ones already there: both queries write into the same table. Returns how
 * many it filled in on this pass. */
int cz_parse(const char *json, int len, cz_datos_t *out);

/* Formats a price in cents as "1524,40". Returns out. */
char *cz_precio(int32_t cent, char *out, int max);

/* "hace 3 min" / "hace 2 h" from the rate's epoch. With a date of 0 or a now
 * of 0 it returns an empty string. */
void cz_antiguedad(int64_t fecha, int64_t ahora, char *out, int max);
