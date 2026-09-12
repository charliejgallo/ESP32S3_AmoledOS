# Hardware

AmoledOS targets one board: the **Waveshare ESP32-S3-Touch-AMOLED-1.8**.

| Block | Detail |
| --- | --- |
| MCU | ESP32-S3R8, dual LX7 at 240 MHz, 512 KB SRAM, **8 MB octal PSRAM**, 16 MB flash |
| Display | 1.8" AMOLED, **368 x 448**, QSPI |
| Controller | **v1**: SH8601 + FT3168 touch · **v2**: CO5300 + CST816 touch |
| Brightness | panel command `0x51` — real AMOLED dimming, there is no backlight PWM |
| PMU | AXP2101 (battery, charging, power off) |
| RTC | PCF85063A with its own cell |
| IMU | QMI8658, 6 axes |
| Audio | ES8311 codec + speaker + microphone |
| Storage | microSD over SDMMC, 1-bit |
| Radio | WiFi 2.4 GHz + BLE 5 |

Pins, from the official BSP:

| Block | Pins |
| --- | --- |
| QSPI AMOLED | CS 12, PCLK 11, D0–D3 4/5/6/7 |
| I2C | SDA 15, SCL 14 @ 400 kHz |
| Touch INT | 21 |
| I2S ES8311 | MCLK 16, BCLK 9, WS 45, DOUT 8, DIN 10, PA_EN 46 |
| microSD | CLK 2, CMD 1, D0 3 |
| TCA9554 @ 0x20 | bit0 LCD_RST, bit1 PWR_EN, bit2 TOUCH_RST, bit7 SD_CS |

## One binary, both revisions

The firmware **detects the board revision at run time** by probing the touch
controller's I2C address: `0x15` = CST816 → v2, `0x38` = FT3168 → v1. The same
binary runs on both. This is done by the official BSP; `components/aos_board/`
only reads the result.

## Measured quirks

These are things the datasheets do not say and that cost real debugging time.
Each one is written up at length next to the code that deals with it.

### The touch panel does not reach the bottom

**The digitiser reports nothing below y = 395**, although the panel draws down
to y = 447. The last ~53 pixels are visible and untouchable. Anything clickable
down there is dead, with no error and no warning.

It was measured by printing every touch: with a finger running along the whole
bottom bar, X moved from 23 to 325 while Y read 395 on 13 of 22 touches and
never went past it. Seventy-eight touches across four apps later, the maximum
was still 395.

Calibration does not fix it — the correction is `y = a*raw + b` over a raw
value the chip clamps, so the ceiling moves with `a` but does not go away. Nor
is our software clipping it. The limit belongs to the chip.

It went unnoticed for months because the five calibration points sit at y = 55
and y = 393, so the procedure never measures below 393.

**The rule:** no clickable object may end below `AOS_TOUCH_Y_MAX` (390). What
*should* go there is read-only text — a score, a status line — which is free and
frees up live pixels higher up. Several apps do exactly that.

See `AOS_TOUCH_Y_MAX` in [`components/aos_hal/include/aos_hal.h`](../components/aos_hal/include/aos_hal.h).

**The top has the same limit.** Measured on 2026-09-11 with the Pixel Art
app: six taps on a bar of buttons at y = 8..48 all arrived as y = 55, whatever
the finger did, so the digitiser clamps there too. The usable touch window is
**55..395** — 340 of the 448 rows. Anything that has to be touched goes at
y >= `AOS_TOUCH_Y_MIN` (56); the first rows, like the last, are for text.
The bars that older apps had at y = 8 moved below in v0.3.2 and came back in
v0.3.3, when the raw view showed where the limit really was (next section);
the layout audit (`tools/audit_layout.sh`) measures reach against the landing
rows of that section.

**Where the limit really lived, audited on the board (2026-09-11).** The
whole path was read first: `esp_lcd_touch_cst816s` passes the 12-bit X/Y
through, `esp_lcd_touch` has every mirror/swap flag off, the LVGL port
multiplies by 1, the calibration wrapper clamps only to 0..447, LVGL's
rotation is 0, and the 16 px X gap the BSP applies for the CO5300 is an
offset in the panel's memory addressing that the touch never needed
compensating for. Then the raw view (**Ajustes → TÁCTIL → Ver crudo**: the
raw point, the fit's dot, a ruler, the extremes logged on close) showed the
chip reporting 1..447 and 1..367 — reached with the finger *against the
bezel*, not before: dragging to the bottom edge read 441, only a tap at the
rim read 447. The digitiser already stretches its coordinates so that the
rim reads the last pixel. The five-cross calibration measures exactly that
stretch and inverts it (a = 0.81, b = 29 on this unit, on two occasions),
so between the crosses a touch lands where the fingertip is, and the rim
lands at y = 30 and y = 390. **The dead bands were the calibration's, not
the chip's.** `aos_ui_touch_map()` now keeps the fit between two anchor
rows (60..350) and ramps from there to the bezel, landing on
`AOS_TOUCH_LAND_TOP` (24) and `AOS_TOUCH_LAND_BOTTOM` (410), 16 and 352 in
X: rows 24..410 are reachable, continuously, and a control that contains
the landing row is reachable from the very rim. The two constants above
remain the region where precision is the fit's own.

### The v2's touch chip falls asleep

The v2 carries a **CST820** (ID `0xB7`), not the CST816S the BSP claims. It
sleeps on its own: touching it wakes it, it latches the gesture and the
coordinates, and it goes back to sleep before the driver manages to read the
finger-count register. The chip answers over I2C, reports correct coordinates,
and the screen still looks dead.

Writing any non-zero value to register `0xFE` disables auto-sleep. It also puts
itself back to sleep after its own internal reset, so the firmware rewrites that
register every 5 seconds.

The same chip also **detects swipes itself** and stops sending intermediate
coordinates during a fast one, so LVGL never gathers the 50 px it needs to
declare a gesture. Apps with horizontal pages have to ask
`aos_ui_take_gesture()` as well as listening for `LV_EVENT_GESTURE`.

### The AXP2101 does not measure battery current

It gives voltage, VBUS, VSYS, die temperature, the TS pin and the fuel gauge's
percentage. Current is reported as `NAN` rather than an invented number. The
percentage is a voltage-model gauge, not a coulomb counter, so drain rates and
hours-left are arithmetic on it over time (see [POWER.md](POWER.md)).

### The PMU's interrupt line does not reach the ESP32

From the schematic: `AXP_IRQ` goes to **EXIO5** of the TCA9554 expander, and
the expander's own `INT` pin is only pulled up. So PMU interrupts (power key,
USB in/out, charge done, low battery) are **polled** through the expander over
I2C, every 200 ms in the housekeeping task. The power key's level is also
readable, on **EXIO4** (`SYS_OUT`, high while pressed, through a BSS138).

`PWROK` from the PMU drives `CHIP_PU`: when the PMU cuts the rails, the ESP32
is reset by hardware, which is why a PMU power-off is clean by construction.

### The TS pin has a real NTC on it

`RP2`, a 10K thermistor, sits between TS and ground next to the PMU. Waveshare's
own example disables TS measurement "to avoid abnormal charging" on boards
without one; this board has it, so the firmware leaves it on: the charger keeps
its temperature guard and the firmware gets a board temperature. It is the
PCB's temperature, not the cell's — the battery pack is two wires.

### Which rail feeds what (measured)

Measured on 2026-09-09 by switching each regulator off, rebooting, and
checking the panel's tearing-effect line (GPIO13, 60 Hz only while the driver
IC runs), the accelerometer, the microphone's RMS and the speaker by ear:

| Rail                              | Feeds                                  |
|-----------------------------------|----------------------------------------|
| DCDC1 3.3 V                       | everything: ESP32-S3, IMU, RTC, touch, codec, expander |
| ALDO1 3.3 V, ALDO2 3.3 V, ALDO3 3.0 V, ALDO4 1.8 V, BLDO2 2.8 V | the AMOLED. TE stops with any one of them off |
| DCDC2 0.9 V, DCDC3 1.2 V, DCDC4 1.8 V, BLDO1 1.2 V, CPUSLDO 1.2 V, DLDO1, DLDO2 | **nothing**. The firmware switches them off at boot |
| DCDC5                             | not fitted, off from the factory       |

Two things to know before repeating the experiment. The PMU **keeps the rail
states across an ESP32 reset**: a reboot does not restore anything, only the
firmware's own programme does. And **cutting a panel rail while the panel
runs leaves it dead until the next init**: the screen stays black even after
the rail is back, and the touch keeps reading. Always reboot between a
switch and a judgement. `/api/pmu?rail=NAME&on=0|1` and `/api/pmu?probe=1`
exist for exactly this.

### The panel's reset line is on the expander, and it matters

`LCD_RESET` is EXIO0 of the TCA9554. The BSP leaves it alone and resets the
panel with the software command (0x01) over QSPI. A panel whose interface-mode
register has been corrupted — which is what happens when the QSPI lines float
during light sleep — no longer decodes that command, and neither an ESP32
reset nor switching its five rails off and on recovers it, because its logic
is on DCDC1. The firmware pulls EXIO0 low for 20 ms before every display
init; `/api/pmu?panelreset=1` does it on demand.

### The QMI8658 does not survive accel-only mode

Writing CTRL7 with only the accelerometer enabled (to save the gyro's ~1 mA)
makes the accelerometer itself read 0x7FFF/0x8000 on every axis, and it only
recovers with the gyro enabled again. With the Waveshare driver's init, both
sensors stay on.

### Accelerometer axes

Measured on the board on 2026-08-28, and **not** what the code originally
assumed:

```
ax  is the screen's VERTICAL axis, and the BOTTOM edge is +ax
ay  is the HORIZONTAL one, and the RIGHT of the screen is -ay
az  the normal, and face UP is -az
```

Every app driven by tilting carries a stored axis-mapping setting so a wrong
sign can be fixed without recompiling.

### The QSPI clock runs at 40 MHz, not 80

At 80 MHz the image comes out with garbage: isolated dark pixels along the path
of whatever is moving, which stay put because LVGL considers that area drawn.
It went unnoticed while every app had a black background — a corrupt pixel on
black is indistinguishable from a pixel that is off — and appeared at once on
Truco's green baize.

Pushing a full screen goes from 8 to 16.5 ms, which the frame budget showed was
not the bottleneck anyway.

### The BSP registers the QSPI panel as if it were RGB

`bsp_display_lcd_init()` registers the display with `lvgl_port_add_disp_rgb()`,
and for that type LVGL's port calls `lv_disp_flush_ready()` immediately after
`esp_lcd_panel_draw_bitmap()`. On a real RGB panel that is correct. On this QSPI
panel `draw_bitmap` only *queues* a DMA transfer, so LVGL starts rendering the
next chunk on top of the one still being sent.

The BSP also **ignores the configuration it is given**, building its own from
Kconfig values. AmoledOS therefore brings the display up itself, in
`display_start()` in [`aos_hal_esp32.c`](../components/aos_hal/aos_hal_esp32.c).

### Internal RAM is the scarce resource

There are 8 MB of PSRAM and about 390 KB of internal heap, and the WiFi and
Bluetooth drivers, the task stacks and the firmware's own tables take most of
it: v0.3.3 idled with 30 KB free in the executable heap and 22 KB in its
largest block. Running short of *internal* RAM does not raise an error: it puts
garbage on the screen, because the display flush's DMA buffer allocation fails
and that strip is never drawn.

Three consequences that shape the whole system:

* Large static buffers go to PSRAM (`AOS_BSS_PSRAM`, or plain `malloc()` with
  `CONFIG_SPIRAM_USE_MALLOC`). Since the RAM audit so does the `.bss` of every
  AmoledOS component (`main/aos_psram.lf`) and every LVGL object, style and
  layer (`aos_lvmem.c` wraps `lv_malloc_core`): the launcher alone used to
  scatter 48 KB of small blocks through the executable heap.
* The code of dynamic apps runs **from PSRAM, through the MMU**. The loader
  takes a 64 KB-aligned PSRAM block and maps it onto the instruction bus with
  `esp_mmu_map()`, so it is fetched through the 16 KB instruction cache like
  the firmware's own code from flash; the games measured the same frame rate
  as from internal RAM. Until v0.3.3 the code came out of a 48 KB contiguous
  reservation taken at startup, which is why turning WiFi or Bluetooth on
  used to be an *app* switch too.
* A task that can start a flash operation — anything that reads NVS: the HTTP
  client, the player, the microphone — keeps its stack in internal RAM. The
  flash driver asserts otherwise, and the watch resets.

The whole measurement, lever by lever, is in [RAM-AUDIT.md](RAM-AUDIT.md).
With all of it the executable heap idles with 140 KB free and 131 KB in one
block.
