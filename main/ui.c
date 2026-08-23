/**
    ui.c : RFScout situational-awareness user interface
    Description: Displays live device counts, surveillance alerts, and top brands.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui.h"

#include "display.h"
#include "observation_store.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define UI_REFRESH_INTERVAL_US 500000LL
#define UI_MAX_BRANDS 9

#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_GREEN  0x07E0
#define COLOR_YELLOW 0xFFE0
#define COLOR_RED    0xF800
#define COLOR_CYAN   0x07FF
#define COLOR_GRAY   0x8410

typedef struct
{
    char name[ RFSCOUT_VENDOR_NAME_MAX ];
    uint16_t count;
} brand_count_t;

static const char *TAG = "ui";
static int64_t last_refresh_us;

/** @brief Insert one vendor into the fixed-size descending brand ranking. */
static void add_brand( brand_count_t *a_brands, size_t a_capacity, const char *a_name )
{
    if( ( NULL == a_brands ) || ( 0U == a_capacity ) || ( NULL == a_name ) || ( '\0' == a_name[ 0 ] ) )
    {
        return;
    }

    size_t slot = a_capacity;
    for( size_t i = 0; i < a_capacity; ++i )
    {
        if( ( 0U != a_brands[ i ].count ) && ( 0 == strcmp( a_brands[ i ].name, a_name ) ) )
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
        strlcpy( a_brands[ slot ].name, a_name, sizeof( a_brands[ slot ].name ) );
    }
    ++a_brands[ slot ].count;

    while( ( slot > 0U ) && ( a_brands[ slot ].count > a_brands[ slot - 1U ].count ) )
    {
        const brand_count_t temporary = a_brands[ slot - 1U ];
        a_brands[ slot - 1U ] = a_brands[ slot ];
        a_brands[ slot ] = temporary;
        --slot;
    }
}

/** @brief Render one complete RFScout summary frame. */
static void render_summary( void )
{
    static rf_device_t snapshot[ RFSCOUT_DEVICE_CAPACITY ];
    brand_count_t brands[ UI_MAX_BRANDS ] = { 0 };
    const size_t device_count = observation_store_snapshot( snapshot, RFSCOUT_DEVICE_CAPACITY );
    size_t wifi_count = 0;
    size_t ble_count = 0;
    size_t alert_count = 0;

    for( size_t i = 0; i < device_count; ++i )
    {
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
        {
            ++wifi_count;
        }
        if( 0U != ( snapshot[ i ].source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) )
        {
            ++ble_count;
        }
        if( SURVEILLANCE_NONE != snapshot[ i ].surveillance )
        {
            ++alert_count;
        }
        add_brand( brands, UI_MAX_BRANDS, snapshot[ i ].vendor );
    }

    display_clear( COLOR_BLACK );
    display_draw_text( 6, 5, "RFSCOUT", COLOR_GREEN, 2 );
    display_draw_text( 104, 8, "SITUATIONAL AWARENESS", COLOR_GRAY, 1 );

    char line[ 64 ];
    snprintf( line, sizeof( line ), "DEV %u   WIFI %u   BLE %u",
              (unsigned)device_count, (unsigned)wifi_count, (unsigned)ble_count );
    display_draw_text( 6, 25, line, COLOR_WHITE, 1 );

    snprintf( line, sizeof( line ), "SURVEILLANCE %u", (unsigned)alert_count );
    display_draw_text( 210, 25, line, ( 0U == alert_count ) ? COLOR_CYAN : COLOR_RED, 1 );
    display_fill_rect( 5, 36, 310, 1, COLOR_GRAY );

    display_draw_text( 6, 43, "TOP NEARBY BRANDS", COLOR_YELLOW, 1 );
    int y = 57;
    for( size_t i = 0; i < UI_MAX_BRANDS; ++i )
    {
        if( 0U == brands[ i ].count )
        {
            break;
        }
        snprintf( line, sizeof( line ), "%-27.27s %3u", brands[ i ].name, (unsigned)brands[ i ].count );
        display_draw_text( 6, y, line, ( i < 3U ) ? COLOR_GREEN : COLOR_WHITE, 1 );
        y += 12;
    }

    if( ESP_OK != display_present() )
    {
        ESP_LOGW( TAG, "LCD refresh failed" );
    }
}

esp_err_t ui_init( void )
{
    const esp_err_t result = display_init();
    if( ESP_OK != result )
    {
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
