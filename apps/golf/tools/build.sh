#!/bin/zsh
# The test bench, with plain cc: ./build.sh && /tmp/gfh map 1 /tmp/m.ppm
cd ${0:a:h}
cc -O2 -Wall -I../main -I../../../components/aos_hal/include -DAOS_SIM gf_harness.c \
   ../main/gf_world.c ../main/gf_map.c ../main/gf_gfx.c ../main/gf_holes.c ../main/gf_art.c \
   ../main/gf_view3d.c ../main/gf_phys.c ../main/gf_game.c ../main/gf_outfit.c -o /tmp/gfh -lm
