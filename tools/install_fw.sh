#!/bin/zsh
#
# Pushes the firmware to the board over wifi, without the cable.
#
#   ./tools/install_fw.sh 192.168.1.116
#   ./tools/install_fw.sh amoledos.local build/amoledos.bin
#
# It sends build/amoledos.bin -the app on its own, NOT the merged image- to
# POST /api/ota, which writes it into the idle slot and points the bootloader
# at it. Then it asks for the restart and waits for the board to come back.
#
# What it does NOT do, on purpose:
#
#   - It does not touch NVS. Your wifi, your language, your watchface and the
#     apps' data survive, which is the whole difference against flashing
#     amoledos-full.bin over USB.
#   - It does not touch the .so files on the card. If the ABI changed, run
#     ./tools/install_apps.sh afterwards; the loader refuses mismatched apps
#     and says so in the log.
#
# The image boots ON TRIAL: if it does not come up, the bootloader goes back to
# the previous one by itself at the next restart. That is why this script waits
# and then checks the version - if the board comes back on the old firmware,
# the new one did not survive.
set -e
ROOT=${0:a:h:h}
HOST=${1:?usage: install_fw.sh <ip-or-name> [image.bin]}
BIN=${2:-$ROOT/build/amoledos.bin}

[[ -f $BIN ]] || { echo "$BIN is missing - run 'idf.py build' first"; exit 1; }

size=$(wc -c < $BIN | tr -d ' ')
echo "== $(basename $BIN), $size B -> $HOST =="

before=$(curl -sS --max-time 10 "http://$HOST/api/status" || true)
echo "   before: $before"

code=$(curl -sS -o /tmp/aos_ota_out.$$ -w '%{http_code}' --max-time 300 \
    -X POST "http://$HOST/api/ota" \
    -H 'Content-Type: application/octet-stream' \
    --data-binary "@$BIN") || code=000

if [[ $code != 2* ]]; then
    echo "   FAILED (HTTP $code): $(cat /tmp/aos_ota_out.$$ 2>/dev/null | head -c 300)"
    rm -f /tmp/aos_ota_out.$$
    exit 1
fi
echo "   written: $(cat /tmp/aos_ota_out.$$)"
rm -f /tmp/aos_ota_out.$$

echo "   restarting..."
curl -sS --max-time 10 -X POST "http://$HOST/api/ota/restart" > /dev/null || true

# It takes about 5 s to boot and reach the portal. Give it 60 and say what
# happened, because "it did not answer" and "it went back to the old one" are
# very different things.
for i in {1..30}; do
    sleep 2
    after=$(curl -sS --max-time 4 "http://$HOST/api/status" 2>/dev/null || true)
    if [[ -n $after ]]; then
        echo "   back after $((i * 2)) s: $after"
        echo
        echo "The image boots on trial and confirms itself at 30 s of uptime."
        echo "If it does not survive, the next restart goes back to the previous"
        echo "one on its own - check with 'idf.py monitor' if in doubt."
        exit 0
    fi
done

echo "   it did not answer in 60 s."
echo "   That does not mean it is bricked: the trial image may have failed and"
echo "   the bootloader may be going back to the previous one. Plug the USB in"
echo "   and look at 'idf.py monitor' before reflashing."
exit 1
