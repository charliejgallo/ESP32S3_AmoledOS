# Power: the AXP2101, and what the firmware does to make the battery last

> **State of this document (2026-09-25).** Sections 1 to 8 are the first
> pass, of 2026-09-09. Section 9 is the review of 2026-09-25, after a watch
> ran flat in a pocket in 70 minutes: it corrects three things the first pass
> got wrong (the cell's capacity, what the gauge's percentage is worth, and
> what woke the chip with the screen off) and adds paced WiFi retries, the
> firmware's own state of charge, sleep while dimmed and deep sleep at night.
> Everything in section 9 was checked on the board except the night itself
> and the capacity learning, which need a real night and a real charge.

Written on 2026-09-09 after reading the AXP2101 datasheet (V1.0, section 6.13),
the board's schematic, Waveshare's `90_axp2101_pmu` example and their Arduino
`12_LVGL_AXP2101_ADC_Data` sketch, and comparing them with what
`components/aos_board/axp2101.c` did until then. The numbers marked *measured*
were read on a board; the rest come from the datasheet and are marked as such.

## 1. What was there

The firmware's driver read five things from the PMU — percentage, VBAT, VBUS,
VSYS, die temperature — and could power the board off. Nothing was ever
*written* to the chip except the ADC enable bits. Every charger parameter, every
threshold and every interrupt was whatever the chip's EFUSE said.

"Screen off" meant the panel's brightness register at 0. The driver IC kept
scanning, the QSPI link stayed up, the CPU stayed at 240 MHz, the gyroscope
kept sampling at 125 Hz, and WiFi stayed in its light modem sleep.

## 2. What Waveshare does

Their ESP-IDF example (`examples/esp-idf/90_axp2101_pmu`) wraps XPowersLib in
C++ and, in `pmu_init()`:

| They set                          | Value                          |
|-----------------------------------|--------------------------------|
| Precharge current                 | 50 mA                          |
| Constant-current charge           | **400 mA**                     |
| Termination current               | 25 mA                          |
| Target voltage                    | 4.2 V                          |
| TS pin measurement                | **disabled**                   |
| IRQs                              | battery/VBUS in-out, key short/long, charge start/done |
| Rails                             | dumped to the log, none changed |

Two of those deserve a comment. **400 mA into the 300 mAh cell the board ships
with is 1.3 C**; the chip's own default is 300 mA (1 C) and the cell's
datasheet class asks for 0.5 C. And the TS pin is disabled "because boards
without a thermistor charge abnormally" — but this board *has* the thermistor
(`RP2`, 10K, right next to the PMU), so disabling it throws away the charger's
temperature guard for nothing.

Their example also never gets an interrupt: it polls `pmu_isr_handler()` once a
second. The Arduino sketch shows why — the IRQ is read from **pin 5 of the
TCA9554 expander**, not from a GPIO.

The official BSP (`waveshare/esp32_s3_touch_amoled_1_8`) does not touch the
PMU at all.

## 3. What the schematic says

* `AXP_IRQ` → `EXIO5` on the TCA9554. The expander's `INT` pin has a pull-up
  and nothing else. **No interrupt reaches the ESP32**: it is a poll.
* The power key: `PWRON` on the PMU, and through `T1` (BSS138) to `EXIO4` as
  `SYS_OUT`, high while pressed.
* `PWROK` → `CHIP_PU` through `R9`. When the PMU drops its rails the ESP32 is
  held in reset by hardware.
* `TS` → `RP2` 10K NTC → GND. The charger's temperature guard is real.
* `CHGLED` → test point only. No charge LED to drive.
* `VBACKUP` → `H3` (not fitted). The RTC's backup cell pads.
* Rails: **DCDC1 = VCC3V3**, which feeds the ESP32-S3, the AMOLED (`VCI`,
  `VDDIO`), the IMU and the codec's digital side. DCDC2 (0.9 V), DCDC3 (1.2 V),
  DCDC4 (1.8 V), ALDO1–4, BLDO2 and CPUSLDO are drawn with net names and no
  consumer that could be traced in the PDF. DCDC5 and BLDO1 are not fitted.

## 4. What the datasheet says the chip can do, and what it cannot

Can:

* Measure VBAT, VBUS, VSYS, its own die temperature, and the TS pin (0.5 mV
  per LSB, with a 20/40/50/60 µA source it injects itself).
* Report the charger's stage: trickle, precharge, CC, CV, done, idle.
* Raise interrupts for: USB in/out, battery in/out, key press / release /
  short / long, charge start / done / timeout, SOC at a warning level, SOC at a
  shutdown level, over-temperature (die and battery, charge and work), LDO and
  BATFET over-current, battery over-voltage.
* Take a programme: precharge, CC and termination currents; target voltage
  (4.0 / 4.1 / 4.2 / 4.35 / 4.4 V); USB input current limit; the two SOC
  levels; VOFF (the VSYS voltage at which it cuts everything, 2.6–3.3 V); the
  key's on / long / off timings; each rail on or off and its voltage.
* Remember why it powered on (reg 0x20) and **why it last powered off** (reg
  0x21), across the off period.

Cannot:

* Measure current. There is no sense resistor path and no coulomb counter. The
  percentage is a voltage-model "E-Gauge". Anything about rates is arithmetic
  on that percentage over time.
* Power itself off on a percentage. The SOC levels are interrupts; the only
  autonomous cut is VOFF, whose factory value is **2.6 V** — well past the
  point a lithium pouch cell should ever rest at.

## 5. What the firmware does now

### 5.1 The charger's programme (`aos_hal_esp32.c`, `AOS_CHARGE_*`)

| Parameter       | Chip default | Firmware, battery care ON | battery care OFF |
|-----------------|--------------|---------------------------|------------------|
| CC current      | 300 mA (1 C) | **150 mA (0.5 C)**        | 300 mA           |
| Target voltage  | 4.20 V       | **4.10 V**                | 4.20 V           |
| Precharge       | 125 mA       | 50 mA                     | 50 mA            |
| Termination     | 125 mA       | **25 mA (C/12)**          | 25 mA            |
| Warning level   | 15 %         | 10 %                      | 10 %             |
| Shutdown level  | 1 %          | 3 %                       | 3 %              |
| VOFF            | 2.6 V        | **2.9 V**                 | 2.9 V            |

*Battery care* is the Settings switch (`Cuidar la bateria`, default on). The
4.1 V ceiling gives up roughly a tenth of each charge and, by every cycle-life
curve published for these chemistries, roughly doubles the number of charges
before the cell is at 80 % of its capacity. 0.5 C does the same on the current
side. The termination at C/12 (instead of the chip's C/2.4) is the opposite
correction: with 125 mA the charger declared "done" while the cell was still
visibly not full.

VOFF at 2.9 V is a backstop. It leaves the cell a margin of about 0.3 V over
the factory cut, which is weeks of the PMU's own off-state drain before the
cell reaches a voltage it does not come back from. It is not higher because
VSYS sags under a WiFi burst and the PMU must not cut on a sag.

### 5.2 A clean power-off before the cell is flat

Two paths, both only on battery:

* The PMU's *shutdown level* interrupt at 3 %.
* A software backstop every 5 s: three consecutive readings at ≤ 3 % **or**
  VBAT < 3.30 V, not during the first 30 s after boot (the gauge needs a moment).

Either one raises `AOS_POWER_CRITICAL`, the UI shows *Bateria agotada,
apagando*, three seconds pass, and if USB has not appeared meanwhile the
counters are saved and the PMU is told to cut. The watch therefore never runs
down to VOFF on its own.

### 5.3 The PMU's interrupts, polled

`housekeeping_task` reads EXIO5 every 200 ms (one I2C read of the expander's
input register). Only when the line is low does it read and clear the three
status registers. What arrives:

| IRQ                          | Becomes                                              |
|------------------------------|------------------------------------------------------|
| key negative edge            | `AOS_BUTTON_PWR` / `PRESS`                           |
| key short press              | `AOS_BUTTON_PWR` / `CLICK` — the screen switch       |
| key long press (1.5 s)       | `AOS_BUTTON_PWR` / `LONG` — "keep holding to power off" |
| VBUS insert / remove         | `AOS_POWER_USB_IN` / `OUT`, resets the drain arithmetic |
| charge done                  | `AOS_POWER_CHARGE_DONE`, cycle counter +1            |
| SOC at warning level         | `AOS_POWER_LOW_BATTERY`, once per discharge          |
| SOC at shutdown level        | the clean power-off above                            |
| die / battery over-temp      | `AOS_POWER_OVERHEAT`                                 |
| charger safety timer         | a warning in the log                                 |

The power key never did anything before: the HAL had `AOS_BUTTON_PWR` in its
enum and nothing that emitted it. Now a click turns the screen on when it is
off and off when it is on; holding it is still the PMU's own power-off (6 s
from the factory, read at boot and shown in the log).

### 5.4 Panel sleep (built, measured, and OFF by default)

With the screen off the AMOLED's driver IC can be put in *sleep in* (0x10):
charge pumps and scanning stop, frame memory is kept, and the wake is
*sleep out* (0x11) plus the 120 ms the DCS spec asks for. It works, it
survives light sleep, and **it flashes**: on every sleep-out this CO5300 shows
a light-blue then a white frame before the first real one, with the
brightness register at 0, with display-off around it or without it, and with
light sleep on or off (three rounds of A/B with the board in hand). It is the
panel's own power-up, not anything the firmware draws.

So the default is off: "screen off" is brightness 0, which the panel shows as
black and which is what the firmware always did. The Settings switch *Dormir
el panel apagado* turns sleep-in on for whoever prefers the saving to the
flash; the code path stays, with one sequence trap fixed on the way (the UI
moves on while the panel sleeps, so the whole screen is rendered into the
panel's memory before it is shown, or the old content bleeds through).

### 5.5 CPU frequency

`CONFIG_PM_ENABLE` is on and the HAL configures DFS between 80 and 240 MHz,
**without light sleep** — the QSPI panel, the I2S codec and the USB console
would need more work to survive it. A pm lock on `ESP_PM_CPU_FREQ_MAX` is held
while the screen is active or audio is playing or recording, and released
otherwise. With *Ahorro de energia* off in Settings the lock is simply always
held, which is exactly the firmware's old behaviour.

Power saving also switches itself on under 20 % on battery, and off again on
USB or above that.

### 5.6 WiFi modem sleep

`WIFI_PS_MAX_MODEM` with the screen off and power saving active,
`WIFI_PS_MIN_MODEM` otherwise. The web portal answers a few hundred ms later
with the screen off.

### 5.7 The gyroscope stays on (tried, and it broke the accelerometer)

Steps, orientation and wrist-raise are accelerometer-only, and the gyroscope
is the expensive half of the QMI8658 (about 1 mA at 125 Hz). The first
version of this branch started the chip with CTRL7 = accelerometer only and
switched the gyro on for the one app that shows it. **On the board the
accelerometer then read 0x7FFF/0x8000 on every axis**, and it only came back
with the gyro enabled again. The saving is still there to be had, but not
through that register write with this driver's init; both sensors stay on and
`aos_hal_imu_gyro_request()` only counts.

### 5.8 What the battery screen and `/api/status` show

Straight from the PMU: percentage, VBAT, die temperature, board temperature
(the NTC), VBUS, the charger's stage and its programme. Kept by the firmware:
drain in %/h and hours left (nothing until a quarter of an hour and two percent
of drop have gone by, reset on every USB insert), time since unplugged,
lifetime minutes on battery and completed charge cycles (both in NVS), the CPU
clock right now, whether the panel is asleep, whether saving is active, and why
the PMU last powered off.

### 5.9 One SPI device, two tasks: the panel-off race (2026-09-15)

While the icons branch was being tested, the board rebooted once on its own
with `boot_reason: PANIC`. The dump in the `coredump` partition, symbolised
with `xtensa-esp32s3-elf-gdb`:

```
assert failed: spi_device_release_bus (spi_master.c:1400)
    "Cannot release bus when a polling transaction is in progress."
  panel_io_spi_tx_param  <-  esp_lcd_panel_io_tx_param
  bsp_display_brightness_set              the BSP, command 0x51
  aos_hal_display_set_state(AOS_DISPLAY_OFF)   aos_hal_esp32.c
  housekeeping_task                       the idle timeout
```

**What happened.** The panel is one SPI device, and both the LVGL flush
(`tx_color`, the pixels) and the brightness register (`tx_param`, 0x51)
go through it. esp_lcd wraps each transaction in
`spi_device_acquire_bus()` / `release_bus()`, and the bus lock behind those
is **per device, not per task**: a second task acquiring the device the
first one already holds is let straight through (`spi_bus_lock.c`,
`req_core`: "if we are the acquiring processor"). So the housekeeping task,
turning the screen off on the idle timeout, sent the brightness command in
the middle of a flush the LVGL task was polling on the same device. The
first of the two to release hit the assert. esp_lcd's documentation says
the panel IO is not thread-safe; this is what that looks like.

`panel_sleep()` already took the LVGL lock around its own commands (5.4),
precisely for this. `bsp_display_brightness_set()` did not: it was called
bare from `aos_hal_display_set_state()` (three of its four paths), from
`aos_hal_brightness_set()` and `aos_hal_aod_brightness_set()` - and those
run on the housekeeping task and on the web server's task, not on LVGL's.

**The fix.** Every brightness write goes through `panel_brightness()`, which
takes the LVGL lock (recursive, so the LVGL task's own calls from Settings
are unchanged), writes, and releases. The flush runs inside
`lv_timer_handler()` under that same mutex, so with it held the bus is
either idle or has only queued colour transfers, which `tx_param` itself
waits for. That is the one order esp_lcd supports.

**Measured, A/B, same firmware otherwise.** `/api/mem?spin=N` writes the
brightness N times from the HTTP task while the UI draws, and
`tools/spi_stress.sh` runs three rounds: 1,500 writes with Vida
(Game of Life) redrawing, 5,000 writes while the launcher list is scrolled
by injected drags, and 120 cycles of `apagar` / `despertar` over the portal
with Vida up.

| Build | Vida x 1,500 | launcher x 5,000 | off/on x 120 |
|---|---|---|---|
| A - `AOS_TEST_UNLOCKED_BRIGHTNESS=1`, the old bare call | 2,773 ms, survived | **task watchdog reboot**: the LVGL task stuck in `wait_for_flushing()` for a flush whose completion the interleaved command had lost | not reached |
| B - the fix | 8,850 ms, clean | 13,084 ms, clean | clean |

The A side shows the same race with the other face: instead of the assert,
a lost flush and a watchdog. The B side takes longer because each write now
waits its turn behind the frame being drawn - which is the point. The
original trigger, the idle timeout with a flush in flight, is not something
a script can time; the spin is that window opened wide.

The switch `AOS_TEST_UNLOCKED_BRIGHTNESS` is for reproducing this and
nothing else; it is off by default and sticks in the CMake cache like the
audit switches (BUILDING.md).

## 6. Measured on the board (2026-09-09, v2, USB-powered, WiFi + BLE up)

* **Boot programme confirmed in the PMU's own dump**: cc 150 mA, pre 50 mA,
  term 25 mA, target 4100 mV, warn 10 %, shutdown 3 %, VOFF 2900 mV, key
  long 1500 ms / off 6000 ms. Powered on by "power key", last power-off
  "power key held".
* **The power key works**: press → `pmu irq 0x000200`, release → `0x000900`
  (positive edge + short). A click from *dimmed* lit the screen; a click from
  *active* put it off. The whole round trip through the expander poll is
  under 250 ms.
* **Panel sleep**: `panel asleep in 4 ms`, `panel awake in 119 ms`, and the
  touch wakes it. WiFi went to `ps type 2` (MAX_MODEM) on off and back to 1 on
  wake.
* **The TS pin has the NTC after all**, but Waveshare's EFUSE leaves reg 0x50
  at 0x12: TS as "external input" (does not gate the charger) and the
  current source OFF, so the ADC read full scale and the board looked
  thermistor-less. With the source on it reads 0.43 V = 8.6 kΩ = 28.7 °C next
  to a die at 33 °C. The driver now sets the source to "on while sampling"
  and leaves the charger ungated, which for a PCB thermistor is the right
  call.
* **The rail map**, by switching each regulator off, rebooting and checking
  the panel's TE line, the accelerometer and the microphone (details in
  [HARDWARE.md](HARDWARE.md)): the AMOLED needs ALDO1-4 and BLDO2; DCDC2,
  DCDC3, DCDC4, BLDO1, CPUSLDO, DLDO1 and DLDO2 feed nothing and are now off
  from boot. Two traps found on the way: rail states **survive an ESP32
  reset** (only the firmware's programme restores them), and cutting a panel
  rail while the panel runs leaves it black until the next init even after
  the rail returns.
* **DFS cannot be read from a task.** ESP-IDF holds a `CPU_FREQ_MAX` lock
  (`rtos0`/`rtos1`) whenever a core is not idle, so any measurement made from
  a task — the web handler included — sees 240 MHz. The frequency is 80 MHz
  only in idle. `CONFIG_PM_PROFILING` accumulates time per mode and
  `/api/pmu?locks=1` prints it; that is the number to quote.

## 6b. Light sleep (2026-09-09, second pass)

Armed only with the screen off, power saving active and no audio, through
`esp_pm_configure()` at run time, so the rest of the time the firmware behaves
exactly as in section 5. What it took, in the order the lock dump forced it:

1. **The BLE controller.** It pinned `btLS NO_LIGHT_SLEEP` and `bt
   APB_FREQ_MAX` for good. With modem sleep (`BT_CTRL_MODEM_SLEEP`, mode 1)
   and the main crystal as its low-power clock (the ESP32 has no 32 kHz
   crystal on this board), kept powered through sleep
   (`BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP`), it holds the APB lock 25 % of
   the time and no light-sleep lock at all.
2. **The I2S channels.** The BSP enables both at init, `esp_codec_dev` enables
   them on open and never disables them on close, and an enabled channel holds
   `APB_FREQ_MAX`. The HAL wraps `i2s_new_channel` to keep the handles and
   parks both channels whenever nothing plays or records; the codec brings
   them back on its next open.
3. **LVGL's tick.** The port ran a 5 ms `esp_timer` for it, and tickless idle
   wants 8 ms of nothing before it sleeps. The tick now comes from
   `esp_timer_get_time()` through `lv_tick_set_cb()`, and the port's timer is
   left at 100 ms.
4. **LVGL's own timers.** With the screen off the refresh timer is paused and
   the touch read timer goes from 33 to 200 ms. The housekeeping task goes
   from 40 to 100 ms.
5. **Wake sources.** GPIO21 (the touch INT, pulses low on a finger) and GPIO0
   (BOOT), both low-level. WiFi and BLE wake the chip on their own.
6. **The panel.** `ESP_SLEEP_GPIO_RESET_WORKAROUND` isolates every GPIO in
   sleep: chip select and the four QSPI lines float, the CO5300 reads the
   noise as commands, and the first real sleep left it black. Worse: what it
   corrupts includes the interface-mode register, after which it no longer
   understands the software reset the BSP relies on, and neither an ESP32
   reset nor a power cycle of its five rails brings it back (its logic hangs
   from DCDC1). Two fixes: the six panel pins keep their normal configuration
   through sleep (`gpio_sleep_sel_dis`), and the firmware now pulls the
   panel's hardware reset on EXIO0 before every init, which the BSP never
   did. `/api/pmu?panelreset=1` does it on demand.

Measured with the screen off, 120 s, WiFi and BLE connected, over USB:

| Mode                | Time  |
|---------------------|-------|
| light sleep         | 53 %  |
| 80 MHz, awake idle  | 21 %  |
| 240 MHz, busy       | 26 %  |

About 30 sleeps a second, so the average nap is under 20 ms: the phone's BLE
connection interval and the 100/200 ms cadences above are what bound it. The
web portal answers in about 190 ms through it, and a touch wakes the screen
as before.

Panel sleep was on during these measurements and is off by default since
(5.4); the light-sleep figures do not depend on it.

Two things to know when working on the board with light sleep on:

* **The USB console dies the moment the chip sleeps** (the USB-Serial-JTAG
  peripheral does not survive light sleep), and after a while the device is
  gone from the Mac altogether until it is replugged. `idf.py flash` then
  cannot find the port; **`tools/install_fw.sh <ip>` over WiFi is the way to
  update** while this is on. That is also why the PM dump is served by
  `/api/pmu?locks=1` as text and the reset reason by `/api/status` instead
  of being read from the log.
* Light sleep is armed **on battery only**, since the USB console does not
  survive it and there is nothing to save on the cable. Plugging USB in
  disarms it within five seconds, through the PMU's VBUS report.
* One trial image rolled back once during the A/B rounds; its crash was not
  captured (no console). `boot_reason` in `/api/status` exists since, so the
  next one will be.
* It is a Settings switch (*Dormir el chip apagado*), so a misbehaving
  peripheral can be taken out of the equation without a reflash.

## 6c. What has NOT been done, and why

* **No current measured with a meter.** The gains are argued from the
  datasheet and observed in the PM statistics, not in milliamps. A USB power
  meter inline, or a night's drain figure before and after each switch, is
  the measurement that is still missing.
* **The gyro saving** (5.7) needs the QMI8658C's CTRL2/CTRL7 pages read
  with the board in hand.
* **Longer naps.** The BLE connection interval is the phone's to set; the
  housekeeping and touch cadences with the screen off could be stretched
  further at the cost of wrist-raise and touch latency.

## 9. The review of 2026-09-25

### 9.1 What happened

The watch left home at 9, in a pocket, screen off, and was flat by half past
ten. Charged, taken out again, flat again in 55 minutes. The battery history
(`sd/data/battery24.bin`, one sample every five minutes) showed both stretches
losing about 50 % an hour, against 23 to 28 % an hour at home while being used.
And both times the watch switched off with the percentage at **49 %** and
**65 %**.

Three things were wrong, and none of them was light sleep.

**The WiFi never stopped looking for home.** The disconnect handler called
`esp_wifi_connect()` on every disconnect, at once, for ever. At home that is a
reconnect after a hiccup. Away from home each attempt is a scan of every
channel with the radio at full power, it fails with reason 201 (no access
point found), and the next one starts straight away. The radio never rests and
the chip never sleeps: about 110 mA on average, in a pocket, screen off.

**The percentage was the AXP2101's gauge, and it does not know this cell.** The
cell was empty both times: the recharge from "2 %" took 55 minutes, the same as
from zero. The 3.30 V backstop of 5.2 is what switched the watch off, and it
was right to.

**The cell is about 130 mAh, not 300.** 55 minutes at 150 mA at most, from
empty to the 4.1 V target. Section 5.1 assumed 300 mAh without checking it,
and so did its "0.5 C". Battery care's 150 mA is 1 C on this cell; the
estimator below measures the capacity on every charge that starts low.

### 9.2 Paced reconnection

Each failure waits longer than the one before: 0, 2, 5, 15, 30 s, 1, 2, 5 and
10 min (60 s at most on USB). On battery with the screen not lit, after three
failures the station stops altogether until the screen is lit or USB comes in,
and then it tries once at once and carries on a few rungs down the ladder.
`/api/status` reports `wifi_fail`, `wifi_parked`, `wifi_next_s` and
`wifi_reason`.

Measured with `/api/pmu?wifitest=150&idle=1`, which points the station at a
network that does not exist for 150 s without touching the stored one:

```
wifi test: the network is gone for 150 s, acting as if on battery, screen off
wifi: failure 2 (reason 201), next try in 2 s
wifi: failure 3 (reason 201), next try in 5 s
wifi: 4 failures (reason 201), on battery with the screen off: not retrying
   ... 136 s of silence ...
wifi test: over, back to the stored network
wifi connected                                  (1.1 s later)
```

Each failed attempt is about 2.4 s of scanning. The same test with a touch in
the middle logged `screen lit, trying again now` on the touch.

Two holes found on the way, both closed: the "screen lit" kick did nothing
when no retry was scheduled, and `aos_hal_net_enable()` with the stack already
up got no `STA_START` event to connect from, so new credentials over a failing
station waited for the next retry. The old endless loop had hidden both.

In `WIFI_PS_MAX_MODEM` (screen not lit) the station now wakes every 10 beacons
(`listen_interval`) instead of every 3.

### 9.3 The firmware's own state of charge

`components/aos_hal/aos_soc.c`, pure C, with a bench in `tools/soc/`:

- **At rest** (screen not lit for 30 s, no audio, the radio not connecting) the
  voltage is close to the open-circuit voltage, and a generic LiPo curve maps
  it to charge, with 100 % at the charge target after resting.
- **With the screen lit** the voltage sags; how much is learned on the watch,
  comparing the voltage at rest with the voltage 20 s after the screen comes
  on. The first sample on the board: 39 mV.
- **Charging**, the current is known in the constant-current phase (it is what
  the firmware programmed), so charge is counted in; the constant-voltage tail
  is approached smoothly; "done" is 100 %.
- **The capacity** is measured on every charge that starts at 40 % or less
  from a reading at rest: charge counted in over the fraction filled.

On the bench, against simulated cells that deliberately do not match its
assumptions (capacity, curve and internal resistance all off), it stays within
6 points in everyday use and reaches 2-3 % when the cell reaches 3 %, where the
gauge showed 49 %. It is never optimistic; with the radio at full power and
not flagged it reads low. One charge from low learned 127 mAh for a 130 mAh
cell. `/api/status` carries `soc`, `gauge_pct`, `sag_mv`, `cap_mah` and their
sample counts; the battery history now keeps the voltage of every sample and
whether USB was in (file format BST2, BST1 files still read).

The low-battery warning (10 %) and the clean power-off use this percentage.
The power-off asks for 2 % **and** less than 3.45 V, so a wrong estimate cannot
switch off a watch that still has charge; the 3.30 V backstop stays as it was.

### 9.4 What kept the chip awake with the screen off

`CONFIG_PM_PROFILING` and a per-task run-time dump (`/api/pmu?tasks=1`),
differenced over a minute on battery, screen off, Bluetooth off:

| | v0.6.2 | now |
|---|---|---|
| time in light sleep | 81 % | 93.5 % |
| I2C transactions per second | 145 | 16.5 |
| time at 240 MHz | 16.5 % | 4.7 % |

What the 145 were:

- **110 a second came from one line**: the housekeeping task's "no always-on
  with the battery on its last legs" check, from the first pass, did a full
  PMU read (eleven I2C transactions) on every pass, ten times a second. It now
  looks at the percentage `power_watch()` already keeps.
- **30 a second were the IMU**: the Waveshare driver reads the timestamp, the
  data and the temperature for every sample. The data is now one burst; the
  temperature is read every five seconds.
- The touch task read the CST820 ten times a second with nobody touching. With
  the screen not lit it now waits for INT, lights the screen itself and
  swallows that first touch (the gesture that woke the screen does not open
  the launcher, checked by hand). LVGL's touch read is paused.
- A full PMU read now serves everybody for a second (the status bar and
  several watchfaces asked on every UI tick), and the main loop ticks once a
  second with the screen not lit, returning at once when the display changes.

**The CST820 goes into its own auto-sleep with the screen not lit.** The old
comment said that asleep it stops asserting INT; measured, a single short
touch still wakes the screen, from light sleep and from deep sleep. It is the
default (`touch_slp`).

**`gpio_wakeup_enable()` turns the touch pin into a level interrupt**,
overriding the falling edge the touch driver set: with INT low it fired again
and again. The ISR now disables itself and the task enables it after reading.

### 9.5 Light sleep while dimmed

Always-on used to hold the chip awake for its five minutes after every
glance. Now light sleep is armed whenever the screen is not lit: the panel
keeps its image on its own and its six QSPI pins keep their levels through
sleep (6b). Measured, dimmed, on battery: **91.5 %** of the time asleep. The
portal answers in about 180 ms.

### 9.6 Deep sleep at night

A preference, off by default (Settings, Battery; portal, `noche`): during the
scheduled do-not-disturb hours, on battery, with the screen off for ten
minutes and nothing going on, the watch goes into deep sleep.

- **What vetoes it**: USB, audio, the link, an image on trial, "calls always"
  with the phone connected, and any app holding it with
  `aos_hal_sleep_hold()` (the timer, the pomodoro and the stopwatch while they
  count: a boot would lose them).
- **What wakes it**: a touch (GPIO21, the CST820's INT) or BOOT (GPIO0), both
  through ext1; the end of the window; an alarm (main.c's guard brings the
  wake-up two minutes before the next enabled alarm). The power key and the
  IMU cannot: they end on the TCA9554, whose INT does not reach the ESP32.
- **Chunks**: it sleeps half an hour at a time. The chip's timer runs on an RC
  oscillator; each chunk ends in a quick boot that reads the PCF85063 again
  and, if the night goes on, goes straight back to sleep before the panel,
  the WiFi or the apps (`night_boot()`).
- **Before sleeping**: counters saved (steps included), panel display-off and
  sleep-in, its five rails cut, the IMU powered down, the CST820 in
  auto-sleep.
- **Waking is a full boot**: WiFi at about 4.8 s, the apps at 5.75 s.

Checked with `/api/pmu?deep=N` (a night of N seconds now, chunks of a third):
two quick re-sleeps and a full boot at 182 s with panel, IMU, codec and
microphone working; and a touch wake. `/api/status` keeps `nights`,
`night_chunks`, `night_slept_s` and `night_last_wake` (1 end, 2 touch or
BOOT, 3 USB) until the next power-on.

Two things the first touch wake broke, both fixed:

- **A panic on the boot out of deep sleep.** The CST820, left in auto-sleep,
  did not answer on I2C; LVGL's port reads the touch before our task exists
  and treats a failed read as fatal (`ESP_ERROR_CHECK` in
  `lvgl_port_touchpad_read`). Now the touch controller is reset through
  **EXIO2 (TP_RESET)** at the start of `aos_board_init()`, before the board
  variant is probed, and a failed touch read is reported to LVGL as "no
  finger", never as an error.
- **A green bar down the right edge.** The CO5300's memory is wider than the
  368 columns LVGL draws (the v2's image sits 16 columns in) and the glass
  shows a few columns past the right edge. Nothing ever wrote them; they were
  black only because the panel never lost power. Cutting its rails left
  garbage there. Every start now clears 466 columns once, before LVGL.

### 9.7 Still to do

- **A real night** with deep sleep on, and a real charge from low to see the
  capacity learned (`cap_n` goes to 1). Section 7 is the protocol.
- **The cell's own discharge curve**, from the voltage the history now keeps,
  to replace the generic one in `aos_soc.c`.
- **Battery care's current**, once the capacity is known: 150 mA may be 1 C.
- **No current measured with a meter.** The plan is the Riden RD6012 as a
  battery on the cell's connector, which also exercises the power-off path in
  minutes instead of hours.

## 7. The night on battery: protocol

The one measurement still missing is a whole night, and it has to be done
the same way every time so nights compare. `tools/battery_night.py` records
and reports; this is the procedure around it.

**Before bed**

1. Charge to "full" over USB. With battery care on, full is 4.10 V and the
   gauge will say somewhere in the 90s: that is expected, the gauge's model
   ends at 4.2 V. Note the starting percent and volts from `/api/status`.
2. Leave the watch where it will spend the night: on the table, face up, not
   moving (wrist-raise is accelerometer-driven; a watch on a table never
   raises). The phone within BLE range if a normal night has it there.
3. Settings as they will be used: power saving on, battery care on, panel
   sleep off, chip sleep on, always-on as you normally have it. Write down
   what they were.
4. Start the recorder on the Mac, under `caffeinate` so the Mac stays up:

       cd ESP32S3_AmoledOS_power
       caffeinate -i python3 tools/battery_night.py 192.168.1.125

5. **Unplug USB.** The recorder notices (`bat` instead of `USB`) and the
   report starts counting from that sample.

**In the morning**

6. Ctrl-C the recorder, then:

       python3 tools/battery_night.py --report night-<date>.csv

   It prints percent and millivolts per hour, the share of time in light
   sleep, how often the screen was found on, whether the board rebooted and
   how many polls it missed.

**What the numbers mean.** The AXP2101 cannot measure current, so the report
divides the percent drop over the nominal 300 mAh and calls the result a
yardstick. Volts per hour is the finer signal but the discharge curve is not
linear: compare nights on the same stretch of the curve (start full every
time). A night is one point; two nights of the same firmware say how
repeatable the point is; then the other firmware.

**What to compare, in order**

| Night | Firmware / setting                      | Question it answers                |
|-------|-----------------------------------------|------------------------------------|
| 1     | this branch, everything as above        | what the watch costs at rest now   |
| 2     | same again                              | is night 1 repeatable              |
| 3     | this branch, chip sleep OFF in Settings | what light sleep is worth          |
| 4     | this branch, WiFi off                   | what the network costs at rest     |
| 5     | the published firmware, via `install_fw.sh` from the main checkout | the before figure |

The recorder needs WiFi on the watch, so night 4 is the firmware's own
`drain_pct_h` and `battery_minutes` read from the Battery app (a page of
Settings since v0.5.1) in the morning,
not the CSV.

## 8. Register cheat-sheet (the ones the firmware touches)

| Reg  | What                                  | Encoding                              |
|------|---------------------------------------|---------------------------------------|
| 0x00 | status 1                              | bit5 VBUS good, bit3 battery present  |
| 0x01 | status 2                              | bits 6:5 current direction (01 charge), bits 2:0 charge stage |
| 0x10 | common config                         | bit0 = soft power-off                 |
| 0x16 | USB current limit                     | 0..5 = 100/500/900/1000/1500/2000 mA  |
| 0x18 | charger / gauge / watchdog enable     | bit1 charge, bit3 gauge               |
| 0x1A | low battery levels                    | 7:4 warn = 5 + n %, 3:0 shutdown = n % |
| 0x20 | power-on source                       | bit0 key, bit2 VBUS, bit3 charged, bit4 battery in |
| 0x21 | power-off source                      | bit0 key, bit1 software, bit3 VSYS < VOFF, bit7 die temp |
| 0x24 | VOFF                                  | 2:0 = (mV − 2600) / 100               |
| 0x27 | key timings                           | 5:4 long 1/1.5/2/2.5 s, 3:2 off 4/6/8/10 s, 1:0 on 128/512/1000/2000 ms |
| 0x30 | ADC enables                           | bit0 VBAT, bit1 TS, bit2 VBUS, bit3 VSYS, bit4 die |
| 0x34 | VBAT                                  | H5L8, mV                              |
| 0x36 | TS                                    | H6L8, 0.5 mV/LSB                      |
| 0x38 / 0x3A / 0x3C | VBUS / VSYS / die       | H6L8; die °C = 22 + (7274 − raw) / 20 |
| 0x40–0x42 | IRQ enables                      | see `AXP2101_IRQ_*`                   |
| 0x48–0x4A | IRQ status                       | same bits, write 1 to clear           |
| 0x50 | TS control                            | bit4 = TS not a thermistor, 1:0 source current |
| 0x61 | precharge current                     | 25 × n mA, n ≤ 8                      |
| 0x62 | CC current                            | 25 × n mA to n = 8, then 200 + 100 × (n − 8) |
| 0x63 | termination                           | bit4 enable, 3:0 = 25 × n mA          |
| 0x64 | target voltage                        | 1..5 = 4.0 / 4.1 / 4.2 / 4.35 / 4.4 V |
| 0x80 | DCDC enables                          | bits 4:0 = DCDC5..1                   |
| 0x90 | LDO enables                           | ALDO1-4, BLDO1-2, CPUSLDO, DLDO1      |
| 0xA4 | battery percentage                    | 0..100                                |
