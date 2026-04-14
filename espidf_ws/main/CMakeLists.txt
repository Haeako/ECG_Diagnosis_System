idf_component_register(SRCS "components/filter/HPfilter.c" "components/filter/LPfilter.c" "components/filter/SBfilter.c" "components/filter/Kalman.c" "drivers/adc/adc.c" "drivers/timer/timer.c" "drivers/sd_spi/sd_spi.c" "components/os/semaphore.c" "main.c"
                    "components/network/wifi/wifi_sta.c" "components/network/wifi/wifi_sta.h" "components/ring_buffer/ring_buffer.c"
                    INCLUDE_DIRS ".")
