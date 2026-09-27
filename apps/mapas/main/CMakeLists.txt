idf_component_register(
    SRCS "mapas.c" "mp_mvt.c" "mp_draw.c" "mp_render.c" "mp_store.c" "mp_search.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the decoder and the renderer is a #pragma in each of those files:
# the .so is compiled by elf_loader.cmake with -Os, and per-file options do
# not reach it.
