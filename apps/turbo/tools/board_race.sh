#!/bin/zsh
# Races Metro Freeway on the board with the gas held (nobody steers) and
# prints the line the app appends to turbo_stats.txt when the race ends:
# fps, and the milliseconds of each part of the frame (CPU cycles).
#
#   ./board_race.sh [stage] [host]   stage 0..4 (default 0), host 192.168.1.107
#
# With apps/turbo_dev.txt on the card saying "auto unlock" the bot drives
# and every stage is open; without it only the first stage, gas held.
#
# Wakes the screen before opening the app and before the taps (the first
# tap on a dimmed screen only wakes it). No "curl | grep -q" in the loops:
# here that pipe hung whole loops, so the answers go through variables.
ST=${1:-0}
H=http://${2:-192.168.1.107}
tap() { curl -s -m 6 "$H/api/mem?tap=$1" > /dev/null; }
stats() { curl -s -m 6 "$H/api/download?dir=apps&name=turbo_stats.txt"; }

before=$(stats | grep -c "race:")
tap 184,20; sleep 1
apps=$(curl -s -m 8 "$H/api/apps")
case "$apps" in
*'"abierta":"demo.turbo"'*) ;;
*)  curl -s -m 8 -X POST "$H/api/accion" -d "que=abrir&id=demo.turbo" > /dev/null
    sleep 9 ;;
esac
tap 184,440; sleep 1.5
tap 184,227; sleep 2          # Time trial
tap 184,$((93 + ST * 74)); sleep 5   # the stage, then its loading
tap 320,400,70000             # the gas, held
for i in $(seq 1 45); do
    s=$(stats)
    n=$(print -r -- "$s" | grep -c "race:")
    [ "$n" -gt "$before" ] && break
    sleep 4
done
print -r -- "$s" | grep "race:" | tail -1
