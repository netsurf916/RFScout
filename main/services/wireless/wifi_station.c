/**
    wifi_station.c : RFScout temporary infrastructure Wi-Fi service
    Description: Owns passive station scans, explicit association, DHCP state, and teardown.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_station.h"

#include "wifi_core.h"

#include "device_time.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include <string.h>

#define WIFI_STATION_PASSIVE_DWELL_MS 200U
#define WIFI_STATION_ACTIVE_MAX_DWELL_MS 120U
#define WIFI_STATION_NTP_SERVER "pool.ntp.org"
#define WIFI_STATION_EVENT_DRAIN_TIMEOUT_MS 1000U

ESP_EVENT_DEFINE_BASE( RFSCOUT_WIFI_STATION_INTERNAL_EVENT );
#define WIFI_STATION_INTERNAL_EVENT_BARRIER 1

static const char *TAG = "wifi_station";
static bool initialized;
static bool started;
static bool stopping;
static bool disconnect_requested;
static bool ntp_initialized;
static bool mac_randomized;
static bool factory_mac_valid;
static uint8_t factory_mac[ 6 ];
static esp_netif_t *station_netif;
static esp_event_handler_instance_t wifi_event_handler;
static esp_event_handler_instance_t ip_event_handler;
static esp_event_handler_instance_t barrier_event_handler;
static uint32_t event_barrier_requested;
static uint32_t event_barrier_completed;
static bool event_drain_pending;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static rfscout_wifi_station_status_t status;
static rfscout_wifi_station_ap_t scan_results[ RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS ];
static wifi_ap_record_t scan_records[ RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS ];
static size_t scan_result_count;
static rfscout_wifi_station_bss_t bss_results[ RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS ];
static size_t bss_result_count;
static char active_search_ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];

static void barrier_event( void *a_context,
                           esp_event_base_t a_event_base,
                           int32_t a_event_id,
                           void *a_event_data )
{
    (void)a_context;
    (void)a_event_base;
    if( WIFI_STATION_INTERNAL_EVENT_BARRIER != a_event_id || NULL == a_event_data )
    {
        return;
    }

    const uint32_t token = *(const uint32_t *)a_event_data;
    portENTER_CRITICAL( &state_lock );
    event_barrier_completed = token;
    portEXIT_CRITICAL( &state_lock );
}

static esp_err_t drain_default_event_loop( void )
{
    uint32_t token;
    portENTER_CRITICAL( &state_lock );
    token = ++event_barrier_requested;
    if( 0U == token )
    {
        token = ++event_barrier_requested;
    }
    portEXIT_CRITICAL( &state_lock );

    esp_err_t result = esp_event_post( RFSCOUT_WIFI_STATION_INTERNAL_EVENT,
                                       WIFI_STATION_INTERNAL_EVENT_BARRIER,
                                       &token,
                                       sizeof( token ),
                                       pdMS_TO_TICKS( 100U ) );
    if( ESP_OK != result )
    {
        return result;
    }

    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS( WIFI_STATION_EVENT_DRAIN_TIMEOUT_MS );
    while( true )
    {
        uint32_t completed;
        portENTER_CRITICAL( &state_lock );
        completed = event_barrier_completed;
        portEXIT_CRITICAL( &state_lock );
        if( completed == token )
        {
            return ESP_OK;
        }
        if( xTaskGetTickCount() >= deadline )
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }
}

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
    bool active;
    portENTER_CRITICAL( &state_lock );
    active = started && !stopping && RFSCOUT_WIFI_STATION_CONNECTED == status.state;
    if( active )
    {
        status.ntp_syncing = true;
        status.ntp_synced = false;
    }
    portEXIT_CRITICAL( &state_lock );
    if( !active )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG( WIFI_STATION_NTP_SERVER );
    config.sync_cb = ntp_sync_callback;
    const esp_err_t result = esp_netif_sntp_init( &config );

    bool deinit = false;
    portENTER_CRITICAL( &state_lock );
    active = started && !stopping && RFSCOUT_WIFI_STATION_CONNECTED == status.state;
    if( ESP_OK == result && active )
    {
        ntp_initialized = true;
    }
    else
    {
        status.ntp_syncing = false;
        deinit = ESP_OK == result;
    }
    portEXIT_CRITICAL( &state_lock );

    if( deinit )
    {
        esp_netif_sntp_deinit();
        return ESP_ERR_INVALID_STATE;
    }
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

/** @brief Classify a driver auth mode for credential-free association eligibility. */
static rfscout_wifi_station_security_t security_class( wifi_auth_mode_t a_authmode )
{
    if( WIFI_AUTH_OPEN == a_authmode )
    {
        return RFSCOUT_WIFI_STATION_SECURITY_OPEN;
    }
    if( WIFI_AUTH_OWE == a_authmode )
    {
        return RFSCOUT_WIFI_STATION_SECURITY_OWE;
    }
    return RFSCOUT_WIFI_STATION_SECURITY_SECURED;
}

static void collect_scan_results( void )
{
    uint16_t count = RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS;
    const esp_err_t result = esp_wifi_scan_get_ap_records( &count, scan_records );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not retrieve passive scan results: %s", esp_err_to_name( result ) );
        set_state( RFSCOUT_WIFI_STATION_IDLE, (int32_t)result );
        return;
    }

    portENTER_CRITICAL( &state_lock );
    bss_result_count = 0U;
    memset( bss_results, 0, sizeof( bss_results ) );
    for( uint16_t index = 0U;
         index < count && bss_result_count < RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS;
         ++index )
    {
        const wifi_ap_record_t *record = &scan_records[ index ];
        rfscout_wifi_station_bss_t *output = &bss_results[ bss_result_count++ ];
        memcpy( output->bssid, record->bssid, sizeof( output->bssid ) );
        memcpy( output->ssid, record->ssid, RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH );
        output->ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH ] = '\0';
        output->rssi = record->rssi;
        output->channel = record->primary;
        output->security = security_class( record->authmode );
    }

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
        if( stopping || !started )
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
        portENTER_CRITICAL( &state_lock );
        if( started && !stopping && !disconnect_requested &&
            RFSCOUT_WIFI_STATION_CONNECTING == status.state )
        {
            status.state = RFSCOUT_WIFI_STATION_CONNECTED;
            status.disconnect_reason = 0;
        }
        portEXIT_CRITICAL( &state_lock );
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

/** @brief Put the factory station MAC back after a randomized connection; driver must be stopped. */
static void restore_factory_mac( void )
{
    if( !mac_randomized )
    {
        return;
    }
    if( !factory_mac_valid )
    {
        ESP_LOGE( TAG, "cannot restore station MAC because the factory MAC was not captured" );
        return;
    }
    const esp_err_t result = esp_wifi_set_mac( WIFI_IF_STA, factory_mac );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not restore factory station MAC: %s", esp_err_to_name( result ) );
        return;
    }
    mac_randomized = false;
}

/** @brief Restart the started STA driver with a fresh random locally administered unicast MAC. */
static esp_err_t restart_with_random_mac( void )
{
    if( !factory_mac_valid )
    {
        ESP_LOGE( TAG, "MAC randomization unavailable: factory MAC not captured" );
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mac[ 6 ];
    esp_fill_random( mac, sizeof( mac ) );
    mac[ 0 ] = (uint8_t)( ( mac[ 0 ] & 0xFCU ) | 0x02U );

    portENTER_CRITICAL( &state_lock );
    stopping = true;
    portEXIT_CRITICAL( &state_lock );

    esp_err_t result = esp_wifi_stop();
    if( ESP_OK == result )
    {
        portENTER_CRITICAL( &state_lock );
        started = false;
        portEXIT_CRITICAL( &state_lock );

        result = esp_wifi_set_mac( WIFI_IF_STA, mac );
        if( ESP_OK == result )
        {
            mac_randomized = true;
        }

        const esp_err_t start_result = esp_wifi_start();
        if( ESP_OK == start_result )
        {
            portENTER_CRITICAL( &state_lock );
            started = true;
            portEXIT_CRITICAL( &state_lock );
        }
        else if( ESP_OK == result )
        {
            result = start_result;
        }

        if( ESP_OK != start_result && mac_randomized )
        {
            restore_factory_mac();
        }
    }

    portENTER_CRITICAL( &state_lock );
    stopping = false;
    status.state = started ? RFSCOUT_WIFI_STATION_IDLE : RFSCOUT_WIFI_STATION_OFF;
    status.disconnect_reason = ESP_OK == result ? 0 : (int32_t)result;
    portEXIT_CRITICAL( &state_lock );

    if( ESP_OK == result )
    {
        ESP_LOGI( TAG,
                  "station MAC randomized to %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[ 0 ],
                  mac[ 1 ],
                  mac[ 2 ],
                  mac[ 3 ],
                  mac[ 4 ],
                  mac[ 5 ] );
    }
    memset( mac, 0, sizeof( mac ) );
    return result;
}

esp_err_t rfscout_wifi_station_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    const esp_err_t core_result = wifi_core_init();
    if( ESP_OK != core_result )
    {
        return core_result;
    }

    station_netif = esp_netif_create_default_wifi_sta();
    if( NULL == station_netif )
    {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t mac_result = esp_read_mac( factory_mac, ESP_MAC_WIFI_STA );
    factory_mac_valid = ESP_OK == mac_result;
    if( !factory_mac_valid )
    {
        memset( factory_mac, 0, sizeof( factory_mac ) );
        ESP_LOGW( TAG,
                  "factory station MAC unavailable; per-check MAC randomization disabled: %s",
                  esp_err_to_name( mac_result ) );
    }

    esp_err_t result = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &wifi_event_handler );
    if( ESP_OK == result )
    {
        result = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL, &ip_event_handler );
        if( ESP_OK == result )
        {
            result = esp_event_handler_instance_register( RFSCOUT_WIFI_STATION_INTERNAL_EVENT,
                                                          WIFI_STATION_INTERNAL_EVENT_BARRIER,
                                                          barrier_event,
                                                          NULL,
                                                          &barrier_event_handler );
        }
        if( ESP_OK != result )
        {
            (void)esp_event_handler_instance_unregister(
                WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler );
            (void)esp_event_handler_instance_unregister(
                IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler );
            memset( &wifi_event_handler, 0, sizeof( wifi_event_handler ) );
            memset( &ip_event_handler, 0, sizeof( ip_event_handler ) );
        }
    }
    if( ESP_OK != result )
    {
        memset( &ip_event_handler, 0, sizeof( ip_event_handler ) );
        esp_netif_destroy_default_wifi( station_netif );
        station_netif = NULL;
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    started = false;
    stopping = false;
    disconnect_requested = false;
    ntp_initialized = false;
    mac_randomized = false;
    status = ( rfscout_wifi_station_status_t ){
        .state = RFSCOUT_WIFI_STATION_OFF,
        .scan_generation = 0U,
        .disconnect_reason = 0,
        .ssid = "",
        .ntp_syncing = false,
        .ntp_synced = false,
    };
    scan_result_count = 0U;
    bss_result_count = 0U;
    event_barrier_requested = 0U;
    event_barrier_completed = 0U;
    event_drain_pending = false;
    portEXIT_CRITICAL( &state_lock );
    initialized = true;
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_passive_scan_start( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );
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

size_t rfscout_wifi_station_bss_results( rfscout_wifi_station_bss_t *a_results, size_t a_capacity )
{
    portENTER_CRITICAL( &state_lock );
    const size_t count = bss_result_count < a_capacity ? bss_result_count : a_capacity;
    if( NULL != a_results && count > 0U )
    {
        memcpy( a_results, bss_results, count * sizeof( bss_results[ 0 ] ) );
    }
    portEXIT_CRITICAL( &state_lock );
    return count;
}

esp_err_t rfscout_wifi_station_connect_open_bss( const rfscout_wifi_station_bss_t *a_bss,
                                                 bool a_randomize_mac )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( NULL == a_bss || RFSCOUT_WIFI_STATION_SECURITY_SECURED == a_bss->security )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t ssid_length = strnlen( a_bss->ssid, RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U );
    if( 0U == ssid_length || ssid_length > RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );
    if( RFSCOUT_WIFI_STATION_SCANNING == current.state ||
        RFSCOUT_WIFI_STATION_SEARCHING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_DISCONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTED == current.state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_started();
    if( ESP_OK == result && a_randomize_mac )
    {
        result = restart_with_random_mac();
    }
    if( ESP_OK != result )
    {
        return result;
    }

    wifi_config_t config = { 0 };
    memcpy( config.sta.ssid, a_bss->ssid, ssid_length );
    memcpy( config.sta.bssid, a_bss->bssid, sizeof( config.sta.bssid ) );
    config.sta.bssid_set = true;
    config.sta.channel = a_bss->channel;
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.threshold.authmode =
        ( RFSCOUT_WIFI_STATION_SECURITY_OWE == a_bss->security ) ? WIFI_AUTH_OWE : WIFI_AUTH_OPEN;
    config.sta.owe_enabled = RFSCOUT_WIFI_STATION_SECURITY_OWE == a_bss->security;
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
    memcpy( status.ssid, a_bss->ssid, ssid_length );
    status.ntp_syncing = false;
    status.ntp_synced = false;
    status.state = RFSCOUT_WIFI_STATION_CONNECTING;
    portEXIT_CRITICAL( &state_lock );

    result = esp_wifi_connect();
    if( ESP_OK != result )
    {
        set_state( RFSCOUT_WIFI_STATION_FAILED, (int32_t)result );
    }
    return result;
}

esp_err_t rfscout_wifi_station_start_time_sync( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }
    stop_ntp_sync();
    return start_ntp_sync();
}

esp_err_t rfscout_wifi_station_cancel_scan( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &state_lock );
    if( !started || ( RFSCOUT_WIFI_STATION_SCANNING != status.state &&
                      RFSCOUT_WIFI_STATION_SEARCHING != status.state ) )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }
    status.state = RFSCOUT_WIFI_STATION_IDLE;
    status.disconnect_reason = 0;
    memset( active_search_ssid, 0, sizeof( active_search_ssid ) );
    portEXIT_CRITICAL( &state_lock );

    (void)esp_wifi_scan_stop();
    (void)esp_wifi_clear_ap_list();
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_ip_info( rfscout_wifi_station_ip_info_t *a_info )
{
    if( NULL == a_info )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_info, 0, sizeof( *a_info ) );

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );
    if( NULL == station_netif || RFSCOUT_WIFI_STATION_CONNECTED != current.state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t info;
    const esp_err_t result = esp_netif_get_ip_info( station_netif, &info );
    if( ESP_OK == result )
    {
        a_info->address = info.ip.addr;
        a_info->netmask = info.netmask.addr;
        a_info->gateway = info.gw.addr;

        esp_netif_dns_info_t dns;
        if( ESP_OK == esp_netif_get_dns_info( station_netif, ESP_NETIF_DNS_MAIN, &dns ) &&
            ESP_IPADDR_TYPE_V4 == dns.ip.type )
        {
            a_info->dns_primary = dns.ip.u_addr.ip4.addr;
        }
        memset( &dns, 0, sizeof( dns ) );
        if( ESP_OK == esp_netif_get_dns_info( station_netif, ESP_NETIF_DNS_BACKUP, &dns ) &&
            ESP_IPADDR_TYPE_V4 == dns.ip.type )
        {
            a_info->dns_backup = dns.ip.u_addr.ip4.addr;
        }
    }
    return result;
}

esp_err_t
rfscout_wifi_station_connect( const char *a_ssid, const char *a_password, bool a_randomize_mac )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
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

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );
    if( RFSCOUT_WIFI_STATION_SCANNING == current.state ||
        RFSCOUT_WIFI_STATION_SEARCHING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_DISCONNECTING == current.state ||
        RFSCOUT_WIFI_STATION_CONNECTED == current.state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_started();
    if( ESP_OK == result && a_randomize_mac )
    {
        result = restart_with_random_mac();
    }
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

esp_err_t rfscout_wifi_station_cancel_time_sync( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &state_lock );
    const bool can_cancel = started && RFSCOUT_WIFI_STATION_CONNECTED == status.state;
    portEXIT_CRITICAL( &state_lock );
    if( !can_cancel )
    {
        return ESP_ERR_INVALID_STATE;
    }

    stop_ntp_sync();
    return ESP_OK;
}

esp_err_t rfscout_wifi_station_stop( void )
{
    if( !initialized )
    {
        return ESP_OK;
    }

    portENTER_CRITICAL( &state_lock );
    const bool was_started = started;
    const bool drain_was_pending = event_drain_pending;
    stopping = true;
    portEXIT_CRITICAL( &state_lock );

    if( was_started )
    {
        (void)esp_wifi_scan_stop();
        (void)esp_wifi_clear_ap_list();

        const esp_err_t stop_result = esp_wifi_stop();
        if( ESP_OK != stop_result )
        {
            portENTER_CRITICAL( &state_lock );
            if( RFSCOUT_WIFI_STATION_SCANNING == status.state ||
                RFSCOUT_WIFI_STATION_SEARCHING == status.state )
            {
                status.state = RFSCOUT_WIFI_STATION_IDLE;
                status.disconnect_reason = 0;
            }
            stopping = false;
            portEXIT_CRITICAL( &state_lock );
            return stop_result;
        }

        portENTER_CRITICAL( &state_lock );
        started = false;
        event_drain_pending = true;
        portEXIT_CRITICAL( &state_lock );

        wifi_config_t blank = { 0 };
        (void)esp_wifi_set_config( WIFI_IF_STA, &blank );
        memset( &blank, 0, sizeof( blank ) );
        restore_factory_mac();
    }

    if( was_started || drain_was_pending )
    {
        const esp_err_t drain_result = drain_default_event_loop();
        if( ESP_OK != drain_result )
        {
            stop_ntp_sync();
            portENTER_CRITICAL( &state_lock );
            stopping = false;
            portEXIT_CRITICAL( &state_lock );
            return drain_result;
        }

        portENTER_CRITICAL( &state_lock );
        event_drain_pending = false;
        portEXIT_CRITICAL( &state_lock );
    }

    stop_ntp_sync();

    portENTER_CRITICAL( &state_lock );
    disconnect_requested = false;
    status.state = RFSCOUT_WIFI_STATION_OFF;
    status.disconnect_reason = 0;
    memset( status.ssid, 0, sizeof( status.ssid ) );
    status.ntp_syncing = false;
    status.ntp_synced = false;
    scan_result_count = 0U;
    memset( scan_results, 0, sizeof( scan_results ) );
    bss_result_count = 0U;
    memset( bss_results, 0, sizeof( bss_results ) );
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

void rfscout_wifi_station_status( rfscout_wifi_station_status_t *a_status )
{
    if( NULL == a_status )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    *a_status = status;
    portEXIT_CRITICAL( &state_lock );
}
