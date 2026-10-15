/**
    thermal_view.c : RFScout thermal application view implementation
    Description: Displays the native MLX90640 cells as a responsive relative-IR heat map.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "thermal_view.h"

#include "app_framework.h"
#include "display.h"
#include "mlx90640.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_log.h"

#include <stdio.h>

#define THERMAL_IMAGE_X ( UI_LAYOUT_PANEL_LEFT_X + 4 )
#define THERMAL_IMAGE_Y ( THERMAL_PANEL_Y + 8 )
#define THERMAL_IMAGE_WIDTH 186
#define THERMAL_IMAGE_HEIGHT 120
#define THERMAL_INFO_X ( THERMAL_PANEL_X + UI_LAYOUT_PANEL_TEXT_INSET_X )
#define THERMAL_PANEL_X 204
#define THERMAL_PANEL_Y UI_LAYOUT_PANEL_TOP_Y
#define THERMAL_PANEL_WIDTH 112
#define THERMAL_PANEL_HEIGHT ( UI_LAYOUT_PANEL_BOTTOM_Y - THERMAL_PANEL_Y )
#define THERMAL_CELL_WIDTH ( THERMAL_IMAGE_WIDTH / MLX90640_WIDTH )
#define THERMAL_CELL_HEIGHT ( THERMAL_IMAGE_HEIGHT / MLX90640_HEIGHT )
#define THERMAL_RANGE_MARGIN_DENOMINATOR 16

static const char *TAG = "thermal_view";
static mlx90640_frame_t frame;

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

/** @brief Suppress alternating raw-pixel pattern spatially, without frame lag. */
static int32_t spatial_sample( int a_x, int a_y )
{
    const int center = a_y * MLX90640_WIDTH + a_x;
    const int32_t value = frame.pixels[ center ];
    int32_t sum = value * 4;
    int weight = 4;
    if( a_x > 0 )
    {
        sum += frame.pixels[ center - 1 ];
        ++weight;
    }
    if( a_x + 1 < MLX90640_WIDTH )
    {
        sum += frame.pixels[ center + 1 ];
        ++weight;
    }
    if( a_y > 0 )
    {
        sum += frame.pixels[ center - MLX90640_WIDTH ];
        ++weight;
    }
    if( a_y + 1 < MLX90640_HEIGHT )
    {
        sum += frame.pixels[ center + MLX90640_WIDTH ];
        ++weight;
    }
    return sum / weight;
}

void thermal_view_enter( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "THERMAL", theme->secondary );
    if( ui_game_active() )
    {
        ui_game_draw_panel( UI_LAYOUT_PANEL_LEFT_X, THERMAL_PANEL_Y, 196, THERMAL_PANEL_HEIGHT, theme->divider );
        ui_game_draw_panel( THERMAL_PANEL_X, THERMAL_PANEL_Y, THERMAL_PANEL_WIDTH, THERMAL_PANEL_HEIGHT, theme->divider );
    }
    else
    {
        display_fill_rect_raw( UI_LAYOUT_PANEL_LEFT_X, THERMAL_PANEL_Y, 196, THERMAL_PANEL_HEIGHT, theme->surface );
        display_fill_rect_raw( THERMAL_PANEL_X, THERMAL_PANEL_Y, THERMAL_PANEL_WIDTH, THERMAL_PANEL_HEIGHT, theme->surface );
    }
    display_draw_text( THERMAL_INFO_X, THERMAL_PANEL_Y + UI_LAYOUT_PANEL_TITLE_INSET_Y,
                       "IR INTENSITY", theme->secondary, 1U );
    display_draw_text( THERMAL_IMAGE_X + 30, THERMAL_IMAGE_Y + 52,
                       "WAITING FOR FRAME", theme->secondary, 1U );
    (void)display_present();
}

esp_err_t thermal_view_render( void )
{
    const esp_err_t result = mlx90640_read_frame( &frame );
    if( ESP_ERR_NOT_FINISHED == result )
    {
        return ESP_OK;
    }

    if( ESP_OK == result )
    {
        app_framework_report_availability( RFSCOUT_APP_THERMAL,
                                           RFSCOUT_APP_AVAILABILITY_AVAILABLE );
    }
    else if( !mlx90640_is_detected() )
    {
        /* Periodic hot-plug probing intentionally skips the active app. Runtime
           I/O failure is therefore authoritative for an active Thermal session
           and must update the framework before app_manager navigates away. */
        app_framework_report_availability( RFSCOUT_APP_THERMAL,
                                           RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
    }

    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "THERMAL", theme->secondary );

    if( !mlx90640_is_detected() )
    {
        display_draw_text( 56, 70, "MLX90640 NOT DETECTED", theme->error, 2 );
        display_draw_text( 73, 98, "I2C ADDRESS 0x33", theme->info, 1 );
        return display_present();
    }

    if( ESP_OK != result )
    {
        display_draw_text( 72, 76, "THERMAL READ ERROR", theme->error, 2 );
        display_present();
        return result;
    }

    const int32_t span = (int32_t)frame.maximum - (int32_t)frame.minimum;
    const int32_t margin = span > 0 ? span / THERMAL_RANGE_MARGIN_DENOMINATOR : 1;
    const int32_t display_minimum = (int32_t)frame.minimum - margin;
    const int32_t display_maximum = (int32_t)frame.maximum + margin;

    /* Both surfaces share an exact baseline and height. Use the existing
       clipped-corner panel primitive rather than drawing legacy box edges. */
    if( ui_game_active() )
    {
        ui_game_draw_panel( UI_LAYOUT_PANEL_LEFT_X, THERMAL_PANEL_Y, 196, THERMAL_PANEL_HEIGHT, theme->divider );
        ui_game_draw_panel( THERMAL_PANEL_X,
                            THERMAL_PANEL_Y,
                            THERMAL_PANEL_WIDTH,
                            THERMAL_PANEL_HEIGHT,
                            theme->divider );
    }
    else
    {
        display_fill_rect_raw( UI_LAYOUT_PANEL_LEFT_X, THERMAL_PANEL_Y, 196, THERMAL_PANEL_HEIGHT, theme->surface );
        display_fill_rect_raw( THERMAL_PANEL_X,
                               THERMAL_PANEL_Y,
                               THERMAL_PANEL_WIDTH,
                               THERMAL_PANEL_HEIGHT,
                               theme->surface );
    }

    /* One filled rectangle per native sensor cell. This avoids thousands of
       individual-pixel draws, interpolation artifacts and false spatial detail. */
    for( int y = 0; y < MLX90640_HEIGHT; ++y )
    {
        for( int x = 0; x < MLX90640_WIDTH; ++x )
        {
            const int32_t value = spatial_sample( x, y );
            display_fill_rect_raw( THERMAL_IMAGE_X + x * THERMAL_CELL_WIDTH,
                                   THERMAL_IMAGE_Y + y * THERMAL_CELL_HEIGHT,
                                   THERMAL_CELL_WIDTH,
                                   THERMAL_CELL_HEIGHT,
                                   palette( value, display_minimum, display_maximum ) );
        }
    }

    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ) - 5,
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ),
                       11,
                       1,
                       theme->text );
    display_fill_rect( THERMAL_IMAGE_X + ( THERMAL_IMAGE_WIDTH / 2 ),
                       THERMAL_IMAGE_Y + ( THERMAL_IMAGE_HEIGHT / 2 ) - 5,
                       1,
                       11,
                       theme->text );

    char text[ 24 ];
    display_draw_text( THERMAL_INFO_X,
                       THERMAL_PANEL_Y + UI_LAYOUT_PANEL_TITLE_INSET_Y,
                       "IR INTENSITY",
                       theme->secondary,
                       1U );
    snprintf( text, sizeof( text ), "MAX %6d", (int)frame.maximum );
    display_draw_text( THERMAL_INFO_X, 56, text, theme->text, 1 );
    snprintf( text, sizeof( text ), "MIN %6d", (int)frame.minimum );
    display_draw_text( THERMAL_INFO_X, 74, text, theme->text, 1 );
    snprintf( text, sizeof( text ), "CTR %6d", (int)frame.center );
    display_draw_text( THERMAL_INFO_X, 92, text, theme->text, 1 );
    display_draw_text( THERMAL_INFO_X, 128, "32x24 LIVE", theme->info, 1 );
    display_draw_text( THERMAL_INFO_X, 144, "RAW IR DATA", theme->secondary, 1 );

    const esp_err_t present = display_present();
    if( ESP_OK != present )
    {
        ESP_LOGW( TAG, "thermal LCD refresh failed" );
    }
    return present;
}

void thermal_view_menu( unsigned a_selected, bool a_locked, bool a_keep_awake )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const char *items[] = { "Back", "App lock", "Keep screen awake" };
    display_clear( theme->background );
    ui_chrome_draw( "THERMAL / SETTINGS", theme->secondary );
    if( ui_game_active() )
    {
        ui_game_draw_panel( 8, 32, 304, 108, theme->divider );
    }
    for( unsigned index = 0U; index < 3U; ++index )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)index * UI_LAYOUT_MENU_ROW_HEIGHT;
        ui_game_draw_menu_marker(
            UI_LAYOUT_LEFT_X + 4, y, a_selected == index ? ">" : " ", theme->selected );
        display_draw_text( UI_LAYOUT_LABEL_X + 6,
                           y,
                           items[ index ],
                           a_selected == index ? theme->selected : theme->text,
                           1U );
    }
    display_draw_text( 251, UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
                       a_locked ? "ON" : "OFF", theme->info, 1U );
    display_draw_text( 251, UI_LAYOUT_MENU_START_Y + 2 * UI_LAYOUT_MENU_ROW_HEIGHT,
                       a_keep_awake ? "ON" : "OFF", theme->info, 1U );
    (void)display_present();
}
