/**
    ble_monitor.c : RFScout passive BLE collector
    Description: Normalizes BLE advertisements including names, manufacturer data, and services.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ble_monitor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <string.h>

#define BLE_AD_TYPE_UUID16_INCOMPLETE 0x02U
#define BLE_AD_TYPE_UUID16_COMPLETE 0x03U
#define BLE_AD_TYPE_SERVICE_DATA16 0x16U
#define BLE_AD_TYPE_NAME_SHORT 0x08U
#define BLE_AD_TYPE_NAME_COMPLETE 0x09U
#define BLE_AD_TYPE_MANUFACTURER 0xFFU
#define OPEN_DRONE_ID_UUID 0xFFFAU
#define OPEN_DRONE_ID_APP_CODE 0x0DU

static const char *TAG = "ble_monitor";
static uint8_t own_addr_type;
static ble_observation_fn sink;
static bool host_synced;
static bool scan_requested = true;
static bool scanning;

static void add_service_uuid(rf_device_t *o, uint16_t uuid)
{
    for (size_t i = 0; i < o->service_uuid_count; ++i) if (o->service_uuids[i] == uuid) return;
    if (o->service_uuid_count < RFSCOUT_BLE_SERVICE_UUID_MAX) o->service_uuids[o->service_uuid_count++] = uuid;
}

static void parse_adv(const struct ble_gap_disc_desc *d, rf_device_t *o)
{
    memset(o, 0, sizeof(*o));
    for (size_t i = 0; i < 6; ++i) o->address[i] = d->addr.val[5U - i];
    o->address_type = d->addr.type; o->last_source = RF_SOURCE_BLE; o->source_mask = RF_SOURCE_BIT(RF_SOURCE_BLE);
    o->rssi = d->rssi; o->first_seen_ms = o->last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    size_t offset = 0;
    while (offset < d->length_data) {
        const uint8_t length = d->data[offset];
        if (length == 0U || offset + 1U + length > d->length_data) break;
        const uint8_t type = d->data[offset + 1U]; const uint8_t *data = &d->data[offset + 2U]; size_t n = length - 1U;
        if ((type == BLE_AD_TYPE_NAME_SHORT || type == BLE_AD_TYPE_NAME_COMPLETE) && n > 0U) {
            if (n >= sizeof(o->name)) n = sizeof(o->name) - 1U; memcpy(o->name, data, n); o->name[n] = '\0'; o->has_name = true;
        } else if (type == BLE_AD_TYPE_UUID16_INCOMPLETE || type == BLE_AD_TYPE_UUID16_COMPLETE) {
            for (size_t i = 0; i + 1U < n; i += 2U) add_service_uuid(o, (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8U));
        } else if (type == BLE_AD_TYPE_SERVICE_DATA16 && n >= 2U) {
            const uint16_t uuid = (uint16_t)data[0] | ((uint16_t)data[1] << 8U); add_service_uuid(o, uuid);
            if (uuid == OPEN_DRONE_ID_UUID && n >= 3U && data[2] == OPEN_DRONE_ID_APP_CODE) o->has_remote_id = true;
        } else if (type == BLE_AD_TYPE_MANUFACTURER && n >= 2U) {
            o->company_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8U); n -= 2U;
            if (n > sizeof(o->manufacturer_data)) n = sizeof(o->manufacturer_data);
            memcpy(o->manufacturer_data, data + 2, n); o->manufacturer_data_len = (uint8_t)n; o->has_manufacturer_data = true;
        }
        offset += (size_t)length + 1U;
    }
}

static void start_scan(void);

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event->type == BLE_GAP_EVENT_DISC && scan_requested) {
        rf_device_t observation; parse_adv(&event->disc, &observation); if (sink != NULL) sink(&observation);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        scanning = false; if (scan_requested) start_scan();
    }
    return 0;
}

static void start_scan(void)
{
    if (!host_synced || !scan_requested || scanning) return;
    struct ble_gap_disc_params p = { .itvl = 0x0040, .window = 0x0030, .filter_policy = BLE_HCI_SCAN_FILT_NO_WL, .limited = 0, .passive = 1, .filter_duplicates = 0 };
    const int result = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (result != 0) ESP_LOGE(TAG, "scan start failed: %d", result);
    else { scanning = true; ESP_LOGI(TAG, "passive BLE monitor enabled"); }
}

static void on_sync(void)
{
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) result = ble_hs_id_infer_auto(0, &own_addr_type);
    if (result != 0) ESP_LOGE(TAG, "identity setup failed: %d", result);
    else { host_synced = true; start_scan(); }
}

static void host_task(void *argument) { (void)argument; nimble_port_run(); nimble_port_freertos_deinit(); }

esp_err_t ble_monitor_start(void)
{
    scan_requested = true;
    start_scan();
    return ESP_OK;
}

esp_err_t ble_monitor_stop(void)
{
    scan_requested = false;
    if (host_synced && scanning) {
        const int result = ble_gap_disc_cancel();
        if (result != 0 && result != BLE_HS_EALREADY) ESP_LOGW(TAG, "scan stop failed: %d", result);
        scanning = false;
    }
    ESP_LOGI(TAG, "BLE scan stopped for inactive app");
    return ESP_OK;
}

esp_err_t ble_monitor_init(ble_observation_fn callback)
{
    sink = callback;
    const int result = nimble_port_init();
    if (result != 0) return ESP_FAIL;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
