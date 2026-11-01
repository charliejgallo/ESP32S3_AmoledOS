cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(face_kaleido)

# The apps' shared configuration: the same includes, the same LVGL.
include(${CMAKE_CURRENT_LIST_DIR}/../../apps/common.cmake)

# Builds face_kaleido.so, a watchface module (docs/FACES.md).
include(elf_loader)
project_so(face_kaleido)
