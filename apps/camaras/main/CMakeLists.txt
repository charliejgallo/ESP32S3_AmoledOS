idf_component_register(
    SRCS "camaras.c" "cam_cfg.c" "cam_rtsp.c" "cam_http.c" "cam_depay.c" "cam_conv.c" "cam_view.c" "cam_sps.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl espressif__esp_new_jpeg
)
