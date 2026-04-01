cmake_minimum_required(VERSION 3.10)

project(mgrep LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()


add_library(mgrep_config INTERFACE)

target_include_directories(mgrep_config INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(mgrep_config INTERFACE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wpedantic
    -Wunreachable-code
    -Wredundant-decls
    -Wno-unused-parameter
    -Wno-unused-variable
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
)

add_library(mgrep_library
    src/threads.cpp
)
target_link_libraries(mgrep_library
    PUBLIC mgrep_config
)

add_executable(mgrep
    src/main.cpp
)

target_link_libraries(mgrep 
    PRIVATE mgrep_library
)

add_executable(tests
    testing/thread-pool-testing.cpp
)

target_include_directories( tests PRIVATE
    ${CMAKE_SOURCE_DIR}/external
)

target_link_libraries(tests
    PRIVATE mgrep_library 
)
