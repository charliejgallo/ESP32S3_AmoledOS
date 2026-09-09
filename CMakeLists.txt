cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Va ANTES de project(): despues, las flags de los componentes ya estan puestas.
include(${CMAKE_CURRENT_LIST_DIR}/components/aos_fonts/aos_fonts.cmake)

# La version sale del tag, no de una constante que hay que acordarse de tocar.
#
# 'git describe --tags' da "v0.1.2" justo sobre un tag y "v0.1.2-4-gabc1234"
# cuatro commits despues, que es exactamente lo que uno quiere leer en
# /api/status cuando esta averiguando si una actualizacion entro. El IDF la
# mete en el descriptor de la app y aos_hal_firmware_version() la devuelve.
#
# Sin repo git -un tarball descargado- queda el valor de abajo y nada se rompe.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE AOS_GIT_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT AOS_GIT_VER)
    set(AOS_GIT_VER "0.1.2")
endif()
set(PROJECT_VER "${AOS_GIT_VER}")

project(amoledos)

# Genera la tabla de simbolos que ven las apps dinamicas y habilita
# 'idf.py so' para compilar los .so de las apps.
include(elf_loader OPTIONAL)
