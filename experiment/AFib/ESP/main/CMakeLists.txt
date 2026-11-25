idf_component_register(
    SRCS "main.c" "components/af_model/af_model_esp32_benchmark.c" "components/af_model/af_model_int8.c"
    INCLUDE_DIRS "." "components/af_model"
    PRIV_REQUIRES freertos log esp_timer heap)
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE AF_MODEL_BENCHMARK_ENABLE=1)
