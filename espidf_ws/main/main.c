#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>   /* offsetof */

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "components/network/wifi/wifi_sta.h"
#include "components/os/semaphore.h"

#include "drivers/adc/adc.h"
#include "drivers/timer/timer.h"
#include "drivers/sd_spi/sd_spi.h"

#include "components/filter/HPfilter.h"
#include "components/filter/Kalman.h"
#include "components/filter/LPfilter.h"
#include "components/filter/SBfilter.h"
#include "components/ring_buffer/ring_buffer.h"

// ─────────────────────────────────────────
// CONFIG
// ─────────────────────────────────────────
#define PC_IP           "192.168.0.244"
#define PC_PORT         5005
#define BATCH_SIZE      10
#define TCP_RETRY_DELAY 3000

// ─────────────────────────────────────────
// Structs (packed — khớp Python "<hHI" / "<IB3x")
// ─────────────────────────────────────────
typedef struct __attribute__((packed)) {
    int16_t  raw;
    int16_t  filtered;   /* Python dùng uint16 H — nếu muốn signed đổi thành int16 cả hai đầu */
    uint32_t ts_ms;
} ecg_sample_t;

typedef struct __attribute__((packed)) {
    uint32_t    seq;
    uint8_t     count;
    uint8_t     _pad[3];
    ecg_sample_t samples[BATCH_SIZE];
} tcp_packet_t;

// ─────────────────────────────────────────
// Global
// ─────────────────────────────────────────
static const char *TAG_ECG = "ECG";
static const char *TAG_TCP = "TCP";

SemaphoreHandle_t adc_timer_semaphore_handle = NULL;
adc_oneshot_unit_handle_t adc_handle   = NULL;
gptimer_handle_t          adc_timer_handle = NULL;

HPfilterType HPfilter;
SBfilterType SBfilter;
LPfilterType LPfilter;

/* BUG FIX 2: tên biến nhất quán — chọn buf_handle, bỏ extern buf_handler */
extern RingbufHandle_t buf_handle;

/* Consumer activity flags - prevent ADC from pushing if no consumer is active */
static volatile bool tcp_consumer_active = false;
static volatile bool sd_consumer_active = false;

// ─────────────────────────────────────────
// Filter pipeline
// ─────────────────────────────────────────
static int16_t filtering(int data)
{
    float f = (float)data;
    HPfilter_writeInput(&HPfilter, f);  f = HPfilter_readOutput(&HPfilter);
    SBfilter_writeInput(&SBfilter, f);  f = SBfilter_readOutput(&SBfilter);
    LPfilter_writeInput(&LPfilter, f);  f = LPfilter_readOutput(&LPfilter);
    return (int16_t)KFupdateEstimate(f);
}

// ─────────────────────────────────────────
// Task Core 1 — ADC + Filter → Ring Buffer
// ─────────────────────────────────────────
void ECG_Processing_Task(void *pvParameters)
{
    int raw = 0;
    ecg_sample_t sample;

    while (1) {
        if (xSemaphoreTake(adc_timer_semaphore_handle, portMAX_DELAY) == pdTRUE) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_Channel, &raw));

            sample.raw      = (int16_t)(raw - 2048);
            sample.filtered = filtering(raw - 2048);
            sample.ts_ms    = (uint32_t)(esp_timer_get_time() / 1000ULL);

            /* Only push to ring buffer if a consumer task is active */
            if (tcp_consumer_active || sd_consumer_active) {
                if (xRingbufferSend(buf_handle, &sample, sizeof(sample), 0) != pdTRUE) {
                    ESP_LOGW(TAG_ECG, "Ring buffer full, drop sample");
                }
            }
        }
    }
}

// ─────────────────────────────────────────
// Task Core 0 — TCP Send
// ─────────────────────────────────────────
void TCP_Send_Task(void *pvParameters)
{
    int sock = -1;
    struct sockaddr_in dest_addr = {
        .sin_addr.s_addr = inet_addr(PC_IP),
        .sin_family      = AF_INET,
        .sin_port        = htons(PC_PORT),
    };

    tcp_packet_t packet;
    uint32_t seq = 0;
    while (1) {
        // ── CONNECT ──────────────────────────
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG_TCP, "socket() failed: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY));
            continue;
        }

        ESP_LOGI(TAG_TCP, "Connecting to %s:%d...", PC_IP, PC_PORT);

        if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            ESP_LOGE(TAG_TCP, "connect() failed: %d", errno);
            close(sock);
            sock = -1;
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY));
            continue;
        }

        ESP_LOGI(TAG_TCP, "Connected");
        tcp_consumer_active = true;  /* Signal ADC that TCP consumer is active */

        // ── SEND LOOP ────────────────────────
        while (1) {
            packet.seq   = seq++;
            packet.count = 0;

            for (int i = 0; i < BATCH_SIZE; i++) {
                size_t item_size;
                ecg_sample_t *ptr = (ecg_sample_t *)xRingbufferReceive(
                    buf_handle, &item_size, pdMS_TO_TICKS(1000));

                if (ptr == NULL) break;   /* timeout → gửi packet nhỏ hơn */

                memcpy(&packet.samples[i], ptr, sizeof(ecg_sample_t));
                packet.count++;
                vRingbufferReturnItem(buf_handle, ptr);
            }

            if (packet.count == 0) continue;

            /* BUG FIX 3: dùng offsetof thay vì tính tay — luôn đúng dù struct thay đổi */
            int to_send = (int)(offsetof(tcp_packet_t, samples)
                                + packet.count * sizeof(ecg_sample_t));

            if (send(sock, &packet, to_send, 0) < 0) {
                ESP_LOGE(TAG_TCP, "send() failed → reconnect");
                break;
            }
        }

        // ── CLEANUP ──────────────────────────
        tcp_consumer_active = false;  /* Signal ADC that TCP consumer is inactive */
        close(sock);
        sock = -1;
        ESP_LOGW(TAG_TCP, "Disconnected → retry in %d ms...", TCP_RETRY_DELAY);
        vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY));
    }
}

// ─────────────────────────────────────────
// Task — SD Write (ghi 100 samples vào SD)
// ─────────────────────────────────────────
void SD_Write_Task(void *pvParameters)
{
    const char *filename = "/sdcard/ecg_data.bin";
    tcp_packet_t sd_packet;
    uint32_t sd_seq = 0;

    // Chờ trước khi bắt đầu ghi
    vTaskDelay(pdMS_TO_TICKS(2000));
    sd_consumer_active = true;  /* Signal ADC that SD consumer is active */

    while (1) {
        sd_packet.seq = sd_seq++;
        sd_packet.count = 0;

        // Lấy BATCH_SIZE samples từ ring buffer
        for (int i = 0; i < BATCH_SIZE; i++) {
            size_t item_size;
            ecg_sample_t *ptr = (ecg_sample_t *)xRingbufferReceive(
                buf_handle, &item_size, pdMS_TO_TICKS(500));

            if (ptr == NULL) break;

            memcpy(&sd_packet.samples[i], ptr, sizeof(ecg_sample_t));
            sd_packet.count++;
            vRingbufferReturnItem(buf_handle, ptr);
        }

        if (sd_packet.count > 0) {
            int to_write = (int)(offsetof(tcp_packet_t, samples)
                                 + sd_packet.count * sizeof(ecg_sample_t));

            if (sd_spi_write(filename, &sd_packet, to_write) != ESP_OK) {
                ESP_LOGW(TAG_ECG, "SD write failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Tránh chiếm CPU 100%
    }
}

// ─────────────────────────────────────────
// app_main
// ─────────────────────────────────────────
void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Peripherals & Filters (khởi tạo TRƯỚC wifi để giảm thời gian chờ)
    ADC_Init();
    HPfilter_init(&HPfilter);
    SBfilter_init(&SBfilter);
    LPfilter_init(&LPfilter);
    KalmanFilter_init(1.0f, 1.0f, 0.001f);

    Semaphore_Init(&adc_timer_semaphore_handle);
    Timer_Init();
    ring_buffer_init();

    // SD Card
    ESP_LOGI(TAG_ECG, "Initializing SD card...");
    if (sd_spi_init() != ESP_OK) {
        ESP_LOGE(TAG_ECG, "SD card failed — TCP only mode");
    }
    else
    {
        xTaskCreatePinnedToCore(SD_Write_Task,        "SD_Task",  4096, NULL, 2, NULL, 0);
    }

    // WiFi — BUG FIX 1: check return value, không spawn TCP task nếu fail
    ESP_LOGI(TAG_TCP, "Connecting WiFi...");
    if (!wifi_init_sta()) {
        ESP_LOGE(TAG_TCP, "WiFi failed — halting. Check SSID/password.");
        /* Reboot sau 5 giây thay vì treo vô ích */
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }
    else
    {
        xTaskCreatePinnedToCore(TCP_Send_Task,        "TCP_Task", 4096, NULL, 3, NULL, 0);
    }
    ESP_LOGI(TAG_TCP, "WiFi OK");
    // Tasks
    xTaskCreatePinnedToCore(ECG_Processing_Task, "ECG_Task", 4096, NULL, 5, NULL, 1);
}