# RAM audit tools

Scripts used for `docs/RAM-AUDIT.md`. They talk to the board over WiFi (the
audit build's `/api/mem`) and, for a build that may not survive its trial
boot, capture the USB serial port. Set `AOS_AUDIT_DIR` to a scratch directory
(default `/tmp/aos_audit`) and run them from the repository root.

- `snap.sh <label> [ip]` — saves `/api/mem`, `/api/status` and `/api/log` under `$AOS_AUDIT_DIR/runs/<label>/`.
- `symbolize.py <mem.txt> <elf>` — resolves the call sites of `/api/mem` with `xtensa-esp32s3-elf-addr2line` and ranks internal bytes by owner function.
- `memsum.py <mem.txt>...` — one summary per snapshot: heaps, exec free, stacks, traced bytes.
- `serial_capture.py <port> <out> <seconds>` — reads the serial port to a file, reconnecting across the resets an OTA causes.
- `try.sh <label> [build-dir]` — OTA a build with the serial captured and say whether it booted and confirmed.
- `round3.sh <prefix>` — OTA and snapshot every state (idle, apps, launcher, Bluetooth off) plus the render A/B.
- `bench.sh <label> [lvpsram]` — the per-screen render benchmark (`/api/mem?bench=1`).

Audit build switches (CMake variables, e.g. `idf.py -DAOS_AUDIT_LVGL_PSRAM=1 -DAOS_AUDIT_LVGL_STACK16=1 build`):
`AOS_AUDIT_LVGL_PSRAM` (LVGL allocations to PSRAM), `AOS_AUDIT_LVGL_STACK16`
(LVGL task stack 16 K), `AOS_AUDIT_PSRAM_STACKS` (the tone task's stack in
PSRAM), `AOS_AUDIT_PSRAM_BSS` (our components' `.bss` to PSRAM).
- `fpsrun.sh <label> <app-id> [tap "x,y,ms"...]` — open an app, inject taps, capture the screen, count rendered frames for 5 s (`/api/mem?fps=5`).
- `fpsround.sh <label> <build-dir>` — OTA a build with the serial captured, then the three games' FPS twice.
