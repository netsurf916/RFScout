/**
    app_session.c : RFScout application resource session
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_session.h"

#include "ble_monitor.h"
#include "cc1101.h"
#include "memory_manager.h"
#include "nrf24.h"
#include "radio_monitor.h"

#include "esp_log.h"

#include <string.h>

#define APP_SESSION_MAX_SERVICES 8U

typedef struct
{
    const char *name;
    rfscout_app_service_fn start;
    rfscout_app_service_fn stop;
    void *context;
} app_session_service_t;

static const char *TAG = "app_session";
static rfscout_app_id_t session_app = RFSCOUT_APP_COUNT;
static app_session_service_t services[ APP_SESSION_MAX_SERVICES ];
static size_t service_count;
static size_t close_attempted_count;
static bool ready_to_reclaim;

static bool session_active( void )
{
    return ( session_app >= 0 ) && ( session_app < RFSCOUT_APP_COUNT );
}

static rfscout_memory_owner_t session_owner( void )
{
    return memory_manager_app_owner( session_app );
}

static esp_err_t start_wifi( void *a_context )
{
    (void)a_context;
    return radio_monitor_start();
}

static esp_err_t stop_wifi( void *a_context )
{
    (void)a_context;
    return radio_monitor_stop();
}

static esp_err_t start_ble( void *a_context )
{
    (void)a_context;
    return ble_monitor_start();
}

static esp_err_t stop_ble( void *a_context )
{
    (void)a_context;
    return ble_monitor_stop();
}

static esp_err_t start_cc1101( void *a_context )
{
    (void)a_context;
    return cc1101_start();
}

static esp_err_t stop_cc1101( void *a_context )
{
    (void)a_context;
    return cc1101_stop() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t start_nrf24( void *a_context )
{
    (void)a_context;
    return nrf24_start();
}

static esp_err_t stop_nrf24( void *a_context )
{
    (void)a_context;
    return nrf24_stop() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t register_platform_services( rfscout_app_id_t a_app )
{
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
        {
            esp_err_t result = app_session_register_service( "Wi-Fi", start_wifi, stop_wifi, NULL );
            if( ESP_OK == result )
            {
                result = app_session_register_service( "BLE", start_ble, stop_ble, NULL );
            }
            return result;
        }
        case RFSCOUT_APP_CC1101:
            return app_session_register_service( "CC1101", start_cc1101, stop_cc1101, NULL );
        case RFSCOUT_APP_NRF24:
            return app_session_register_service( "nRF24", start_nrf24, stop_nrf24, NULL );
        case RFSCOUT_APP_SYSTEM_EVENTS:
        case RFSCOUT_APP_AUDIO:
        case RFSCOUT_APP_THERMAL:
        case RFSCOUT_APP_DEVICE:
        case RFSCOUT_APP_COUNT:
        default:
            return ESP_OK;
    }
}

static void reset_session_state( void )
{
    memset( services, 0, sizeof( services ) );
    service_count = 0U;
    close_attempted_count = 0U;
    ready_to_reclaim = false;
}

esp_err_t app_session_ensure( rfscout_app_id_t a_app )
{
    if( ( a_app < 0 ) || ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( session_active() )
    {
        if( session_app == a_app )
        {
            return ESP_OK;
        }

        if( ( 0U != memory_manager_app_allocation_count( session_app ) ) ||
            ( 0U != close_attempted_count ) || ready_to_reclaim )
        {
            return ESP_ERR_INVALID_STATE;
        }

        /* Startup/session restoration can select the final app after lifecycle initialization.
           Rebinding an unused descriptor-only session is safe because it owns no app memory and
           no close transaction has begun. */
        reset_session_state();
        session_app = RFSCOUT_APP_COUNT;
    }

    session_app = a_app;
    reset_session_state();

    const esp_err_t result = register_platform_services( a_app );
    if( ESP_OK != result )
    {
        reset_session_state();
        session_app = RFSCOUT_APP_COUNT;
    }
    return result;
}

static bool ensure_current_app_session( void )
{
    return ESP_OK == app_session_ensure( app_manager_current() );
}

void *app_session_calloc( size_t a_count, size_t a_size )
{
    return ensure_current_app_session()
               ? memory_manager_calloc( session_owner(), a_count, a_size )
               : NULL;
}

void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size )
{
    return ensure_current_app_session()
               ? memory_manager_resize( session_owner(), a_pointer, a_count, a_size )
               : NULL;
}

bool app_session_free( void *a_pointer )
{
    return ensure_current_app_session() && memory_manager_free( session_owner(), a_pointer );
}

esp_err_t app_session_register_service( const char *a_name,
                                        rfscout_app_service_fn a_start,
                                        rfscout_app_service_fn a_stop,
                                        void *a_context )
{
    if( !session_active() || ( NULL == a_stop ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    for( size_t i = 0U; i < service_count; ++i )
    {
        if( ( services[ i ].stop == a_stop ) && ( services[ i ].context == a_context ) )
        {
            return ESP_OK;
        }
    }
    if( service_count >= APP_SESSION_MAX_SERVICES )
    {
        ESP_LOGE( TAG, "app=%d exceeded platform service slots", (int)session_app );
        return ESP_ERR_NO_MEM;
    }

    services[ service_count++ ] = ( app_session_service_t ){
        .name = a_name,
        .start = a_start,
        .stop = a_stop,
        .context = a_context,
    };
    return ESP_OK;
}

esp_err_t app_session_prepare_close( void )
{
    if( !session_active() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    ready_to_reclaim = false;
    close_attempted_count = 0U;
    for( size_t i = service_count; i > 0U; --i )
    {
        app_session_service_t *service = &services[ i - 1U ];
        ++close_attempted_count;
        const esp_err_t result = service->stop( service->context );
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "app=%d service=%s did not close safely: %s",
                      (int)session_app,
                      ( NULL != service->name ) ? service->name : "unnamed",
                      esp_err_to_name( result ) );
            return result;
        }
    }

    ready_to_reclaim = true;
    return ESP_OK;
}

esp_err_t app_session_abort_close( void )
{
    if( !session_active() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t first_error = ESP_OK;
    const size_t first_attempted = service_count - close_attempted_count;
    for( size_t i = first_attempted; i < service_count; ++i )
    {
        app_session_service_t *service = &services[ i ];
        if( NULL == service->start )
        {
            continue;
        }

        const esp_err_t result = service->start( service->context );
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "app=%d service=%s could not reactivate after close abort: %s",
                      (int)session_app,
                      ( NULL != service->name ) ? service->name : "unnamed",
                      esp_err_to_name( result ) );
            if( ESP_OK == first_error )
            {
                first_error = result;
            }
        }
    }

    close_attempted_count = 0U;
    ready_to_reclaim = false;
    return first_error;
}

bool app_session_ready_to_reclaim( void )
{
    return session_active() && ready_to_reclaim;
}

void app_session_commit_close( void )
{
    if( !ready_to_reclaim )
    {
        ESP_LOGE( TAG, "refusing to commit an app session that has not quiesced" );
        return;
    }

    reset_session_state();
    session_app = RFSCOUT_APP_COUNT;
}
