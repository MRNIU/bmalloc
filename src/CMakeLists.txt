# Copyright The bmalloc Contributors

add_library(${PROJECT_NAME} STATIC
        buddy.cpp
        bmalloc.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/3rd>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3;-Werror>
        $<$<CONFIG:Debug>:-O0;-g;-ggdb>
        -Wall
        -Wextra
        -ffreestanding
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
        BUDDY_PRINTF=\(void\)
)

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "include/bmalloc.h")
