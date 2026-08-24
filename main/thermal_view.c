/**
    thermal_view.c : RFScout thermal application view implementation
    Description: Displays the MLX90640 array as an honest relative-IR heat map.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "thermal_view.h"

#include "battery_indicator.h"
#include "display.h"
#include "mlx90640.h"
#include "status_bar.h"

#include "esp_log.h"

#include <stdio.h>

#define THERMAL_IMAGE_X 4
#define THERMAL_IMAGE_Y 22
#define THERMAL_PIXEL_SCALE 6
#define THERMAL_IMAGE_WIDTH ( MLX90640_WIDTH * THERMAL_PIXEL_SCALE )
#define THERMAL_IMAGE_HEIGHT ( MLX90640_HEIGHT * THERMAL_PIXEL_SCALE )
#define THERMAL_INFO_X 204

#define COLOR_BLACK 0x0000U
#define COLOR_WHITE 0xFFFFU
#define COLOR_GRAY 0x8410U
#define COLOR_RED 0xF800U
#define COLOR_SUBDUED_TEAL 0x03F0U

static const char *TAG = "thermal_view";
static mlx90640_frame_t frame;

/** @brief Convert normalized signed raw intensity to blue/cyan/yellow/red. */
static uint16_t palette( int16_t a_value, int16_t a_minimum, int16_t a_maximum )
{
    if( a_maximum <= a_minimum )
    {
        return 0x0010U;
    }

    const int32_t offset = (int32_t)a_value - (int32_t)a_minimum;
    const int32_t range = (int32_t)a_maximum - (int32_t)a_minimum;
    uint32_t level = (uint32_t)( ( offset * 255 ) / range );
    uint8_t red = 0U;
    uint8_t green = 0U;
    uint8_t blue = 0U;

    if( level < 85U )
    {
        blue = (uint8_t)( 96U + ( level * 159U / 84U ) );
        green = (uint8_t)( level * 180U / 84U );
    }
    else if( level < 170U )
    {
        const uint32_t local = level - 85U;
        blue = (uint8_t)( 255U - ( local * 255U / 84U ) );
        green = 255U;
        red = (uint8_t)( local * 255U / 84U );
    }
    else
    {
        const uint32_t local = level - 170U;
        red = 255U;
        green = (uint8_t)( 255U - ( local * 255U / 85U ) );
    }

    return (uint16_t)( ( ( red >> 3 ) << 11 ) |
                       ( ( green >> 2 ) << 5 ) |
                       ( blue >> 3 ) );
}

void thermal_view_enter( void )
{
    /* Availability is owned by the app manager; no I2C probing is done here. */
}

esp_err_t thermal_view_render( void )
{
    display_clear( COLOR_BLACK );
    display_draw_text( 6, 5, "THERMAL", COLOR_SUBDUED_TEAL, 2 );
    status_bar_draw();
    battery_indicator_draw();

    if( !mlx90640_is_detected() )
    {
        display_draw_text( 56, 70, "MLX90640 NOT DETECTED", COLOR_RED, 2 );
        display_draw_text( 73, 98, "I2C ADDRESS 0x33", COLOR_GRAY, 1 );
        return display_present();
    }

    const esp_err_t result = mlx90640_read_frame( &frame );
    if( ESP_ERR_NOT_FINISHED == result )
    {
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        display_draw_text( 72, 76, "THERMAL READ ERROR", COLOR_RED, 2 );
        display_present();
        return result;
    }

    for( int y = 0; y < MLX90640_HEIGHT; ++y )
    {
        for( int x = 0; x < MLX90640_WIDTH; ++x )
        {
            const int16_t value = frame.pixels[ ( y * MLX90640_WIDTH ) + x ];
            display_fill_rect( THERMAL_IMAGE_X + ( x * THERMAL_PIXEL_SCALE ),
                               THERMAL_IMAGE_Y + ( y * THERMAL_PIXEL_SCALE ),
                               THERMAL_PIXEL_SCALE,
                               THERMAL_PIXEL_SCALE,
                               palette( value, frame.minimum, frame.maximum ) );
        }
    }

    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ) - 5,
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ), 11, 1,
                       COLOR_WHITE );
    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ),
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ) - 5, 1, 11,
                       COLOR_WHITE );

    char text[ 24 ];
    display_draw_text( THERMAL_INFO_X, 30, "RELATIVE IR", COLOR_GRAY, 1 );
    snprintf( text, sizeof( text ), "MAX %6d", (int)frame.maximum );
    display_draw_text( THERMAL_INFO_X, 52, text, COLOR_WHITE, 1 );
    snprintf( text, sizeof( text ), "MIN %6d", (int)frame.minimum );
    display_draw_text( THERMAL_INFO_X, 68, text, COLOR_WHITE, 1 );
    snprintf( text, sizeof( text ), "CTR %6d", (int)frame.center );
    display_draw_text( THERMAL_INFO_X, 84, text, COLOR_WHITE, 1 );
    display_draw_text( THERMAL_INFO_X, 116, "RAW SENSOR", COLOR_GRAY, 1 );
    display_draw_text( THERMAL_INFO_X, 130, "CAL PENDING", COLOR_GRAY, 1 );

    const esp_err_t present = display_present();
    if( ESP_OK != present )
    {
        ESP_LOGW( TAG, "thermal LCD refresh failed" );
    }
    return present;
}
