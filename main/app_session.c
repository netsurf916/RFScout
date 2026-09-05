/**
    app_session.c : RFScout application resource session
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_session.h"

#include "app_framework.h"
#include "app_manager.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "memory_manager.h"
#include "nrf24.h"
#include "radio_monitor.h"
#include "recorder.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_SESSION_RECORDER_QUIESCE_TIMEOUT_MS 5000U

typedef esp_err_t ( *app_session_service_fn )( void *a_context );
typedef bool ( *app_session_service_active_fn )( void *a_context );

typedef struct
{
    uint32_t capability;
    const char *name;
    app_session_service_fn start;
    app_session_service_fn stop;
    app_session_service_fn recover_failed_stop;
    app_session_service_active_fn active;
    void *context;
    bool always_stop_for_close;
    bool was_active_for_close;
    bool stopped_for_close;
    bool failed_for_close;
} app_session_service_t;

static const char *TAG = "app_session";
static rfscout_app_id_t session_app = RFSCOUT_APP_COUNT;
static uint32_t session_generation;
static uint32_t session_services;
static bool close_in_progress;
static bool ready_to_reclaim;

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

static bool wifi_active( void *a_context )
{
    (void)a_context;
    return radio_monitor_is_running();
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

static bool ble_active( void *a_context )
{
    (void)a_context;
    return ble_monitor_is_running();
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

static bool recorder_references_clear( void )
{
    return ESP_ERR_INVALID_STATE != recorder_probe_microphone();
}

static bool audio_capture_active( void *a_context )
{
    (void)a_context;
    const rfscout_recorder_state_t state = recorder_state();
    return ( RFSCOUT_RECORDER_RECORDING == state ) || ( RFSCOUT_RECORDER_SAVING == state ) ||
           ( RFSCOUT_RECORDER_MONITORING == state ) || ( RFSCOUT_RECORDER_PLAYING == state );
}

static esp_err_t stop_recorder( void *a_context )
{
    (void)a_context;

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

static app_session_service_t platform_services[] = {
    {
        .capability = RFSCOUT_APP_SERVICE_WIFI_MONITOR,
        .name = "Wi-Fi",
        .start = start_wifi,
        .stop = stop_wifi,
        .recover_failed_stop = start_wifi,
        .active = wifi_active,
        .always_stop_for_close = true,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_BLE_MONITOR,
        .name = "BLE",
        .start = start_ble,
        .stop = stop_ble,
        .recover_failed_stop = start_ble,
        .active = ble_active,
        .always_stop_for_close = true,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_CC1101,
        .name = "CC1101",
        .start = start_cc1101,
        .stop = stop_cc1101,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_NRF24,
        .name = "nRF24",
        .start = start_nrf24,
        .stop = stop_nrf24,
    },
    {
        .capability = RFSCOUT_APP_SERVICE_AUDIO_CAPTURE,
        .name = "Audio capture",
        .stop = stop_recorder,
        .active = audio_capture_active,
    },
};

static size_t platform_service_count( void )
{
    return sizeof( platform_services ) / sizeof( platform_services[ 0 ] );
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
        platform_services[ i ].stopped_for_close = false;
        platform_services[ i ].failed_for_close = false;
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

