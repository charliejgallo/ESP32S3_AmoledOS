idf_component_register(
    SRCS "aos_web.c" "aos_log.c" "aos_mem.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_ui aos_apps aos_dynapp spi_flash esp_http_server
    EMBED_TXTFILES "portal.html" "wifi.html" "clima.html" "remoto.html" "red.html"
                   "cotiz.html" "sensores.html" "ap.html" "pixel.html"
                   "inicio.html" "ajustes.html" "pantalla.html" "registro.html" "alarmas.html"
                   "aos.css" "aos.js"
)
