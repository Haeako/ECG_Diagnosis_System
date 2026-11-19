# Resolve custom options before project() invokes the ESP-IDF Component Manager.
include("${CMAKE_CURRENT_LIST_DIR}/EcgLegacyOptions.cmake")

# Minimal benchmark builds have a smaller Kconfig tree. Give them their own
# configuration file per selected module.
get_filename_component(_ecg_project_dir "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_ecg_select_sdkconfig OFF)
if(NOT SDKCONFIG)
    set(_ecg_select_sdkconfig ON)
elseif(ECG_AUTO_SDKCONFIG)
    get_filename_component(_ecg_sdkconfig "${SDKCONFIG}" ABSOLUTE BASE_DIR "${_ecg_project_dir}")
    get_filename_component(_ecg_previous_sdkconfig "${ECG_AUTO_SDKCONFIG}" ABSOLUTE BASE_DIR "${_ecg_project_dir}")
    file(TO_CMAKE_PATH "${_ecg_sdkconfig}" _ecg_sdkconfig)
    file(TO_CMAKE_PATH "${_ecg_previous_sdkconfig}" _ecg_previous_sdkconfig)
    if(WIN32)
        string(TOLOWER "${_ecg_sdkconfig}" _ecg_sdkconfig)
        string(TOLOWER "${_ecg_previous_sdkconfig}" _ecg_previous_sdkconfig)
    endif()
    if(_ecg_sdkconfig STREQUAL _ecg_previous_sdkconfig)
        set(_ecg_select_sdkconfig ON)
    endif()
endif()
if(_ecg_select_sdkconfig)
    set(_ecg_selected_sdkconfig "${CMAKE_BINARY_DIR}/sdkconfig.${ECG_BUILD_PROFILE}")
    set(SDKCONFIG "${_ecg_selected_sdkconfig}" CACHE FILEPATH "Selected profile's configuration file" FORCE)
    set(ECG_AUTO_SDKCONFIG "${SDKCONFIG}" CACHE INTERNAL "Last automatically selected SDKCONFIG" FORCE)
else()
    # An explicit custom SDKCONFIG stays selected across profile changes.
    unset(ECG_AUTO_SDKCONFIG CACHE)
endif()

# IDF's dependency scan is a separate process; its environment carries the same
# resolved profile as the actual component registration.
foreach(_ecg_option ECG_BUILD_PROFILE ECG_ENABLE_TFLM QRS_COMMON_INPUT_DIR)
    set(ENV{${_ecg_option}} "${${_ecg_option}}")
endforeach()
if(ECG_ENABLE_TFLM)
    set(ENV{ECG_BUILD_WITH_TFLM} "ON")
else()
    set(ENV{ECG_BUILD_WITH_TFLM} "OFF")
endif()
