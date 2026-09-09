/*
 * AmoledOS - translation catalogs
 *
 * See aos_i18n.h for the design and docs/I18N.md for the whole plan.
 *
 * A catalog is one tab-separated file read whole into memory, unescaped in
 * place, and indexed by a sorted array of {hash, key offset, value offset}.
 * Lookup is a binary search over the hashes plus a strcmp to settle
 * collisions. For the ~800 strings the system has, that is about ten
 * comparisons - nothing next to the cost of building a screen.
 */

#include "aos_i18n.h"
#include "aos_hal.h"
#include "aos_lang_embedded.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PREF_KEY        "lang"
#define BASE_CODE       "es"
#define SYSTEM_CATALOG  "_sistema.lang"
#define META_FILE       "meta.txt"

/* A pack bigger than this is a corrupt or hostile file, not a translation.
 * The system catalog is ~11 KB of Spanish today; 256 KB is room to grow into
 * languages that are much wordier without risking the heap on a bad card. */
#define CATALOG_MAX_BYTES  (256 * 1024)

typedef struct {
    uint32_t hash;
    uint32_t key;   /* offset into blob */
    uint32_t val;   /* offset into blob */
} entry_t;

typedef struct {
    char    *blob;      /* whole file, NUL-terminated per field after parsing */
    entry_t *index;     /* sorted by hash */
    int      count;
} catalog_t;

static catalog_t s_system;
static catalog_t s_app;
static char      s_code[AOS_LANG_CODE_MAX] = BASE_CODE;

/* -------------------------------------------------------------------------
 * hashing and lookup
 * ------------------------------------------------------------------------- */

/* FNV-1a, 32 bit. Cheap, no table, good enough spread for text keys - and
 * collisions are not a correctness problem here because every hit is
 * confirmed with strcmp. */
static uint32_t hash32(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static int cmp_entry(const void *a, const void *b)
{
    uint32_t ha = ((const entry_t *)a)->hash;
    uint32_t hb = ((const entry_t *)b)->hash;
    return (ha > hb) - (ha < hb);
}

static const char *catalog_find(const catalog_t *cat, const char *key, uint32_t h)
{
    if (!cat->index || cat->count == 0) {
        return NULL;
    }

    int lo = 0, hi = cat->count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (cat->index[mid].hash < h) {
            lo = mid + 1;
        } else if (cat->index[mid].hash > h) {
            hi = mid - 1;
        } else {
            /* Walk back to the first entry with this hash, then forward over
             * all of them: qsort gives no order among equal keys, so a
             * collision could put the wanted one on either side of 'mid'. */
            while (mid > 0 && cat->index[mid - 1].hash == h) {
                mid--;
            }
            for (int i = mid; i < cat->count && cat->index[i].hash == h; i++) {
                if (strcmp(cat->blob + cat->index[i].key, key) == 0) {
                    return cat->blob + cat->index[i].val;
                }
            }
            return NULL;
        }
    }
    return NULL;
}

/* AOS_I18N_MARK=1 prefixes every string that reached aos_tr() and came back
 * without a translation.
 *
 * What this catches: a key that is not in the catalog. Either nobody wrote
 * the translation yet, or the key does not match what the extractor filed -
 * the LV_SYMBOL_* paste being the classic case, where the run-time string
 * carries the glyph's bytes and the catalog entry does not.
 *
 * What it does NOT catch, and this is worth knowing: a string drawn from a
 * site nobody wrapped in _(). No call happens, so there is nothing to mark,
 * and the extractor is perfectly happy because the N_ in the table did its
 * job. The converter shipped exactly that bug - the unit names were in the
 * catalog and still drew in Spanish - and only a screenshot found it. For
 * that class, look at the screen.
 *
 * Off by default, read once. */
static bool mark_untranslated(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("AOS_I18N_MARK");
        cached = (v && *v && *v != '0') ? 1 : 0;
    }
    return cached == 1;
}

static const char *marked(const char *es)
{
    /* A handful of rotating buffers: LVGL copies the text on the spot, so the
     * only requirement is surviving until the caller is done with it. */
    static char ring[8][96];
    static int slot;
    slot = (slot + 1) % 8;
    snprintf(ring[slot], sizeof(ring[slot]), "\xc2\xbb%s", es);
    return ring[slot];
}

const char *aos_tr(const char *es)
{
    if (!es || !*es) {
        return es;
    }
    if (!s_system.count && !s_app.count) {
        return es;              /* nothing loaded: the common case, no hashing */
    }

    uint32_t h = hash32(es);
    const char *hit = catalog_find(&s_app, es, h);
    if (!hit) {
        hit = catalog_find(&s_system, es, h);
    }
    if (hit) {
        return hit;
    }
    return mark_untranslated() ? marked(es) : es;
}

const char *aos_trc(const char *ctx, const char *es)
{
    if (!ctx || !*ctx || !es || !*es) {
        return aos_tr(es);
    }
    if (!s_system.count && !s_app.count) {
        return es;
    }

    char key[160];
    int n = snprintf(key, sizeof(key), "%s\x04%s", ctx, es);
    if (n < 0 || (size_t)n >= sizeof(key)) {
        return es;              /* does not fit: better Spanish than the other sense */
    }

    uint32_t h = hash32(key);
    const char *hit = catalog_find(&s_app, key, h);
    if (!hit) {
        hit = catalog_find(&s_system, key, h);
    }
    if (hit) {
        return hit;
    }
    return mark_untranslated() ? marked(es) : es;
}

/* -------------------------------------------------------------------------
 * parsing
 * ------------------------------------------------------------------------- */

/* Unescapes in place and returns the new length.
 *
 * This is the whole reason the file format is escaped at all. In C,
 * _("Ene\nFeb") hands aos_tr() a string with a REAL newline in it - the
 * compiler resolved the escape long ago. A line-oriented catalog cannot carry
 * a raw newline inside a field, so the file stores the escaped form and we
 * undo it here, on both sides of the pair, before anything is hashed. Without
 * this the 24 multi-line strings in the system (the month rollers, the
 * dropdowns) miss every lookup in silence and stay Spanish while the UI around
 * them changes language.
 *
 * Unescaping only ever shrinks, so writing over the source is safe. An escape
 * we do not know is left exactly as it was found, backslash included, rather
 * than being swallowed - a catalog with a Windows path in it should survive a
 * round trip. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t unescape(char *s)
{
    char *w = s;
    for (const char *r = s; *r; ) {
        if (*r != '\\') {
            *w++ = *r++;
            continue;
        }
        switch (r[1]) {
        case 'n':  *w++ = '\n'; r += 2; break;
        case 't':  *w++ = '\t'; r += 2; break;
        case 'r':  *w++ = '\r'; r += 2; break;
        case '\\': *w++ = '\\'; r += 2; break;
        /* The double quote. gen_lang.py copies the key EXACTLY as it appears
         * between the quotes in the C source, so a _("dice \\"hola\\"") leaves
         * a real backslash in the catalogue, while the compiler already ate it
         * and at run time the string carries a bare quote. Without this case
         * the two never match.
         *
         * And they fail to match SILENTLY, which is the expensive part:
         * aos_tr() returns its own argument when it does not find the key, so
         * the screen looks perfect in Spanish and the English is not applied
         * with nothing to say so. There was already a victim in the tree
         * before this was written: buscaminas.c:385, with the seconds
         * symbol. */
        case '"':  *w++ = '"';  r += 2; break;
        case '\'': *w++ = '\''; r += 2; break;
        case 'x': {
            /* \xHH. It exists because of the 0x04 that separates context from
             * key: that way the catalogue stays printable ASCII and can be
             * opened in any editor without seeing a control character. */
            int hi = hexval(r[2]), lo = (hi >= 0) ? hexval(r[3]) : -1;
            if (lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                r += 4;
            } else {
                *w++ = *r++; *w++ = *r++;
            }
            break;
        }
        case '\0': *w++ = *r++;         break;   /* trailing backslash */
        default:   *w++ = *r++; *w++ = *r++; break;
        }
    }
    *w = '\0';
    return (size_t)(w - s);
}

static void catalog_free(catalog_t *cat)
{
    free(cat->blob);
    free(cat->index);
    cat->blob = NULL;
    cat->index = NULL;
    cat->count = 0;
}

/* Reads a whole file into a NUL-terminated malloc'd buffer. Big buffers go
 * through malloc and not lv_malloc so they land in PSRAM. */
static char *slurp(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || len > CATALOG_MAX_BYTES) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) {
        *out_len = (long)got;
    }
    return buf;
}

/* Parses an already-loaded blob into cat. Takes ownership of blob either way:
 * on failure it is freed here. */
static bool catalog_parse(catalog_t *cat, char *blob, const char *what)
{
    /* One pass to count candidate lines, so the index is allocated once. */
    int lines = 1;
    for (const char *p = blob; *p; p++) {
        if (*p == '\n') {
            lines++;
        }
    }

    entry_t *index = malloc(sizeof(entry_t) * (size_t)lines);
    if (!index) {
        free(blob);
        return false;
    }

    int n = 0;
    int malformed = 0;
    char *line = blob;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        char *next = nl ? nl + 1 : NULL;

        /* Tolerate CRLF: the catalogs are hand-edited and may come off a PC. */
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r') {
            line[ll - 1] = '\0';
        }

        if (*line && *line != '#') {
            char *tab = strchr(line, '\t');
            if (!tab) {
                malformed++;
            } else {
                *tab = '\0';
                char *key = line;
                char *val = tab + 1;
                unescape(key);
                unescape(val);
                if (*key) {
                    index[n].hash = hash32(key);
                    index[n].key  = (uint32_t)(key - blob);
                    index[n].val  = (uint32_t)(val - blob);
                    n++;
                }
            }
        }
        line = next;
    }

    qsort(index, (size_t)n, sizeof(entry_t), cmp_entry);

    cat->blob  = blob;
    cat->index = index;
    cat->count = n;

    if (malformed) {
        aos_hal_log("i18n", "%s: %d lineas sin tabulacion, ignoradas", what, malformed);
    }
    return true;
}

/* -------------------------------------------------------------------------
 * paths
 * ------------------------------------------------------------------------- */

static void pack_path(char *out, size_t len, const char *code, const char *file)
{
    snprintf(out, len, "%s/%s/%s", aos_hal_path_lang(), code, file);
}

/* The embedded pack for that code, or NULL. */
static const aos_lang_pack_t *embedded_pack(const char *code)
{
    for (int i = 0; i < aos_lang_pack_count; i++) {
        if (strcmp(aos_lang_packs[i].code, code) == 0) {
            return &aos_lang_packs[i];
        }
    }
    return NULL;
}

/* Mutable copy of an embedded catalogue. catalog_parse() unescapes IN PLACE,
 * and the embedded blob is in flash and const: without a copy, the first
 * unescape() writes into read-only memory. */
static char *embedded_dup(const char *code, const char *file)
{
    const aos_lang_pack_t *p = embedded_pack(code);
    if (!p) {
        return NULL;
    }
    for (int i = 0; i < p->nfiles; i++) {
        if (strcmp(p->files[i].file, file) == 0) {
            size_t n = strlen(p->files[i].blob);
            char *copy = malloc(n + 1);
            if (copy) {
                memcpy(copy, p->files[i].blob, n + 1);
            }
            return copy;
        }
    }
    return NULL;
}

/* The CARD beats the firmware, on purpose and in this order:
 *
 *   1. /sdcard/lang/<code>/<file>
 *   2. the catalogue embedded in the binary
 *
 * That way a bad translation is fixed by copying a file, without recompiling
 * or reflashing, and the third language comes in by exactly the same path as
 * the second instead of being a special case. */
static bool catalog_load(catalog_t *cat, const char *code, const char *file)
{
    catalog_free(cat);

    char path[192];
    pack_path(path, sizeof(path), code, file);

    long len = 0;
    char *blob = slurp(path, &len);
    if (!blob) {
        blob = embedded_dup(code, file);
    }
    if (!blob) {
        return false;
    }
    return catalog_parse(cat, blob, file);
}

/* -------------------------------------------------------------------------
 * public API
 * ------------------------------------------------------------------------- */

const char *aos_i18n_current(void)
{
    return s_code;
}

int aos_i18n_count(void)
{
    return s_system.count;
}

int aos_i18n_app_count(void)
{
    return s_app.count;
}

void aos_i18n_app_load(const char *app_id)
{
    catalog_free(&s_app);
    if (!app_id || strcmp(s_code, BASE_CODE) == 0) {
        return;
    }

    char file[AOS_LANG_CODE_MAX + 64];
    snprintf(file, sizeof(file), "%s.lang", app_id);
    if (catalog_load(&s_app, s_code, file)) {
        aos_hal_log("i18n", "catalogo de %s: %d cadenas", app_id, s_app.count);
    }
}

void aos_i18n_app_unload(void)
{
    catalog_free(&s_app);
}

bool aos_i18n_set(const char *code)
{
    if (!code || !*code) {
        return false;
    }

    if (strcmp(code, BASE_CODE) == 0) {
        catalog_free(&s_app);
        catalog_free(&s_system);
        snprintf(s_code, sizeof(s_code), "%s", BASE_CODE);
        aos_hal_pref_set_str(PREF_KEY, BASE_CODE);
        aos_hal_log("i18n", "idioma: es (fuente, sin pack)");
        return true;
    }

    catalog_t fresh = { 0 };
    char path[192];
    pack_path(path, sizeof(path), code, SYSTEM_CATALOG);
    long len = 0;
    char *blob = slurp(path, &len);
    bool desde_tarjeta = (blob != NULL);
    if (!blob) {
        blob = embedded_dup(code, SYSTEM_CATALOG);
    }
    if (!blob || !catalog_parse(&fresh, blob, SYSTEM_CATALOG)) {
        aos_hal_log("i18n", "no pude leer %s ni el embebido; sigo en %s",
                    path, s_code);
        return false;
    }

    /* Only now that the new catalog is in hand do we drop the old one, so a
     * half-readable pack cannot leave the UI with no strings at all. */
    catalog_free(&s_app);
    catalog_free(&s_system);
    s_system = fresh;
    snprintf(s_code, sizeof(s_code), "%s", code);
    aos_hal_pref_set_str(PREF_KEY, s_code);
    aos_hal_log("i18n", "idioma: %s, %d cadenas (%s)", s_code, s_system.count,
                desde_tarjeta ? "tarjeta" : "firmware");
    return true;
}

void aos_i18n_init(void)
{
    char saved[AOS_LANG_CODE_MAX] = { 0 };
    if (!aos_hal_pref_get_str(PREF_KEY, saved, sizeof(saved)) || !saved[0]) {
        return;                         /* never chosen: Spanish, nothing to do */
    }
    if (strcmp(saved, BASE_CODE) == 0) {
        return;
    }
    if (!aos_i18n_set(saved)) {
        /* The card with the pack on it may simply not be in the slot. Keep the
         * preference so the language comes back when it is, and run in Spanish
         * meanwhile - which is exactly what the source already says. */
        aos_hal_log("i18n", "pack '%s' no disponible, arranco en es", saved);
    }
}

/* Reads the display name out of a pack's meta.txt. Falls back to the
 * directory name, so a pack with a broken meta is still selectable. */
static void read_meta_name(const char *code, char *out, size_t len)
{
    snprintf(out, len, "%s", code);

    char path[192];
    pack_path(path, sizeof(path), code, META_FILE);
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "name=", 5) != 0) {
            continue;
        }
        char *v = line + 5;
        v[strcspn(v, "\r\n")] = '\0';
        if (*v) {
            /* A precision and not a bare "%s": meta.txt comes from the card,
             * that is, from outside, and a long line has to be clipped. With
             * "%s" snprintf would clip it anyway, but
             * -Werror=format-truncation rejects that, and it is right to ask
             * for the limit to be written down. */
            snprintf(out, len, "%.*s", (int)len - 1, v);
        }
        break;
    }
    fclose(f);
}

/* Counts the *.lang files in a pack other than the system one. */
static int count_app_catalogs(const char *code)
{
    char dir[192];
    snprintf(dir, sizeof(dir), "%s/%s", aos_hal_path_lang(), code);
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    int n = 0;
    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (l > 5 && strcmp(e->d_name + l - 5, ".lang") == 0 &&
            strcmp(e->d_name, SYSTEM_CATALOG) != 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

/* Adds the embedded languages the card has not already provided.
 *
 * They are compared by code and the card's wins, as in catalog_load(): if both
 * are present, the list has to show the one that will really be loaded, not
 * both. */
static int scan_add_embedded(aos_lang_t *out, int max, int n)
{
    for (int i = 0; i < aos_lang_pack_count && n < max; i++) {
        const aos_lang_pack_t *p = &aos_lang_packs[i];
        if (strcmp(p->code, BASE_CODE) == 0) {
            continue;               /* the source language is not embedded */
        }
        bool ya = false;
        for (int j = 0; j < n; j++) {
            if (strcmp(out[j].code, p->code) == 0) {
                ya = true;
                break;
            }
        }
        if (ya) {
            continue;
        }
        snprintf(out[n].code, sizeof(out[n].code), "%s", p->code);
        snprintf(out[n].name, sizeof(out[n].name), "%s", p->name);
        out[n].strings = p->strings;
        out[n].apps = p->apps;
        out[n].origin = AOS_LANG_EMBEDDED;
        n++;
    }
    return n;
}

int aos_i18n_scan(aos_lang_t *out, int max)
{
    if (!out || max < 1) {
        return 0;
    }

    /* Entry 0 is always Spanish. It is not a pack and cannot be missing: it is
     * the language the code is written in. */
    int n = 0;
    snprintf(out[n].code, sizeof(out[n].code), "%s", BASE_CODE);
    snprintf(out[n].name, sizeof(out[n].name), "%s", _("Español"));
    out[n].strings = 0;
    out[n].apps = 0;
    out[n].origin = AOS_LANG_SOURCE;
    n++;

    DIR *d = opendir(aos_hal_path_lang());
    if (!d) {
        return scan_add_embedded(out, max, n);  /* with no card the firmware's are left */
    }

    const struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= AOS_LANG_CODE_MAX) {
            continue;
        }
        if (strcmp(e->d_name, BASE_CODE) == 0) {
            continue;                   /* a pack cannot shadow the source */
        }

        /* A directory holding a readable system catalog is a pack. Testing for
         * the file rather than trusting dirent.d_type keeps this working the
         * same on FATFS and on the simulator's real filesystem. */
        char probe[192];
        pack_path(probe, sizeof(probe), e->d_name, SYSTEM_CATALOG);
        struct stat st;
        if (stat(probe, &st) != 0 || st.st_size <= 0) {
            continue;
        }

        snprintf(out[n].code, sizeof(out[n].code), "%s", e->d_name);
        read_meta_name(e->d_name, out[n].name, sizeof(out[n].name));
        out[n].apps = count_app_catalogs(e->d_name);

        /* Counts the lines that really are a pair: with a tab and not starting
         * with '#'. Counting the line breaks outright is cheaper but gives a
         * number that means nothing -1092 for 355 strings, because the
         * template carries a "#: file:line" comment per entry- and a number
         * that means nothing on a screen is worse than none. */
        char *blob = slurp(probe, NULL);
        int pairs = 0;
        if (blob) {
            const char *line = blob;
            while (line && *line) {
                const char *nl = strchr(line, '\n');
                if (*line != '#' && *line != '\n' && *line != '\r') {
                    const char *tab = strchr(line, '\t');
                    if (tab && (!nl || tab < nl)) {
                        pairs++;
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
            free(blob);
        }
        out[n].strings = pairs;
        out[n].origin = AOS_LANG_CARD;
        n++;
    }
    closedir(d);
    return scan_add_embedded(out, max, n);
}
