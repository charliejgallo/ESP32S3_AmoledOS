#!/bin/zsh
# round3.sh <prefix> — OTA the fork's build (serial captured), then snapshot every state and run the render A/B.
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
P=${1:?prefix}; IP=192.168.1.125
cd ${AOS_AUDIT_REPO:-$(pwd)}
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
act() { curl -s -m 5 -X POST "http://$IP/api/accion" -d "$1" >/dev/null; }
snapapp() { act que=despertar; sleep 1; act "que=abrir&id=$1"; sleep ${2:-10}; $S/snap.sh $P-$3 $IP | grep -E "^==|in-use"; act que=inicio; sleep 5; }
rm -f $S/runs/$P.serial.log
python $S/serial_capture.py /dev/cu.usbmodem1101 $S/runs/$P.serial.log 900 &
SER=$!
sleep 12
./tools/install_fw.sh $IP 2>&1 | grep -E "written|back after|Running from|DID NOT|did not" | cut -c1-160
sleep 45
curl -s -m 5 "http://$IP/api/status" | head -c 160; echo
sleep 40
$S/snap.sh $P-idle $IP | grep -E "^==|in-use"
snapapp demo.2043 12 app2043
snapapp aos.settings 8 settings
snapapp demo.chatarra 12 chatarra
snapapp aos.clima 25 clima
snapapp aos.photos 12 photos
snapapp aos.calendar 8 calendar
act que=despertar; act que=menu; sleep 6; $S/snap.sh $P-launcher $IP | grep -E "^=="; act que=inicio; sleep 4
curl -s -m 5 -X POST "http://$IP/api/ajustes" -d "bt=0" >/dev/null; sleep 10; $S/snap.sh $P-btoff $IP | grep "^=="
curl -s -m 5 -X POST "http://$IP/api/ajustes" -d "bt=1" >/dev/null; sleep 10; $S/snap.sh $P-bton $IP | grep "^=="
echo "--- bench, lvgl in psram ---"; $S/bench.sh $P-bench-psram 1
echo "--- bench, lvgl internal ---"; $S/bench.sh $P-bench-int 0
curl -s -m 5 "http://$IP/api/mem?lvpsram=1" | head -2
act que=inicio
kill $SER 2>/dev/null
echo ROUND3-DONE
