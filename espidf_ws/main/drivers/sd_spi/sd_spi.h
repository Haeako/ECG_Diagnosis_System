#ifndef __SD_SPI_H__
#define __SD_SPI_H__

#include <stdbool.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <stdio.h>

/**
 * Khởi tạo SD card qua SPI
 * @return ESP_OK nếu thành công
 */
esp_err_t sd_spi_init(void);

/** Trả về true khi thẻ đã được mount. */
bool sd_spi_is_mounted(void);

/**
 * Ghi dữ liệu vào file trên SD
 * @param filename: tên file (vd: "/sdcard/ecg_data.bin")
 * @param data: con trỏ dữ liệu
 * @param len: độ dài dữ liệu (bytes)
 * @return ESP_OK nếu thành công
 */
esp_err_t sd_spi_write(const char *filename, const void *data, size_t len);

/**
 * Đóng file SD
 */
void sd_spi_close(void);

#endif
