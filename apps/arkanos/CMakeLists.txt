cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(arkanos)

include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)

# Genera arkanos.so en lugar de un firmware completo.
include(elf_loader)
project_so(arkanos)
