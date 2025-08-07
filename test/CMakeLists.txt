# Copyright The malloc Contributors

project(
        malloc_test
)

add_executable(${PROJECT_NAME}
        example.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -ffreestanding
)

target_link_options(${PROJECT_NAME} PRIVATE
        -nostartfiles
        -no-pie
)

# 添加要链接的库
target_link_libraries(${PROJECT_NAME} PRIVATE
        malloc
)

add_dependencies(${PROJECT_NAME}
        malloc
)
