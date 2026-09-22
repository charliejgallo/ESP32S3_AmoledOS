idf_component_register(
    SRCS "turbo.c" "tb_link.c" "tb_game.c" "tb_track.c" "tb_render.c" "tb_art.c" "tb_gfx.c" "tb_hud.c" "tb_audio.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the pixel loops is a #pragma in each of those files: the .so is
# compiled by elf_loader.cmake with -Os, and per-file options do not reach it.
