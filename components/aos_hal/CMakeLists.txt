idf_component_register(
    SRCS "aos_hal_esp32.c" "aos_lvmem.c" "aos_http.c" "aos_scan.c" "aos_notif.c" "aos_steps.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_ble aos_board lvgl esp_wifi esp_netif nvs_flash esp_timer espressif__mdns lwip esp_mm
             waveshare__esp32_s3_touch_amoled_1_8 espressif__esp_lcd_touch
             espressif__esp_lvgl_port
    # mbedtls es de aos_http.c: el bundle de certificados y el handshake.
    PRIV_REQUIRES fatfs esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_lcd mbedtls app_update esp_mm
)

# El reloj del QSPI del panel lo fija un macro del BSP, que es un componente
# gestionado. Se intercepta la creacion del panel para subirlo (ver
# __wrap_esp_lcd_new_panel_io_spi en aos_hal_esp32.c).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lcd_new_panel_io_spi" "-Wl,--wrap=i2s_new_channel")

# RAM audit switches (docs/RAM-AUDIT.md). All ON by default since the
# candidate build of 2026-09-12; pass -D<name>=0 to idf.py to turn one off.
foreach(_sw AOS_AUDIT_LVGL_PSRAM AOS_AUDIT_LVGL_STACK16 AOS_AUDIT_PSRAM_STACKS)
    if(NOT DEFINED ${_sw})
        set(${_sw} 1)
    endif()
endforeach()
#   AOS_AUDIT_LVGL_PSRAM    LVGL objects, styles and layers go to PSRAM (aos_lvmem.c)
#   AOS_AUDIT_LVGL_STACK16  LVGL task stack 16 KB instead of 20
#   AOS_AUDIT_PSRAM_STACKS  the tone task's stack in PSRAM (http/player/mic stay internal: they can reach NVS)
if(AOS_AUDIT_LVGL_PSRAM)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lv_malloc_core" "-Wl,--wrap=lv_realloc_core")
endif()
if(AOS_AUDIT_LVGL_STACK16)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AOS_AUDIT_LVGL_STACK16=1)
endif()
if(AOS_AUDIT_PSRAM_STACKS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AOS_AUDIT_PSRAM_STACKS=1)
endif()

# Test-only, OFF by default and never for a release: the brightness write
# WITHOUT the LVGL lock, the way it was before 2026-09-15, so that
# /api/mem?spin=N reproduces the SPI bus assert on the A side of an A/B
# (docs/POWER.md 5.9). Like the switches above it sticks in the CMake cache:
# build with -DAOS_TEST_UNLOCKED_BRIGHTNESS=0 afterwards.
if(AOS_TEST_UNLOCKED_BRIGHTNESS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AOS_TEST_UNLOCKED_BRIGHTNESS=1)
endif()
