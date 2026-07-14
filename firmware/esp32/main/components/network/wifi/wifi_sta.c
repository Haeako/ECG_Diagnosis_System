#include "wifi_sta.h"

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_http_server = NULL;
static const char *TAG = "wifi";
static int s_retry_num = 0;
static bool s_wifi_ready = false;
static bool s_wifi_stop_requested = false;
static char s_ssid[33] = ESP_WIFI_SSID;
static char s_pass[65] = ESP_WIFI_PASS;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_stop_requested) {
            return;
        }
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry %d/%d", s_retry_num, ESP_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "STA connection attempt failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;

    if (dst_size == 0) {
        return;
    }

    while (*src != '\0' && out < dst_size - 1) {
        if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int high = hex_value(src[1]);
            int low = hex_value(src[2]);
            if (high >= 0 && low >= 0) {
                dst[out++] = (char)((high << 4) | low);
                src += 3;
                continue;
            }
        }
        dst[out++] = *src == '+' ? ' ' : *src;
        src++;
    }
    dst[out] = '\0';
}

static void form_get_value(const char *body, const char *key,
                           char *dst, size_t dst_size)
{
    const char *field = strstr(body, key);
    const char *end;
    size_t len;

    if (field == NULL || dst_size == 0) {
        return;
    }
    field += strlen(key);
    if (*field != '=') {
        return;
    }
    field++;
    end = strchr(field, '&');
    len = end == NULL ? strlen(field) : (size_t)(end - field);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    char encoded[96];
    if (len >= sizeof(encoded)) {
        len = sizeof(encoded) - 1;
    }
    memcpy(encoded, field, len);
    encoded[len] = '\0';
    url_decode(dst, dst_size, encoded);
}

static void wifi_load_config(void)
{
    nvs_handle_t nvs;
    size_t ssid_len = sizeof(s_ssid);
    size_t pass_len = sizeof(s_pass);

    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    if (nvs_get_str(nvs, "ssid", s_ssid, &ssid_len) != ESP_OK || s_ssid[0] == '\0') {
        strlcpy(s_ssid, ESP_WIFI_SSID, sizeof(s_ssid));
    }
    if (nvs_get_str(nvs, "pass", s_pass, &pass_len) != ESP_OK) {
        strlcpy(s_pass, ESP_WIFI_PASS, sizeof(s_pass));
    }
    nvs_close(nvs);
}

static void wifi_save_config(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;

    if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "cannot open NVS for WiFi config");
        return;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "pass", pass));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ECG WiFi</title></head><body>"
        "<form method='post' action='/wifi'>"
        "<h3>ECG WiFi Setup</h3>"
        "<input name='ssid' placeholder='SSID' maxlength='32' required><br>"
        "<input name='pass' placeholder='Password' maxlength='64' type='password'><br>"
        "<button type='submit'>Connect</button>"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char body[160] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    form_get_value(body, "ssid", s_ssid, sizeof(s_ssid));
    form_get_value(body, "pass", s_pass, sizeof(s_pass));
    if (s_ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    wifi_save_config(s_ssid, s_pass);
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONFIG_NEW_BIT);
    ESP_LOGI(TAG, "new WiFi config received for SSID:%s", s_ssid);
    return httpd_resp_sendstr(req, "Saved. Device is connecting...");
}

static void http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    httpd_uri_t wifi = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };

    if (s_http_server != NULL) {
        return;
    }
    config.server_port = 80;
    ESP_ERROR_CHECK(httpd_start(&s_http_server, &config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &wifi));
}

static void http_server_stop(void)
{
    if (s_http_server != NULL) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
}

static void wifi_stop_if_started(void)
{
    esp_err_t ret = esp_wifi_stop();

    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_ERROR_CHECK(ret);
    }
}

static void wifi_init_once(void)
{
    if (s_wifi_ready) {
        return;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    s_wifi_ready = true;
    (void)s_sta_netif;
    (void)s_ap_netif;
}

static bool wifi_try_sta(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CONFIG_NEW_BIT);
    s_retry_num = 0;
    strlcpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_pass, sizeof(wifi_config.sta.password));

    s_wifi_stop_requested = true;
    wifi_stop_if_started();
    s_wifi_stop_requested = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to SSID:%s", s_ssid);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void wifi_start_config_ap(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASS,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    wifi_stop_if_started();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    http_server_start();
    ESP_LOGW(TAG, "config AP started: SSID:%s PASS:%s URL:http://192.168.4.1/",
             WIFI_AP_SSID, WIFI_AP_PASS);
}

bool wifi_init_sta(void)
{
    wifi_init_once();
    wifi_load_config();

    while (1) {
        if (wifi_try_sta()) {
            http_server_stop();
            ESP_LOGI(TAG, "connected to SSID:%s", s_ssid);
            return true;
        }

        ESP_LOGW(TAG, "failed to connect SSID:%s after %d retries, starting config AP",
                 s_ssid, ESP_MAXIMUM_RETRY);
        wifi_start_config_ap();
        return false;
    }
}

void wifi_sta_stop(void)
{
    if (!s_wifi_ready) {
        return;
    }

    http_server_stop();
    s_wifi_stop_requested = true;
    wifi_stop_if_started();
    ESP_LOGI(TAG, "WiFi stopped");
}
