#!/bin/zsh
#
# tools/pong_stress.sh <minutes> <log>: the SPI/WiFi race test (docs/internal/HANDOFF-SPI-WIFI-NUCLEOS.md).
# Pong on both boards for N minutes; every 20 s a tap on the host so a finished game restarts;
# every 60 s the uptime of both, and a reboot is an uptime that went backwards.
A=192.168.1.107; B=192.168.1.100; MIN=${1:-20}; LOG=${2:-pong_stress.log}
act() { curl -s -m 5 -X POST -d "$2" http://$1/api/accion >/dev/null; }
up() { curl -s -m 5 http://$1/api/status | python3 -c "import sys,json; print(json.load(sys.stdin).get('uptime_s',-1))" 2>/dev/null || echo -1; }
# SPIN=1: besides Pong, a loop of brightness writes from the portal's httpd task on both
# boards (/api/mem?spin=N): polling transactions on the panel's SPI from another task,
# which on an unpinned firmware is the other reader of the bus lock.
if [ "${SPIN:-0}" = "1" ]; then
  ( while true; do curl -s -m 60 "http://$A/api/mem?spin=1500" >/dev/null; sleep 1; done ) &
  ( while true; do curl -s -m 60 "http://$B/api/mem?spin=1500" >/dev/null; sleep 1; done ) &
  SPINPIDS="$!"
  echo "spin loops on"
fi
act $A "que=despertar"; act $B "que=despertar"; sleep 1
act $A "que=abrir&id=aos.pong"; act $B "que=abrir&id=aos.pong"; sleep 3
ua=$(up $A); ub=$(up $B); ra=0; rb=0
echo "$(date +%H:%M:%S) start uptime A=$ua B=$ub" | tee -a $LOG
end=$(( $(date +%s) + MIN*60 )); t=0
while [ $(date +%s) -lt $end ]; do
  sleep 20; t=$((t+20))
  curl -s -m 5 "http://$A/api/mem?tap=184,300" >/dev/null   # a finished game restarts on the host
  curl -s -m 5 "http://$B/api/mem?tap=184,300,300,120,300" >/dev/null  # a little drag on the guest's paddle
  if [ $((t % 60)) -eq 0 ]; then
    na=$(up $A); nb=$(up $B)
    [ "$na" -ge 0 ] && [ "$na" -lt "$ua" ] && { ra=$((ra+1)); echo "$(date +%H:%M:%S) *** A REBOOTED (uptime $ua -> $na)" | tee -a $LOG; act $A "que=abrir&id=aos.pong"; }
    [ "$nb" -ge 0 ] && [ "$nb" -lt "$ub" ] && { rb=$((rb+1)); echo "$(date +%H:%M:%S) *** B REBOOTED (uptime $ub -> $nb)" | tee -a $LOG; act $B "que=abrir&id=aos.pong"; }
    ua=$na; ub=$nb
    echo "$(date +%H:%M:%S) t=${t}s A=$na B=$nb reboots A=$ra B=$rb" | tee -a $LOG
  fi
done
echo "$(date +%H:%M:%S) END reboots A=$ra B=$rb" | tee -a $LOG
act $A "que=inicio"; act $B "que=inicio"
[ "${SPIN:-0}" = "1" ] && pkill -P $$ 2>/dev/null
