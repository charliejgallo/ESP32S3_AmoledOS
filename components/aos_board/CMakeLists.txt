idf_component_register(
    SRCS "aos_board.c" "axp2101.c" "pcf85063.c" "qmi8658.c"
    INCLUDE_DIRS "include"
    REQUIRES waveshare__esp32_s3_touch_amoled_1_8
             waveshare__qmi8658
             waveshare__pcf85063a
             esp_timer
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio
)
