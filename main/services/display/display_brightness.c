/**
    display_brightness.c : RFScout LCD backlight brightness control
    Description: Applies the runtime-selected board backlight control semantics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_BACKLIGHT_MODE LEDC_LOW_SPEED_MODE
#define DISPLAY_BACKLIGHT_TIMER LEDC_TIMER_1
#define DISPLAY_BACKLIGHT_CHANNEL LEDC_CHANNEL_1
#define DISPLAY_PULSE_STEPS 16U
#define DISPLAY_PULSE_RESET_MS 3U
#define DISPLAY_PULSE_WAKE_US 30U

static const char *TAG = "display_brightness";
static bool pwm_initialized;
static uint8_t pulse_level;

static bool ensure_pwm_initialized( void )
{
    if( pwm_initialized )
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

    pwm_initialized = true;
    return true;
}

static void set_pwm_brightness( uint8_t a_percent )
{
    if( !ensure_pwm_initialized() )
    {
        return;
    }
    const uint32_t duty = ( (uint32_t)a_percent * 255U ) / 100U;
    if( ESP_OK == ledc_set_duty( DISPLAY_BACKLIGHT_MODE, DISPLAY_BACKLIGHT_CHANNEL, duty ) )
    {
        (void)ledc_update_duty( DISPLAY_BACKLIGHT_MODE, DISPLAY_BACKLIGHT_CHANNEL );
    }
}

static void set_pulse_brightness( uint8_t a_percent )
{
    uint8_t target_level = (uint8_t)( ( (uint32_t)a_percent * DISPLAY_PULSE_STEPS + 99U ) / 100U );
    if( target_level > DISPLAY_PULSE_STEPS )
    {
        target_level = DISPLAY_PULSE_STEPS;
    }

    if( 0U == target_level )
    {
        gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 0 );
        vTaskDelay( pdMS_TO_TICKS( DISPLAY_PULSE_RESET_MS ) );
        pulse_level = 0U;
        return;
    }

    if( 0U == pulse_level )
    {
        gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 1 );
        pulse_level = DISPLAY_PULSE_STEPS;
        esp_rom_delay_us( DISPLAY_PULSE_WAKE_US );
    }

    const uint8_t from = DISPLAY_PULSE_STEPS - pulse_level;
    const uint8_t to = DISPLAY_PULSE_STEPS - target_level;
    const uint8_t pulses = (uint8_t)( ( DISPLAY_PULSE_STEPS + to - from ) % DISPLAY_PULSE_STEPS );
    for( uint8_t index = 0U; index < pulses; ++index )
    {
        gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 0 );
        gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 1 );
    }
    pulse_level = target_level;
}

void display_set_brightness( uint8_t a_percent )
{
    if( a_percent > 100U )
    {
        a_percent = 100U;
    }

    if( RFSCOUT_BACKLIGHT_PULSE_16_STEP == RFSCOUT_BOARD_BACKLIGHT_TYPE )
    {
        set_pulse_brightness( a_percent );
        return;
    }
    set_pwm_brightness( a_percent );
}