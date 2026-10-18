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
#include "components/sqi_gate/sqi_runtime.h"

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
static ecg_ble_command_cb_t command_callback;

/* Both packet types fit the default 20-byte ATT payload. */
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t type;
    uint8_t count;
    uint8_t sample_period_ms;
    uint16_t sequence;
    uint32_t first_timestamp_ms;
    int16_t samples[5];
} ecg_ble_batch_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t sqi_rule_mask;
    uint32_t timestamp_ms;
    uint16_t bpm;
    uint16_t rr_interval_ms;
    uint8_t sqi_score;
    uint8_t sqi_status;
    uint16_t af_probability_x10000;
    uint16_t af_score;
    uint16_t af_event_id;
} ecg_ble_event_packet_t;

#define ECG_BLE_PROTOCOL_VERSION 1U
#define ECG_BLE_PACKET_BATCH     1U
#define ECG_BLE_PACKET_EVENT     2U
#define ECG_BLE_BATCH_SAMPLES    5U
#define ECG_BLE_EVENT_R_PEAK     (1U << 0)
#define ECG_BLE_EVENT_AF_RAW     (1U << 1)
#define ECG_BLE_EVENT_AF_CONFIRM (1U << 2)
#define ECG_BLE_EVENT_AF_NEW     (1U << 3)
#define ECG_BLE_EVENT_BAD_SQI    (1U << 4)

_Static_assert(sizeof(ecg_ble_batch_packet_t) == 20U, "BLE batch must fit ATT");
_Static_assert(sizeof(ecg_ble_event_packet_t) == 20U, "BLE event must fit ATT");

static int ecg_ble_gap_event(struct ble_gap_event *event, void *arg);
static int ecg_ble_data_access(uint16_t conn_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);
static int ecg_ble_command_access(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg);
static uint8_t latest_packet[20];
static uint8_t latest_packet_len;
static ecg_ble_batch_packet_t pending_batch;
static uint16_t batch_sequence;
static uint32_t last_batch_sample_ms;
static uint32_t last_sqi_event_window = UINT32_MAX;
static uint32_t last_af_event_window = UINT32_MAX;

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

    return os_mbuf_append(ctxt->om, latest_packet, latest_packet_len) == 0
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
            ESP_LOGW(TAG, "Central connected handle=%u",
                     (unsigned)conn_handle);
        } else {
            ESP_LOGW(TAG, "Connect attempt failed status=%d",
                     event->connect.status);
            ecg_ble_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Central disconnected reason=%d",
                 event->disconnect.reason);
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

static void ecg_ble_send_packet(const void *packet, uint8_t length)
{
    memcpy(latest_packet, packet, length);
    latest_packet_len = length;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, length);
    if (om != NULL) {
        int rc = ble_gatts_notify_custom(conn_handle, data_handle, om);
        (void)rc;
    }
}

void ecg_ble_notify(const ecg_record_t *sample)
{
    if (sample == NULL || !notify_enabled
        || conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

    if (last_batch_sample_ms == 0U
        || (sample->timestamp_ms - last_batch_sample_ms) >= 10U) {
        last_batch_sample_ms = sample->timestamp_ms;
        if (pending_batch.count == 0U) {
            pending_batch.version = ECG_BLE_PROTOCOL_VERSION;
            pending_batch.type = ECG_BLE_PACKET_BATCH;
            pending_batch.sequence = batch_sequence++;
            pending_batch.first_timestamp_ms = sample->timestamp_ms;
            pending_batch.sample_period_ms = 10U;
        }
        if (pending_batch.count < ECG_BLE_BATCH_SAMPLES) {
            pending_batch.samples[pending_batch.count++] = sample->corrected;
        }
        if (pending_batch.count == ECG_BLE_BATCH_SAMPLES) {
            ecg_ble_send_packet(&pending_batch, sizeof(pending_batch));
            memset(&pending_batch, 0, sizeof(pending_batch));
        }
    }

    bool sqi_changed = sample->sqi_status != ECG_SQI_INSUFFICIENT
        && sample->sqi_window_id != last_sqi_event_window;
    bool af_changed = sample->af_valid
        && sample->af_window_id != last_af_event_window;
    if (!sample->is_peak && !sqi_changed && !af_changed
        && !sample->af_new_event) return;

    ecg_ble_event_packet_t event = {
        .version = ECG_BLE_PROTOCOL_VERSION,
        .type = ECG_BLE_PACKET_EVENT,
        .sqi_rule_mask = sample->sqi_rule_mask,
        .timestamp_ms = sample->is_peak && sample->r_peak_timestamp_ms != 0U
            ? sample->r_peak_timestamp_ms : sample->timestamp_ms,
        .bpm = sample->bpm,
        .rr_interval_ms = sample->rr_interval_ms,
        .sqi_score = sample->sqi_score,
        .sqi_status = sample->sqi_status,
        .af_probability_x10000 = sample->af_probability_x10000,
        .af_score = sample->af_score,
        .af_event_id = sample->af_event_id,
    };
    if (sample->is_peak) event.flags |= ECG_BLE_EVENT_R_PEAK;
    if (sample->af_raw_prediction) event.flags |= ECG_BLE_EVENT_AF_RAW;
    if (sample->af_confirmed) event.flags |= ECG_BLE_EVENT_AF_CONFIRM;
    if (sample->af_new_event) event.flags |= ECG_BLE_EVENT_AF_NEW;
    if (sample->sqi_status == ECG_SQI_UNUSABLE) {
        event.flags |= ECG_BLE_EVENT_BAD_SQI;
    }
    ecg_ble_send_packet(&event, sizeof(event));
    if (sqi_changed) last_sqi_event_window = sample->sqi_window_id;
    if (af_changed) last_af_event_window = sample->af_window_id;
}
