idf_component_register(
    SRCS
        "main.c"
        "utils/app_recovery.c"
        "utils/user_button.c"
        "task/Core0/StateMachine/StateMachine.c"
        "task/Core1/ECGPipeline/ecg_pipeline.c"
        "task/Core1/ECGPipeline/ExFeature/ExtractFeature.c"
        "task/Core0/BLETransmit/ble_transmit.c"
        "components/filter/SBfilter.c"
        "components/af_model/af_model_int8.c"
        "components/af_model/af_runtime.c"
        "components/battery/battery.c"
        "drivers/adc/adc.c"
        "drivers/timer/timer.c"
        "drivers/sd_spi/sd_spi.c"
        "components/os/semaphore.c"
        "components/esp_ble/ecg_ble.c"
        "components/ring_buffer/ring_buffer.c"
        "components/metrics/pipeline_metrics.c"
        "components/sqi_gate/sqi_runtime.c"
        "components/sqi_gate/sqi_orphanidou.c"
        "components/sqi_gate/sqi_template_matching.c"
        "components/sqi_gate/sqi_bsqi.c"
    INCLUDE_DIRS "." "task/Core1/ECGPipeline"
                 "task/Core1/ECGPipeline/ExFeature"
                 "task/Core0/StateMachine" "task/Core0/BLETransmit"
    PRIV_REQUIRES bt nvs_flash esp_timer
                  esp_adc esp_driver_gpio esp_driver_gptimer
                  esp_driver_spi esp_driver_sdspi esp_ringbuf
                  fatfs sdmmc vfs
)
