cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Va ANTES de project(): despues, las flags de los componentes ya estan puestas.
include(${CMAKE_CURRENT_LIST_DIR}/components/aos_fonts/aos_fonts.cmake)

# The ELF goes out with every release (amoledos.elf.xz, since v0.14.0), and
# its debug information carried the build machine's paths: the user's home
# and folders, ~10,000 times. They are written relative instead: the project
# as ".", the IDF as "/IDF" (what CONFIG_COMPILER_HIDE_PATHS_MACROS already
# does for __FILE__), and anything else under the home as "~" (the
# toolchain's own headers). gdb and tools/coredump.sh need the symbols, not
# where the sources were. CONFIG_APP_REPRODUCIBLE_BUILD would do it too, and
# would also take the build date out of the firmware's description.
idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=$ENV{IDF_PATH}=/IDF" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fdebug-prefix-map=$ENV{HOME}=~" APPEND)

# La version del firmware sale de 'git describe --tags', pero NO se fija aca:
# lo hace el propio IDF, que la calcula en cada build y la mete en el descriptor
# de la app. aos_hal_firmware_version() la lee de ahi.
#
# Fijarla con PROJECT_VER parecia mas explicito y es peor: CMake la evaluaria en
# el configure y no en el build, asi que un commit o un tag posterior dejarian la
# version vieja pegada hasta el siguiente reconfigure. Se probo y pasa
# exactamente eso.

project(amoledos)

# RAM audit: building the IDF components that own IRAM code with -Os was
# measured at -4.5 K of IRAM in all (docs/RAM-AUDIT.md, section 7); not
# worth a bisection of its own, so it is not applied.

# Genera la tabla de simbolos que ven las apps dinamicas y habilita
# 'idf.py so' para compilar los .so de las apps.
include(elf_loader OPTIONAL)
