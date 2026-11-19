cmake_minimum_required(VERSION 3.16)

# Options must be available before ESP-IDF resolves conditional dependencies.
include("${CMAKE_CURRENT_LIST_DIR}/main/cmake/EcgProjectOptions.cmake")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Compile only main and its explicit dependency closure.
idf_build_set_property(MINIMAL_BUILD ON)
if(DEFINED ECG_DEPENDENCIES_LOCK)
    idf_build_set_property(DEPENDENCIES_LOCK "${ECG_DEPENDENCIES_LOCK}")
endif()
project(ECG)
