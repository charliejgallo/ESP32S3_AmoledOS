idf_component_register(
    SRCS "chatarra.c"
         "ch_pixel.c"
         "ch_parts.c"
         "ch_world.c"
         "ch_sound.c"
         "ch_zonas.c"
         "ch_map.c"
         "ch_battle.c"
         "ch_ui.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
