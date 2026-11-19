# One profile selects an entry point and its kernels. Feature switches only add
# their own sources, compile definitions and component dependencies.
# IDF discovers dependencies in a separate cmake -P process which does not
# forward arbitrary cache entries. The project exports these resolved options.
if(CMAKE_BUILD_EARLY_EXPANSION)
    foreach(_option ECG_BUILD_PROFILE ECG_ENABLE_TFLM QRS_COMMON_INPUT_DIR)
        if(DEFINED ENV{${_option}})
            set(${_option} "$ENV{${_option}}")
        endif()
    endforeach()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/EcgLegacyOptions.cmake")

set(ECG_SRCS)
set(ECG_COMPILE_DEFINITIONS)
set(ECG_EMBED_FILES)
set(ECG_PRIVATE_REQUIRES freertos log esp_timer)
set(ECG_INCLUDE_DIRS "." "components/bpm")

set(_ecg_qrs_runtime
    "components/bpm/ExtractFeature.c"
    "components/filter/SBfilter.c")
set(_ecg_orphanidou_runtime
    "components/sqi_gate/sqi_runtime.c"
    "components/sqi_gate/sqi_orphanidou.c"
    "components/sqi_gate/sqi_template_matching.c")
set(_ecg_sqi_runtime ${_ecg_orphanidou_runtime}
    "components/sqi_gate/sqi_bsqi.c")
set(_ecg_logistic_kernel
    "components/sqi_gate/sqi_gate_logistic.c"
    "components/sqi_gate/sqi_logistic_runtime.c")

# Each declaration contributes only when its profile is selected. Keep benchmark
# timing routines in their existing files; the catalog only controls linkage.
macro(_ecg_profile name)
    if(ECG_BUILD_PROFILE STREQUAL "${name}")
        cmake_parse_arguments(_profile "" "" "SRCS;DEFINITIONS;REQUIRES;EMBED_FILES;INCLUDE_DIRS" ${ARGN})
        list(APPEND ECG_SRCS ${_profile_SRCS})
        list(APPEND ECG_COMPILE_DEFINITIONS ${_profile_DEFINITIONS})
        list(APPEND ECG_PRIVATE_REQUIRES ${_profile_REQUIRES})
        list(APPEND ECG_EMBED_FILES ${_profile_EMBED_FILES})
        list(APPEND ECG_INCLUDE_DIRS ${_profile_INCLUDE_DIRS})
    endif()
endmacro()

if(NOT ECG_BUILD_PROFILE STREQUAL "qrs-han-cnn" AND NOT ECG_BUILD_PROFILE STREQUAL "bpm-fast-qrs")
    list(APPEND ECG_SRCS "benchmarks/benchmark_main.c")
endif()

_ecg_profile(bpm-runtime
    SRCS "benchmarks/runtime_benchmark.c" ${_ecg_qrs_runtime}
    DEFINITIONS ECG_RUNTIME_BENCHMARK=1 ECG_MODULE=1)
_ecg_profile(bpm-fast-qrs
    SRCS "benchmarks/fast_qrs_benchmark.c" "components/qrs_detector/fast_qrs_port.c")
if(ECG_BUILD_PROFILE STREQUAL "bpm-fast-qrs")
    if(NOT EXISTS "${QRS_COMMON_INPUT_DIR}/bpm_candidates_vectors.h")
        message(FATAL_ERROR "bpm-fast-qrs requires QRS_COMMON_INPUT_DIR containing bpm_candidates_vectors.h")
    endif()
    list(APPEND ECG_INCLUDE_DIRS "${QRS_COMMON_INPUT_DIR}")
endif()
_ecg_profile(bpm-candidates
    SRCS "benchmarks/bpm_candidates_benchmark.c" ${_ecg_qrs_runtime}
         "components/qrs_detector/pan_pyecg.c" "components/qrs_detector/elgendi_pyecg.c"
         "components/qrs_detector/fast_qrs_port.c" "components/qrs_detector/pan_tompkins_plus_plus_port.c"
         "components/qrs_detector/wim_nguyen.c" "components/qrs_detector/wim_elgendi.c"
         "components/qrs_detector/wim_pantompkins.c" "components/qrs_detector/wim_hamilton.c"
         "components/qrs_detector/wim_arteaga_falconi.c" "components/qrs_detector/wim_shaik.c"
         "components/qrs_detector/wim_zhai.c"
    DEFINITIONS BPM_CANDIDATES_BENCHMARK=1)
if(ECG_BUILD_PROFILE STREQUAL "bpm-candidates")
    if(NOT EXISTS "${QRS_COMMON_INPUT_DIR}/bpm_candidates_vectors.h")
        message(FATAL_ERROR "bpm-candidates requires QRS_COMMON_INPUT_DIR containing bpm_candidates_vectors.h")
    endif()
    list(APPEND ECG_INCLUDE_DIRS "${QRS_COMMON_INPUT_DIR}")
endif()
_ecg_profile(af-runtime
    SRCS "benchmarks/runtime_benchmark.c" "components/af_model/af_runtime.c"
         "components/af_model/af_model_int8.c"
    DEFINITIONS ECG_RUNTIME_BENCHMARK=1 ECG_MODULE=3)

_ecg_profile(af-extra-trees
    SRCS "components/af_model/af_model_esp32_benchmark.c" "components/af_model/af_model_int8.c"
    DEFINITIONS AF_MODEL_BENCHMARK_ENABLE=1)
_ecg_profile(af-logistic
    SRCS "components/af_model/af_model_candidates_benchmark.c" "components/af_model/af_model_logistic_int8.c"
    DEFINITIONS AF_MODEL_CANDIDATE_BENCHMARK_ENABLE=1 AF_MODEL_BENCHMARK_LOGISTIC=1)
_ecg_profile(af-random-forest
    SRCS "components/af_model/af_model_candidates_benchmark.c" "components/af_model/af_model_rf_int8.c"
    DEFINITIONS AF_MODEL_CANDIDATE_BENCHMARK_ENABLE=1 AF_MODEL_BENCHMARK_RF=1)

_ecg_profile(qrs-pan
    SRCS "components/qrs_detector/pan_pyecg_device_benchmark.c" "components/qrs_detector/pan_pyecg.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(qrs-elgendi
    SRCS "components/qrs_detector/elgendi_pyecg_device_benchmark.c" "components/qrs_detector/elgendi_pyecg.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(qrs-common
    SRCS "components/qrs_detector/qrs_common_input_device_benchmark.c" ${_ecg_qrs_runtime}
         "components/qrs_detector/pan_pyecg.c" "components/qrs_detector/elgendi_pyecg.c"
         "components/qrs_detector/wim_nguyen.c" "components/qrs_detector/wim_elgendi.c"
         "components/qrs_detector/wim_hamilton.c" "components/qrs_detector/wim_arteaga_falconi.c"
         "components/qrs_detector/wim_pantompkins.c" "components/qrs_detector/pan_tompkins_plus_plus_port.c"
         "components/qrs_detector/wim_shaik.c" "components/qrs_detector/wim_zhai.c"
         "components/qrs_detector/wim_xia.c" "components/qrs_detector/fast_qrs_port.c"
         "components/qrs_detector/gqrs_wfdb_port.c" "components/sqi_gate/sqi_xqrs.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1 QRS_COMMON_INPUT_BENCHMARK=1)
if(ECG_BUILD_PROFILE STREQUAL "qrs-common")
    if(NOT EXISTS "${QRS_COMMON_INPUT_DIR}/qrs_common_test_vectors.h")
        message(FATAL_ERROR
            "qrs-common requires generated vectors. Set -DQRS_COMMON_INPUT_DIR=<directory> "
            "containing qrs_common_test_vectors.h (or set the QRS_COMMON_INPUT_DIR environment variable).")
    endif()
    list(APPEND ECG_INCLUDE_DIRS "${QRS_COMMON_INPUT_DIR}")
    if(ECG_ENABLE_TFLM)
        list(APPEND ECG_SRCS "components/qrs_detector/wim_xiang_tflm.cpp")
        list(APPEND ECG_COMPILE_DEFINITIONS QRS_WIM_XIANG_TFLM_BENCHMARK=1)
        list(APPEND ECG_EMBED_FILES "components/qrs_detector/wim_xiang_int8.tflite")
    endif()
endif()
_ecg_profile(qrs-han-cnn
    SRCS "components/qrs_detector/han_cnn_tflm_smoke.cpp"
    EMBED_FILES "components/qrs_detector/han_cnn_int8.tflite"
    REQUIRES esp_system)
if(ECG_ENABLE_TFLM)
    list(APPEND ECG_PRIVATE_REQUIRES espressif__esp-tflite-micro)
endif()

_ecg_profile(sqi-synthetic
    SRCS "components/sqi_gate/sqi_device_benchmark.c" ${_ecg_qrs_runtime}
         ${_ecg_sqi_runtime} ${_ecg_logistic_kernel}
         "components/sqi_gate/sqi_zhao.c" "components/sqi_gate/sqi_kramer.c"
         "components/sqi_gate/sqi_xqrs.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-xqrs
    SRCS "components/sqi_gate/sqi_xqrs_device_benchmark.c"
         "components/sqi_gate/sqi_xqrs.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-logistic
    SRCS "components/sqi_gate/sqi_logistic_full_device_benchmark.c"
         ${_ecg_logistic_kernel} "components/sqi_gate/sqi_xqrs.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1 SQI_LOGISTIC_MAX_SAMPLES=2500)
_ecg_profile(sqi-kramer
    SRCS "components/sqi_gate/sqi_kramer_full_device_benchmark.c"
         "components/sqi_gate/sqi_kramer.c" "components/sqi_gate/sqi_kramer_full.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-kramer-rules
    SRCS "components/sqi_gate/sqi_kramer_rules_benchmark.c" "components/sqi_gate/sqi_kramer.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-ho
    SRCS "components/sqi_gate/sqi_ho_device_benchmark.c" "components/sqi_gate/sqi_ho.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-clifford
    SRCS "components/sqi_gate/sqi_clifford_device_benchmark.c" "components/sqi_gate/sqi_clifford.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-zhao
    SRCS "components/sqi_gate/sqi_zhao_device_benchmark.c" "components/sqi_gate/sqi_zhao.c"
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)
_ecg_profile(sqi-orphanidou
    SRCS "components/sqi_gate/sqi_orphanidou_device_benchmark.c" ${_ecg_orphanidou_runtime}
    DEFINITIONS SQI_BENCHMARK_ENABLE=1)

list(REMOVE_DUPLICATES ECG_SRCS)
list(REMOVE_DUPLICATES ECG_COMPILE_DEFINITIONS)
list(REMOVE_DUPLICATES ECG_PRIVATE_REQUIRES)
list(REMOVE_DUPLICATES ECG_INCLUDE_DIRS)
message(STATUS "ECG module profile: ${ECG_BUILD_PROFILE}; TFLM: ${ECG_ENABLE_TFLM}")
