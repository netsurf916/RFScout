/**
    app_manager.c : RFScout application view manager implementation
    Description: Switches isolated full-screen applications with the encoder.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "platform.h"
#include "thermal_view.h"
#include "ui.h"

#include "esp_log.h"
#include "esp_timer.h"

#define APP_REFRESH_INTERVAL_US 125000LL

typedef enum
{
    APP_RF_SUMMARY = 0,
    APP_THERMAL,
    APP_COUNT
} app_id_t;

static const char *TAG = "app_manager";
static app_id_t active_app;
static int64_t last_app_refresh_us;

esp_err_t app_manager_init( void )
{
    active_app = APP_RF_SUMMARY;
    last_app_refresh_us = 0;
    return ui_init();
}

void app_manager_poll( void )
{
    const int delta = platform_encoder_delta();
    if( 0 != delta )
    {
        int next = (int)active_app + ( ( delta > 0 ) ? 1 : -1 );
        if( next < 0 )
        {
            next = APP_COUNT - 1;
        }
        else if( next >= APP_COUNT )
        {
            next = 0;
        }
        active_app = (app_id_t)next;
        last_app_refresh_us = 0;
        if( APP_THERMAL == active_app )
        {
            thermal_view_enter();
            ESP_LOGI( TAG, "switched to Thermal app" );
        }
        else
        {
            ESP_LOGI( TAG, "switched to RF Summary app" );
        }
    }

    if( APP_RF_SUMMARY == active_app )
    {
        ui_poll();
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_app_refresh_us ) < APP_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_app_refresh_us = now_us;
    (void)thermal_view_render();
}
