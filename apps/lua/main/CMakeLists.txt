# Los fuentes del interprete NO se listan aca: el .so los toma por el
# GLOB_RECURSE de project_so(). Esta lista es la del build normal de
# componentes, que solo necesita compilar la app.
idf_component_register(
    SRCS "lua_app.c" "lx_api.c" "lx_pixel.c"
    INCLUDE_DIRS "." "lua"
    REQUIRES lvgl
)
