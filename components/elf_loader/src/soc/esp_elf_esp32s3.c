/*
 * AmoledOS, RAM audit prototype (2026-09-12): the .text of a loaded object
 * runs from PSRAM on the ESP32-S3.
 *
 * How: the loader allocates the .text in PSRAM (aos_dynapp's wrapper hands it
 * a 64 KB-aligned block), and this file asks esp_mmu_map() to give that
 * physical PSRAM range an executable alias on the instruction bus, page by
 * page (64 KB), the way ELF_LOADER_SET_MMU does on the ESP32-S2 by hand.
 * elf->text_off is the distance from the data-bus address of the code to its
 * alias; elf_remap_text() adds it to every address that lands in .text
 * (relocations, the entry point, dlsym). Data, rodata and bss stay where
 * they were, reached through the data bus.
 *
 * esp_mmu_map() does the delicate part itself: it freezes the caches and
 * interrupts while writing the shared MMU table, enables the cache bus of
 * the range on both cores and drops stale lines. Two earlier versions of
 * this file did that by hand and taught two lessons: the code that runs with
 * the caches stopped must itself be in IRAM, and
 * esp_mmu_map_reserve_block_with_caps() must not be used once the mapper's
 * block list exists (it moves free_head and every later esp_partition_mmap
 * fails its unmap check).
 *
 * Cache: the code is written through the data cache (write-back on this
 * chip), so after the relocations it is written back to PSRAM and the alias
 * is dropped from the instruction cache (esp_elf_arch_flush_text).
 */
#include <sys/errno.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_mmu_map.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "hal/cache_hal.h"
#include "private/elf_platform.h"

#define PAGE_SZ   CONFIG_MMU_PAGE_SIZE

static const char *TAG = "elf_s3mmu";

int esp_elf_arch_init_mmu(esp_elf_t *elf)
{
    uintptr_t text   = (uintptr_t)elf->ptext;
    size_t    size   = elf->sec[ELF_SEC_TEXT].size;
    uintptr_t first  = text & ~(uintptr_t)(PAGE_SZ - 1);
    uintptr_t last   = (text + size - 1) & ~(uintptr_t)(PAGE_SZ - 1);
    uint32_t  npages = (uint32_t)((last - first) / PAGE_SZ) + 1;

    esp_paddr_t  paddr  = 0;
    mmu_target_t target = 0;
    if (esp_mmu_vaddr_to_paddr((void *)first, &paddr, &target) != ESP_OK ||
        target != MMU_TARGET_PSRAM0) {
        ESP_LOGE(TAG, ".text at %p is not in PSRAM", elf->ptext);
        return -EIO;
    }

    void *alias = NULL;
    esp_err_t e = esp_mmu_map(paddr, npages * PAGE_SZ, MMU_TARGET_PSRAM0,
                              MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ | MMU_MEM_CAP_32BIT,
                              0, &alias);
    if (e != ESP_OK || !alias) {
        ESP_LOGE(TAG, "no executable alias for PSRAM 0x%06x (%s)", (unsigned)paddr, esp_err_to_name(e));
        return -EIO;
    }

    elf->mmu_off  = (uint32_t)(uintptr_t)alias;      /* the alias, for unmap */
    elf->mmu_num  = npages;
    elf->text_off = (uint32_t)((uintptr_t)alias - first);

    ESP_LOGI(TAG, ".text %u B at %p (PSRAM 0x%06x) runs at 0x%08x, %u page(s)",
             (unsigned)size, elf->ptext, (unsigned)paddr,
             (unsigned)(text + elf->text_off), (unsigned)npages);
    return 0;
}

void esp_elf_arch_deinit_mmu(esp_elf_t *elf)
{
    if (!elf->mmu_num) {
        return;
    }
    esp_err_t e = esp_mmu_unmap((void *)(uintptr_t)elf->mmu_off);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "unmap 0x%08x: %s", (unsigned)elf->mmu_off, esp_err_to_name(e));
    }
    elf->mmu_num = 0;
    elf->mmu_off = 0;
}

void esp_elf_arch_flush_text(esp_elf_t *elf)
{
    if (!elf->mmu_num) {
        return;
    }
    uintptr_t text = (uintptr_t)elf->ptext;
    size_t    size = elf->sec[ELF_SEC_TEXT].size;

    /* The data cache line is 64 B on this chip; msync wants aligned ranges. */
    uintptr_t a = text & ~(uintptr_t)63u;
    size_t    l = ((text + size + 63u) & ~(uintptr_t)63u) - a;
    esp_cache_msync((void *)a, l, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    esp_cache_sync_ops_enter_critical_section();
    cache_hal_invalidate_addr(elf->mmu_off, (uint32_t)elf->mmu_num * PAGE_SZ);
    esp_cache_sync_ops_exit_critical_section();
}
