#!/bin/zsh
#
# Patches the local ESP-IDF with the fixes this project needs and upstream
# has not shipped. Idempotent: says so if a patch is already in.
#
#   ./tools/idf-patches/apply.sh            uses $IDF_PATH or ~/esp/esp-idf
#   ./tools/idf-patches/apply.sh --check    only reports
#
# 0001  spi_bus_lock.c: `acquiring_dev` read once in bg_exit_core()
#       (espressif/esp-idf#18527, open since 2026-04-28). Without it the SPI
#       ISR of the panel dereferences NULL now and then when another core
#       clears the field between two reads: LoadProhibited at 0x0 in
#       resume_dev_in_isr(), seen on the watch with the radio up and the panel
#       flushing at 30 fps (docs/internal/HANDOFF-SPI-WIFI-NUCLEOS.md).
#
set -e
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
HERE=${0:a:h}
CHECK=0; [ "$1" = "--check" ] && CHECK=1
for p in $HERE/*.patch; do
    name=$(basename $p)
    # forward first: `patch -R --dry-run` on an unpatched file says "reversed
    # patch detected, assume -R" and exits 0, which read as "already in"
    if (cd $IDF && patch -p1 -N --dry-run -s -f < $p > /dev/null 2>&1); then
        if [ $CHECK = 1 ]; then echo "  MISSING $name"; else (cd $IDF && patch -p1 -N -s -f < $p) && echo "  applied $name"; fi
    elif (cd $IDF && patch -p1 -R -N --dry-run -s -f < $p > /dev/null 2>&1); then
        echo "  in     $name"
    else
        echo "  ?? $name does not apply cleanly to $IDF (different IDF version?)"
    fi
done
