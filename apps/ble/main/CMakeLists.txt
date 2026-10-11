# The .so takes every .c under main/ through project_so()'s GLOB_RECURSE;
# this list is the normal component build's, which only needs to compile.
idf_component_register(
    SRCS "ble.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
