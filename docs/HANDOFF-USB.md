# Handoff: the USB port, for whoever builds on it next

> Written 2026-09-13 at the end of the first USB session (branch `usb`,
> 17 commits over `main`, none merged). Everything measured is in
> [USB.md](USB.md); this is the map for continuing the work - a new app
> that uses the port, the merge, or the parked pieces - without re-reading
> the session.

## 1. Where things are

| | |
| --- | --- |
| Worktree | `/Users/charlie/Dropbox/ClaudeCode/ESP32S3_AmoledOS_usb`, branch `usb` on `main` 99ae26f |
| On the watch | the last build of the branch, over OTA; boots on the console; the `en` and `de` packs on the card are the branch's (`tools/install_lang.sh <ip> en de`) |
| Partition table on the watch | has a `coredump` entry at 0xF20000 (256 K), flashed over USB on 2026-09-12; `partitions.csv` of the branch matches; OTA never touches it |
| Board facts | [USB.md](USB.md) section 1: D+/D- on GPIO19/20, VBUS only into the AXP2101 (no 5 V out), one PHY shared by the Serial-JTAG and the OTG, four IN endpoints for classes |

## 2. The four modes, and what each one exposes

| Mode (`aos_hal_usb_mode_t`) | On the computer | Descriptor |
| --- | --- | --- |
| `CONSOLE` (boot default) | the USB-Serial-JTAG: log, `esptool`, `idf.py monitor` | none, the PHY is on the Serial-JTAG |
| `KEYS` ("Teclado y red") | a keyboard + consumer control + mouse + gamepad (one HID interface, report IDs 1-4), a MIDI port, and a CDC-NCM network card: 192.168.7.1, DHCP gives the computer .2, portal and `amoledos.local` over the cable | `s_cfg_device`, PID 0x402C |
| `DISK` | the microSD as a removable drive, a CDC serial port with the console on it; the watch has no card while the computer holds it, ejecting gives it back | `s_cfg_disk`, PID 0x4003 |
| `HOST` | nothing on the computer; a pendrive plugged into the watch (needs 5 V from elsewhere) mounts at `/usb` | host library + MSC class driver |

**The hard limits behind that table** (each cost a night, do not rediscover):

- **Four IN endpoints for classes** (`ep_in_count = 5` with EP0). KEYS uses all four: HID, NCM notification, NCM data, MIDI. Nothing more fits in KEYS; a new class means a new mode or dropping one.
- **Eight string descriptors** (`USB_STRING_DESCRIPTOR_ARRAY_SIZE`): MSC and MIDI borrow the product's.
- **The PHY mux survives a software reset**: `aos_usb_init()` puts it back at boot, with the pad down for 100 ms so the computer sees a detach.
- **Teardown order**: uninstall the stacks first, then hand the PHY back (`phy_back_to_console()`), never the other way: the computer would not see a detach.
- **Never read `USB_DWC` / `USB_WRAP` registers outside an OTG mode**: the peripheral's clock is gated and the read hangs the bus (interrupt watchdog, no panic, no core dump).
- **`tinyusb_console_init/deinit` is unusable on a Serial-JTAG console** (it reopens `/dev/uart/-1` and leaves `stdout` NULL): `aos_usb.c` redirects the streams itself.
- **`usb_host_install()` from the httpd task fails** (no free level-1 interrupt on that core): the host library installs from its own task pinned to core 1.
- **The MAC in the NCM descriptor is the computer's**; the watch's netif uses it with the last bit flipped.
- **The portal counts the USB network as a network** (`main.c`, `network_up`): before that, WiFi off killed the portal on both sides.

## 3. Code map

| File | What |
| --- | --- |
| `components/aos_usb/aos_usb.c` | the switch (`aos_usb_mode_set`), the descriptors, HID (keys, consumer, mouse, gamepad), MIDI, disk mode (the card's hand-over), host mode (inspector + pendrive), the RTC breadcrumbs (`STEP()`), TinyUSB's log ring, `/api/usb`'s JSON |
| `components/aos_usb/aos_usb_net.c` | the NCM ↔ esp_netif glue, the DHCP server, mDNS on the USB interface |
| `components/aos_usb/aos_usb_hal.c` | the `aos_hal_usb_*` functions of `aos_hal.h` (here and not in `aos_hal_esp32.c`, because aos_usb depends on aos_hal for the card and the cycle is not allowed); the mode switch runs in a task of its own so LVGL never waits |
| `components/aos_hal/include/aos_hal.h` | the USB block: modes, keys, typing, mouse, click, gamepad, MIDI, the card's whereabouts; plus `aos_hal_sd_release/reclaim/mark_mounted` and `aos_hal_mdns_add_netif/remove_netif` |
| `sim/hal_sim.c` | the simulator's stubs: switches instantly, keyboard always ready in KEYS, prints what it would send |
| `components/aos_apps/aos_app_pcremote.c` | **Control PC**: the keys pad, the mouse face, the pad face, the MIDI face - the reference for any app that uses the port |
| `components/aos_apps/aos_app_settings.c` | the USB section (dropdown + status line, `usb_refresh()`) |
| `components/aos_web/usb.html`, `aos_web.c` (`usb_handler`, `coredump_handler`) | the `/usb` page and `/api/usb`, `/api/coredump` |
| `main/main.c` | `aos_usb_init()` first thing; the portal's `network_up` |
| `sdkconfig.defaults` | the TinyUSB classes (CDC, MSC 8 KB buffer, HID, MIDI, NCM with one 2 KB NTB each way), `MDNS_PREDEF_NETIF_ETH=n`, core dump to flash, `PANIC_SILENT_REBOOT` |
| `docs/USB.md`, `docs/PORTAL.md`, `README.md` ("The USB port") | the measurements, the API, the pictures (`docs/img/usb-*.png`) |

## 4. Using the port from an app

Everything an app needs is in `aos_hal.h`; nothing in `aos_apps` includes
`aos_usb.h`. The calls, and what they cost:

```c
aos_hal_usb_mode_t aos_hal_usb_mode(void);
bool aos_hal_usb_mode_set(aos_hal_usb_mode_t m);   /* asynchronous: returns at once */
bool aos_hal_usb_busy(void);                       /* true while the switch runs (up to ~4 s out of HOST) */
bool aos_hal_usb_keys_ready(void);                 /* KEYS and the computer configured the device */

bool aos_hal_usb_key(const char *name);   /* "play" "next" "prev" "stop" "mute" "volup" "voldown"
                                           * "brightup" "brightdown" "pgup" "pgdn" "up" "down" "left"
                                           * "right" "enter" "esc" "space" "tab" "home" "end" "b" "f5"
                                           * "delete", or one character; "cmd+" "ctrl+" "alt+" "shift+"
                                           * prefixes stack. Press + release, blocks ~50 ms. */
int  aos_hal_usb_type(const char *ascii); /* a US keyboard; ~50 ms per character */
bool aos_hal_usb_mouse(int dx, int dy, int wheel);   /* one relative report, no waiting; a report
                                                      * in flight means this one is dropped */
bool aos_hal_usb_click(int button);       /* 1 left, 2 right; blocks ~40 ms */
bool aos_hal_usb_gamepad(int x, int y, int hat, unsigned buttons);   /* -127..127, hat 0..8
                                          * (1 up, clockwise), TinyUSB's button bits: A=1<<0 B=1<<1
                                          * X=1<<3 Y=1<<4 L=1<<6 R=1<<7 Select=1<<10 Start=1<<11 */
bool aos_hal_usb_midi_ready(void);
bool aos_hal_usb_midi_note(int note, int velocity, bool on);   /* channel 1 */
bool aos_hal_usb_midi_cc(int control, int value);
bool aos_hal_usb_midi_bend(int value);    /* -8192..8191, 0 centre */
bool aos_hal_usb_card_away(void);         /* DISK and the computer holds the card: no /sdcard */
```

Patterns that work (all in `aos_app_pcremote.c`):

- **Poll `aos_hal_usb_keys_ready()` from a 500 ms `lv_timer`** and show a
  "switch the port to keyboard" button when it is false; call
  `aos_hal_usb_mode_set(AOS_HAL_USB_KEYS)` from it. Do not switch back on
  exit: the user chose the mode.
- **Streams (mouse, stick, bend) from a 20-40 ms `lv_timer`** reading
  `aos_hal_imu_read()`; request the gyro with `aos_hal_imu_gyro_request()`
  while the face is open and release it on the way out. Rates (gyro) for a
  pointer, angles (accelerometer) for a stick or a bend, and a dead band
  in both.
- **Press and release**: `aos_button()` hooks `LV_EVENT_CLICKED`; for a key
  that is held (a pad button, a piano key) remove that callback and hook
  `LV_EVENT_PRESSED`, `LV_EVENT_RELEASED` and `LV_EVENT_PRESS_LOST` - the
  last one is what fires when the runtime's swipe steals the finger.
- **The side button** reaches the app through `aos_app_t.button`
  (`AOS_BUTTON_PRESS / CLICK / LONG`), from the HAL's task with the LVGL
  lock held: note and send, nothing heavier.
- **On `destroy`**: release every key and note, centre the bend, delete the
  timers. `mouse_show(false)` / `midi_show(false)` / `pad_show(false)` do it.
- The panel is single-touch: one key or note at a time, plus the tilt.

**Dynamic (`.so`) apps cannot call any of this yet.** The symbol table
(`tools/gen_symbols.py`, `DEFAULT_LIBS`) exports `aos_hal` but not
`aos_usb`, where `aos_hal_usb_*` live. To let a `.so` use the port: add
`"aos_usb"` to `DEFAULT_LIBS`, `idf.py build` → `python3 tools/gen_symbols.py`
→ `idf.py build` (the two passes of [BUILDING.md](BUILDING.md)), check the
count went up, and verify the app with `xtensa-esp-elf-nm -D -u app.so`
against the table. Adding symbols keeps `AOS_ABI_VERSION` and every
existing `.so`.

## 5. Testing without touching the watch

| Want | How |
| --- | --- |
| switch modes, press keys, move the pointer, play a note, a gamepad report | `/api/usb?mode=…`, `?key=a,b`, `?type=text`, `?mouse=dx,dy`, `?click=1`, `?midi=60,100`, `?pad=x,y,hat,buttons` |
| open an app and see the screen | `POST /api/accion que=abrir&id=aos.pcremote`, `GET /api/captura` (BMP; `sips -s format png`) |
| tap or drag on the screen | `/api/mem?tap=x,y[,ms[,x2,y2]]` (root of the app starts ~30 px below the top; the touch ceiling is y 390) |
| what the Mac enumerated | `ioreg -p IOUSB -w0 -r -n "AmoledOS watch"` (a `!registered` node up to ~30 s is normal), `ioreg -c IOHIDDevice -l` for the HID usages, `ifconfig` for the NCM interface, `dns-sd -G v4 amoledos.local` for mDNS per interface (`dig` cannot pick the interface) |
| the Mac's volume / cursor | `osascript -e 'output volume of (get volume settings)'`; `osascript -l JavaScript -e 'ObjC.import("Cocoa"); $.NSEvent.mouseLocation'` |
| MIDI arriving | a venv with `mido` + `python-rtmidi`, `mido.open_input('AmoledOS watch')` |
| raw HID reports | not from a script: macOS wants the Input Monitoring permission (`hidapi` opens fail); use `https://hardwaretester.com/gamepad` in a browser for the pad |
| a portal page in the in-app browser | it only opens localhost: a 20-line `http.server` proxy to the watch's IP (the session's `proxy.py`); local HTML files render as static snapshots, no JavaScript |
| a panic in an OTG mode | `/api/coredump` (ELF) and `esp-coredump info_corefile --core core.elf --core-format elf build/amoledos.elf`; `/api/usb` `boot_step` says the last breadcrumb before a reset that left no dump; `/api/usb?tusblog=1` is TinyUSB's own log (level 1: asserts) |
| new strings | `gen_lang.py template en` / `de`, translate in `sim/sim_fs/lang/<code>/_sistema.lang` (gitignored, copied from the main repo), `gen_lang.py embed en de`, and **`install_lang.sh <ip> en de`**: the card's packs beat the embedded ones |

## 6. Ideas that fit what is there

Cheap, on the KEYS composite, each an afternoon in `aos_apps` or as a `.so`
once section 4's symbol step is done:

- A **presentation remote** with a timer and a "black screen" that remembers
  the slide count; the clicker keys exist.
- **Typed snippets**: a list of texts on the card (`/sdcard/snippets.txt`),
  tap one and the watch types it into the computer (`aos_hal_usb_type`).
  Passwords are a bad idea over a keyboard that any USB host can read.
- **A volume / scrub knob**: the wrist's roll as consumer volume or as a
  MIDI CC, with the bend switch's code.
- **A drum pad or step sequencer** on MIDI out, using the tone generator for
  the click track.
- **Shortcuts for the Mac**: a grid the user edits in the portal (`/usb`
  already has the key names), each cell a `cmd+…` combination.
- **The watch's own games with a real controller**: HOST mode with a HID
  gamepad (`usb_host_hid` is in, `H4` in USB.md) - needs the 5 V rig.
- **D8, the screen as a webcam** (`usb_device_uvc`): its own mode, since KEYS
  has no endpoint left; software JPEG of 368x448 at 3-5 fps.

## 7. Before the merge

- **All three uses are checked** (2026-09-13/14): Bluetooth connected in
  KEYS for 8 hours (executable heap never below 107 K), light sleep on
  battery in CONSOLE, and disk cycles with big files: 636 MB of MP3s in one
  drag, every file back with its size and a 14.9 MB one with the original's
  MD5 (USB.md, T5). Mind the eject: a forced unmount on the Mac does not
  send the SCSI eject, `diskutil eject /dev/diskN` afterwards does. **Light sleep on battery in
  CONSOLE is checked**: the user's log of 2026-09-13 17:31 shows "light
  sleep on" at the unplug, the display off, and the touch reads dropping
  from ~90 to ~15 per heartbeat, which is the chip sleeping between them.
- `sdkconfig` of `main` must be deleted before the first build there (the
  defaults changed; `idf.py fullclean` does not remove it).
- Decide `CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT` for `main`: the branch
  chose silent + core dump to flash because a panic in an OTG mode has no
  console; for everyday use `PRINT_REBOOT` + core dump gives both.
- Static RAM: the USB classes take 16 KB of internal RAM whether the port
  is used or not (8 KB is the MSC buffer, the price of 767 KB/s in disk
  mode; 4 KB the NCM buffers). If the RAM audit's numbers matter more than
  disk speed, `CONFIG_TINYUSB_MSC_BUFSIZE=4096` is the knob.
- `docs/USB.md` "Where host mode stands" says why host is parked and what
  rig would unpark it.
