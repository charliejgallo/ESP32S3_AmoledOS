/*
 * AmoledOS - bench for the updater: aos_fetch.c and aos_update.c, the real
 * files, on the Mac.
 *
 * What it checks:
 *   1. aos_update_newer() against the versions git describe gives.
 *   2. aos_fetch_get() through GitHub's two redirects, to a file of the
 *      latest release (it needs the internet).
 *   3. aos_update_check() + aos_update_install() against a manifest served by
 *      tools/update_test_server.py, with the OTA's three calls stubbed here:
 *      what reaches the "flash" is compared with the file served. A damaged
 *      image (wrong hash) must be refused and never "set to boot".
 *
 * Build and run (from the repository):
 *     cc -DAOS_SIM -I components/aos_hal/include -I components/aos_hal \
 *        -I $(brew --prefix mbedtls)/include \
 *        tools/update_harness.c components/aos_hal/aos_http.c \
 *        components/aos_hal/aos_fetch.c components/aos_hal/aos_update.c \
 *        -L $(brew --prefix mbedtls)/lib -lmbedtls -lmbedx509 -lmbedcrypto \
 *        -o /tmp/update_harness
 *     python3 tools/update_test_server.py --port 18393 &   # case 3
 *     /tmp/update_harness [http://127.0.0.1:18393]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>

#include "aos_hal.h"
#include "aos_fetch.h"
#include "aos_update.h"

/* ---- what aos_http.c, aos_fetch.c and aos_update.c ask of the HAL -------- */

static const char *g_version = "v0.17.0-12-gabcdef0";
bool aos_hal_time_is_valid(void) { return true; }
const char *aos_hal_firmware_version(void) { return g_version; }
uint64_t aos_hal_uptime_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
aos_net_state_t aos_hal_net_state(void) { return AOS_NET_CONNECTED; }
bool aos_hal_net_sync_time(void) { return true; }
void aos_hal_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }

static char g_pref[512];
bool aos_hal_pref_get_str(const char *k, char *out, size_t n) { (void)k; snprintf(out, n, "%s", g_pref); return g_pref[0]; }
bool aos_hal_pref_set_str(const char *k, const char *v) { (void)k; snprintf(g_pref, sizeof g_pref, "%s", v); return true; }
bool aos_hal_pref_erase(const char *k) { (void)k; g_pref[0] = 0; return true; }

/* The "flash": what was written, and whether it was set to boot. */
static unsigned char *g_flash;
static size_t g_flash_len, g_flash_cap;
static bool g_open, g_booted, g_aborted;
bool aos_hal_ota_begin(size_t total)
{
    free(g_flash);
    g_flash = malloc(total ? total : 1);
    g_flash_cap = total;
    g_flash_len = 0;
    g_open = true;
    g_booted = g_aborted = false;
    return true;
}
bool aos_hal_ota_write(const void *d, size_t n)
{
    if (!g_open || g_flash_len + n > g_flash_cap) return false;
    memcpy(g_flash + g_flash_len, d, n);
    g_flash_len += n;
    return true;
}
bool aos_hal_ota_end(void) { if (!g_open) return false; g_open = false; g_booted = true; return true; }
void aos_hal_ota_abort(void) { if (g_open) g_aborted = true; g_open = false; }
const char *aos_hal_ota_error(void) { return "stub"; }

/* -------------------------------------------------------------------------- */

static int fails, tests;
static void ok(const char *what, bool cond, const char *detail)
{
    tests++;
    printf("  %s %-52s %s\n", cond ? "OK " : "BAD", what, detail ? detail : "");
    if (!cond) fails++;
}

static bool sink_count(void *ctx, const uint8_t *d, size_t n) { (void)d; *(size_t *)ctx += n; return true; }

static aos_upd_state_t wait_job(int max_s)
{
    aos_update_status_t st;
    for (int i = 0; i < max_s * 20; i++) {
        aos_update_status(&st);
        if (st.state != AOS_UPD_CHECKING && st.state != AOS_UPD_DOWNLOADING) return st.state;
        usleep(50000);
    }
    return st.state;
}

int main(int argc, char **argv)
{
    printf("1. versions\n");
    struct { const char *o, *r; bool want; } V[] = {
        { "v0.18.0", "v0.17.0", true },
        { "v0.18.0", "v0.17.0-12-gabcdef0", true },
        { "v0.18.0", "v0.17.0-12-gabcdef0-dirty", true },
        { "v0.17.0", "v0.17.0", false },
        { "v0.17.0", "v0.17.0-dirty", false },
        { "v0.17.0", "v0.17.0-3-g1234567", false },
        { "v0.17.0", "v0.17.0-rc.1", true },
        { "v0.17.0-rc.2", "v0.17.0-rc.1", false },
        { "v0.17.0", "v0.18.0", false },
        { "v0.17.1", "v0.17.0-40-gffffff0", true },
        { "v1.0.0", "v0.99.99", true },
        { "garbage", "v0.17.0", false },
    };
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        char what[96];
        snprintf(what, sizeof what, "%s newer than %s: %s", V[i].o, V[i].r, V[i].want ? "yes" : "no");
        ok(what, aos_update_newer(V[i].o, V[i].r) == V[i].want, NULL);
    }

    printf("2. GitHub's redirects\n");
    {
        size_t n = 0;
        aos_fetch_t f = { .url = "https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/latest/download/flash_args.txt",
                          .sink = sink_count, .ctx = &n };
        uint64_t t0 = aos_hal_uptime_ms();
        int rc = aos_fetch_get(&f);
        char d[160];
        snprintf(d, sizeof d, "rc %d, %zu B of %u, %d redirects, from %s, %u ms", rc, n,
                 (unsigned)f.total, f.redirects, f.host, (unsigned)(aos_hal_uptime_ms() - t0));
        ok("latest/download/flash_args.txt", rc == 200 && n == f.total && n > 0 && f.redirects == 2, d);

        char *body = NULL;
        rc = aos_fetch_text("https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/latest/download/no-such-file.json",
                            4096, &body, NULL, NULL);
        snprintf(d, sizeof d, "rc %d (%s)", rc, aos_fetch_reason(rc));
        ok("a file the release does not have: 404", rc == 404 && !body, d);
        free(body);
    }

    if (argc < 2) {
        printf("(3. skipped: pass the test server's address)\n");
    } else {
        const char *base = argv[1];
        char url[256], d[200];
        aos_update_status_t st;

        printf("3. check and install against %s\n", base);
        snprintf(url, sizeof url, "%s/good/amoledos-update.json", base);
        ok("set the source", aos_update_set_source(url), NULL);
        ok("check starts", aos_update_check(), NULL);
        aos_upd_state_t s = wait_job(20);
        aos_update_status(&st);
        snprintf(d, sizeof d, "state %d, offers %s (%u B), newer %d, why: %s", s, st.ver,
                 (unsigned)st.size, st.newer, st.why);
        ok("check ends with an offer", s == AOS_UPD_IDLE && st.checked && st.newer, d);
        char notes[256];
        aos_update_notes("de", notes, sizeof notes);
        ok("notes in German", strstr(notes, "Deutsch") != NULL, notes);
        aos_update_notes("fr", notes, sizeof notes);
        ok("notes in French fall back to English", strstr(notes, "English") != NULL, notes);
        ok("custom source said", st.custom_source && strstr(st.source_host, "127.0.0.1"), st.source_host);

        ok("install starts", aos_update_install(), NULL);
        uint64_t t0 = aos_hal_uptime_ms();
        s = wait_job(60);
        aos_update_status(&st);
        /* the file served, to compare */
        snprintf(url, sizeof url, "%s/good/amoledos.bin", base);
        char *img = NULL;
        uint32_t ilen = 0;
        aos_fetch_text(url, 8u << 20, &img, &ilen, NULL);
        snprintf(d, sizeof d, "state %d, %zu B written, booted %d, %u ms, why: %s", s, g_flash_len,
                 g_booted, (unsigned)(aos_hal_uptime_ms() - t0), st.why);
        ok("install ends READY, the bytes are the file's", s == AOS_UPD_READY && g_booted &&
           img && ilen == g_flash_len && memcmp(img, g_flash, ilen) == 0, d);
        free(img);
        ok("a check while READY is refused", !aos_update_check(), NULL);

        /* a damaged image: the hash in the manifest is not the file's. A new
         * process state is needed to leave READY: go through the source. */
        printf("   damaged image\n");
        snprintf(url, sizeof url, "%s/bad/amoledos-update.json", base);
        aos_update_set_source(url);
        extern void aos_update_test_reset(void);
        aos_update_test_reset();
        aos_update_check();
        wait_job(20);
        aos_update_install();
        s = wait_job(60);
        aos_update_status(&st);
        snprintf(d, sizeof d, "state %d, err %d, booted %d, aborted %d, why: %s", s, st.err,
                 g_booted, g_aborted, st.why);
        ok("refused as damaged, never set to boot", s == AOS_UPD_FAILED &&
           st.err == AOS_UPD_E_DAMAGED && !g_booted && g_aborted, d);

        printf("   up to date\n");
        g_version = "v99.0.0";
        aos_update_test_reset();
        snprintf(url, sizeof url, "%s/good/amoledos-update.json", base);
        aos_update_set_source(url);
        aos_update_check();
        wait_job(20);
        aos_update_status(&st);
        ok("an older offer is not newer", !st.newer && !aos_update_install(), st.why);
        aos_update_set_source(NULL);
        ok("back to the official source", strstr(aos_update_source(), "github.com") != NULL, aos_update_source());
    }

    printf("\n%d of %d passed\n", tests - fails, tests);
    return fails ? 1 : 0;
}
