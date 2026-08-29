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
#include "surveillance_history.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define UI_MAX_KNOWN_BRANDS 8
#define UI_REFRESH_INTERVAL_US 100000LL
#define UI_LIST_START_Y 43
#define UI_LIST_ROW_HEIGHT 12
#define UI_STATIC_DIVIDER_Y 139
#define UI_UNKNOWN_ROW_Y 146
#define UI_RANDOM_ROW_Y 158
#define UI_COLUMN_DIVIDER_X 198
#define UI_SURVEILLANCE_X 205
#define UI_SURVEILLANCE_LABEL_WIDTH 14
#define COLOR_BLACK ( ui_theme_get()->background )
#define COLOR_WHITE ( ui_theme_get()->text )
#define COLOR_ORANGE ( ui_theme_get()->accent )
#define COLOR_GRAY ( ui_theme_get()->divider )
#define COLOR_SUBDUED_TEAL ( ui_theme_get()->secondary )
/* 0xFFE0 is a legacy warning token translated through the theme resolver. Use the adjacent
 * literal RGB565 value so WiFi/BT alerts remain unmistakably yellow against RFScout orange. */
#define COLOR_ALERT_YELLOW 0xFFE1U

typedef struct
{
    char name[ RFSCOUT_VENDOR_NAME_MAX ];
    uint16_t count;
} brand_count_t;

static const surveillance_signature_id_t surveillance_rows[] = {
    SURVEILLANCE_SIGNATURE_FLOCK,
    SURVEILLANCE_SIGNATURE_RAVEN,
    SURVEILLANCE_SIGNATURE_AXON,
    SURVEILLANCE_SIGNATURE_RING,
    SURVEILLANCE_SIGNATURE_VERKADA,
    SURVEILLANCE_SIGNATURE_AXIS,
    SURVEILLANCE_SIGNATURE_HIKVISION,
    SURVEILLANCE_SIGNATURE_DAHUA,
    SURVEILLANCE_SIGNATURE_REMOTE_ID,
    SURVEILLANCE_SIGNATURE_META,
};

static const char *TAG = "ui";
static int64_t last_refresh_us;
static rf_device_t *snapshot;
static brand_count_t *brands;

static void add_brand( const char *a_name )
{
    if( ( NULL == brands ) || ( NULL == a_name ) || ( '\0' == a_name[ 0 ] ) )
    {
        return;
    }

    size_t slot = RFSCOUT_DEVICE_CAPACITY;
    for( size_t i = 0; i < RFSCOUT_DEVICE_CAPACITY; ++i )
    {
        if( ( 0U != brands[ i ].count ) && ( 0 == strcmp( brands[ i ].name, a_name ) ) )
        {
            slot = i;
            break;
        }
        if( ( RFSCOUT_DEVICE_CAPACITY == slot ) && ( 0U == brands[ i ].count ) )
        {
            slot = i;
        }
    }

    if( RFSCOUT_DEVICE_CAPACITY == slot )
    {
        return;
    }
    if( 0U == brands[ slot ].count )
    {
        strlcpy( brands[ slot ].name, a_name, sizeof( brands[ slot ].name ) );
    }
    ++brands[ slot ].count;

    while( ( slot > 0U ) && ( brands[ slot ].count > brands[ slot - 1U ].count ) )
    {
        const brand_count_t temporary = brands[ slot - 1U ];
        brands[ slot - 1U ] = brands[ slot ];
        brands[ slot ] = temporary;
        --slot;
    }
}

static void draw_static_rows( size_t a_unknown_count, size_t a_random_count )
{
    display_fill_rect( 5, UI_STATIC_DIVIDER_Y, UI_COLUMN_DIVIDER_X - 10, 1, COLOR_GRAY );

    char line[ 64 ];
    snprintf( line, sizeof( line ), "%-27.27s %3u", "UNKNOWN", (unsigned)a_unknown_count );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_UNKNOWN_ROW_Y, line, COLOR_ORANGE, 1 );

    snprintf( line, sizeof( line ), "%-27.27s %3u", "RANDOM", (unsigned)a_random_count );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_RANDOM_ROW_Y, line, COLOR_ORANGE, 1 );
}

static void draw_surveillance_rows( const uint16_t *a_signature_counts )
{
    int y = UI_LIST_START_Y;
    for( size_t row = 0; row < sizeof( surveillance_rows ) / sizeof( surveillance_rows[ 0 ] );
         ++row )
    {
        const surveillance_signature_id_t signature = surveillance_rows[ row ];
        const uint16_t count = a_signature_counts[ signature ];
        const uint16_t color = ( 0U == count ) ? COLOR_ORANGE : COLOR_ALERT_YELLOW;
        char line[ 32 ];
        snprintf( line,
                  sizeof( line ),
                  "%-*.*s %3u",
                  UI_SURVEILLANCE_LABEL_WIDTH,
                  UI_SURVEILLANCE_LABEL_WIDTH,
                  surveillance_signature_label( signature ),
                  (unsigned)count );
        display_draw_text( UI_SURVEILLANCE_X, y, line, color, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }
}

static void render_summary( void )
{
    if( ( NULL == snapshot ) || ( NULL == brands ) )
    {
        return;
    }

    memset( brands, 0, RFSCOUT_DEVICE_CAPACITY * sizeof( *brands ) );
    uint16_t signature_counts[ SURVEILLANCE_SIGNATURE_COUNT ] = { 0 };
    const size_t device_count = observation_store_snapshot( snapshot, RFSCOUT_DEVICE_CAPACITY );
    const size_t surveillance_count =
        surveillance_history_signature_counts( signature_counts, SURVEILLANCE_SIGNATURE_COUNT );
    size_t wifi_count = 0;
    size_t ble_count = 0;
    size_t unknown_count = 0;
    size_t random_count = 0;

    for( size_t i = 0; i < device_count; ++i )
    {
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
            ++wifi_count;
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) )
            ++ble_count;

        if( rf_device_address_is_randomized( &snapshot[ i ] ) )
            ++random_count;
        else if( '\0' == snapshot[ i ].vendor[ 0 ] )
            ++unknown_count;
        else
            add_brand( snapshot[ i ].vendor );
    }

    size_t brand_count = 0;
    while( ( brand_count < UI_MAX_KNOWN_BRANDS ) && ( 0U != brands[ brand_count ].count ) )
        ++brand_count;

    display_clear( COLOR_BLACK );
    ui_chrome_draw( "WIFI / BT", COLOR_SUBDUED_TEAL );

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "DEV %u   WIFI %u   BLE %u",
              (unsigned)device_count,
              (unsigned)wifi_count,
              (unsigned)ble_count );
    display_draw_text( UI_LAYOUT_LEFT_X, 25, line, COLOR_WHITE, 1 );

    snprintf( line, sizeof( line ), "SURVEILLANCE %u", (unsigned)surveillance_count );
    display_draw_text( 210,
                       25,
                       line,
                       ( 0U == surveillance_count ) ? COLOR_GRAY : COLOR_ALERT_YELLOW,
                       1 );

    display_fill_rect( 5, 36, 310, 1, COLOR_GRAY );
    display_fill_rect( UI_COLUMN_DIVIDER_X, 36, 1, RFSCOUT_DISPLAY_HEIGHT - 36, COLOR_GRAY );

    int y = UI_LIST_START_Y;
    for( size_t i = 0; i < brand_count; ++i )
    {
        snprintf(
            line, sizeof( line ), "%-27.27s %3u", brands[ i ].name, (unsigned)brands[ i ].count );
        display_draw_text( UI_LAYOUT_LEFT_X, y, line, COLOR_ORANGE, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }

    draw_static_rows( unknown_count, random_count );
    draw_surveillance_rows( signature_counts );

    if( ESP_OK != display_present() )
    {
        ESP_LOGW( TAG, "LCD refresh failed" );
    }
}

esp_err_t ui_init( void )
{
    snapshot = heap_caps_calloc(
        RFSCOUT_DEVICE_CAPACITY, sizeof( *snapshot ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    brands = heap_caps_calloc(
        RFSCOUT_DEVICE_CAPACITY, sizeof( *brands ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( ( NULL == snapshot ) || ( NULL == brands ) )
    {
        heap_caps_free( snapshot );
        heap_caps_free( brands );
        snapshot = NULL;
        brands = NULL;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result = display_init();
    if( ESP_OK != result )
    {
        heap_caps_free( snapshot );
        heap_caps_free( brands );
        snapshot = NULL;
        brands = NULL;
        return result;
    }
    last_refresh_us = 0;
    render_summary();
    return ESP_OK;
}

void ui_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_refresh_us ) < UI_REFRESH_INTERVAL_US )
        return;
    last_refresh_us = now_us;
    render_summary();
}
