/**
    app_manager.c : RFScout application view manager implementation
    Description: Owns full-screen app order, availability, and hardware lifecycle.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "ble_monitor.h"
#include "mlx90640.h"
#include "platform.h"
#include "radio_app_view.h"
#include "radio_monitor.h"
#include "thermal_view.h"
#include "ui.h"

#include "esp_log.h"
#include "esp_timer.h"

#define APP_REFRESH_INTERVAL_US 125000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static bool cc1101_settings_open;

static bool probe_thermal( void )
{
    const bool was_available = mlx90640_is_detected();
    const bool available = ( ESP_OK == mlx90640_probe() );
    if( available && !was_available ) ESP_LOGI( TAG, "Thermal app available" );
    else if( !available && was_available ) ESP_LOGW( TAG, "Thermal app unavailable" );
    return available;
}

static bool app_available( rfscout_app_id_t a_app )
{
    return ( RFSCOUT_APP_THERMAL != a_app ) || mlx90640_is_detected();
}

static void leave_app( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        (void)ble_monitor_stop();
        (void)radio_monitor_stop();
    }
    cc1101_settings_open = false;
}

static void enter_app( rfscout_app_id_t a_app )
{
    last_app_refresh_us = 0;
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            (void)radio_monitor_start();
            (void)ble_monitor_start();
            ESP_LOGI( TAG, "switched to WiFi/BT app" );
            break;
        case RFSCOUT_APP_CC1101:
            radio_app_view_cc1101( false );
            ESP_LOGI( TAG, "switched to CC1101 placeholder app" );
            break;
        case RFSCOUT_APP_NRF24:
            radio_app_view_nrf24();
            ESP_LOGI( TAG, "switched to nRF24 placeholder app" );
            break;
        case RFSCOUT_APP_THERMAL:
            thermal_view_enter();
            ESP_LOGI( TAG, "switched to Thermal app" );
            break;
        default:
            break;
    }
}

static rfscout_app_id_t next_available_app( int a_direction, int64_t a_now_us )
{
    int next = (int)active_app;
    for( int attempts = 0; attempts < RFSCOUT_APP_COUNT; ++attempts )
    {
        next += a_direction;
        if( next < 0 ) next = RFSCOUT_APP_COUNT - 1;
        else if( next >= RFSCOUT_APP_COUNT ) next = 0;
        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;
        if( RFSCOUT_APP_THERMAL == candidate && !app_available( candidate ) )
        {
            last_thermal_probe_us = a_now_us;
            if( !probe_thermal() ) continue;
        }
        return candidate;
    }
    return active_app;
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_WIFI_BT;
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();
    cc1101_settings_open = false;
    const esp_err_t result = ui_init();
    if( ESP_OK != result ) return result;
    (void)mlx90640_init();
    if( mlx90640_is_detected() ) ESP_LOGI( TAG, "Thermal app available" );
    return ESP_OK;
}

rfscout_app_id_t app_manager_current( void ) { return active_app; }

void app_manager_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( RFSCOUT_APP_THERMAL != active_app &&
        ( now_us - last_thermal_probe_us ) >= APP_FEATURE_PROBE_INTERVAL_US )
    {
        (void)probe_thermal();
        last_thermal_probe_us = now_us;
    }

    if( platform_encoder_button_pressed() )
    {
        if( RFSCOUT_APP_CC1101 == active_app )
        {
            cc1101_settings_open = !cc1101_settings_open;
            radio_app_view_cc1101( cc1101_settings_open );
        }
    }

    const int delta = platform_encoder_delta();
    if( 0 != delta && !cc1101_settings_open )
    {
        const rfscout_app_id_t candidate = next_available_app( delta > 0 ? 1 : -1, now_us );
        if( candidate != active_app )
        {
            leave_app( active_app );
            active_app = candidate;
            enter_app( active_app );
        }
    }

    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        radio_monitor_step();
        ui_poll();
        return;
    }
    if( RFSCOUT_APP_CC1101 == active_app || RFSCOUT_APP_NRF24 == active_app ) return;

    if( !mlx90640_is_detected() )
    {
        leave_app( active_app );
        active_app = RFSCOUT_APP_WIFI_BT;
        enter_app( active_app );
        ESP_LOGW( TAG, "Thermal app closed after sensor disconnect" );
        return;
    }
    if( ( now_us - last_app_refresh_us ) < APP_REFRESH_INTERVAL_US ) return;
    last_app_refresh_us = now_us;
    const esp_err_t result = thermal_view_render();
    if( result != ESP_OK && result != ESP_ERR_NOT_FINISHED && !mlx90640_is_detected() )
    {
        leave_app( active_app );
        active_app = RFSCOUT_APP_WIFI_BT;
        enter_app( active_app );
    }
}
