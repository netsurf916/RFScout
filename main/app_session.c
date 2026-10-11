/**
    app_session.c : RFScout application resource and presentation session
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_session.h"

#include "app_framework.h"
#include "app_manager.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "connectivity_probe.h"
#include "display.h"
#include "memory_manager.h"
#include "network_session.h"
#include "nrf24.h"
#include "ota_acquisition.h"
#include "ota_installer.h"
#include "radio_monitor.h"
#include "recorder.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_SESSION_RECORDER_QUIESCE_TIMEOUT_MS 5000U

typedef esp_err_t ( *app_session_service_fn )( void );
typedef bool ( *app_session_service_active_fn )( void );

/**
 * @brief Uniform lifecycle adapter for one app-owned platform service.
 * @details The framework owns idempotent inactive init. Persistent services additionally provide
 *          app-driven idempotent start plus stop/is_active. Parameterized transactional services
 *          omit generic start because their request cannot be reconstructed; stop remains
 *          idempotent and is always invoked during app close.
 */
typedef struct
{
    uint32_t capability;
    const char *name;
    app_session_service_fn init;
    app_session_service_fn start;
    app_session_service_fn stop;
    app_session_service_active_fn active;
    bool was_active_for_close;
} app_session_service_t;

static const char *TAG = "app_session";
static rfscout_app_id_t session_app = RFSCOUT_APP_COUNT;
static const rfscout_view_t *session_view;
static uint32_t session_generation;
static uint32_t session_services;
static bool close_in_progress;
static bool ready_to_reclaim;

/* Stable provider hooks; applications never register with wireless drivers. */
static void wifi_observation_dispatch( rf_device_t *a_observation )
{
    const rfscout_app_descriptor_t *app = app_framework_descriptor( session_app );
    if( !close_in_progress && app && app->consumers.wifi_observation )
        app->consumers.wifi_observation( a_observation );
}

static void ble_observation_dispatch( rf_device_t *a_observation )
{
    const rfscout_app_descriptor_t *app = app_framework_descriptor( session_app );
    if( !close_in_progress && app && app->consumers.ble_observation )
        app->consumers.ble_observation( a_observation );
}

static void wifi_raw_dispatch( const uint8_t *bytes, size_t length, int8_t rssi, uint8_t channel )
{
    const rfscout_app_descriptor_t *app = app_framework_descriptor( session_app );
    if( !close_in_progress && app && app->consumers.wifi_raw )
        app->consumers.wifi_raw( bytes, length, rssi, channel );
}

static void ble_raw_dispatch( const uint8_t *bytes, size_t length, const uint8_t address[6],
                              uint8_t address_type, uint8_t event_type, int8_t rssi )
{
    const rfscout_app_descriptor_t *app = app_framework_descriptor( session_app );
    if( !close_in_progress && app && app->consumers.ble_raw )
        app->consumers.ble_raw( bytes, length, address, address_type, event_type, rssi );
}

static void bind_session_consumers( const rfscout_app_descriptor_t *app )
{
    radio_monitor_set_observer( app && app->consumers.wifi_observation ? wifi_observation_dispatch : NULL );
    radio_monitor_set_raw_observer( app && app->consumers.wifi_raw ? wifi_raw_dispatch : NULL );
    ble_monitor_set_observer( app && app->consumers.ble_observation ? ble_observation_dispatch : NULL );
    ble_monitor_set_raw_observer( app && app->consumers.ble_raw ? ble_raw_dispatch : NULL );
}

static esp_err_t stop_cc1101( void )
{
    return cc1101_stop() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t stop_nrf24( void )
{
    return nrf24_stop() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool recorder_references_clear( void )
{
    return ESP_ERR_INVALID_STATE != recorder_probe_microphone();
}

static esp_err_t stop_recorder( void )
{
    if( !recorder_prepare_storage_shutdown( APP_SESSION_RECORDER_QUIESCE_TIMEOUT_MS ) )
    {
        return ESP_ERR_TIMEOUT;
    }
    recorder_stop();

    const int64_t deadline_us =
        esp_timer_get_time() + (int64_t)APP_SESSION_RECORDER_QUIESCE_TIMEOUT_MS * 1000LL;
    while( !recorder_references_clear() )
    {
        if( esp_timer_get_time() >= deadline_us )
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( 10U ) );
    }
    return ESP_OK;
}

/* Services close in reverse table order. Dependencies therefore appear before their consumers:
   active HTTPS work is stopped before the network session, and persistent services can be restarted
   with the same idempotent start() operation if a later close step causes the transition to abort. */
static app_session_service_t platform_services[] = {
    {
        .capability = RFSCOUT_APP_SERVICE_WIFI_MONITOR,
        .name = "Wi-Fi",
        .init = radio_monitor_init,
        .start = radio_monitor_start,
        .stop = radio_monitor_stop,
        .active = radio_monitor_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_NETWORK_SESSION,
        .name = "Network session",
        .init = network_session_init,
        .stop = network_session_release,
        .active = network_session_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_CONNECTIVITY_PROBE,
        .name = "Connectivity probe",
        .init = connectivity_probe_init,
        .stop = connectivity_probe_stop,
        .active = connectivity_probe_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_OTA_INSTALLER,
        .name = "OTA installer",
        .init = ota_installer_init,
        .stop = ota_installer_abort,
        .active = ota_installer_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_OTA_ACQUISITION,
        .name = "OTA acquisition",
        .init = ota_acquisition_init,
        .stop = ota_acquisition_stop,
        .active = ota_acquisition_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_BLE_MONITOR,
        .name = "BLE",
        .init = ble_monitor_init,
        .start = ble_monitor_start,
        .stop = ble_monitor_stop,
        .active = ble_monitor_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_CC1101,
        .name = "CC1101",
        .init = cc1101_init,
        .start = cc1101_start,
        .stop = stop_cc1101,
        .active = cc1101_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_NRF24,
        .name = "nRF24",
        .init = nrf24_init,
        .start = nrf24_start,
        .stop = stop_nrf24,
        .active = nrf24_is_active,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_AUDIO_CAPTURE,
        .name = "Audio capture",
        .init = recorder_init,
        .stop = stop_recorder,
        .active = recorder_is_active,
    },
};

static size_t platform_service_count( void )
{
    return sizeof( platform_services ) / sizeof( platform_services[ 0 ] );
}

static bool service_selected( const app_session_service_t *a_service );

static esp_err_t init_selected_services( void )
{
    for( size_t i = 0U; i < platform_service_count(); ++i )
    {
        app_session_service_t *service = &platform_services[ i ];
        if( !service_selected( service ) || NULL == service->init )
        {
            continue;
        }

        const esp_err_t result = service->init();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "app=%d service=%s initialization failed: %s",
                      (int)session_app,
                      service->name,
                      esp_err_to_name( result ) );
            return result;
        }
    }
    return ESP_OK;
}

static bool session_active( void )
{
    return ( session_app >= 0 ) && ( session_app < RFSCOUT_APP_COUNT );
}

static rfscout_memory_owner_t session_owner( void )
{
    return memory_manager_app_owner( session_app );
}

static bool service_selected( const app_session_service_t *a_service )
{
    return 0U != ( session_services & a_service->capability );
}

static void clear_close_state( void )
{
    for( size_t i = 0U; i < platform_service_count(); ++i )
    {
        platform_services[ i ].was_active_for_close = false;
    }
}

static void reset_session_state( void )
{
    clear_close_state();
    session_services = RFSCOUT_APP_SERVICE_NONE;
    close_in_progress = false;
    ready_to_reclaim = false;
}

static void advance_generation( void )
{
    ++session_generation;
    if( 0U == session_generation )
    {
        ++session_generation;
    }
}

static esp_err_t acquire_view( void )
{
    session_view = display_app_view();
    if( NULL == session_view )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( NULL != session_view->init )
    {
        const esp_err_t result = session_view->init( session_view );
        if( ESP_OK != result )
        {
            session_view = NULL;
            return result;
        }
    }
    return ESP_OK;
}

static void release_view( void )
{
    if( NULL != session_view && NULL != session_view->deinit )
    {
        session_view->deinit( session_view );
    }
    session_view = NULL;
}

esp_err_t app_session_ensure( rfscout_app_id_t a_app )
{
    if( ( a_app < 0 ) || ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( NULL == descriptor )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( session_active() )
    {
        if( session_app == a_app )
        {
            return ESP_OK;
        }
        if( ( 0U != memory_manager_app_allocation_count( session_app ) ) || close_in_progress ||
            ready_to_reclaim )
        {
            return ESP_ERR_INVALID_STATE;
        }
        release_view();
        reset_session_state();
        session_app = RFSCOUT_APP_COUNT;
    }

    session_app = a_app;
    reset_session_state();
    session_services = descriptor->services;
    bind_session_consumers( descriptor );

    const esp_err_t init_result = init_selected_services();
    if( ESP_OK != init_result )
    {
        reset_session_state();
        session_app = RFSCOUT_APP_COUNT;
        return init_result;
    }

    const esp_err_t view_result = acquire_view();
    if( ESP_OK != view_result )
    {
        reset_session_state();
        session_app = RFSCOUT_APP_COUNT;
        return view_result;
    }
    advance_generation();
    return ESP_OK;
}

rfscout_app_id_t app_session_app( void )
{
    return session_app;
}

const rfscout_view_t *app_session_view( void )
{
    return session_active() ? session_view : NULL;
}

uint32_t app_session_generation( void )
{
    return session_generation;
}

static bool ensure_current_app_session( void )
{
    return ESP_OK == app_session_ensure( app_manager_current() );
}

void *app_session_calloc( size_t a_count, size_t a_size )
{
    return ensure_current_app_session() ? memory_manager_calloc( session_owner(), a_count, a_size )
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

esp_err_t app_session_prepare_close( void )
{
    if( !session_active() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    close_in_progress = true;
    ready_to_reclaim = false;
    clear_close_state();

    for( size_t i = platform_service_count(); i > 0U; --i )
    {
        app_session_service_t *service = &platform_services[ i - 1U ];
        if( !service_selected( service ) )
        {
            continue;
        }

        service->was_active_for_close = ( NULL != service->active ) && service->active();

        /* stop() is the uniform quiesce operation and must be idempotent. Call it for every
           selected service so transactional services without a generic start() can also clean
           residual state before app memory is reclaimed. */
        const esp_err_t result = service->stop();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "app=%d service=%s did not close safely: %s",
                      (int)session_app,
                      service->name,
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
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;
    for( size_t i = 0U; i < platform_service_count(); ++i )
    {
        app_session_service_t *service = &platform_services[ i ];
        if( !service_selected( service ) )
        {
            continue;
        }

        if( !service->was_active_for_close || NULL == service->start )
        {
            continue;
        }

        const esp_err_t result = service->start();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG,
                      "app=%d service=%s could not reactivate after close abort: %s",
                      (int)session_app,
                      service->name,
                      esp_err_to_name( result ) );
            if( ESP_OK == first_error )
            {
                first_error = result;
            }
        }
    }

    clear_close_state();
    close_in_progress = false;
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

    bind_session_consumers( NULL );
    release_view();
    reset_session_state();
    session_app = RFSCOUT_APP_COUNT;
}
