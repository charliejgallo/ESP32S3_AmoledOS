idf_component_register(SRCS "aos_usb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES usb espressif__esp_tinyusb esp_pm hal soc
                       PRIV_REQUIRES esp_vfs_console vfs)
