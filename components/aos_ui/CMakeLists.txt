# ESP-IDF evalua este archivo tambien con 'cmake -P' (modo script) para juntar
# las dependencias entre componentes, y ahi CONFIGURE_DEPENDS esta prohibido.
# En la pasada de verdad si lo usamos, para que un archivo nuevo entre al build
# sin tener que reconfigurar a mano.
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB UI_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
else()
    file(GLOB UI_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
endif()

idf_component_register(
    SRCS ${UI_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES aos_hal aos_fonts lvgl
)
