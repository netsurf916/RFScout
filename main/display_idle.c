/**
    display_idle.c : RFScout global display-idle policy
    Description: Keeps application execution independent from LCD dim/off presentation state.
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

#include <stdint.h>

#define DISPLAY_IDLE_US_PER_SECOND 1000000LL
#define DISPLAY_IDLE_DIM_DIVISOR 4U
#define DISPLAY_IDLE_DIM_MIN_PERCENT 1U

static const char *TAG = "display_idle";

typedef enum
{
    DISPLAY_IDLE_ACTIVE = 0,
    DISPLAY_IDLE_DIMMED,
    DISPLAY_IDLE_OFF
} display_idle_state_t;

static display_idle_state_t state;
static int64_t last_activity_us;
static bool off_requested;

static uint8_t dim_brightness( uint8_t a_normal_percent )
{
    uint8_t dimmed = (uint8_t)( a_normal_percent / DISPLAY_IDLE_DIM_DIVISOR );
    if( dimmed < DISPLAY_IDLE_DIM_MIN_PERCENT )
    {
        dimmed = DISPLAY_IDLE_DIM_MIN_PERCENT;
    }
    return dimmed;
}

static void set_dimmed( void )
{
    const uint8_t brightness = device_config_get()->brightness_percent;
    display_set_brightness( dim_brightness( brightness ) );
    state = DISPLAY_IDLE_DIMMED;
    ESP_LOGI( TAG, "display dimmed after inactivity" );
}

static esp_err_t enter_off_state( const char *a_reason )
{
    if( DISPLAY_IDLE_OFF == state )
    {
        return ESP_OK;
    }

    const device_config_t *config = device_config_get();
    const uint8_t previous_brightness = ( DISPLAY_IDLE_DIMMED == state )
                                            ? dim_brightness( config->brightness_percent )
                                            : config->brightness_percent;

    display_set_brightness( 0U );
    const esp_err_t result = display_enter_low_power();
    if( ESP_OK != result )
    {
        display_set_brightness( previous_brightness );
        ESP_LOGW( TAG, "could not put LCD into hardware sleep: %s", esp_err_to_name( result ) );
        return result;
    }

    state = DISPLAY_IDLE_OFF;
    ESP_LOGI( TAG,
              "display entered ST7789 hardware sleep (%s)",
              ( NULL != a_reason ) ? a_reason : "requested" );
    return ESP_OK;
}

static bool wake_display( const char *a_reason )
{
    const device_config_t *config = device_config_get();
    if( DISPLAY_IDLE_OFF == state )
    {
        const esp_err_t enabled = display_exit_low_power();
        if( ESP_OK != enabled )
        {
            ESP_LOGW(
                TAG, "could not wake LCD from hardware sleep: %s", esp_err_to_name( enabled ) );
            return false;
        }

        /* Keep the backlight dark until the latest application framebuffer has
           replaced the LCD's pre-timeout contents. */
        display_invalidate();
        const esp_err_t presented = display_present();
        if( ESP_OK != presented )
        {
            ESP_LOGW( TAG, "wake presentation failed: %s", esp_err_to_name( presented ) );
            const esp_err_t sleeping = display_enter_low_power();
            if( ESP_OK != sleeping )
            {
                ESP_LOGW( TAG,
                          "could not restore LCD sleep state after wake failure: %s",
                          esp_err_to_name( sleeping ) );
            }
            return false;
        }
    }

    display_set_brightness( config->brightness_percent );
    state = DISPLAY_IDLE_ACTIVE;
    ESP_LOGI( TAG, "display woke (%s)", ( NULL != a_reason ) ? a_reason : "requested" );
    return true;
}

esp_err_t display_idle_init( void )
{
    state = DISPLAY_IDLE_ACTIVE;
    last_activity_us = esp_timer_get_time();
    off_requested = false;
    const esp_err_t result = display_exit_low_power();
    display_set_brightness( device_config_get()->brightness_percent );
    return result;
}

esp_err_t display_idle_request_off( void )
{
    /* Queue the transition rather than forcing it synchronously. A short power-button
       release is still visible to the platform activity detector in this loop; waiting
       for a quiet poll prevents that same gesture from immediately waking the LCD. */
    off_requested = true;
    return ESP_OK;
}

esp_err_t display_idle_request_wake( void )
{
    off_requested = false;
    last_activity_us = esp_timer_get_time();
    if( DISPLAY_IDLE_ACTIVE == state )
    {
        display_set_brightness( device_config_get()->brightness_percent );
        return ESP_OK;
    }
    return wake_display( "device request" ) ? ESP_OK : ESP_FAIL;
}

void display_idle_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( platform_input_activity_pending() )
    {
        if( DISPLAY_IDLE_ACTIVE != state )
        {
            /* Wake is a device-level gesture. Consume the entire gesture before
               lock, app navigation, menu, or power-button semantics can see it. */
            platform_consume_wake_input();
            power_consume_wake_input();
            (void)wake_display( "user activity" );
        }
        last_activity_us = now_us;
        return;
    }

    if( off_requested )
    {
        const esp_err_t result = enter_off_state( "explicit request" );
        if( ESP_OK == result )
        {
            off_requested = false;
            last_activity_us = now_us;
        }
        return;
    }

    const device_config_t *config = device_config_get();
    const int64_t idle_us = now_us - last_activity_us;
    const uint16_t off_seconds = config->display_off_timeout_seconds;
    const uint16_t dim_seconds = config->display_dim_timeout_seconds;

    if( ( 0U != off_seconds ) && ( idle_us >= (int64_t)off_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        if( DISPLAY_IDLE_OFF != state )
        {
            (void)enter_off_state( "inactivity" );
        }
        return;
    }

    if( ( DISPLAY_IDLE_ACTIVE == state ) && ( 0U != dim_seconds ) &&
        ( idle_us >= (int64_t)dim_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        set_dimmed();
    }
}
