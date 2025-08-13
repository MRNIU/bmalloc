# Copyright The bmalloc Contributors

project(
        bmalloc_test
)

# 查找GoogleTest
find_package(GTest REQUIRED)

add_executable(${PROJECT_NAME}
        # buddy_test.cpp
        # firstfit_test.cpp
        slab_test.cpp
        # slub_test.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -pedantic
        --coverage
        -fprofile-update=atomic
)

target_link_options(${PROJECT_NAME} PRIVATE
        --coverage
        -fsanitize=leak
        -fsanitize=address
        -fno-omit-frame-pointer
)

# 添加要链接的库
target_link_libraries(${PROJECT_NAME} PRIVATE
        bmalloc
        GTest::gtest
        GTest::gtest_main
)

add_dependencies(${PROJECT_NAME}
        bmalloc
)

# 添加测试到CTest
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
