option(SQI_COST_DISABLE "Build without SQI for linked-size baseline" OFF)
option(SQI_BENCHMARK_ENABLE "Boot into the synthetic SQI cycle benchmark" OFF)
option(SQI_BENCHMARK_XQRS_ONLY "Boot into the full bSQI/XQRS benchmark" OFF)
option(SQI_BENCHMARK_LOGISTIC_FULL_ONLY "Boot into full Logistic+detectors benchmark" OFF)
option(SQI_BENCHMARK_KRAMER_FULL_ONLY "Boot into full Kramer benchmark" OFF)
option(SQI_COST_BSQI_ONLY "Link only the bSQI scoring kernel for size measurement" OFF)
option(SQI_COST_LOGISTIC_ONLY "Link only the logistic decision kernel for size measurement" OFF)
option(SQI_COST_LOGISTIC_FULL_ONLY "Link Logistic five-SQI plus XQRS for full cost" OFF)
option(SQI_COST_ZHAO_ONLY "Link only the Zhao-simple waveform kernel for size measurement" OFF)
option(SQI_COST_KRAMER_ONLY "Link only the Kramer rule kernel for size measurement" OFF)
option(SQI_COST_KRAMER_FULL_ONLY "Link full Kramer pipeline for size measurement" OFF)

if((SQI_COST_BSQI_ONLY AND (SQI_COST_LOGISTIC_ONLY OR SQI_COST_LOGISTIC_FULL_ONLY)) OR
   (SQI_COST_LOGISTIC_ONLY AND SQI_COST_LOGISTIC_FULL_ONLY) OR
   (SQI_COST_BSQI_ONLY AND SQI_COST_ZHAO_ONLY) OR
   ((SQI_COST_LOGISTIC_ONLY OR SQI_COST_LOGISTIC_FULL_ONLY) AND SQI_COST_ZHAO_ONLY) OR
   ((SQI_COST_KRAMER_ONLY OR SQI_COST_KRAMER_FULL_ONLY) AND (SQI_COST_BSQI_ONLY OR SQI_COST_LOGISTIC_ONLY OR SQI_COST_LOGISTIC_FULL_ONLY OR SQI_COST_ZHAO_ONLY)) OR
   (SQI_COST_KRAMER_ONLY AND SQI_COST_KRAMER_FULL_ONLY))
    message(FATAL_ERROR "Select at most one SQI cost-only target")
endif()

set(ECG_SRCS "main.c" "utils/app_recovery.c" "utils/user_button.c"
             "task/Core0/StateMachine/StateMachine.c"
             "task/Core1/ECGPipeline/ecg_pipeline.c"
             "task/Core1/ECGPipeline/ExFeature/ExtractFeature.c"
             "task/Core0/BLETransmit/ble_transmit.c"
             "components/filter/SBfilter.c" "components/af_model/af_model_int8.c"
             "components/af_model/af_runtime.c"
             "drivers/adc/adc.c" "drivers/timer/timer.c" "drivers/sd_spi/sd_spi.c" "components/os/semaphore.c"
             "components/esp_ble/ecg_ble.c" "components/ring_buffer/ring_buffer.c")

if(SQI_COST_DISABLE)
    add_compile_definitions(SQI_COST_DISABLE=1)
else()
    list(APPEND ECG_SRCS
         "components/sqi_gate/sqi_runtime.c"
         "components/sqi_gate/sqi_orphanidou.c"
         "components/sqi_gate/sqi_template_matching.c"
         "components/sqi_gate/sqi_bsqi.c")
endif()

if(SQI_COST_BSQI_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_BSQI=1)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_bsqi.c"
                         "components/sqi_gate/sqi_xqrs.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
elseif(SQI_COST_LOGISTIC_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_LOGISTIC=1)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_gate_logistic.c"
                         "components/sqi_gate/sqi_logistic_runtime.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
elseif(SQI_COST_LOGISTIC_FULL_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_LOGISTIC_FULL=1
                            SQI_LOGISTIC_MAX_SAMPLES=2500)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_gate_logistic.c"
                         "components/sqi_gate/sqi_logistic_runtime.c"
                         "components/sqi_gate/sqi_xqrs.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
elseif(SQI_COST_ZHAO_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_ZHAO=1)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_zhao.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
elseif(SQI_COST_KRAMER_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_KRAMER=1)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_kramer.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
elseif(SQI_COST_KRAMER_FULL_ONLY)
    add_compile_definitions(SQI_COST_ANCHOR_ENABLE=1 SQI_COST_ANCHOR_KRAMER_FULL=1)
    list(APPEND ECG_SRCS "components/sqi_gate/sqi_kramer.c"
                         "components/sqi_gate/sqi_kramer_full.c"
                         "components/sqi_gate/sqi_cost_anchor.c")
endif()

if(SQI_BENCHMARK_KRAMER_FULL_ONLY)
    add_compile_definitions(SQI_BENCHMARK_ENABLE=1)
    list(APPEND ECG_SRCS
         "components/sqi_gate/sqi_kramer_full_device_benchmark.c"
         "components/sqi_gate/sqi_kramer.c"
         "components/sqi_gate/sqi_kramer_full.c")
elseif(SQI_BENCHMARK_LOGISTIC_FULL_ONLY)
    add_compile_definitions(SQI_BENCHMARK_ENABLE=1 SQI_LOGISTIC_MAX_SAMPLES=2500)
    list(APPEND ECG_SRCS
         "components/sqi_gate/sqi_logistic_full_device_benchmark.c"
         "components/sqi_gate/sqi_gate_logistic.c"
         "components/sqi_gate/sqi_logistic_runtime.c"
         "components/sqi_gate/sqi_xqrs.c")
elseif(SQI_BENCHMARK_XQRS_ONLY)
    add_compile_definitions(SQI_BENCHMARK_ENABLE=1)
    list(APPEND ECG_SRCS
         "components/sqi_gate/sqi_xqrs_device_benchmark.c"
         "components/sqi_gate/sqi_bsqi.c"
         "components/sqi_gate/sqi_xqrs.c")
elseif(SQI_BENCHMARK_ENABLE)
    add_compile_definitions(SQI_BENCHMARK_ENABLE=1)
    list(APPEND ECG_SRCS
         "components/sqi_gate/sqi_device_benchmark.c"
         "components/sqi_gate/sqi_gate_logistic.c"
         "components/sqi_gate/sqi_logistic_runtime.c"
         "components/sqi_gate/sqi_zhao.c"
         "components/sqi_gate/sqi_kramer.c"
         "components/sqi_gate/sqi_xqrs.c")
endif()

idf_component_register(SRCS ${ECG_SRCS}
                    INCLUDE_DIRS "." "task/Core1/ECGPipeline"
                                 "task/Core1/ECGPipeline/ExFeature"
                                 "task/Core0/StateMachine"
                                 "task/Core0/BLETransmit"
                    PRIV_REQUIRES bt nvs_flash esp_timer
                                  esp_adc esp_driver_gpio esp_driver_gptimer
                                  esp_driver_spi esp_driver_sdspi esp_ringbuf
                                  fatfs sdmmc vfs)
