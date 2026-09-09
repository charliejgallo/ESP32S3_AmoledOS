idf_component_register(
    SRCS "aos_web.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_ui esp_http_server
    EMBED_TXTFILES "portal.html" "wifi.html" "clima.html" "remoto.html" "red.html"
                   "cotiz.html" "sensores.html" "ap.html"
                   "aos.css" "aos.js"
)
