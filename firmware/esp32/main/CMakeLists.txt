# Profile selection is shared with the project before ESP-IDF resolves dependencies.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/EcgProfiles.cmake")

idf_component_register(
    SRCS ${ECG_SRCS}
    EMBED_FILES ${ECG_EMBED_FILES}
    INCLUDE_DIRS ${ECG_INCLUDE_DIRS}
    PRIV_REQUIRES ${ECG_PRIVATE_REQUIRES}
)

# Dependency discovery evaluates this file in a separate CMake script process.
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${ECG_COMPILE_DEFINITIONS})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ECG_PROFILE_NAME="${ECG_BUILD_PROFILE}")
message(STATUS "ECG profile: ${ECG_BUILD_PROFILE}; TFLM: ${ECG_ENABLE_TFLM}")
