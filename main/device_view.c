/**
    device_view.c : RFScout Home and Device settings implementation
    Description: Provides the branded Home landing screen, global settings, notification mappings, timezone selection, and manual time setting.
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
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_ITEM_BACK 0U
#define DEVICE_ITEM_BRIGHTNESS 1U
#define DEVICE_ITEM_THEME 2U
#define DEVICE_ITEM_CLOCK_FORMAT 3U
#define DEVICE_ITEM_TIMEZONE 4U
#define DEVICE_ITEM_STARTUP 5U
#define DEVICE_ITEM_YEAR 6U
#define DEVICE_ITEM_MONTH 7U
#define DEVICE_ITEM_DAY 8U
#define DEVICE_ITEM_HOUR 9U
#define DEVICE_ITEM_MINUTE 10U
#define DEVICE_ITEM_WIFI 11U
#define DEVICE_ITEM_CC1101 12U
#define DEVICE_ITEM_NRF24 13U
#define DEVICE_ITEM_AUDIO 14U
#define DEVICE_ITEM_THERMAL 15U
#define DEVICE_ITEM_LOGGING 16U
#define DEVICE_ITEM_ALERT_MODE 17U
#define DEVICE_ITEM_ALERT_VOLUME 18U
#define DEVICE_ITEM_NOTIFICATION_FIRST 19U
#define DEVICE_ITEM_NOTIFICATION_LAST                                                              \
    ( DEVICE_ITEM_NOTIFICATION_FIRST + RFSCOUT_NOTIFICATION_TYPE_COUNT - 1U )
#define DEVICE_ITEM_SAVE ( DEVICE_ITEM_NOTIFICATION_LAST + 1U )
#define DEVICE_ITEM_COUNT ( DEVICE_ITEM_SAVE + 1U )
#define DEVICE_ROWS_PER_PAGE 6U
#define DEVICE_LOG_SHUTDOWN_TIMEOUT_MS 3000U
#define DEVICE_ALERT_VOLUME_STEP 10

#define COLOR_BACKGROUND ( ui_theme_get()->background )
#define COLOR_TEXT ( ui_theme_get()->normal )
#define COLOR_SELECTED ( ui_theme_get()->selected )
#define COLOR_DIM ( ui_theme_get()->info )
#define COLOR_ERROR ( ui_theme_get()->error )
#define COLOR_ACCENT ( ui_theme_get()->accent )

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

/** @brief Return the user-facing name for a startup target. */
static const char *startup_app_name( rfscout_app_id_t a_app )
{
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            return "WiFi / BT";
        case RFSCOUT_APP_CC1101:
            return "CC1101";
        case RFSCOUT_APP_NRF24:
            return "nRF24";
        case RFSCOUT_APP_AUDIO:
            return "Recorder";
        case RFSCOUT_APP_THERMAL:
            return "Thermal";
        case RFSCOUT_APP_DEVICE:
            return "Home";
        case RFSCOUT_APP_COUNT:
        default:
            return "Home";
    }
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
        case DEVICE_ITEM_THEME:
            return "Theme";
        case DEVICE_ITEM_CLOCK_FORMAT:
            return "Clock format";
        case DEVICE_ITEM_TIMEZONE:
            return "Timezone";
        case DEVICE_ITEM_STARTUP:
            return "Startup app";
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
            return "?";
    }
}

/** @brief Return the user-facing value for one Device settings item. */
static void item_value( unsigned a_item, char *a_value, size_t a_capacity )
{
    const device_config_t *config = device_config_get();
    if( NULL == config )
    {
        snprintf( a_value, a_capacity, "?" );
        return;
    }

    if( ( a_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
        ( a_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
    {
        const rfscout_notification_type_t notification = notification_for_item( a_item );
        snprintf( a_value,
                  a_capacity,
                  "%c",
                  device_config_notification_character( notification ) );
        return;
    }

    switch( a_item )
    {
        case DEVICE_ITEM_BRIGHTNESS:
            snprintf( a_value, a_capacity, "%u%%", (unsigned)config->brightness_percent );
            break;
        case DEVICE_ITEM_THEME:
            snprintf( a_value, a_capacity, "%s", ui_theme_display_name( ui_theme_current() ) );
            break;
        case DEVICE_ITEM_CLOCK_FORMAT:
            snprintf( a_value,
                      a_capacity,
                      "%s",
                      RFSCOUT_CLOCK_FORMAT_12H == config->clock_format ? "12 hour" : "24 hour" );
            break;
        case DEVICE_ITEM_TIMEZONE:
            snprintf( a_value, a_capacity, "%s", config->timezone );
            break;
        case DEVICE_ITEM_STARTUP:
            snprintf( a_value, a_capacity, "%s", startup_app_name( config->startup_app ) );
            break;
        case DEVICE_ITEM_YEAR:
            snprintf( a_value, a_capacity, "%u", (unsigned)( edited_time.tm_year + 1900 ) );
            break;
        case DEVICE_ITEM_MONTH:
            snprintf( a_value, a_capacity, "%u", (unsigned)( edited_time.tm_mon + 1 ) );
            break;
        case DEVICE_ITEM_DAY:
            snprintf( a_value, a_capacity, "%u", (unsigned)edited_time.tm_mday );
            break;
        case DEVICE_ITEM_HOUR:
            snprintf( a_value, a_capacity, "%02u", (unsigned)edited_time.tm_hour );
            break;
        case DEVICE_ITEM_MINUTE:
            snprintf( a_value, a_capacity, "%02u", (unsigned)edited_time.tm_min );
            break;
        case DEVICE_ITEM_WIFI:
            snprintf( a_value, a_capacity, "%s", on_off( config->wifi_enabled ) );
            break;
        case DEVICE_ITEM_CC1101:
            snprintf( a_value, a_capacity, "%s", on_off( config->cc1101_enabled ) );
            break;
        case DEVICE_ITEM_NRF24:
            snprintf( a_value, a_capacity, "%s", on_off( config->nrf24_enabled ) );
            break;
        case DEVICE_ITEM_AUDIO:
            snprintf( a_value, a_capacity, "%s", on_off( config->audio_enabled ) );
            break;
        case DEVICE_ITEM_THERMAL:
            snprintf( a_value, a_capacity, "%s", on_off( config->thermal_enabled ) );
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
        default:
            a_value[ 0 ] = '\0';
            break;
    }
}

/** @brief Return true when one item is a calendar field that writes the RTC. */
static bool is_time_item( unsigned a_item )
{
    return ( a_item >= DEVICE_ITEM_YEAR ) && ( a_item <= DEVICE_ITEM_MINUTE );
}

/** @brief Return the total number of rows in the Device settings list. */
static unsigned device_item_count( void )
{
    return DEVICE_ITEM_COUNT;
}

/** @brief Save edited values and return to the Home landing screen. */
static void save_device_settings( void )
{
    const device_config_t *config = device_config_get();
    const uint8_t old_brightness = config->brightness_percent;
    const rfscout_theme_id_t old_theme = ui_theme_current();
    const rfscout_clock_format_t old_clock_format = config->clock_format;
    const rfscout_app_id_t old_startup_app = config->startup_app;
    char old_timezone[ sizeof( config->timezone ) ];
    strlcpy( old_timezone, config->timezone, sizeof( old_timezone ) );
    char old_notification_morse[ RFSCOUT_NOTIFICATION_TYPE_COUNT ];
    memcpy( old_notification_morse, config->notification_morse, sizeof( old_notification_morse ) );

    if( is_time_item( selected_item ) )
    {
        edited_time.tm_sec = 0;
        edited_time.tm_isdst = -1;
        time_t before;
        const bool had_before = device_time_now( &before );
        const esp_err_t time_result = device_time_set_local( &edited_time );
        if( ESP_OK != time_result )
        {
            save_error = true;
            return;
        }
        time_t after;
        const bool had_after = device_time_now( &after );
        char old_time[ 32 ];
        char new_time[ 32 ];
        format_epoch( had_before, before, old_time, sizeof( old_time ) );
        format_epoch( had_after, after, new_time, sizeof( new_time ) );
        log_config_change( "time", old_time, new_time );
    }

    const esp_err_t save_result = device_config_save( false, 0, NULL );
    if( ESP_OK != save_result )
    {
        save_error = true;
        return;
    }

    if( old_brightness != config->brightness_percent )
    {
        char old_value[ 16 ];
        char new_value[ 16 ];
        snprintf( old_value, sizeof( old_value ), "%u", (unsigned)old_brightness );
        snprintf( new_value, sizeof( new_value ), "%u", (unsigned)config->brightness_percent );
        log_config_change( "brightness_percent", old_value, new_value );
    }
    if( old_theme != ui_theme_current() )
    {
        log_config_change(
            "theme", ui_theme_config_name( old_theme ), ui_theme_config_name( ui_theme_current() ) );
    }
    if( old_clock_format != config->clock_format )
    {
        log_config_change( "clock_format",
                           RFSCOUT_CLOCK_FORMAT_12H == old_clock_format ? "12h" : "24h",
                           RFSCOUT_CLOCK_FORMAT_12H == config->clock_format ? "12h" : "24h" );
    }
    if( 0 != strcmp( old_timezone, config->timezone ) )
    {
        log_config_change( "timezone", old_timezone, config->timezone );
    }
    if( old_startup_app != config->startup_app )
    {
        log_config_change(
            "startup_app", startup_app_name( old_startup_app ), startup_app_name( config->startup_app ) );
    }
    for( unsigned index = 0U; index < RFSCOUT_NOTIFICATION_TYPE_COUNT; ++index )
    {
        if( old_notification_morse[ index ] == config->notification_morse[ index ] )
        {
            continue;
        }
        char old_value[ 2 ] = { old_notification_morse[ index ], '\0' };
        char new_value[ 2 ] = { config->notification_morse[ index ], '\0' };
        log_config_change( alert_notification_name( (rfscout_notification_type_t)index ),
                           old_value,
                           new_value );
    }

    display_set_brightness( config->brightness_percent );
    save_error = false;
}

/** @brief Begin editing the Device settings screen. */
void device_view_enter( void )
{
    selected_item = DEVICE_ITEM_BACK;
    editing = false;
    save_error = false;
    refresh_edited_time();
}

/** @brief Apply encoder/button input and redraw the Device settings screen. */
bool device_view_input( bool a_pressed, int a_delta )
{
    if( a_pressed )
    {
        if( DEVICE_ITEM_BACK == selected_item )
        {
            return true;
        }
        if( DEVICE_ITEM_SAVE == selected_item )
        {
            save_device_settings();
            return true;
        }
        if( item_editable( selected_item ) )
        {
            editing = !editing;
        }
    }
    else if( 0 != a_delta )
    {
        if( editing )
        {
            const int direction = ( a_delta > 0 ) ? 1 : -1;
            device_config_adjust( selected_item, direction, &edited_time );
        }
        else
        {
            int next = (int)selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
            if( next < 0 )
            {
                next = (int)device_item_count() - 1;
            }
            if( next >= (int)device_item_count() )
            {
                next = 0;
            }
            selected_item = (unsigned)next;
        }
    }

    const unsigned first = ( selected_item / DEVICE_ROWS_PER_PAGE ) * DEVICE_ROWS_PER_PAGE;
    const unsigned last = first + DEVICE_ROWS_PER_PAGE;
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "SETTINGS", COLOR_DIM );

    for( unsigned item = first; item < last && item < device_item_count(); ++item )
    {
        const int row = (int)( item - first );
        const int y = UI_LAYOUT_MENU_START_Y + row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( item == selected_item );
        const bool action = ( DEVICE_ITEM_BACK == item ) || ( DEVICE_ITEM_SAVE == item );
        const char *marker = " ";
        if( active )
        {
            marker = action ? ">" : ( editing ? "*" : ">" );
        }

        display_draw_text( UI_LAYOUT_LEFT_X, y, marker, active ? COLOR_SELECTED : COLOR_DIM, 1U );
        display_draw_text( UI_LAYOUT_LABEL_X, y, item_label( item ), active ? COLOR_TEXT : COLOR_DIM, 1U );

        char value[ 48 ];
        item_value( item, value, sizeof( value ) );
        if( '\0' != value[ 0 ] )
        {
            if( active && editing )
            {
                display_fill_rect( 164, y - 4, 136, 18, COLOR_ACCENT );
                display_draw_text( 168, y, value, COLOR_BACKGROUND, 1U );
            }
            else
            {
                display_draw_text( 168, y, value, active ? COLOR_SELECTED : COLOR_DIM, 1U );
            }
        }
    }

    if( save_error )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "SAVE FAILED", COLOR_ERROR, 1U );
    }
    else
    {
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT  CENTER: EDIT",
                           COLOR_DIM,
                           1U );
    }
    (void)display_present();
    return false;
}
