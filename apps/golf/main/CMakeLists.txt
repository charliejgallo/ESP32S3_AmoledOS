idf_component_register(
    SRCS "golf.c" "gf_play.c" "gf_shop.c" "gf_link.c" "gf_world.c" "gf_holes.c"
         "gf_map.c" "gf_view3d.c" "gf_gfx.c" "gf_phys.c" "gf_game.c" "gf_art.c" "gf_outfit.c" "gf_audio.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the pixel loops is a #pragma in each of those files: the .so is
# compiled by elf_loader.cmake with -Os, and per-file options do not reach it.
