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
static uint8_t max_channel = 14;
static int64_t last_hop_us;
static rf_wifi_region_t region = RF_WIFI_REGION_EXTENDED_RX;
static bool initialized;
static bool running;

#define CHANNEL_DWELL_US 350000LL

static void emit_mac( const uint8_t mac[ 6 ], int8_t rssi, uint8_t ch )
{
    if( sink == NULL || ( mac[ 0 ] & 0x01U ) )
        return;
    rf_device_t o = { 0 };
    memcpy( o.address, mac, 6 );
    o.address_type = 0;
    o.last_source = RF_SOURCE_WIFI;
    o.source_mask = RF_SOURCE_BIT( RF_SOURCE_WIFI );
    o.rssi = rssi;
    o.channel = ch;
    o.first_seen_ms = o.last_seen_ms = (uint32_t)( esp_timer_get_time() / 1000ULL );
    sink( &o );
}

static void promiscuous( void *buf, wifi_promiscuous_pkt_type_t type )
{
    if( !running || buf == NULL || type == WIFI_PKT_MISC )
        return;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *f = pkt->payload;
    const uint16_t fc = (uint16_t)f[ 0 ] | ( (uint16_t)f[ 1 ] << 8 );
    const uint8_t frame_type = ( fc >> 2 ) & 0x3U;
    const uint8_t to_ds = ( fc >> 8 ) & 1U;
    const uint8_t from_ds = ( fc >> 9 ) & 1U;
    if( pkt->rx_ctrl.sig_len < 24 || frame_type == 1 )
        return;
    emit_mac( f + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
    if( frame_type == 0 )
        emit_mac( f + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
    else if( frame_type == 2 && !( to_ds && from_ds ) )
        emit_mac( to_ds ? f + 4 : f + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
}

const char *radio_monitor_region_name( rf_wifi_region_t value )
{
    switch( value )
    {
        case RF_WIFI_REGION_US:
            return "US (1-11)";
        case RF_WIFI_REGION_ETSI:
            return "ETSI (1-13)";
        case RF_WIFI_REGION_JAPAN:
            return "Japan (1-14)";
        case RF_WIFI_REGION_EXTENDED_RX:
            return "Extended RX (1-14)";
        default:
            return "Unknown";
    }
}

rf_wifi_region_t radio_monitor_get_region( void )
{
    return region;
}

esp_err_t radio_monitor_set_region( rf_wifi_region_t value )
{
    wifi_country_t country = {
        .schan = 1, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_MANUAL };
    switch( value )
    {
        case RF_WIFI_REGION_US:
            memcpy( country.cc, "US", 3 );
            country.nchan = 11;
            max_channel = 11;
            break;
        case RF_WIFI_REGION_ETSI:
            memcpy( country.cc, "DE", 3 );
            country.nchan = 13;
            max_channel = 13;
            break;
        case RF_WIFI_REGION_JAPAN:
        case RF_WIFI_REGION_EXTENDED_RX:
            memcpy( country.cc, "JP", 3 );
            country.nchan = 14;
            max_channel = 14;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_wifi_set_country( &country );
    if( err != ESP_OK )
        return err;
    region = value;
    if( running && channel > max_channel )
    {
        channel = 1;
        err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    }
    return err;
}

esp_err_t radio_monitor_start( void )
{
    if( running )
        return ESP_OK;
    esp_err_t err = esp_wifi_start();
    if( err != ESP_OK )
        return err;
    err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    if( err == ESP_OK )
        err = esp_wifi_set_promiscuous( true );
    if( err != ESP_OK )
    {
        (void)esp_wifi_stop();
        return err;
    }
    running = true;
    last_hop_us = esp_timer_get_time();
    ESP_LOGI( TAG, "Wi-Fi monitor started" );
    return ESP_OK;
}

esp_err_t radio_monitor_stop( void )
{
    if( !running )
        return ESP_OK;
    (void)esp_wifi_set_promiscuous( false );
    running = false;
    const esp_err_t err = esp_wifi_stop();
    ESP_LOGI( TAG, "Wi-Fi monitor stopped for inactive app" );
    return err;
}

esp_err_t radio_monitor_init( radio_observation_fn callback )
{
    sink = callback;
    if( !initialized )
    {
        ESP_ERROR_CHECK( esp_netif_init() );
        ESP_ERROR_CHECK( esp_event_loop_create_default() );
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );
        ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_NULL ) );
        ESP_ERROR_CHECK( esp_wifi_set_promiscuous_rx_cb( promiscuous ) );
        initialized = true;
        ESP_ERROR_CHECK( radio_monitor_set_region( RF_WIFI_REGION_EXTENDED_RX ) );
    }
    return radio_monitor_start();
}

void radio_monitor_step( void )
{
    if( !running )
        return;
    const int64_t now = esp_timer_get_time();
    if( now - last_hop_us < CHANNEL_DWELL_US )
        return;
    if( ++channel > max_channel )
        channel = 1;
    const esp_err_t err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    if( err != ESP_OK )
        ESP_LOGW( TAG, "unable to switch to channel %u: %s", channel, esp_err_to_name( err ) );
    last_hop_us = now;
}
