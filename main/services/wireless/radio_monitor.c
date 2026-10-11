/**
    radio_monitor.c : RFScout passive Wi-Fi collector
    Description: Interprets receive-only 802.11 observations and adaptively scans channels 1-14.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "radio_monitor.h"

#include "wifi_core.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define WIFI_DISCOVERY_DWELL_US 120000LL
#define WIFI_ACTIVE_DWELL_US 350000LL
#define WIFI_CALLBACK_QUIESCE_TIMEOUT_MS 1000U
#define WIFI_MIN_CHANNEL 1U

#define IEEE80211_TYPE_MANAGEMENT 0U
#define IEEE80211_TYPE_CONTROL 1U
#define IEEE80211_TYPE_DATA 2U

#define IEEE80211_SUBTYPE_PROBE_RESPONSE 5U
#define IEEE80211_SUBTYPE_BEACON 8U
#define IEEE80211_SUBTYPE_BLOCK_ACK_REQUEST 8U
#define IEEE80211_SUBTYPE_BLOCK_ACK 9U
#define IEEE80211_SUBTYPE_PS_POLL 10U
#define IEEE80211_SUBTYPE_RTS 11U
#define IEEE80211_SUBTYPE_CTS 12U
#define IEEE80211_SUBTYPE_ACK 13U
#define IEEE80211_SUBTYPE_CF_END 14U
#define IEEE80211_SUBTYPE_CF_END_ACK 15U

typedef struct
{
    uint8_t type;
    uint8_t subtype;
    bool to_ds;
    bool from_ds;
    const uint8_t *receiver;
    const uint8_t *transmitter;
    const uint8_t *bssid;
} wifi_frame_info_t;

static const char *TAG = "radio_monitor";
static radio_observation_fn sink;
static radio_raw_frame_fn raw_sink;
static uint8_t channel = WIFI_MIN_CHANNEL;
static int64_t last_hop_us;
static uint16_t active_ap_channel_mask;
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

static bool same_mac( const uint8_t *a_left, const uint8_t *a_right )
{
    return ( NULL != a_left ) && ( NULL != a_right ) && ( 0 == memcmp( a_left, a_right, 6U ) );
}

static void emit_mac( const uint8_t a_mac[ 6 ], int8_t a_rssi, uint8_t a_channel, bool a_is_ap )
{
    if( ( NULL == sink ) || ( NULL == a_mac ) || ( 0U != ( a_mac[ 0 ] & 0x01U ) ) )
    {
        return;
    }

    rf_device_t observation = { 0 };
    memcpy( observation.address, a_mac, sizeof( observation.address ) );
    observation.address_type = 0U;
    observation.last_source = RF_SOURCE_WIFI;
    observation.source_mask = RF_SOURCE_BIT( RF_SOURCE_WIFI );
    observation.rssi = a_rssi;
    observation.channel = a_channel;
    observation.wifi_channel = a_channel;
    observation.wifi_is_ap = a_is_ap;
    observation.first_seen_ms = observation.last_seen_ms =
        (uint32_t)( esp_timer_get_time() / 1000ULL );
    sink( &observation );
}

static bool parse_wifi_frame( const uint8_t *a_frame, size_t a_length, wifi_frame_info_t *a_info )
{
    if( ( NULL == a_frame ) || ( NULL == a_info ) || ( a_length < 2U ) )
    {
        return false;
    }

    memset( a_info, 0, sizeof( *a_info ) );
    const uint16_t frame_control = (uint16_t)a_frame[ 0 ] | ( (uint16_t)a_frame[ 1 ] << 8U );
    a_info->type = (uint8_t)( ( frame_control >> 2U ) & 0x3U );
    a_info->subtype = (uint8_t)( ( frame_control >> 4U ) & 0x0FU );
    a_info->to_ds = 0U != ( ( frame_control >> 8U ) & 1U );
    a_info->from_ds = 0U != ( ( frame_control >> 9U ) & 1U );

    if( ( IEEE80211_TYPE_MANAGEMENT == a_info->type ) || ( IEEE80211_TYPE_DATA == a_info->type ) )
    {
        if( a_length < 24U )
        {
            return false;
        }

        a_info->receiver = a_frame + 4U;
        a_info->transmitter = a_frame + 10U;
        if( IEEE80211_TYPE_MANAGEMENT == a_info->type )
        {
            a_info->bssid = a_frame + 16U;
        }
        else if( a_info->to_ds && !a_info->from_ds )
        {
            a_info->bssid = a_frame + 4U;
        }
        else if( !a_info->to_ds && a_info->from_ds )
        {
            a_info->bssid = a_frame + 10U;
        }
        return true;
    }

    if( IEEE80211_TYPE_CONTROL != a_info->type )
    {
        return false;
    }

    if( ( IEEE80211_SUBTYPE_CTS == a_info->subtype ) ||
        ( IEEE80211_SUBTYPE_ACK == a_info->subtype ) )
    {
        if( a_length < 10U )
        {
            return false;
        }
        a_info->receiver = a_frame + 4U;
        return true;
    }

    if( a_length < 16U )
    {
        return false;
    }

    a_info->receiver = a_frame + 4U;
    a_info->transmitter = a_frame + 10U;
    if( IEEE80211_SUBTYPE_PS_POLL == a_info->subtype )
    {
        a_info->bssid = a_frame + 4U;
    }
    else if( ( IEEE80211_SUBTYPE_CF_END == a_info->subtype ) ||
             ( IEEE80211_SUBTYPE_CF_END_ACK == a_info->subtype ) )
    {
        a_info->bssid = a_frame + 10U;
    }
    return true;
}

static void
emit_management_observations( const wifi_frame_info_t *a_info, int8_t a_rssi, uint8_t a_channel )
{
    const bool identifies_ap = ( IEEE80211_SUBTYPE_BEACON == a_info->subtype ) ||
                               ( IEEE80211_SUBTYPE_PROBE_RESPONSE == a_info->subtype );
    if( identifies_ap && ( NULL != a_info->bssid ) )
    {
        emit_mac( a_info->bssid, a_rssi, a_channel, true );
        if( !same_mac( a_info->transmitter, a_info->bssid ) )
        {
            emit_mac( a_info->transmitter, a_rssi, a_channel, false );
        }
        return;
    }

    emit_mac( a_info->transmitter, a_rssi, a_channel, false );
    if( !same_mac( a_info->bssid, a_info->transmitter ) )
    {
        emit_mac( a_info->bssid, a_rssi, a_channel, false );
    }
}

static void
emit_data_observations( const wifi_frame_info_t *a_info, int8_t a_rssi, uint8_t a_channel )
{
    if( a_info->to_ds && !a_info->from_ds )
    {
        emit_mac( a_info->transmitter, a_rssi, a_channel, false );
        emit_mac( a_info->bssid, a_rssi, a_channel, true );
        return;
    }

    if( !a_info->to_ds && a_info->from_ds )
    {
        emit_mac( a_info->bssid, a_rssi, a_channel, true );
        emit_mac( a_info->receiver, a_rssi, a_channel, false );
        return;
    }

    emit_mac( a_info->transmitter, a_rssi, a_channel, false );
    if( !same_mac( a_info->receiver, a_info->transmitter ) )
    {
        emit_mac( a_info->receiver, a_rssi, a_channel, false );
    }
}

static void
emit_control_observations( const wifi_frame_info_t *a_info, int8_t a_rssi, uint8_t a_channel )
{
    if( IEEE80211_SUBTYPE_PS_POLL == a_info->subtype )
    {
        emit_mac( a_info->bssid, a_rssi, a_channel, true );
        emit_mac( a_info->transmitter, a_rssi, a_channel, false );
        return;
    }

    if( ( IEEE80211_SUBTYPE_CF_END == a_info->subtype ) ||
        ( IEEE80211_SUBTYPE_CF_END_ACK == a_info->subtype ) )
    {
        emit_mac( a_info->bssid, a_rssi, a_channel, true );
        emit_mac( a_info->receiver, a_rssi, a_channel, false );
        return;
    }

    if( ( IEEE80211_SUBTYPE_RTS == a_info->subtype ) ||
        ( IEEE80211_SUBTYPE_BLOCK_ACK_REQUEST == a_info->subtype ) ||
        ( IEEE80211_SUBTYPE_BLOCK_ACK == a_info->subtype ) )
    {
        emit_mac( a_info->transmitter, a_rssi, a_channel, false );
        if( !same_mac( a_info->receiver, a_info->transmitter ) )
        {
            emit_mac( a_info->receiver, a_rssi, a_channel, false );
        }
    }
}

static void promiscuous( void *a_buffer, wifi_promiscuous_pkt_type_t a_type )
{
    if( ( NULL == a_buffer ) || ( WIFI_PKT_MISC == a_type ) || !begin_observation_callback() )
    {
        return;
    }

    const wifi_promiscuous_pkt_t *packet = a_buffer;
    if( NULL != raw_sink )
    {
        raw_sink( packet->payload, packet->rx_ctrl.sig_len, packet->rx_ctrl.rssi,
                  packet->rx_ctrl.channel );
    }
    wifi_frame_info_t frame;
    if( parse_wifi_frame( packet->payload, packet->rx_ctrl.sig_len, &frame ) )
    {
        if( IEEE80211_TYPE_MANAGEMENT == frame.type )
        {
            emit_management_observations( &frame, packet->rx_ctrl.rssi, packet->rx_ctrl.channel );
        }
        else if( IEEE80211_TYPE_DATA == frame.type )
        {
            emit_data_observations( &frame, packet->rx_ctrl.rssi, packet->rx_ctrl.channel );
        }
        else if( IEEE80211_TYPE_CONTROL == frame.type )
        {
            emit_control_observations( &frame, packet->rx_ctrl.rssi, packet->rx_ctrl.channel );
        }
    }

    end_observation_callback();
}

static esp_err_t configure_receive_channels( void )
{
    wifi_country_t country = {
        .schan = WIFI_MIN_CHANNEL,
        .nchan = RFSCOUT_WIFI_CHANNEL_MAX,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    memcpy( country.cc, "JP", 3U );
    return esp_wifi_set_country( &country );
}

static esp_err_t configure_promiscuous_filters( void )
{
    wifi_promiscuous_filter_t packet_filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA |
                       WIFI_PROMIS_FILTER_MASK_CTRL,
    };
    esp_err_t result = esp_wifi_set_promiscuous_filter( &packet_filter );
    if( ESP_OK != result )
    {
        return result;
    }

    wifi_promiscuous_filter_t control_filter = {
        .filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_BAR | WIFI_PROMIS_CTRL_FILTER_MASK_BA |
                       WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL | WIFI_PROMIS_CTRL_FILTER_MASK_RTS |
                       WIFI_PROMIS_CTRL_FILTER_MASK_CFEND | WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK,
    };
    return esp_wifi_set_promiscuous_ctrl_filter( &control_filter );
}

static int64_t channel_dwell_us( uint8_t a_channel )
{
    if( ( a_channel < WIFI_MIN_CHANNEL ) || ( a_channel > RFSCOUT_WIFI_CHANNEL_MAX ) )
    {
        return WIFI_DISCOVERY_DWELL_US;
    }

    const uint16_t channel_bit = (uint16_t)( 1U << ( a_channel - WIFI_MIN_CHANNEL ) );
    return ( 0U != ( active_ap_channel_mask & channel_bit ) ) ? WIFI_ACTIVE_DWELL_US
                                                              : WIFI_DISCOVERY_DWELL_US;
}

bool radio_monitor_is_active( void )
{
    bool result;
    portENTER_CRITICAL( &callback_lock );
    result = running;
    portEXIT_CRITICAL( &callback_lock );
    return result;
}

esp_err_t radio_monitor_start( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t result = esp_wifi_set_mode( WIFI_MODE_NULL );
    if( ESP_OK == result )
    {
        result = esp_wifi_start();
    }
    if( ESP_OK == result )
    {
        result = configure_receive_channels();
    }
    if( ESP_OK == result )
    {
        result = configure_promiscuous_filters();
    }
    if( ESP_OK == result )
    {
        result = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    }
    if( ESP_OK == result )
    {
        result = esp_wifi_set_promiscuous( true );
    }
    if( ESP_OK != result )
    {
        portENTER_CRITICAL( &callback_lock );
        running = false;
        portEXIT_CRITICAL( &callback_lock );
        (void)esp_wifi_stop();
        return result;
    }

    last_hop_us = esp_timer_get_time();
    ESP_LOGI( TAG,
              "Wi-Fi monitor started with adaptive channels 1-%u",
              (unsigned)RFSCOUT_WIFI_CHANNEL_MAX );
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

    active_ap_channel_mask = 0U;
    if( was_running )
    {
        ESP_LOGI( TAG, "Wi-Fi monitor stopped for inactive app" );
    }
    return stop_result;
}

void radio_monitor_set_raw_observer( radio_raw_frame_fn a_callback )
{
    raw_sink = a_callback;
}

void radio_monitor_set_observer( radio_observation_fn a_callback )
{
    sink = a_callback;
}

esp_err_t radio_monitor_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }
    if( NULL == sink )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = wifi_core_init();
    if( ESP_OK != result )
    {
        return result;
    }

    result = esp_wifi_set_promiscuous_rx_cb( promiscuous );
    if( ESP_OK != result )
    {
        return result;
    }

    portENTER_CRITICAL( &callback_lock );
    running = false;
    observation_callbacks_inflight = 0U;
    portEXIT_CRITICAL( &callback_lock );
    active_ap_channel_mask = 0U;
    initialized = true;
    return ESP_OK;
}

void radio_monitor_set_active_ap_channels( uint16_t a_channel_mask )
{
    const uint16_t valid_mask = (uint16_t)( ( 1U << RFSCOUT_WIFI_CHANNEL_MAX ) - 1U );
    active_ap_channel_mask = a_channel_mask & valid_mask;
}

void radio_monitor_step( void )
{
    if( !radio_monitor_is_active() )
    {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if( ( now - last_hop_us ) < channel_dwell_us( channel ) )
    {
        return;
    }

    ++channel;
    if( channel > RFSCOUT_WIFI_CHANNEL_MAX )
    {
        channel = WIFI_MIN_CHANNEL;
    }

    const esp_err_t result = esp_wifi_set_channel( channel, WIFI_SECOND_CHAN_NONE );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "unable to switch to channel %u: %s", channel, esp_err_to_name( result ) );
    }
    last_hop_us = now;
}
