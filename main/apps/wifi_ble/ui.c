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
#include "surveillance_history.h"
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
static unsigned surveillance_selection;
static observation_history_identity_t live_surveillance_rows[ 48 ];
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
    surveillance_selection = 0U;
    last_refresh_us = 0;
}

bool ui_surveillance_page_active( void )
{
    return surveillance_page;
}

void ui_surveillance_navigate( int a_direction )
{
    if( a_direction > 0 )
    {
        ++surveillance_selection;
    }
    else if( surveillance_selection > 0U )
    {
        --surveillance_selection;
    }
    last_refresh_us = 0;
}

static bool history_identity_live( const surveillance_history_entry_t *a_entry,
                                   const observation_history_identity_t *a_live,
                                   size_t a_count )
{
    for( size_t i = 0U; i < a_count; ++i )
        if( a_entry->address_type == a_live_surveillance_rows[ i ].address_type &&
            0 == memcmp( a_entry->address, a_live_surveillance_rows[ i ].address, 6U ) )
            return true;
    return false;
}

static void render_surveillance_page( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    rfscout_gps_snapshot_t gps = { 0 };
    const bool has_fix =
        gps_snapshot_get( &gps ) && gps.location_valid && gps.location_age_ms <= 5000U;

    /* Live devices are independent of GPS and must not be hidden by history replay. */
    const size_t live_total = observation_store_surveillance_devices( live_surveillance_rows, 48U );
    const size_t live_count = live_total < 48U ? live_total : 48U;
    const size_t history_count = surveillance_history_list( NULL, 0U );
    size_t unique_history = 0U;
    for( size_t i = 0U; i < history_count; ++i )
    {
        surveillance_history_entry_t entry = { 0 };
        if( surveillance_history_get( i, &entry ) &&
            !history_identity_live( &entry, live_surveillance_rows, live_count ) )
            ++unique_history;
    }
    const size_t count = live_count + unique_history;
    if( count > 0U && surveillance_selection >= count )
        surveillance_selection = (unsigned)( count - 1U );

    display_clear( theme->background );
    ui_chrome_draw( "SURVEILLANCE", theme->secondary );
    char line[ 80 ];
    snprintf( line,
              sizeof( line ),
              "LIVE %u  SAVED %u  GPS %s",
              (unsigned)live_count,
              (unsigned)unique_history,
              has_fix ? "FIX" : "NO FIX" );
    display_draw_text( UI_LAYOUT_LEFT_X, 25, line, theme->info, 1U );

    const size_t start = surveillance_selection >= 6U ? surveillance_selection - 5U : 0U;
    size_t row = 0U;
    for( size_t i = 0U; i < live_count; ++i, ++row )
    {
        if( row < start || row >= start + 6U )
            continue;
        snprintf( line,
                  sizeof( line ),
                  "%c * %02X%02X%02X %-13.13s LIVE",
                  row == surveillance_selection ? '>' : ' ',
                  live_surveillance_rows[ i ].address[ 3 ],
                  live_surveillance_rows[ i ].address[ 4 ],
                  live_surveillance_rows[ i ].address[ 5 ],
                  live_surveillance_rows[ i ].vendor[ 0 ] ? live_surveillance_rows[ i ].vendor : "Unknown" );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           43 + (int)( row - start ) * 17,
                           line,
                           row == surveillance_selection ? theme->selected : theme->normal,
                           1U );
    }
    for( size_t i = 0U; i < history_count; ++i )
    {
        surveillance_history_entry_t entry = { 0 };
        if( !surveillance_history_get( i, &entry ) ||
            history_identity_live( &entry, live_surveillance_rows, live_count ) )
            continue;
        if( row >= start && row < start + 6U )
        {
            char distance[ 20 ] = "--";
            if( has_fix )
            {
                const double meters = entry.nearest_distance_m;
                if( meters >= 1000.0 )
                    snprintf( distance, sizeof( distance ), "%.1fkm", meters / 1000.0 );
                else
                    snprintf( distance, sizeof( distance ), "%.0fm", meters );
            }
            snprintf( line,
                      sizeof( line ),
                      "%c   %02X%02X%02X %-13.13s %s",
                      row == surveillance_selection ? '>' : ' ',
                      entry.address[ 3 ],
                      entry.address[ 4 ],
                      entry.address[ 5 ],
                      entry.vendor[ 0 ] ? entry.vendor : "Unknown",
                      distance );
            display_draw_text( UI_LAYOUT_LEFT_X,
                               43 + (int)( row - start ) * 17,
                               line,
                               row == surveillance_selection ? theme->selected : theme->normal,
                               1U );
        }
        ++row;
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
