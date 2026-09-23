idf_component_register(
    SRCS "mila.c" "ml_rules.c" "ml_level.c" "ml_world.c" "ml_play.c" "ml_mila.c" "ml_casita.c" "ml_map.c"
         "ml_hud.c" "ml_ui.c" "ml_shop.c" "ml_prog.c" "ml_audio.c" "ml_link.c" "ml_art.c" "ml_gfx.c"
         "ml_render.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the pixel loops is a #pragma in each of those files: the .so is
# compiled by elf_loader.cmake with -Os, and per-file options do not reach it.
