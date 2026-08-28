/**
    device_view.c : RFScout Device application implementation
    Description: Provides global settings, notification mappings, timezone selection, and manual time setting.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "alert.h"
#include "app_logging.h"
#include "app_manager.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "log_writer.h"
#include "morse.h"
#include "observation_store.h"
#include "storage.h"
#include "ui_chrome.h"
#include "ui_layout.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_ITEM_BACK 0U
#define DEVICE_ITEM_BRIGHTNESS 1U
#define DEVICE_ITEM_CLOCK_FORMAT 2U
#define DEVICE_ITEM_TIMEZONE 3U
#define DEVICE_ITEM_YEAR 4U
#define DEVICE_ITEM_MONTH 5U
#define DEVICE_ITEM_DAY 6U
#define DEVICE_ITEM_HOUR 7U
#define DEVICE_ITEM_MINUTE 8U
#define DEVICE_ITEM_WIFI 9U
#define DEVICE_ITEM_CC1101 10U
#define DEVICE_ITEM_NRF24 11U
#define DEVICE_ITEM_AUDIO 12U
#define DEVICE_ITEM_THERMAL 13U
#define DEVICE_ITEM_LOGGING 14U
#define DEVICE_ITEM_ALERT_MODE 15U
#define DEVICE_ITEM_ALERT_VOLUME 16U
#define DEVICE_ITEM_NOTIFICATION_FIRST 17U
#define DEVICE_ITEM_NOTIFICATION_LAST                                                              \
    ( DEVICE_ITEM_NOTIFICATION_FIRST + RFSCOUT_NOTIFICATION_TYPE_COUNT - 1U )
#define DEVICE_ITEM_SAVE ( DEVICE_ITEM_NOTIFICATION_LAST + 1U )
#define DEVICE_ITEM_COUNT ( DEVICE_ITEM_SAVE + 1U )
#define DEVICE_ROWS_PER_PAGE 6U
#define DEVICE_LOG_SHUTDOWN_TIMEOUT_MS 3000U
#define DEVICE_ALERT_VOLUME_STEP 10

#define COLOR_BACKGROUND 0x0000U
#define COLOR_TEXT 0xBDF7U
#define COLOR_SELECTED 0xFFFFU
#define COLOR_DIM 0x7BEFU
#define COLOR_ERROR 0xF800U
#define COLOR_ACCENT 0xFFE0U

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

/**
 * @brief Return the notification category represented by one Device item.
 * @note Device notification rows intentionally correspond 1:1 with the enum/descriptor table in
 *       alert.h and alert.c. Do not add a second alert-item list here.
 */
static rfscout_notification_type_t notification_for_item( unsigned a_item )
{
    return (rfscout_notification_type_t)( a_item - DEVICE_ITEM_NOTIFICATION_FIRST );
}

/** @brief Return the next supported single-character Morse value. */
static char next_morse_character( char a_current, int a_direction )
{
    const char *characters = morse_supported_characters();
    const size_t count = strlen( characters );
    const char *current = strchr( characters, a_current );
    size_t index = ( NULL != current ) ? (size_t)( current - characters ) : 0U;

    if( a_direction > 0 )
    {
        index = ( index + 1U ) % count;
    }
    else
    {
        index = ( index + count - 1U ) % count;
    }
    return characters[ index ];
}

/** @brief Return true when one Device item can enter edit mode. */
static bool item_editable( unsigned a_item )
{
    return ( DEVICE_ITEM_BACK != a_item ) && ( DEVICE_ITEM_SAVE != a_item );
}

/** @brief Return the user-facing label for one Device settings item. */
static const char *item_label( unsigned a_item )
{
    switch( a_item )
    {
        case DEVICE_ITEM_BACK:
            return "Back";
        case DEVICE_ITEM_BRIGHTNESS:
            return "Brightness";
        case DEVICE_ITEM_CLOCK_FORMAT:
            return "Clock format";
        case DEVICE_ITEM_TIMEZONE:
            return "Timezone";
        case DEVICE_ITEM_YEAR:
            return "Year";
        case DEVICE_ITEM_MONTH:
            return "Month";
        case DEVICE_ITEM_DAY:
            return "Day";
        case DEVICE_ITEM_HOUR:
            return "Hour";
        case DEVICE_ITEM_MINUTE:
            return "Minute";
        case DEVICE_ITEM_WIFI:
            return "WiFi / BT";
        case DEVICE_ITEM_CC1101:
            return "CC1101";
        case DEVICE_ITEM_NRF24:
            return "nRF24";
        case DEVICE_ITEM_AUDIO:
            return "Audio";
        case DEVICE_ITEM_THERMAL:
            return "Thermal";
        case DEVICE_ITEM_LOGGING:
            return "SD logging";
        case DEVICE_ITEM_ALERT_MODE:
            return "Notifications";
        case DEVICE_ITEM_ALERT_VOLUME:
            return "Alert volume";
        case DEVICE_ITEM_SAVE:
            return "Save config";
        default:
            if( ( a_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
                ( a_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
            {
                return alert_notification_name( notification_for_item( a_item ) );
            }
            return "Unknown";
    }
}

/** @brief Format the current value of one Device settings item. */
static void item_value( unsigned a_item, char *a_value, size_t a_capacity )
{
    const device_config_t *config = device_config_get();
    a_value[ 0 ] = '\0';

    switch( a_item )
    {
        case DEVICE_ITEM_BRIGHTNESS:
            snprintf( a_value, a_capacity, "%u%%", (unsigned)config->brightness_percent );
            break;
        case DEVICE_ITEM_CLOCK_FORMAT:
            snprintf( a_value, a_capacity, "%s", config->clock_24h ? "24h" : "12h" );
            break;
        case DEVICE_ITEM_TIMEZONE:
        {
            const device_timezone_t *zone = device_config_timezone();
            snprintf( a_value,
                      a_capacity,
                      "%s",
                      ( NULL != zone ) ? zone->label : config->timezone_selected );
            break;
        }
        case DEVICE_ITEM_YEAR:
            snprintf( a_value, a_capacity, "%04d", edited_time.tm_year + 1900 );
            break;
        case DEVICE_ITEM_MONTH:
            snprintf( a_value, a_capacity, "%02d", edited_time.tm_mon + 1 );
            break;
        case DEVICE_ITEM_DAY:
            snprintf( a_value, a_capacity, "%02d", edited_time.tm_mday );
            break;
        case DEVICE_ITEM_HOUR:
            snprintf( a_value, a_capacity, "%02d", edited_time.tm_hour );
            break;
        case DEVICE_ITEM_MINUTE:
            snprintf( a_value, a_capacity, "%02d", edited_time.tm_min );
            break;
        case DEVICE_ITEM_WIFI:
            snprintf(
                a_value, a_capacity, "%s", on_off( config->apps_enabled[ RFSCOUT_APP_WIFI_BT ] ) );
            break;
        case DEVICE_ITEM_CC1101:
            snprintf(
                a_value, a_capacity, "%s", on_off( config->apps_enabled[ RFSCOUT_APP_CC1101 ] ) );
            break;
        case DEVICE_ITEM_NRF24:
            snprintf(
                a_value, a_capacity, "%s", on_off( config->apps_enabled[ RFSCOUT_APP_NRF24 ] ) );
            break;
        case DEVICE_ITEM_AUDIO:
            snprintf(
                a_value, a_capacity, "%s", on_off( config->apps_enabled[ RFSCOUT_APP_AUDIO ] ) );
            break;
        case DEVICE_ITEM_THERMAL:
            snprintf(
                a_value, a_capacity, "%s", on_off( config->apps_enabled[ RFSCOUT_APP_THERMAL ] ) );
            break;
        case DEVICE_ITEM_LOGGING:
            snprintf( a_value, a_capacity, "%s", on_off( config->logging_enabled ) );
            break;
        case DEVICE_ITEM_ALERT_MODE:
            snprintf( a_value, a_capacity, "%s", alert_mode_name( config->alert_mode ) );
            break;
        case DEVICE_ITEM_ALERT_VOLUME:
            snprintf( a_value, a_capacity, "%u%%", (unsigned)config->alert_volume_percent );
            break;
        case DEVICE_ITEM_SAVE:
            snprintf( a_value, a_capacity, "%s", storage_is_available() ? "SD" : "No SD" );
            break;
        default:
            if( ( a_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
                ( a_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
            {
                const rfscout_notification_type_t type = notification_for_item( a_item );
                const char character = device_config_notification_character( type );
                const char *pattern = morse_pattern( character );
                snprintf(
                    a_value, a_capacity, "%c %s", character, ( NULL != pattern ) ? pattern : "?" );
            }
            break;
    }
}

/** @brief Draw one selectable setting row. */
static void draw_row( int a_y, unsigned a_item )
{
    char value[ 48 ];
    item_value( a_item, value, sizeof( value ) );
    const bool active = selected_item == a_item;
    const uint16_t color = active ? COLOR_SELECTED : COLOR_TEXT;

    if( active )
    {
        display_draw_text( UI_LAYOUT_LEFT_X,
                           a_y,
                           ( editing && item_editable( a_item ) ) ? ">*" : ">",
                           color,
                           1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, item_label( a_item ), color, 1U );
    if( '\0' != value[ 0 ] )
    {
        display_draw_text( 232, a_y, value, active ? COLOR_ACCENT : COLOR_DIM, 1U );
    }
}

void device_view_landing( void )
{
    const device_config_t *config = device_config_get();
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "DEVICE", COLOR_DIM );
    display_draw_text( UI_LAYOUT_LABEL_X, 46, "Global configuration", COLOR_TEXT, 1U );

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "Notifications: %s %u%%",
              alert_mode_name( config->alert_mode ),
              (unsigned)config->alert_volume_percent );
    display_draw_text( UI_LAYOUT_LABEL_X, 68, line, COLOR_TEXT, 1U );
    snprintf( line, sizeof( line ), "SD logging: %s", on_off( config->logging_enabled ) );
    display_draw_text( UI_LAYOUT_LABEL_X, 86, line, COLOR_TEXT, 1U );
    snprintf( line, sizeof( line ), "Time: %s", device_time_source_label() );
    display_draw_text( UI_LAYOUT_LABEL_X, 104, line, COLOR_DIM, 1U );

    display_draw_text( UI_LAYOUT_LABEL_X, 136, "CENTER: SETTINGS", COLOR_ACCENT, 1U );
    display_draw_text( UI_LAYOUT_LABEL_X, UI_LAYOUT_FOOTER_Y, "TURN: NEXT APP", COLOR_DIM, 1U );
    (void)display_present();
}

void device_view_render( void )
{
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "DEVICE SETTINGS", COLOR_DIM );

    const unsigned page_start = ( selected_item / DEVICE_ROWS_PER_PAGE ) * DEVICE_ROWS_PER_PAGE;
    for( unsigned row = 0U; row < DEVICE_ROWS_PER_PAGE; ++row )
    {
        const unsigned item = page_start + row;
        if( item >= DEVICE_ITEM_COUNT )
        {
            break;
        }
        draw_row( UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT, item );
    }

    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  %s",
              (unsigned)( selected_item + 1U ),
              (unsigned)DEVICE_ITEM_COUNT,
              editing ? "TURN CHANGE / CENTER DONE" : "TURN SELECT / CENTER ENTER" );
    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, editing ? COLOR_ACCENT : COLOR_DIM, 1U );

    if( save_error )
    {
        display_draw_text( 246, UI_LAYOUT_FOOTER_Y, "SAVE ERROR", COLOR_ERROR, 1U );
    }
    (void)display_present();
}

void device_view_enter( void )
{
    selected_item = DEVICE_ITEM_BACK;
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
    const size_t next = ( a_delta > 0 )
                            ? ( current + 1U ) % config->timezone_count
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

/** @brief Return whether mktime preserved every user-edited local calendar field. */
static bool manual_time_round_trip_valid( const struct tm *a_requested, time_t a_epoch )
{
    struct tm normalized;
    if( NULL == localtime_r( &a_epoch, &normalized ) )
    {
        return false;
    }
    return ( normalized.tm_year == a_requested->tm_year ) &&
           ( normalized.tm_mon == a_requested->tm_mon ) &&
           ( normalized.tm_mday == a_requested->tm_mday ) &&
           ( normalized.tm_hour == a_requested->tm_hour ) &&
           ( normalized.tm_min == a_requested->tm_min );
}

/** @brief Commit manually edited local date/time to the authoritative system wall clock. */
static void commit_manual_time( void )
{
    time_t old_epoch = 0;
    const bool had_old = device_time_now( &old_epoch );

    struct tm requested = edited_time;
    requested.tm_sec = 0;
    requested.tm_isdst = -1;
    struct tm conversion = requested;
    const time_t new_epoch = mktime( &conversion );
    const bool calendar_valid = manual_time_round_trip_valid( &requested, new_epoch );
    if( ( new_epoch < (time_t)1577836800 ) || !calendar_valid )
    {
        save_error = true;
        return;
    }
    if( ESP_OK != device_time_set_epoch( new_epoch, RFSCOUT_TIME_SOURCE_MANUAL, "manual" ) )
    {
        save_error = true;
        return;
    }

    edited_time = conversion;
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
        (void)device_config_set_logging_enabled( false );
        log_config_change( "logging.enabled", "On", "Off" );
        if( !log_writer_shutdown( DEVICE_LOG_SHUTDOWN_TIMEOUT_MS ) )
        {
            (void)device_config_set_logging_enabled( true );
            log_config_change( "logging.enabled", "Off", "On" );
            save_error = true;
            return;
        }
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

/** @brief Adjust the global notification mode. */
static void adjust_alert_mode( int a_delta )
{
    const rfscout_alert_mode_t old_mode = device_config_get()->alert_mode;
    int mode = (int)old_mode + ( ( a_delta > 0 ) ? 1 : -1 );
    if( mode < 0 )
    {
        mode = RFSCOUT_ALERT_MODE_COUNT - 1;
    }
    if( mode >= RFSCOUT_ALERT_MODE_COUNT )
    {
        mode = 0;
    }

    (void)device_config_set_alert_mode( (rfscout_alert_mode_t)mode );
    log_config_change( "notifications.mode",
                       alert_mode_name( old_mode ),
                       alert_mode_name( (rfscout_alert_mode_t)mode ) );
}

/** @brief Adjust the global notification volume in 10-percent steps. */
static void adjust_alert_volume( int a_delta )
{
    const uint8_t old_value = device_config_get()->alert_volume_percent;
    int value =
        (int)old_value + ( ( a_delta > 0 ) ? DEVICE_ALERT_VOLUME_STEP : -DEVICE_ALERT_VOLUME_STEP );
    if( value < 0 )
    {
        value = 0;
    }
    if( value > RFSCOUT_ALERT_VOLUME_MAX_PERCENT )
    {
        value = RFSCOUT_ALERT_VOLUME_MAX_PERCENT;
    }
    if( value == old_value )
    {
        return;
    }

    if( ESP_OK == device_config_set_alert_volume( (uint8_t)value ) )
    {
        char old_text[ 8 ];
        char new_text[ 8 ];
        snprintf( old_text, sizeof( old_text ), "%u", (unsigned)old_value );
        snprintf( new_text, sizeof( new_text ), "%u", (unsigned)value );
        log_config_change( "notifications.volume", old_text, new_text );
    }
}

/** @brief Adjust one detection category's Morse character. */
static void adjust_notification_character( unsigned a_item, int a_delta )
{
    const rfscout_notification_type_t type = notification_for_item( a_item );
    const char old_character = device_config_notification_character( type );
    const char new_character = next_morse_character( old_character, a_delta );
    if( ESP_OK != device_config_set_notification_character( type, new_character ) )
    {
        return;
    }

    char key[ 64 ];
    char old_text[ 2 ] = { old_character, '\0' };
    char new_text[ 2 ] = { new_character, '\0' };
    const char *config_key = alert_notification_config_key( type );
    snprintf( key,
              sizeof( key ),
              "notifications.characters.%s",
              ( NULL != config_key ) ? config_key : "unknown" );
    log_config_change( key, old_text, new_text );
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
    else if( DEVICE_ITEM_ALERT_MODE == selected_item )
    {
        adjust_alert_mode( a_delta );
    }
    else if( DEVICE_ITEM_ALERT_VOLUME == selected_item )
    {
        adjust_alert_volume( a_delta );
    }
    else if( ( selected_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
             ( selected_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
    {
        adjust_notification_character( selected_item, a_delta );
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
        if( ( DEVICE_ITEM_SAVE == selected_item ) && !editing )
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
        if( item_editable( selected_item ) )
        {
            editing = !editing;
        }
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
