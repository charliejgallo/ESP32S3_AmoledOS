# Tests for the .so loader

Apps that check the loader itself, kept out of `apps/` so they are not built,
packed or installed with the real ones. To run one on a board:

```bash
cp -r tools/so_tests/globtest apps/
./tools/build_apps.sh globtest
curl -X POST "http://<ip>/api/upload?dir=apps&name=globtest.so" --data-binary @apps/globtest/build/globtest.so
curl -X POST http://<ip>/api/ota/restart
curl -s http://<ip>/api/log | grep globtest       # "GLOB_DAT check OK"
curl -X POST "http://<ip>/api/delete?dir=apps&name=globtest.so"
rm -rf apps/globtest
```

| Test | What it checks |
| --- | --- |
| `globtest` | `R_XTENSA_GLOB_DAT` with an addend: a loop over a global table defined in another file makes GCC put `table + 24` in the GOT. Until v0.4.9 the loader wrote `table + 0` (`components/elf_loader/src/arch/esp_elf_xtensa.c`). It never registers: the check runs in `init()` at boot and logs OK or FAILED. |
