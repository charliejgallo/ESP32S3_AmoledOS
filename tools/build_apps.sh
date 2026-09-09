#!/bin/zsh
#
# Builds every app in apps/ and checks that they are usable on the board.
#
#   ./tools/build_apps.sh            all of them
#   ./tools/build_apps.sh gemas      just one
#
# It does three things that get forgotten by hand:
#
#  1. Deletes build/so_objs before building. project_so() declares its objects
#     with DEPENDS on the .c alone, without tracking headers, so a change in
#     aos_app.h or aos_hal.h does NOT trigger a rebuild: 'idf.py so' relinks an
#     old object and announces "Build Shared Object" as if nothing were wrong.
#  2. Checks that the ABI number baked into the .so is the one the firmware
#     expects.
#  3. Checks that every undefined symbol of the .so is in the firmware's table,
#     since otherwise the app fails only when it is loaded on the board.
#
set -e

ROOT=${0:a:h:h}
source ~/esp/esp-idf/export.sh > /dev/null 2>&1

TABLE=$ROOT/components/aos_dynapp/aos_symbols.c
ABI=$(grep -oE '#define AOS_ABI_VERSION +[0-9]+' $ROOT/components/aos_ui/include/aos_app.h | grep -oE '[0-9]+$')
NM=$(ls ~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp-elf-nm | head -1)
OD=$(ls ~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp-elf-objdump | head -1)

if [ ! -f $TABLE ] || ! grep -q ESP_ELFSYM_EXPORT $TABLE; then
    echo "The symbol table is empty. Run this first:"
    echo "  idf.py build && python3 tools/gen_symbols.py && idf.py build"
    exit 1
fi

# The app and the firmware share LVGL structures, and some of them change
# layout according to the configuration: lv_global_t has fields under
# "#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN" and under "#if LV_USE_FS_POSIX".
# If the two configurations do not match, every inline LVGL function the app
# compiles writes into the wrong field: silent corruption, which neither heap
# poisoning nor the stack watchpoint detects. Measured: it hung claudito and
# 2043 after a second of drawing correctly.
# That is why the apps' LVGL configuration is DERIVED from the firmware's.
LVCFG=$ROOT/build/aos_lvgl_sync.defaults
if [ ! -f $ROOT/sdkconfig ]; then
    echo "$ROOT/sdkconfig is missing: run 'idf.py build' on the firmware first."
    exit 1
fi
mkdir -p $ROOT/build
grep -E '^CONFIG_LV_' $ROOT/sdkconfig > $LVCFG
grep -E '^# CONFIG_LV_[A-Z0-9_]+ is not set$' $ROOT/sdkconfig |
    sed -E 's/^# (CONFIG_LV_[A-Z0-9_]+) is not set$/\1=n/' >> $LVCFG
echo "LVGL configuration synced from the firmware ($(wc -l < $LVCFG | tr -d ' ') options)"

apps=${@:-$(ls $ROOT/apps | grep -v '\.cmake$')}
problemas=0

for app in ${(z)apps}; do
    dir=$ROOT/apps/$app
    [ -d $dir/main ] || continue
    echo "=== $app ==="
    cd $dir

    rm -rf build/so_objs
    # The sdkconfig is only deleted when the LVGL config has gone stale:
    # regenerating it forces a full rebuild of LVGL, which is several minutes
    # per app.
    if ! diff -q <(grep -E '^CONFIG_LV_' sdkconfig 2>/dev/null | sort) \
                 <(sort $LVCFG | grep -E '^CONFIG_LV_.*=[^n]|^CONFIG_LV_.*=n') \
                 > /dev/null 2>&1; then
        rm -f sdkconfig
    fi
    # The first time an app is built there is no build/, and the idf log is
    # written inside it: without this, a new app fails with "no such file or
    # directory" and the message says nothing about what is really going on.
    mkdir -p $dir/build
    log=$dir/build/idf_so.log
    if ! idf.py -DSDKCONFIG_DEFAULTS="$dir/sdkconfig.defaults;$LVCFG" so > $log 2>&1 ||
       ! grep -qE "Linking .*\.so completed" $log; then
        echo "  BUILD FAILED. Last lines of $log:"
        tail -12 $log | sed 's/^/    /'
        problemas=$((problemas + 1))
        continue
    fi

    so=$(ls build/*.so | head -1)

    # The entry point is two functions, not a structure: elf_loader's dlsym()
    # only finds symbols of function type. The ABI number is therefore not in
    # the rodata but inside aos_app_abi()'s code, which compiles to a single
    # 'movi' with the constant.
    if ! $NM -D --defined-only $so | grep -qE ' aos_app_abi$' ||
       ! $NM -D --defined-only $so | grep -qE ' aos_app_init$'; then
        echo "  FAIL: it does not export aos_app_abi/aos_app_init"
        problemas=$((problemas + 1))
        continue
    fi
    abi=$($OD -d --disassemble=aos_app_abi $so |
          grep -oE 'movi(\.n)?[[:space:]]+a[0-9]+,[[:space:]]*-?[0-9]+' |
          head -1 | grep -oE '\-?[0-9]+$')
    if [ "$abi" != "$ABI" ]; then
        echo "  FAIL: the .so says ABI $abi and the firmware expects $ABI"
        problemas=$((problemas + 1))
        continue
    fi

    # printf/fprintf/vfprintf come from the table elf_loader brings on its own
    # (see the EXTRA_SYMBOLS comment in gen_symbols.py), not from
    # aos_symbols.c: without this list, the check marked them as missing even
    # though the resolver finds them on the board all the same.
    LOADER_BUILTIN=" printf fprintf vfprintf "
    faltan=""
    for s in $($NM -D -u $so | awk '{print $2}'); do
        case "$LOADER_BUILTIN" in
            *" $s "*) continue ;;
        esac
        grep -q "ESP_ELFSYM_EXPORT($s)" $TABLE || faltan="$faltan $s"
    done
    if [ -n "$faltan" ]; then
        echo "  FAIL: symbols outside the firmware table:$faltan"
        echo "        add them to EXTRA_SYMBOLS in tools/gen_symbols.py and regenerate"
        problemas=$((problemas + 1))
        continue
    fi

    printf "  ok  %s  %s  ABI %s  %s symbols\n" \
        $(basename $so) $(du -h $so | cut -f1) $abi $($NM -D -u $so | wc -l | tr -d ' ')
done

echo
if [ $problemas -eq 0 ]; then
    echo "Built. NOT INSTALLED YET: the new .so files are still in apps/*/build/"
    echo "  over wifi:  ./tools/install_apps.sh <board-ip>"
    echo "  by hand:    cp apps/*/build/*.so /Volumes/<sd>/apps/"
    echo ""
    echo "  Without this step the board keeps running the old binaries, and the"
    echo "  symptom misleads: the firmware looks updated and the apps do not."
else
    echo "$problemas app(s) with problems"
    exit 1
fi
