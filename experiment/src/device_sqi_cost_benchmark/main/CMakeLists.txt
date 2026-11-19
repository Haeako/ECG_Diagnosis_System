get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(SQI "${REPO}/firmware/esp32/main/components/sqi_gate")
idf_component_register(
    SRCS "sqi_cost_main.c" "${SQI}/sqi_xqrs.c" "${SQI}/sqi_bsqi.c"
         "${SQI}/sqi_logistic_runtime.c" "${SQI}/sqi_gate_logistic.c"
    INCLUDE_DIRS "${SQI}"
    PRIV_REQUIRES freertos log esp_timer)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SQI_LOGISTIC_MAX_SAMPLES=2500)
endif()
