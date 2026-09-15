#!/bin/zsh
#
# Every built-in icon through the simulator's bench (docs/ICONS.md, F4):
# drawn by its switch case on the left and from its generated AIC table on
# the right, at 66, 74 and 82 px, the object trees compared and the two
# halves of the LVGL dump diffed pixel by pixel.
#
#   ./tools/icon_bench.sh              # all of them, one line each
#   ./tools/icon_bench.sh 42           # just that id (see aos_app.h)
#   ./tools/icon_bench.sh --golden     # against tools/icon_golden/ (no switch needed)
#   ./tools/icon_bench.sh --save       # (re)write tools/icon_golden/ from the left halves
#
# --golden compares the RIGHT half (the table, drawn by production code) with
# the left half kept from the last run that still had the switch: that is how
# the interpreter is checked against the original drawings now that they are
# gone from the firmware.
#
# Zero pixels means the table IS the old drawing at the launcher's sizes.
# A handful of icons are documented as "within a pixel": their numbers here
# are small and stay small.
set -e
ROOT=${0:a:h:h}
SIM=$ROOT/sim
OUT=${AOS_BENCH_DIR:-/tmp/aos_icon_bench}
mkdir -p $OUT
[[ -x $SIM/build/amoledos_sim ]] || { echo "build the simulator first"; exit 1; }

NAMES=(${(f)"$(awk '/AOS_ICON_NONE = 0/,/} aos_icon_id_t/' $ROOT/components/aos_ui/include/aos_app.h \
        | grep -oE '^\s*AOS_ICON_[A-Z_0-9]+' | tr -d ' ')"})
COUNT=$(( ${#NAMES[@]} - 1 ))          # the last one is AOS_ICON_COUNT

MODE=halves
[[ "$1" == "--golden" ]] && { MODE=golden; shift; }
[[ "$1" == "--save" ]]   && { MODE=save;   shift; }
GOLD=$ROOT/tools/icon_golden
IDS=("$@")
(( ${#IDS[@]} )) || IDS=({1..$((COUNT - 1))})

total_bad=0
for id in $IDS; do
    name=${NAMES[$((id + 1))]}
    shot=$OUT/$id.ppm
    log=$( cd $SIM && AOS_SIM_VIEW=icontest AOS_SIM_ICON=$id AOS_SIM_SHOT=$shot \
           perl -e 'alarm 60; exec @ARGV' ./build/amoledos_sim 2>&1 | grep -E "ICONTEST (trees|depth)" )
    trees=$(echo $log | grep -oE "[0-9]+ mismatch" | head -1)
    case $MODE in
        golden) px=$(python3 $ROOT/tools/aic.py cmp $shot $GOLD/$id.gz | grep -oE "[0-9]+ differing" | grep -oE "[0-9]+") ;;
        save)   mkdir -p $GOLD; python3 $ROOT/tools/aic.py golden $shot $GOLD/$id.gz > /dev/null
                px=$(python3 $ROOT/tools/aic.py halves $shot | grep -oE "[0-9]+ differing" | grep -oE "[0-9]+") ;;
        *)      px=$(python3 $ROOT/tools/aic.py halves $shot | grep -oE "[0-9]+ differing" | grep -oE "[0-9]+") ;;
    esac
    printf "%2d %-22s trees: %-12s pixels: %s\n" $id $name "$trees" $px
    (( px > 0 )) && total_bad=$((total_bad + 1))
done
echo "icons with any differing pixel: $total_bad"
