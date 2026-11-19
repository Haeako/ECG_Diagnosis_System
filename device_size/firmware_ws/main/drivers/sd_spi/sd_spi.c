#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "FreeRTOSConfig.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "sd_spi.h"

// SPI GPIO pins for SD card
#define SD_MOSI_PIN GPIO_NUM_23
#define SD_MISO_PIN GPIO_NUM_19
#define SD_CLK_PIN  GPIO_NUM_18
#define SD_CS_PIN   GPIO_NUM_5

static const char *TAG = "SD_SPI";
static sdmmc_card_t *card = NULL;

#define SD_FILE_PATH_MAX 64
#define SD_FILE_BUFFER_SIZE 4096

typedef struct {
    FILE *file;
    char path[SD_FILE_PATH_MAX];
    char buffer[SD_FILE_BUFFER_SIZE];
} sd_cached_file_t;

/* Keep the high-rate ECG stream and sparse event journal open independently.
 * Reopening a FAT file for every 16 samples cannot sustain a 360 Hz CSV. */
static sd_cached_file_t cached_files[2];

static void close_cached_file(sd_cached_file_t *cached)
{
    if (cached->file != NULL) {
        fflush(cached->file);
        fclose(cached->file);
        cached->file = NULL;
    }
    cached->path[0] = '\0';
}

static sd_cached_file_t *cached_file_for_path(const char *filename,
                                              bool log_errors)
{
    const char *basename = strrchr(filename, '/');
    size_t index = basename != NULL && basename[1] == 'E' ? 1U : 0U;
    sd_cached_file_t *cached = &cached_files[index];
    if (cached->file != NULL && strcmp(cached->path, filename) == 0) {
        return cached;
    }
    close_cached_file(cached);
    cached->file = fopen(filename, "ab");
    if (cached->file == NULL) {
        if (log_errors) {
            ESP_LOGE(TAG, "fopen(%s) failed: errno=%d (%s)", filename,
                     errno, strerror(errno));
        }
        return NULL;
    }
    (void)setvbuf(cached->file, cached->buffer, _IOFBF,
                  sizeof(cached->buffer));
    snprintf(cached->path, sizeof(cached->path), "%s", filename);
    return cached;
}

esp_err_t sd_spi_init(void)
{
    esp_err_t ret;

    if (card != NULL) {
        return ESP_OK;
    }

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
        return ret;
    }

    // Initialize SPI host driver
    ret = sdspi_host_init();
    if (ret != ESP_OK) {
        spi_bus_free(host.slot);
        return ret;
    }

    // Mount FAT filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        sdspi_host_deinit();
        spi_bus_free(host.slot);
        return ret;
    }

    // In thông tin SD card
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted successfully");

    return ESP_OK;
}

bool sd_spi_is_mounted(void)
{
    return card != NULL;
}

static esp_err_t sd_spi_write_internal(const char *filename, const void *data,
                                       size_t len, bool log_errors)
{
    if (card == NULL) {
        if (log_errors) ESP_LOGE(TAG, "SD card not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0) {
        if (log_errors) ESP_LOGE(TAG, "Invalid data or length");
        return ESP_ERR_INVALID_ARG;
    }

    sd_cached_file_t *cached = cached_file_for_path(filename, log_errors);
    if (cached == NULL) {
        if (log_errors) ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return ESP_FAIL;
    }
    
    // Ghi dữ liệu
    size_t written = fwrite(data, 1, len, cached->file);
    if (written != len) {
        if (log_errors) ESP_LOGE(TAG, "Failed to write data to file");
        close_cached_file(cached);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sd_spi_write(const char *filename, const void *data, size_t len)
{
    return sd_spi_write_internal(filename, data, len, true);
}

esp_err_t sd_spi_try_write(const char *filename, const void *data, size_t len)
{
    return sd_spi_write_internal(filename, data, len, false);
}

void sd_spi_close(void)
{
    close_cached_file(&cached_files[0]);
    close_cached_file(&cached_files[1]);

    if (card != NULL) {
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        card = NULL;
        
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        sdspi_host_deinit();
        spi_bus_free(host.slot);
    }
}
