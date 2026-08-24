/**
    app_manager.c : RFScout application view manager implementation
    Description: Switches isolated full-screen applications with the encoder.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "mlx90640.h"
#include "platform.h"
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

/** @brief Probe the optional thermal module and log availability transitions. */
static bool probe_thermal( void )
{
    const bool was_available = mlx90640_is_detected();
    const bool available = ( ESP_OK == mlx90640_probe() );

    if( available && !was_available )
    {
        ESP_LOGI( TAG, "Thermal app available" );
    }
    else if( !available && was_available )
    {
        ESP_LOGW( TAG, "Thermal app unavailable" );
    }
    return available;
}

/** @brief Return whether the requested app is currently selectable. */
static bool app_available( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_RF_SUMMARY == a_app )
    {
        return true;
    }
    if( RFSCOUT_APP_THERMAL == a_app )
    {
        return mlx90640_is_detected();
    }
    return false;
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_RF_SUMMARY;
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();

    const esp_err_t result = ui_init();
    if( ESP_OK != result )
    {
        return result;
    }

    (void)mlx90640_init();
    if( mlx90640_is_detected() )
    {
        ESP_LOGI( TAG, "Thermal app available" );
    }
    return ESP_OK;
}

rfscout_app_id_t app_manager_current( void )
{
    return active_app;
}

void app_manager_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( RFSCOUT_APP_THERMAL != active_app ) &&
        ( ( now_us - last_thermal_probe_us ) >= APP_FEATURE_PROBE_INTERVAL_US ) )
    {
        (void)probe_thermal();
        last_thermal_probe_us = now_us;
    }

    const int delta = platform_encoder_delta();
    if( 0 != delta )
    {
        const int direction = ( delta > 0 ) ? 1 : -1;
        int next = (int)active_app + direction;
        if( next < 0 )
        {
            next = RFSCOUT_APP_COUNT - 1;
        }
        else if( next >= RFSCOUT_APP_COUNT )
        {
            next = 0;
        }

        rfscout_app_id_t candidate = (rfscout_app_id_t)next;
        if( ( RFSCOUT_APP_THERMAL == candidate ) && !app_available( candidate ) )
        {
            /* A navigation request is also an immediate hot-plug probe. */
            last_thermal_probe_us = now_us;
            if( !probe_thermal() )
            {
                candidate = RFSCOUT_APP_RF_SUMMARY;
                ESP_LOGI( TAG, "Thermal app skipped: MLX90640 not detected" );
            }
        }

        if( candidate != active_app )
        {
            active_app = candidate;
            last_app_refresh_us = 0;
            if( RFSCOUT_APP_THERMAL == active_app )
            {
                thermal_view_enter();
                ESP_LOGI( TAG, "switched to Thermal app" );
            }
            else
            {
                ESP_LOGI( TAG, "switched to RF Summary app" );
            }
        }
    }

    if( RFSCOUT_APP_RF_SUMMARY == active_app )
    {
        ui_poll();
        return;
    }

    if( !mlx90640_is_detected() )
    {
        active_app = RFSCOUT_APP_RF_SUMMARY;
        last_app_refresh_us = 0;
        ESP_LOGW( TAG, "Thermal app closed after sensor disconnect" );
        return;
    }

    if( ( now_us - last_app_refresh_us ) < APP_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_app_refresh_us = now_us;

    const esp_err_t result = thermal_view_render();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) &&
        !mlx90640_is_detected() )
    {
        active_app = RFSCOUT_APP_RF_SUMMARY;
        last_app_refresh_us = 0;
        ESP_LOGW( TAG, "Thermal app closed after I2C failure" );
    }
}
