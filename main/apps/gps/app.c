/**
    apps/gps/app.c : GPS receiver and directional LED presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "breadcrumbs.h"
#include "device_config.h"
#include "display_idle.h"
#include "gps.h"
#include "gps_view.h"
#include "led_ring.h"

#include "esp_log.h"

#include <stddef.h>

#define GPS_VIEW_REFRESH_INTERVAL_US 500000LL
#define GPS_MENU_ITEMS 9U

static const char *TAG = "gps_app";
static int64_t last_refresh_us;
static bool menu_open;
static bool reset_failed;
static unsigned menu_item;
static int last_led_sector = -1;
static bool led_ready;

static void render( void )
{
    gps_view_render( menu_open, menu_item, reset_failed );
}

static void update_direction_led( void )
{
    const device_config_t *config = device_config_get();
    if( !config->gps_led_enabled || !led_ready )
    {
        return;
    }

    rfscout_gps_snapshot_t gps = { 0 };
    int sector = -1;
    if( gps_snapshot_get( &gps ) && gps.location_valid && gps.location_age_ms <= 5000U &&
        gps.track_valid && gps.speed_valid && gps.speed_mps >= 0.7f && gps.track_deg >= 0.0f &&
        gps.track_deg < 360.0f )
    {
        /* Indices are logical azimuth sectors; physical ring orientation needs calibration. */
        sector = ( (int)( ( gps.track_deg + 22.5f ) / 45.0f ) ) % 8;
    }
    if( sector == last_led_sector )
    {
        return;
    }
    last_led_sector = sector;
    (void)led_ring_clear();
    if( sector < 0 )
    {
        return;
    }
    const rfscout_led_color_t color = {
        .red = (uint8_t)( config->gps_led_rgb >> 16 ),
        .green = (uint8_t)( config->gps_led_rgb >> 8 ),
        .blue = (uint8_t)config->gps_led_rgb,
    };
    (void)led_ring_set( (unsigned)sector, color );
    (void)led_ring_show();
}

static void refresh_led_settings( void )
{
    const device_config_t *config = device_config_get();
    if( config->gps_led_enabled && !led_ready )
    {
        led_ready = ESP_OK == led_ring_init();
        if( !led_ready )
        {
            ESP_LOGW( TAG, "LED ring unavailable" );
        }
    }
    if( led_ready )
    {
        (void)led_ring_set_brightness( config->gps_led_brightness );
        (void)led_ring_clear();
        last_led_sector = -1;
        update_direction_led();
    }
}

static void select_menu_item( void )
{
    const device_config_t *config = device_config_get();
    esp_err_t result = ESP_OK;
    switch( menu_item )
    {
        case 0U:
            menu_open = false;
            return;
        case 1U:
            result = gps_trip_reset();
            reset_failed = ESP_OK != result;
            break;
        case 2U:
            result = device_config_set_gps_led(
                !config->gps_led_enabled, config->gps_led_brightness, config->gps_led_rgb );
            break;
        case 3U:
            result =
                device_config_set_gps_led( config->gps_led_enabled,
                                           (uint8_t)( ( config->gps_led_brightness + 10U ) % 110U ),
                                           config->gps_led_rgb );
            break;
        case 4U:
        {
            static const uint32_t colors[] = {
                0x00FF00U, 0xFF0000U, 0x0000FFU, 0xFFFFFFU, 0xFF8000U, 0x00FFFFU };
            unsigned next = 0U;
            for( unsigned i = 0U; i < sizeof( colors ) / sizeof( colors[ 0 ] ); ++i )
            {
                if( config->gps_led_rgb == colors[ i ] )
                {
                    next = ( i + 1U ) % ( sizeof( colors ) / sizeof( colors[ 0 ] ) );
                    break;
                }
            }
            result = device_config_set_gps_led(
                config->gps_led_enabled, config->gps_led_brightness, colors[ next ] );
            break;
        }
        case 5U:
            result = device_config_set_gps_keep_awake( !config->gps_keep_awake );
            display_idle_set_keep_awake( device_config_get()->gps_keep_awake );
            break;
        case 6U:
            result = breadcrumbs_set_enabled( !breadcrumbs_enabled() );
            break;
        case 7U:
        {
            static const uint16_t options[] = { 24U, 72U, 144U, 288U, 1008U };
            uint16_t next = options[ 0 ];
            for( unsigned i = 0U; i < sizeof( options ) / sizeof( options[ 0 ] ); ++i )
            {
                if( breadcrumbs_file_limit() == options[ i ] )
                {
                    next = options[ ( i + 1U ) % ( sizeof( options ) / sizeof( options[ 0 ] ) ) ];
                    break;
                }
            }
            result = breadcrumbs_set_file_limit( next );
            break;
        }
        case 8U:
        {
            uint8_t next = (uint8_t)( config->gps_timeout_seconds + 5U );
            if( next > RFSCOUT_GPS_TIMEOUT_MAX_SECONDS )
            {
                next = RFSCOUT_GPS_TIMEOUT_MIN_SECONDS;
            }
            result = device_config_set_gps_timeout_seconds( next );
            break;
        }
        default:
            return;
    }
    if( ESP_OK == result && menu_item != 1U && menu_item != 6U && menu_item != 7U )
    {
        result = device_config_save();
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "GPS setting could not be saved: %s", esp_err_to_name( result ) );
        reset_failed = true;
    }
    refresh_led_settings();
}

static rfscout_app_runtime_result_t gps_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = 0;
        menu_open = false;
        menu_item = 0U;
        reset_failed = false;
        led_ready = false;
        last_led_sector = -1;
        display_idle_set_keep_awake( device_config_get()->gps_keep_awake );
        refresh_led_settings();
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        if( led_ready )
        {
            (void)led_ring_clear();
        }
        display_idle_set_keep_awake( false );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        update_direction_led();
        if( 0 == last_refresh_us ||
            a_event->now_us - last_refresh_us >= GPS_VIEW_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            render();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( menu_open )
        {
            if( RFSCOUT_INPUT_NAVIGATION == a_event->input.type )
            {
                menu_item =
                    ( menu_item + ( a_event->input.value > 0 ? 1U : GPS_MENU_ITEMS - 1U ) ) %
                    GPS_MENU_ITEMS;
            }
            else if( RFSCOUT_INPUT_LONG_PRESS == a_event->input.type )
            {
                menu_open = false;
            }
            else if( RFSCOUT_INPUT_SELECT == a_event->input.type )
            {
                select_menu_item();
            }
            render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            menu_open = true;
            menu_item = 0U;
            reset_failed = false;
            render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_gps_app = {
    .id = RFSCOUT_APP_GPS,
    .name = "GPS",
    .persistence_key = "gps",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = gps_runtime,
};
