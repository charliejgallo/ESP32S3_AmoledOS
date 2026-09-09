idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES aos_hal aos_ui aos_apps aos_board aos_dynapp aos_web aos_ble
)
