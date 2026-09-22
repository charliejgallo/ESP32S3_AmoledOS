#!/bin/zsh
# Races one stage on the board, the bot driving, and prints the line the app
# appends to turbo_stats.txt when the race ends: fps, the milliseconds of
# each part of the frame (CPU cycles) and the free PSRAM.
#
#   ./board_race.sh [stage] [host]   stage 0..6 as in tb_track.h (default 0),
#                                    host 192.168.1.107
#   ./board_race.sh clean [host]     afterwards: progress back to a fresh
#                                    install, and the switch file deleted
#
# It writes apps/turbo_dev.txt ("auto unlock go N": the bot drives, every
# stage is open, and a time trial of stage N starts as soon as the app has
# loaded) and reopens Turbo so the app reads it. The bot's races leave coins
# and records behind: run "clean" when done.
#
# No "curl | grep -q" in the loops: here that pipe hung whole loops, so the
# answers go through variables.
ST=${1:-0}
H=http://${2:-192.168.1.107}
DEV=$(mktemp)
stats() { curl -s -m 6 "$H/api/download?dir=apps&name=turbo_stats.txt"; }
dev() {
    print -n -- "$1" > $DEV
    curl -s -m 20 -o /dev/null -X POST "$H/api/upload?dir=apps&name=turbo_dev.txt" --data-binary "@$DEV"
}
reopen() {
    curl -s -m 8 -X POST "$H/api/accion" -d "que=despertar" > /dev/null
    curl -s -m 8 -X POST "$H/api/accion" -d "que=inicio" > /dev/null
    sleep 2
    curl -s -m 8 -X POST "$H/api/accion" -d "que=abrir&id=demo.turbo" > /dev/null
}

# the portal first (right after a boot the download comes back empty, and
# every old line looked new)
for i in $(seq 1 30); do
    code=$(curl -s -m 6 -o /dev/null -w "%{http_code}" "$H/api/download?dir=apps&name=turbo_stats.txt")
    [ "$code" = "200" ] || [ "$code" = "404" ] && break
    sleep 2
done

if [ "$ST" = "clean" ]; then
    dev "reset"
    reopen
    sleep 12
    curl -s -m 8 -X POST "$H/api/accion" -d "que=inicio" > /dev/null
    curl -s -m 8 -X POST "$H/api/delete?dir=apps&name=turbo_dev.txt" > /dev/null
    rm -f $DEV
    echo "progress reset, turbo_dev.txt deleted"
    exit 0
fi

before=$(stats | grep -c "race:")
dev "auto unlock go $ST"
reopen
for i in $(seq 1 60); do
    sleep 4
    s=$(stats)
    n=$(print -r -- "$s" | grep -c "race:")
    [ "$n" -gt "$before" ] && break
done
rm -f $DEV
print -r -- "$s" | grep "race:" | tail -1
