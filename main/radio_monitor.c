#include "radio_monitor.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "radio_monitor";
static radio_observation_fn sink;
static uint8_t channel = 1;
static int64_t last_hop_us;

/* US 2.4 GHz regulatory domain: channels 1-11. Keep this explicit rather
   than silently listening on channels that may not be legal in the selected
   country. A future settings screen can derive this list from country config. */
static const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
#define CHANNEL_COUNT (sizeof(channels) / sizeof(channels[0]))
#define CHANNEL_DWELL_US 350000LL

static void emit_mac(const uint8_t mac[6], int8_t rssi, uint8_t ch)
{
    if (sink == NULL || (mac[0] & 0x01U)) return;
    rf_device_t o = {0};
    memcpy(o.address, mac, 6);
    o.address_type = 0;
    o.last_source = RF_SOURCE_WIFI;
    o.source_mask = RF_SOURCE_BIT(RF_SOURCE_WIFI);
    o.rssi = rssi;
    o.channel = ch;
    o.first_seen_ms = o.last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    sink(&o);
}

static void promiscuous(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == NULL || type == WIFI_PKT_MISC) return;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *f = pkt->payload;
    const uint16_t fc = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
    const uint8_t frame_type = (fc >> 2) & 0x3U;
    const uint8_t to_ds = (fc >> 8) & 1U;
    const uint8_t from_ds = (fc >> 9) & 1U;
    if (pkt->rx_ctrl.sig_len < 24 || frame_type == 1) return;

    /* Address 2 is transmitter for management and ordinary data frames. */
    emit_mac(f + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
    /* Management frames expose the peer/BSSID cheaply; data frames can expose
       another endpoint without attempting payload inspection. */
    if (frame_type == 0) {
        emit_mac(f + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
    } else if (frame_type == 2 && !(to_ds && from_ds)) {
        emit_mac(to_ds ? f + 4 : f + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
    }
}

esp_err_t radio_monitor_init(radio_observation_fn callback)
{
    sink = callback;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

    /* Explicitly select the US regulatory domain for this baseline build.
       This permits channels 1-11 and prevents accidental use of 12-14. */
    wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 11,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscuous));
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    last_hop_us = esp_timer_get_time();
    ESP_LOGI(TAG, "passive Wi-Fi monitor scanning channels 1-11 (%lld ms dwell)",
             CHANNEL_DWELL_US / 1000LL);
    return ESP_OK;
}

void radio_monitor_step(void)
{
    const int64_t now = esp_timer_get_time();
    if (now - last_hop_us < CHANNEL_DWELL_US) return;

    size_t i = 0;
    for (; i < CHANNEL_COUNT; ++i) {
        if (channels[i] == channel) break;
    }
    channel = channels[(i + 1U) % CHANNEL_COUNT];

    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unable to switch to channel %u: %s", channel, esp_err_to_name(err));
    }
    last_hop_us = now;
}
