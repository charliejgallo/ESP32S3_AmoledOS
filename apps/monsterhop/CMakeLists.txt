cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(monsterhop)

include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)

# Genera monsterhop.so en lugar de un firmware completo.
include(elf_loader)
project_so(monsterhop)
