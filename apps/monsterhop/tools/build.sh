#!/bin/zsh
# The test bench, with plain cc: ./build.sh && /tmp/mhh frame test_1 5 3 /tmp/f.ppm
# (stub/ stands in for the firmware's headers the rules and the scene use)
cd ${0:a:h}
cc -O2 -Wall -Wextra -Wno-unused-parameter -I../main -Istub -DMH_HARNESS mh_harness.c \
   ../main/mh_gfx.c ../main/mh_art.c ../main/mh_level.c ../main/mh_world.c ../main/mh_render.c ../main/mh_game.c \
   ../main/mh_cast.c ../main/mh_scene.c ../main/mh_shop.c \
   -o /tmp/mhh -lm
