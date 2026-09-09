idf_component_register(SRCS "aos_ble.c" "aos_ancs.c" "aos_ams.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt nvs_flash esp_timer aos_hal)

# Barrido del aire al arrancar: descomentar para que el reloj liste los BLE que
# escucha. Separa "la radio no anda" de "lo que mandamos no se entiende", que
# desde afuera se ven igual. Compila con CENTRAL=y, no hace falta el observador.
#target_compile_definitions(${COMPONENT_LIB} PRIVATE AOS_BLE_DIAG_SCAN=1)

# Recorrido del arbol GATT del telefono: descomentar para que liste que
# servicios expone y lea la hora y la bateria. Lo que devolvio el 2026-09-07
# esta en docs/HANDOFF-BLE-ANCS.md seccion 25.
#target_compile_definitions(${COMPONENT_LIB} PRIVATE AOS_BLE_DIAG_GATT=1)
