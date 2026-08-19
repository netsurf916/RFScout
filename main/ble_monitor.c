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

static const char *TAG = "ble_monitor";
static uint8_t own_addr_type;
static ble_observation_fn sink;

static void parse_adv(const struct ble_gap_disc_desc *d, rf_device_t *o)
{
    memset(o, 0, sizeof(*o));
    for (size_t i = 0; i < 6; ++i) o->address[i] = d->addr.val[5U - i];
    o->address_type = d->addr.type;
    o->last_source = RF_SOURCE_BLE;
    o->source_mask = RF_SOURCE_BIT(RF_SOURCE_BLE);
    o->rssi = d->rssi;
    o->first_seen_ms = o->last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    size_t off = 0;
    while (off < d->length_data) {
        uint8_t len = d->data[off];
        if (!len || off + 1U + len > d->length_data) break;
        uint8_t type = d->data[off + 1U];
        const uint8_t *data = &d->data[off + 2U];
        size_t n = len - 1U;
        if ((type == 0x08U || type == 0x09U) && n) {
            if (n >= sizeof(o->name)) n = sizeof(o->name) - 1U;
            memcpy(o->name, data, n); o->name[n] = 0; o->has_name = true;
        } else if (type == 0xFFU && n >= 2U) {
            o->company_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            n -= 2U;
            if (n > sizeof(o->manufacturer_data)) n = sizeof(o->manufacturer_data);
            memcpy(o->manufacturer_data, data + 2, n);
            o->manufacturer_data_len = (uint8_t)n;
            o->has_manufacturer_data = true;
        }
        off += (size_t)len + 1U;
    }
}

static void start_scan(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        rf_device_t o;
        parse_adv(&event->disc, &o);
        if (sink) sink(&o);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        start_scan();
    }
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params p = {
        .itvl = 0x0040, .window = 0x0030,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0, .passive = 1, .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc) ESP_LOGE(TAG, "scan start failed: %d", rc);
    else ESP_LOGI(TAG, "passive BLE monitor enabled");
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) ESP_LOGE(TAG, "identity setup failed: %d", rc);
    else start_scan();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_monitor_init(ble_observation_fn callback)
{
    sink = callback;
    int rc = nimble_port_init();
    if (rc) return ESP_FAIL;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
