/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "display.h"
#include "geo.h"
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

#define UI_MAX_KNOWN_BRANDS 8U
#define UI_REFRESH_INTERVAL_US 100000LL
#define UI_SURVEILLANCE_MAX_ROWS 8U
#define UI_VENDOR_X 10
#define UI_VENDOR_FIRST_Y 42
#define UI_VENDOR_ROW_HEIGHT 10
#define UI_SURVEILLANCE_X 166
#define UI_SURVEILLANCE_FIRST_Y 77
#define UI_SURVEILLANCE_ROW_HEIGHT 10
#define UI_CHANNEL_GRID_X 12
#define UI_CHANNEL_GRID_TOP_Y 150
#define UI_CHANNEL_GRID_CELL_WIDTH 20
#define UI_CHANNEL_GRID_CELL_HEIGHT 12
#define UI_CHANNEL_GRID_COLUMN_STEP 21

static const char *TAG = "ui";
static int64_t last_refresh_us;

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
    const uint16_t color = active ? theme->accent : theme->divider;
    const uint16_t label_color = active ? theme->accent : theme->secondary;

    display_fill_rect( x, y, UI_CHANNEL_GRID_CELL_WIDTH, 1, color );
    display_fill_rect(
        x, y + UI_CHANNEL_GRID_CELL_HEIGHT - 1, UI_CHANNEL_GRID_CELL_WIDTH, 1, color );
    display_fill_rect( x, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );
    display_fill_rect(
        x + UI_CHANNEL_GRID_CELL_WIDTH - 1, y, 1, UI_CHANNEL_GRID_CELL_HEIGHT, color );
    char label[ 3 ];
    (void)snprintf( label, sizeof( label ), "%u", a_channel );
    const int text_width = a_channel < 10U ? 6 : 12;
    display_draw_text(
        x + ( UI_CHANNEL_GRID_CELL_WIDTH - text_width ) / 2, y + 2, label, label_color, 1U );
}

/** @brief Recompute nearest known signature distances at most once per second. */
static void surveillance_distances( const observation_surveillance_signature_t *a_rows,
                                    size_t a_count,
                                    const rfscout_gps_snapshot_t *a_gps,
                                    double *a_meters,
                                    bool *a_valid )
{
    static int64_t last_update_us;
    static surveillance_signature_id_t cached_signatures[ 5 ];
    static double cached_meters[ 5 ];
    static bool cached_valid[ 5 ];
    const int64_t now_us = esp_timer_get_time();
    bool refresh = ( 0 == last_update_us ) || ( now_us - last_update_us >= 1000000LL );
    for( size_t row = 0U; row < a_count; ++row )
    {
        if( cached_signatures[ row ] != a_rows[ row ].signature )
        {
            refresh = true;
        }
    }
    if( refresh )
    {
        for( size_t row = 0U; row < a_count; ++row )
        {
            cached_signatures[ row ] = a_rows[ row ].signature;
            cached_valid[ row ] = false;
        }
        const size_t total = observation_store_surveillance_devices( NULL, 0U );
        for( size_t index = 0U; index < total; ++index )
        {
            observation_history_identity_t device = { 0 };
            if( !observation_store_surveillance_get( index, &device ) ||
                !device.receiver_bounds.valid )
            {
                continue;
            }
            const double meters = geo_bounds_distance_m(
                &device.receiver_bounds, a_gps->latitude_deg, a_gps->longitude_deg );
            for( size_t row = 0U; row < a_count; ++row )
            {
                if( device.signature == a_rows[ row ].signature &&
                    ( !cached_valid[ row ] || meters < cached_meters[ row ] ) )
                {
                    cached_meters[ row ] = meters;
                    cached_valid[ row ] = true;
                }
            }
        }
        last_update_us = now_us;
    }
    for( size_t row = 0U; row < a_count; ++row )
    {
        a_meters[ row ] = cached_meters[ row ];
        a_valid[ row ] = cached_valid[ row ];
    }
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
    draw_dashboard_panel( UI_LAYOUT_PANEL_LEFT_X, UI_LAYOUT_PANEL_TOP_Y, 152, 108 );
    draw_dashboard_panel( 160, UI_LAYOUT_PANEL_TOP_Y, 156, 34 );
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
    const size_t vendor_rows =
        snapshot.vendor_count < UI_MAX_KNOWN_BRANDS ? snapshot.vendor_count : UI_MAX_KNOWN_BRANDS;
    for( size_t i = 0U; i < vendor_rows; ++i )
    {
        (void)snprintf(
            line, sizeof( line ), "%-15.15s %4u", vendors[ i ].name, (unsigned)vendors[ i ].count );
        display_draw_text( UI_VENDOR_X,
                           UI_VENDOR_FIRST_Y + (int)i * UI_VENDOR_ROW_HEIGHT,
                           line,
                           theme->normal,
                           1U );
    }
    (void)snprintf(
        line, sizeof( line ), "SURVEILLANCE %u", (unsigned)snapshot.surveillance_count );
    display_draw_text( UI_SURVEILLANCE_X, 66, line, theme->secondary, 1U );
    /* Reserve the fifth row for overflow when more than five signatures are active. */
    const size_t shown = snapshot.signature_count > 5U ? 4U : snapshot.signature_count;
    rfscout_gps_snapshot_t gps = { 0 };
    const bool has_fix = gps_snapshot_get( &gps ) && gps_location_fresh( &gps );
    double nearest_meters[ 5 ] = { 0 };
    bool nearest_valid[ 5 ] = { false };
    if( has_fix && shown > 0U )
    {
        surveillance_distances( signatures, shown, &gps, nearest_meters, nearest_valid );
    }

    for( size_t row = 0U; row < shown; ++row )
    {
        char distance[ 8 ] = "";
        if( has_fix && nearest_valid[ row ] )
        {
            geo_format_distance( nearest_meters[ row ], distance, sizeof( distance ) );
        }
        (void)snprintf( line,
                        sizeof( line ),
                        "%-9.9s %2u",
                        surveillance_signature_label( signatures[ row ].signature ),
                        (unsigned)signatures[ row ].count );
        const int row_y = UI_SURVEILLANCE_FIRST_Y + (int)row * UI_SURVEILLANCE_ROW_HEIGHT;
        display_draw_text( UI_SURVEILLANCE_X, row_y, line, theme->warning, 1U );
        if( distance[ 0 ] != '\0' )
        {
            const int distance_x = 309 - (int)strlen( distance ) * 6;
            display_draw_text( distance_x, row_y, distance, theme->info, 1U );
        }
    }
    if( snapshot.signature_count > shown )
    {
        (void)snprintf(
            line, sizeof( line ), "+%u MORE", (unsigned)( snapshot.signature_count - shown ) );
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
