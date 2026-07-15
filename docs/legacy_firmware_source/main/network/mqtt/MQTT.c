#include "MQTT.h"

esp_mqtt_client_handle_t mqtt_client = NULL;
static const char *TAG = "MQTT";
volatile bool mqtt_is_connected = false;

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe_single(mqtt_client, TOPIC, 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        mqtt_is_connected = true;
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_is_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        msg_id = esp_mqtt_client_publish(client, "topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error tAype: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_init(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = URI,        // "wss://mqtt.ecgproject.site"
                .port = PORT,      // 443
                .path = MQTT_PATH, // "/api/v1/mqtt" 
            },
            .verification = {
                // Tự động dùng bộ chứng chỉ có sẵn trong ESP32 để tin cậy Cloudflare
                // Không cần file .pem thủ công
                .crt_bundle_attach = esp_crt_bundle_attach,
            },
        },
        .credentials = {
            .username = ACCESS_TOKEN, // Token ThingsBoard
        },
        .session = {
            .keepalive = 60, // Giữ kết nối mỗi 60s
            .disable_clean_session = 0,
        },
        .network = {
            .reconnect_timeout_ms = 1000, // Thử kết nối lại sau 1s nếu mất mạng
            .timeout_ms = 5000,
        },
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    subscribe_single_topic(TOPIC);
}
void publish(const char *topic, const char *data)
{
    if (mqtt_client && mqtt_is_connected) esp_mqtt_client_publish(mqtt_client, topic, data, strlen(data), 0, 0);
}

void subscribe_single_topic(const char *topic)
{
    if (mqtt_client && mqtt_is_connected) esp_mqtt_client_subscribe_single(mqtt_client, topic, 1);
}

void unsubscribe_single_topic(const char *topic)
{
    if (mqtt_client && mqtt_is_connected) esp_mqtt_client_unsubscribe(mqtt_client, topic);
}