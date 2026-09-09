idf_component_register(
    SRCS "afinador.c" "af_dsp.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)

# Las apps se compilan con -Og (CONFIG_COMPILER_OPTIMIZATION_DEBUG, el valor de
# fabrica del IDF) mientras que el firmware usa -O2. Para la UI da igual; para
# el lazo del NSDF no: medido en la placa, es una parte grande de los 284 ms
# que tardaba el analisis. Se sube la optimizacion SOLO del archivo que
# calcula, para no engordar el .text de toda la app.
set_source_files_properties(af_dsp.c
    PROPERTIES COMPILE_OPTIONS "-O2"
    DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
