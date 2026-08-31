/**
    display_brightness.c : RFScout LCD backlight brightness control
    Description: Reuses the display backlight GPIO through ESP32-S3 LEDC PWM after LCD initialization.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "board.h"

#include "driver/ledc.h"
#include "esp_log.h"

#include <stdbool.h>

#define DISPLAY_BACKLIGHT_MODE LEDC_LOW_SPEED_MODE
#define DISPLAY_BACKLIGHT_TIMER LEDC_TIMER_1
#define DISPLAY_BACKLIGHT_CHANNEL LEDC_CHANNEL_1

static const char *TAG = "display_brightness";
static bool initialized;

/** @brief Lazily bind the board backlight pin to an 8-bit LEDC channel. */
static bool ensure_initialized( void )
{
    if( initialized )
    {
        return true;
    }

    ledc_timer_config_t timer = {
        .speed_mode = DISPLAY_BACKLIGHT_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = DISPLAY_BACKLIGHT_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if( ESP_OK != ledc_timer_config( &timer ) )
    {
        ESP_LOGW( TAG, "could not configure backlight PWM timer" );
        return false;
    }

    ledc_channel_config_t channel = {
        .gpio_num = RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN,
        .speed_mode = DISPLAY_BACKLIGHT_MODE,
        .channel = DISPLAY_BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = DISPLAY_BACKLIGHT_TIMER,
        .duty = 255U,
        .hpoint = 0,
    };
    if( ESP_OK != ledc_channel_config( &channel ) )
    {
        ESP_LOGW( TAG, "could not configure backlight PWM channel" );
        return false;
    }

    initialized = true;
    return true;
}

void display_set_brightness( uint8_t a_percent )
{
    if( !ensure_initialized() )
    {
        return;
    }
    if( a_percent > 100U )
    {
        a_percent = 100U;
    }
    const uint32_t duty = ( (uint32_t)a_percent * 255U ) / 100U;
    if( ESP_OK == ledc_set_duty( DISPLAY_BACKLIGHT_MODE, DISPLAY_BACKLIGHT_CHANNEL, duty ) )
    {
        (void)ledc_update_duty( DISPLAY_BACKLIGHT_MODE, DISPLAY_BACKLIGHT_CHANNEL );
    }
}
