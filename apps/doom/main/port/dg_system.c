/*
 * AmoledOS - Doom: the engine's life cycle on the watch.
 *
 * The engine runs in the app's worker (core 0). It is started by
 * doomgeneric_Create() and advanced by doomgeneric_Tick(), forever: Chocolate
 * Doom has no way out but exit(). Here exit() is a longjmp back to the top of
 * the worker (dg_compat.h reroutes it), and so are I_Error and a stop asked
 * by the app. From there every block the engine allocated and every file it
 * opened is given back, since the .so is unloaded but the heap is not.
 *
 * This file does NOT include the engine's headers on purpose: the compat
 * macros would turn its own malloc into dg_malloc. What it needs from the
 * engine is declared by hand below.
 */
#include "../doom_port.h"
#include "dg_port.h"
#include "dg_raw.h"
#include "aos_hal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef AOS_SIM
#include "esp_heap_caps.h"
#endif

/* from the engine */
void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick(void);
void M_SaveDefaults(void);
extern unsigned int main_loop_started;      /* boolean, an unsigned int */

/* Measured on the board with /api/mem: 3.9 KB at the title and in the
 * demos. Four times that, for the deeper corners (saves, the finale). */
#define WORKER_STACK    (16 * 1024)

/* --------------------------------------------------------------------------
 * Memory: every block on a list, all freed when the engine leaves
 * -------------------------------------------------------------------------- */

/* Two pointers: 8 bytes on the board and 16 on the Mac, so the payload
 * keeps the alignment malloc gave the block on both. */
typedef struct blk {
    struct blk *prev, *next;
} blk_t;

static blk_t    s_blocks;           /* circular list head */
static size_t   s_live_blocks;

static void *raw_alloc(size_t n)
{
#ifdef AOS_SIM
    return malloc(n);
#else
    /* PSRAM always: the engine allocates in crumbs (lump names, strings)
     * and those would go to internal RAM under ALWAYSINTERNAL */
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

static void *raw_realloc(void *p, size_t n)
{
#ifdef AOS_SIM
    return realloc(p, n);
#else
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

static void raw_free(void *p)
{
#ifdef AOS_SIM
    free(p);
#else
    heap_caps_free(p);
#endif
}

void *dg_raw_alloc(size_t n, bool internal)
{
#ifdef AOS_SIM
    (void)internal;
    return malloc(n);
#else
    if (internal) {
        void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return raw_alloc(n);
#endif
}

void dg_raw_free(void *p)
{
    if (p) raw_free(p);
}

static void list_init(void)
{
    s_blocks.prev = s_blocks.next = &s_blocks;
    s_live_blocks = 0;
}

static void link_blk(blk_t *b)
{
    b->next = s_blocks.next;
    b->prev = &s_blocks;
    s_blocks.next->prev = b;
    s_blocks.next = b;
    s_live_blocks++;
}

static void unlink_blk(blk_t *b)
{
    b->prev->next = b->next;
    b->next->prev = b->prev;
    s_live_blocks--;
}

void *dg_malloc(size_t n)
{
    blk_t *b = raw_alloc(sizeof(blk_t) + n);
    if (!b) return NULL;
    link_blk(b);
    return b + 1;
}

void *dg_calloc(size_t n, size_t size)
{
    size_t t = n * size;
    void *p = dg_malloc(t);
    if (p) memset(p, 0, t);
    return p;
}

void *dg_realloc(void *p, size_t n)
{
    if (!p) return dg_malloc(n);
    blk_t *b = (blk_t *)p - 1;
    unlink_blk(b);
    blk_t *nb = raw_realloc(b, sizeof(blk_t) + n);
    if (!nb) {
        link_blk(b);
        return NULL;
    }
    link_blk(nb);
    return nb + 1;
}

void dg_free(void *p)
{
    if (!p) return;
    blk_t *b = (blk_t *)p - 1;
    unlink_blk(b);
    raw_free(b);
}

char *dg_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = dg_malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void free_everything(void)
{
    size_t n = 0;
    while (s_blocks.next != &s_blocks) {
        blk_t *b = s_blocks.next;
        unlink_blk(b);
        raw_free(b);
        n++;
    }
    if (n) aos_hal_log("doom", "gave back %u blocks", (unsigned)n);
}

/* The zone: Doom's own allocator lives inside one big block. Shareware
 * Doom played in 4 MB of RAM with DOS and the code inside; the zone here is
 * whatever the PSRAM gives, leaving a margin for the watch, from 6 MB down
 * in steps of 256 KB (heap_caps_get_largest_free_block is not lent to the
 * apps, and a failed malloc is cheap). */
#define ZONE_MAX        (6 * 1024 * 1024)
#define ZONE_MIN        (2 * 1024 * 1024)
#define ZONE_MARGIN     (768 * 1024)
#define ZONE_STEP       (256 * 1024)

unsigned char *dg_zone_alloc(int *size)
{
    uint32_t fi = 0, fp = 0;
    aos_hal_heap_info(&fi, &fp);
    int want = ZONE_MAX;
#ifndef AOS_SIM
    if ((int)fp - ZONE_MARGIN < want) want = (int)fp - ZONE_MARGIN;
#endif
    want &= ~(ZONE_STEP - 1);
    for (; want >= ZONE_MIN; want -= ZONE_STEP) {
        unsigned char *z = dg_malloc((size_t)want);
        if (z) {
            *size = want;
            aos_hal_log("doom", "zone %d KB (psram free was %u KB)", want / 1024,
                        (unsigned)(fp / 1024));
            return z;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Files
 * -------------------------------------------------------------------------- */

#define MAX_FILES 8
static FILE *s_files[MAX_FILES];

FILE *dg_fopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (!f) return NULL;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!s_files[i]) {
            s_files[i] = f;
            return f;
        }
    }
    return f;       /* untracked: only leaks if the engine is cut mid-use */
}

int dg_fclose(FILE *f)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (s_files[i] == f) s_files[i] = NULL;
    }
    return fclose(f);
}

static void close_everything(void)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (s_files[i]) {
            fclose(s_files[i]);
            s_files[i] = NULL;
        }
    }
}

/* --------------------------------------------------------------------------
 * stdout: to the watch's log, a line at a time
 * -------------------------------------------------------------------------- */

static char s_line[160];
static int  s_line_n;

static void out_char(char c)
{
    if (c == '\n' || s_line_n >= (int)sizeof s_line - 1) {
        s_line[s_line_n] = 0;
        /* the banner's rows of '=' and blank lines say nothing on a watch */
        bool empty = true;
        for (int i = 0; i < s_line_n; i++) {
            if (s_line[i] != ' ' && s_line[i] != '=') { empty = false; break; }
        }
        if (!empty) aos_hal_log("doom", "%s", s_line);
        s_line_n = 0;
        if (c == '\n') return;
    }
    s_line[s_line_n++] = c;
}

int dg_vprintf(const char *fmt, va_list ap)
{
    char buf[640];      /* the startup banner is one printf of five lines */
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    for (const char *p = buf; *p; p++) out_char(*p);
    return n;
}

int dg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = dg_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int dg_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    if (f == stdout || f == stderr) return dg_vprintf(fmt, ap);
    char buf[512];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n > 0) fwrite(buf, 1, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1), f);
    return n;
}

int dg_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = dg_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int dg_puts(const char *s)
{
    while (*s) out_char(*s++);
    out_char('\n');
    return 1;
}

int dg_putchar(int c)
{
    out_char((char)c);
    return c;
}

int dg_fflush(FILE *f)
{
    if (f == stdout || f == stderr) return 0;
    return fflush(f);
}

/* --------------------------------------------------------------------------
 * The small libc pieces the firmware does not lend
 * -------------------------------------------------------------------------- */

int dg_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int dg_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int dg_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int dg_isdigit(int c) { return c >= '0' && c <= '9'; }
int dg_isalnum(int c) { return dg_isdigit(c) || (dg_tolower(c) >= 'a' && dg_tolower(c) <= 'z'); }
int dg_isprint(int c) { return c >= 0x20 && c < 0x7F; }

int dg_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int d = dg_tolower((unsigned char)*a) - dg_tolower((unsigned char)*b);
        if (d || !*a) return d;
    }
    return 0;
}

int dg_strcasecmp(const char *a, const char *b)
{
    return dg_strncasecmp(a, b, (size_t)-1);
}

double dg_atof(const char *s)
{
    return strtod(s, NULL);
}

#ifndef AOS_SIM
/* gcc turns sha1.c's shifts into a byte swap, and Xtensa has no instruction
 * for it: the call goes to libgcc, which the .so does not link and the
 * firmware does not lend.
 *
 * Careful: written with the same shifts, gcc recognises the idiom HERE too
 * and compiles this body into a call to __bswapsi2, i.e. to itself. That
 * recursion ate the worker's stack and then the kernel's lists next to it
 * (three different panics on the board, all on the other core). At -O0 the
 * bswap pass does not run; the disassembly has no call. */
__attribute__((optimize("O0")))
unsigned int __bswapsi2(unsigned int x)
{
    volatile unsigned char b[4];
    b[0] = (unsigned char)x;
    b[1] = (unsigned char)(x >> 8);
    b[2] = (unsigned char)(x >> 16);
    b[3] = (unsigned char)(x >> 24);
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8) | (unsigned int)b[3];
}
#endif

/* --------------------------------------------------------------------------
 * Leaving: exit(), I_Error and the app's stop all land here
 * -------------------------------------------------------------------------- */

static jmp_buf              s_jmp;
static volatile bool        s_armed;
static volatile dp_state_t  s_state = DP_IDLE;
static char                 s_error[200];
static char                 s_wad[128];
static volatile bool        s_stopping;

enum { JMP_QUIT = 1, JMP_ERROR, JMP_STOP };

void dg_exit(int code)
{
    if (s_armed) {
        s_armed = false;
        longjmp(s_jmp, s_stopping ? JMP_STOP : code == 0 ? JMP_QUIT : JMP_ERROR);
    }
    /* outside the engine's run nothing calls exit(); if it ever happens,
     * better a visible abort than returning into code that assumed it
     * would not come back */
    abort();
}

void dg_fatal(const char *msg)
{
    snprintf(s_error, sizeof s_error, "%s", msg);
    aos_hal_log("doom", "I_Error: %s", msg);
    dg_exit(1);
}

void dg_poll_stop(void)
{
    if (!aos_hal_worker_should_stop() || !s_armed) return;
    s_stopping = true;
    /* the settings (volume, screen size...) are saved on the way out, as
     * Doom does when quitting from its menu */
    if (main_loop_started) M_SaveDefaults();
    dg_exit(0);
}

static void worker(void *arg)
{
    (void)arg;
    static char *argv[] = { "doom", "-iwad", s_wad, NULL };

    int r = setjmp(s_jmp);
    if (r == 0) {
        s_armed = true;
        s_state = DP_LOADING;
        doomgeneric_Create(3, argv);
        s_state = DP_RUNNING;
        for (;;) doomgeneric_Tick();
    }
    s_armed = false;
    dg_sound_shutdown();
    close_everything();
    free_everything();
    s_state = r == JMP_QUIT ? DP_QUIT : r == JMP_STOP ? DP_STOPPED : DP_ERROR;
    if (s_state == DP_ERROR && !s_error[0]) snprintf(s_error, sizeof s_error, "Doom exited");
    aos_hal_log("doom", "engine out (%s)", s_state == DP_QUIT ? "quit" :
                s_state == DP_STOPPED ? "stopped" : s_error);
}

/* --------------------------------------------------------------------------
 * The app's side
 * -------------------------------------------------------------------------- */

static char s_dir[96];

const char *dp_data_dir(void)
{
    if (!s_dir[0]) {
        const char *root = aos_hal_path_sd_root();
        snprintf(s_dir, sizeof s_dir, "%s/doom/", root ? root : ".");
    }
    return s_dir;
}

const char *dg_data_dir(void)
{
    return dp_data_dir();
}

bool dp_find_wad(char *out, int len)
{
    /* the full game before the demo, like Doom's own search */
    static const char *names[] = {
        "doom.wad", "DOOM.WAD", "doom2.wad", "DOOM2.WAD", "doomu.wad",
        "plutonia.wad", "tnt.wad", "doom1.wad", "DOOM1.WAD",
        "freedoom1.wad", "freedoom2.wad",
    };
    struct stat st;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(out, (size_t)len, "%s%s", dp_data_dir(), names[i]);
        if (stat(out, &st) == 0 && st.st_size > 0) return true;
    }
    return false;
}

bool dp_start(const char *wad_path, bool swap_bytes)
{
    if (s_state == DP_LOADING || s_state == DP_RUNNING) return false;
    list_init();
    memset(s_files, 0, sizeof s_files);
    s_error[0] = 0;
    s_stopping = false;
    s_line_n = 0;
    snprintf(s_wad, sizeof s_wad, "%s", wad_path);
    /* the folder for the config and the saves */
    char dir[96];
    snprintf(dir, sizeof dir, "%s", dp_data_dir());
    size_t n = strlen(dir);
    if (n && dir[n - 1] == '/') dir[n - 1] = 0;
    mkdir(dir, 0755);

    if (!dg_video_alloc(swap_bytes)) return false;
    dg_input_reset();
    s_state = DP_LOADING;
    /* core 0: LVGL and the panel's SPI are pinned to core 1 and push the
     * frames; the engine must not keep them off the CPU (aos_hal.h) */
    if (!aos_hal_worker_start_on("doom", worker, NULL, WORKER_STACK, 0, 5)) {
        dg_video_release();
        s_state = DP_ERROR;
        snprintf(s_error, sizeof s_error, "No memory for the engine's task");
        return false;
    }
    return true;
}

void dp_stop(void)
{
    aos_hal_worker_stop();
    dg_video_release();
}

dp_state_t dp_state(void)
{
    return s_state;
}

const char *dp_error(void)
{
    return s_error;
}

/* --------------------------------------------------------------------------
 * doomgeneric's platform hooks
 * -------------------------------------------------------------------------- */

void DG_Init(void)
{
}

void DG_DrawFrame(void)
{
}

void DG_SleepMs(uint32_t ms)
{
    dg_poll_stop();
    aos_hal_worker_sleep(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
