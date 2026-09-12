/* GET /api/mem: the whole picture of the RAM at this instant, as text.
 *
 * Written for the RAM audit (branch ram-audit). It answers what the heartbeat
 * cannot: how each heap region is used and fragmented, how big every task's
 * stack is and how much of it was ever used, which call sites own the internal
 * blocks (standalone heap tracing, started in app_main), and what the biggest
 * blocks are. Needs CONFIG_HEAP_TRACING_STANDALONE and
 * CONFIG_FREERTOS_USE_TRACE_FACILITY: audit settings, not release settings.
 *
 * Regions, by address (heap/port/esp32s3/memory_layout.c):
 *   IRAM   0x40374000..0x40378000  SRAM0 leftover, instruction bus only
 *   DIRAM  0x3FC88000..0x3FCF0000  SRAM1, data AND instruction bus (MALLOC_CAP_EXEC)
 *   DRAM   0x3FCF0000..0x3FCF8000  SRAM2 half not used by the data cache, data only
 *   RTC    0x600FE000..0x60100000  RTC fast memory as heap
 *   PSRAM  0x3C000000..            external
 * The DMA reserve (SPIRAM_MALLOC_RESERVE_INTERNAL) is carved out of DIRAM at
 * boot and re-registered as a heap WITHOUT the EXEC cap: it shows up here as a
 * separate DIRAM heap, and it is why "exec" is 64 KB below "internal".
 */
#include "sdkconfig.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "spi_flash_chip_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "aos_dynapp.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"

/* ?fps=N: frames LVGL actually rendered per second, counted over N seconds.
 * LV_EVENT_RENDER_READY fires once per refresh that had something to draw,
 * which for a game redrawing its canvas every frame is one per frame. */
static volatile uint32_t s_renders;
static bool              s_fps_hooked;
static void render_cb(lv_event_t *e) { (void)e; s_renders++; }

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#endif

typedef struct {
    httpd_req_t *req;
    char         buf[1024];
    int          len;
    esp_err_t    err;
} out_t;

static void out_flush(out_t *o)
{
    if (o->len > 0 && o->err == ESP_OK) {
        o->err = httpd_resp_send_chunk(o->req, o->buf, o->len);
    }
    o->len = 0;
}

static void outf(out_t *o, const char *fmt, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    if (o->len + n > (int)sizeof(o->buf)) out_flush(o);
    memcpy(o->buf + o->len, line, n);
    o->len += n;
}

static const char *region_of(uintptr_t a)
{
    if (a >= 0x40370000u && a < 0x40380000u) return "IRAM";
    if (a >= 0x3FC88000u && a < 0x3FCF0000u) return "DIRAM";
    if (a >= 0x3FCF0000u && a < 0x3FD00000u) return "DRAM";
    if (a >= 0x600FE000u && a < 0x60100000u) return "RTC";
    if (a >= 0x3C000000u && a < 0x3E000000u) return "PSRAM";
    return "?";
}

static bool is_internal(uintptr_t a)
{
    return (a >= 0x3FC88000u && a < 0x3FD00000u) || (a >= 0x40370000u && a < 0x40380000u) ||
           (a >= 0x600FE000u && a < 0x60100000u);
}

/* ---- per-heap walk: fragmentation, totals, and every used block ---------- */
typedef struct {
    intptr_t start, end;
    size_t   used, freeb, largest;
    unsigned nused, nfree;
} heap_row_t;

typedef struct { void *ptr; uint32_t size; } blk_t;

typedef struct {
    heap_row_t rows[16];
    int        n;
    blk_t     *blk;         /* used blocks, all heaps */
    size_t     nblk, maxblk;
} walk_t;

static bool walker(walker_heap_into_t h, walker_block_info_t b, void *ud)
{
    walk_t *w = ud;
    heap_row_t *r = NULL;
    for (int i = 0; i < w->n; i++) {
        if (w->rows[i].start == h.start) { r = &w->rows[i]; break; }
    }
    if (!r) {
        if (w->n >= 16) return true;
        r = &w->rows[w->n++];
        memset(r, 0, sizeof(*r));
        r->start = h.start;
        r->end   = h.end;
    }
    if (b.used) {
        r->used += b.size; r->nused++;
        if (w->blk && w->nblk < w->maxblk) { w->blk[w->nblk].ptr = b.ptr; w->blk[w->nblk].size = b.size; w->nblk++; }
    } else {
        r->freeb += b.size; r->nfree++;
        if (b.size > r->largest) r->largest = b.size;
    }
    return true;
}

static void caps_line(out_t *o, const char *name, uint32_t caps)
{
    multi_heap_info_t i;
    heap_caps_get_info(&i, caps);
    outf(o, "%-9s free %7u  alloc %7u  largest %7u  minfree %7u  freeblk %4u  usedblk %5u\n",
         name, (unsigned)i.total_free_bytes, (unsigned)i.total_allocated_bytes,
         (unsigned)i.largest_free_block, (unsigned)i.minimum_free_bytes,
         (unsigned)i.free_blocks, (unsigned)i.allocated_blocks);
}

static int cmp_blk_desc(const void *a, const void *b)
{
    const blk_t *x = a, *y = b;
    return (x->size < y->size) - (x->size > y->size);
}

#if CONFIG_HEAP_TRACING_STANDALONE
/* call-site aggregation of the live traced blocks */
typedef struct {
    void    *pc[CONFIG_HEAP_TRACING_STACK_DEPTH];
    uint32_t bytes_int, bytes_ext;
    uint32_t n_int, n_ext;
    uint32_t big;             /* largest single block */
} site_t;

static int cmp_site_desc(const void *a, const void *b)
{
    const site_t *x = a, *y = b;
    return (x->bytes_int < y->bytes_int) - (x->bytes_int > y->bytes_int);
}

typedef struct { void *addr; uint32_t size; void *pc[3]; } tb_t;
static int cmp_tb_desc(const void *a, const void *b)
{
    const tb_t *x = a, *y = b;
    return (x->size < y->size) - (x->size > y->size);
}
#endif

esp_err_t aos_mem_handler(httpd_req_t *req)
{
    out_t o = { .req = req, .len = 0, .err = ESP_OK };
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");

    /* ?tap=x,y[,ms]: inject a tap; ?fps=N: rendered frames per second. */
    {
        char q[64] = "", v[24];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            if (httpd_query_key_value(q, "tap", v, sizeof(v)) == ESP_OK) {
                int x = 0, y = 0, ms = 80;
                if (sscanf(v, "%d,%d,%d", &x, &y, &ms) >= 2) {
                    aos_ui_inject_tap(x, y, ms);
                    outf(&o, "tap injected at %d,%d for %d ms\n", x, y, ms);
                }
            }
            if (httpd_query_key_value(q, "fps", v, sizeof(v)) == ESP_OK) {
                int secs = atoi(v);
                if (secs < 1) secs = 3;
                if (secs > 20) secs = 20;
                if (!s_fps_hooked && aos_hal_lock(2000)) {
                    lv_display_add_event_cb(lv_display_get_default(), render_cb, LV_EVENT_RENDER_READY, NULL);
                    s_fps_hooked = true;
                    aos_hal_unlock();
                }
                uint32_t a = s_renders;
                vTaskDelay(pdMS_TO_TICKS(secs * 1000));
                uint32_t b = s_renders;
                outf(&o, "== fps ==\nrendered frames %u in %d s = %.1f fps\n\n",
                     (unsigned)(b - a), secs, (double)(b - a) / (double)secs);
            }
        }
    }

    /* ?lvpsram=0|1: flip the LVGL-to-PSRAM policy for new allocations. */
    {
        char q[64] = "", v[8];
        extern void aos_lvmem_set_psram(bool on);
        extern bool aos_lvmem_get_psram(void);
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "lvpsram", v, sizeof(v)) == ESP_OK) {
            aos_lvmem_set_psram(v[0] == '1');
        }
        outf(&o, "lvgl allocations to psram: %s\n", aos_lvmem_get_psram() ? "on" : "off");
    }

    /* ?bench=1: full render of the CURRENT screen, 8 times, average. Unlike
     * the startup benchmark (one rectangle) this walks the real object tree
     * and styles, which is what moving LVGL's memory changes. */
    {
        char q[64] = "", v[8];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "bench", v, sizeof(v)) == ESP_OK) {
            int64_t us = -1;
            uint32_t objs = 0;
            if (aos_hal_lock(2000)) {
                lv_display_t *d = lv_display_get_default();
                lv_obj_t *scr = lv_screen_active();
                objs = lv_obj_get_child_count_by_type(scr, NULL);
                const int N = 8;
                int64_t t0 = esp_timer_get_time();
                for (int i = 0; i < N; i++) {
                    lv_obj_invalidate(scr);
                    lv_refr_now(d);
                }
                us = (esp_timer_get_time() - t0) / N;
                aos_hal_unlock();
            }
            outf(&o, "== bench ==\nscreen render %lld us avg of 8 (%u top-level objects)\n\n",
                 (long long)us, (unsigned)objs);
        }
    }

    outf(&o, "== caps (heap_caps_get_info) ==\n");
    caps_line(&o, "internal", MALLOC_CAP_INTERNAL);
    caps_line(&o, "exec",     MALLOC_CAP_EXEC);
    caps_line(&o, "dma",      MALLOC_CAP_DMA);
    caps_line(&o, "rtcram",   MALLOC_CAP_RTCRAM);
    caps_line(&o, "psram",    MALLOC_CAP_SPIRAM);

    /* ---- heaps ---- */
    enum { MAXBLK = 9000 };
    walk_t w = { .n = 0, .nblk = 0, .maxblk = MAXBLK };
    w.blk = heap_caps_calloc(MAXBLK, sizeof(blk_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    heap_caps_walk_all(walker, &w);
    outf(&o, "\n== heaps (heap_caps_walk_all) ==\n");
    outf(&o, "%-6s %-10s %-10s %7s %7s %7s %7s %5s %5s\n",
         "region", "start", "end", "size", "used", "free", "largest", "nused", "nfree");
    size_t tot_int_size = 0, tot_int_used = 0, tot_int_free = 0;
    for (int i = 0; i < w.n; i++) {
        heap_row_t *r = &w.rows[i];
        size_t size = (size_t)(r->end - r->start);
        outf(&o, "%-6s 0x%08x 0x%08x %7u %7u %7u %7u %5u %5u\n",
             region_of((uintptr_t)r->start), (unsigned)r->start, (unsigned)r->end,
             (unsigned)size, (unsigned)r->used, (unsigned)r->freeb, (unsigned)r->largest,
             r->nused, r->nfree);
        if (is_internal((uintptr_t)r->start)) {
            tot_int_size += size; tot_int_used += r->used; tot_int_free += r->freeb;
        }
    }
    outf(&o, "internal heaps: size %u used %u free %u (headers+overhead %u), used blocks seen %u\n",
         (unsigned)tot_int_size, (unsigned)tot_int_used, (unsigned)tot_int_free,
         (unsigned)(tot_int_size - tot_int_used - tot_int_free), (unsigned)w.nblk);

    /* size classes of the used blocks, internal vs psram */
    {
        static const size_t cls[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096, 16384, (size_t)-1 };
        enum { NCLS = sizeof(cls) / sizeof(cls[0]) };
        size_t ib[NCLS] = {0}, ic[NCLS] = {0}, pb[NCLS] = {0}, pc[NCLS] = {0};
        for (size_t k = 0; k < w.nblk; k++) {
            int c = 0;
            while (w.blk[k].size > cls[c]) c++;
            if (is_internal((uintptr_t)w.blk[k].ptr)) { ib[c] += w.blk[k].size; ic[c]++; }
            else                                        { pb[c] += w.blk[k].size; pc[c]++; }
        }
        outf(&o, "\n== used blocks by size ==\n%-8s %16s %16s\n", "<= size", "internal (n)", "psram (n)");
        for (int c = 0; c < NCLS; c++) {
            char lab[12];
            if (cls[c] == (size_t)-1) snprintf(lab, sizeof(lab), ">16384");
            else snprintf(lab, sizeof(lab), "%u", (unsigned)cls[c]);
            outf(&o, "%-8s %9u (%5u) %9u (%5u)\n", lab, (unsigned)ib[c], (unsigned)ic[c], (unsigned)pb[c], (unsigned)pc[c]);
        }
    }

    /* ---- tasks ---- */
    UBaseType_t ntask = uxTaskGetNumberOfTasks();
    TaskStatus_t *ts = heap_caps_calloc(ntask + 8, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    UBaseType_t got = 0;
    if (ts) {
        got = uxTaskGetSystemState(ts, ntask + 8, NULL);
    }
    outf(&o, "\n== tasks (%u) ==\n%-18s %3s %4s %7s %7s %6s %-6s\n", (unsigned)got,
         "name", "pri", "core", "stack", "peak", "hwm", "region");
    size_t stacks_internal = 0, stacks_psram = 0;
    for (UBaseType_t i = 0; i < got; i++) {
        uintptr_t base = (uintptr_t)ts[i].pxStackBase;
        size_t stack_size = 0;
        for (size_t k = 0; k < w.nblk; k++) {
            if ((uintptr_t)w.blk[k].ptr == base) { stack_size = w.blk[k].size; break; }
        }
        if (is_internal(base)) stacks_internal += stack_size; else stacks_psram += stack_size;
        outf(&o, "%-18s %3u %4d %7u %7u %6u %-6s\n",
             ts[i].pcTaskName, (unsigned)ts[i].uxCurrentPriority, (int)xTaskGetCoreID(ts[i].xHandle),
             (unsigned)stack_size,
             stack_size ? (unsigned)(stack_size - ts[i].usStackHighWaterMark) : 0u,
             (unsigned)ts[i].usStackHighWaterMark, region_of(base));
    }
    outf(&o, "stacks: internal %u  psram %u\n", (unsigned)stacks_internal, (unsigned)stacks_psram);

    /* ---- biggest internal blocks (all, traced or not) ---- */
    if (w.blk) {
        qsort(w.blk, w.nblk, sizeof(blk_t), cmp_blk_desc);
        outf(&o, "\n== biggest internal blocks (>= 2048 B) ==\n");
        for (size_t k = 0; k < w.nblk; k++) {
            if (!is_internal((uintptr_t)w.blk[k].ptr) || w.blk[k].size < 2048) continue;
            const char *stack_of = "";
            for (UBaseType_t i = 0; i < got; i++) {
                if ((uintptr_t)ts[i].pxStackBase == (uintptr_t)w.blk[k].ptr) { stack_of = ts[i].pcTaskName; break; }
            }
            outf(&o, "%7u  0x%08x  %-5s %s%s\n", (unsigned)w.blk[k].size, (unsigned)(uintptr_t)w.blk[k].ptr,
                 region_of((uintptr_t)w.blk[k].ptr), stack_of[0] ? "stack of " : "", stack_of);
        }
    }

#if CONFIG_HEAP_TRACING_STANDALONE
    /* ---- who allocated what: the traced live blocks, by call site ---- */
    heap_trace_summary_t sm;
    memset(&sm, 0, sizeof(sm));
    heap_trace_summary(&sm);
    size_t count = heap_trace_get_count();
    outf(&o, "\n== heap trace (since app_main) ==\nrecords %u / %u  overflowed %u  allocs %u frees %u  hwm %u\n",
         (unsigned)sm.count, (unsigned)sm.capacity, (unsigned)sm.has_overflowed,
         (unsigned)sm.total_allocations, (unsigned)sm.total_frees, (unsigned)sm.high_water_mark);

    enum { MAXSITE = 2000, MAXTB = 4000 };
    site_t *sites = heap_caps_calloc(MAXSITE, sizeof(site_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tb_t   *tb    = heap_caps_calloc(MAXTB, sizeof(tb_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t nsite = 0, ntb = 0, traced_int = 0, traced_ext = 0, traced_n_int = 0;
    if (sites && tb) {
        heap_trace_record_t rec;
        for (size_t i = 0; i < count; i++) {
            if (heap_trace_get(i, &rec) != ESP_OK || rec.freed || !rec.address) continue;
            bool internal = is_internal((uintptr_t)rec.address);
            if (internal) { traced_int += rec.size; traced_n_int++; } else { traced_ext += rec.size; }
            size_t s;
            for (s = 0; s < nsite; s++) {
                if (memcmp(sites[s].pc, rec.alloced_by, sizeof(sites[s].pc)) == 0) break;
            }
            if (s == nsite) {
                if (nsite >= MAXSITE) continue;
                memcpy(sites[nsite].pc, rec.alloced_by, sizeof(sites[nsite].pc));
                nsite++;
            }
            if (internal) { sites[s].bytes_int += rec.size; sites[s].n_int++; }
            else          { sites[s].bytes_ext += rec.size; sites[s].n_ext++; }
            if (rec.size > sites[s].big) sites[s].big = rec.size;
            if (internal && rec.size >= 1024 && ntb < MAXTB) {
                tb[ntb].addr = rec.address; tb[ntb].size = rec.size;
                memcpy(tb[ntb].pc, rec.alloced_by, sizeof(tb[ntb].pc));
                ntb++;
            }
        }
        outf(&o, "traced live: internal %u B in %u blocks, psram %u B; sites %u\n",
             (unsigned)traced_int, (unsigned)traced_n_int, (unsigned)traced_ext, (unsigned)nsite);
        outf(&o, "untraced internal (allocated before app_main or by ROM/boot): %u B\n",
             (unsigned)(tot_int_used > traced_int ? tot_int_used - traced_int : 0));

        qsort(sites, nsite, sizeof(site_t), cmp_site_desc);
        outf(&o, "\n== internal bytes by call site (top 120) ==\n%8s %5s %7s %8s  %s\n", "int_B", "n", "max", "psram_B", "pc0 pc1 pc2 pc3 pc4 pc5");
        for (size_t s = 0; s < nsite && s < 120; s++) {
            if (sites[s].bytes_int == 0) break;
            outf(&o, "%8u %5u %7u %8u ", (unsigned)sites[s].bytes_int, (unsigned)sites[s].n_int,
                 (unsigned)sites[s].big, (unsigned)sites[s].bytes_ext);
            for (int d = 0; d < CONFIG_HEAP_TRACING_STACK_DEPTH; d++) {
                outf(&o, " %08x", (unsigned)(uintptr_t)sites[s].pc[d]);
            }
            outf(&o, "\n");
        }

        qsort(tb, ntb, sizeof(tb_t), cmp_tb_desc);
        outf(&o, "\n== traced internal blocks >= 1024 B (top 80) ==\n");
        for (size_t k = 0; k < ntb && k < 80; k++) {
            outf(&o, "%7u  0x%08x  %-5s  %08x %08x %08x\n", (unsigned)tb[k].size, (unsigned)(uintptr_t)tb[k].addr,
                 region_of((uintptr_t)tb[k].addr), (unsigned)(uintptr_t)tb[k].pc[0],
                 (unsigned)(uintptr_t)tb[k].pc[1], (unsigned)(uintptr_t)tb[k].pc[2]);
        }
    }
    free(sites);
    free(tb);
#endif
    free(ts);
    free(w.blk);

    /* ---- the apps' code reservation ---- */
    uint32_t libre = 0, mayor = 0;
    int usados = 0;
    aos_dynapp_pool_info(&libre, &mayor, &usados);
    char loaded[160] = "";
    int nl = aos_dynapp_loaded_list(loaded, sizeof(loaded));
    outf(&o, "\n== apps code reservation ==\nfree %u largest %u in-use %d  loaded(%d): %s\n",
         (unsigned)libre, (unsigned)mayor, usados, nl, loaded);

    /* ---- flash chip (for the auto-suspend question) ---- */
    esp_flash_t *chip = esp_flash_default_chip;
    uint32_t fsz = 0;
    esp_flash_get_size(chip, &fsz);
    uint32_t fcaps = (chip && chip->chip_drv && chip->chip_drv->get_chip_caps) ?
                     (uint32_t)chip->chip_drv->get_chip_caps(chip) : 0;
    outf(&o, "\n== flash ==\nid 0x%06x driver %s size %u caps 0x%x (suspend %s)\n",
         (unsigned)(chip ? chip->chip_id : 0),
         (chip && chip->chip_drv && chip->chip_drv->name) ? chip->chip_drv->name : "?",
         (unsigned)fsz, (unsigned)fcaps, (fcaps & SPI_FLASH_CHIP_CAP_SUSPEND) ? "yes" : "no");

    outf(&o, "\n== build ==\nSPIRAM_MALLOC_ALWAYSINTERNAL %d  RESERVE_INTERNAL %d  LVGL buf lines %d\n",
         CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL,
         CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT);

    out_flush(&o);
    if (o.err != ESP_OK) return o.err;
    return httpd_resp_send_chunk(req, NULL, 0);
}
