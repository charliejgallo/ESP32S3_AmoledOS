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

### Which rail feeds what

DCDC1 is `VCC3V3`, and everything hangs from it: the ESP32-S3, the AMOLED
(`VCI`/`VDDIO`), the IMU, the RTC's I2C side. DCDC2/3/4 (0.9/1.2/1.8 V), the
four ALDOs, BLDO2 and CPUSLDO appear on the schematic with names and no
consumer that could be traced. The firmware logs the rail table at boot
(`axp2101: rails`); nothing is switched off until that table has been read on
a real board, and DCDC1 is refused by the driver.

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

There are 8 MB of PSRAM and roughly 83 KB of executable internal RAM. Running
short of *internal* RAM does not raise an error: it puts garbage on the screen,
because the display flush's DMA buffer allocation fails and that strip is never
drawn.

Two consequences that shape the whole system:

* Large static buffers go to PSRAM (`AOS_BSS_PSRAM`, or plain `malloc()` with
  `CONFIG_SPIRAM_USE_MALLOC`).
* The code of dynamic apps comes out of a **48 KB contiguous reservation** taken
  at startup, before anything can fragment the heap. Turning WiFi on costs
  ~60 KB of the same pool; turning Bluetooth on costs ~30 KB. Both switches are
  therefore also *app* switches.
