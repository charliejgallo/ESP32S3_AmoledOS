idf_component_register(
    SRCS "video.c" "vd_avi.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl espressif__esp_new_jpeg
)
