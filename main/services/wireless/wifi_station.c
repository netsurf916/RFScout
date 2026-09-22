/**
    wifi_station.c : RFScout temporary infrastructure Wi-Fi service
    Description: Owns passive station scans, explicit association, DHCP state, and teardown.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_station.h"

#include "device_time.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <string.h>

#define WIFI_STATION_PASSIVE_DWELL_MS 200U
#define WIFI_STATION_ACTIVE_MAX_DWELL_MS 120U
#define WIFI_STATION_NTP_SERVER "pool.ntp.org"

static const char *TAG = "wifi_station";
static bool initialized;
static bool started;
static bool stopping;
static bool disconnect_requested;
static bool ntp_initialized;
static esp_event_handler_instance_t wifi_event_handler;
static esp_event_handler_instance_t ip_event_handler;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static rfscout_wifi_station_status_t status;
static rfscout_wifi_station_ap_t scan_results[ RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS ];
static wifi_ap_record_t scan_records[ RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS ];
static size_t scan_result_count;
static char active_search_ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];

static void set_state( rfscout_wifi_station_state_t a_state, int32_t a_disconnect_reason )
{
    portENTER_CRITICAL( &state_lock );
    status.state = a_state;
    status.disconnect_reason = a_disconnect_reason;
    portEXIT_CRITICAL( &state_lock );
}

static void ntp_sync_callback( struct timeval *a_time )
{
    if( NULL == a_time )
    {
        return;
    }

    const esp_err_t result = device_time_offer_ntp( a_time->tv_sec );
    portENTER_CRITICAL( &state_lock );
    if( started && !stopping && RFSCOUT_WIFI_STATION_CONNECTED == status.state )
    {
        status.ntp_syncing = false;
        status.ntp_synced = ESP_OK == result;
    }
    portEXIT_CRITICAL( &state_lock );

    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not accept NTP time: %s", esp_err_to_name( result ) );
    }
}

static void stop_ntp_sync( void )
{
    bool deinit;
    portENTER_CRITICAL( &state_lock );
    deinit = ntp_initialized;
    ntp_initialized = false;
    status.ntp_syncing = false;
    status.ntp_synced = false;
    portEXIT_CRITICAL( &state_lock );

    if( deinit )
    {
        esp_netif_sntp_deinit();
    }
}

static esp_err_t start_ntp_sync( void )
{
    portENTER_CRITICAL( &state_lock );
    status.ntp_syncing = true;
    status.ntp_synced = false;
    portEXIT_CRITICAL( &state_lock );

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG( WIFI_STATION_NTP_SERVER );
    config.sync_cb = ntp_sync_callback;
    const esp_err_t result = esp_netif_sntp_init( &config );

    portENTER_CRITICAL( &state_lock );
    if( ESP_OK == result )
    {
        ntp_initialized = true;
    }
    else
    {
        status.ntp_syncing = false;
    }
    portEXIT_CRITICAL( &state_lock );
    return result;
}

static bool ssid_already_retained( const uint8_t *a_ssid, size_t a_count )
{
    for( size_t index = 0U; index < a_count; ++index )
    {
        if( 0 == strncmp( scan_results[ index ].ssid,
                          (const char *)a_ssid,
                          RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH ) )
        {
            return true;
        }
    }
    return false;
}

static bool passive_snapshot_contains( const char *a_ssid )
{
    bool found = false;
    portENTER_CRITICAL( &state_lock );
    for( size_t index = 0U; index < scan_result_count; ++index )
    {
        if( 0 == strcmp( scan_results[ index ].ssid, a_ssid ) )
        {
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL( &state_lock );
    return found;
}

static void collect_scan_results( void )
{
    uint16_t count = RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS;
    const esp_err_t result = esp_wifi_scan_get_ap_records( &count, scan_records );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not retrieve passive scan results: %s", esp_err_to_name( result ) );
        set_state( RFSCOUT_WIFI_STATION_IDLE, (int32_t)result );
        return;
    }

    portENTER_CRITICAL( &state_lock );
    scan_result_count = 0U;
    memset( scan_results, 0, sizeof( scan_results ) );
    for( uint16_t index = 0U;
         index < count && scan_result_count < RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS;
         ++index )
    {
        const wifi_ap_record_t *record = &scan_records[ index ];
        if( '\0' == record->ssid[ 0 ] || ssid_already_retained( record->ssid, scan_result_count ) )
        {
            continue;
        }

        rfscout_wifi_station_ap_t *output = &scan_results[ scan_result_count++ ];
        memcpy( output->ssid, record->ssid, RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH );
        output->ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH ] = '\0';
        output->rssi = record->rssi;
        output->channel = record->primary;
        output->open = WIFI_AUTH_OPEN == record->authmode;
    }
    ++status.scan_generation;
    status.state = RFSCOUT_WIFI_STATION_IDLE;
    status.disconnect_reason = 0;
    portEXIT_CRITICAL( &state_lock );

    memset( scan_records, 0, sizeof( scan_records ) );
}

static void complete_active_search( void )
{
    uint16_t count = 0U;
    esp_err_t result = esp_wifi_scan_get_ap_num( &count );
    (void)esp_wifi_clear_ap_list();

    if( ESP_OK != result )
    {
        memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
        set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)result );
        return;
    }
    if( 0U == count )
    {
        memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
        set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)ESP_ERR_NOT_FOUND );
        return;
    }

    memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
    set_state( RFSCOUT_WIFI_STATION_CONNECTING, 0 );
    result = esp_wifi_connect();
    if( ESP_OK != result )
    {
        set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)result );
    }
}

static void
wifi_event( void *a_context, esp_event_base_t a_event_base, int32_t a_event_id, void *a_event_data )
{
    (void)a_context;
    (void)a_event_base;

    if( WIFI_EVENT_SCAN_DONE == a_event_id )
    {
        rfscout_wifi_station_state_t scan_state;
        portENTER_CRITICAL( &state_lock );
        scan_state = started && !stopping ? status.state : RFSCOUT_WIFI_STATION_OFF;
        portEXIT_CRITICAL( &state_lock );

        if( RFSCOUT_WIFI_STATION_SCANNING == scan_state )
        {
            collect_scan_results();
        }
        else if( RFSCOUT_WIFI_STATION_SEARCHING == scan_state )
        {
            complete_active_search();
        }
        else
        {
            (void)esp_wifi_clear_ap_list();
        }
        return;
    }

    if( WIFI_EVENT_STA_DISCONNECTED == a_event_id )
    {
        const wifi_event_sta_disconnected_t *event = a_event_data;
        const int32_t reason = NULL != event ? (int32_t)event->reason : 0;

        portENTER_CRITICAL( &state_lock );
        if( stopping )
        {
            portEXIT_CRITICAL( &state_lock );
            return;
        }
        if( disconnect_requested )
        {
            disconnect_requested = false;
            status.state = RFSCOUT_WIFI_STATION_IDLE;
            status.disconnect_reason = 0;
            memset( status.ssid, 0, sizeof( status.ssid ) );
        }
        else
        {
            status.state = RFSCOUT_WIFI_STATION_FAILED;
            status.disconnect_reason = reason;
        }
        portEXIT_CRITICAL( &state_lock );
        stop_ntp_sync();
    }
}

static void
ip_event( void *a_context, esp_event_base_t a_event_base, int32_t a_event_id, void *a_event_data )
{
    (void)a_context;
    (void)a_event_base;
    (void)a_event_data;

    if( IP_EVENT_STA_GOT_IP == a_event_id )
    {
        bool synchronize_time = false;
        portENTER_CRITICAL( &state_lock );
        if( started && !stopping && !disconnect_requested &&
            RFSCOUT_WIFI_STATION_CONNECTING == status.state )
        {
            status.state = RFSCOUT_WIFI_STATION_CONNECTED;
            status.disconnect_reason = 0;
            synchronize_time = true;
        }
        portEXIT_CRITICAL( &state_lock );

        if( synchronize_time )
        {
            const esp_err_t result = start_ntp_sync();
            if( ESP_OK != result )
            {
                ESP_LOGW(
                    TAG, "could not start NTP synchronization: %s", esp_err_to_name( result ) );
            }
        }
    }
}

static esp_err_t ensure_started( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool already_started = started;
    portEXIT_CRITICAL( &state_lock );
    if( already_started )
    {
        return ESP_OK;
    }

    esp_err_t result = esp_wifi_set_storage( WIFI_STORAGE_RAM );
    if( ESP_OK == result )
    {
        result = esp_wifi_set_mode( WIFI_MODE_STA );
    }
    if( ESP_OK == result )
    {
        result = esp_wifi_start();
    }
    if( ESP_OK != result )
    {
        (void)esp_wifi_stop();
        (void)esp_wifi_set_mode( WIFI_MODE_NULL );
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    started = true;
    stopping = false;
    disconnect_requested = false;
    status.state = RFSCOUT_WIFI_STATION_IDLE;
    status.disconnect_reason = 0;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    esp_netif_t *station_netif = esp_netif_create_default_wifi_sta();
    if( NULL == station_netif )
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &wifi_event_handler );
    if( ESP_OK == result )
    {
        result = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL, &ip_event_handler );
    }
    if( ESP_OK != result )
    {
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    started = false;
    stopping = false;
    disconnect_requested = false;
    ntp_initialized = false;
    status = ( rfscout_wifi_station_status_t ){
        .state = RFSCOUT_WIFI_STATION_OFF,
        .scan_generation = 0U,
        .disconnect_reason = 0,
        .ssid = "",
        .ntp_syncing = false,
        .ntp_synced = false,
    };
    scan_result_count = 0U;
    portEXIT_CRITICAL( &state_lock );
    initialized = true;
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_passive_scan_start( void )
{
    if( !initialized )
    {
        const esp_err_t init_result = rfscout_wifi_station_init();
        if( ESP_OK != init_result )
        {
            return init_result;
        }
    }

    rfscout_wifi_station_status_t current = rfscout_wifi_station_status();
    if( RFSCOUT_WIFI_STATION_CONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_DISCONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTED == current.state ||
        RFSCOUT_WIFI_STATION_SCANNING == current.state ||
        RFSCOUT_WIFI_STATION_SEARCHING == current.state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_started();
    if( ESP_OK != result )
    {
        return result;
    }

    wifi_scan_config_t config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0U,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time = { .passive = WIFI_STATION_PASSIVE_DWELL_MS },
    };

    set_state( RFSCOUT_WIFI_STATION_SCANNING, 0 );
    result = esp_wifi_scan_start( &config, false );
    if( ESP_OK != result )
    {
        set_state( RFSCOUT_WIFI_STATION_IDLE, (int32_t)result );
    }
    return result;
}

size_t rfscout_wifi_station_scan_results( rfscout_wifi_station_ap_t *a_results, size_t a_capacity )
{
    portENTER_CRITICAL( &state_lock );
    const size_t count = scan_result_count < a_capacity ? scan_result_count : a_capacity;
    if( NULL != a_results && count > 0U )
    {
        memcpy( a_results, scan_results, count * sizeof( scan_results[ 0 ] ) );
    }
    portEXIT_CRITICAL( &state_lock );
    return count;
}

esp_err_t rfscout_wifi_station_connect( const char *a_ssid, const char *a_password )
{
    if( !initialized )
    {
        const esp_err_t init_result = rfscout_wifi_station_init();
        if( ESP_OK != init_result )
        {
            return init_result;
        }
    }
    if( NULL == a_ssid )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_length = strnlen( a_ssid, RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U );
    const size_t password_length =
        NULL == a_password ? 0U
                           : strnlen( a_password, RFSCOUT_WIFI_STATION_PASSWORD_MAX_LENGTH + 1U );
    if( 0U == ssid_length || ssid_length > RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH ||
        password_length > RFSCOUT_WIFI_STATION_PASSWORD_MAX_LENGTH )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    rfscout_wifi_station_status_t current = rfscout_wifi_station_status();
    if( RFSCOUT_WIFI_STATION_SCANNING == current.state ||
        RFSCOUT_WIFI_STATION_SEARCHING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_DISCONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTED == current.state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_started();
    if( ESP_OK != result )
    {
        return result;
    }

    wifi_config_t config = { 0 };
    memcpy( config.sta.ssid, a_ssid, ssid_length );
    if( password_length > 0U )
    {
        memcpy( config.sta.password, a_password, password_length );
    }
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.failure_retry_cnt = 0U;

    result = esp_wifi_set_config( WIFI_IF_STA, &config );
    memset( &config, 0, sizeof( config ) );
    if( ESP_OK != result )
    {
        return result;
    }

    stop_ntp_sync();

    portENTER_CRITICAL( &state_lock );
    disconnect_requested = false;
    status.disconnect_reason = 0;
    memset( status.ssid, 0, sizeof( status.ssid ) );
    memcpy( status.ssid, a_ssid, ssid_length );
    status.ntp_syncing = false;
    status.ntp_synced = false;
    portEXIT_CRITICAL( &state_lock );

    if( passive_snapshot_contains( a_ssid ) )
    {
        set_state( RFSCOUT_WIFI_STATION_CONNECTING, 0 );
        result = esp_wifi_connect();
        if( ESP_OK != result )
        {
            set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)result );
        }
        return result;
    }

    memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
    memcpy( active_search_ssid, a_ssid, ssid_length );
    wifi_scan_config_t search = {
        .ssid = (uint8_t *)active_search_ssid,
        .bssid = NULL,
        .channel = 0U,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 0U, .max = WIFI_STATION_ACTIVE_MAX_DWELL_MS } },
    };

    set_state( RFSCOUT_WIFI_STATION_SEARCHING, 0 );
    result = esp_wifi_scan_start( &search, false );
    if( ESP_OK != result )
    {
        memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
        set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)result );
    }
    return result;
}

esp_err_t rfscout_wifi_station_disconnect( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    stop_ntp_sync();

    portENTER_CRITICAL( &state_lock );
    if( !started || RFSCOUT_WIFI_STATION_CONNECTED != status.state )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }
    disconnect_requested = true;
    status.state = RFSCOUT_WIFI_STATION_DISCONNECTING;
    status.disconnect_reason = 0;
    portEXIT_CRITICAL( &state_lock );

    const esp_err_t result = esp_wifi_disconnect();
    if( ESP_OK != result )
    {
        portENTER_CRITICAL( &state_lock );
        disconnect_requested = false;
        status.state = RFSCOUT_WIFI_STATION_IDLE;
        memset( status.ssid, 0, sizeof( status.ssid ) );
        portEXIT_CRITICAL( &state_lock );
        return result;
    }

    wifi_config_t blank = { 0 };
    (void)esp_wifi_set_config( WIFI_IF_STA, &blank );
    memset( &blank, 0, sizeof( blank ) );
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_stop( void )
{
    if( !initialized )
    {
        return ESP_OK;
    }

    stop_ntp_sync();

    portENTER_CRITICAL( &state_lock );
    const bool was_started = started;
    const bool needs_mode_reset = was_started || RFSCOUT_WIFI_STATION_OFF != status.state;
    stopping = true;
    portEXIT_CRITICAL( &state_lock );

    if( was_started )
    {
        (void)esp_wifi_scan_stop();
        (void)esp_wifi_clear_ap_list();
        (void)esp_wifi_disconnect();

        wifi_config_t blank = { 0 };
        (void)esp_wifi_set_config( WIFI_IF_STA, &blank );
        memset( &blank, 0, sizeof( blank ) );

        const esp_err_t stop_result = esp_wifi_stop();
        if( ESP_OK != stop_result )
        {
            portENTER_CRITICAL( &state_lock );
            stopping = false;
            portEXIT_CRITICAL( &state_lock );
            return stop_result;
        }

        portENTER_CRITICAL( &state_lock );
        started = false;
        portEXIT_CRITICAL( &state_lock );
    }

    if( needs_mode_reset )
    {
        const esp_err_t mode_result = esp_wifi_set_mode( WIFI_MODE_NULL );
        if( ESP_OK != mode_result )
        {
            portENTER_CRITICAL( &state_lock );
            stopping = false;
            portEXIT_CRITICAL( &state_lock );
            return mode_result;
        }
    }

    portENTER_CRITICAL( &state_lock );
    disconnect_requested = false;
    status.state = RFSCOUT_WIFI_STATION_OFF;
    status.disconnect_reason = 0;
    memset( status.ssid, 0, sizeof( status.ssid ) );
    status.ntp_syncing = false;
    status.ntp_synced = false;
    scan_result_count = 0U;
    memset( scan_results, 0, sizeof( scan_results ) );
    memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
    stopping = false;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

bool rfscout_wifi_station_is_active( void )
{
    bool active;
    portENTER_CRITICAL( &state_lock );
    active = started;
    portEXIT_CRITICAL( &state_lock );
    return active;
}

rfscout_wifi_station_status_t rfscout_wifi_station_status( void )
{
    rfscout_wifi_station_status_t result;
    portENTER_CRITICAL( &state_lock );
    result = status;
    portEXIT_CRITICAL( &state_lock );
    return result;
}
