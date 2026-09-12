#!/bin/zsh
# fpsrun.sh <label> <app-id> [tap "x,y,ms" ...] — open the app, apply the taps (2 s apart), capture the screen, measure fps over 5 s.
S=${AOS_AUDIT_DIR:-/tmp/aos_audit}
L=${1:?label}; APP=${2:?app}; shift 2; IP=192.168.1.125
D=$S/fps/$L; mkdir -p $D
act() { curl -s -m 5 -X POST "http://$IP/api/accion" -d "$1" >/dev/null; }
act que=despertar; act que=inicio; sleep 2; act que=despertar; act "que=abrir&id=$APP"; sleep 6
for t in "$@"; do curl -s -m 5 "http://$IP/api/mem?tap=$t" | head -1; sleep 2; done
cd ${AOS_AUDIT_REPO:-$(pwd)} && python3 tools/captura.py $IP $D/$APP.png --sin-despertar >/dev/null 2>&1
r=$(curl -s -m 30 "http://$IP/api/mem?fps=5" | grep "rendered frames")
echo "$L $APP: $r" | tee -a $S/fps/results.txt
curl -s -m 20 "http://$IP/api/mem" | grep -E "^exec |apps code|^free " | tr '\n' ' '; echo
act que=inicio
