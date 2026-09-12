# Roadmap

Work that is planned and has no date. Each item says where things stand,
what is missing and what "done" means, cut into steps that each end in
something that can be checked, the way every other feature here was built.
When a step starts, it goes to the [CHANGELOG](../CHANGELOG.md) as it lands.

## Android phones

**Planned, not scheduled.** Today every phone feature needs an iPhone.

### Where it stands (v0.3.5)

Everything the watch takes from the phone travels over one BLE connection,
and all of it comes from services the iPhone publishes:

| | iPhone | How |
| --- | --- | --- |
| notifications, answering and rejecting calls | yes | ANCS |
| music: controls, title, artist, album | yes | AMS |
| the time, without WiFi | yes | Current Time Service |
| the phone's battery | yes | Battery Service |
| the phone's name | yes | Device Name (0x2A00) |

An Android phone gets none of it, for two reasons:

- **ANCS and AMS are Apple's.** Android publishes no standard equivalent of
  either, and the ESP32-S3 has no Bluetooth Classic, so AVRCP and HFP are
  out as well.
- **The watch does not even ask for what Android could give.** When the link
  is encrypted, `aos_ble.c` looks for ANCS first; the name, the time, the
  battery and AMS are chained after it. With no ANCS the chain stops at
  "the ANCS service did not show up", so a paired Android phone would not
  even be asked its name.

What is already in place for it:

- **The HAL never names ANCS or AMS.** `aos_hal_bt_*`, `aos_hal_notif_*` and
  `aos_hal_media_*` are phone-agnostic, and the notification policy (do not
  disturb, categories, calls always, bursts) lives in `aos_notif.c`, which
  takes whatever a provider pushes. An Android provider lives inside
  `aos_ble` and `aos_hal`: the UI, the internal apps and the `.so` ABI do
  not change.
- **"Controls only" already exists.** `aos_media_info_t.has_metadata = false`
  means commands without track information, and Control BT already has that
  screen.
- **Memory is no longer the constraint it was when BLE arrived.** Since
  v0.3.4 the apps' code runs from PSRAM, and the executable heap has 140 K
  free with 131 K in one block at idle ([RAM-AUDIT.md](RAM-AUDIT.md)). New
  services still get measured with `/api/mem` before and after.

### The shape of the work

With the iPhone the watch is a GATT **client**: it reads services the phone
publishes. Every Android path makes it a GATT **server** as well, with the
phone writing to services the watch publishes. BLE HID is the smallest
useful thing that forces building that side, and what comes after it adds
services to the same server.

### 1. Survey an Android phone

Before AMS was written, the iPhone's GATT tree was walked and read. Same
here: measured, not assumed.

- Pair a real Android phone. Pairing is numeric comparison with LE Secure
  Connections, and the watch drops a phone that asks for anything else.
- Let the discovery chain go on without ANCS: the name, the time and the
  battery do not depend on it. Useful on its own, whatever comes after.
- Walk the phone's tree with `AOS_BLE_DIAG_GATT` (already in `aos_ble.c`,
  off): does it publish Current Time? Battery? That varies by phone, and it
  is what this step answers.
- With a real scanner (nRF Connect, LightBlue). The iPhone's and the Mac's
  Bluetooth settings are filtered lists, and taking them for evidence
  already cost two rounds of debugging.

**Done when** what an Android phone offers is written down, and its name,
and its time and battery if it has them, show up on the watch.

### 2. Music controls over BLE HID

No app on the phone. This was the project's first plan for music, set aside
when AMS turned out to do everything on the iPhone.

- A GATT server in `aos_ble` with the HID service (0x1812) and one Consumer
  Control input report: play/pause 0xCD, next 0xB5, previous 0xB6, volume
  0xE9/0xEA. `aos_media_cmd_t` maps one to one. HID over GATT also expects a
  Battery and a Device Information service beside it; the watch's battery
  is the AXP2101's.
- The pieces exist. ESP-IDF 5.5 has a NimBLE HID device backend (`esp_hid`,
  `nimble_hidd.c`, `CONFIG_BT_NIMBLE_HID_SERVICE`), and
  `examples/bluetooth/esp_hid_device` has a consumer-control report map and
  a NimBLE build. The
  [DOMCOM Remote](https://github.com/charliejgallo/DOMCOM-TouchRemote)
  already sends media keys over BLE HID from an ESP32-C6.
- `aos_hal_media_*` sends a command to AMS when the phone has it and to HID
  otherwise. On HID `has_metadata` is false, and Control BT says there is no
  track information.
- The simulator's imaginary phone always has metadata (`hal_sim.c`); it
  needs a controls-only mode so that screen is designed and layout-audited
  on the Mac, like everything else.

Two decisions come first:

1. **HID always, or only for a phone that is not an iPhone?** The GATT table
   is fixed when the stack starts, so "only for Android" is a setting (or a
   detection plus a restart of the stack), not a per-connection switch. With
   HID always on, the iPhone also sees a HID device: harmless with AMS doing
   the work, but to be checked on the phone.
2. **The advertising packet is full.** It is 31 bytes of 31: flags 3, name
   10 and the ANCS solicitation 18, and iOS needs the name in that packet,
   not in the scan response. The HID UUID does not fit next to both. An
   Android mode can advertise HID in place of the solicitation; a single
   mode has to move something to the scan response and be tried on both
   phones.

**Done when** an Android phone with no app installed controls its music from
Control BT, the screen says there is no track information, an iPhone still
gets AMS with the title and the artist, and turning Bluetooth off still gives
the memory back.

### 3. Notifications on Android

Android has no ANCS, so notifications need an app on the phone that reads
them and sends them over. Two ways, and choosing is the first task:

| | Gadgetbridge | An app of our own |
| --- | --- | --- |
| what it is | an open-source Android app, on F-Droid, that already drives many watches | an Android app written for AmoledOS |
| written on the phone side | nothing, once Gadgetbridge knows the watch | all of it: the notification listener, media, BLE, distribution |
| what comes with it | for InfiniTime (PineTime), per its docs: notifications, answering and declining calls, music control, time | whatever we write |
| protocol | InfiniTime's: the standard Current Time and Alert Notification services, plus its own for music, calls, weather and navigation | our own GATT service |
| cost | a device entry in Gadgetbridge (upstream or a fork), and living with its protocol | an Android project to keep alongside the firmware |

**Gadgetbridge first.** The watch side is GATT services on the server from
step 2, and nothing gets written for the phone. Passing for an InfiniTime so
that Gadgetbridge picks the watch up without a device entry might work, and
would be brittle: not the plan.

- The provider pushes into `aos_notif.c` like ANCS does, so the policy comes
  as it is. Categories and call actions need a mapping; what Gadgetbridge
  actually sends is the first thing to read.
- The track's title and artist on Android come with this step (InfiniTime's
  music service carries them), which is what HID alone cannot give.

**Done when** a notification from an Android phone reaches the full-screen
overlay under the same policy as an iPhone's, and a call can be answered and
declined from the watch.

### With every step: "phone", not "iPhone"

The texts that say iPhone (the portal's Bluetooth footer, the README) become
right for whichever phone is paired, in the three languages, with
`gen_lang.py` and `audit_layout.sh`. It goes with each step, not at the end.

### What has already cost something

- **One 2.4 GHz radio**, shared with WiFi (coexistence is on).
- **One phone at a time** (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`): going from
  an iPhone to an Android is Forget phone and pair again.
- **The controller's instances are not trimmed by eye.** With
  `BT_CTRL_BLE_MAX_ACT=2` the log said "advertising" and nothing reached the
  air. Nobody should trim it again to save 3 KB.
- **One GATT procedure at a time** on the client side: launching them from
  inside each other's callbacks lost two of ANCS's three characteristics.
- **Test with a scanner**, not with the phone's settings.

### Not planned

- **A2DP, AVRCP, HFP**: Bluetooth Classic, which the ESP32-S3 does not have.
- **Location or health data**: neither phone exposes them over GATT; it
  would take a phone app for its own sake.
