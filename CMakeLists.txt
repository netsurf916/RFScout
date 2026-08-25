cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rfscout)

option(RFSCOUT_FORMAT_CHECK "Run clang-format checks during normal builds" ON)

if(RFSCOUT_FORMAT_CHECK)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(
        rfscout-format-check ALL
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check_format.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking changed RFScout C sources with clang-format"
        VERBATIM
    )
endif()
