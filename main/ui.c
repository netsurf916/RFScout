/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "battery_indicator.h"
#include "display.h"
#include "observation_store.h"
#include "power.h"
#include "surveillance_history.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define UI_REFRESH_INTERVAL_US 100000LL
#define UI_MAX_KNOWN_BRANDS 9
#define UI_BRAND_ATTENTION_THRESHOLD 5U
#define UI_MAX_ATTENTION_ROWS 3U
#define UI_LIST_START_Y 43
#define UI_LIST_ROW_HEIGHT 12
#define UI_UNKNOWN_DIVIDER_Y 151
#define UI_UNKNOWN_ROW_Y 158
#define UI_COLUMN_DIVIDER_X 198
#define UI_SURVEILLANCE_X 205
#define COLOR_BLACK  0x0000U
#define COLOR_WHITE  0xFFFFU
#define COLOR_GREEN  0x07E0U
#define COLOR_YELLOW 0xFFE0U
#define COLOR_RED    0xF800U
#define COLOR_CYAN   0x07FFU
#define COLOR_GRAY   0x8410U

typedef struct { char name[ RFSCOUT_VENDOR_NAME_MAX ]; uint16_t count; } brand_count_t;
static const char *TAG = "ui";
static int64_t last_refresh_us;
static rf_device_t *snapshot;

static const char *surveillance_label( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK: return "FLOCK/ALPR";
        case SURVEILLANCE_BODY_CAMERA: return "BODY CAM";
        case SURVEILLANCE_DRONE: return "DRONE";
        case SURVEILLANCE_CAMERA: return "CAMERA";
        case SURVEILLANCE_SMART_GLASSES: return "GLASSES";
        case SURVEILLANCE_OTHER: return "OTHER";
        default: return "";
    }
}

static void add_brand( brand_count_t *a_brands, size_t a_capacity, const char *a_name )
{
    if( ( NULL == a_brands ) || ( 0U == a_capacity ) || ( NULL == a_name ) || ( '\0' == a_name[ 0 ] ) ) return;
    size_t slot = a_capacity;
    for( size_t i = 0; i < a_capacity; ++i )
    {
        if( ( 0U != a_brands[ i ].count ) && ( 0 == strcmp( a_brands[ i ].name, a_name ) ) ) { slot = i; break; }
        if( ( a_capacity == slot ) && ( 0U == a_brands[ i ].count ) ) slot = i;
    }
    if( a_capacity == slot ) return;
    if( 0U == a_brands[ slot ].count ) strlcpy( a_brands[ slot ].name, a_name, sizeof( a_brands[ slot ].name ) );
    ++a_brands[ slot ].count;
    while( ( slot > 0U ) && ( a_brands[ slot ].count > a_brands[ slot - 1U ].count ) )
    {
        const brand_count_t temporary = a_brands[ slot - 1U ];
        a_brands[ slot - 1U ] = a_brands[ slot ];
        a_brands[ slot ] = temporary;
        --slot;
    }
}

static void draw_unknown_row( size_t a_unknown_count )
{
    display_fill_rect( 5, UI_UNKNOWN_DIVIDER_Y, UI_COLUMN_DIVIDER_X - 10, 1, COLOR_GRAY );
    char line[ 64 ];
    snprintf( line, sizeof( line ), "%-27.27s %3u", "UNKNOWN", (unsigned)a_unknown_count );
    display_draw_text( 6, UI_UNKNOWN_ROW_Y, line, COLOR_CYAN, 1 );
}

static void draw_power_overlay( void )
{
    power_status_t power_status;
    power_get_status( &power_status );
    if( POWER_HOLD_IDLE == power_status.state ) return;

    display_fill_rect( 42, 50, 236, 70, COLOR_BLACK );
    display_fill_rect( 42, 50, 236, 2, COLOR_RED );
    display_fill_rect( 42, 118, 236, 2, COLOR_RED );
    display_fill_rect( 42, 50, 2, 70, COLOR_RED );
    display_fill_rect( 276, 50, 2, 70, COLOR_RED );

    if( POWER_HOLD_SHUTDOWN == power_status.state )
    {
        display_draw_text( 91, 77, "POWERING OFF...", COLOR_RED, 2 );
        return;
    }

    display_draw_text( 69, 63, "HOLD TO POWER OFF", COLOR_YELLOW, 2 );
    char countdown[ 4 ];
    snprintf( countdown, sizeof( countdown ), "%u", (unsigned)power_status.seconds_remaining );
    display_draw_text( 150, 88, countdown, COLOR_RED, 3 );
}

static void render_summary( void )
{
    if( NULL == snapshot ) return;
    brand_count_t brands[ UI_MAX_KNOWN_BRANDS ] = { 0 };
    uint16_t surveillance_counts[ RFSCOUT_SURVEILLANCE_CLASS_COUNT ] = { 0 };
    const size_t device_count = observation_store_snapshot( snapshot, RFSCOUT_DEVICE_CAPACITY );
    const size_t surveillance_count = surveillance_history_counts( surveillance_counts, RFSCOUT_SURVEILLANCE_CLASS_COUNT );
    size_t wifi_count = 0, ble_count = 0, unknown_count = 0;

    for( size_t i = 0; i < device_count; ++i )
    {
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) ++wifi_count;
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) ++ble_count;
        if( '\0' == snapshot[ i ].vendor[ 0 ] ) ++unknown_count;
        else add_brand( brands, UI_MAX_KNOWN_BRANDS, snapshot[ i ].vendor );
    }

    size_t brand_count = 0;
    while( ( brand_count < UI_MAX_KNOWN_BRANDS ) && ( 0U != brands[ brand_count ].count ) ) ++brand_count;

    display_clear( COLOR_BLACK );
    display_draw_text( 6, 5, "RFSCOUT", COLOR_GREEN, 2 );
    battery_indicator_draw();

    char line[ 64 ];
    snprintf( line, sizeof( line ), "DEV %u   WIFI %u   BLE %u", (unsigned)device_count, (unsigned)wifi_count, (unsigned)ble_count );
    display_draw_text( 6, 25, line, COLOR_WHITE, 1 );
    snprintf( line, sizeof( line ), "SURVEILLANCE %u", (unsigned)surveillance_count );
    display_draw_text( 210, 25, line, ( 0U == surveillance_count ) ? COLOR_CYAN : COLOR_RED, 1 );
    display_fill_rect( 5, 36, 310, 1, COLOR_GRAY );
    display_fill_rect( UI_COLUMN_DIVIDER_X, 36, 1, RFSCOUT_DISPLAY_HEIGHT - 36, COLOR_GRAY );

    int y = UI_LIST_START_Y;
    size_t attention_rows = 0;
    for( size_t i = 0; i < brand_count; ++i )
    {
        snprintf( line, sizeof( line ), "%-27.27s %3u", brands[ i ].name, (unsigned)brands[ i ].count );
        const bool highlight = ( brands[ i ].count > UI_BRAND_ATTENTION_THRESHOLD ) && ( attention_rows < UI_MAX_ATTENTION_ROWS );
        if( highlight ) ++attention_rows;
        display_draw_text( 6, y, line, highlight ? COLOR_YELLOW : COLOR_CYAN, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }
    draw_unknown_row( unknown_count );

    y = UI_LIST_START_Y;
    for( size_t classification = (size_t)SURVEILLANCE_FLOCK; classification <= (size_t)SURVEILLANCE_OTHER; ++classification )
    {
        if( 0U == surveillance_counts[ classification ] ) continue;
        snprintf( line, sizeof( line ), "%-10.10s %3u", surveillance_label( (surveillance_class_t)classification ), (unsigned)surveillance_counts[ classification ] );
        display_draw_text( UI_SURVEILLANCE_X, y, line, COLOR_RED, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }

    draw_power_overlay();
    if( ESP_OK != display_present() ) ESP_LOGW( TAG, "LCD refresh failed" );
}

esp_err_t ui_init( void )
{
    snapshot = heap_caps_calloc( RFSCOUT_DEVICE_CAPACITY, sizeof( *snapshot ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == snapshot ) return ESP_ERR_NO_MEM;
    const esp_err_t result = display_init();
    if( ESP_OK != result )
    {
        heap_caps_free( snapshot );
        snapshot = NULL;
        return result;
    }
    last_refresh_us = 0;
    render_summary();
    return ESP_OK;
}

void ui_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_refresh_us ) < UI_REFRESH_INTERVAL_US ) return;
    last_refresh_us = now_us;
    render_summary();
}
