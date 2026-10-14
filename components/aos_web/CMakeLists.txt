idf_component_register(
    SRCS "aos_web.c" "aos_web_access.c" "aos_log.c" "aos_mem.c" "aos_jpegbench.c" "aos_h264bench.c" "aos_player_api.c" "aos_radio_api.c" "aos_ble_api.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_ui aos_apps aos_dynapp aos_usb aos_board spi_flash esp_mm esp_http_server espcoredump esp_partition espressif__esp_new_jpeg espressif__esp_h264
    # the pages of the card's apps (clima, radio, mapas...) are not here: each
    # lives in its app, apps/<app>/web/<app>.html, and is served from the card
    EMBED_TXTFILES "portal.html" "wifi.html" "red.html"
                   "ap.html" "pato.html" "usb.html" "iconos.html"
                   "inicio.html" "ajustes.html" "pantalla.html" "registro.html" "alarmas.html"
                   "aos.css" "aos.js" "menu.html" "aic.js" "glifos.js"
)
