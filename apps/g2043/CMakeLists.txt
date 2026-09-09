cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(g2043)

include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)

include(elf_loader)
project_so(g2043)
