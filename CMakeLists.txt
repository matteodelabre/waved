cmake_minimum_required(VERSION 3.7)
project(waved VERSION 0.1.0)

find_package(Threads REQUIRED)

add_executable(waved
    src/display.cpp
    src/file_descriptor.cpp
    src/main.cpp
    src/waveform_table.cpp
)

add_executable(waved-dump-wbf
    src/dump_wbf.cpp
    src/waveform_table.cpp
)

# Enable C++17 support
if(CMAKE_VERSION VERSION_LESS "3.8")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")
    else()
        message(FATAL_ERROR "Unknown compiler")
    endif()
else()
    set_property(TARGET waved waved-dump-wbf PROPERTY CXX_STANDARD 17)
endif()

# Disable optimizations in debug mode
if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
endif(CMAKE_COMPILER_IS_GNUCC)

target_link_libraries(waved Threads::Threads)
target_include_directories(waved PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)
