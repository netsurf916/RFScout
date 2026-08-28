/**
    thermal_view.c : RFScout thermal application view implementation
    Description: Displays the MLX90640 array as a smoothed relative-IR heat map.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "thermal_view.h"

#include "display.h"
#include "mlx90640.h"
#include "ui_chrome.h"

#include "esp_log.h"

#include <limits.h>
#include <stdio.h>

#define THERMAL_IMAGE_X 4
#define THERMAL_IMAGE_Y 22
#define THERMAL_IMAGE_WIDTH 192
#define THERMAL_IMAGE_HEIGHT 144
#define THERMAL_INFO_X 204
#define THERMAL_RANGE_MARGIN_NUMERATOR 1
#define THERMAL_RANGE_MARGIN_DENOMINATOR 16
#define THERMAL_RANGE_OLD_WEIGHT 3
#define THERMAL_RANGE_DIVISOR 4

#define COLOR_BLACK 0x0000U
#define COLOR_WHITE 0xFFFFU
#define COLOR_GRAY 0x8410U
#define COLOR_RED 0xF800U
#define COLOR_SUBDUED_TEAL 0x03F0U

static const char *TAG = "thermal_view";
static mlx90640_frame_t frame;
static int32_t display_minimum;
static int32_t display_maximum;
static bool display_range_valid;

/** @brief Convert one RGB888 triplet to the display's RGB565 representation. */
static uint16_t rgb565( uint8_t a_red, uint8_t a_green, uint8_t a_blue )
{
    return (uint16_t)( ( (uint16_t)( a_red >> 3 ) << 11 ) | ( (uint16_t)( a_green >> 2 ) << 5 ) |
                       ( a_blue >> 3 ) );
}

/** @brief Map normalized intensity through a high-contrast thermal palette. */
static uint16_t palette( int32_t a_value, int32_t a_minimum, int32_t a_maximum )
{
    if( a_maximum <= a_minimum )
    {
        return rgb565( 8U, 8U, 24U );
    }

    int32_t level = ( ( a_value - a_minimum ) * 255 ) / ( a_maximum - a_minimum );
    if( level < 0 )
    {
        level = 0;
    }
    else if( level > 255 )
    {
        level = 255;
    }

    /* Compact Inferno-like progression: black/purple -> magenta -> orange ->
       yellow/white. It preserves more perceived contrast than a rainbow map. */
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if( level < 64 )
    {
        red = (uint8_t)( level * 80 / 63 );
        green = 0U;
        blue = (uint8_t)( 20 + ( level * 80 / 63 ) );
    }
    else if( level < 128 )
    {
        const int32_t local = level - 64;
        red = (uint8_t)( 80 + ( local * 120 / 63 ) );
        green = (uint8_t)( local * 30 / 63 );
        blue = (uint8_t)( 100 - ( local * 45 / 63 ) );
    }
    else if( level < 192 )
    {
        const int32_t local = level - 128;
        red = (uint8_t)( 200 + ( local * 55 / 63 ) );
        green = (uint8_t)( 30 + ( local * 130 / 63 ) );
        blue = (uint8_t)( 55 - ( local * 45 / 63 ) );
    }
    else
    {
        const int32_t local = level - 192;
        red = 255U;
        green = (uint8_t)( 160 + ( local * 95 / 63 ) );
        blue = (uint8_t)( 10 + ( local * 210 / 63 ) );
    }
    return rgb565( red, green, blue );
}

/** @brief Return a bilinearly interpolated native-frame sample using Q8 coordinates. */
static int32_t interpolate( uint32_t a_x_q8, uint32_t a_y_q8 )
{
    unsigned x0 = a_x_q8 >> 8;
    unsigned y0 = a_y_q8 >> 8;
    const unsigned fx = a_x_q8 & 0xFFU;
    const unsigned fy = a_y_q8 & 0xFFU;

    if( x0 >= ( MLX90640_WIDTH - 1U ) )
    {
        x0 = MLX90640_WIDTH - 1U;
    }
    if( y0 >= ( MLX90640_HEIGHT - 1U ) )
    {
        y0 = MLX90640_HEIGHT - 1U;
    }

    const unsigned x1 = ( x0 + 1U < MLX90640_WIDTH ) ? x0 + 1U : x0;
    const unsigned y1 = ( y0 + 1U < MLX90640_HEIGHT ) ? y0 + 1U : y0;
    const int32_t p00 = frame.pixels[ y0 * MLX90640_WIDTH + x0 ];
    const int32_t p10 = frame.pixels[ y0 * MLX90640_WIDTH + x1 ];
    const int32_t p01 = frame.pixels[ y1 * MLX90640_WIDTH + x0 ];
    const int32_t p11 = frame.pixels[ y1 * MLX90640_WIDTH + x1 ];
    const int32_t top = p00 * (int32_t)( 256U - fx ) + p10 * (int32_t)fx;
    const int32_t bottom = p01 * (int32_t)( 256U - fx ) + p11 * (int32_t)fx;
    return ( top * (int32_t)( 256U - fy ) + bottom * (int32_t)fy ) >> 16;
}

/** @brief Slowly adapt the display range to avoid color pumping between frames. */
static void update_display_range( void )
{
    const int32_t span = (int32_t)frame.maximum - (int32_t)frame.minimum;
    const int32_t margin =
        ( span > 0 ) ? span * THERMAL_RANGE_MARGIN_NUMERATOR / THERMAL_RANGE_MARGIN_DENOMINATOR : 1;
    const int32_t target_minimum = (int32_t)frame.minimum - margin;
    const int32_t target_maximum = (int32_t)frame.maximum + margin;

    if( !display_range_valid )
    {
        display_minimum = target_minimum;
        display_maximum = target_maximum;
        display_range_valid = true;
        return;
    }

    display_minimum =
        ( display_minimum * THERMAL_RANGE_OLD_WEIGHT + target_minimum ) / THERMAL_RANGE_DIVISOR;
    display_maximum =
        ( display_maximum * THERMAL_RANGE_OLD_WEIGHT + target_maximum ) / THERMAL_RANGE_DIVISOR;
}

void thermal_view_enter( void )
{
    display_range_valid = false;
}

esp_err_t thermal_view_render( void )
{
    const esp_err_t result = mlx90640_read_frame( &frame );
    if( ESP_ERR_NOT_FINISHED == result )
    {
        return ESP_OK;
    }

    display_clear( COLOR_BLACK );
    ui_chrome_draw( "THERMAL", COLOR_SUBDUED_TEAL );

    if( !mlx90640_is_detected() )
    {
        display_draw_text( 56, 70, "MLX90640 NOT DETECTED", COLOR_RED, 2 );
        display_draw_text( 73, 98, "I2C ADDRESS 0x33", COLOR_GRAY, 1 );
        return display_present();
    }

    if( ESP_OK != result )
    {
        display_draw_text( 72, 76, "THERMAL READ ERROR", COLOR_RED, 2 );
        display_present();
        return result;
    }

    update_display_range();

    /* Render one horizontal line at a time. This is bilinear interpolation of
       the native 32x24 measurements, not additional sensor resolution. */
    for( int y = 0; y < THERMAL_IMAGE_HEIGHT; ++y )
    {
        const uint32_t source_y =
            (uint32_t)y * ( MLX90640_HEIGHT - 1U ) * 256U / ( THERMAL_IMAGE_HEIGHT - 1U );
        for( int x = 0; x < THERMAL_IMAGE_WIDTH; ++x )
        {
            const uint32_t source_x =
                (uint32_t)x * ( MLX90640_WIDTH - 1U ) * 256U / ( THERMAL_IMAGE_WIDTH - 1U );
            const int32_t value = interpolate( source_x, source_y );
            display_fill_rect( THERMAL_IMAGE_X + x,
                               THERMAL_IMAGE_Y + y,
                               1,
                               1,
                               palette( value, display_minimum, display_maximum ) );
        }
    }

    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ) - 5,
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ),
                       11,
                       1,
                       COLOR_WHITE );
    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ),
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ) - 5,
                       1,
                       11,
                       COLOR_WHITE );

    char text[ 24 ];
    display_draw_text( THERMAL_INFO_X, 30, "RELATIVE IR", COLOR_GRAY, 1 );
    snprintf( text, sizeof( text ), "MAX %6d", (int)frame.maximum );
    display_draw_text( THERMAL_INFO_X, 52, text, COLOR_WHITE, 1 );
    snprintf( text, sizeof( text ), "MIN %6d", (int)frame.minimum );
    display_draw_text( THERMAL_INFO_X, 68, text, COLOR_WHITE, 1 );
    snprintf( text, sizeof( text ), "CTR %6d", (int)frame.center );
    display_draw_text( THERMAL_INFO_X, 84, text, COLOR_WHITE, 1 );
    display_draw_text( THERMAL_INFO_X, 116, "32x24 SENSOR", COLOR_GRAY, 1 );
    display_draw_text( THERMAL_INFO_X, 130, "CAL PENDING", COLOR_GRAY, 1 );

    const esp_err_t present = display_present();
    if( ESP_OK != present )
    {
        ESP_LOGW( TAG, "thermal LCD refresh failed" );
    }
    return present;
}
