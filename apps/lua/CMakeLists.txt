cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lua)

include(${CMAKE_CURRENT_LIST_DIR}/../common.cmake)

# Genera lua.so en lugar de un firmware completo.
#
# project_so() junta los fuentes con file(GLOB_RECURSE "*.c") sobre el
# directorio del componente, asi que TODO lo que este bajo main/ -incluido
# main/lua/- entra en el .so sin listarlo. Por eso main/lua/ no puede tener
# lua.c ni luac.c: traen main() y son los ejecutables de escritorio.
# 32 bits: enteros de 'int' y numeros de 'float' en lugar de long long y
# double. No es una optimizacion, es la condicion para que esto entre: el S3
# tiene FPU de precision simple y NADA de doble, asi que cada suma en double
# la resuelve gcc llamando a __adddf3 y compania, que no estan en la tabla de
# simbolos del firmware. Medido en la primera compilacion de esta rama: sin
# esta bandera faltaban 45 simbolos (sin, cos, pow, floor... y una docena de
# helpers __*df3); con ella, los que quedan son los de la lista corta.
#
# Se pasa por -D y no editando luaconf.h para que el arbol de lua/ siga
# siendo el de lua.org salvo el #ifndef que hace posible este -D.
idf_build_set_property(COMPILE_DEFINITIONS "LUA_32BITS=1" APPEND)

# LUAI_MAXCCALLS es la guarda de Lua contra su propia recursion, y por omision
# vale 200. Medido en la placa el 2026-09-17: el parser gasta ~105 bytes de
# pila C por nivel de anidamiento, o sea que 200 niveles son ~21 KB. El task
# de LVGL tiene 16384 en total y le quedan unos 7 KB libres. La cuenta no da:
# con la guarda de fabrica, un guion con 68 parentesis anidados NO da error de
# Lua, pisa la pila y el reloj panica y se reinicia solo (verificado: 66
# sobrevive, 68 mata).
#
# Con 40, la guarda salta antes que la pila: 1000 parentesis anidados le dan
# al guion un "C stack overflow" como cualquier otro error de Lua, y el reloj
# sigue. Verificado en placa.
#
# Y no cuesta lo que parecia: este contador cuenta niveles de C, no llamadas
# de Lua. Una funcion que se llama a si misma 100 veces sigue andando con la
# guarda en 40, porque esas llamadas se resuelven adentro del bucle del VM
# sin apilar marcos de C. Medido, no supuesto.
idf_build_set_property(COMPILE_DEFINITIONS "LUAI_MAXCCALLS=40" APPEND)

include(elf_loader)
project_so(lua)
