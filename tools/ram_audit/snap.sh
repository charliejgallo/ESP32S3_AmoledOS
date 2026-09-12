#!/bin/zsh
# snap.sh <label> [ip]  — saves /api/mem, /api/status and /api/log under scratchpad/runs/<label>/
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
L=${1:?label}; IP=${2:-192.168.1.125}
D=$S/runs/$L; mkdir -p $D
curl -s -m 20 "http://$IP/api/mem"    > $D/mem.txt
curl -s -m 10 "http://$IP/api/status" > $D/status.json
curl -s -m 10 "http://$IP/api/log"    > $D/log.txt
echo "== $L =="; head -6 $D/mem.txt; echo "..."; grep -A1 "stacks:" $D/mem.txt | head -2; grep "code reservation\|== flash" -A1 $D/mem.txt | head -4
