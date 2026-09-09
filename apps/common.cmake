# Configuracion compartida por todas las apps dinamicas de AmoledOS.
#
# Se incluye DESPUES de project(), porque necesita que ESP-IDF ya haya
# procesado los componentes.
#
#   cmake_minimum_required(VERSION 3.16)
#   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#   project(mi_app)
#   include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)
#   include(elf_loader)
#   project_so(mi_app)

set(AOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# Que include(elf_loader) resuelva a NUESTRA copia y no a la de
# managed_components.
#
# Las dos son la misma version (1.3.3) y el archivo es identico byte a byte;
# la unica diferencia es que la nuestra le agrega -Os y -ffunction-sections al
# paso que compila el .so. Sin esto, los .so salen en -O0 y ocupan un 40 % de
# mas de la reserva de codigo, que es el recurso escaso.
#
# Se hace por CMAKE_MODULE_PATH y no parcheando managed_components porque eso
# se regenera solo con el lock de dependencias: seria una mina. Este include va
# despues de project(), asi que los componentes ya pusieron su directorio en el
# path y hay que ANTEPONER el nuestro.
list(PREPEND CMAKE_MODULE_PATH "${AOS_ROOT}/components/elf_loader")

# project_so() compila los fuentes fuera del sistema de componentes y arma los
# -I unicamente con la propiedad COMPILE_INCLUDE_DIRECTORIES, que viene casi
# vacia. Hay que cargarle el sdkconfig.h generado, los headers de la API de
# AmoledOS y los includes de todos los componentes del build (LVGL incluido).
# Includes globales para la compilacion normal de componentes: sin esto, el
# main de la app no encuentra aos_app.h.
# Aca NO se incluye components/aos_fonts/aos_fonts.cmake, y es a proposito.
#
# Ese archivo define LV_FONT_DEFAULT y LV_FONT_CUSTOM_DECLARE por -D. Los dos
# valores llevan caracteres que para el shell significan algo -"&" y "()"- y
# project_so() arma su linea de comandos a mano, sin comillas: las 19 apps
# fallan con "syntax error near unexpected token '('" antes de compilar nada.
#
# Y no hacen falta. LV_FONT_DEFAULT solo se usa dentro de los .c de LVGL, que
# la app no compila; en los headers aparece unicamente en comentarios. Si algun
# dia una app terminara necesitandolo, no se rompe en silencio: quedaria como
# simbolo indefinido y build_apps.sh compara TODOS los indefinidos del .so
# contra la tabla del firmware antes de dar el ok.

idf_build_set_property(INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_ui/include" APPEND)
idf_build_set_property(INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_fonts/include" APPEND)
idf_build_set_property(INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_hal/include" APPEND)

# Y estos son los que ve el paso de project_so(), que compila aparte.
idf_build_set_property(COMPILE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/config" APPEND)
idf_build_set_property(COMPILE_INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_ui/include" APPEND)
idf_build_set_property(COMPILE_INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_fonts/include" APPEND)
idf_build_set_property(COMPILE_INCLUDE_DIRECTORIES "${AOS_ROOT}/components/aos_hal/include" APPEND)

idf_build_get_property(_aos_components BUILD_COMPONENTS)
list(LENGTH _aos_components _aos_count)
message(STATUS "AmoledOS: propagando includes de ${_aos_count} componentes al .so")

foreach(_comp ${_aos_components})
    idf_component_get_property(_comp_dir ${_comp} COMPONENT_DIR)
    idf_component_get_property(_comp_incs ${_comp} INCLUDE_DIRS)
    foreach(_inc ${_comp_incs})
        if(IS_ABSOLUTE "${_inc}")
            set(_path "${_inc}")
        else()
            set(_path "${_comp_dir}/${_inc}")
        endif()
        if(EXISTS "${_path}")
            idf_build_set_property(COMPILE_INCLUDE_DIRECTORIES "${_path}" APPEND)
        endif()
    endforeach()
endforeach()
