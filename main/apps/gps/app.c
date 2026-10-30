/**
    apps/gps/app.c : GPS receiver and directional LED presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_manager.h"

#include "breadcrumbs.h"
#include "device_config.h"
#include "display_idle.h"
#include "geo.h"
#include "gps.h"
#include "gps_view.h"
#include "led_ring.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define GPS_VIEW_REFRESH_INTERVAL_US 500000LL
#define GPS_MENU_ITEMS GPS_SETTING_COUNT

static const char *TAG = "gps_app";
static int64_t last_refresh_us;
static bool menu_open;
static bool menu_error;
static unsigned menu_item;
static int last_led_sector = -1;
static bool led_refresh_pending;
static bool led_ready;
static rfscout_gps_snapshot_t last_ui_gps;
static bool last_ui_gps_valid;
static bool last_ui_receiver_present;

static void render( void )
{
    if( !app_manager_rendering() )
    {
        app_manager_request_render( RFSCOUT_APP_GPS, true );
        return;
    }
    gps_view_render( menu_open, menu_item, menu_error );
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
    if( gps_snapshot_get( &gps ) && gps_location_fresh( &gps ) && gps.track_valid &&
        gps.speed_valid && gps.speed_mps >= 0.7f && gps.track_deg >= 0.0f &&
        gps.track_deg < 360.0f )
    {
        /* Hardware calibration: physical LED index 0 points West, not North.
           Shift the clockwise azimuth sector two LEDs so North uses index 2. */
        sector = (int)( ( geo_compass_sector( gps.track_deg ) + 2U ) % 8U );
    }
    if( sector == last_led_sector && !led_refresh_pending )
    {
        return;
    }
    if( ESP_OK != led_ring_clear() )
    {
        return;
    }
    if( sector >= 0 )
    {
        const rfscout_led_color_t color = {
            .red = (uint8_t)( config->gps_led_rgb >> 16 ),
            .green = (uint8_t)( config->gps_led_rgb >> 8 ),
            .blue = (uint8_t)config->gps_led_rgb,
        };
        if( ESP_OK != led_ring_set( (unsigned)sector, color ) || ESP_OK != led_ring_show() )
        {
            return;
        }
    }
    last_led_sector = sector;
    led_refresh_pending = false;
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
    if( !config->gps_led_enabled && led_ready )
    {
        if( ESP_OK == led_ring_deinit() )
        {
            led_ready = false;
        }
        return;
    }
    if( led_ready )
    {
        led_refresh_pending = true;
        if( ESP_OK != led_ring_set_brightness( config->gps_led_brightness ) )
        {
            return;
        }
        update_direction_led();
    }
}

static void select_menu_item( void )
{
    const device_config_t *config = device_config_get();
    esp_err_t result = ESP_OK;
    menu_error = false;
    if( !gps_view_setting_available( menu_item ) )
    {
        menu_error = true;
        return;
    }
    switch( menu_item )
    {
        case GPS_SETTING_BACK:
            menu_open = false;
            return;
        case GPS_SETTING_RESET_TRIP:
            result = gps_trip_reset();
            break;
        case GPS_SETTING_LED:
            result = device_config_set_gps_led(
                !config->gps_led_enabled, config->gps_led_brightness, config->gps_led_rgb );
            break;
        case GPS_SETTING_LED_BRIGHTNESS:
            result = device_config_set_gps_led( config->gps_led_enabled,
                                                (uint8_t)( config->gps_led_brightness >= 90U
                                                               ? 0U
                                                               : config->gps_led_brightness + 10U ),
                                                config->gps_led_rgb );
            break;
        case GPS_SETTING_LED_COLOR:
            result = device_config_set_gps_led( config->gps_led_enabled,
                                                config->gps_led_brightness,
                                                gps_view_next_led_color( config->gps_led_rgb ) );
            break;
        case GPS_SETTING_KEEP_AWAKE:
            result = device_config_set_gps_keep_awake( !config->gps_keep_awake );
            display_idle_set_keep_awake( device_config_get()->gps_keep_awake );
            break;
        case GPS_SETTING_BREADCRUMBS:
            result = breadcrumbs_set_enabled( !breadcrumbs_enabled() );
            break;
        case GPS_SETTING_BREADCRUMB_RETENTION:
            result = breadcrumbs_set_retention(
                breadcrumbs_retention_step( breadcrumbs_retention(), 1 ) );
            break;
        case GPS_SETTING_MOVEMENT:
            result = device_config_set_gps_movement_m(
                device_config_gps_movement_step( config->gps_movement_m, 1 ) );
            break;
        case GPS_SETTING_TIMEPULSE:
            result = device_config_set_gps_timepulse( !config->gps_timepulse_enabled );
            break;
        case GPS_SETTING_TIMEOUT:
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
    /* Trip and breadcrumb settings persist through their own services. */
    if( ESP_OK == result && GPS_SETTING_RESET_TRIP != menu_item &&
        GPS_SETTING_BREADCRUMBS != menu_item && GPS_SETTING_BREADCRUMB_RETENTION != menu_item )
    {
        result = device_config_save();
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "GPS menu action failed: %s", esp_err_to_name( result ) );
        menu_error = true;
    }
    if( ESP_OK == result && GPS_SETTING_TIMEPULSE == menu_item )
    {
        gps_timepulse_apply();
    }
    refresh_led_settings();
}

/* App manager invokes this before committing a navigation transition. */
static esp_err_t gps_prepare_leave( void )
{
    if( !led_ready )
    {
        return ESP_OK;
    }
    esp_err_t result = led_ring_deinit();
    if( ESP_ERR_TIMEOUT == result )
    {
        /* One bounded retry; do not transfer ownership if the LEDs remain lit. */
        result = led_ring_deinit();
    }
    if( ESP_OK == result )
    {
        led_ready = false;
    }
    else
    {
        ESP_LOGE( TAG, "LED ring cleanup blocked app exit: %s", esp_err_to_name( result ) );
    }
    return result;
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
        last_ui_gps_valid = false;
        last_ui_receiver_present = false;
        menu_open = false;
        menu_item = 0U;
        menu_error = false;
        led_ready = false;
        last_led_sector = -1;
        led_refresh_pending = false;
        display_idle_set_keep_awake( device_config_get()->gps_keep_awake );
        refresh_led_settings();
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        /* The prepare hook already cleared and released the LED ring. */
        last_led_sector = -1;
        led_refresh_pending = false;
        display_idle_set_keep_awake( false );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( led_ready && !device_config_get()->gps_led_enabled )
        {
            if( ESP_OK == led_ring_deinit() )
            {
                led_ready = false;
            }
        }
        update_direction_led();
        if( !menu_open && ( 0 == last_refresh_us ||
                            a_event->now_us - last_refresh_us >= GPS_VIEW_REFRESH_INTERVAL_US ) )
        {
            last_refresh_us = a_event->now_us;
            rfscout_gps_snapshot_t current = { 0 };
            const bool present = gps_snapshot_get( &current );
            /* Unshown UTC/age fields cannot turn a static screen into a refresh loop. */
            current.utc_epoch = 0;
            current.time_age_ms = 0;
            if( !gps_location_fresh( &current ) )
            {
                current.location_age_ms = 0;
            }
            if( !last_ui_gps_valid || present != last_ui_receiver_present ||
                0 != memcmp( &current, &last_ui_gps, sizeof( current ) ) )
            {
                last_ui_gps = current;
                last_ui_gps_valid = true;
                last_ui_receiver_present = present;
                app_manager_request_render( RFSCOUT_APP_GPS, false );
            }
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
            menu_error = false;
            render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    return RFSCOUT_APP_RUNTIME_HANDLED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    rfscout_gps_snapshot_t gps = { 0 };
    const bool receiver_present = gps_snapshot_get( &gps );
    bool success = log_bool( a_record, "uart_active", gps_available() ) &&
                   log_bool( a_record, "receiver", receiver_present ) &&
                   log_bool( a_record, "fix", receiver_present && gps.location_valid ) &&
                   log_double( a_record, "trip_m", gps_trip_distance_m() );
    if( success && receiver_present && gps.speed_valid )
    {
        success = log_double( a_record, "speed_mps", gps.speed_mps );
    }
    if( success && receiver_present && gps.altitude_valid )
    {
        success = log_double( a_record, "altitude_m", gps.altitude_m );
    }
    if( success && receiver_present && gps.satellites_valid )
    {
        success = log_uint( a_record, "satellites_used", gps.satellites );
    }
    return success;
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
    .telemetry = app_telemetry,
    .prepare_leave = gps_prepare_leave,
    .transition_denial_message = "GPS LED ring is busy",
};
