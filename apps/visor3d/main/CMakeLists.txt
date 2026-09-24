idf_component_register(
    SRCS "visor3d.c" "v3_mesh.c" "v3_raster.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
# -O2 for the rasteriser and the loader is a #pragma in each of those files:
# the .so is compiled by elf_loader.cmake with -Os, and per-file options do
# not reach it.
