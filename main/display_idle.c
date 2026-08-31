/**
    display_idle.c : RFScout global display idle policy
    Description: Dims or blanks only the display while applications, logging, and alerts keep running.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display_idle.h"

#include "device_config.h"
#include "display.h"
#include "platform.h"
#include "power.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stddef.h>

#define DISPLAY_IDLE_DIM_BRIGHTNESS_PERCENT 10U

static const char *TAG = "display_idle";
static const uint32_t dim_options[] = { 0U, 30U, 60U, 120U, 300U };
static const uint32_t off_options[] = { 0U, 60U, 120U, 300U, 600U, 1800U };
static uint32_t dim_timeout_seconds = RFSCOUT_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS;
static uint32_t off_timeout_seconds = RFSCOUT_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS;
static int64_t last_activity_us;
static rfscout_display_idle_state_t idle_state = RFSCOUT_DISPLAY_IDLE_ACTIVE;
static bool presentation_enabled = true;

static uint32_t
adjusted_option( uint32_t a_current, int a_direction, const uint32_t *a_options, size_t a_count )
{
    size_t current = 0U;
    for( size_t index = 0U; index < a_count; ++index )
    {
        if( a_options[ index ] == a_current )
        {
            current = index;
            break;
        }
    }
    if( a_direction > 0 )
    {
        current = ( current + 1U ) % a_count;
    }
    else
    {
        current = ( current + a_count - 1U ) % a_count;
    }
    return a_options[ current ];
}

static void wake_display( int64_t a_now_us )
{
    const rfscout_display_idle_state_t previous = idle_state;

    /* Wake activity is device-level and deliberately precedes both the global
       input lock and app dispatch. The physical gesture is consumed in full so
       it cannot also navigate, select, toggle lock, sleep, or power off. */
    platform_consume_wake_input();
    power_consume_wake_input();

    idle_state = RFSCOUT_DISPLAY_IDLE_ACTIVE;
    last_activity_us = a_now_us;
    presentation_enabled = true;
    display_invalidate();

    /* Keep the backlight dark until the latest logical framebuffer has been
       transferred after an OFF interval, avoiding a flash of stale LCD GRAM. */
    if( RFSCOUT_DISPLAY_IDLE_OFF == previous )
    {
        const esp_err_t result = display_present();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "display refresh on wake failed: %s", esp_err_to_name( result ) );
        }
    }
    display_set_brightness( device_config_get()->brightness_percent );
    ESP_LOGI( TAG, "display awake" );
}

esp_err_t display_idle_init( void )
{
    dim_timeout_seconds = device_config_display_dim_timeout_seconds();
    off_timeout_seconds = device_config_display_off_timeout_seconds();
    last_activity_us = esp_timer_get_time();
    idle_state = RFSCOUT_DISPLAY_IDLE_ACTIVE;
    presentation_enabled = true;
    return ESP_OK;
}

void display_idle_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool activity = platform_user_activity();

    if( activity )
    {
        if( RFSCOUT_DISPLAY_IDLE_ACTIVE != idle_state )
        {
            wake_display( now_us );
            return;
        }
        last_activity_us = now_us;
        return;
    }

    const uint64_t elapsed_seconds =
        ( now_us > last_activity_us ) ? (uint64_t)( now_us - last_activity_us ) / 1000000ULL : 0ULL;

    if( ( 0U != off_timeout_seconds ) && ( elapsed_seconds >= off_timeout_seconds ) )
    {
        if( RFSCOUT_DISPLAY_IDLE_OFF != idle_state )
        {
            display_set_brightness( 0U );
            presentation_enabled = false;
            idle_state = RFSCOUT_DISPLAY_IDLE_OFF;
            ESP_LOGI( TAG, "display off after %u seconds idle", (unsigned)off_timeout_seconds );
        }
        return;
    }

    if( ( 0U != dim_timeout_seconds ) && ( elapsed_seconds >= dim_timeout_seconds ) &&
        ( RFSCOUT_DISPLAY_IDLE_ACTIVE == idle_state ) )
    {
        display_set_brightness( DISPLAY_IDLE_DIM_BRIGHTNESS_PERCENT );
        idle_state = RFSCOUT_DISPLAY_IDLE_DIMMED;
        ESP_LOGI( TAG, "display dimmed after %u seconds idle", (unsigned)dim_timeout_seconds );
    }
}

bool display_idle_presentation_enabled( void )
{
    return presentation_enabled;
}

rfscout_display_idle_state_t display_idle_state( void )
{
    return idle_state;
}

uint32_t display_idle_dim_timeout_seconds( void )
{
    return dim_timeout_seconds;
}

uint32_t display_idle_off_timeout_seconds( void )
{
    return off_timeout_seconds;
}

esp_err_t display_idle_adjust_dim_timeout( int a_direction )
{
    if( 0 == a_direction )
    {
        return ESP_OK;
    }
    const uint32_t next = adjusted_option( dim_timeout_seconds,
                                           a_direction,
                                           dim_options,
                                           sizeof( dim_options ) / sizeof( dim_options[ 0 ] ) );
    const esp_err_t result = device_config_set_display_dim_timeout_seconds( (uint16_t)next );
    if( ESP_OK == result )
    {
        dim_timeout_seconds = next;
        last_activity_us = esp_timer_get_time();
    }
    return result;
}

esp_err_t display_idle_adjust_off_timeout( int a_direction )
{
    if( 0 == a_direction )
    {
        return ESP_OK;
    }
    const uint32_t next = adjusted_option( off_timeout_seconds,
                                           a_direction,
                                           off_options,
                                           sizeof( off_options ) / sizeof( off_options[ 0 ] ) );
    const esp_err_t result = device_config_set_display_off_timeout_seconds( (uint16_t)next );
    if( ESP_OK == result )
    {
        off_timeout_seconds = next;
        last_activity_us = esp_timer_get_time();
    }
    return result;
}

const char *display_idle_timeout_label( uint32_t a_seconds )
{
    switch( a_seconds )
    {
        case 0U:
            return "Off";
        case 30U:
            return "30 sec";
        case 60U:
            return "1 min";
        case 120U:
            return "2 min";
        case 300U:
            return "5 min";
        case 600U:
            return "10 min";
        case 1800U:
            return "30 min";
        default:
            return "Custom";
    }
}
