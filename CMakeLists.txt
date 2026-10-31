cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build only main, ESP-IDF's mandatory components, and declared dependencies.
# In particular, do not compile unrelated console, provisioning, or filesystem
# implementations just because they exist in the IDF checkout.
idf_build_set_property(MINIMAL_BUILD ON)
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
