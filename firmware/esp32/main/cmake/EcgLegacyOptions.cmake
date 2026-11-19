# Compatibility for existing benchmark scripts. New builds should select one
# ECG_BUILD_PROFILE and use the ECG_ENABLE_* options instead.
include_guard(GLOBAL)

set(_ecg_legacy_profile_options
    "AF_MODEL_BENCHMARK_ENABLE|af-extra-trees"
    "AF_MODEL_BENCHMARK_LOGISTIC_ONLY|af-logistic"
    "AF_MODEL_BENCHMARK_RF_ONLY|af-random-forest"
    "QRS_BENCHMARK_PAN_PYECG_ONLY|qrs-pan"
    "QRS_BENCHMARK_ELGENDI_PYECG_ONLY|qrs-elgendi"
    "QRS_BENCHMARK_COMMON_INPUT|qrs-common"
    "QRS_HAN_CNN_TFLM_SMOKE|qrs-han-cnn"
    "SQI_BENCHMARK_ENABLE|sqi-synthetic"
    "SQI_BENCHMARK_XQRS_ONLY|sqi-xqrs"
    "SQI_BENCHMARK_LOGISTIC_FULL_ONLY|sqi-logistic"
    "SQI_BENCHMARK_KRAMER_FULL_ONLY|sqi-kramer"
    "SQI_BENCHMARK_KRAMER_RULES_ONLY|sqi-kramer-rules"
    "SQI_BENCHMARK_HO_ONLY|sqi-ho"
    "SQI_BENCHMARK_CLIFFORD_ONLY|sqi-clifford"
    "SQI_BENCHMARK_ZHAO_ONLY|sqi-zhao"
    "SQI_BENCHMARK_ORPHANIDOU_ONLY|sqi-orphanidou")

# An OFF legacy flag is inert, including an old OFF value left in CMakeCache.
# Never silently choose one of several active flags through elseif order.
function(_ecg_resolve_legacy_selection mappings canonical description)
    set(_active_options)
    set(_selected_value)
    foreach(_mapping IN LISTS ${mappings})
        string(REPLACE "|" ";" _pair "${_mapping}")
        list(GET _pair 0 _flag)
        list(GET _pair 1 _value)
        if(DEFINED ${_flag} AND ${_flag})
            list(APPEND _active_options "${_flag}")
            set(_selected_value "${_value}")
        endif()
    endforeach()

    list(LENGTH _active_options _active_count)
    if(_active_count GREATER 1)
        message(FATAL_ERROR
            "Conflicting ${description} options: ${_active_options}. "
            "Select one with -D${canonical}=<value> and turn the old options OFF.")
    endif()
    if(_active_count EQUAL 1)
        if(DEFINED ${canonical} AND NOT "${${canonical}}" STREQUAL "${_selected_value}")
            message(FATAL_ERROR
                "${_active_options}=ON selects ${canonical}=${_selected_value}, "
                "but ${canonical}=${${canonical}} is already set. "
                "Turn the legacy flag OFF or select the matching ${canonical}.")
        endif()
        message(DEPRECATION
            "${_active_options}=ON is deprecated; use -D${canonical}=${_selected_value}.")
        set(${canonical} "${_selected_value}" PARENT_SCOPE)
    endif()
endfunction()

macro(ecg_resolve_build_options)
    _ecg_resolve_legacy_selection(_ecg_legacy_profile_options ECG_BUILD_PROFILE "benchmark")
    foreach(_removed ECG_ENABLE_SQI ECG_SQI_COST_PROFILE SQI_COST_DISABLE
            SQI_COST_BSQI_ONLY SQI_COST_LOGISTIC_ONLY SQI_COST_LOGISTIC_FULL_ONLY
            SQI_COST_ZHAO_ONLY SQI_COST_KRAMER_ONLY SQI_COST_KRAMER_FULL_ONLY)
        if(DEFINED ${_removed})
            message(FATAL_ERROR "${_removed} was removed. Use a fresh build directory and select an isolated module profile.")
        endif()
    endforeach()

    if(DEFINED QRS_WIM_XIANG_TFLM_BENCHMARK AND QRS_WIM_XIANG_TFLM_BENCHMARK)
        if(NOT ECG_BUILD_PROFILE STREQUAL "qrs-common")
            message(FATAL_ERROR
                "QRS_WIM_XIANG_TFLM_BENCHMARK=ON requires the qrs-common build profile. "
                "Use -DECG_BUILD_PROFILE=qrs-common -DECG_ENABLE_TFLM=ON.")
        endif()
        if(DEFINED ECG_ENABLE_TFLM AND NOT ECG_ENABLE_TFLM)
            message(FATAL_ERROR
                "QRS_WIM_XIANG_TFLM_BENCHMARK=ON conflicts with ECG_ENABLE_TFLM=OFF. "
                "Use -DECG_ENABLE_TFLM=ON and turn the legacy flag OFF.")
        endif()
        message(DEPRECATION
            "QRS_WIM_XIANG_TFLM_BENCHMARK=ON is deprecated; use -DECG_ENABLE_TFLM=ON.")
        set(ECG_ENABLE_TFLM ON)
    endif()

    if(NOT DEFINED ECG_BUILD_PROFILE)
        set(ECG_BUILD_PROFILE bpm-runtime)
    endif()
    set(ECG_BUILD_PROFILE "${ECG_BUILD_PROFILE}" CACHE STRING "Isolated algorithm benchmark to build")
    set(_ecg_build_profiles
        bpm-runtime bpm-candidates bpm-fast-qrs af-runtime af-extra-trees af-logistic af-random-forest
        qrs-pan qrs-elgendi qrs-common qrs-han-cnn
        sqi-synthetic sqi-xqrs sqi-logistic sqi-kramer sqi-kramer-rules
        sqi-ho sqi-clifford sqi-zhao sqi-orphanidou)
    set_property(CACHE ECG_BUILD_PROFILE PROPERTY STRINGS ${_ecg_build_profiles})
    if(NOT ECG_BUILD_PROFILE IN_LIST _ecg_build_profiles)
        message(FATAL_ERROR
            "Unknown ECG_BUILD_PROFILE='${ECG_BUILD_PROFILE}'. Valid profiles: ${_ecg_build_profiles}")
    endif()

    if(NOT DEFINED ECG_ENABLE_TFLM)
        if(ECG_BUILD_PROFILE STREQUAL "qrs-han-cnn")
            set(ECG_ENABLE_TFLM ON)
        else()
            set(ECG_ENABLE_TFLM OFF)
        endif()
    endif()
    set(ECG_ENABLE_TFLM "${ECG_ENABLE_TFLM}" CACHE BOOL "Include the selected profile's TensorFlow Lite Micro model")
    if(ECG_ENABLE_TFLM AND NOT ECG_BUILD_PROFILE STREQUAL "qrs-common"
                       AND NOT ECG_BUILD_PROFILE STREQUAL "qrs-han-cnn")
        message(FATAL_ERROR "ECG_ENABLE_TFLM is supported by qrs-common and qrs-han-cnn only.")
    endif()
    if(ECG_BUILD_PROFILE STREQUAL "qrs-han-cnn" AND NOT ECG_ENABLE_TFLM)
        message(FATAL_ERROR "The qrs-han-cnn profile requires -DECG_ENABLE_TFLM=ON.")
    endif()

    if(NOT QRS_COMMON_INPUT_DIR AND DEFINED ENV{QRS_COMMON_INPUT_DIR})
        set(QRS_COMMON_INPUT_DIR "$ENV{QRS_COMMON_INPUT_DIR}")
    endif()
    set(QRS_COMMON_INPUT_DIR "${QRS_COMMON_INPUT_DIR}" CACHE PATH "Directory containing qrs_common_test_vectors.h")
endmacro()

# Resolve before project() as well as during isolated component discovery.
ecg_resolve_build_options()
