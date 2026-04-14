#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"

#include "sd_spi.h"

// SPI GPIO pins for SD card
#define SD_MOSI_PIN GPIO_NUM_23
#define SD_MISO_PIN GPIO_NUM_19
#define SD_CLK_PIN  GPIO_NUM_18
#define SD_CS_PIN   GPIO_NUM_5

static const char *TAG = "SD_SPI";
static sdmmc_card_t *card = NULL;
static FILE *sd_file = NULL;

esp_err_t sd_spi_init(void)
{
    esp_err_t ret;

    // Cấu hình cho SPI host
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = host.slot;

    // Configure SPI bus - initialize bus with GPIO pins
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Initialize SPI host driver
    ret = sdspi_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD SPI host (%s)", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    // Mount FAT filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
        }
        sdspi_host_deinit();
        spi_bus_free(host.slot);
        return ret;
    }

    // In thông tin SD card
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted successfully");

    // Test write to verify functionality
    const char *test_file = "/sdcard/test.txt";
    const char *test_content = "Hello from ESP";
    FILE *test_fp = fopen(test_file, "w");
    if (test_fp != NULL) {
        size_t written = fwrite(test_content, 1, strlen(test_content), test_fp);
        fclose(test_fp);
        if (written == strlen(test_content)) {
            ESP_LOGI(TAG, "Test file created successfully: %s", test_file);
        } else {
            ESP_LOGW(TAG, "Test file write incomplete: %d/%d bytes", written, strlen(test_content));
        }
    } else {
        ESP_LOGW(TAG, "Failed to create test file");
    }

    return ESP_OK;
}

esp_err_t sd_spi_write(const char *filename, const void *data, size_t len)
{
    if (card == NULL) {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }

    // Mở file ở chế độ append binary
    sd_file = fopen(filename, "ab");
    if (sd_file == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return ESP_FAIL;
    }

    // Ghi dữ liệu
    size_t written = fwrite(data, 1, len, sd_file);
    if (written != len) {
        ESP_LOGE(TAG, "Failed to write data to file");
        fclose(sd_file);
        sd_file = NULL;
        return ESP_FAIL;
    }

    // Đóng file
    fclose(sd_file);
    sd_file = NULL;

    ESP_LOGI(TAG, "Wrote %d bytes to %s", written, filename);
    return ESP_OK;
}

void sd_spi_close(void)
{
    if (sd_file != NULL) {
        fclose(sd_file);
        sd_file = NULL;
    }

    if (card != NULL) {
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        card = NULL;
        
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        sdspi_host_deinit();
        spi_bus_free(host.slot);
        ESP_LOGI(TAG, "SD card unmounted and SPI bus freed");
    }
}
