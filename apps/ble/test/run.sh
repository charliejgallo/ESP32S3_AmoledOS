#!/bin/sh
# BLE - builds and runs the decoder's tests on the Mac, under ASan and UBSan:
# packets written by hand from each format's documentation, the simulator's
# whole neighbourhood (sim/ble_sim.c, through shim/aos_hal.h), and noise.
#   apps/ble/test/run.sh [fuzz iterations, 200000 by default]
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/ble_test_decode.$$"
trap 'rm -rf "$OUT" "$OUT.dSYM" "$OUT.d"' EXIT

# The name tables are searched by halves: each must stay sorted.
awk '
    /^static const (name16_t|bth_obj_t) [A-Z0-9]+\[\] = \{/ { t = $4; last = -1; next }
    /^\};/ { t = "" }
    t != "" && match($0, /^ *\{ 0x[0-9A-Fa-f]+,/) {
        v = substr($0, RSTART, RLENGTH); sub(/^ *\{ /, "", v); sub(/,$/, "", v)
        n = 0
        for (i = 3; i <= length(v); i++) n = n * 16 + index("0123456789abcdef", tolower(substr(v, i, 1))) - 1
        if (n <= last) { printf "unsorted %s: %s\n", t, v; bad = 1 }
        last = n
    }
    END { exit bad }
' ../main/bl_names.c ../main/bl_decode.c

SAN="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
# the simulator is not this app's: built apart, its warnings shown but not fatal
mkdir -p "$OUT.d"
cc -std=c11 -Wall -Wextra -Wno-missing-field-initializers $SAN -Ishim -c ../../../sim/ble_sim.c -o "$OUT.d/ble_sim.o"
cc -std=c11 -Wall -Wextra -Werror $SAN -I../main -Ishim \
   ../main/bl_decode.c ../main/bl_names.c ../main/bl_crypt.c test_decode.c "$OUT.d/ble_sim.o" -lm -o "$OUT"
"$OUT" "$@"
