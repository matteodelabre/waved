cmake_minimum_required(VERSION 3.7)
project(waved VERSION 0.1.0)

add_executable(waved
    src/main.cpp
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
    set_property(TARGET waved PROPERTY CXX_STANDARD 17)
endif()

target_include_directories(waved PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)
