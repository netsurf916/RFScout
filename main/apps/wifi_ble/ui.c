/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "display.h"
#include "gps.h"
#include "observation_store.h"
#include "surveillance.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

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
#define UI_SURVEILLANCE_MAX_ROWS 8U
#define UI_CHANNEL_GRID_X ( UI_COLUMN_DIVIDER_X + 5 )
#define UI_CHANNEL_GRID_TOP_Y 143
#define UI_CHANNEL_GRID_CELL_WIDTH 16
#define UI_CHANNEL_GRID_CELL_HEIGHT 12
#define UI_CHANNEL_GRID_ROW_STEP 14
#define UI_CHANNEL_GRID_COLUMNS 7U

static const char *TAG = "ui";
static int64_t last_refresh_us;
static bool surveillance_page;
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

static void draw_channel_box( unsigned a_channel, uint16_t a_active_channel_mask )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned index = a_channel - 1U;
    const unsigned column = index % UI_CHANNEL_GRID_COLUMNS;
    const unsigned row = index / UI_CHANNEL_GRID_COLUMNS;
    const int x = UI_CHANNEL_GRID_X + (int)column * UI_CHANNEL_GRID_CELL_WIDTH;
    const int y = UI_CHANNEL_GRID_TOP_Y + (int)row * UI_CHANNEL_GRID_ROW_STEP;
    const bool active = 0U != ( a_active_channel_mask & (uint16_t)( 1U << index ) );
    const uint16_t color = active ? theme->warning : theme->info;

    display_fill_rect( x, y, UI_CHANNEL_GRID_CELL_WIDTH, 1, color );
    display_fill_rect(
        x, y + UI_CHANNEL_GRID_CELL_HEIGHT - 1, UI_CHANNEL_GRID_CELL_WIDTH, 1, color );
    display_fill_rect( x, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );
    display_fill_rect(
        x + UI_CHANNEL_GRID_CELL_WIDTH - 1, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );

    char label[ 3 ];
    snprintf( label, sizeof( label ), "%u", a_channel );
    const int label_width = ( a_channel < 10U ) ? 6 : 12;
    const int label_x = x + ( UI_CHANNEL_GRID_CELL_WIDTH - label_width ) / 2;
    display_draw_text( label_x, y + 3, label, color, 1U );
}

static void draw_channel_grid( uint16_t a_active_channel_mask )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( UI_CHANNEL_GRID_X,
                       UI_STATIC_DIVIDER_Y,
                       RFSCOUT_DISPLAY_WIDTH - UI_CHANNEL_GRID_X - 5,
                       1,
                       theme->divider );

    for( unsigned channel = 1U; channel <= RFSCOUT_WIFI_CHANNEL_MAX; ++channel )
    {
        draw_channel_box( channel, a_active_channel_mask );
    }
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
    const uint16_t active_ap_channel_mask = observation_store_wifi_ap_channel_mask();

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
    draw_channel_grid( active_ap_channel_mask );

    if( ESP_OK != display_present() )
    {
        ESP_LOGW( TAG, "LCD refresh failed" );
    }
}

void ui_set_surveillance_page( bool a_enabled )
{
    surveillance_page = a_enabled;
    last_refresh_us = 0;
}

bool ui_surveillance_page_active( void )
{
    return surveillance_page;
}

static double live_receiver_distance( const observation_history_identity_t *a_device,
                                      double a_latitude,
                                      double a_longitude )
{
    double lat = a_latitude;
    double lon = a_longitude;
    if( lat < a_device->min_latitude )
        lat = a_device->min_latitude;
    if( lat > a_device->max_latitude )
        lat = a_device->max_latitude;
    const double west = a_device->min_longitude;
    const double east = a_device->max_longitude;
    const bool inside = west <= east ? lon >= west && lon <= east
                                     : lon >= west || lon <= east;
    if( !inside )
    {
        double to_west = fabs( lon - west );
        double to_east = fabs( lon - east );
        if( to_west > 180.0 )
            to_west = 360.0 - to_west;
        if( to_east > 180.0 )
            to_east = 360.0 - to_east;
        lon = to_west <= to_east ? west : east;
    }
    const double dy = ( a_latitude - lat ) * 0.017453292519943295;
    double delta_lon = fabs( a_longitude - lon );
    if( delta_lon > 180.0 )
        delta_lon = 360.0 - delta_lon;
    const double dx =
        delta_lon * 0.017453292519943295 * cos( a_latitude * 0.017453292519943295 );
    return 6371000.0 * sqrt( dx * dx + dy * dy );
}

static void render_surveillance_page( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    rfscout_gps_snapshot_t gps = { 0 };
    const bool has_fix =
        gps_snapshot_get( &gps ) && gps.location_valid && gps.location_age_ms <= 5000U;
    const size_t count = observation_store_surveillance_devices( NULL, 0U );

    display_clear( theme->background );
    ui_chrome_draw( "SURVEILLANCE", theme->secondary );
    char line[ 80 ];
    snprintf(
        line, sizeof( line ), "ACTIVE %u  %s", (unsigned)count, has_fix ? "GPS FIX" : "NO FIX" );
    display_draw_text( UI_LAYOUT_LEFT_X, 25, line, theme->info, 1U );

    for( size_t row = 0U; row < count && row < 6U; ++row )
    {
        observation_history_identity_t device = { 0 };
        if( !observation_store_surveillance_get( row, &device ) )
            continue;
        char distance[ 20 ] = "--";
        if( has_fix && device.location_valid )
        {
            const double meters =
                live_receiver_distance( &device, gps.latitude_deg, gps.longitude_deg );
            if( meters >= 1000.0 )
                snprintf( distance, sizeof( distance ), "%.1fkm", meters / 1000.0 );
            else
                snprintf( distance, sizeof( distance ), "%.0fm", meters );
        }
        snprintf( line,
                  sizeof( line ),
                  "%c %02X%02X%02X %-13.13s %s",
                  ' ',
                  device.address[ 3 ],
                  device.address[ 4 ],
                  device.address[ 5 ],
                  device.vendor[ 0 ] ? device.vendor : "Unknown",
                  distance );
        display_draw_text( UI_LAYOUT_LEFT_X, 43 + (int)row * 17, line, theme->normal, 1U );
    }
    (void)display_present();
}

void ui_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_refresh_us ) < UI_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_refresh_us = now_us;
    if( surveillance_page )
    {
        render_surveillance_page();
    }
    else
    {
        render_summary();
    }
}
