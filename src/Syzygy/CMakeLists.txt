# Vendored Fathom (src/Syzygy/fathom) plus the Sigma bridge. Warnings are off for the vendored code only.
add_library(fathom STATIC fathom/tbprobe.c SyzygyGlue.c)
target_include_directories(fathom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fathom)
target_compile_definitions(fathom PRIVATE TB_NO_THREADS)
set_source_files_properties(fathom/tbprobe.c PROPERTIES COMPILE_OPTIONS -w)
