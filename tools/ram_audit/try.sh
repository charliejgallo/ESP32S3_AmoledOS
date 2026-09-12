#!/bin/zsh
# try.sh <label> — OTA the fork's current build with the serial captured; say whether it boots and confirms.
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
L=${1:?label}; IP=192.168.1.125; B=${2:-build}
cd ${AOS_AUDIT_REPO:-$(pwd)}
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
mkdir -p $S/$L && cp $B/amoledos.elf $B/amoledos.map $S/$L/
python -m esp_idf_size $B/amoledos.map | sed -n '5,5p'
rm -f $S/runs/$L.serial.log
python $S/serial_capture.py /dev/cu.usbmodem1101 $S/runs/$L.serial.log 200 &
SER=$!
sleep 12
./tools/install_fw.sh $IP $B/amoledos.bin 2>&1 | grep -E "written|back after|Running from|DID NOT|did not" | cut -c1-100
sleep 55
curl -s -m 5 "http://$IP/api/status" | grep -o '"version":"[^"]*","battery":[0-9]*,"heap":[0-9]*,"psram":[0-9]*' ; echo
curl -s -m 5 "http://$IP/api/status" | grep -o '"slot":"[^"]*","trial":[a-z]*'; echo
grep -a -n "Guru\|assert failed\|abort()\|confirmed\|AUDIT" $S/runs/$L.serial.log | head -6 | cut -c1-160
kill $SER 2>/dev/null
