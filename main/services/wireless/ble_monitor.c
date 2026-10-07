/**
    ble_monitor.c : RFScout passive BLE collector
    Description: Normalizes BLE advertisements including names, manufacturer data, and services.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ble_monitor.h"
#include "surveillance_database.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include <string.h>

#define BLE_AD_TYPE_UUID16_INCOMPLETE 0x02U
#define BLE_AD_TYPE_UUID16_COMPLETE 0x03U
#define BLE_AD_TYPE_UUID128_INCOMPLETE 0x06U
#define BLE_AD_TYPE_UUID128_COMPLETE 0x07U
#define BLE_AD_TYPE_SERVICE_DATA16 0x16U
#define BLE_AD_TYPE_SERVICE_DATA128 0x21U
#define BLE_AD_TYPE_NAME_SHORT 0x08U
#define BLE_AD_TYPE_NAME_COMPLETE 0x09U
#define BLE_AD_TYPE_MANUFACTURER 0xFFU
#define OPEN_DRONE_ID_UUID 0xFFFAU
#define OPEN_DRONE_ID_APP_CODE 0x0DU
#define BLE_CALLBACK_QUIESCE_TIMEOUT_MS 1000U

static const char *TAG = "ble_monitor";
static uint8_t own_addr_type;
static ble_observation_fn sink;
static bool initialized;
static bool host_synced;
static bool scan_requested;
static bool scanning;
static portMUX_TYPE callback_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t observation_callbacks_inflight;

static void add_service_uuid( rf_device_t *a_observation, uint16_t a_uuid )
{
    for( size_t index = 0U; index < a_observation->service_uuid_count; ++index )
    {
        if( a_observation->service_uuids[ index ] == a_uuid )
        {
            return;
        }
    }

    if( a_observation->service_uuid_count < RFSCOUT_BLE_SERVICE_UUID_MAX )
    {
        a_observation->service_uuids[ a_observation->service_uuid_count++ ] = a_uuid;
    }
}

static void parse_adv( const struct ble_gap_disc_desc *a_description, rf_device_t *a_observation )
{
    memset( a_observation, 0, sizeof( *a_observation ) );

    for( size_t index = 0U; index < 6U; ++index )
    {
        a_observation->address[ index ] = a_description->addr.val[ 5U - index ];
    }

    a_observation->address_type = a_description->addr.type;
    a_observation->last_source = RF_SOURCE_BLE;
    a_observation->source_mask = RF_SOURCE_BIT( RF_SOURCE_BLE );
    a_observation->rssi = a_description->rssi;
    a_observation->first_seen_ms = (uint32_t)( esp_timer_get_time() / 1000ULL );
    a_observation->last_seen_ms = a_observation->first_seen_ms;

    size_t offset = 0U;
    while( offset < a_description->length_data )
    {
        const uint8_t length = a_description->data[ offset ];
        if( ( 0U == length ) || ( offset + 1U + length > a_description->length_data ) )
        {
            break;
        }

        const uint8_t type = a_description->data[ offset + 1U ];
        const uint8_t *data = &a_description->data[ offset + 2U ];
        size_t data_length = length - 1U;

        if( ( ( BLE_AD_TYPE_NAME_SHORT == type ) || ( BLE_AD_TYPE_NAME_COMPLETE == type ) ) &&
            ( data_length > 0U ) )
        {
            if( data_length >= sizeof( a_observation->name ) )
            {
                data_length = sizeof( a_observation->name ) - 1U;
            }

            memcpy( a_observation->name, data, data_length );
            a_observation->name[ data_length ] = '\0';
            a_observation->has_name = true;
        }
        else if( ( BLE_AD_TYPE_UUID16_INCOMPLETE == type ) ||
                 ( BLE_AD_TYPE_UUID16_COMPLETE == type ) )
        {
            for( size_t index = 0U; index + 1U < data_length; index += 2U )
            {
                add_service_uuid( a_observation,
                                  (uint16_t)data[ index ] |
                                      ( (uint16_t)data[ index + 1U ] << 8U ) );
            }
        }
        else if( ( BLE_AD_TYPE_UUID128_INCOMPLETE == type ) ||
                 ( BLE_AD_TYPE_UUID128_COMPLETE == type ) )
        {
            for( size_t index = 0U; index + 15U < data_length; index += 16U )
            {
                surveillance_database_match_service_uuid128( a_observation, &data[ index ] );
            }
        }
        else if( ( BLE_AD_TYPE_SERVICE_DATA128 == type ) && ( data_length >= 16U ) )
        {
            surveillance_database_match_service_uuid128( a_observation, data );
        }
        else if( ( BLE_AD_TYPE_SERVICE_DATA16 == type ) && ( data_length >= 2U ) )
        {
            const uint16_t uuid = (uint16_t)data[ 0 ] | ( (uint16_t)data[ 1 ] << 8U );
            add_service_uuid( a_observation, uuid );

            if( ( OPEN_DRONE_ID_UUID == uuid ) && ( data_length >= 3U ) &&
                ( OPEN_DRONE_ID_APP_CODE == data[ 2 ] ) )
            {
                a_observation->has_remote_id = true;
            }
        }
        else if( ( BLE_AD_TYPE_MANUFACTURER == type ) && ( data_length >= 2U ) )
        {
            a_observation->company_id = (uint16_t)data[ 0 ] | ( (uint16_t)data[ 1 ] << 8U );
            data_length -= 2U;

            if( data_length > sizeof( a_observation->manufacturer_data ) )
            {
                data_length = sizeof( a_observation->manufacturer_data );
            }

            memcpy( a_observation->manufacturer_data, data + 2, data_length );
            a_observation->manufacturer_data_len = (uint8_t)data_length;
            a_observation->has_manufacturer_data = true;
        }

        offset += (size_t)length + 1U;
    }
}

static void start_scan( void );

static bool begin_observation_callback( void )
{
    bool deliver;
    portENTER_CRITICAL( &callback_lock );
    deliver = scan_requested;
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

static bool scan_is_requested( void )
{
    bool requested;
    portENTER_CRITICAL( &callback_lock );
    requested = scan_requested;
    portEXIT_CRITICAL( &callback_lock );
    return requested;
}

static uint32_t observation_callbacks_active( void )
{
    uint32_t active;
    portENTER_CRITICAL( &callback_lock );
    active = observation_callbacks_inflight;
    portEXIT_CRITICAL( &callback_lock );
    return active;
}

static int gap_event( struct ble_gap_event *a_event, void *a_argument )
{
    (void)a_argument;

    if( BLE_GAP_EVENT_DISC == a_event->type )
    {
        if( begin_observation_callback() )
        {
            rf_device_t observation;
            parse_adv( &a_event->disc, &observation );
            if( NULL != sink )
            {
                sink( &observation );
            }
            end_observation_callback();
        }
    }
    else if( BLE_GAP_EVENT_DISC_COMPLETE == a_event->type )
    {
        scanning = false;
        if( scan_is_requested() )
        {
            start_scan();
        }
    }
    return 0;
}

static void start_scan( void )
{
    if( !host_synced || !scan_is_requested() || scanning )
    {
        return;
    }

    struct ble_gap_disc_params parameters = { .itvl = 0x0040,
                                              .window = 0x0040,
                                              .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
                                              .limited = 0,
                                              .passive = 1,
                                              .filter_duplicates = 0 };

    const int result = ble_gap_disc( own_addr_type, BLE_HS_FOREVER, &parameters, gap_event, NULL );
    if( 0 != result )
    {
        ESP_LOGE( TAG, "scan start failed: %d", result );
    }
    else
    {
        scanning = true;
        ESP_LOGI( TAG, "passive BLE monitor enabled" );
    }
}

static void on_sync( void )
{
    int result = ble_hs_util_ensure_addr( 0 );
    if( 0 == result )
    {
        result = ble_hs_id_infer_auto( 0, &own_addr_type );
    }

    if( 0 != result )
    {
        ESP_LOGE( TAG, "identity setup failed: %d", result );
    }
    else
    {
        host_synced = true;
        start_scan();
    }
}

static void host_task( void *a_argument )
{
    (void)a_argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ble_monitor_is_active( void )
{
    return scan_is_requested();
}

esp_err_t ble_monitor_start( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &callback_lock );
    scan_requested = true;
    portEXIT_CRITICAL( &callback_lock );
    start_scan();
    return ESP_OK;
}

esp_err_t ble_monitor_stop( void )
{
    portENTER_CRITICAL( &callback_lock );
    scan_requested = false;
    portEXIT_CRITICAL( &callback_lock );

    if( host_synced && scanning )
    {
        const int result = ble_gap_disc_cancel();
        if( ( 0 != result ) && ( BLE_HS_EALREADY != result ) )
        {
            ESP_LOGW( TAG, "scan stop failed: %d", result );
        }
        scanning = false;
    }

    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS( BLE_CALLBACK_QUIESCE_TIMEOUT_MS );
    while( observation_callbacks_active() > 0U )
    {
        if( ( xTaskGetTickCount() - started ) >= timeout )
        {
            ESP_LOGE( TAG,
                      "BLE observation callbacks did not quiesce within %u ms",
                      (unsigned)BLE_CALLBACK_QUIESCE_TIMEOUT_MS );
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( 1U ) );
    }

    ESP_LOGI( TAG, "BLE scan stopped for inactive app" );
    return ESP_OK;
}

void ble_monitor_set_observer( ble_observation_fn a_callback )
{
    sink = a_callback;
}

esp_err_t ble_monitor_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }
    if( NULL == sink )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &callback_lock );
    scan_requested = false;
    observation_callbacks_inflight = 0U;
    portEXIT_CRITICAL( &callback_lock );

    const int result = nimble_port_init();
    if( 0 != result )
    {
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init( host_task );
    initialized = true;
    return ESP_OK;
}
