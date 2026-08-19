cmake_minimum_required(VERSION 3.16)

project(twig VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TWIG_ENABLE_ASAN "Enable AddressSanitizer and UBSan" OFF)

enable_testing()

add_executable(twig
    src/main.cpp
    src/repository.cpp
)

target_include_directories(twig
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(twig_test
    tests/repository_test.cpp
    src/repository.cpp
)

target_include_directories(twig_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_test(NAME twig_repository_test COMMAND twig_test)

if(TWIG_ENABLE_ASAN)
    if(MSVC)
        target_compile_options(twig PRIVATE /fsanitize=address)
        target_link_options(twig PRIVATE /fsanitize=address)
        target_compile_options(twig_test PRIVATE /fsanitize=address)
        target_link_options(twig_test PRIVATE /fsanitize=address)
    else()
        target_compile_options(twig PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(twig PRIVATE -fsanitize=address,undefined)
        target_compile_options(twig_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(twig_test PRIVATE -fsanitize=address,undefined)
    endif()
endif()

if(MSVC)
    target_compile_options(twig PRIVATE /W4)
    target_compile_options(twig_test PRIVATE /W4)
else()
    target_compile_options(twig PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(twig_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
