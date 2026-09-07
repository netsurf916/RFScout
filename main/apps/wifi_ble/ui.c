/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "display.h"
#include "observation_store.h"
#include "surveillance.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>

#define UI_MAX_KNOWN_BRANDS 8U
#define UI_REFRESH_INTERVAL_US 100000LL
#define UI_LIST_START_Y 43
#define UI_LIST_ROW_HEIGHT 12
#define UI_STATIC_DIVIDER_Y 139
#define UI_UNKNOWN_ROW_Y 146
#define UI_RANDOM_ROW_Y 158
#define UI_COLUMN_DIVIDER_X 198
#define UI_SURVEILLANCE_X 205
#define UI_SURVEILLANCE_LABEL_WIDTH 14
#define UI_SURVEILLANCE_MAX_ROWS 9U

static const char *TAG = "ui";
static int64_t last_refresh_us;

static void draw_static_rows( size_t a_unknown_count, size_t a_random_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( 5, UI_STATIC_DIVIDER_Y, UI_COLUMN_DIVIDER_X - 10, 1, theme->divider );

    char line[ 64 ];
    snprintf( line, sizeof( line ), "%-27.27s %3u", "UNKNOWN", (unsigned)a_unknown_count );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_UNKNOWN_ROW_Y, line, theme->info, 1 );

    snprintf( line, sizeof( line ), "%-27.27s %3u", "RANDOM", (unsigned)a_random_count );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_RANDOM_ROW_Y, line, theme->info, 1 );
}

static void draw_surveillance_rows( const observation_surveillance_signature_t *a_rows,
                                    size_t a_row_count,
                                    size_t a_active_signature_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    int y = UI_LIST_START_Y;
    for( size_t row = 0U; row < a_row_count; ++row )
    {
        char line[ 32 ];
        snprintf( line,
                  sizeof( line ),
                  "%-*.*s %3u",
                  UI_SURVEILLANCE_LABEL_WIDTH,
                  UI_SURVEILLANCE_LABEL_WIDTH,
                  surveillance_signature_label( a_rows[ row ].signature ),
                  (unsigned)a_rows[ row ].count );
        display_draw_text( UI_SURVEILLANCE_X, y, line, theme->warning, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }

    if( a_active_signature_count > a_row_count )
    {
        char line[ 32 ];
        snprintf( line,
                  sizeof( line ),
                  "+%u MORE",
                  (unsigned)( a_active_signature_count - a_row_count ) );
        display_draw_text( UI_SURVEILLANCE_X, y, line, theme->info, 1 );
    }
}

static void render_summary( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    observation_store_summary_t summary = { 0 };
    observation_vendor_count_t vendors[ UI_MAX_KNOWN_BRANDS ] = { 0 };
    const size_t vendor_count = observation_store_summary( &summary, vendors, UI_MAX_KNOWN_BRANDS );
    observation_surveillance_signature_t surveillance_rows[ UI_SURVEILLANCE_MAX_ROWS ] = { 0 };
    size_t surveillance_count = 0U;
    const size_t active_signature_count = observation_store_active_surveillance(
        surveillance_rows, UI_SURVEILLANCE_MAX_ROWS, &surveillance_count );
    const size_t surveillance_row_capacity = ( active_signature_count > UI_SURVEILLANCE_MAX_ROWS )
                                                 ? ( UI_SURVEILLANCE_MAX_ROWS - 1U )
                                                 : UI_SURVEILLANCE_MAX_ROWS;
    const size_t surveillance_row_count = ( active_signature_count < surveillance_row_capacity )
                                              ? active_signature_count
                                              : surveillance_row_capacity;

    display_clear( theme->background );
    ui_chrome_draw( "WIFI / BLE", theme->secondary );

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "DEV %u   WIFI %u   BLE %u",
              (unsigned)summary.device_count,
              (unsigned)summary.wifi_count,
              (unsigned)summary.ble_count );
    display_draw_text( UI_LAYOUT_LEFT_X, 25, line, theme->normal, 1 );

    snprintf( line, sizeof( line ), "SURVEILLANCE %u", (unsigned)surveillance_count );
    display_draw_text(
        210, 25, line, ( 0U == surveillance_count ) ? theme->info : theme->warning, 1 );

    display_fill_rect( 5, 36, 310, 1, theme->divider );
    const rfscout_view_t *view = display_app_view();
    const int view_height = ( NULL != view ) ? (int)view->height : UI_RANDOM_ROW_Y + 12;
    display_fill_rect( UI_COLUMN_DIVIDER_X, 36, 1, view_height - 36, theme->divider );

    int y = UI_LIST_START_Y;
    for( size_t i = 0U; i < vendor_count; ++i )
    {
        snprintf(
            line, sizeof( line ), "%-27.27s %3u", vendors[ i ].name, (unsigned)vendors[ i ].count );
        display_draw_text( UI_LAYOUT_LEFT_X, y, line, theme->warning, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }

    draw_static_rows( summary.unknown_count, summary.random_count );
    draw_surveillance_rows( surveillance_rows, surveillance_row_count, active_signature_count );

    if( ESP_OK != display_present() )
    {
        ESP_LOGW( TAG, "LCD refresh failed" );
    }
}

void ui_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_refresh_us ) < UI_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_refresh_us = now_us;
    render_summary();
}
