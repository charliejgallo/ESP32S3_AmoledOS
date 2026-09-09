idf_component_register(
    SRCS "remoto.c" "rc_model.c" "rc_tilt.c" "rc_ha.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
