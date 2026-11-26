/*
 * AmoledOS - The firmware's next version, from the watch itself. See
 * aos_update.h for what it does; this is how.
 *
 * The manifest is one flat JSON object, written by tools/gen_update.py:
 *
 *   {"schema":1,"ver":"v0.18.0","date":"2026-11-26","abi":2,
 *    "url":"https://github.com/<owner>/<repo>/releases/download/v0.18.0/amoledos.bin",
 *    "size":4081536,"sha256":"<64 hex>",
 *    "notes_es":"...","notes_en":"...","notes_de":"..."}
 *
 * Flat on purpose: the firmware has no JSON parser of its own (cJSON is not
 * in the simulator), and reading six known keys does not need one.
 *
 * The image is not kept anywhere on its way: each block that arrives goes
 * through the hash and into the idle slot. What makes that safe is the
 * order: the slot is set to boot only after the size and the hash are the
 * published ones, and esp_ota_end() has checked the image's own checksum on
 * top. A download that is cut, refused or damaged leaves the idle slot half
 * written and the running one, and what boots, untouched.
 *
 * Memory: the state and the notes in PSRAM; the task's 8 KB stack internal,
 * because it writes the flash (a task whose stack is in PSRAM cannot), and
 * only while a job runs: the task ends with it.
 */
#include "aos_update.h"
#include "aos_fetch.h"
#include "aos_hal.h"
#include "aos_http_stream.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/sha256.h"

#ifdef AOS_SIM
  #include <pthread.h>
  static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
  #define LOCK()    pthread_mutex_lock(&s_lock)
  #define UNLOCK()  pthread_mutex_unlock(&s_lock)
  #define LOGI(...) do { printf("[update] " __VA_ARGS__); printf("\n"); } while (0)
  #define LOGW(...) LOGI(__VA_ARGS__)
#else
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_log.h"
  #include "esp_ota_ops.h"
  #include "esp_image_format.h"
  static SemaphoreHandle_t s_lock_h;
  static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
  static void lock_make(void)
  {
      if (!s_lock_h) {
          SemaphoreHandle_t m = xSemaphoreCreateMutex();
          taskENTER_CRITICAL(&s_init_mux);
          if (!s_lock_h) {
              s_lock_h = m;
              m = NULL;
          }
          taskEXIT_CRITICAL(&s_init_mux);
          if (m) {
              vSemaphoreDelete(m);
          }
      }
  }
  #define LOCK()    (lock_make(), xSemaphoreTake(s_lock_h, portMAX_DELAY))
  #define UNLOCK()  xSemaphoreGive(s_lock_h)
  #define LOGI(...) ESP_LOGI("update", __VA_ARGS__)
  #define LOGW(...) ESP_LOGW("update", __VA_ARGS__)
#endif

#define MANIFEST_MAX    (16 * 1024)
#define NOTES_MAX       640
#define URL_MAX         512
#define PREF_SOURCE     "upd_url"       /* NVS keys are 15 characters at most */
#define STACK           8192

typedef enum { J_NONE = 0, J_CHECK, J_INSTALL } job_t;

typedef struct {
    char     ver[32], date[16];
    char     url[URL_MAX];
    char     sha[65];
    uint32_t size;
    int      abi;
    char     notes[3][NOTES_MAX];       /* es, en, de */
} offer_t;

static const char *const LANGS[3] = { "es", "en", "de" };

AOS_BSS_PSRAM static offer_t  s_offer;
AOS_BSS_PSRAM static offer_t  s_tmp;     /* the manifest being read */
AOS_BSS_PSRAM static char     s_source[URL_MAX];
static bool             s_source_read;
static aos_upd_state_t  s_state;
static aos_upd_err_t    s_err;
static char             s_why[120];
static bool             s_checked;
static uint64_t         s_checked_ms;
static bool             s_have_offer;
static char             s_previous[32];
static bool             s_previous_known;
static job_t            s_job;
static volatile bool    s_cancel;
static aos_fetch_t     *s_fetch;         /* the download in hand, for its progress */

/* ---- versions ------------------------------------------------------------ */

static long version_of(const char *v)
{
    while (*v && !isdigit((unsigned char)*v)) v++;
    int a = 0, b = 0, c = 0;
    if (sscanf(v, "%d.%d.%d", &a, &b, &c) < 2) {
        return -1;
    }
    return (long)(a & 0xFF) << 16 | (b & 0xFF) << 8 | (c & 0xFF);
}

/* After x.y.z of 'v': nothing, "-N-g<hash>" (a build after that tag), or a
 * test tag's word ("-rc.1"). "-dirty" at the end is a build with local
 * changes of whichever of those it was, and does not count. */
static bool later_build(const char *v)
{
    while (*v && !isdigit((unsigned char)*v)) v++;
    while (*v && (isdigit((unsigned char)*v) || *v == '.')) v++;
    if (v[0] != '-') {
        return false;
    }
    const char *q = v + 1;
    while (isdigit((unsigned char)*q)) q++;
    return q > v + 1 && q[0] == '-' && q[1] == 'g';
}

static bool is_tag_itself(const char *v)
{
    while (*v && !isdigit((unsigned char)*v)) v++;
    while (*v && (isdigit((unsigned char)*v) || *v == '.')) v++;
    return !v[0] || strcmp(v, "-dirty") == 0;
}

bool aos_update_newer(const char *offered, const char *running)
{
    if (!offered || !running) {
        return false;
    }
    long want = version_of(offered), have = version_of(running);
    if (want < 0 || have < 0) {
        return false;
    }
    if (want != have) {
        return want > have;
    }
    /* The same x.y.z. What runs is that release, a build after it, or one of
     * its test tags; only the last is older than the release. A test tag
     * offered (a test manifest) is not newer than anything of its x.y.z. */
    if (!is_tag_itself(offered)) {
        return false;
    }
    return !is_tag_itself(running) && !later_build(running);
}

/* ---- the manifest's JSON ------------------------------------------------- */

/* Where the value of "key" starts, past its colon; NULL when absent. */
static const char *json_at(const char *js, const char *key)
{
    size_t kl = strlen(key);
    for (const char *p = strchr(js, '"'); p; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = p + 2 + kl;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == ':') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
                return q;
            }
        }
    }
    return NULL;
}

static void put_utf8(char *out, size_t len, size_t *n, unsigned cp)
{
    char b[4];
    int k;
    if (cp < 0x80)       { b[0] = (char)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
    else                 { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                           b[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
    if (*n + (size_t)k < len) {
        memcpy(out + *n, b, (size_t)k);
        *n += (size_t)k;
    }
}

/* A string value, unescaped. False when absent or not a string. */
static bool json_str(const char *js, const char *key, char *out, size_t len)
{
    const char *p = json_at(js, key);
    if (!p || *p != '"' || !len) {
        return false;
    }
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': put_utf8(out, len, &n, '\n'); break;
            case 't': put_utf8(out, len, &n, '\t'); break;
            case 'r': break;
            case 'u': {
                unsigned cp = 0;
                int i;
                for (i = 1; i <= 4 && isxdigit((unsigned char)p[i]); i++) {
                    char c = p[i];
                    cp = cp * 16 + (unsigned)(isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
                }
                if (i <= 4) {
                    return false;
                }
                p += 4;
                /* a surrogate pair is an emoji: the watch's fonts have none */
                put_utf8(out, len, &n, (cp >= 0xD800 && cp <= 0xDFFF) ? '?' : cp);
                break;
            }
            default: put_utf8(out, len, &n, (unsigned char)*p); break;
            }
            p++;
            continue;
        }
        if (n + 1 < len) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = 0;
    return *p == '"';
}

static bool json_num(const char *js, const char *key, long *out)
{
    const char *p = json_at(js, key);
    if (!p || !(isdigit((unsigned char)*p) || *p == '-')) {
        return false;
    }
    *out = strtol(p, NULL, 10);
    return true;
}

/* ---- the source ---------------------------------------------------------- */

static void source_load(void)       /* with the lock held */
{
    if (s_source_read) {
        return;
    }
    s_source_read = true;
    if (!aos_hal_pref_get_str(PREF_SOURCE, s_source, sizeof s_source) || !s_source[0]) {
        s_source[0] = 0;
    }
}

const char *aos_update_source(void)
{
    LOCK();
    source_load();
    const char *s = s_source[0] ? s_source : AOS_UPDATE_URL_DEFAULT;
    UNLOCK();
    return s;
}

bool aos_update_set_source(const char *url)
{
    if (url && url[0] && strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) {
        return false;
    }
    if (url && strlen(url) >= URL_MAX) {
        return false;
    }
    LOCK();
    source_load();
    if (url && url[0] && strcmp(url, AOS_UPDATE_URL_DEFAULT) != 0) {
        snprintf(s_source, sizeof s_source, "%s", url);
        aos_hal_pref_set_str(PREF_SOURCE, s_source);
    } else {
        s_source[0] = 0;
        aos_hal_pref_erase(PREF_SOURCE);
    }
    /* what the other source offered is not this one's */
    s_checked = false;
    s_have_offer = false;
    if (s_state == AOS_UPD_FAILED) {
        s_state = AOS_UPD_IDLE;
    }
    UNLOCK();
    LOGI("source: %s", url && url[0] ? url : "the official one");
    return true;
}

/* ---- the other slot ------------------------------------------------------ */

/* The version in the other slot, when it is a whole, good image. Reads the
 * whole slot for its hash (~4 MB through the cache), so it runs in the job's
 * task and its answer is kept. */
static void previous_look(void)
{
    char ver[32] = "";
#ifndef AOS_SIM
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t d;
    esp_ota_img_states_t st;
    if (other && esp_ota_get_partition_description(other, &d) == ESP_OK &&
        !(esp_ota_get_state_partition(other, &st) == ESP_OK &&
          (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED))) {
        esp_partition_pos_t pos = { .offset = other->address, .size = other->size };
        esp_image_metadata_t md;
        uint64_t t0 = aos_hal_uptime_ms();
        if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) == ESP_OK) {
            snprintf(ver, sizeof ver, "%s", d.version);
        }
        LOGI("the other slot (%s): %s, looked at in %u ms", other->label,
             ver[0] ? ver : "no good image", (unsigned)(aos_hal_uptime_ms() - t0));
    }
#endif
    LOCK();
    snprintf(s_previous, sizeof s_previous, "%s", ver);
    s_previous_known = true;
    UNLOCK();
}

bool aos_update_rollback(void)
{
#ifdef AOS_SIM
    return false;
#else
    LOCK();
    bool busy = s_job != J_NONE;
    UNLOCK();
    if (busy) {
        return false;
    }
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    if (!other) {
        return false;
    }
    /* set_boot_partition checks the image whole before it points at it */
    esp_err_t err = esp_ota_set_boot_partition(other);
    if (err != ESP_OK) {
        LOGW("could not go back to %s: %s", other->label, esp_err_to_name(err));
        return false;
    }
    LOGW("%s is the boot partition again: it runs after the restart", other->label);
    LOCK();
    s_state = AOS_UPD_READY;        /* what boots next is not what runs */
    s_err = AOS_UPD_E_NONE;
    snprintf(s_why, sizeof s_why, "the previous version boots after the restart");
    UNLOCK();
    return true;
#endif
}

/* ---- the jobs ------------------------------------------------------------ */

static void fail(aos_upd_err_t e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(aos_upd_err_t e, const char *fmt, ...)
{
    char why[sizeof s_why];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    LOGW("%s", why);
    LOCK();
    s_state = AOS_UPD_FAILED;
    s_err = e;
    memcpy(s_why, why, sizeof s_why);
    UNLOCK();
}

static aos_upd_err_t kind_of(int rc)
{
    if (rc == 404 || rc == 410)              return AOS_UPD_E_NOT_THERE;
    if (rc == AOS_HTTP_ERR_SIN_HORA)         return AOS_UPD_E_CLOCK;
    if (rc == AOS_HTTP_ERR_TLS)              return AOS_UPD_E_SERVER;
    if (rc == AOS_HTTP_ERR_MEM)              return AOS_UPD_E_MEMORY;
    if (rc == AOS_FETCH_ERR_CANCEL)          return AOS_UPD_E_CANCELLED;
    return AOS_UPD_E_NETWORK;
}

static bool hex64(const char *s)
{
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return s[64] == 0;
}

static void job_check(void)
{
    char src[URL_MAX];
    bool custom;
    LOCK();
    source_load();
    custom = s_source[0] != 0;
    snprintf(src, sizeof src, "%s", custom ? s_source : AOS_UPDATE_URL_DEFAULT);
    bool look = !s_previous_known;
    UNLOCK();

    if (look) {
        previous_look();
    }
    aos_fetch_clock();

    uint64_t t0 = aos_hal_uptime_ms();
    char *js = NULL;
    int rc = aos_fetch_text(src, MANIFEST_MAX, &js, NULL, &s_cancel);
    if (rc < 200 || rc >= 300) {
        free(js);
        fail(kind_of(rc), "manifest: %s (%d)", aos_fetch_reason(rc), rc);
        return;
    }

    offer_t *o = &s_tmp;
    memset(o, 0, sizeof *o);
    long schema = 0, size = 0, abi = -1;
    json_num(js, "schema", &schema);
    json_num(js, "abi", &abi);
    bool ok = schema == 1 &&
              json_str(js, "ver", o->ver, sizeof o->ver) && o->ver[0] &&
              json_str(js, "url", o->url, sizeof o->url) &&
              json_str(js, "sha256", o->sha, sizeof o->sha) && hex64(o->sha) &&
              json_num(js, "size", &size) && size > 0;
    json_str(js, "date", o->date, sizeof o->date);
    for (int i = 0; i < 3; i++) {
        char key[12];
        snprintf(key, sizeof key, "notes_%s", LANGS[i]);
        json_str(js, key, o->notes[i], NOTES_MAX);
    }
    free(js);
    if (!ok) {
        fail(AOS_UPD_E_MANIFEST, schema > 1 ? "manifest: of schema %ld, to come" : "manifest: it does not read (schema %ld)", schema);
        return;
    }
    /* the official one's image comes over https and nothing else */
    if (strncmp(o->url, "https://", 8) != 0 && !(custom && strncmp(o->url, "http://", 7) == 0)) {
        fail(AOS_UPD_E_MANIFEST, "manifest: the image is not at an https:// address");
        return;
    }
    o->size = (uint32_t)size;
    o->abi = (int)abi;
    for (char *c = o->sha; *c; c++) {
        *c = (char)tolower((unsigned char)*c);
    }

    bool newer = aos_update_newer(o->ver, aos_hal_firmware_version());
    LOCK();
    s_offer = *o;
    s_have_offer = true;
    s_checked = true;
    s_checked_ms = aos_hal_uptime_ms();
    s_state = AOS_UPD_IDLE;
    s_err = AOS_UPD_E_NONE;
    snprintf(s_why, sizeof s_why, "%.31s offered, %.40s running", o->ver, aos_hal_firmware_version());
    UNLOCK();
    LOGI("manifest read in %u ms: %s (%u B), %s", (unsigned)(aos_hal_uptime_ms() - t0),
         o->ver, (unsigned)o->size, newer ? "newer than this one" : "not newer");
}

typedef struct {
    mbedtls_sha256_context sha;
    bool                   flash_failed;
} sink_t;

AOS_BSS_PSRAM static sink_t      s_sink;
AOS_BSS_PSRAM static aos_fetch_t s_dl;

static bool ota_sink(void *ctx, const uint8_t *data, size_t len)
{
    sink_t *k = ctx;
    mbedtls_sha256_update(&k->sha, data, len);
    if (!aos_hal_ota_write(data, len)) {
        k->flash_failed = true;     /* write() aborted already */
        return false;
    }
    return true;
}

static void job_install(void)
{
    offer_t *o = &s_tmp;
    LOCK();
    bool have = s_have_offer;
    if (have) {
        *o = s_offer;
    }
    UNLOCK();
    if (!have) {
        fail(AOS_UPD_E_UP_TO_DATE, "nothing offered: check first");
        return;
    }
    if (!aos_update_newer(o->ver, aos_hal_firmware_version())) {
        fail(AOS_UPD_E_UP_TO_DATE, "%s is not newer than %s, which runs", o->ver, aos_hal_firmware_version());
        return;
    }
    aos_fetch_clock();
    if (!aos_hal_ota_begin(o->size)) {
        fail(AOS_UPD_E_FLASH, "%s", aos_hal_ota_error());
        return;
    }
    LOCK();
    s_previous[0] = 0;              /* the other slot is being written over */
    s_previous_known = false;
    UNLOCK();

    sink_t *k = &s_sink;
    aos_fetch_t *f = &s_dl;
    memset(k, 0, sizeof *k);
    memset(f, 0, sizeof *f);
    mbedtls_sha256_init(&k->sha);
    mbedtls_sha256_starts(&k->sha, 0);
    f->url = o->url;
    f->sink = ota_sink;
    f->ctx = k;
    f->max_bytes = o->size;
    f->cancel = &s_cancel;
    LOCK();
    s_fetch = f;
    UNLOCK();

    uint64_t t0 = aos_hal_uptime_ms();
    int rc = aos_fetch_get(f);
    uint32_t got = f->got;
    unsigned char sum[32];
    mbedtls_sha256_finish(&k->sha, sum);
    mbedtls_sha256_free(&k->sha);
    bool flash_failed = k->flash_failed;
    char host[sizeof f->host];
    snprintf(host, sizeof host, "%s", f->host);
    LOCK();
    s_fetch = NULL;
    UNLOCK();
    unsigned ms = (unsigned)(aos_hal_uptime_ms() - t0);

    if (flash_failed) {
        fail(AOS_UPD_E_FLASH, "%s", aos_hal_ota_error());
        return;
    }
    if (rc < 200 || rc >= 300) {
        aos_hal_ota_abort();
        fail(kind_of(rc), "image: %s (%d) after %u of %u B", aos_fetch_reason(rc), rc,
             (unsigned)got, (unsigned)o->size);
        return;
    }
    if (got != o->size) {
        aos_hal_ota_abort();
        fail(AOS_UPD_E_DAMAGED, "image: %u B and not the %u published", (unsigned)got, (unsigned)o->size);
        return;
    }
    char hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    }
    if (strcmp(hex, o->sha) != 0) {
        aos_hal_ota_abort();
        fail(AOS_UPD_E_DAMAGED, "image: its SHA-256 is not the published one");
        return;
    }
    if (!aos_hal_ota_end()) {
        fail(AOS_UPD_E_FLASH, "%s", aos_hal_ota_error());
        return;
    }
    LOGW("%s downloaded from %s and written in %u ms (%u KB/s): it runs after a restart",
         o->ver, host, ms, ms ? (unsigned)((uint64_t)got * 1000 / 1024 / ms) : 0);
    LOCK();
    s_state = AOS_UPD_READY;
    s_err = AOS_UPD_E_NONE;
    snprintf(s_why, sizeof s_why, "%.31s is in the other slot: restart to run it", o->ver);
    UNLOCK();
}

static void job_run(job_t j)
{
    if (j == J_CHECK) {
        job_check();
    } else {
        job_install();
    }
    LOCK();
    s_job = J_NONE;
    UNLOCK();
}

#ifdef AOS_SIM
static void *job_thread(void *arg)
{
    job_run((job_t)(intptr_t)arg);
    return NULL;
}
#else
static void job_task(void *arg)
{
    job_run((job_t)(intptr_t)arg);
    vTaskDelete(NULL);
}
#endif

static bool start(job_t j)
{
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        fail(AOS_UPD_E_NO_NETWORK, "the watch is not on a network");
        return false;
    }
    LOCK();
    /* READY waits for the restart: another check would hide that */
    if (s_job != J_NONE || s_state == AOS_UPD_READY) {
        UNLOCK();
        return false;
    }
    s_job = j;
    s_cancel = false;
    s_state = j == J_CHECK ? AOS_UPD_CHECKING : AOS_UPD_DOWNLOADING;
    s_err = AOS_UPD_E_NONE;
    s_why[0] = 0;
    UNLOCK();

    bool ok;
#ifdef AOS_SIM
    pthread_t th;
    ok = pthread_create(&th, NULL, job_thread, (void *)(intptr_t)j) == 0;
    if (ok) {
        pthread_detach(th);
    }
#else
    /* xTaskCreate: the stack from internal RAM. Priority 3, under LVGL's 4:
     * the screen goes on drawing while it downloads. */
    ok = xTaskCreate(job_task, "aos_update", STACK, (void *)(intptr_t)j, 3, NULL) == pdPASS;
#endif
    if (!ok) {
        LOCK();
        s_job = J_NONE;
        UNLOCK();
        fail(AOS_UPD_E_MEMORY, "could not start the task");
    }
    return ok;
}

bool aos_update_check(void)
{
    aos_http_stream_init();
    return start(J_CHECK);
}

bool aos_update_install(void)
{
    aos_http_stream_init();
    LOCK();
    bool ok = s_have_offer && aos_update_newer(s_offer.ver, aos_hal_firmware_version());
    UNLOCK();
    return ok && start(J_INSTALL);
}

void aos_update_cancel(void)
{
    s_cancel = true;
}

void aos_update_status(aos_update_status_t *out)
{
    memset(out, 0, sizeof *out);
    LOCK();
    source_load();
    out->state = s_state;
    out->err = s_err;
    snprintf(out->why, sizeof out->why, "%s", s_why);
    out->checked = s_checked;
    out->checked_ago_s = s_checked ? (uint32_t)((aos_hal_uptime_ms() - s_checked_ms) / 1000) : 0;
    out->abi = -1;
    if (s_have_offer) {
        snprintf(out->ver, sizeof out->ver, "%s", s_offer.ver);
        snprintf(out->date, sizeof out->date, "%s", s_offer.date);
        out->size = s_offer.size;
        out->abi = s_offer.abi;
        out->newer = aos_update_newer(s_offer.ver, aos_hal_firmware_version());
    }
    if (s_fetch) {
        out->got = s_fetch->got;
        out->total = s_fetch->total ? s_fetch->total : s_offer.size;
    } else if (s_state == AOS_UPD_READY) {
        out->got = out->total = s_offer.size;
    }
    snprintf(out->previous, sizeof out->previous, "%s", s_previous);
    out->custom_source = s_source[0] != 0;
    if (out->custom_source) {
        const char *h = strstr(s_source, "://");
        h = h ? h + 3 : s_source;
        size_t n = strcspn(h, "/");
        if (n >= sizeof out->source_host) {
            n = sizeof out->source_host - 1;
        }
        memcpy(out->source_host, h, n);
        out->source_host[n] = 0;
    }
    UNLOCK();
}

void aos_update_notes(const char *lang, char *out, size_t len)
{
    if (!len) {
        return;
    }
    out[0] = 0;
    LOCK();
    if (s_have_offer) {
        int want = 1;                   /* English, unless asked otherwise */
        for (int i = 0; i < 3; i++) {
            if (lang && strcmp(lang, LANGS[i]) == 0) {
                want = i;
            }
        }
        int pick = s_offer.notes[want][0] ? want : s_offer.notes[1][0] ? 1 : 0;
        snprintf(out, len, "%s", s_offer.notes[pick]);
    }
    UNLOCK();
}

#ifdef AOS_SIM
/* tools/update_harness.c only: forget the last job, as a restart would. */
void aos_update_test_reset(void);
void aos_update_test_reset(void)
{
    LOCK();
    s_state = AOS_UPD_IDLE;
    s_err = AOS_UPD_E_NONE;
    s_checked = false;
    s_have_offer = false;
    UNLOCK();
}
#endif
