#ifndef WIFI_STA_H
#define WIFI_STA_H
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#define ESP_WIFI_SSID      CONFIG_ECG_MABUTRACE_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ECG_MABUTRACE_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  3
#define WIFI_AP_SSID       CONFIG_ECG_MABUTRACE_AP_SSID
#define WIFI_AP_PASS       CONFIG_ECG_MABUTRACE_AP_PASSWORD
#define ESP_WIFI_SAE_MODE  WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_CONFIG_NEW_BIT BIT2

bool wifi_init_sta(void);
void wifi_sta_stop(void);

#endif
