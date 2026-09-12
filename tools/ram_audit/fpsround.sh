#!/bin/zsh
# fpsround.sh <label> <build-dir> — install the build (serial captured), then FPS of claudito, gems (PLAY) and 2043 (TOUCH), twice each.
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
L=${1:?label}; B=${2:?build}; IP=192.168.1.125
before=$(curl -s -m 5 http://$IP/api/status | grep -o '"slot":"[^"]*"')
$S/try.sh $L $B 2>&1 | tail -3 | cut -c1-140
after=$(curl -s -m 5 http://$IP/api/status | grep -o '"slot":"[^"]*"')
echo "slot $before -> $after"
if [ "$before" = "$after" ]; then echo "DID NOT TAKE"; grep -a -n "Guru\|assert failed\|abort()\|rst:0x\|elf_s3mmu" $S/runs/$L.serial.log | head -8 | cut -c1-200; exit 1; fi
grep -a -n "elf_s3mmu\|apps' code runs" $S/runs/$L.serial.log | head -4 | cut -c1-160
for pass in 1 2; do
  $S/fpsrun.sh $L demo.claudito
  $S/fpsrun.sh $L demo.gemas "184,363,100"
  $S/fpsrun.sh $L demo.2043 "184,163,200"
  curl -s -m 8 "http://$IP/api/log" | grep -a "real frame" | tail -2 | cut -c1-110
done
