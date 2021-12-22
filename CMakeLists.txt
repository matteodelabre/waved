# SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.7)
project(waved VERSION 0.1.0)

find_package(Threads REQUIRED)

# Option to enable perf reporting
option(ENABLE_PERF_REPORT "Report performance information" OFF)

if(ENABLE_PERF_REPORT)
    add_compile_definitions(ENABLE_PERF_REPORT)
endif()

# Enable C++17 support
if(CMAKE_VERSION VERSION_LESS "3.8")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")
    else()
        message(FATAL_ERROR "Unknown compiler")
    endif()
else()
    set(CMAKE_CXX_STANDARD 17)
endif()

# Disable optimizations in debug mode
if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
endif(CMAKE_COMPILER_IS_GNUCC)

# Main library
add_library(waved SHARED
    lib/display.cpp
    lib/file_descriptor.cpp
    lib/waveform_table.cpp
)
set_target_properties(waved PROPERTIES
    VERSION ${CMAKE_PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
target_link_libraries(waved Threads::Threads)
target_include_directories(waved PUBLIC lib)

# Demo program
add_executable(waved-demo src/demo/main.cpp)
target_link_libraries(waved-demo waved)

# WBF dump program
add_executable(waved-dump src/dump/main.cpp)
target_link_libraries(waved-dump waved)
