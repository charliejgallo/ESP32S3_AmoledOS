#!/bin/zsh
# bench.sh <label> [lvpsram 0|1] — per-screen render benchmark on the running firmware.
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
L=${1:?label}; IP=192.168.1.125; M=${2:-}
D=$S/runs/$L; mkdir -p $D
act() { curl -s -m 5 -X POST "http://$IP/api/accion" -d "$1" >/dev/null; }
[[ -n $M ]] && curl -s -m 5 "http://$IP/api/mem?lvpsram=$M" | head -3
act que=despertar; act que=inicio; sleep 4
for scr in watchface launcher settings clima calendar; do
  case $scr in
    watchface) act que=inicio ;;
    launcher)  act que=menu ;;
    settings)  act "que=abrir&id=aos.settings" ;;
    clima)     act "que=abrir&id=aos.clima" ;;
    calendar)  act "que=abrir&id=aos.calendar" ;;
  esac
  sleep 6
  r=$(curl -s -m 30 "http://$IP/api/mem?bench=1" | tee $D/mem-$scr.txt | grep -E "screen render|^internal|^exec " | tr '\n' ' ')
  echo "$scr: $r"
  act que=inicio; sleep 3
done
