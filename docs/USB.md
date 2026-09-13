# USB: what the connector can become, and in which order

> **State of this document (2026-09-12).** Theory phase, on the `usb` branch
> (worktree `ESP32S3_AmoledOS_usb/`, nothing merged). Sections 1 and 2 come
> from the board's schematic and ESP-IDF's sources; nothing in section 3 has
> run on the board yet. The dependency graph of section 5 resolves with
> ESP-IDF v5.5 and the firmware builds with the USB components present.
> Phase 1 (the switch, `components/aos_usb`, `/api/usb`) runs on the board:
> T1, T2 and T3 are measured (section 6). T4 onwards wait for a host rig
> (section 4).

Today the USB-C port does one thing: it is the console and the flashing port,
through the ESP32-S3's USB-Serial-JTAG. The chip has a second USB controller,
a full OTG one, that can make the watch a **device** for a computer (a disk, a
keyboard, a network card, a serial port) or a **host** for a peripheral (a
pendrive, a keyboard, a gamepad, a webcam). This document is the survey of
what that allows on this board, what it costs, and the plan to get the most of
it into the firmware.

## 1. What the board has (from the schematic)

Read from `ESP32-S3-Touch-AMOLED-1.8.pdf` (Waveshare's schematic, one page),
block "Power Supply" and the ESP32-S3 sheet:

* **H1, the USB-C receptacle.** `D+`/`D-` (pins A6/A7 and B6/B7 paralleled)
  go **straight to GPIO20 / GPIO19** as `USB_P` / `USB_N`, through a
  `LTVS16H5.0ET5G` TVS for ESD. No UART bridge: the port *is* the chip's USB.
* **`CC1` and `CC2` each have a 5.1 K pull-down** (`R1`, `R2`) to ground. In
  USB-C terms the board declares itself a **device** (UFP, "Rd"): a USB-C
  source sees the pull-downs and supplies 5 V. It never asks anything of a
  peripheral, because it cannot supply 5 V to one.
* **`VBUS` goes to the AXP2101's `VBUS` input (pin 37) and nowhere else.** It
  is the charger's input. The PMU has no boost converter, and there is no
  other 5 V rail on the board. **The board cannot power a USB peripheral**;
  in host mode the 5 V has to come from outside (section 4).
* **A second copy of the data lines on solder pads.** `USB_N`/`USB_P` also go
  through `R19`/`R20` (22 Ω) to `USB'_N`/`USB'_P`, two pads next to the
  `GPIO38/39/40` test points — the "1 USB solder pad" in Waveshare's
  documentation. Same PHY, same signals; only one of the two connectors may be
  used at a time.
* **The PMU knows when VBUS is there** (`AXP2101_IRQ_VBUS_INSERT/REMOVE`, and
  `usb_present` in `aos_board_pmu_read()`), which the firmware already uses
  for "USB connected" and to arm light sleep on battery only
  ([POWER.md](POWER.md)).

And in the chip (`soc_caps.h`, `usb_wrap_ll.h`, `usb_serial_jtag_ll.h`):

* **One USB OTG controller** (`SOC_USB_OTG_PERIPH_NUM = 1`), **full speed
  only**: 12 Mb/s on the wire, which is about 1 MB/s of bulk payload on a good
  day and 0.5 MB/s as Espressif measures it for isochronous streams.
* **One USB-Serial-JTAG controller**, the console today
  (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`).
* **One internal PHY for both** (`SOC_USB_FSLS_PHY_NUM = 1`). Which controller
  owns it is a register mux, `RTC_CNTL.usb_conf.sw_usb_phy_sel`: `0` = the
  PHY belongs to the Serial-JTAG, `1` = to the OTG controller. `usb_new_phy()`
  flips it to OTG (`usb_wrap_ll_phy_enable_external(false)`), and the
  Serial-JTAG driver flips it back (`usb_serial_jtag_ll_phy_enable_external(false)`).
  Nothing is burnt in eFuse: the choice is made at run time and survives
  nothing — every reset boots with the PHY on Serial-JTAG.
* The ROM's download mode uses the Serial-JTAG side. The OTG side has a DFU
  and a CDC in ROM too, but only with the `USB_PHY_SEL` eFuse burnt, which is
  irreversible and would take the current flashing path away. Not planned.

## 2. The constraints that decide the design

1. **One PHY, so one mode at a time.** The firmware will have exactly three
   USB states: **console** (Serial-JTAG, what it is today), **device** (OTG
   as a peripheral of a computer) and **host** (OTG driving a peripheral).
   Switching is a register plus the driver's install/uninstall, so it can be a
   setting and does not need a reboot — that claim is test T2 in section 6.
   Whatever the state, a reset brings the console back, and OTA over WiFi
   keeps working: **the console is the boot default and USB modes are opted
   into**, never the other way round.
2. **In the two OTG modes the Serial-JTAG is gone.** The log stops reaching
   `idf.py monitor`; the DTR/RTS reset that `tools/install_apps.sh` sends
   over `/dev/cu.usbmodem*` does nothing; `esptool` needs BOOT held. The
   portal's `/registro` and OTA over WiFi are the working paths, and they
   already are the everyday ones ([amoledos-deploy](PORTAL.md)). In device
   mode a CDC port can carry the log instead (D1).
3. **Host mode needs 5 V from somewhere else.** Section 4 has three ways.
   There is no VBUS sensing in host mode on the S3; the host just drives
   D+/D-, so a peripheral powered from a third party works.
4. **Full speed.** A pendrive will move about 1 MB/s at best (the card itself,
   1-bit SDMMC, is faster than that, so USB is the ceiling). A webcam gets
   ~0.5 MB/s: 320x240 MJPEG at 30 fps or 640x480 at 15 fps is what cameras
   offer at full speed, and many refuse to stream at all when they see a
   full-speed host. Audio over USB fits (a stereo 48 kHz stream is 192 kB/s)
   but leaves little else.
5. **RAM.** Transfer buffers must live in internal, DMA-capable RAM: the host
   library's URBs, TinyUSB's endpoint buffers, the MSC sector buffer. The
   executable heap sits at ~137 K free with 125 K in one block
   ([RAM-AUDIT.md](RAM-AUDIT.md)); every stack goes on **on demand and off
   again**, never at boot, and each one gets a before/after with `/api/mem`.
   Frame buffers (webcam) and file buffers go to PSRAM.
6. **The card has one owner.** Disk mode (D2) hands the card's raw sectors to
   the computer; the FAT filesystem must be unmounted on the watch meanwhile
   or both sides corrupt it. While the computer has the card: no photos, no
   music, no recordings, no new apps — the `.so` already loaded keep running
   (their code and data are in PSRAM). On eject the card is mounted again and
   `aos_dynapp_scan()` runs again, because the computer may have added or
   removed apps.
7. **Light sleep and power.** With the cable in a computer the PMU sees VBUS
   and light sleep is off, as today. On the solder-pad rig (4.C) there is no
   VBUS, the firmware believes it is on battery, and with the screen off it
   would light-sleep and take the USB host down with it. **Any OTG mode holds
   a `NO_LIGHT_SLEEP` lock**, like BLE does. Host mode also costs the OTG
   controller's own consumption (unmeasured; section 6).
8. **The apps.** Anything an app should reach (a gamepad, a keyboard, a
   file on a pendrive) goes through `aos_hal.h`, because the `.so` apps only
   see the HAL and LVGL ([APP-API.md](APP-API.md)). New HAL functions mean a
   new symbol table and an ABI note; the pattern is the microphone's
   (`aos_hal_mic_*`).

## 3. The catalogue

Every function the two modes allow on this board, with what it needs and what
it is worth. *Stack* is the component that does the heavy lifting; the
versions that resolved with ESP-IDF v5.5 are in section 5. *Cost* is code and
integration effort, not RAM (RAM gets measured). *Value* is for this watch,
on this board, with this user's setup — it is an opinion, and it drives the
order in section 7.

### 3.1 Device mode — the watch plugged into a computer

| # | Function | What it gives | Stack | Cost | Value |
| --- | --- | --- | --- | --- | --- |
| D1 | **Serial console over CDC** | The log and a serial port again while OTG owns the PHY. Foundation for every other device function: without it, device mode is blind. `tinyusb_console` routes stdout to the CDC port. | esp_tinyusb CDC-ACM | low | needed |
| D2 | **Disk mode: the card as a USB drive** | The microSD appears on the Mac as a removable disk. Drag photos, GIFs, recordings and music out; drop `.so` apps, language packs and music in — at cable speed, no WiFi, no portal. Faster than `/api/upload`, and it is how people expect a gadget with a card to behave. | esp_tinyusb MSC (`tinyusb_msc_new_storage_sdmmc`) | medium (card ownership, section 2.6) | high |
| D3 | **HID keyboard + consumer control** | The watch as a remote for the computer: play/pause, volume, next track, mute; a presentation clicker (next/previous slide, black screen); a macro pad of eight big buttons on the AMOLED. | esp_tinyusb HID | low | high |
| D4 | **HID mouse / trackpad** | The IMU tilts the pointer (`aos_hal_imu_*` already gives the angles) or the glass is a trackpad; the side button clicks. | esp_tinyusb HID | low | medium |
| D5 | **HID gamepad** | Tilt and touch as a game controller for the computer. Same descriptor work as D3/D4. | esp_tinyusb HID | low | low |
| D6 | **USB network (NCM for macOS/Linux, RNDIS for Windows)** | The watch is a network interface: the portal at `http://192.168.7.1`, `/api/mem`, `/registro`, OTA, all over the cable, with no WiFi at all. Also the road to "internet through the computer" for the watch (IP forwarding on the Mac) — a later step. | esp_tinyusb NET + `esp_netif` | medium | high for development |
| D7 | **MIDI controller** | Touch pads and tilt as MIDI notes and control changes into GarageBand / Ableton. The tuner app already knows the note table. | esp_tinyusb MIDI | low | fun |
| D8 | **Webcam (UVC device): the screen as a camera** | The watch shows up as a webcam whose picture is its own screen: a live capture of the UI into OBS, Zoom, QuickTime, with no `captura.py`. Software JPEG of a 368x448 frame: 3-5 fps expected, CPU-heavy. | `usb_device_uvc` + `esp_jpeg` (encoder) | medium-high | novelty, useful for demos |
| D9 | **USB audio (UAC2): speaker and microphone for the computer** | The ES8311 as the Mac's sound card. Isochronous timing and clock drift with no Espressif wrapper: raw TinyUSB. | tinyusb UAC2 | high | low |
| D10 | **DFU: firmware over `dfu-util`** | An alternative to OTA. Runtime DFU writing the OTA partition is doable; ROM DFU needs the irreversible eFuse. OTA over WiFi is already better. | esp_tinyusb DFU | medium | low |
| D11 | **WebUSB / vendor class** | A Chrome page talks to the watch directly (a web-based "Pixel Art" that pushes frames over the cable). Chrome only. | tinyusb vendor | medium | low |
| D12 | **Composite device** | D1 + D2 + D3 at once (a serial port, a disk and a keyboard in one plug), which TinyUSB composes from the enabled classes. It is a descriptor, not a feature, but it decides that the modes above are not exclusive among themselves. | esp_tinyusb | low | comes with D1-D3 |

### 3.2 Host mode — a peripheral plugged into the watch

| # | Function | What it gives | Stack | Cost | Value |
| --- | --- | --- | --- | --- | --- |
| H0 | **USB inspector** | An app (and `/api/usb`) that lists whatever is plugged in: VID/PID, class, strings, endpoints, speed. The first thing to run on the rig, and a permanent diagnostic. | `usb_host` (IDF) | low | needed |
| H1 | **Pendrive (MSC)** | A stick mounted at `/usb`: browse it on the watch, copy files to and from the card (apps, music, photos, recordings, language packs), back the whole card up to the stick, install apps from a stick with no computer. | `usb_host_msc` + its VFS | medium (a file-copy UI) | high |
| H2 | **Keyboard** | Type where the watch today has no way to type: WiFi passwords, names in Pixel Art, notes. Arrow keys and Enter/Esc drive the launcher. Boot-protocol keyboard, no descriptor parsing. Needs a text-entry hook in the UI. | `usb_host_hid` | low stack, medium UI | medium |
| H3 | **Mouse** | A pointer on the AMOLED; a click becomes a touch through the same injection path `/api/mem?tap=` uses (`aos_ui_inject_tap`). Boot-protocol mouse. | `usb_host_hid` | low | medium |
| H4 | **Gamepad** | 2043, Claude Jump, Chatarra, Arkanos with real buttons. Generic HID gamepads send raw reports, so the report descriptor has to be parsed (axes, buttons, hat) — the component does not do it; boot protocol does not cover gamepads. Xbox pads are XInput (vendor), out; DirectInput / Switch-mode pads (8BitDo, cheap SNES clones) are HID, in. Exposed as `aos_hal_gamepad_*` so the `.so` apps get it. | `usb_host_hid` + own parser | medium | high for the games |
| H5 | **Webcam (UVC)** | A viewfinder on the screen and a shutter: the JPEG the camera sends is saved as-is to `/photos` (no encoder needed); the Photos app shows it. Decoding MJPEG to the screen with `esp_jpeg` (tjpgd) at 320x240: ~10 fps expected, to measure. Time-lapse for free. **Risk: camera compatibility at full speed** — the tested list is short (Logitech C270/C170/Brio 100, Anker C200, Trust). | `usb_host_uvc` + `esp_jpeg` (decoder) | medium | high, if a camera cooperates |
| H6 | **Serial adapters and other boards (CDC-ACM, CP210x, CH34x, FTDI)** | The watch as a serial terminal for another ESP32/Arduino, or as a **control panel for the bench**: the Riden RD6012 speaks Modbus RTU over a USB-serial, the UNI-T generator USBTMC (USBTMC would be a class driver of our own, bulk in/out). | `usb_host_cdc_acm`, `usb_host_*_vcp` | low-medium | medium, niche |
| H7 | **USB audio (UAC host)** | A USB headset or speaker for the music player; a USB mic for the recorder. Isochronous at full speed; Espressif has a driver. | `usb_host_uac` | medium | low-medium |
| H8 | **Hub** | Keyboard *and* pendrive at once, through a powered hub — which also solves the 5 V. `CONFIG_USB_HOST_HUBS_SUPPORTED` exists in v5.5 and is off. | `usb_host` | low (a Kconfig) | comes with 4.B |
| H9 | **Thermal receipt printer** | Print a photo or a Pixel Art drawing on a USB ESC/POS printer: printer class is bulk out plus a few control requests. | own class driver | medium | fun |
| H10 | **MIDI keyboard** | A piano keyboard playing the watch's tone generator. USB-MIDI host is bulk in with 4-byte packets; no component. | own class driver | medium | low |
| H11 | **USB Ethernet adapter** | No component for CDC-ECM/RTL8152 on the host side; a driver of our own is a project. Not planned. | — | high | low |
| H12 | **A phone as a peripheral** | iPhone needs iAP2 (licensed); Android exposes MTP. Neither is worth it: the phone link is BLE and stays there. Not planned. | — | — | — |

### 3.3 What is not on the list, and why

* **Two things at once across modes** (a disk for the Mac *and* a keyboard
  plugged into the watch): one PHY, one controller, no. Composite (D12) is
  several device classes on one cable, which is the only "at once" there is.
* **High speed, USB 3, video capture of the computer's screen:** full-speed
  hardware.
* **Charging a phone from the watch:** no 5 V source (section 1).

## 4. Test rigs

**Device mode:** the Mac and the cable that is already there. Nothing else.
`system_profiler SPUSBDataType` and `ioreg -p IOUSB -l` show what the Mac
enumerated; `diskutil list` shows the card in disk mode.

**Host mode** needs 5 V for the peripheral. Three options, from the one to buy
to the one to solder:

* **4.A — a USB-C "OTG + charging" Y-splitter.** A small adapter with a
  USB-C plug (into the watch), a USB-A socket (the peripheral) and a USB-C
  socket for a charger. The charger's 5 V reaches both the peripheral and the
  watch's VBUS — **the PMU charges the battery while the watch hosts**, and
  `usb_present` stays true so light sleep stays off. D+/D- pass straight
  through. Buy the *passive* kind: adapters that negotiate USB-PD may withhold
  the 5 V because the watch's CC pins carry the pull-downs of a device, not
  the pull-ups of a host. This is the recommended everyday rig.
* **4.B — a self-powered hub through a USB-C-to-A OTG adapter.** Needs H8
  (hubs support) on. Some hubs wait for VBUS from the host before they
  connect upstream, and this rig gives them none: it may or may not work per
  hub. Worth one test because a hub is what people have in the drawer.
* **4.C — the solder pads.** `USB'_P`, `USB'_N` (22 Ω in series, fine at full
  speed) and a ground to a USB-A female breakout board; the breakout's VBUS
  from a bench supply or a power bank. Electrically the cleanest, and the
  only rig where the PMU sees no VBUS (section 2.7). It means soldering two
  wires to the board.

For every host test the peripheral is powered before or at the same time as
the mode is switched on; the S3 has no VBUS detection to wait on.

## 5. What resolves with ESP-IDF v5.5

Added to `main/idf_component.yml` on the branch, resolved on 2026-09-12:

| Component | Version | For |
| --- | --- | --- |
| `espressif/esp_tinyusb` | 2.2.1 (pulls `espressif/tinyusb` 0.21.0~1) | every device function (D1-D12) |
| `espressif/usb_host_msc` | 1.2.0 | H1 |
| `espressif/usb_host_hid` | 1.2.1 | H2-H4 |
| `espressif/usb_host_cdc_acm` | 2.4.1 | H6 |
| `espressif/usb_host_uvc` | 2.5.2 | H5 |

Available but not added yet: `usb_host_uac` 1.5.0 (H7), `usb_device_uvc`
1.3.1 (D8), `esp_jpeg` 1.3.1 (D8/H5), `usb_host_ch34x_vcp` 2.2.1,
`usb_host_cp210x_vcp` 2.2.0, `usb_host_ftdi_vcp` 2.1.1 (H6). The host
library itself is IDF's `usb` component; `CONFIG_USB_OTG_SUPPORTED=y` is
already in the config and the TinyUSB classes are all off
(`CONFIG_TINYUSB_*_ENABLED`), which is why the firmware's size does not move
until a function is wired in.

The USB-C port stays the console at boot in every build of this branch.

## 6. Measurements the plan depends on

Each one has a test number; results go here, with the date, as they come.

| Test | Question | How |
| --- | --- | --- |
| T1 | **Device mode comes up and the Mac sees it.** | `/api/usb?mode=device`; `system_profiler SPUSBDataType` shows an Espressif device with a CDC port; `screen /dev/cu.usbmodem*` shows the log. |
| T2 | **The PHY goes back to the console without a reboot.** | `/api/usb?mode=console`; the `usbmodem` of the Serial-JTAG reappears and `idf.py monitor` works. If it does not, the answer is "USB modes end at the next reboot", which is acceptable and gets documented. |
| T3 | **RAM of each stack.** | `/api/mem` before, in mode, and after going back: internal, exec, largest block. What does not come back is the fragmentation figure to write down. |
| T4 | **Host mode enumerates on rig 4.A.** | A pendrive on the Y-splitter; `/api/usb` lists VID/PID and class; the log has the descriptors. Then a keyboard, then a hub (4.B). |
| T5 | **Disk mode throughput and card hand-back.** | Copy a 50 MB file each way and time it; eject; `/api/apps` still reports 18 + 23 and the photos app opens. |
| T6 | **A camera streams at full speed.** | Which of the cameras in the house negotiate 320x240 MJPEG; frames per second decoded to the screen. |
| T7 | **What host mode costs the battery.** | `/api/pmu` drain over an hour with a keyboard plugged in versus none, screen off. |
| T8 | **Light sleep with USB on.** | Rig 4.C, screen off, five minutes: the host must stay up (the lock of section 2.7). |

### Results (2026-09-12, v0.3.5-3-g99ae26f-dirty on the branch, board v2, over OTA)

**T1 — passes.** `?mode=device` and four seconds later `ioreg` has
"AmoledOS watch" (VID 303a, the default PID) and the Mac creates
`/dev/cu.usbmodem1234561` (the default serial string, "123456"). Reading it
with pyserial shows the heartbeat lines: the console is on the CDC port. The
Serial-JTAG's `usbmodem1101` is gone meanwhile, as expected.

**T2 — passes, after two bugs.** `?mode=console` brings `usbmodem1101` back
**0.6 s** later, the log flows over it, no reboot. The two things that had
to be fixed to get there, both measured on the board:

1. **`tinyusb_console_deinit()` panics on this firmware.** esp_tinyusb 2.2.1
   restores the streams by reopening `/dev/uart/<CONFIG_ESP_CONSOLE_UART_NUM>`;
   with the console on the Serial-JTAG that number is **-1**, `freopen()`
   fails, the function stores the NULL in `stdout`, and the next log line
   from any task is a `PANIC` (boot reason after the first T2 attempt).
   `aos_usb` does its own redirect: `esp_vfs_tusb_cdc_register()` plus
   `freopen()` on `/dev/tusb_cdc`, and back on **`/dev/console`**, which is
   the real console whatever it sits on; each path is `open()`-tested first
   because a failed `freopen()` leaves the stream unusable.
2. **Flipping the mux with the pad enabled is not a detach.** Giving the PHY
   back to the Serial-JTAG with `usb_serial_jtag_ll_phy_enable_external(false)`
   and the pad on keeps D+ high through the change: the Mac never sees the
   TinyUSB device go, keeps "AmoledOS watch" in `ioreg`, and the Serial-JTAG
   does not enumerate until the cable is pulled. The fix is the pad down for
   100 ms around the flip: a detach, then a fresh attach.

**T3 — measured.** Free heap in bytes, from the log of the switches (the
figures `/api/usb` reports as `mem`):

| | internal | exec | largest exec block |
| --- | --- | --- | --- |
| console, fresh boot | 179 871 | 131 872 | 122 880 |
| **device** (TinyUSB + CDC + console) | 175 331 | 127 332 | 114 688 |
| back on the console | 180 115 | 132 116 | 118 784 |
| **host** (library + inspector, no device plugged) | 168 151 | 120 152 | 110 592 |
| back on the console | 179 731 | 131 732 | 118 784 |

So device mode costs **4.5 KB** and host mode **11.6 KB** of internal RAM
while on, both from the executable pool, and both come back in full; the
largest block loses **4 KB** after the first cycle of either and then holds.
Nothing measured yet with a device enumerated (its URBs and the class
driver's task come on top).

**Also found:** `usb_host_install()` returns `ESP_ERR_NOT_FOUND` when called
from the portal's httpd task ("Interrupt alloc error": no free level-1
interrupt on that core), while TinyUSB installs fine because it does so from
its own task on core 1. The host library is now installed from `aos_usb`'s
own library task, pinned to core 1, with a fallback to any low/medium level.

**T4-T8:** not run; they need a host rig (section 4).

## 7. The plan, in phases

Each phase ends in something that runs on the board and is checked, the way
the BLE fork and the RAM audit were built. Nothing merges to `main` until
phase 4.

### Phase 0 — theory (this document)

Done: the schematic read, the constraints listed, the catalogue rated, the
dependencies resolved, the firmware built with them present.

### Phase 1 — the switch, measured

**Running on the board since 2026-09-12** (T1-T3 in section 6). Flash:
3 245 520 B against 3 176 048 B with the stacks present but unused, so
TinyUSB with one CDC port plus the host library and the inspector cost
**69 KB of flash**.

A component `aos_usb` with one job: own the PHY. Three states —
`AOS_USB_CONSOLE` (boot default), `AOS_USB_DEVICE`, `AOS_USB_HOST` — and a
setter that installs and uninstalls the stacks. In device mode it brings D1
(CDC console) and nothing else; in host mode H0 (the inspector, in the log
and in `/api/usb`). `/api/usb` switches modes from the portal so the first
tests need no UI. Tests T1-T4.

**Done when** each mode comes up and goes away from the portal, T2 has an
answer, and T3's numbers are in this document. *Done except T4*, which is
host mode with something plugged in and needs the rig.

### Phase 2 — device functions

In this order, each behind the same switch and each measured with T3:

1. **D2 disk mode.** The card's owner moves: `aos_usb` takes the card from
   the BSP mount (`esp_vfs_fat_sdcard_unmount` keeps the `sdmmc_card_t`
   only if we mount it ourselves, so the mount moves from `bsp_sdcard_mount()`
   into `aos_hal`), hands it to `tinyusb_msc_new_storage_sdmmc`, and on eject
   mounts it back and rescans the apps. T5.
2. **D3 HID keyboard + consumer control** as an app: media keys, clicker,
   macro pad. Composite with D1 (D12).
3. **D6 USB network (NCM).** The portal over the cable.
4. **D4/D5/D7** if D3 left the descriptor machinery in place: an afternoon
   each.

**Done when** disk mode survives ten mount/eject cycles with the apps intact,
the media keys work on the Mac, and the portal answers at `192.168.7.1` with
WiFi off.

### Phase 3 — host functions

1. **H1 pendrive**: mount, browse, copy both ways, backup. The file list UI
   already exists in the portal; the watch gets a two-pane copy screen.
2. **H2/H3 keyboard and mouse**: text entry hook, launcher navigation, pointer.
3. **H4 gamepad**: the report-descriptor parser, `aos_hal_gamepad_*`, and one
   game (2043) reading it.
4. **H5 webcam**: the inspector says which camera streams; then the
   viewfinder app with shutter to `/photos`. T6.
5. **H6 serial adapters**: a terminal app; the Riden panel if it is still
   wanted.
6. **H8 hubs** on, T4 with rig 4.B.

**Done when** each device in the house that should work does, the ones that
do not are in this document with why, and T7/T8 are measured.

### Phase 4 — into the firmware

* Settings → USB: the mode, what is plugged in, disk-mode eject.
* The portal: `/usb` page, `/api/usb` documented in [PORTAL.md](PORTAL.md).
* [HARDWARE.md](HARDWARE.md) gets the connector's facts (section 1);
  [APP-API.md](APP-API.md) the new HAL calls; the README a paragraph and the
  docs table a row; the CHANGELOG each step as it lands.
* Three languages for every new string (`gen_lang.py`, `audit_layout.sh`).
* Merge by fast-forward after a week of daily use, like the RAM audit.

### What is deliberately last or out

D8 (screen as webcam), D9 (USB audio device), D10 (DFU), D11 (WebUSB), H7
(USB audio host), H9/H10 (printer, MIDI host) are fun or niche and wait for
everything above; H11/H12 are not planned.
