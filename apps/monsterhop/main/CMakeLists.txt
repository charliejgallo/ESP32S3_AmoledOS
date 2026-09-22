idf_component_register(
    SRCS "monsterhop.c" "mh_game.c" "mh_scene.c" "mh_world.c" "mh_render.c" "mh_art.c" "mh_cast.c"
         "mh_level.c" "mh_gfx.c" "mh_hud.c" "mh_audio.c" "mh_shop.c" "mh_prog.c" "mh_ui.c" "mh_link.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the pixel loops is a #pragma in each of those files: the .so is
# compiled by elf_loader.cmake with -Os, and per-file options do not reach it.
