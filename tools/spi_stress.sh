#!/bin/zsh
#
# The SPI bus race of docs/POWER.md 5.9, run three ways against a board:
#
#   ./tools/spi_stress.sh <ip>
#
#   1. 1,500 brightness writes from the server's task while Vida redraws
#   2. 5,000 writes while the launcher list is scrolled by injected drags
#   3. 120 cycles of screen off / on over the portal with Vida up
#
# A firmware built with -DAOS_TEST_UNLOCKED_BRIGHTNESS=1 (the pre-fix path)
# reboots during round 2 - task watchdog, the LVGL task stuck waiting for a
# flush the interleaved command lost. The fixed firmware finishes all three
# and prints the same uptime, larger. Every request is spaced: the server has
# a finite number of sockets (docs/PORTAL.md).
set -u
B=${1:?board ip}
st() { curl -s --max-time 10 "http://$B/api/status" | grep -o '"version":"[^"]*"\|"uptime_s":[0-9]*\|"boot_reason":"[^"]*"' | tr '\n' ' '; echo; }
post() { curl -s --max-time 10 -X POST "http://$B/api/accion" --data "$1" > /dev/null; }
alive() { for i in 1 2 3 4 5 6; do curl -s --max-time 8 "http://$B/api/status" > /dev/null && return 0; sleep 8; done; return 1; }

echo -n "before:  "; st
echo "-- 1. spin 1500 with Vida"
post "que=despertar"; post "que=abrir&id=aos.life"; sleep 2
curl -s --max-time 90 "http://$B/api/mem?spin=1500" | grep -A1 "== spin" | tail -1 || echo "   no answer"
alive || { echo "   board gone after round 1"; }
echo "-- 2. spin 5000 while the launcher scrolls"
post "que=inicio"; sleep 1; post "que=menu"; sleep 1
( for i in $(seq 1 12); do
      curl -s --max-time 10 "http://$B/api/mem?tap=184,400,2500,184,100" > /dev/null; sleep 2.6
      curl -s --max-time 10 "http://$B/api/mem?tap=184,100,2500,184,400" > /dev/null; sleep 2.6
  done ) &
curl -s --max-time 150 "http://$B/api/mem?spin=5000" | grep -A1 "== spin" | tail -1 || echo "   no answer"
wait
alive || { echo "   board gone after round 2"; }
echo "-- 3. 120 cycles of off / on with Vida up"
post "que=inicio"; post "que=abrir&id=aos.life"; sleep 2
for i in $(seq 1 120); do
    post "que=apagar"; sleep 0.4
    post "que=despertar"; sleep 0.6
done
post "que=inicio"; sleep 3
echo -n "after:   "; st
