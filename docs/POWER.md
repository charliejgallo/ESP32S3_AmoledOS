# Power: the AXP2101, and what the firmware does to make the battery last

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

### 5.4 Panel sleep

With the screen off, the AMOLED's driver IC gets *display off* (0x28) and
*sleep in* (0x10): charge pumps and scanning stop, frame memory is kept.
Waking is *sleep out* (0x11), the 120 ms the DCS spec asks for, *display on*
(0x29) and the brightness command. Both revisions' controllers (SH8601 and
CO5300) go through the same driver and the same QSPI opcode wrap.

The commands are sent under the LVGL lock so a flush is never mid-flight on
the bus. Wake latency is the 120 ms plus whatever the housekeeping tick adds.
The Settings switch *Dormir el panel apagado* turns this off without a
reflash, in case one revision's panel dislikes it.

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

### 5.7 The gyroscope is off

Steps, orientation and wrist-raise are accelerometer-only, and the gyroscope
is the expensive half of the QMI8658 (about 1 mA at 125 Hz against tens of µA
for the accelerometer). It is enabled only while an app that shows it is open,
through `aos_hal_imu_gyro_request()`; today that is *Actividad*. With it off
`gx/gy/gz` read as 0.

### 5.8 What the battery screen and `/api/status` show

Straight from the PMU: percentage, VBAT, die temperature, board temperature
(the NTC), VBUS, the charger's stage and its programme. Kept by the firmware:
drain in %/h and hours left (nothing until a quarter of an hour and two percent
of drop have gone by, reset on every USB insert), time since unplugged,
lifetime minutes on battery and completed charge cycles (both in NVS), the CPU
clock right now, whether the panel is asleep, whether saving is active, and why
the PMU last powered off.

## 6. What has NOT been done, and why

* **No rail is switched off.** The candidates (DCDC2/3/4, the ALDOs, BLDO2,
  CPUSLDO) are unloaded on paper, and an unloaded regulator costs tens of µA
  each. Against a 60–80 mA active budget that is noise; against a 1 mA
  screen-off budget it is not. The driver can do it (`axp2101_rail_enable`,
  which refuses DCDC1) and the boot log prints the table; the decision waits
  for that table to be read on both board revisions.
* **No light sleep.** It is where the real screen-off savings are — the S3 at
  80 MHz idle still draws ~25 mA — but the QSPI panel driver, the I2S codec,
  the USB-Serial-JTAG console and the touch interrupt all need to be walked
  through it. DFS first; light sleep is the next step and this document is
  where its measurements go.
* **Nothing here has been measured with a meter yet.** The datasheet figures
  are datasheet figures. The way to measure is a USB power meter inline, or the
  battery's own drain figure over a night, before and after each switch.

## 7. Register cheat-sheet (the ones the firmware touches)

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
