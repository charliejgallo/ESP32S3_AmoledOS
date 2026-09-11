idf_component_register(
    SRCS "aos_web.c" "aos_log.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_ui aos_apps esp_http_server
    EMBED_TXTFILES "portal.html" "wifi.html" "clima.html" "remoto.html" "red.html"
                   "cotiz.html" "sensores.html" "ap.html" "pixel.html"
                   "inicio.html" "ajustes.html" "pantalla.html" "registro.html" "alarmas.html"
                   "aos.css" "aos.js"
)
