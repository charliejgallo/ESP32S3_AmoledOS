idf_component_register(
    SRCS "radio.c" "radio_art.c" "radio_ui.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl espressif__esp_new_jpeg
)
