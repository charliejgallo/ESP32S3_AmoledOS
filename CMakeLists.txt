cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Va ANTES de project(): despues, las flags de los componentes ya estan puestas.
include(${CMAKE_CURRENT_LIST_DIR}/components/aos_fonts/aos_fonts.cmake)

project(amoledos)

# Genera la tabla de simbolos que ven las apps dinamicas y habilita
# 'idf.py so' para compilar los .so de las apps.
include(elf_loader OPTIONAL)
