idf_component_register(
    SRCS "aos_dynapp.c" "aos_symbols.c"
    INCLUDE_DIRS "include"
    REQUIRES aos_ui aos_hal espressif__elf_loader
)

# Interceptamos esp_elf_malloc() para mandar los datos del cargador a PSRAM y
# dejar la RAM interna ejecutable entera para el .text de las apps.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_elf_malloc" INTERFACE "-Wl,--wrap=esp_elf_free")
