#!/bin/zsh
#
# Uploads the built .so files to the board over wifi, without taking the card
# out.
#
#   ./tools/install_apps.sh 192.168.1.116          all of them
#   ./tools/install_apps.sh 192.168.1.116 gemas    just one
#
# build_apps.sh BUILDS but does not install: it leaves the .so files in
# apps/<x>/build/ and says "ready to copy to /sdcard/apps/". If you forget this
# step, the board goes on running the old binaries and the symptom is thoroughly
# confusing -it really happened-: the firmware and its built-in apps show in the
# new language, and the dynamic apps stay in Spanish, because the card's .so
# files predate the marking.
#
# The firmware loads the .so files ONCE at startup, so this restarts the board
# when it finishes. Without the restart nothing changes and it looks as though
# it did not work.
set -e
ROOT=${0:a:h:h}
HOST=${1:?usage: install_apps.sh <ip-or-name> [app...]}
shift
WANT=("$@")

files=()
for d in $ROOT/apps/*(/); do
    name=${d:t}
    (( ${#WANT[@]} )) && [[ ${WANT[(Ie)$name]} -eq 0 ]] && continue
    so=($d/build/*.so(.N))
    (( ${#so[@]} )) && files+=($so[1])
done
(( ${#files[@]} )) || { echo "no .so found - run ./tools/build_apps.sh first"; exit 1; }

echo "== ${#files[@]} apps -> $HOST =="
total=0
for f in $files; do
    name=${f:t}
    size=$(wc -c < $f | tr -d ' ')
    code=$(curl -sS -o /dev/null -w '%{http_code}' --max-time 60 \
        -X POST "http://$HOST/api/upload?dir=apps&name=$name" --data-binary "@$f") || code=000
    if [[ $code == 2* ]]; then
        printf "   %-18s %7s B  ok\n" $name $size
        (( total += size ))
    else
        printf "   %-18s %7s B  FAILED (HTTP %s)\n" $name $size $code
        exit 1
    fi
done
echo "   $total bytes in total"
echo
# A bare 'python' may be the system's python2, which has no pyserial: the
# upload succeeds, the restart fails with an ImportError and the board is left
# with the old .so files loaded, which is exactly the symptom this script
# exists to prevent. We look for an interpreter that HAS pyserial, not one that
# exists.
#
# And mind WHERE to look: on a Mac with ESP-IDF installed, the only python with
# pyserial is usually the IDF's virtual environment's, not the system's.
# Without this search the script uploads the .so files and then reports that it
# cannot restart, which is half the job and the less obvious half.
PYBIN=""
for cand in \
    ${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python} \
    ~/.espressif/python_env/*/bin/python(N) \
    python3 python
do
    if command -v $cand > /dev/null 2>&1 && $cand -c "import serial" 2>/dev/null; then
        PYBIN=$cand
        break
    fi
done

echo "restarting the board (the .so files are loaded at startup)..."
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [[ -n $PORT && -n $PYBIN ]]; then
    $PYBIN - $PORT <<'PY'
import sys, time, serial
s = serial.Serial(sys.argv[1], 115200)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
PY
    echo "   done, restarted over $PORT"
elif [[ -n $PORT ]]; then
    echo "   there is a port but no python with pyserial (try 'source ~/esp/esp-idf/export.sh')"
    echo "   restart the board by hand so it picks up the new .so files"
else
    echo "   no serial port: restart the board by hand so it picks up the new .so files"
fi
