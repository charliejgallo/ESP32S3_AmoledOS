cmake_minimum_required(VERSION 3.16)
project(amoledos_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# mbedtls para el https:// de aos_http.c. Es la MISMA biblioteca que usa la
# placa (el IDF trae 3.6.7, Homebrew 3.4.1) y por eso el cliente sigue siendo
# uno solo: lo unico que cambia entre plataformas es de donde sale el almacen
# de CAs, y eso son cinco lineas adentro de trust_attach().
#   brew install mbedtls
find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h
          HINTS /opt/homebrew/opt/mbedtls/include /usr/local/opt/mbedtls/include)
find_library(MBEDTLS_LIB     mbedtls     HINTS /opt/homebrew/opt/mbedtls/lib /usr/local/opt/mbedtls/lib)
find_library(MBEDX509_LIB    mbedx509    HINTS /opt/homebrew/opt/mbedtls/lib /usr/local/opt/mbedtls/lib)
find_library(MBEDCRYPTO_LIB  mbedcrypto  HINTS /opt/homebrew/opt/mbedtls/lib /usr/local/opt/mbedtls/lib)
if(NOT MBEDTLS_INCLUDE_DIR OR NOT MBEDTLS_LIB)
    message(FATAL_ERROR "falta mbedtls: brew install mbedtls")
endif()

# LVGL toma la configuracion de ../sim/lv_conf.h (ruta relativa a lvgl/src)
# LVGL incluye <SDL2/SDL.h>, asi que necesita el directorio padre del include
foreach(dir ${SDL2_INCLUDE_DIRS})
    get_filename_component(_parent ${dir} DIRECTORY)
    include_directories(${dir} ${_parent})
endforeach()
link_directories(${SDL2_LIBRARY_DIRS})

# Los ejemplos y demos de LVGL, apagados. Las variables que MANDAN son estas
# dos: os_desktop.cmake las declara con option(... ON) y no mira ninguna
# LV_CONF_BUILD_DISABLE_*, asi que las tres lineas que habia antes no apagaban
# nada y el simulador venia compilando demos y ejemplos en cada build. Se noto
# recien al sacar las Montserrat de LVGL: lv_example_style_21.c las nombra
# directo y fue lo unico que no compilo.
#
# FORCE porque option() no pisa una entrada de cache que ya exista.
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
add_subdirectory(lvgl)
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

file(GLOB AOS_UI_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_ui/*.c)
file(GLOB AOS_APPS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_apps/*.c)

# Generated font set (tools/gen_fonts.py). Same sources as the board, so the
# simulator renders accented text exactly the way the hardware does.
file(GLOB AOS_FONT_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_fonts/*.c)

# Las apps dinamicas de apps/ tambien se compilan adentro del simulador, para
# poder disenarlas sin la placa. AOS_SIM_BUILTIN hace que se auto-registren en
# lugar de exportar el simbolo de entrada del .so.
file(GLOB AOS_DYNAPP_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../apps/*/main/*.c)

add_executable(amoledos_sim
    main.c
    hal_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_hal/aos_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_hal/aos_notif.c
    ${AOS_UI_SRC}
    ${AOS_APPS_SRC}
    ${AOS_FONT_SRC}
    ${AOS_DYNAPP_SRC}
)

target_include_directories(amoledos_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_hal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_apps/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/aos_fonts/include
    ${SDL2_INCLUDE_DIRS}
    ${MBEDTLS_INCLUDE_DIR}
)

target_compile_options(amoledos_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(amoledos_sim PRIVATE AOS_SIM_BUILTIN AOS_SIM)
target_link_libraries(amoledos_sim PRIVATE lvgl ${SDL2_LIBRARIES} m
    ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB})
target_link_directories(amoledos_sim PRIVATE ${SDL2_LIBRARY_DIRS})
