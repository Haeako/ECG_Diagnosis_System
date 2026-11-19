cmake_minimum_required(VERSION 3.16)
project(ecg_algorithm_modules C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(KERNELS "${CMAKE_CURRENT_LIST_DIR}/../esp32/main")

# Each target owns its state. No peripheral, RTOS, or other algorithm dependency.
add_library(ecg_bpm STATIC "${KERNELS}/components/bpm/ExtractFeature.c"
                           "${KERNELS}/components/filter/SBfilter.c")
add_library(ecg_sqi STATIC "${KERNELS}/components/sqi_gate/sqi_runtime.c"
                           "${KERNELS}/components/sqi_gate/sqi_orphanidou.c"
                           "${KERNELS}/components/sqi_gate/sqi_template_matching.c")
add_library(ecg_afib STATIC "${KERNELS}/components/af_model/af_runtime.c"
                            "${KERNELS}/components/af_model/af_model_int8.c")
set(module_id 0)
include(CTest)
foreach(module bpm sqi afib)
    math(EXPR module_id "${module_id} + 1")
    target_include_directories(ecg_${module} PUBLIC "${KERNELS}")
    if(NOT MSVC)
        target_link_libraries(ecg_${module} PUBLIC m)
    endif()
    add_executable(profile_${module} profile_module.c)
    target_compile_definitions(profile_${module} PRIVATE ECG_MODULE=${module_id})
    target_link_libraries(profile_${module} PRIVATE ecg_${module})
    if(BUILD_TESTING)
        add_executable(test_${module} test_module.c)
        target_compile_definitions(test_${module} PRIVATE ECG_MODULE=${module_id})
        target_link_libraries(test_${module} PRIVATE ecg_${module})
        add_test(NAME ${module}_contract COMMAND test_${module})
        add_test(NAME ${module}_profile COMMAND profile_${module} --repeat 2)
        set_tests_properties(${module}_profile PROPERTIES PASS_REGULAR_EXPRESSION "calls=72000")
    endif()
endforeach()
