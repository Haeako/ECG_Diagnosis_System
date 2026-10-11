#include "ecg_ble.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* 7b9a0001-6e6f-4e69-8d65-636700000001 */
static const ble_uuid128_t ecg_service_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x00, 0x67, 0x63, 0x65, 0x8d,
    0x69, 0x4e, 0x6f, 0x6e, 0x01, 0x00, 0x9a, 0x7b);
/* 7b9a0002-6e6f-4e69-8d65-636700000001 */
static const ble_uuid128_t ecg_data_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x00, 0x67, 0x63, 0x65, 0x8d,
    0x69, 0x4e, 0x6f, 0x6e, 0x02, 0x00, 0x9a, 0x7b);
/* 7b9a0003-6e6f-4e69-8d65-636700000001 */
static const ble_uuid128_t ecg_command_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x00, 0x67, 0x63, 0x65, 0x8d,
    0x69, 0x4e, 0x6f, 0x6e, 0x03, 0x00, 0x9a, 0x7b);

static const char *TAG = "ECG_BLE";
static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t data_handle;
static uint16_t command_handle;
static bool notify_enabled;
static uint32_t last_notify_ms;
static ecg_ble_command_cb_t command_callback;

/* Fixed 16-byte little-endian packet; fits the default ATT payload. */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int16_t filtered;
    int16_t corrected;
    uint16_t raw_adc;
    uint16_t bpm;
    uint16_t rr_interval_ms;
    uint8_t is_peak;
    uint8_t reserved;
} ecg_ble_packet_t;

static int ecg_ble_gap_event(struct ble_gap_event *event, void *arg);
static int ecg_ble_data_access(uint16_t conn_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);
static int ecg_ble_command_access(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg);
static ecg_ble_packet_t latest_packet;

void ble_store_config_init(void);

static const struct ble_gatt_svc_def ecg_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &ecg_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &ecg_data_uuid.u,
                .access_cb = ecg_ble_data_access,
                .val_handle = &data_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &ecg_command_uuid.u,
                .access_cb = ecg_ble_command_access,
                .val_handle = &command_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

static int ecg_ble_data_access(uint16_t connection_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    (void)connection_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return os_mbuf_append(ctxt->om, &latest_packet, sizeof(latest_packet)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool ecg_ble_parse_command(const char *text, ecg_ble_command_t *command)
{
    if (strcmp(text, "start") == 0) {
        *command = ECG_BLE_COMMAND_START;
        return true;
    }
    if (strcmp(text, "stop") == 0) {
        *command = ECG_BLE_COMMAND_STOP;
        return true;
    }
    return false;
}

static int ecg_ble_command_access(uint16_t connection_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg)
{
    (void)connection_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    char command_text[16];
    if (len == 0 || len >= sizeof(command_text)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, len, command_text) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    command_text[len] = '\0';

    ecg_ble_command_t command;
    if (!ecg_ble_parse_command(command_text, &command)) {
        ESP_LOGW(TAG, "Unknown command: %s", command_text);
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (command_callback != NULL) {
        command_callback(command);
    }
    return 0;
}

void ecg_ble_set_command_callback(ecg_ble_command_cb_t callback)
{
    command_callback = callback;
}

static void ecg_ble_advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    struct ble_gap_adv_params params = { 0 };
    const char *name = ble_svc_gap_device_name();
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&ecg_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot set advertising data: %d", rc);
        return;
    }

    struct ble_hs_adv_fields response_fields = { 0 };
    response_fields.name = (const uint8_t *)name;
    response_fields.name_len = strlen(name);
    response_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot set scan response data: %d", rc);
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(CONFIG_ECG_BLE_ADV_INTERVAL_MS);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(CONFIG_ECG_BLE_ADV_INTERVAL_MS + 10);
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, ecg_ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot start advertising: %d", rc);
    }
}

static int ecg_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
        } else {
            ecg_ble_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_enabled = false;
        ecg_ble_advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ecg_ble_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == data_handle) {
            notify_enabled = event->subscribe.cur_notify != 0;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void ecg_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "No usable BLE identity address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot determine BLE address: %d", rc);
        return;
    }
    ecg_ble_advertise();
}

static void ecg_ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

static void ecg_ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

esp_err_t ecg_ble_init(void)
{
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        return rc;
    }

    ble_hs_cfg.reset_cb = ecg_ble_on_reset;
    ble_hs_cfg.sync_cb = ecg_ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set(CONFIG_ECG_BLE_DEVICE_NAME);
    if (rc == 0) {
        rc = ble_gatts_count_cfg(ecg_gatt_services);
    }
    if (rc == 0) {
        rc = ble_gatts_add_svcs(ecg_gatt_services);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT initialization failed: %d", rc);
        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(ecg_ble_host_task, "NimBLE Host", 4096, NULL, 5,
                                NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "Cannot create NimBLE host task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void ecg_ble_notify(const ecg_record_t *sample)
{
    if (sample == NULL || !notify_enabled
        || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    /* Keep radio/host load bounded; SD still receives the full 360 Hz stream. */
    if (!sample->is_peak && (sample->timestamp_ms - last_notify_ms) < 40) {
        return;
    }
    last_notify_ms = sample->timestamp_ms;

    ecg_ble_packet_t packet = {
        /* Peak packets carry aligned R time; regular packets carry sample time. */
        .timestamp_ms = sample->is_peak && sample->r_peak_timestamp_ms != 0
            ? sample->r_peak_timestamp_ms : sample->timestamp_ms,
        .filtered = sample->filtered,
        .corrected = sample->corrected,
        .raw_adc = sample->raw_adc,
        .bpm = sample->bpm,
        .rr_interval_ms = sample->rr_interval_ms,
        .is_peak = sample->is_peak,
    };
    latest_packet = packet;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&packet, sizeof(packet));
    if (om != NULL) {
        int rc = ble_gatts_notify_custom(conn_handle, data_handle, om);
        (void)rc;
    }
}
