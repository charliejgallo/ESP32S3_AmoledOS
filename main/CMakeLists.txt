idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES aos_hal aos_ui aos_apps aos_board aos_dynapp aos_web aos_ble esp_mm
)
# RAM audit switch (see docs/RAM-AUDIT.md): the .bss of our components to PSRAM.
if(AOS_AUDIT_PSRAM_BSS)
    set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LDFRAGMENTS "aos_psram.lf")
    idf_build_set_property(__LDGEN_FRAGMENT_FILES "${CMAKE_CURRENT_LIST_DIR}/aos_psram.lf" APPEND)
endif()
