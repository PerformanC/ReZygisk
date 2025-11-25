project(external)

OPTION(LSPLT_BUILD_SHARED OFF)
add_subdirectory(lsplt/lsplt/src/main/jni)
add_subdirectory(csoloader)