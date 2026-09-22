/**
    radio_monitor.c : RFScout passive Wi-Fi collector
    Description: Normalizes receive-only Wi-Fi observations from the promiscuous callback.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "radio_monitor.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define CHANNEL_DWELL_US 350000LL
#define WIFI_CALLBACK_QUIESCE_TIMEOUT_MS 1000U

static const char *TAG = "radio_monitor";
static radio_observation_fn sink;
static uint8_t channel = 1;
static uint8_t max_channel = 14;
static int64_t last_hop_us;
static rf_wifi_region_t region = RF_WIFI_REGION_EXTENDED_RX;
static bool initialized;
static bool running;
static portMUX_TYPE callback_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t observation_callbacks_inflight;

static bool begin_observation_callback( void )
{
    bool deliver;
    portENTER_CRITICAL( &callback_lock );
    deliver = running;
    if( deliver )
    {
        ++observation_callbacks_inflight;
    }
    portEXIT_CRITICAL( &callback_lock );
    return deliver;
}

static void end_observation_callback( void )
{
    portENTER_CRITICAL( &callback_lock );
    if( observation_callbacks_inflight > 0U )
    {
        --observation_callbacks_inflight;
    }
    portEXIT_CRITICAL( &callback_lock );
}

static uint32_t observation_callbacks_active( void )
{
    uint32_t active;
    portENTER_CRITICAL( &callback_lock );
    active = observation_callbacks_inflight;
    portEXIT_CRITICAL( &callback_lock );
    return active;
}

static void emit_mac( const uint8_t mac[ 6 ], int8_t rssi, uint8_t ch )
{
    if( ( NULL == sink ) || ( mac[ 0 ] & 0x01U ) )
    {
        return;
    }
    rf_device_t observation = { 0 };
    memcpy( observation.address, mac, 6 );
    observation.address_type = 0;
    observation.last_source = RF_SOURCE_WIFI;
    observation.source_mask = RF_SOURCE_BIT( RF_SOURCE_WIFI );
    observation.rssi = rssi;
    observation.channel = ch;
    observation.first_seen_ms = observation.last_seen_ms =
        (uint32_t)( esp_timer_get_time() / 1000ULL );
    sink( &observation );
}

static void promiscuous( void *buf, wifi_promiscuous_pkt_type_t type )
{
    if( ( NULL == buf ) || ( WIFI_PKT_MISC == type ) || !begin_observation_callback() )
    {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *frame = pkt->payload;
    if( pkt->rx_ctrl.sig_len >= 24U )
    {
        const uint16_t frame_control = (uint16_t)frame[ 0 ] | ( (uint16_t)frame[ 1 ] << 8U );
        const uint8_t frame_type = ( frame_control >> 2U ) & 0x3U;
        const uint8_t to_ds = ( frame_control >> 8U ) & 1U;
        const uint8_t from_ds = ( frame_control >> 9U ) & 1U;
        if( 1U != frame_type )
        {
            emit_mac( frame + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
            if( 0U == frame_type )
            {
                emit_mac( frame + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
            }
            else if( ( 2U == frame_type ) && !( to_ds && from_ds ) )
            {
                emit_mac( to_ds ? frame + 4 : frame + 16, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel );
            }
        }
    }

    end_observation_callback();
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
    if( ESP_OK != err )
    {
        return err;
    }
    region = value;
    if( radio_monitor_is_running() && ( channel > max_channel ) )
    {
        channel = 1;
        err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    }
    return err;
}

bool radio_monitor_is_running( void )
{
    bool result;
    portENTER_CRITICAL( &callback_lock );
    result = running;
    portEXIT_CRITICAL( &callback_lock );
    return result;
}

esp_err_t radio_monitor_start( void )
{
    portENTER_CRITICAL( &callback_lock );
    const bool already_running = running;
    if( !already_running )
    {
        running = true;
    }
    portEXIT_CRITICAL( &callback_lock );
    if( already_running )
    {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_set_mode( WIFI_MODE_NULL );
    if( ESP_OK == err )
    {
        err = esp_wifi_start();
    }
    if( ESP_OK != err )
    {
        portENTER_CRITICAL( &callback_lock );
        running = false;
        portEXIT_CRITICAL( &callback_lock );
        return err;
    }
    err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    if( ESP_OK == err )
    {
        err = esp_wifi_set_promiscuous( true );
    }
    if( ESP_OK != err )
    {
        portENTER_CRITICAL( &callback_lock );
        running = false;
        portEXIT_CRITICAL( &callback_lock );
        (void)esp_wifi_stop();
        return err;
    }
    last_hop_us = esp_timer_get_time();
    ESP_LOGI( TAG, "Wi-Fi monitor started" );
    return ESP_OK;
}

esp_err_t radio_monitor_stop( void )
{
    portENTER_CRITICAL( &callback_lock );
    const bool was_running = running;
    running = false;
    portEXIT_CRITICAL( &callback_lock );

    esp_err_t stop_result = ESP_OK;
    if( was_running )
    {
        (void)esp_wifi_set_promiscuous( false );
        stop_result = esp_wifi_stop();
    }

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS( WIFI_CALLBACK_QUIESCE_TIMEOUT_MS );
    while( observation_callbacks_active() > 0U )
    {
        if( ( xTaskGetTickCount() - started ) >= timeout )
        {
            ESP_LOGE( TAG,
                      "Wi-Fi observation callbacks did not quiesce within %u ms",
                      (unsigned)WIFI_CALLBACK_QUIESCE_TIMEOUT_MS );
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }

    if( was_running )
    {
        ESP_LOGI( TAG, "Wi-Fi monitor stopped for inactive app" );
    }
    return stop_result;
}

esp_err_t radio_monitor_init( radio_observation_fn callback )
{
    sink = callback;
    portENTER_CRITICAL( &callback_lock );
    running = false;
    observation_callbacks_inflight = 0U;
    portEXIT_CRITICAL( &callback_lock );

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
    return ESP_OK;
}

void radio_monitor_step( void )
{
    if( !radio_monitor_is_running() )
    {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if( ( now - last_hop_us ) < CHANNEL_DWELL_US )
    {
        return;
    }
    if( ++channel > max_channel )
    {
        channel = 1;
    }
    const esp_err_t err = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    if( ESP_OK != err )
    {
        ESP_LOGW( TAG, "unable to switch to channel %u: %s", channel, esp_err_to_name( err ) );
    }
    last_hop_us = now;
}
