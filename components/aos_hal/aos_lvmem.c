/* RAM audit (E1): LVGL's allocator core goes to PSRAM.
 *
 * With CONFIG_LV_USE_CLIB_MALLOC, LVGL calls lv_malloc_core() -> malloc(), and
 * malloc() keeps everything under SPIRAM_MALLOC_ALWAYSINTERNAL (1 KB) in
 * internal RAM. Every LVGL object, style, event list and spec_attr is smaller
 * than that, so a busy screen puts 25-48 KB of small blocks in the executable
 * heap (measured: launcher +48 KB, calendar +33 KB, clima +31 KB, settings
 * +25 KB), and their churn on every screen change is what pulverises it.
 *
 * These two wrappers send LVGL's allocations to PSRAM first and fall back to
 * the original core when PSRAM is out (it is not). lv_free_core() needs no
 * wrapper: free() works on any heap. The apps call lv_malloc() through the
 * firmware's symbol table, so they get the same policy without recompiling;
 * their own malloc() is untouched. It changes no LVGL configuration, so
 * lv_global_t keeps its layout and the .so files stay valid. */
#include <stddef.h>
#include <stdbool.h>
#include "esp_heap_caps.h"

void *__real_lv_malloc_core(size_t size);
void *__real_lv_realloc_core(void *p, size_t new_size);

/* Audit switch (/api/mem?lvpsram=0|1): with 0 the wrappers step aside and
 * LVGL allocates as before, so the same firmware can be measured both ways
 * on the same screen. Only affects allocations made after the flip. */
static bool s_lv_psram = true;

void aos_lvmem_set_psram(bool on)  { s_lv_psram = on; }
bool aos_lvmem_get_psram(void)     { return s_lv_psram; }

void *__wrap_lv_malloc_core(size_t size)
{
    if (s_lv_psram) {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) {
            return p;
        }
    }
    return __real_lv_malloc_core(size);
}

void *__wrap_lv_realloc_core(void *p, size_t new_size)
{
    if (s_lv_psram) {
        void *n = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (n || new_size == 0) {
            return n;
        }
    }
    return __real_lv_realloc_core(p, new_size);
}
