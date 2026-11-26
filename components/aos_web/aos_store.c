/*
 * AmoledOS - The Store's side on the watch (docs/STORE.md).
 *
 * The catalogue, amoledos-store.json, is a file of the latest published
 * release; tools/gen_store.py writes it. The browser cannot read a release's
 * files (GitHub sends them without CORS), so the watch fetches it, keeps it
 * on the card, and the portal's /tienda page reads it from here and draws the
 * shop. The page also decides what an install is: it sends the watch the
 * pieces of an app or a face - where each goes, its size, its SHA-256, its
 * address - and the watch downloads them one by one.
 *
 * What the watch checks, because the page is not trusted to:
 *
 *  - every address is under the release's own (github.com/<repo>/releases/
 *    download/), or under the test source's host when one is set;
 *  - every destination is one a piece may have: apps/<name>.so|.html|.pak[.N]
 *    on the card, faces/face_<x>.so|.html in the flash, lang/<xx>/<id>.lang;
 *    plain names, no dots in front, no "..";
 *  - every piece comes with its size and its SHA-256, and takes its place
 *    only when what arrived is both: it goes to <name>.p first and is renamed
 *    over the old one after the check.
 *
 * None of that is a new door: whoever reaches the portal can already put any
 * file there with /api/upload. It keeps a careless or broken page from
 * writing where nothing should be, and a download that is cut or tampered
 * with from replacing a good file.
 *
 * What is installed is written down in /sdcard/.store/installed.txt, one line
 * per piece: its destination, size and SHA-256. A file whose receipt says the
 * catalogue's hash, and whose size is still the receipt's, is the catalogue's.
 * Apps copied by hand have no receipt; "verify" hashes them once and writes
 * one for each that turns out to be the catalogue's.
 *
 * The card's apps are loaded at boot, and so are new faces: after an install
 * the status says a restart is due, and the restart is the person's.
 *
 * Memory: the state, the pieces and the receipts in PSRAM; the job's task has
 * its 8 KB stack internal (it writes the flash and the card) and only while a
 * job runs.
 */
#include "aos_store.h"

#include "aos_hal.h"
#include "aos_fetch.h"
#include "aos_dynapp.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define TAG "store"

#define REPO_RELEASES   "https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/"
#define URL_DEFAULT     REPO_RELEASES "latest/download/amoledos-store.json"
#define FILES_OFFICIAL  REPO_RELEASES "download/"
#define PREF_SOURCE     "store_url"
#define CATALOG_MAX     (1024 * 1024)
#define PIECE_MAX       (16u * 1024 * 1024)
#define PIECES_MAX      192     /* an install is a few; a verify, every file of the card */
#define RECEIPTS_MAX    400
#define URL_MAX         256
#define DEST_MAX        64
#define BODY_MAX        (32 * 1024)
#define STACK           8192

#define BIG(n)   heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

typedef struct {
    char     dest[DEST_MAX];
    char     url[URL_MAX];
    uint32_t size;
    char     sha[65];
} piece_t;

typedef struct {
    char     dest[DEST_MAX];
    uint32_t size;
    char     sha[65];
} receipt_t;

typedef enum { J_NONE = 0, J_REFRESH, J_INSTALL, J_VERIFY } job_t;

AOS_BSS_PSRAM static piece_t   s_pieces[PIECES_MAX];
AOS_BSS_PSRAM static receipt_t s_rec[RECEIPTS_MAX];
AOS_BSS_PSRAM static char      s_source[URL_MAX];
AOS_BSS_PSRAM static aos_fetch_t s_dl;

static SemaphoreHandle_t s_lock;
static int        s_rec_n = -1;         /* -1: not read yet */
static bool       s_source_read;
static job_t      s_job;
static int        s_count, s_index;     /* pieces of the job, and which one */
static char       s_item[48];
static volatile bool s_cancel;
static bool       s_fetching;
static bool       s_last_ok = true;
static char       s_last[160];
static char       s_last_job[12];
static char       s_last_item[48];
static uint32_t   s_jobs_done;
static bool       s_restart;            /* something was installed that loads at boot */
static int        s_verified, s_adopted;

static void lock(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();   /* first called from the httpd task */
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* ---- where things go ----------------------------------------------------- */

static bool ends(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

/* A plain file name: lower case, digits and _ . -, not starting with a dot. */
static bool name_ok(const char *n, size_t max)
{
    size_t l = strlen(n);
    if (!l || l >= max || n[0] == '.' || strstr(n, "..")) {
        return false;
    }
    for (size_t i = 0; i < l; i++) {
        char c = n[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool pak_name(const char *n)
{
    const char *p = strstr(n, ".pak");
    if (!p) {
        return false;
    }
    p += 4;
    if (!*p) {
        return true;
    }
    if (*p++ != '.' || !*p) {
        return false;
    }
    while (isdigit((unsigned char)*p)) p++;
    return !*p;
}

typedef enum { W_CARD, W_FLASH } where_t;

/* The full path of a destination, and whether it is a face (in the flash).
 * False when it is not a place a piece may go. */
static bool dest_path(const char *dest, char *out, size_t len, where_t *where)
{
    const char *sd = aos_hal_path_sd_root();
    if (strncmp(dest, "apps/", 5) == 0) {
        const char *n = dest + 5;
        if (!sd || !name_ok(n, 48) || !(ends(n, ".so") || ends(n, ".html") || pak_name(n))) {
            return false;
        }
        snprintf(out, len, "%.20s/apps/%.48s", sd, n);
        *where = W_CARD;
        return true;
    }
    if (strncmp(dest, "faces/", 6) == 0) {
        const char *n = dest + 6;
        /* SPIFFS keeps 32 bytes of a name, "/faces/" and the ".p" included */
        if (strncmp(n, "face_", 5) != 0 || !name_ok(n, 22) || !(ends(n, ".so") || ends(n, ".html"))) {
            return false;
        }
        snprintf(out, len, "%s/%.22s", AOS_FACES_DIR, n);
        *where = W_FLASH;
        return true;
    }
    if (strncmp(dest, "lang/", 5) == 0) {
        const char *c = dest + 5;
        if (!sd || !islower((unsigned char)c[0]) || !islower((unsigned char)c[1]) || c[2] != '/') {
            return false;
        }
        const char *n = c + 3;
        if (!name_ok(n, 48) || !ends(n, ".lang")) {
            return false;
        }
        snprintf(out, len, "%.20s/lang/%.2s/%.48s", sd, c, n);
        *where = W_CARD;
        return true;
    }
    return false;
}

/* ---- the source ---------------------------------------------------------- */

static void source_load(void)       /* with the lock */
{
    if (!s_source_read) {
        s_source_read = true;
        if (!aos_hal_pref_get_str(PREF_SOURCE, s_source, sizeof s_source)) {
            s_source[0] = 0;
        }
    }
}

/* The addresses pieces may come from: the release's, or the test source's
 * scheme and host. */
static bool url_allowed(const char *url)
{
    if (strncmp(url, FILES_OFFICIAL, strlen(FILES_OFFICIAL)) == 0) {
        return true;
    }
    lock();
    source_load();
    bool ok = false;
    if (s_source[0]) {
        const char *h = strstr(s_source, "://");
        if (h) {
            const char *slash = strchr(h + 3, '/');
            size_t n = slash ? (size_t)(slash - s_source) + 1 : strlen(s_source);
            ok = strncmp(url, s_source, n) == 0;
        }
    }
    unlock();
    return ok;
}

static void catalog_path(char *out, size_t len, const char *suffix)
{
    snprintf(out, len, "%s/.store/catalog.json%s", aos_hal_path_sd_root(), suffix);
}

static void receipts_path(char *out, size_t len, const char *suffix)
{
    snprintf(out, len, "%s/.store/installed.txt%s", aos_hal_path_sd_root(), suffix);
}

static void store_dir(void)
{
    char d[48];
    snprintf(d, sizeof d, "%s/.store", aos_hal_path_sd_root());
    mkdir(d, 0775);
}

/* ---- receipts ------------------------------------------------------------ */

static void receipts_load(void)     /* with the lock */
{
    if (s_rec_n >= 0 || !aos_hal_path_sd_root()) {
        return;
    }
    s_rec_n = 0;
    char p[64];
    receipts_path(p, sizeof p, "");
    FILE *f = fopen(p, "r");
    if (!f) {
        return;
    }
    char line[160];
    while (s_rec_n < RECEIPTS_MAX && fgets(line, sizeof line, f)) {
        receipt_t *r = &s_rec[s_rec_n];
        unsigned size;
        if (sscanf(line, "%63[^\t]\t%u\t%64s", r->dest, &size, r->sha) == 3 && strlen(r->sha) == 64) {
            r->size = size;
            s_rec_n++;
        }
    }
    fclose(f);
}

static bool receipts_save(void)     /* with the lock */
{
    char p[64], tmp[64];
    receipts_path(p, sizeof p, "");
    receipts_path(tmp, sizeof tmp, ".p");
    store_dir();
    FILE *f = fopen(tmp, "w");
    if (!f) {
        return false;
    }
    for (int i = 0; i < s_rec_n; i++) {
        fprintf(f, "%s\t%u\t%s\n", s_rec[i].dest, (unsigned)s_rec[i].size, s_rec[i].sha);
    }
    bool ok = fclose(f) == 0;
    if (ok) {
        remove(p);
        ok = rename(tmp, p) == 0;
    }
    return ok;
}

static receipt_t *receipt_find(const char *dest)   /* with the lock */
{
    receipts_load();
    for (int i = 0; i < s_rec_n; i++) {
        if (strcmp(s_rec[i].dest, dest) == 0) {
            return &s_rec[i];
        }
    }
    return NULL;
}

static void receipt_set(const char *dest, uint32_t size, const char *sha)
{
    lock();
    receipt_t *r = receipt_find(dest);
    if (!r && s_rec_n >= 0 && s_rec_n < RECEIPTS_MAX) {
        r = &s_rec[s_rec_n++];
        snprintf(r->dest, sizeof r->dest, "%s", dest);
    }
    if (r) {
        r->size = size;
        snprintf(r->sha, sizeof r->sha, "%s", sha);
        receipts_save();
    }
    unlock();
}

static void receipt_drop(const char *dest)
{
    lock();
    receipt_t *r = receipt_find(dest);
    if (r) {
        *r = s_rec[--s_rec_n];
        receipts_save();
    }
    unlock();
}

/* ---- the jobs ------------------------------------------------------------ */

static void finish(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void finish(bool ok, const char *fmt, ...)
{
    char line[sizeof s_last];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (ok) {
        ESP_LOGI(TAG, "%s", line);
    } else {
        ESP_LOGW(TAG, "%s", line);
    }
    lock();
    s_last_ok = ok;
    memcpy(s_last, line, sizeof s_last);
    snprintf(s_last_job, sizeof s_last_job, "%s",
             s_job == J_REFRESH ? "refresh" : s_job == J_INSTALL ? "install" : "verify");
    snprintf(s_last_item, sizeof s_last_item, "%s", s_item);
    unlock();
}

typedef struct {
    FILE                  *f;
    mbedtls_sha256_context sha;
    bool                   write_failed;
} sink_t;

AOS_BSS_PSRAM static sink_t s_sink;

static bool file_sink(void *ctx, const uint8_t *data, size_t len)
{
    sink_t *k = ctx;
    mbedtls_sha256_update(&k->sha, data, len);
    if (fwrite(data, 1, len, k->f) != len) {
        k->write_failed = true;
        return false;
    }
    return true;
}

static void hex_of(const unsigned char sum[32], char out[65])
{
    for (int i = 0; i < 32; i++) {
        snprintf(out + 2 * i, 3, "%02x", sum[i]);
    }
}

/* Downloads 'url' into 'path'.p, checks it against size and sha (sha NULL:
 * no check, max_bytes caps it), and renames it over 'path'. 0, or why not. */
static const char *fetch_into(const char *url, const char *path, uint32_t size,
                              const char *sha, uint32_t max_bytes)
{
    char part[96];
    snprintf(part, sizeof part, "%s.p", path);
    sink_t *k = &s_sink;
    memset(k, 0, sizeof *k);
    k->f = fopen(part, "wb");
    if (!k->f) {
        return "could not create the file";
    }
    mbedtls_sha256_init(&k->sha);
    mbedtls_sha256_starts(&k->sha, 0);
    aos_fetch_t *f = &s_dl;
    memset(f, 0, sizeof *f);
    f->url = url;
    f->sink = file_sink;
    f->ctx = k;
    f->max_bytes = max_bytes;
    f->cancel = &s_cancel;
    lock();
    s_fetching = true;
    unlock();
    int rc = aos_fetch_get(f);
    lock();
    s_fetching = false;
    unlock();
    unsigned char sum[32];
    mbedtls_sha256_finish(&k->sha, sum);
    mbedtls_sha256_free(&k->sha);
    bool closed = fclose(k->f) == 0;
    static char why[120];
    if (rc < 200 || rc >= 300) {
        remove(part);
        snprintf(why, sizeof why, "%s (%d)", aos_fetch_reason(rc), rc);
        return why;
    }
    if (k->write_failed || !closed) {
        remove(part);
        return "could not write the file (full?)";
    }
    if (sha) {
        char hex[65];
        hex_of(sum, hex);
        if (f->got != size || strcmp(hex, sha) != 0) {
            remove(part);
            snprintf(why, sizeof why, "not the published file (%u B of %u, or its hash)",
                     (unsigned)f->got, (unsigned)size);
            return why;
        }
    }
    remove(path);
    if (rename(part, path) != 0) {
        remove(part);
        return "could not put it in its place";
    }
    return NULL;
}

/* The same file at another path, for a face's copy on the card. */
static bool copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb");
    if (!a) {
        return false;
    }
    FILE *b = fopen(to, "wb");
    if (!b) {
        fclose(a);
        return false;
    }
    char *buf = BIG(4096);
    bool ok = buf != NULL;
    size_t n;
    while (ok && (n = fread(buf, 1, 4096, a)) > 0) {
        ok = fwrite(buf, 1, n, b) == n;
    }
    free(buf);
    fclose(a);
    return fclose(b) == 0 && ok;
}

static void mkdirs_for(const char *path)
{
    char d[96];
    snprintf(d, sizeof d, "%s", path);
    for (char *p = d + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(d, 0775);
            *p = '/';
        }
    }
}

/* The SHA-256 of a file, into hex; false when it cannot be read. */
static bool hash_file(const char *path, char hex[65], uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    char *buf = BIG(8192);
    mbedtls_sha256_context *c = BIG(sizeof *c);
    if (!buf || !c) {
        free(buf);
        free(c);
        fclose(f);
        return false;
    }
    mbedtls_sha256_init(c);
    mbedtls_sha256_starts(c, 0);
    size_t n, total = 0;
    while ((n = fread(buf, 1, 8192, f)) > 0) {
        mbedtls_sha256_update(c, (unsigned char *)buf, n);
        total += n;
    }
    unsigned char sum[32];
    mbedtls_sha256_finish(c, sum);
    mbedtls_sha256_free(c);
    free(c);
    free(buf);
    fclose(f);
    hex_of(sum, hex);
    *size = (uint32_t)total;
    return true;
}

static void job_refresh(void)
{
    char src[URL_MAX], path[64];
    lock();
    source_load();
    snprintf(src, sizeof src, "%s", s_source[0] ? s_source : URL_DEFAULT);
    unlock();
    store_dir();
    catalog_path(path, sizeof path, "");
    aos_fetch_clock();
    uint64_t t0 = aos_hal_uptime_ms();
    const char *why = fetch_into(src, path, 0, NULL, CATALOG_MAX);
    if (why) {
        finish(false, "catalogue: %s", why);
        return;
    }
    struct stat st;
    stat(path, &st);
    finish(true, "catalogue: %u B in %u ms", (unsigned)st.st_size, (unsigned)(aos_hal_uptime_ms() - t0));
}

static void job_install(void)
{
    aos_fetch_clock();
    uint64_t t0 = aos_hal_uptime_ms();
    uint32_t bytes = 0;
    int fetched = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_cancel) {
            finish(false, "%s: cancelled", s_item);
            return;
        }
        lock();
        s_index = i;
        unlock();
        piece_t *p = &s_pieces[i];
        char path[96];
        where_t where;
        dest_path(p->dest, path, sizeof path, &where);     /* checked when it was asked */

        /* already the catalogue's: by its receipt and its size */
        struct stat st;
        lock();
        receipt_t *r = receipt_find(p->dest);
        bool same = r && strcmp(r->sha, p->sha) == 0 && r->size == p->size;
        unlock();
        if (same && stat(path, &st) == 0 && (uint32_t)st.st_size == p->size) {
            continue;
        }
        mkdirs_for(path);
        const char *why = fetch_into(p->url, path, p->size, p->sha, p->size);
        if (why) {
            finish(false, "%s: %s: %s", s_item, p->dest, why);
            return;
        }
        fetched++;
        bytes += p->size;
        receipt_set(p->dest, p->size, p->sha);
        if (where == W_FLASH) {
            const char *n = strrchr(path, '/') + 1;
            aos_dynapp_faces_changed(n);
            /* the card's copy is what the boot installs from: keep it the same */
            const char *sd = aos_hal_path_sd_root();
            char card[96];
            snprintf(card, sizeof card, "%s/%s/%s", sd ? sd : "", AOS_FACES_CARD_SUBDIR, n);
            if (sd && stat(card, &st) == 0) {
                copy_file(path, card);
            }
        }
        if (where == W_FLASH || ends(p->dest, ".so")) {
            lock();
            s_restart = true;
            unlock();
        }
    }
    unsigned ms = (unsigned)(aos_hal_uptime_ms() - t0);
    finish(true, "%s: %d of %d files downloaded, %u KB in %u ms", s_item, fetched, s_count,
           (unsigned)(bytes / 1024), ms);
}

static void job_verify(void)
{
    int adopted = 0, looked = 0;
    for (int i = 0; i < s_count && !s_cancel; i++) {
        lock();
        s_index = i;
        unlock();
        piece_t *p = &s_pieces[i];
        char path[96], hex[65];
        where_t where;
        struct stat st;
        uint32_t size;
        if (!dest_path(p->dest, path, sizeof path, &where) || stat(path, &st) != 0 ||
            (uint32_t)st.st_size != p->size) {
            continue;
        }
        looked++;
        if (hash_file(path, hex, &size) && size == p->size && strcmp(hex, p->sha) == 0) {
            receipt_set(p->dest, p->size, p->sha);
            adopted++;
        }
    }
    lock();
    s_verified = looked;
    s_adopted = adopted;
    unlock();
    finish(true, "verify: %d of %d files the catalogue's", adopted, looked);
}

static void job_task(void *arg)
{
    job_t j = (job_t)(intptr_t)arg;
    if (j == J_REFRESH) {
        job_refresh();
    } else if (j == J_INSTALL) {
        job_install();
    } else {
        job_verify();
    }
    lock();
    s_job = J_NONE;
    s_jobs_done++;
    unlock();
    vTaskDelete(NULL);
}

/* Starts a job; with the lock held, s_job already set. */
static bool start(job_t j)
{
    s_cancel = false;
    s_index = 0;
    bool ok = xTaskCreate(job_task, "aos_store", STACK, (void *)(intptr_t)j, 3, NULL) == pdPASS;
    if (!ok) {
        s_job = J_NONE;
    }
    return ok;
}

/* ---- the portal's side --------------------------------------------------- */

static esp_err_t reply(httpd_req_t *req, int code, const char *json)
{
    if (code != 200) {
        httpd_resp_set_status(req, code == 409 ? "409 Conflict" : code == 404 ? "404 Not Found"
                                 : "400 Bad Request");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t fail_reply(httpd_req_t *req, int code, const char *error)
{
    char out[200];
    snprintf(out, sizeof out, "{\"ok\":false,\"error\":\"%s\"}", error);
    return reply(req, code, out);
}

/* The whole body, NUL-terminated, in PSRAM; NULL having answered. */
static char *body_of(httpd_req_t *req, size_t max)
{
    if (req->content_len <= 0 || req->content_len >= max) {
        fail_reply(req, 400, "cuerpo invalido");
        return NULL;
    }
    char *b = BIG(req->content_len + 1);
    if (!b) {
        fail_reply(req, 400, "sin memoria");
        return NULL;
    }
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, b + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(b);
            return NULL;
        }
        got += r;
    }
    b[got] = 0;
    return b;
}

static bool sha_ok(const char *s)
{
    if (strlen(s) != 64) {
        return false;
    }
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/* GET /api/store */
esp_err_t aos_store_get_handler(httpd_req_t *req)
{
    char path[64], src[URL_MAX];
    struct stat st;
    bool sd = aos_hal_path_sd_root() != NULL;
    bool have = false;
    long size = 0, age = -1;
    if (sd) {
        catalog_path(path, sizeof path, "");
        if (stat(path, &st) == 0) {
            have = true;
            size = (long)st.st_size;
            time_t now = time(NULL);
            age = now > st.st_mtime ? (long)(now - st.st_mtime) : 0;
        }
    }
    char *out = BIG(1024);
    if (!out) {
        return fail_reply(req, 400, "sin memoria");
    }
    lock();
    source_load();
    snprintf(src, sizeof src, "%s", s_source[0] ? s_source : URL_DEFAULT);
    uint32_t got = s_fetching ? s_dl.got : 0, total = s_fetching ? s_dl.total : 0;
    const char *piece = (s_job == J_INSTALL || s_job == J_VERIFY) && s_index < s_count
                        ? s_pieces[s_index].dest : "";
    snprintf(out, 1024,
             "{\"sd\":%s,\"busy\":%s,\"job\":\"%s\",\"item\":\"%s\",\"piece\":\"%s\",\"index\":%d,"
             "\"count\":%d,\"got\":%u,\"total\":%u,\"jobs_done\":%u,\"last_ok\":%s,\"last\":\"%s\","
             "\"last_job\":\"%s\",\"last_item\":\"%s\",\"restart\":%s,\"verified\":%d,\"adopted\":%d,"
             "\"catalog\":%s,\"catalog_bytes\":%ld,\"catalog_age_s\":%ld,\"custom_source\":%s,"
             "\"source\":\"%s\",\"abi_here\":%d,\"running\":\"%s\"}",
             sd ? "true" : "false", s_job ? "true" : "false",
             s_job == J_REFRESH ? "refresh" : s_job == J_INSTALL ? "install" : s_job == J_VERIFY ? "verify" : "",
             s_job ? s_item : "", piece, s_index, s_count, (unsigned)got, (unsigned)total,
             (unsigned)s_jobs_done, s_last_ok ? "true" : "false", s_last, s_last_job, s_last_item,
             s_restart ? "true" : "false", s_verified, s_adopted, have ? "true" : "false", size, age,
             s_source[0] ? "true" : "false", src, AOS_STORE_ABI, aos_hal_firmware_version());
    unlock();
    esp_err_t r = reply(req, 200, out);
    free(out);
    return r;
}

/* GET /api/store/catalog: the catalogue as it came, from the card. */
esp_err_t aos_store_catalog_handler(httpd_req_t *req)
{
    char path[64];
    if (!aos_hal_path_sd_root()) {
        return fail_reply(req, 404, "sin tarjeta");
    }
    catalog_path(path, sizeof path, "");
    FILE *f = fopen(path, "rb");
    if (!f) {
        return fail_reply(req, 404, "todavia no se bajo el catalogo");
    }
    char *buf = BIG(4096);
    if (!buf) {
        fclose(f);
        return fail_reply(req, 400, "sin memoria");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t n;
    esp_err_t r = ESP_OK;
    while (r == ESP_OK && (n = fread(buf, 1, 4096, f)) > 0) {
        r = httpd_resp_send_chunk(req, buf, n);
    }
    fclose(f);
    free(buf);
    if (r == ESP_OK) {
        r = httpd_resp_send_chunk(req, NULL, 0);
    }
    return r;
}

/* POST /api/store: accion=refrescar | cancelar | reiniciado; fuente=<address> */
esp_err_t aos_store_post_handler(httpd_req_t *req)
{
    char *b = body_of(req, 1024);
    if (!b) {
        return ESP_FAIL;
    }
    char accion[16] = "", fuente[URL_MAX] = "";
    bool has_source = httpd_query_key_value(b, "fuente", fuente, sizeof fuente) == ESP_OK;
    httpd_query_key_value(b, "accion", accion, sizeof accion);
    free(b);

    if (has_source) {
        /* url-decoded by hand: %XX and + */
        char *w = fuente;
        for (const char *r = fuente; *r; r++) {
            if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
                char h[3] = { r[1], r[2], 0 };
                *w++ = (char)strtol(h, NULL, 16);
                r += 2;
            } else {
                *w++ = *r == '+' ? ' ' : *r;
            }
        }
        *w = 0;
        bool plain = true;
        for (const char *c = fuente; *c; c++) {
            plain &= (unsigned char)*c > ' ' && *c != '"' && *c != '\\';
        }
        if (!plain || (fuente[0] && strncmp(fuente, "https://", 8) != 0 && strncmp(fuente, "http://", 7) != 0)) {
            return fail_reply(req, 400, "la fuente es una direccion http(s)://");
        }
        lock();
        source_load();
        if (fuente[0] && strcmp(fuente, URL_DEFAULT) != 0) {
            snprintf(s_source, sizeof s_source, "%s", fuente);
            aos_hal_pref_set_str(PREF_SOURCE, s_source);
        } else {
            s_source[0] = 0;
            aos_hal_pref_erase(PREF_SOURCE);
        }
        unlock();
        /* the catalogue kept is the other source's: its addresses are not
         * this one's, and showing it would offer what cannot be installed */
        if (aos_hal_path_sd_root()) {
            char path[64];
            catalog_path(path, sizeof path, "");
            remove(path);
        }
        ESP_LOGI(TAG, "source: %s", fuente[0] ? fuente : "the official one");
    }
    if (!strcmp(accion, "refrescar")) {
        if (!aos_hal_path_sd_root()) {
            return fail_reply(req, 409, "sin tarjeta");
        }
        if (aos_hal_net_state() != AOS_NET_CONNECTED) {
            return fail_reply(req, 409, "sin red");
        }
        lock();
        bool ok = s_job == J_NONE;
        if (ok) {
            s_job = J_REFRESH;
            snprintf(s_item, sizeof s_item, "catalog");
            ok = start(J_REFRESH);
        }
        unlock();
        if (!ok) {
            return fail_reply(req, 409, "ocupado");
        }
    } else if (!strcmp(accion, "cancelar")) {
        s_cancel = true;
    } else if (accion[0]) {
        return fail_reply(req, 400, "accion desconocida");
    }
    return reply(req, 200, "{\"ok\":true}");
}

/* Reads "dest\tsize\tsha[\turl]" lines into s_pieces (with the lock, no job
 * running). The first line may be "item <id>". NULL, or why not. */
static const char *parse_pieces(char *b, bool urls)
{
    s_count = 0;
    s_item[0] = 0;
    char *save = NULL;
    for (char *line = strtok_r(b, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') {
            line[--l] = 0;
        }
        if (!l) {
            continue;
        }
        if (!strncmp(line, "item ", 5)) {
            snprintf(s_item, sizeof s_item, "%.47s", line + 5);
            continue;
        }
        if (s_count >= PIECES_MAX) {
            return "demasiadas piezas";
        }
        piece_t *p = &s_pieces[s_count];
        char *f[4] = { 0 };
        int n = 0;
        char *s2 = NULL;
        for (char *t = strtok_r(line, "\t", &s2); t && n < 4; t = strtok_r(NULL, "\t", &s2)) {
            f[n++] = t;
        }
        if (n < (urls ? 4 : 3)) {
            return "una linea incompleta";
        }
        char path[96];
        where_t where;
        if (strlen(f[0]) >= DEST_MAX || !dest_path(f[0], path, sizeof path, &where)) {
            return "un destino que no se permite";
        }
        unsigned long size = strtoul(f[1], NULL, 10);
        if (!size || size > PIECE_MAX || !sha_ok(f[2])) {
            return "un tamano o un hash que no sirven";
        }
        if (urls && (strlen(f[3]) >= URL_MAX || !url_allowed(f[3]))) {
            return "una direccion que no es la de la release";
        }
        snprintf(p->dest, sizeof p->dest, "%s", f[0]);
        p->size = (uint32_t)size;
        snprintf(p->sha, sizeof p->sha, "%s", f[2]);
        snprintf(p->url, sizeof p->url, "%s", urls ? f[3] : "");
        s_count++;
    }
    return s_count ? NULL : "nada que hacer";
}

/* POST /api/store/install (install) and /api/store/verify (verify). */
static esp_err_t job_handler(httpd_req_t *req, job_t j)
{
    if (!aos_hal_path_sd_root()) {
        return fail_reply(req, 409, "sin tarjeta");
    }
    if (j == J_INSTALL && aos_hal_net_state() != AOS_NET_CONNECTED) {
        return fail_reply(req, 409, "sin red");
    }
    char *b = body_of(req, BODY_MAX);
    if (!b) {
        return ESP_FAIL;
    }
    lock();
    if (s_job != J_NONE) {
        unlock();
        free(b);
        return fail_reply(req, 409, "ocupado");
    }
    s_job = j;          /* taken now: the pieces are this job's */
    unlock();
    const char *why = parse_pieces(b, j == J_INSTALL);
    free(b);
    lock();
    if (why) {
        s_job = J_NONE;
        unlock();
        return fail_reply(req, 400, why);
    }
    if (j == J_VERIFY && !s_item[0]) {
        snprintf(s_item, sizeof s_item, "verify");
    }
    bool ok = start(j);
    unlock();
    return ok ? reply(req, 200, "{\"ok\":true}") : fail_reply(req, 409, "no se pudo empezar");
}

esp_err_t aos_store_install_handler(httpd_req_t *req)
{
    return job_handler(req, J_INSTALL);
}

esp_err_t aos_store_verify_handler(httpd_req_t *req)
{
    return job_handler(req, J_VERIFY);
}

/* POST /api/store/look: one destination a line; answers one a line,
 * "dest\tsize\tsha", size -1 when the file is not there and sha "-" when it
 * has no receipt, or one of another size. */
esp_err_t aos_store_look_handler(httpd_req_t *req)
{
    char *b = body_of(req, BODY_MAX);
    if (!b) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain");
    char *save = NULL, line[160];
    esp_err_t r = ESP_OK;
    for (char *d = strtok_r(b, "\n", &save); d && r == ESP_OK; d = strtok_r(NULL, "\n", &save)) {
        size_t l = strlen(d);
        if (l && d[l - 1] == '\r') {
            d[--l] = 0;
        }
        char path[96];
        where_t where;
        struct stat st;
        if (!l || l >= DEST_MAX || !dest_path(d, path, sizeof path, &where)) {
            continue;
        }
        long size = stat(path, &st) == 0 ? (long)st.st_size : -1;
        char sha[65] = "-";
        lock();
        receipt_t *rc = receipt_find(d);
        if (rc && size == (long)rc->size) {
            snprintf(sha, sizeof sha, "%s", rc->sha);
        }
        unlock();
        int n = snprintf(line, sizeof line, "%s\t%ld\t%s\n", d, size, sha);
        r = httpd_resp_send_chunk(req, line, n);
    }
    free(b);
    if (r == ESP_OK) {
        r = httpd_resp_send_chunk(req, NULL, 0);
    }
    return r;
}

/* POST /api/store/remove: one destination a line; each file goes, and its
 * receipt. A face in use is taken off first by /api/esferas/quitar. */
esp_err_t aos_store_remove_handler(httpd_req_t *req)
{
    char *b = body_of(req, BODY_MAX);
    if (!b) {
        return ESP_FAIL;
    }
    int removed = 0;
    bool loads = false;
    char *save = NULL;
    for (char *d = strtok_r(b, "\n", &save); d; d = strtok_r(NULL, "\n", &save)) {
        size_t l = strlen(d);
        if (l && d[l - 1] == '\r') {
            d[--l] = 0;
        }
        char path[96];
        where_t where;
        if (!l || l >= DEST_MAX || !dest_path(d, path, sizeof path, &where)) {
            continue;
        }
        if (remove(path) == 0) {
            removed++;
            if (where == W_FLASH) {
                aos_dynapp_faces_changed(strrchr(path, '/') + 1);
            }
            loads |= where == W_FLASH || ends(d, ".so");
        }
        receipt_drop(d);
    }
    free(b);
    if (loads) {
        lock();
        s_restart = true;
        unlock();
    }
    char out[64];
    snprintf(out, sizeof out, "{\"ok\":true,\"removed\":%d}", removed);
    ESP_LOGI(TAG, "removed %d files", removed);
    return reply(req, 200, out);
}
