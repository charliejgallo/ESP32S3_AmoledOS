# The engine (doomgeneric/) and the port (port/) are NOT listed here: the .so
# takes every .c under main/ through project_so()'s GLOB_RECURSE. This list is
# the normal component build's, which only needs to compile the app.
idf_component_register(
    SRCS "doom.c"
    INCLUDE_DIRS "." "doomgeneric" "port"
    REQUIRES lvgl
)
# -O2 for the renderer and the port's pixel loops is a #pragma in each of
# those files: the .so is compiled by elf_loader.cmake with -Os, and
# per-file options do not reach it.
