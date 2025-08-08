# Copyright The bmalloc Contributors

add_library(${PROJECT_NAME} STATIC
        buddy.cpp
        # first_fit.cpp
        # slab.cpp
        bmalloc.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3;-Werror>
        -Wall
        -Wextra
        -ffreestanding
)

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "include/bmalloc.h")
