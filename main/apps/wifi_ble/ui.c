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
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define UI_MAX_KNOWN_BRANDS 8U
#define UI_REFRESH_INTERVAL_US 100000LL
#define UI_SURVEILLANCE_MAX_ROWS 8U
#define UI_VENDOR_X 10
#define UI_VENDOR_FIRST_Y 42
#define UI_VENDOR_ROW_HEIGHT 10
#define UI_SURVEILLANCE_X 166
#define UI_SURVEILLANCE_FIRST_Y 77
#define UI_SURVEILLANCE_ROW_HEIGHT 10
#define UI_CHANNEL_GRID_X 11
#define UI_CHANNEL_GRID_TOP_Y 151
#define UI_CHANNEL_GRID_CELL_WIDTH 20
#define UI_CHANNEL_GRID_CELL_HEIGHT 10
#define UI_CHANNEL_GRID_COLUMN_STEP 21

static const char *TAG = "ui";
static int64_t last_refresh_us;
static bool surveillance_page;

/** @brief Theme-aware instrument frame; preserve the Pixel Quest clipped-corner language. */
static void draw_dashboard_panel( int a_x, int a_y, int a_width, int a_height )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ui_game_active() )
    {
        ui_game_draw_panel( a_x, a_y, a_width, a_height, theme->divider );
        return;
    }
    display_fill_rect( a_x, a_y, a_width, a_height, theme->surface );
    display_fill_rect( a_x, a_y, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y + a_height - 1, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y, 1, a_height, theme->divider );
    display_fill_rect( a_x + a_width - 1, a_y, 1, a_height, theme->divider );
}

static void draw_channel_box( unsigned a_channel, uint16_t a_active_channel_mask )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned index = a_channel - 1U;
    const int x = UI_CHANNEL_GRID_X + (int)index * UI_CHANNEL_GRID_COLUMN_STEP;
    const int y = UI_CHANNEL_GRID_TOP_Y;
    const bool active = 0U != ( a_active_channel_mask & (uint16_t)( 1U << index ) );
    const uint16_t color = active ? theme->warning : theme->info;

    display_fill_rect( x, y, UI_CHANNEL_GRID_CELL_WIDTH - 2, 1, color );
    display_fill_rect( x, y + UI_CHANNEL_GRID_CELL_HEIGHT - 1, UI_CHANNEL_GRID_CELL_WIDTH - 2, 1, color );
    display_fill_rect( x, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );
    display_fill_rect( x + UI_CHANNEL_GRID_CELL_WIDTH - 3, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );
    char label[ 3 ];
    (void)snprintf( label, sizeof( label ), "%u", a_channel );
    display_draw_text( x + ( a_channel < 10U ? 6 : 3 ), y + 2, label, color, 1U );
}

static void render_summary( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    observation_ui_snapshot_t snapshot = { 0 };
    observation_vendor_count_t vendors[ UI_MAX_KNOWN_BRANDS ] = { 0 };
    observation_surveillance_signature_t signatures[ UI_SURVEILLANCE_MAX_ROWS ] = { 0 };
    observation_store_ui_snapshot(
        &snapshot, vendors, UI_MAX_KNOWN_BRANDS, signatures, UI_SURVEILLANCE_MAX_ROWS );
    const observation_store_summary_t summary = snapshot.summary;

    display_clear( theme->background );
    ui_chrome_draw( "WIFI / BLE", theme->secondary );
    /* Keep system-owned status glyphs above y=22, and use the #115 panel style. */
    draw_dashboard_panel( 4, 24, 152, 108 );
    draw_dashboard_panel( 160, 24, 156, 34 );
    draw_dashboard_panel( 160, 62, 156, 70 );
    draw_dashboard_panel( 4, 136, 312, 28 );

    char line[ 64 ];
    (void)snprintf( line, sizeof( line ), "WIFI %u", (unsigned)summary.wifi_count );
    display_draw_text( 166, 29, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "BLE %u", (unsigned)summary.ble_count );
    display_draw_text( 166, 42, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "UNKNOWN %u", (unsigned)summary.unknown_count );
    display_draw_text( 239, 29, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "RANDOM %u", (unsigned)summary.random_count );
    display_draw_text( 239, 42, line, theme->info, 1U );

    display_draw_text( 10, 29, "TOP VENDORS", theme->secondary, 1U );
    const size_t vendor_rows = snapshot.vendor_count < UI_MAX_KNOWN_BRANDS ? snapshot.vendor_count : UI_MAX_KNOWN_BRANDS;
    for( size_t i = 0U; i < vendor_rows; ++i )
    {
        (void)snprintf( line,
                        sizeof( line ),
                        "%-15.15s %4u",
                        vendors[ i ].name,
                        (unsigned)vendors[ i ].count );
        display_draw_text( UI_VENDOR_X,
                           UI_VENDOR_FIRST_Y + (int)i * UI_VENDOR_ROW_HEIGHT,
                           line,
                           theme->normal,
                           1U );
    }
    (void)snprintf( line, sizeof( line ), "SURVEILLANCE %u", (unsigned)snapshot.surveillance_count );
    display_draw_text( UI_SURVEILLANCE_X,
                       66,
                       line,
                       theme->secondary,
                       1U );
    /* Reserve the fifth row for overflow when more than five signatures are active. */
    const size_t shown = snapshot.signature_count > 5U ? 4U : snapshot.signature_count;
    for( size_t row = 0U; row < shown; ++row )
    {
        (void)snprintf( line,
                        sizeof( line ),
                        "%-14.14s %3u",
                        surveillance_signature_label( signatures[ row ].signature ),
                        (unsigned)signatures[ row ].count );
        display_draw_text( UI_SURVEILLANCE_X,
                           UI_SURVEILLANCE_FIRST_Y + (int)row * UI_SURVEILLANCE_ROW_HEIGHT,
                           line,
                           theme->warning,
                           1U );
    }
    if( snapshot.signature_count > shown )
    {
        (void)snprintf( line,
                        sizeof( line ),
                        "+%u MORE",
                        (unsigned)( snapshot.signature_count - shown ) );
        display_draw_text( UI_SURVEILLANCE_X, 117, line, theme->info, 1U );
    }

    display_draw_text( UI_CHANNEL_GRID_X, 139, "AP CHANNELS", theme->secondary, 1U );
    for( unsigned channel = 1U; channel <= RFSCOUT_WIFI_CHANNEL_MAX; ++channel )
    {
        draw_channel_box( channel, snapshot.ap_channel_mask );
    }
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
    const bool inside = west <= east ? lon >= west && lon <= east : lon >= west || lon <= east;
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
    const double dx = delta_lon * 0.017453292519943295 * cos( a_latitude * 0.017453292519943295 );
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
