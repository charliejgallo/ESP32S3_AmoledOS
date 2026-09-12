# RAM audit (branch `ram-audit`, 2026-09-11)

Where the internal RAM of the watch goes, measured on the board (v2, CO5300 +
CST816) with the v0.3.3 firmware plus an audit build that adds `/api/mem`
(heap regions, task stacks, every live block with the call stack that made it)
and a runtime switch for one experiment. The question behind it: what can be
freed so that the 48 KB reservation for the apps' code can grow later.

All numbers are bytes unless marked K. "Internal" is SRAM; "exec" is the part
of it the instruction bus can reach, which is what the apps' `.text` and the
radios compete for.

## 1. The map of internal RAM

The S3 has 512 KB of SRAM. What the firmware can use, by region
(`heap/port/esp32s3/memory_layout.c`):

| Region | Address | Size | Who can use it |
| --- | --- | --- | --- |
| IRAM (SRAM0 leftover) | 0x40374000 | 16 K | instruction bus only; 100 % static `.iram0.text` + vectors |
| DIRAM (SRAM1) | 0x3FC88000..0x3FCF0000 | 416 K | data and instruction bus: static data/bss, IRAM code mirror, and the exec-capable heap |
| DRAM strip (SRAM2 half) | 0x3FCF0000..0x3FCF8000 | 32 K | data only (the other half is the 32 KB data cache) |
| RTC fast | 0x600FE000 | 8 K | heap, exec-capable, tiny |

Two things about the heap that are not obvious from the boot log:

* **The DMA reserve is carved out of the executable heap.** At boot
  `SPIRAM_MALLOC_RESERVE_INTERNAL` (32 K) is taken from the DIRAM heap and
  re-registered as a separate heap *without* the EXEC cap, so that DMA and
  internal-only requests still succeed when malloc has moved to PSRAM. That is
  why "exec free" is exactly 64 K below "internal free" (32 K reserve + 32 K
  DRAM strip). Measured in every state: the reserve is 100 % free.
* **malloc fills DIRAM first.** The DRAM strip is priority 1 and the reserve
  priority 2, so both sit untouched while DIRAM has room. The "internal free"
  the heartbeat prints is therefore 64-72 K more optimistic than what the
  next allocation can actually get from the exec-capable heap.

## 2. Static use (from the map of the v0.3.3 build)

`idf.py size`: DIRAM 112,519 used (text 70,475, data 23,740, bss 18,304),
IRAM 16,384 (100 %), plus 47,596 of `.bss` already in PSRAM.

By subsystem (data + bss + IRAM text, internal):

| Subsystem | data | bss | IRAM text | Total |
| --- | ---: | ---: | ---: | ---: |
| FreeRTOS, heap, esp_system, hw_support, xtensa, newlib | 6,263 | 2,813 | 38,554 | 47,630 |
| flash driver + hal (spi_flash, hal, bootloader_support, esp_mm, esp_psram) | 7,715 | 146 | 27,037 | 34,898 |
| WiFi (net80211, pp, phy, coexist, esp_wifi, wpa) | 7,658 | 148 | 6,706 | 14,512 |
| AmoledOS (aos_*) | 327 | 9,996 | 0 | 10,323 |
| drivers (spi, i2c, i2s, gpio, sdmmc) | 545 | 65 | 6,320 | 6,930 |
| Bluetooth (btdm_app_flash, bt) | 644 | 742 | 4,625 | 6,011 |
| lwip + mdns + esp_netif | 72 | 2,387 | 0 | 2,459 |
| LVGL + port | 84 | 577 | 98 | 759 |
| mbedtls | 112 | 512 | 80 | 704 |
| other | 158 | 181 | 1,774 | 2,113 |

The largest single objects:

* IRAM text: `spi_master.c` 4,579, `tlsf.c` 4,400, `tasks.c` 3,812,
  `spi_hal_iram.c` 3,030, `spi_flash_hal_iram.c` 3,002, `esp_flash_api.c`
  2,982, `spi_flash_chip_generic.c` 2,930, `rtc_clk.c` 2,698, `phy_reg.o` 2,691.
* DRAM data: `cache_hal.c` **4,979 of assertion strings** (functions that
  run with the cache off keep their strings in DRAM), the two ISR stacks
  3,072, PHY strings 2,013 + 1,325, `xtensa_intr_asm` 1,024.
* DRAM bss: `aos_hal_esp32.c` 3,764 (a 2 KB PCM buffer and a 512 B one),
  `mdns_send.c` 1,460, `aos_ble.c` 1,174, `aos_app_settings.c` 1,136,
  `aos_i18n.c` 796, `lv_global` 536.

## 3. Runtime use, watchface idle, WiFi connected, BLE advertising

Internal heaps: 380,403 total, **278,812 used, 94,028 free** (headers 7.5 K).

| Heap | Size | Used | Free | Largest free |
| --- | ---: | ---: | ---: | ---: |
| DIRAM main | 284,440 | 257,780 | 22,252 | 22,084 |
| DIRAM startup-stack (recycled) | 22,308 | 21,032 | 0 | 0 |
| DIRAM DMA reserve | 32,767 | 0 | 32,020 | 32,020 |
| DRAM strip | 32,768 | 0 | 32,024 | 32,024 |
| RTC | 8,120 | 0 | 7,732 | 7,732 |

So the exec-capable heap that everything competes for has **22 K free in one
block** at idle. `exec free` says 30 K because it adds the RTC heap.

Where the 278.8 K goes (call-stack attribution, `/api/mem`):

| Owner | Bytes | Notes |
| --- | ---: | --- |
| task stacks (17 tasks) | 80,640 | LVGL 20,480 (peak 7,556 after photos), main 8,704 (peak 7,940!), httpd 8,192, wifi 6,656, four of 4,096 (mdns, btController, nimble_host, esp_timer), tiT 3,584, aos_hk/aos_tone 3,072, sys_evt 2,816, Tmr Svc 2,048, idle/ipc 5,632 |
| apps code reservation | 50,176 | the 48 K pool (+1 K TLSF rounding) |
| DMA reserve carve-out | 32,768 | the block that becomes the reserve heap |
| BLE controller heap | 27,904 | `esp_coex_common_malloc_internal_wrapper`, 18 blocks (8 x 1,700 esf_buf, 8 x 1,604 rx, 9,356 lld) |
| WiFi driver heap | 22,504 | `malloc_internal_wrapper`, 26 blocks |
| LVGL display buffer | 14,720 | 20 rows x 368 px, DMA-capable |
| I2S DMA descriptors + buffers | 5,760 | speaker and mic channels, allocated at boot and kept |
| FreeRTOS queues and mutexes | 4,892 | 33 objects |
| LVGL objects and styles | ~6,600 | at the watchface; see below |
| SPI DMA descriptors (LCD) | 1,944 | |
| spiffs, nvs, httpd, netif, misc | ~9,000 | |
| pre-scheduler (IDLE/ipc/timer/main TCBs and stacks, VFS, console) | ~12,000 | |

Switching Bluetooth off returns 34,000 (exec free 30,020 -> 64,020).

## 4. What a screen costs: LVGL objects live in internal RAM

With `CONFIG_LV_USE_CLIB_MALLOC` every LVGL object, style, event list and
`spec_attr` is a `malloc()` under 1 KB, and `SPIRAM_MALLOC_ALWAYSINTERNAL=1024`
keeps those internal. Measured against the idle figure (94,083 internal free,
30,020 exec free):

| Screen | Internal free | Exec free | Largest exec block | LVGL blocks added |
| --- | ---: | ---: | ---: | ---: |
| watchface (idle) | 94,083 | 30,020 | 22,016 | - |
| 2043 (dynamic app, 28.6 K of the pool) | 82,051 | 18,016 | 9,216 | +317 |
| settings | 69,151 | 8,204 | 7,680 | +638 |
| clima | 61,559 | 8,316 | 7,680 | +733 (188 objects, 181 styles) |
| calendar | 58,503 | 8,456 | 7,680 | +1,062 |
| launcher | 36,111 | 8,572 | 7,680 | +1,716 (48 K of internal RAM) |

The launcher alone puts 48 K of small blocks in the executable heap, and the
churn of creating and destroying them on every screen change is what
pulverises that heap into the "18 K in 25 holes" the settings page shows.
With any screen but the watchface open, the largest executable block left
outside the pool is 7.7 K.

Stack peaks worth acting on: `main` runs its boot sequence with 764 B to
spare (8,704 - 7,940); `sys_evt` has 588 B. The LVGL task peaks at 7,556 of
20,480 (the tjpgd path included); `esp_timer`, `mdns` and `aos_tone` use less
than half of theirs.

## 5. The flash chip

`0x204018` (XMC, 16 MB), generic driver, `SPI_FLASH_CHIP_CAP_SUSPEND`
reported. Flash auto-suspend is therefore possible on this hardware, and it is
the only path that would let the `*_IN_IRAM` family and the task stacks leave
internal RAM wholesale; the ESP-IDF documentation warns against it exactly
for systems with LCD DMA, Bluetooth and WiFi interrupts. Not tried.

## 6. Levers tried, and what each one gave

Everything below was built from the same instrumented tree and installed by
OTA with the serial port captured, so a build that does not survive its trial
boot leaves a panic to read. The audit build itself costs +2 K of static DIRAM
(heap tracing) and is not what the numbers below compare against: the
reference is the audit build of v0.3.3 unchanged (`r1`).

### 6.1 The first bundle (E1) did not boot, and the bisection was the lesson

E1 put every lever in one build: DMA reserve 16 K, ROM flash driver, PHY /
heap / SPI-GDMA-I2C ISRs to flash, silent asserts, WiFi 6+6 static buffers,
`-Os` on the IDF components with IRAM code, LVGL allocations to PSRAM, LVGL
task stack 16 K. Static DIRAM went from 114,487 to 79,615 (**-34.9 K**), and
the board died three seconds after boot, the instant the station entered the
`auth` state, every time: a NimBLE global in PSRAM read back as NULL right
after being written, a callback pointer in DRAM overwritten with a heap
address, a TLSF assert inside a `wifi_zalloc` made by the coexistence
scheduler. Memory corruption, not a fault in the changed code.

What the bisection established (one build per line, all on the same tree):

| Build | Levers | Boots |
| --- | --- | --- |
| E1 | all of the above | no |
| B1 | E1 + E1b, WiFi buffers back to 8 | no |
| B2 | B1 without `-Os` | no |
| B3 | B2 without the ROM flash driver | no |
| X1 | reserve 16 K + silent asserts + no enterprise + LVGL to PSRAM + LVGL stack 16 K | **yes** |
| X2 | PHY + heap + SPI/I2C ISRs to flash | **yes** |
| X3 | X2 + LVGL to PSRAM + stack 16 K | **yes** |
| X4 | X2 + reserve 16 K + silent asserts + no enterprise | **yes** |

| X5 | X1 + X2 (every E1 lever except ROM flash driver, `-Os`, WiFi buffers) | **yes** |
| X6 | X5 + own `.bss` and four task stacks to PSRAM + IPv6 off + mDNS to PSRAM | no: assert in `mdns_init` |
| X7 | X5 + ROM flash driver | no: StoreProhibited inside TLSF while loading the apps |

So E1's own culprit was **`SPI_FLASH_ROM_IMPL`**: on top of the working X5 it
alone corrupts the heap (a TLSF free-list pointer of 0xae02c) before the
first app finishes loading. The ROM driver stays off; the 17 K it would
give are not available on this board without finding out why. B1..B3 carried
E1b, and X6 shows E1b's own fault: the linker fragment that sends our
components' `.bss` to PSRAM also moved `libespressif__mdns.a`, whose static
`StaticTask_t` is the mDNS task's TCB, and FreeRTOS asserts that a TCB is in
internal RAM (`portVALID_TCB_MEM`). The fragment no longer touches mDNS.

### 6.2 What the levers are worth, measured

| Build | Static DIRAM | Idle: internal free | Idle: exec free / largest | Watchface render |
| --- | ---: | ---: | ---: | ---: |
| r1 (reference) | 114,487 | 94,083 | 30,020 / 22,016 | (bench not in that build) |
| X2 (code to flash) | 96,079 | 112,575 | 48,512 / 39,936 | 142 ms |
| X1 (config + LVGL) | 106,887 | 115,831 | 67,832 / 56,320 | 115 ms |
| X4 (X2 + reserve/asserts/enterprise) | 90,699 | | | |

The two effects that matter:

* **LVGL allocations to PSRAM** (`aos_lvmem.c`, `-Wl,--wrap=lv_malloc_core`,
  no LVGL config change, apps untouched): with `clima` open the exec heap
  stays at 67,852 free / 52,224 largest; the reference had 8,316 / 7,680.
  The launcher with the policy switched off at runtime costs 57 K of internal
  RAM again (2,154 blocks). The watchface renders in 115 ms with the policy
  on and 117 ms off: no measurable cost, the per-frame draw tasks included.
* **Code to flash** (heap, SPI and I2C ISRs, PHY strings): -20 K static, but
  the same watchface renders in 142 ms instead of 115. The flush path (22
  strips through the SPI ISR) and every `malloc` LVGL makes per frame now run
  from flash through a 16 KB instruction cache. The startup benchmark said the
  same: a full-screen rectangle 52 ms instead of 39.

### 6.3 X5: every E1 lever that survived, together

Every lever that survived, together (X5 = X1 + X2; the branch's
`sdkconfig.defaults` is the later X11, section 6.5): DMA reserve 16 K,
silent asserts, no WPA enterprise, PHY strings, heap allocator and SPI / I2C
ISRs in flash, LVGL allocations in PSRAM, LVGL stack 16 K. Static DIRAM
90,699 (reference 114,487: **-23.8 K**). At runtime, against the reference:

| State | Internal free | Exec free / largest block | Reference exec free / largest |
| --- | ---: | ---: | ---: |
| watchface idle | 132,083 | 84,084 / 75,776 | 30,020 / 22,016 |
| launcher | 132,083 | 84,084 / 75,776 | 8,572 / 7,680 |
| settings | 132,371 | 84,372 / 75,776 | 8,204 / 7,680 |
| clima (dynamic, TLS) | 131,611 | 83,612 / 67,584 | 8,316 / 7,680 |
| calendar | 132,387 | 84,388 / 71,680 | 8,456 / 7,680 |
| photos | 132,055 | 84,056 / 71,680 | 27,088 / 11,008 |
| 2043 / chatarra loaded | 132,055 | 84,056 / 71,680 | 18,016 / 9,216 |
| Bluetooth off | 166,043 | 118,044 / 71,680 | 64,020 / 29,184 |

The general exec heap goes from 22 K in one block at idle (7.7 K with a
screen open) to **68-76 K in one block in every state**. The LVGL task peaks
at 6.3 K of its 16 K; `main` still boots with 764 B to spare and should get
10 K. Watchface full render: 140 ms (X1 without the code-to-flash levers:
115 ms; see 6.4 for the split).

What is still in the exec heap at idle on X5, by owner: task stacks 55.1 K
(LVGL 16 K, main 8.7 K, httpd 8 K, wifi 6.7 K, btController / nimble_host /
esp_timer / mdns 4 K each, tiT 3.6 K, aos_hk / aos_tone 3 K, sys_evt 2.8 K),
BLE controller 27.9 K, WiFi driver 22.5 K, LVGL display buffer 14.7 K, I2S
5.8 K, queues and mutexes 6.7 K, spiffs / nvs / httpd / netif ~5 K, the 48 K
reservation and the 16 K reserve carve-out.

### 6.4 Splitting X5, and the rest of E1b: X8, X9, X10, X11

| Build | Levers | Boots | Static DIRAM | Idle exec free / largest | Watchface render |
| --- | --- | --- | ---: | ---: | ---: |
| X5 | all of section 6.3 | yes | 90,699 | 84,084 / 75,776 | 140 ms |
| X8 | X5 with the ISRs back in IRAM (only the I2C one actually moves: `SPI_MASTER_ISR_IN_IRAM` depends on `!HEAP_PLACE_FUNCTION_INTO_FLASH`) | yes | 91,427 | 83,332 / 73,728 | 140 ms |
| X9 | X5 with the heap allocator back in IRAM | yes | 98,087 | 76,516 / 64,512 | 121 ms |
| X10 | X5 + own `.bss` and tone/http/player/mic stacks to PSRAM + IPv6 off | yes | 80,275 | 97,972 / 86,016 | 139 ms |
| X11 | X10 with the heap allocator back in IRAM: **the configuration to take** | (section 6.5) | | | |

So the render cost of the code-to-flash levers is the heap allocator alone:
7.5 K of exec heap for +16 % on a full render (the draw tasks LVGL allocates
every frame run TLSF from flash). The SPI and I2C ISRs in flash and the PHY
strings together cost 6 ms on 115 (X9 vs X1) for 4.6 K + 0.7 K + 2 K.
`HEAP_PLACE_FUNCTION_INTO_FLASH` stays in `sdkconfig.defaults` commented out,
with its price; note that turning it on also forces the SPI ISR to flash.

The second bundle, without mDNS, is clean: our components' `.bss` in PSRAM
(-10 K static; `main/aos_psram.lf`, `-DAOS_AUDIT_PSRAM_BSS=1`, with
`aos_ble`'s `s_ds` kept internal as its author asked), the tone task's stack
in PSRAM (`-DAOS_AUDIT_PSRAM_STACKS=1`), IPv6 off. X10 boots, confirms,
connects and advertises, and leaves **98 K of executable heap with 86 K in
one block**.

The same switch first moved the http, player and mic stacks too, and the
board found the mistake before the audit did: opening Clima (or
Cotizaciones) reset it. The http task calls `aos_hal_time_is_valid()` before
its request, which reads a preference from NVS, and the flash driver asserts
when the task that starts a flash operation has its stack in PSRAM
(`esp_task_stack_is_sane_cache_disabled`, `cache_utils.c:127`). Only the tone
task, which writes PCM to I2S and nothing else, keeps a PSRAM stack; any task
that can reach NVS, SPIFFS or OTA stays internal.

### 6.5 X11, measured: what this branch ships

X11 = X10 with the heap allocator back in IRAM. It is what
`sdkconfig.defaults` on this branch sets, built with
`idf.py -DAOS_AUDIT_LVGL_PSRAM=1 -DAOS_AUDIT_LVGL_STACK16=1 -DAOS_AUDIT_PSRAM_BSS=1 -DAOS_AUDIT_PSRAM_STACKS=1 build`
(the heap tracing and `/api/mem` come along; drop the instrumentation block
from the defaults for a release). Boots, confirms, connects, advertises.
Static DIRAM **87,663** (reference 114,487, **-26.8 K**).

| State | Internal free | Exec free / largest block | Reference exec free / largest |
| --- | ---: | ---: | ---: |
| watchface idle | 138,523 | **90,524 / 81,920** | 30,020 / 22,016 |
| launcher | 138,523 | 90,524 / 81,920 | 8,572 / 7,680 |
| photos | 138,527 | 90,528 / 81,920 | 27,088 / 11,008 |
| 2043 loaded (28.6 K of the pool) | 138,527 | 90,528 / 81,920 | 18,016 / 9,216 |
| Bluetooth off | 172,535 | 124,536 / 81,920 | 64,020 / 29,184 |

Stacks: 73,472 internal (the tone task's 3 K in PSRAM). LVGL task peak 5.3 K of 16 K. `main` unchanged at
7,948 of 8,704. Watchface full render 128 ms (reference build with the same
instrumentation: 115 ms; the difference is the SPI ISR, the PHY strings and
our `.bss` in PSRAM).

**Net effect on what the audit was for:** the general executable heap, which
is where a bigger app reservation would have to come from, goes from 22 K in
one block at the watchface and 7.7 K with any screen open, to **82 K in one
block in every state**, with the 48 K reservation untouched and the apps'
`.so` files unchanged. The three-and-a-half-times larger block is mostly one
thing: LVGL's objects no longer live there.

### 6.6 Not taken, and why

* `SPI_FLASH_ROM_IMPL`: corrupts the heap on this board (section 6.1).
* `HEAP_PLACE_FUNCTION_INTO_FLASH`: 7.5 K for +16 % on every full render.
* `-Os` on the IDF components with IRAM code: 4.5 K, not worth a bisection.
* WiFi static buffers 6+6, mDNS in PSRAM by Kconfig, BLE controller without
  scan and with 3 activities: never tested alone (they rode in builds that
  died of something else). Candidates for a later round, one at a time.


## 7. Alternatives not considered before, and what they would take

Ordered by what they could give the apps' reservation.

1. **Run the apps' `.text` from PSRAM through the MMU.** The vendored ELF
   loader already has this mode for the S2 (`CONFIG_ELF_LOADER_SET_MMU`,
   `src/soc/esp_elf_esp32s2.c`, about sixty lines): it allocates the `.text`
   in PSRAM, finds free instruction-bus MMU pages (64 K each on the S3),
   points them at the PSRAM pages that hold the code, and `elf_remap_text()`
   translates every symbol to the executable alias. `esp_elf_arch_flush()`
   (dcache write-back plus icache invalidate) exists for the PSRAM case. The
   S3 has the same MMU concept, one linear region shared by both buses
   (`esp_mm/port/esp32s3/ext_mem_layout.c`), and the firmware's own text
   occupies only the first 2 MB of the 32 MB window. `CONFIG_ELF_LOADER_LOAD_PSRAM`
   as shipped does not work here because it assumes the fixed
   `SOC_IROM_LOW - SOC_DROM_LOW` alias that only `SPIRAM_FETCH_INSTRUCTIONS`
   sets up; a port of the S2 file to the S3 (`esp_elf_esp32s3.c`, page
   alignment of the `.text` allocation, cache maintenance) is the whole job.
   It takes the apps out of internal RAM entirely: the 48 K reservation could
   stay for the apps that need speed and everything else load into PSRAM,
   with no size limit but the file. Cost: code fetched through the 16 KB
   instruction cache from 80 MHz octal PSRAM; the game loops are small and
   would mostly hit the cache, but the FPS overlays of claudito / gemas / 2043
   (26 / 30 / 20 today) are the measurement to make first.
2. **LVGL to PSRAM** (section 6.2): done in the fork, no config change, apps
   untouched, no measurable render cost, and it is what stops the executable
   heap from being pulverised by every screen change. This is the one that
   makes a bigger reservation possible: at idle and in `clima` the general
   exec heap keeps 52-56 K in one block instead of 7.7 K.
3. **Half the DMA reserve** (`SPIRAM_MALLOC_RESERVE_INTERNAL` 32 K -> 16 K):
   +16 K to the exec heap at boot. The reserve was 100 % free in every state
   measured; the 32 K DRAM strip stays as the second fallback.
4. **Assertion messages out of DRAM** (`COMPILER_OPTIMIZATION_ASSERTIONS_SILENT`):
   -6.8 K of static data for the strings of functions that run with the cache
   off. A failed assert prints only the address; `addr2line` gives the rest.
5. **Code to flash** (heap allocator, SPI and I2C ISRs, PHY strings): -20 K
   static but -25 % on the render (section 6.2). Worth it only if the frame
   time is not the constraint; the games say it is.
6. **ROM flash driver** (`SPI_FLASH_ROM_IMPL`): -17 K of IRAM (spi_flash and
   its hal). What the ROM lacks (octal flash, 32-bit addressing, auto-suspend,
   write verification, the flash-encryption fix) this firmware does not use.
   Tested alone on top of X5 as X7 (section 6.3).
7. **I2S channels on demand**: the speaker and microphone keep 5,760 of DMA
   descriptors and buffers allocated from boot (`i2s_alloc_dma_desc`, 12
   blocks) although audio plays a few seconds a day. `aos_hal` already wraps
   `i2s_new_channel`; creating the channels when playing or recording starts
   and deleting them after would return that memory to the exec heap.
8. **Task stacks**: `mdns` (4 K) can go to PSRAM by Kconfig; only the tone
   task proved safe in PSRAM (the http one reads NVS and crashes there, see
   6.4); `main` should get 10 K (it boots with 764 B to spare) and `sys_evt`
   3 K. The LVGL task can drop from 20 K to 16 K on the measured 7.6 K peak;
   httpd and wifi stay.
9. **Bluetooth controller**: 3 activities instead of 6 (828 B each) and no
   scan feature; the coexistence path is where two of the E1b crashes were,
   so these need their own test before being trusted.
10. **Flash auto-suspend**: the chip supports it (section 5) and it is the
    only route that lets the `*_IN_IRAM` family and every task stack leave
    internal RAM; ESP-IDF's own documentation names LCD DMA, Bluetooth and
    WiFi interrupt load as the reasons not to. Not tried.
11. **`-Os` on the IDF components with IRAM code**: only -4.5 K; not worth the
    bisection it would need.
12. **Smaller data cache** (`ESP32S3_DATA_CACHE_16KB`): +16 K of DRAM-only
    memory (not exec-capable) at the price of PSRAM bandwidth for every
    canvas and layer. Not recommended.

## 8. Prototype: the apps' code in PSRAM through the MMU

Built on 2026-09-12 on top of X11, behind `CONFIG_ELF_LOADER_TEXT_PSRAM_MMU`
(off by default). What it does:

* `aos_dynapp`'s `esp_elf_malloc` wrapper hands the loader a 64 KB-aligned
  PSRAM block for the `.text` instead of a slice of the 48 K reservation, and
  the reservation is not created at all.
* `components/elf_loader/src/soc/esp_elf_esp32s3.c` (new): for every
  object it asks `esp_mmu_vaddr_to_paddr` where the PSRAM block physically
  is and `esp_mmu_map()` for an executable alias of those pages on the
  instruction bus (`MMU_TARGET_PSRAM0`, `MMU_MEM_CAP_EXEC`; the S3 accepts it,
  only the classic ESP32 refuses PSRAM there). `text_off` becomes the
  distance from the data-bus address of the code to its alias, and the
  loader's existing `elf_remap_text()` (the ESP32-S2 mechanism,
  `CONFIG_ELF_LOADER_CACHE_OFFSET` + `SET_MMU`) applies it to every
  relocation, the entry point and, new in this branch, the addresses `dlsym`
  hands out. After the relocations the code is written back from the data
  cache and the alias dropped from the instruction cache
  (`esp_elf_arch_flush_text`); `esp_mmu_unmap` on close. Every app in this
  repository fits one 64 KB page.
* Two dead ends on the way, both in the serial log: the MMU table must be
  written with the caches frozen *from IRAM code* (the first version called
  the flash-write style "disable caches and the other CPU" from flash and the
  interrupt watchdog reset the board at the first `dlopen`); and
  `esp_mmu_map_reserve_block_with_caps()` must not be used once the mapper's
  block list exists, because it moves `free_head` and every later
  `esp_partition_mmap` then fails its unmap check (the second version died
  reading `otadata`). `esp_mmu_map()` itself does the freezing, the cache-bus
  enable and the invalidation, so the third version has none of that by hand.

Nothing changes for the apps: same `.so` files, same ABI. The pool is gone,
so the general executable heap gains the 48 K as well.

**Result: it works, and the games do not notice.** Build X13 = X11 + the
option. The 22 apps load and unload through the alias at boot, the games run
from PSRAM, the image confirms its trial. At idle the general executable heap
has **140,708 free with 131,072 in one block** (X11: 90,524 / 81,920; the
reference: 30,020 / 22,016), internal free 188,707.

| Game | X12 (code in the 48 K reservation) | X13 (code in PSRAM) |
| --- | ---: | ---: |
| claudito, asleep, idle animation | 13.4 / 13.6 / 13.2 fps | 13.4 / 13.4 fps |
| Claude Jump, playing | 29.2 fps | 29.0 fps |
| 2043, touch mode, level running | 9.6 / 9.8 / 8.6 fps; own log 102-121 ms per frame | 9.6 fps; own log 103-109 ms per frame |

Run-to-run noise (2043: 8.6 to 9.8 on the same build) is larger than any
difference between the builds. Gems could not be measured: the injected
PLAY tap lands only sometimes and its board is static otherwise. The
absolute figures are low because both builds carry heap tracing on every
allocation; only the comparison counts.

How the frame rate was measured: `/api/mem?fps=N` counts LVGL's
`LV_EVENT_RENDER_READY` (one per refresh that drew something, which for a
game redrawing its canvas is one per frame) over N seconds, and
`/api/mem?tap=x,y,ms` injects a touch through the calibration wrapper so the
games can be started without a finger. claudito animates by itself (asleep at
this hour), gems after PLAY (184,363) shows its idle sparkle, 2043 in touch
mode (184,163) runs its level with the ship idle; 2043 also logs its own
"real frame" time. Both builds carry the same instrumentation (heap tracing
included), so only the difference between them means anything.

