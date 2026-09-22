#!/bin/zsh
# The test bench, with plain cc: ./build.sh && /tmp/tbh frame 0 900 /tmp/f.ppm
cd ${0:a:h}
cc -O2 -Wall -Wextra -Wno-unused-parameter -I../main -DTB_HARNESS tb_harness.c \
   ../main/tb_gfx.c ../main/tb_track.c ../main/tb_game.c ../main/tb_art.c ../main/tb_render.c \
   -o /tmp/tbh -lm
