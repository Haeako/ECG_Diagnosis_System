get_filename_component(ECG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(FIRMWARE "${ECG_ROOT}/firmware/esp32/main")
if(CMAKE_BUILD_EARLY_EXPANSION AND DEFINED ENV{REPLAY_VECTOR_DIR})
    set(REPLAY_VECTOR_DIR "$ENV{REPLAY_VECTOR_DIR}")
endif()
if(NOT REPLAY_VECTOR_DIR)
    set(REPLAY_VECTOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../vectors")
endif()
if(NOT EXISTS "${REPLAY_VECTOR_DIR}/replay_vectors.h")
    message(FATAL_ERROR "Run export_vectors.py --output <dir>, then set -DREPLAY_VECTOR_DIR=<dir>")
endif()
idf_component_register(
    SRCS "replay_main.c"
         "${FIRMWARE}/components/bpm/ExtractFeature.c"
         "${FIRMWARE}/components/filter/SBfilter.c"
         "${FIRMWARE}/components/af_model/af_runtime.c"
         "${FIRMWARE}/components/af_model/af_model_int8.c"
         "${FIRMWARE}/components/sqi_gate/sqi_runtime.c"
         "${FIRMWARE}/components/sqi_gate/sqi_orphanidou.c"
         "${FIRMWARE}/components/sqi_gate/sqi_template_matching.c"
    INCLUDE_DIRS "${FIRMWARE}" "${REPLAY_VECTOR_DIR}"
    PRIV_REQUIRES freertos log esp_timer esp_system esp_hw_support heap)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_add_binary_data(${COMPONENT_LIB} "${REPLAY_VECTOR_DIR}/replay_adc.bin" BINARY RENAME_TO replay_adc)
endif()
