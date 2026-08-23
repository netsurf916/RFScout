/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "battery.h"
#include "display.h"
#include "observation_store.h"
#include "surveillance_history.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define UI_REFRESH_INTERVAL_US 500000LL
#define UI_MAX_KNOWN_BRANDS 9
#define UI_BRAND_ATTENTION_THRESHOLD 5U
#define UI_MAX_ATTENTION_ROWS 3U
#define UI_LIST_START_Y 43
#define UI_LIST_ROW_HEIGHT 12
#define UI_UNKNOWN_DIVIDER_Y 151
#define UI_UNKNOWN_ROW_Y 158
#define UI_COLUMN_DIVIDER_X 198
#define UI_SURVEILLANCE_X 205

#define UI_BATTERY_BODY_X 282
#define UI_BATTERY_BODY_Y 4
#define UI_BATTERY_BODY_WIDTH 28
#define UI_BATTERY_BODY_HEIGHT 14
#define UI_BATTERY_TERMINAL_WIDTH 3
#define UI_BATTERY_TERMINAL_HEIGHT 6
#define UI_BATTERY_TEXT_GAP 5

/* Canonical RGB565 values. The ST7789 driver consumes the little-endian
   uint16_t framebuffer directly; these constants are not byte-swapped. */
#define COLOR_BLACK  0x0000U
#define COLOR_WHITE  0xFFFFU
#define COLOR_GREEN  0x07E0U
#define COLOR_YELLOW 0xFFE0U
#define COLOR_RED    0xF800U
#define COLOR_CYAN   0x07FFU
#define COLOR_GRAY   0x8410U

typedef struct
{
    char name[ RFSCOUT_VENDOR_NAME_MAX ];
    uint16_t count;
} brand_count_t;

static const char *TAG = "ui";
static int64_t last_refresh_us;
static rf_device_t *snapshot;

/** @brief Return a compact label for the surveillance summary column. */
static const char *surveillance_label( surveillance_class_t a_classification )
{
    switch( a_classification )
    {
        case SURVEILLANCE_FLOCK:
            return "FLOCK/ALPR";
        case SURVEILLANCE_BODY_CAMERA:
            return "BODY CAM";
        case SURVEILLANCE_DRONE:
            return "DRONE";
        case SURVEILLANCE_CAMERA:
            return "CAMERA";
        case SURVEILLANCE_SMART_GLASSES:
            return "GLASSES";
        case SURVEILLANCE_OTHER:
            return "OTHER";
        default:
            return "";
    }
}

/** @brief Return the battery color associated with its current urgency. */
static uint16_t battery_color( const battery_status_t *a_status )
{
    if( ( NULL == a_status ) || !a_status->available )
    {
        return COLOR_GRAY;
    }
    if( ( BATTERY_CHARGE_COMPLETE == a_status->charge_state ) &&
        a_status->external_power )
    {
        return COLOR_GREEN;
    }
    if( a_status->percentage <= 10U )
    {
        return COLOR_RED;
    }
    if( a_status->percentage <= 20U )
    {
        return COLOR_YELLOW;
    }
    return COLOR_CYAN;
}

/** @brief Draw a compact lightning bolt inside the battery body. */
static void draw_charging_bolt( int a_x, int a_y, uint16_t a_color )
{
    display_fill_rect( a_x + 3, a_y,     2, 3, a_color );
    display_fill_rect( a_x + 1, a_y + 2, 4, 2, a_color );
    display_fill_rect( a_x + 2, a_y + 4, 2, 2, a_color );
    display_fill_rect( a_x,     a_y + 5, 3, 2, a_color );
    display_fill_rect( a_x + 1, a_y + 7, 2, 2, a_color );
}

/** @brief Draw the battery percentage, outline, proportional fill, and charge state. */
static void draw_battery_indicator( void )
{
    battery_status_t status = { 0 };
    (void)battery_get_status( &status );

    const uint16_t color = battery_color( &status );
    char percentage[ 6 ];
    if( status.available )
    {
        snprintf( percentage, sizeof( percentage ), "%u%%",
                  (unsigned)status.percentage );
    }
    else
    {
        strlcpy( percentage, "--%", sizeof( percentage ) );
    }

    const int text_width = (int)strlen( percentage ) * 6;
    const int text_x = UI_BATTERY_BODY_X - UI_BATTERY_TEXT_GAP - text_width;
    display_draw_text( text_x, 7, percentage, color, 1 );

    display_fill_rect( UI_BATTERY_BODY_X,
                       UI_BATTERY_BODY_Y,
                       UI_BATTERY_BODY_WIDTH,
                       1,
                       color );
    display_fill_rect( UI_BATTERY_BODY_X,
                       UI_BATTERY_BODY_Y + UI_BATTERY_BODY_HEIGHT - 1,
                       UI_BATTERY_BODY_WIDTH,
                       1,
                       color );
    display_fill_rect( UI_BATTERY_BODY_X,
                       UI_BATTERY_BODY_Y,
                       1,
                       UI_BATTERY_BODY_HEIGHT,
                       color );
    display_fill_rect( UI_BATTERY_BODY_X + UI_BATTERY_BODY_WIDTH - 1,
                       UI_BATTERY_BODY_Y,
                       1,
                       UI_BATTERY_BODY_HEIGHT,
                       color );

    const int terminal_y =
        UI_BATTERY_BODY_Y +
        ( UI_BATTERY_BODY_HEIGHT - UI_BATTERY_TERMINAL_HEIGHT ) / 2;
    display_fill_rect( UI_BATTERY_BODY_X + UI_BATTERY_BODY_WIDTH,
                       terminal_y,
                       UI_BATTERY_TERMINAL_WIDTH,
                       UI_BATTERY_TERMINAL_HEIGHT,
                       color );

    if( status.available )
    {
        const int inner_x = UI_BATTERY_BODY_X + 2;
        const int inner_y = UI_BATTERY_BODY_Y + 2;
        const int inner_width = UI_BATTERY_BODY_WIDTH - 4;
        const int inner_height = UI_BATTERY_BODY_HEIGHT - 4;
        int fill_width =
            ( (int)status.percentage * inner_width + 99 ) / 100;
        if( fill_width > inner_width )
        {
            fill_width = inner_width;
        }
        if( fill_width > 0 )
        {
            display_fill_rect( inner_x,
                               inner_y,
                               fill_width,
                               inner_height,
                               color );
        }
    }

    if( BATTERY_CHARGE_CHARGING == status.charge_state )
    {
        draw_charging_bolt( UI_BATTERY_BODY_X + 10,
                            UI_BATTERY_BODY_Y + 2,
                            COLOR_WHITE );
    }
}

/** @brief Insert one vendor into the fixed-size descending brand ranking. */
static void add_brand( brand_count_t *a_brands, size_t a_capacity,
                       const char *a_name )
{
    if( ( NULL == a_brands ) || ( 0U == a_capacity ) ||
        ( NULL == a_name ) || ( '\0' == a_name[ 0 ] ) )
    {
        return;
    }

    size_t slot = a_capacity;
    for( size_t i = 0; i < a_capacity; ++i )
    {
        if( ( 0U != a_brands[ i ].count ) &&
            ( 0 == strcmp( a_brands[ i ].name, a_name ) ) )
        {
            slot = i;
            break;
        }
        if( ( a_capacity == slot ) && ( 0U == a_brands[ i ].count ) )
        {
            slot = i;
        }
    }
    if( a_capacity == slot )
    {
        return;
    }

    if( 0U == a_brands[ slot ].count )
    {
        strlcpy( a_brands[ slot ].name, a_name,
                 sizeof( a_brands[ slot ].name ) );
    }
    ++a_brands[ slot ].count;

    while( ( slot > 0U ) &&
           ( a_brands[ slot ].count > a_brands[ slot - 1U ].count ) )
    {
        const brand_count_t temporary = a_brands[ slot - 1U ];
        a_brands[ slot - 1U ] = a_brands[ slot ];
        a_brands[ slot ] = temporary;
        --slot;
    }
}

/** @brief Draw the fixed unresolved-device row beneath the known brand ranking. */
static void draw_unknown_row( size_t a_unknown_count )
{
    display_fill_rect( 5,
                       UI_UNKNOWN_DIVIDER_Y,
                       UI_COLUMN_DIVIDER_X - 10,
                       1,
                       COLOR_GRAY );

    char line[ 64 ];
    snprintf( line, sizeof( line ), "%-27.27s %3u",
              "UNKNOWN", (unsigned)a_unknown_count );
    display_draw_text( 6, UI_UNKNOWN_ROW_Y, line, COLOR_CYAN, 1 );
}

/** @brief Render one complete RFScout summary frame. */
static void render_summary( void )
{
    if( NULL == snapshot )
    {
        return;
    }

    brand_count_t brands[ UI_MAX_KNOWN_BRANDS ] = { 0 };
    uint16_t surveillance_counts[ RFSCOUT_SURVEILLANCE_CLASS_COUNT ] = { 0 };
    const size_t device_count =
        observation_store_snapshot( snapshot, RFSCOUT_DEVICE_CAPACITY );
    const size_t surveillance_count =
        surveillance_history_counts( surveillance_counts,
                                     RFSCOUT_SURVEILLANCE_CLASS_COUNT );
    size_t wifi_count = 0;
    size_t ble_count = 0;
    size_t unknown_count = 0;

    for( size_t i = 0; i < device_count; ++i )
    {
        if( 0U != ( snapshot[ i ].source_mask &
                    RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
        {
            ++wifi_count;
        }
        if( 0U != ( snapshot[ i ].source_mask &
                    RF_SOURCE_BIT( RF_SOURCE_BLE ) ) )
        {
            ++ble_count;
        }

        if( '\0' == snapshot[ i ].vendor[ 0 ] )
        {
            ++unknown_count;
        }
        else
        {
            add_brand( brands,
                       UI_MAX_KNOWN_BRANDS,
                       snapshot[ i ].vendor );
        }
    }

    size_t brand_count = 0;
    while( ( brand_count < UI_MAX_KNOWN_BRANDS ) &&
           ( 0U != brands[ brand_count ].count ) )
    {
        ++brand_count;
    }

    display_clear( COLOR_BLACK );
    display_draw_text( 6, 5, "RFSCOUT", COLOR_GREEN, 2 );
    draw_battery_indicator();

    /* The title-row space between RFSCOUT and the battery remains reserved for
       compact radio, storage, alert, and mute status symbols. */

    char line[ 64 ];
    snprintf( line, sizeof( line ), "DEV %u   WIFI %u   BLE %u",
              (unsigned)device_count, (unsigned)wifi_count,
              (unsigned)ble_count );
    display_draw_text( 6, 25, line, COLOR_WHITE, 1 );

    snprintf( line, sizeof( line ), "SURVEILLANCE %u",
              (unsigned)surveillance_count );
    display_draw_text( 210, 25, line,
                       ( 0U == surveillance_count ) ? COLOR_CYAN : COLOR_RED,
                       1 );
    display_fill_rect( 5, 36, 310, 1, COLOR_GRAY );
    display_fill_rect( UI_COLUMN_DIVIDER_X, 36, 1,
                       RFSCOUT_DISPLAY_HEIGHT - 36, COLOR_GRAY );

    int y = UI_LIST_START_Y;
    size_t attention_rows = 0;
    for( size_t i = 0; i < brand_count; ++i )
    {
        snprintf( line, sizeof( line ), "%-27.27s %3u",
                  brands[ i ].name, (unsigned)brands[ i ].count );

        const bool highlight =
            ( brands[ i ].count > UI_BRAND_ATTENTION_THRESHOLD ) &&
            ( attention_rows < UI_MAX_ATTENTION_ROWS );
        if( highlight )
        {
            ++attention_rows;
        }

        display_draw_text( 6, y, line,
                           highlight ? COLOR_YELLOW : COLOR_CYAN, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }
    draw_unknown_row( unknown_count );

    y = UI_LIST_START_Y;
    for( size_t classification = (size_t)SURVEILLANCE_FLOCK;
         classification <= (size_t)SURVEILLANCE_OTHER;
         ++classification )
    {
        if( 0U == surveillance_counts[ classification ] )
        {
            continue;
        }

        snprintf( line, sizeof( line ), "%-10.10s %3u",
                  surveillance_label( (surveillance_class_t)classification ),
                  (unsigned)surveillance_counts[ classification ] );
        display_draw_text( UI_SURVEILLANCE_X, y, line, COLOR_RED, 1 );
        y += UI_LIST_ROW_HEIGHT;
    }

    if( ESP_OK != display_present() )
    {
        ESP_LOGW( TAG, "LCD refresh failed" );
    }
}

esp_err_t ui_init( void )
{
    snapshot = heap_caps_calloc( RFSCOUT_DEVICE_CAPACITY,
                                 sizeof( *snapshot ),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == snapshot )
    {
        return ESP_ERR_NO_MEM;
    }

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
    if( ( now_us - last_refresh_us ) < UI_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_refresh_us = now_us;
    render_summary();
}
