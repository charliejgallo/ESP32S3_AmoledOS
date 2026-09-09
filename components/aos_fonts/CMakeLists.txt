# Generated font set. See tools/gen_fonts.py.
#
# ESP-IDF also evaluates this file with 'cmake -P' (script mode) to collect
# inter-component dependencies, and CONFIGURE_DEPENDS is rejected there. Use it
# only on the real pass, so a regenerated size lands in the build without a
# manual reconfigure.
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB FONT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
else()
    file(GLOB FONT_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
endif()

idf_component_register(
    SRCS ${FONT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)
