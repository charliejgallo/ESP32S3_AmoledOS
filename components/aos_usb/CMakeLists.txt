idf_component_register(SRCS "aos_usb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES usb espressif__esp_tinyusb esp_pm hal soc aos_hal esp_rom
                       PRIV_REQUIRES esp_vfs_console vfs espressif__usb_host_msc fatfs esp_driver_sdmmc sdmmc waveshare__esp32_s3_touch_amoled_1_8)
