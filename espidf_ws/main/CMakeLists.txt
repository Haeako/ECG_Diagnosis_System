idf_component_register(SRCS "main.c" "ecg_pipeline.c" "ecg_signal.c" "components/filter/HPfilter.c" "components/filter/LPfilter.c" "components/filter/SBfilter.c" "components/filter/Kalman.c" "components/afib/afib_detector.c" "drivers/adc/adc.c" "drivers/timer/timer.c" "drivers/sd_spi/sd_spi.c" "components/os/semaphore.c"
                    "components/ble/ecg_ble.c" "components/ring_buffer/ring_buffer.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES bt nvs_flash esp_timer
                                  esp_adc esp_driver_gpio esp_driver_gptimer
                                  esp_driver_spi esp_driver_sdspi esp_ringbuf
                                  fatfs sdmmc vfs)
