# Copyright The bmalloc Contributors

cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

project(bmalloc)

if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    message(
            FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif ()

add_subdirectory(${PROJECT_SOURCE_DIR}/src)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif ()
