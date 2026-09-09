# Power: the AXP2101, and what the firmware does to make the battery last

> **State of this document (2026-09-09).** Everything in sections 5 and 6 runs
> on the board and was checked there, except two paths that have not yet been
> through a real discharge: the clean power-off at 3 % (5.2) and the charge
> cycle counter (5.3). The night-on-battery figure (section 7) is the
> measurement still missing. Light sleep is armed on battery only; on USB
> there is nothing to save and the console would die.

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
`drain_pct_h` and `battery_minutes` read from the Battery app in the morning,
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
