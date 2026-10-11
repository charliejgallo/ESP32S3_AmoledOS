cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble)

include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)

# Builds ble.so instead of a whole firmware.
include(elf_loader)
project_so(ble)
