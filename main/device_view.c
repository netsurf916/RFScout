/**
    device_view.c : RFScout Device application implementation
    Description: Provides a global settings editor, application controls, timezone selection, and manual time setting.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "app_logging.h"
#include "app_manager.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "log_writer.h"
#include "observation_store.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_ITEM_BRIGHTNESS 0U
#define DEVICE_ITEM_CLOCK_FORMAT 1U
#define DEVICE_ITEM_TIMEZONE 2U
#define DEVICE_ITEM_YEAR 3U
#define DEVICE_ITEM_MONTH 4U
#define DEVICE_ITEM_DAY 5U
#define DEVICE_ITEM_HOUR 6U
#define DEVICE_ITEM_MINUTE 7U
#define DEVICE_ITEM_WIFI 8U
#define DEVICE_ITEM_CC1101 9U
#define DEVICE_ITEM_NRF24 10U
#define DEVICE_ITEM_AUDIO 11U
#define DEVICE_ITEM_THERMAL 12U
#define DEVICE_ITEM_LOGGING 13U
#define DEVICE_ITEM_SAVE 14U
#define DEVICE_ITEM_BACK 15U
#define DEVICE_ITEM_COUNT 16U
#define DEVICE_LOG_SHUTDOWN_TIMEOUT_MS 3000U

#define COLOR_BACKGROUND 0x0000U
#define COLOR_TEXT 0xBDF7U
#define COLOR_SELECTED 0xFFFFU
#define COLOR_DIM 0x7BEFU
#define COLOR_OK 0x07E0U
#define COLOR_ERROR 0xF800U

static unsigned selected_item;
static bool editing;
static struct tm edited_time;
static bool save_error;

/** @brief Emit a standard configuration-change record when logging is active. */
static void log_config_change( const char *a_key, const char *a_old, const char *a_new )
{
    char detail[ 160 ];
    snprintf( detail,
              sizeof( detail ),
              "%s old=%s new=%s",
              a_key,
              ( NULL != a_old ) ? a_old : "-",
              ( NULL != a_new ) ? a_new : "-" );
    (void)log_writer_app_record( "CONFIG", detail );
}

/** @brief Format an epoch for a time-change audit record. */
static void format_epoch( bool a_valid, time_t a_epoch, char *a_output, size_t a_capacity )
{
    if( !a_valid || !device_time_format_iso8601_utc( a_epoch, a_output, a_capacity ) )
    {
        snprintf( a_output, a_capacity, "unset" );
    }
}

/** @brief Refresh the editable local calendar fields from the authoritative clock. */
static void refresh_edited_time( void )
{
    time_t now;
    if( device_time_now( &now ) && ( NULL != localtime_r( &now, &edited_time ) ) )
    {
        return;
    }

    memset( &edited_time, 0, sizeof( edited_time ) );
    edited_time.tm_year = 126;
    edited_time.tm_mon = 0;
    edited_time.tm_mday = 1;
    edited_time.tm_isdst = -1;
}

/** @brief Return a compact boolean value for the settings UI and logs. */
static const char *on_off( bool a_value )
{
    return a_value ? "On" : "Off";
}

/** @brief Draw one selectable setting row. */
static void draw_row( int a_y, unsigned a_item, const char *a_label, const char *a_value )
{
    const uint16_t color = ( selected_item == a_item ) ? COLOR_SELECTED : COLOR_TEXT;
    if( selected_item == a_item )
    {
        display_draw_text( 6, a_y, editing ? ">*" : ">", color, 1U );
    }
    display_draw_text( 22, a_y, a_label, color, 1U );
    display_draw_text( 168, a_y, a_value, color, 1U );
}

/** @brief Render display/time settings. */
static void render_page_one( void )
{
    const device_config_t *config = device_config_get();
    char value[ 48 ];

    snprintf( value, sizeof( value ), "%u%%", (unsigned)config->brightness_percent );
    draw_row( 28, DEVICE_ITEM_BRIGHTNESS, "Brightness", value );
    draw_row( 43, DEVICE_ITEM_CLOCK_FORMAT, "Clock format", config->clock_24h ? "24h" : "12h" );

    const device_timezone_t *zone = device_config_timezone();
    draw_row( 58,
              DEVICE_ITEM_TIMEZONE,
              "Timezone",
              ( NULL != zone ) ? zone->label : config->timezone_selected );

    snprintf( value, sizeof( value ), "%04d", edited_time.tm_year + 1900 );
    draw_row( 73, DEVICE_ITEM_YEAR, "Year", value );
    snprintf( value, sizeof( value ), "%02d", edited_time.tm_mon + 1 );
    draw_row( 88, DEVICE_ITEM_MONTH, "Month", value );
    snprintf( value, sizeof( value ), "%02d", edited_time.tm_mday );
    draw_row( 103, DEVICE_ITEM_DAY, "Day", value );
    snprintf( value, sizeof( value ), "%02d", edited_time.tm_hour );
    draw_row( 118, DEVICE_ITEM_HOUR, "Hour", value );
    snprintf( value, sizeof( value ), "%02d", edited_time.tm_min );
    draw_row( 133, DEVICE_ITEM_MINUTE, "Minute", value );
}

/** @brief Render application/storage settings and time-source status. */
static void render_page_two( void )
{
    const device_config_t *config = device_config_get();
    draw_row( 28,
              DEVICE_ITEM_WIFI,
              "WiFi / BT",
              on_off( config->apps_enabled[ RFSCOUT_APP_WIFI_BT ] ) );
    draw_row( 43,
              DEVICE_ITEM_CC1101,
              "CC1101",
              on_off( config->apps_enabled[ RFSCOUT_APP_CC1101 ] ) );
    draw_row( 58,
              DEVICE_ITEM_NRF24,
              "nRF24",
              on_off( config->apps_enabled[ RFSCOUT_APP_NRF24 ] ) );
    draw_row( 73,
              DEVICE_ITEM_AUDIO,
              "Audio",
              on_off( config->apps_enabled[ RFSCOUT_APP_AUDIO ] ) );
    draw_row( 88,
              DEVICE_ITEM_THERMAL,
              "Thermal",
              on_off( config->apps_enabled[ RFSCOUT_APP_THERMAL ] ) );
    draw_row( 103, DEVICE_ITEM_LOGGING, "SD logging", on_off( config->logging_enabled ) );
    draw_row( 118,
              DEVICE_ITEM_SAVE,
              "Save config",
              storage_is_available() ? "SD" : "No SD" );
    draw_row( 133, DEVICE_ITEM_BACK, "Back", "" );

    char source[ 48 ];
    snprintf( source, sizeof( source ), "Time source: %s", device_time_source_label() );
    display_draw_text( 22, 148, source, COLOR_DIM, 1U );
}

void device_view_render( void )
{
    display_clear( COLOR_BACKGROUND );
    display_draw_text( 6, 4, "DEVICE", COLOR_DIM, 1U );
    if( selected_item <= DEVICE_ITEM_MINUTE )
    {
        render_page_one();
    }
    else
    {
        render_page_two();
    }

    if( save_error )
    {
        display_draw_text( 286, 148, "ERR", COLOR_ERROR, 1U );
    }
    else if( storage_is_available() )
    {
        display_draw_text( 292, 148, "SD", COLOR_OK, 1U );
    }
    (void)display_present();
}

void device_view_enter( void )
{
    selected_item = 0U;
    editing = false;
    save_error = false;
    refresh_edited_time();
    device_view_render();
}

/** @brief Move to the next/previous Device setting. */
static void move_selection( int a_delta )
{
    int item = (int)selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)DEVICE_ITEM_COUNT - 1;
    }
    if( item >= (int)DEVICE_ITEM_COUNT )
    {
        item = 0;
    }
    selected_item = (unsigned)item;
}

/** @brief Toggle one top-level application and audit the setting change. */
static void toggle_app( rfscout_app_id_t a_app, const char *a_key )
{
    const bool old_value = device_config_app_enabled( a_app );
    (void)device_config_set_app_enabled( a_app, !old_value );
    log_config_change( a_key, on_off( old_value ), on_off( !old_value ) );
}

/** @brief Rotate among configured timezone definitions and immediately apply the selected rule. */
static void adjust_timezone( int a_delta )
{
    const device_config_t *config = device_config_get();
    if( 0U == config->timezone_count )
    {
        return;
    }

    size_t current = 0U;
    for( size_t index = 0; index < config->timezone_count; ++index )
    {
        if( 0 == strcmp( config->timezones[ index ].name, config->timezone_selected ) )
        {
            current = index;
            break;
        }
    }
    const size_t next = ( a_delta > 0 ) ? ( current + 1U ) % config->timezone_count
                                        : ( current + config->timezone_count - 1U ) % config->timezone_count;

    char old_name[ RFSCOUT_TIMEZONE_NAME_MAX ];
    snprintf( old_name, sizeof( old_name ), "%s", config->timezone_selected );
    (void)device_config_set_timezone( config->timezones[ next ].name );
    (void)device_time_apply_timezone();
    log_config_change( "timezone.selected", old_name, config->timezones[ next ].name );
    refresh_edited_time();
}

/** @brief Adjust one manual calendar field without changing wall time yet. */
static void adjust_time_field( unsigned a_item, int a_delta )
{
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    switch( a_item )
    {
        case DEVICE_ITEM_YEAR:
            edited_time.tm_year += direction;
            if( edited_time.tm_year < 120 )
            {
                edited_time.tm_year = 120;
            }
            if( edited_time.tm_year > 200 )
            {
                edited_time.tm_year = 200;
            }
            break;

        case DEVICE_ITEM_MONTH:
            edited_time.tm_mon = ( edited_time.tm_mon + direction + 12 ) % 12;
            break;

        case DEVICE_ITEM_DAY:
            edited_time.tm_mday += direction;
            if( edited_time.tm_mday < 1 )
            {
                edited_time.tm_mday = 31;
            }
            if( edited_time.tm_mday > 31 )
            {
                edited_time.tm_mday = 1;
            }
            break;

        case DEVICE_ITEM_HOUR:
            edited_time.tm_hour = ( edited_time.tm_hour + direction + 24 ) % 24;
            break;

        case DEVICE_ITEM_MINUTE:
            edited_time.tm_min = ( edited_time.tm_min + direction + 60 ) % 60;
            break;

        default:
            break;
    }
}

/** @brief Commit manually edited local date/time to the authoritative system wall clock. */
static void commit_manual_time( void )
{
    time_t old_epoch = 0;
    const bool had_old = device_time_now( &old_epoch );

    edited_time.tm_sec = 0;
    edited_time.tm_isdst = -1;
    const time_t new_epoch = mktime( &edited_time );
    if( new_epoch < (time_t)1577836800 )
    {
        save_error = true;
        return;
    }
    if( ESP_OK != device_time_set_epoch( new_epoch, RFSCOUT_TIME_SOURCE_MANUAL, "manual" ) )
    {
        save_error = true;
        return;
    }

    char old_text[ 32 ];
    char new_text[ 32 ];
    format_epoch( had_old, old_epoch, old_text, sizeof( old_text ) );
    format_epoch( true, new_epoch, new_text, sizeof( new_text ) );
    char detail[ 128 ];
    snprintf( detail, sizeof( detail ), "old=%s new=%s source=manual", old_text, new_text );
    (void)log_writer_app_record( "TIME", detail );
    save_error = false;
}

/** @brief Apply the logging preference immediately when possible. */
static void toggle_logging( void )
{
    const bool old_value = device_config_get()->logging_enabled;
    if( old_value )
    {
        log_config_change( "logging.enabled", "On", "Off" );
        if( !log_writer_shutdown( DEVICE_LOG_SHUTDOWN_TIMEOUT_MS ) )
        {
            save_error = true;
            return;
        }
        (void)device_config_set_logging_enabled( false );
        save_error = false;
        return;
    }

    if( !storage_is_available() || ( ESP_OK != log_writer_init() ) )
    {
        save_error = true;
        return;
    }
    (void)device_config_set_logging_enabled( true );
    observation_store_set_event_callback( log_writer_store_event, NULL );
    app_logging_init();
    log_config_change( "logging.enabled", "Off", "On" );
    save_error = false;
}

/** @brief Adjust the currently edited setting. */
static void adjust_selected( int a_delta )
{
    const device_config_t *config = device_config_get();
    if( DEVICE_ITEM_BRIGHTNESS == selected_item )
    {
        const uint8_t old_value = config->brightness_percent;
        int value = (int)old_value + ( ( a_delta > 0 ) ? 10 : -10 );
        if( value < 10 )
        {
            value = 10;
        }
        if( value > 100 )
        {
            value = 100;
        }
        if( value != old_value )
        {
            (void)device_config_set_brightness( (uint8_t)value );
            display_set_brightness( (uint8_t)value );
            char old_text[ 8 ];
            char new_text[ 8 ];
            snprintf( old_text, sizeof( old_text ), "%u", (unsigned)old_value );
            snprintf( new_text, sizeof( new_text ), "%u", (unsigned)value );
            log_config_change( "display.brightness", old_text, new_text );
        }
    }
    else if( DEVICE_ITEM_CLOCK_FORMAT == selected_item )
    {
        const bool old_value = config->clock_24h;
        (void)device_config_set_clock_24h( !old_value );
        log_config_change( "display.clock_24h", on_off( old_value ), on_off( !old_value ) );
    }
    else if( DEVICE_ITEM_TIMEZONE == selected_item )
    {
        adjust_timezone( a_delta );
    }
    else if( ( selected_item >= DEVICE_ITEM_YEAR ) && ( selected_item <= DEVICE_ITEM_MINUTE ) )
    {
        adjust_time_field( selected_item, a_delta );
    }
    else if( DEVICE_ITEM_WIFI == selected_item )
    {
        toggle_app( RFSCOUT_APP_WIFI_BT, "apps.wifi_bt" );
    }
    else if( DEVICE_ITEM_CC1101 == selected_item )
    {
        toggle_app( RFSCOUT_APP_CC1101, "apps.cc1101" );
    }
    else if( DEVICE_ITEM_NRF24 == selected_item )
    {
        toggle_app( RFSCOUT_APP_NRF24, "apps.nrf24" );
    }
    else if( DEVICE_ITEM_AUDIO == selected_item )
    {
        toggle_app( RFSCOUT_APP_AUDIO, "apps.audio" );
    }
    else if( DEVICE_ITEM_THERMAL == selected_item )
    {
        toggle_app( RFSCOUT_APP_THERMAL, "apps.thermal" );
    }
    else if( DEVICE_ITEM_LOGGING == selected_item )
    {
        toggle_logging();
    }
}

bool device_view_input( bool a_pressed, int a_delta )
{
    if( a_pressed )
    {
        if( ( DEVICE_ITEM_BACK == selected_item ) && !editing )
        {
            return true;
        }
        if( DEVICE_ITEM_SAVE == selected_item )
        {
            save_error = ( ESP_OK != device_config_save( false, 0, NULL ) );
            device_view_render();
            return false;
        }

        if( editing && ( selected_item >= DEVICE_ITEM_YEAR ) &&
            ( selected_item <= DEVICE_ITEM_MINUTE ) )
        {
            commit_manual_time();
        }
        editing = !editing;
        device_view_render();
        return false;
    }

    if( 0 == a_delta )
    {
        return false;
    }
    if( editing )
    {
        adjust_selected( a_delta );
    }
    else
    {
        move_selection( a_delta );
    }
    device_view_render();
    return false;
}
