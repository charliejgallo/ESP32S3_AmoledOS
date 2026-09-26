idf_component_register(
    SRCS "aos_web.c" "aos_log.c" "aos_mem.c" "aos_jpegbench.c" "aos_h264bench.c" "aos_player_api.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_ui aos_apps aos_dynapp aos_usb aos_board spi_flash esp_http_server espcoredump esp_partition espressif__esp_new_jpeg espressif__esp_h264
    EMBED_TXTFILES "portal.html" "wifi.html" "clima.html" "remoto.html" "red.html"
                   "cotiz.html" "camaras.html" "sensores.html" "ap.html" "pixel.html" "pato.html" "lua.html" "usb.html" "iconos.html"
                   "inicio.html" "ajustes.html" "pantalla.html" "registro.html" "alarmas.html"
                   "aos.css" "aos.js" "menu.html" "aic.js" "glifos.js" "modelos.html"
)
