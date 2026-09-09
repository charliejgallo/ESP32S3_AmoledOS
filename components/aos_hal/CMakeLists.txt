idf_component_register(
    SRCS "aos_hal_esp32.c" "aos_http.c" "aos_scan.c" "aos_notif.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_ble aos_board lvgl esp_wifi esp_netif nvs_flash esp_timer espressif__mdns lwip
             waveshare__esp32_s3_touch_amoled_1_8 espressif__esp_lcd_touch
             espressif__esp_lvgl_port
    # mbedtls es de aos_http.c: el bundle de certificados y el handshake.
    PRIV_REQUIRES fatfs esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_lcd mbedtls app_update
)

# El reloj del QSPI del panel lo fija un macro del BSP, que es un componente
# gestionado. Se intercepta la creacion del panel para subirlo (ver
# __wrap_esp_lcd_new_panel_io_spi en aos_hal_esp32.c).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lcd_new_panel_io_spi" "-Wl,--wrap=i2s_new_channel")
