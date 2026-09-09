cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Va ANTES de project(): despues, las flags de los componentes ya estan puestas.
include(${CMAKE_CURRENT_LIST_DIR}/components/aos_fonts/aos_fonts.cmake)

# La version del firmware sale de 'git describe --tags', pero NO se fija aca:
# lo hace el propio IDF, que la calcula en cada build y la mete en el descriptor
# de la app. aos_hal_firmware_version() la lee de ahi.
#
# Fijarla con PROJECT_VER parecia mas explicito y es peor: CMake la evaluaria en
# el configure y no en el build, asi que un commit o un tag posterior dejarian la
# version vieja pegada hasta el siguiente reconfigure. Se probo y pasa
# exactamente eso.

project(amoledos)

# Genera la tabla de simbolos que ven las apps dinamicas y habilita
# 'idf.py so' para compilar los .so de las apps.
include(elf_loader OPTIONAL)
