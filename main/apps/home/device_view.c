/**
    device_view.c : RFScout Home and Device settings implementation
    Description: Provides global settings, notification mappings, time policy, timezone selection, and semantic feedback.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "alert.h"
#include "app_logging.h"
#include "app_manager.h"
#include "audio_policy.h"
#include "breadcrumbs.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "event.h"
#include "log_writer.h"
#include "morse.h"
#include "ota_menu.h"
#include "power.h"
#include "power_policy_view.h"
#include "storage.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_ITEM_BACK 0U
#define DEVICE_ITEM_BRIGHTNESS 1U
#define DEVICE_ITEM_DIM_TIMEOUT 2U
#define DEVICE_ITEM_OFF_TIMEOUT 3U
#define DEVICE_ITEM_THEME 4U
#define DEVICE_ITEM_AUDIO_POLICY 5U
#define DEVICE_ITEM_UI_VOLUME 6U
#define DEVICE_ITEM_EVENT_VOLUME 7U
#define DEVICE_ITEM_ALERT_MODE 8U
#define DEVICE_ITEM_ALERT_VOLUME 9U
#define DEVICE_ITEM_CLOCK_FORMAT 10U
#define DEVICE_ITEM_TIME 11U
#define DEVICE_ITEM_STARTUP 12U
#define DEVICE_ITEM_SYSTEM_EVENTS 13U
#define DEVICE_ITEM_OTA 14U
#define DEVICE_ITEM_WIFI 15U
#define DEVICE_ITEM_CC1101 16U
#define DEVICE_ITEM_NRF24 17U
#define DEVICE_ITEM_AUDIO 18U
#define DEVICE_ITEM_THERMAL 19U
#define DEVICE_ITEM_AUDIO_SPECTRUM 20U
#define DEVICE_ITEM_LOGGING 21U
#define DEVICE_ITEM_POWER_BUTTON 22U
#define DEVICE_ITEM_POWER_POLICY 23U
#define DEVICE_ITEM_BREADCRUMBS 24U
#define DEVICE_ITEM_BREADCRUMB_FILES 25U
#define DEVICE_ITEM_NOTIFICATION_FIRST 26U
#define DEVICE_ITEM_NOTIFICATION_LAST                                                              \
    ( DEVICE_ITEM_NOTIFICATION_FIRST + RFSCOUT_NOTIFICATION_TYPE_COUNT - 1U )
#define DEVICE_ITEM_BASE_COUNT ( DEVICE_ITEM_BREADCRUMB_FILES + 1U )
#define DEVICE_ROWS_PER_PAGE 6U
#define DEVICE_LOG_SHUTDOWN_TIMEOUT_MS 3000U
#define DEVICE_AUDIO_VOLUME_STEP 10

#define TIME_MENU_BACK 0U
#define TIME_MENU_MODE 1U
#define TIME_MENU_TIMEZONE 2U
#define TIME_MENU_YEAR 3U
#define TIME_MENU_MONTH 4U
#define TIME_MENU_DAY 5U
#define TIME_MENU_HOUR 6U
#define TIME_MENU_MINUTE 7U
#define TIME_MENU_AUTO_COUNT 3U
#define TIME_MENU_MANUAL_COUNT 8U

#define POWER_MENU_BACK 0U
#define POWER_MENU_SHORT_PRESS 1U
#define POWER_MENU_LONG_PRESS 2U
#define POWER_MENU_ITEM_COUNT 3U

static const uint16_t DIM_TIMEOUT_OPTIONS[] = { 0U, 30U, 60U, 120U, 300U };
static const uint16_t OFF_TIMEOUT_OPTIONS[] = { 0U, 60U, 120U, 300U, 600U, 1800U };
static const uint16_t BREADCRUMB_FILE_LIMIT_OPTIONS[] = { 24U, 72U, 144U, 288U, 1008U };

#define COLOR_BACKGROUND ( ui_theme_get()->background )
#define COLOR_TEXT ( ui_theme_get()->text )
#define COLOR_SELECTED ( ui_theme_get()->selected )
#define COLOR_DIM ( ui_theme_get()->secondary )
#define COLOR_ERROR ( ui_theme_get()->error )
#define COLOR_ACCENT ( ui_theme_get()->accent )

static unsigned selected_item;
static bool editing;
static struct tm edited_time;
static bool save_error;
static bool power_menu_open;
static unsigned power_selected_item;
static bool power_policy_menu_open;
static bool time_menu_open;
static unsigned time_selected_item;
static bool time_editing;
static bool ota_menu_open;

static unsigned device_item_count( void )
{
    return DEVICE_ITEM_BASE_COUNT;
}

static unsigned time_menu_item_count( void )
{
    return RFSCOUT_TIME_MODE_MANUAL == device_time_mode() ? TIME_MENU_MANUAL_COUNT
                                                          : TIME_MENU_AUTO_COUNT;
}

static void emit_device_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_DEVICE, a_value );
}

static void log_config_change( const char *a_key, const char *a_old, const char *a_new )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "config" ) && log_str( &record, "key", a_key ) &&
        log_str( &record, "old", a_old ) && log_str( &record, "new", a_new ) )
    {
        (void)log_end( &record );
    }
}

static void format_epoch( time_t a_epoch, char *a_output, size_t a_capacity )
{
    if( !device_time_format_iso8601_utc( a_epoch, a_output, a_capacity ) )
    {
        snprintf( a_output, a_capacity, "invalid" );
    }
}

static void refresh_edited_time( void )
{
    time_t now = 0;
    if( device_time_now( &now ) && NULL != localtime_r( &now, &edited_time ) )
    {
        return;
    }
    memset( &edited_time, 0, sizeof( edited_time ) );
    edited_time.tm_year = 126;
    edited_time.tm_mon = 0;
    edited_time.tm_mday = 1;
    edited_time.tm_isdst = -1;
}

static const char *on_off( bool a_value )
{
    return a_value ? "On" : "Off";
}

static void format_timeout( uint16_t a_seconds, char *a_value, size_t a_capacity )
{
    if( 0U == a_seconds )
    {
        snprintf( a_value, a_capacity, "Disabled" );
    }
    else if( 0U == ( a_seconds % 60U ) )
    {
        snprintf( a_value, a_capacity, "%um", (unsigned)( a_seconds / 60U ) );
    }
    else
    {
        snprintf( a_value, a_capacity, "%us", (unsigned)a_seconds );
    }
}

static uint16_t
next_timeout_option( const uint16_t *a_options, size_t a_count, uint16_t a_current, int a_delta )
{
    size_t index = 0U;
    for( size_t item = 0U; item < a_count; ++item )
    {
        if( a_options[ item ] == a_current )
        {
            index = item;
            break;
        }
    }
    index = ( a_delta > 0 ) ? ( index + 1U ) % a_count : ( index + a_count - 1U ) % a_count;
    return a_options[ index ];
}

static const char *startup_app_name( rfscout_app_id_t a_app )
{
    switch( a_app )
    {
        case RFSCOUT_APP_SYSTEM_EVENTS:
            return "System Events";
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
        case RFSCOUT_APP_AUDIO_SPECTRUM:
            return "Audio Spectrum";
        case RFSCOUT_APP_DEVICE:
        case RFSCOUT_APP_COUNT:
        default:
            return "Home";
    }
}

static rfscout_notification_type_t notification_for_item( unsigned a_item )
{
    return (rfscout_notification_type_t)( a_item - DEVICE_ITEM_NOTIFICATION_FIRST );
}

static char next_morse_character( char a_current, int a_direction )
{
    const char *characters = morse_supported_characters();
    const size_t count = strlen( characters );
    const char *current = strchr( characters, a_current );
    size_t index = ( NULL != current ) ? (size_t)( current - characters ) : 0U;
    index = ( a_direction > 0 ) ? ( index + 1U ) % count : ( index + count - 1U ) % count;
    return characters[ index ];
}

static bool item_direct_toggle( unsigned a_item )
{
    return ( DEVICE_ITEM_BREADCRUMBS == a_item ) || ( DEVICE_ITEM_AUDIO_POLICY == a_item ) ||
           ( DEVICE_ITEM_ALERT_MODE == a_item ) || ( DEVICE_ITEM_CLOCK_FORMAT == a_item ) ||
           ( DEVICE_ITEM_SYSTEM_EVENTS == a_item ) || ( DEVICE_ITEM_WIFI == a_item ) || ( DEVICE_ITEM_CC1101 == a_item ) ||
           ( DEVICE_ITEM_NRF24 == a_item ) || ( DEVICE_ITEM_AUDIO == a_item ) ||
           ( DEVICE_ITEM_THERMAL == a_item ) || ( DEVICE_ITEM_AUDIO_SPECTRUM == a_item ) ||
           ( DEVICE_ITEM_LOGGING == a_item );
}

static bool item_editable( unsigned a_item )
{
    return ( DEVICE_ITEM_BACK != a_item ) && ( DEVICE_ITEM_TIME != a_item ) &&
           ( DEVICE_ITEM_OTA != a_item ) && ( DEVICE_ITEM_POWER_BUTTON != a_item ) &&
           ( DEVICE_ITEM_POWER_POLICY != a_item ) && ( DEVICE_ITEM_BREADCRUMBS != a_item );
}

static const char *item_label( unsigned a_item )
{
    switch( a_item )
    {
        case DEVICE_ITEM_BACK:
            return "Back";
        case DEVICE_ITEM_BRIGHTNESS:
            return "Brightness";
        case DEVICE_ITEM_DIM_TIMEOUT:
            return "Dim after";
        case DEVICE_ITEM_OFF_TIMEOUT:
            return "Screen off";
        case DEVICE_ITEM_THEME:
            return "Theme";
        case DEVICE_ITEM_AUDIO_POLICY:
            return "Audio policy";
        case DEVICE_ITEM_UI_VOLUME:
            return "UI volume";
        case DEVICE_ITEM_EVENT_VOLUME:
            return "Event volume";
        case DEVICE_ITEM_ALERT_MODE:
            return "Alerts";
        case DEVICE_ITEM_ALERT_VOLUME:
            return "Alert volume";
        case DEVICE_ITEM_CLOCK_FORMAT:
            return "Clock format";
        case DEVICE_ITEM_TIME:
            return "Time";
        case DEVICE_ITEM_STARTUP:
            return "Startup app";
        case DEVICE_ITEM_SYSTEM_EVENTS:
            return "System Events";
        case DEVICE_ITEM_OTA:
            return "Update";
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
        case DEVICE_ITEM_AUDIO_SPECTRUM:
            return "Audio Spectrum";
        case DEVICE_ITEM_LOGGING:
            return "SD logging";
        case DEVICE_ITEM_POWER_BUTTON:
            return "Power button";
        case DEVICE_ITEM_POWER_POLICY:
            return "Power policy";
        case DEVICE_ITEM_BREADCRUMBS:
            return "Breadcrumbs";
        case DEVICE_ITEM_BREADCRUMB_FILES:
            return "Breadcrumb files";
        default:
            if( ( a_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
                ( a_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
            {
                return alert_notification_name( notification_for_item( a_item ) );
            }
            return "Unknown";
    }
}

static void item_value( unsigned a_item, char *a_value, size_t a_capacity )
{
    const device_config_t *config = device_config_get();
    a_value[ 0 ] = '\0';
    switch( a_item )
    {
        case DEVICE_ITEM_BRIGHTNESS:
            snprintf( a_value, a_capacity, "%u%%", (unsigned)config->brightness_percent );
            break;
        case DEVICE_ITEM_DIM_TIMEOUT:
            format_timeout( config->display_dim_timeout_seconds, a_value, a_capacity );
            break;
        case DEVICE_ITEM_OFF_TIMEOUT:
            format_timeout( config->display_off_timeout_seconds, a_value, a_capacity );
            break;
        case DEVICE_ITEM_THEME:
            snprintf( a_value, a_capacity, "%s", ui_theme_display_name( ui_theme_current() ) );
            break;
        case DEVICE_ITEM_AUDIO_POLICY:
            snprintf(
                a_value, a_capacity, "%s", audio_policy_display_name( config->audio_policy ) );
            break;
        case DEVICE_ITEM_UI_VOLUME:
            snprintf(
                a_value, a_capacity, "%u%%", (unsigned)config->audio_interaction_gain_percent );
            break;
        case DEVICE_ITEM_EVENT_VOLUME:
            snprintf(
                a_value, a_capacity, "%u%%", (unsigned)config->audio_notification_gain_percent );
            break;
        case DEVICE_ITEM_ALERT_MODE:
            snprintf( a_value, a_capacity, "%s", alert_mode_name( config->alert_mode ) );
            break;
        case DEVICE_ITEM_ALERT_VOLUME:
            snprintf( a_value, a_capacity, "%u%%", (unsigned)config->alert_volume_percent );
            break;
        case DEVICE_ITEM_CLOCK_FORMAT:
            snprintf( a_value, a_capacity, "%s", config->clock_24h ? "24h" : "12h" );
            break;
        case DEVICE_ITEM_TIME:
            snprintf( a_value,
                      a_capacity,
                      "%s",
                      RFSCOUT_TIME_MODE_AUTO == device_time_mode() ? "Auto" : "Manual" );
            break;
        case DEVICE_ITEM_STARTUP:
            snprintf( a_value, a_capacity, "%s", startup_app_name( config->startup_app ) );
            break;
        case DEVICE_ITEM_SYSTEM_EVENTS:
            snprintf( a_value,
                      a_capacity,
                      "%s",
                      on_off( config->apps_enabled[ RFSCOUT_APP_SYSTEM_EVENTS ] ) );
            break;
        case DEVICE_ITEM_OTA:
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
        case DEVICE_ITEM_AUDIO_SPECTRUM:
            snprintf( a_value,
                      a_capacity,
                      "%s",
                      on_off( config->apps_enabled[ RFSCOUT_APP_AUDIO_SPECTRUM ] ) );
            break;
        case DEVICE_ITEM_LOGGING:
            snprintf( a_value, a_capacity, "%s", on_off( config->logging_enabled ) );
            break;
        case DEVICE_ITEM_BREADCRUMBS:
            snprintf( a_value, a_capacity, "%s", on_off( breadcrumbs_enabled() ) );
            break;
        case DEVICE_ITEM_BREADCRUMB_FILES:
            snprintf( a_value, a_capacity, "%u", (unsigned)breadcrumbs_file_limit() );
            break;
        default:
            if( ( a_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
                ( a_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
            {
                const rfscout_notification_type_t type = notification_for_item( a_item );
                const char character = device_config_notification_character( type );
                if( ' ' == character )
                {
                    snprintf( a_value, a_capacity, "OFF" );
                }
                else
                {
                    const char *pattern = morse_pattern( character );
                    snprintf( a_value,
                              a_capacity,
                              "%c %s",
                              character,
                              ( NULL != pattern ) ? pattern : "?" );
                }
            }
            break;
    }
}

static void draw_row( int a_y, unsigned a_item )
{
    char value[ 48 ];
    item_value( a_item, value, sizeof( value ) );
    const bool active = selected_item == a_item;
    const uint16_t color = active ? COLOR_SELECTED : COLOR_TEXT;
    if( active )
    {
        display_draw_text(
            UI_LAYOUT_LEFT_X, a_y, ( editing && item_editable( a_item ) ) ? ">*" : ">", color, 1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, item_label( a_item ), color, 1U );
    if( '\0' != value[ 0 ] )
    {
        display_draw_text( 232, a_y, value, active ? COLOR_ACCENT : COLOR_DIM, 1U );
    }
}

static void draw_time_row( int a_y, unsigned a_item )
{
    const bool active = time_selected_item == a_item;
    const uint16_t color = active ? COLOR_SELECTED : COLOR_TEXT;
    const char *label = "";
    char value[ 48 ] = "";
    switch( a_item )
    {
        case TIME_MENU_BACK:
            label = "Back";
            break;
        case TIME_MENU_MODE:
        {
            label = "Mode";
            rfscout_time_status_t status;
            device_time_status( &status );
            if( RFSCOUT_TIME_MODE_MANUAL == status.mode )
            {
                snprintf( value, sizeof( value ), "Manual" );
            }
            else if( status.valid && RFSCOUT_TIME_SOURCE_GPS == status.source )
            {
                snprintf( value, sizeof( value ), "Auto / GPS" );
            }
            else
            {
                snprintf( value, sizeof( value ), "Auto / Waiting" );
            }
            break;
        }
        case TIME_MENU_TIMEZONE:
        {
            label = "Timezone";
            const device_timezone_t *zone = device_config_timezone();
            snprintf( value,
                      sizeof( value ),
                      "%s",
                      NULL != zone ? zone->label : device_config_get()->timezone_selected );
            break;
        }
        case TIME_MENU_YEAR:
            label = "Year";
            snprintf( value, sizeof( value ), "%04d", edited_time.tm_year + 1900 );
            break;
        case TIME_MENU_MONTH:
            label = "Month";
            snprintf( value, sizeof( value ), "%02d", edited_time.tm_mon + 1 );
            break;
        case TIME_MENU_DAY:
            label = "Day";
            snprintf( value, sizeof( value ), "%02d", edited_time.tm_mday );
            break;
        case TIME_MENU_HOUR:
            label = "Hour";
            snprintf( value, sizeof( value ), "%02d", edited_time.tm_hour );
            break;
        case TIME_MENU_MINUTE:
            label = "Minute";
            snprintf( value, sizeof( value ), "%02d", edited_time.tm_min );
            break;
        default:
            break;
    }

    if( active )
    {
        display_draw_text( UI_LAYOUT_LEFT_X,
                           a_y,
                           time_editing && a_item >= TIME_MENU_TIMEZONE ? ">*" : ">",
                           color,
                           1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, label, color, 1U );
    if( '\0' != value[ 0 ] )
    {
        display_draw_text( 214, a_y, value, active ? COLOR_ACCENT : COLOR_DIM, 1U );
    }
}

static void render_time_menu( void )
{
    const unsigned item_count = time_menu_item_count();
    if( time_selected_item >= item_count )
    {
        time_selected_item = item_count - 1U;
        time_editing = false;
    }
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "TIME", COLOR_DIM );
    const unsigned page_start =
        ( time_selected_item / DEVICE_ROWS_PER_PAGE ) * DEVICE_ROWS_PER_PAGE;
    for( unsigned row = 0U; row < DEVICE_ROWS_PER_PAGE; ++row )
    {
        const unsigned item = page_start + row;
        if( item >= item_count )
        {
            break;
        }
        draw_time_row( UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT, item );
    }
    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  %s",
              (unsigned)( time_selected_item + 1U ),
              item_count,
              time_editing
                  ? "TURN CHANGE / CENTER DONE"
                  : ( TIME_MENU_MODE == time_selected_item ? "CENTER CYCLE"
                                                           : "TURN SELECT / CENTER ENTER" ) );
    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, time_editing ? COLOR_ACCENT : COLOR_DIM, 1U );
    if( save_error )
    {
        display_draw_text( 246, UI_LAYOUT_FOOTER_Y, "SAVE ERROR", COLOR_ERROR, 1U );
    }
    (void)display_present();
}

static power_action_t next_power_action( power_action_t a_current, bool a_long_press )
{
    power_action_t candidate = a_current;
    for( unsigned attempt = 0U; attempt < POWER_ACTION_COUNT; ++attempt )
    {
        candidate = (power_action_t)( ( (unsigned)candidate + 1U ) % (unsigned)POWER_ACTION_COUNT );
        if( power_action_supported( candidate, a_long_press ) )
        {
            return candidate;
        }
    }
    return POWER_ACTION_NONE;
}

static void
draw_power_menu_row( int a_y, unsigned a_item, const char *a_label, const char *a_value )
{
    const bool active = power_selected_item == a_item;
    const uint16_t color = active ? COLOR_SELECTED : COLOR_TEXT;
    if( active )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, a_y, ">", color, 1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, a_label, color, 1U );
    if( NULL != a_value )
    {
        display_draw_text( 214, a_y, a_value, active ? COLOR_ACCENT : COLOR_DIM, 1U );
    }
}

static void render_power_menu( void )
{
    const device_config_t *config = device_config_get();
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "POWER BUTTON", COLOR_DIM );
    draw_power_menu_row( UI_LAYOUT_MENU_START_Y, POWER_MENU_BACK, "Back", NULL );
    draw_power_menu_row( UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
                         POWER_MENU_SHORT_PRESS,
                         "Short press",
                         power_action_name( config->power_button_short_action ) );
    draw_power_menu_row( UI_LAYOUT_MENU_START_Y + 2 * UI_LAYOUT_MENU_ROW_HEIGHT,
                         POWER_MENU_LONG_PRESS,
                         "Long press",
                         power_action_name( config->power_button_long_action ) );

    const char *hint = ( POWER_MENU_BACK == power_selected_item ) ? "CENTER BACK" : "CENTER CYCLE";
    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  TURN SELECT / %s",
              (unsigned)( power_selected_item + 1U ),
              (unsigned)POWER_MENU_ITEM_COUNT,
              hint );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, COLOR_DIM, 1U );
    (void)display_present();
}

static void cycle_power_action( bool a_long_press )
{
    const device_config_t *config = device_config_get();
    const power_action_t old_action =
        a_long_press ? config->power_button_long_action : config->power_button_short_action;
    const power_action_t new_action = next_power_action( old_action, a_long_press );
    if( new_action == old_action )
    {
        return;
    }

    const esp_err_t result = a_long_press
                                 ? device_config_set_power_button_long_action( new_action )
                                 : device_config_set_power_button_short_action( new_action );
    if( ESP_OK != result )
    {
        save_error = true;
        emit_device_event( RFSCOUT_EVENT_ERROR, -1 );
        return;
    }

    log_config_change( a_long_press ? "power.long_press" : "power.short_press",
                       power_action_name( old_action ),
                       power_action_name( new_action ) );
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, 1 );
}

static void draw_home_logo( void )
{
    display_fill_rect( 50, 54, 8, 48, COLOR_ACCENT );
    display_fill_rect( 58, 54, 22, 8, COLOR_ACCENT );
    display_fill_rect( 58, 74, 18, 8, COLOR_ACCENT );
    display_draw_text( 92, 58, "RF", COLOR_ACCENT, 3U );
    display_draw_text( 130, 58, "SCOUT", COLOR_SELECTED, 3U );
    display_draw_text( 92, 88, "RF SITUATIONAL AWARENESS", COLOR_DIM, 1U );
}

void device_view_landing( void )
{
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "HOME", COLOR_DIM );
    draw_home_logo();
    display_draw_text( 92, 118, "CENTER: SETTINGS", COLOR_ACCENT, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: APPS", COLOR_DIM, 1U );
    (void)display_present();
}

void device_view_render( void )
{
    if( time_menu_open )
    {
        render_time_menu();
        return;
    }
    if( power_policy_menu_open )
    {
        power_policy_view_render();
        return;
    }
    if( power_menu_open )
    {
        render_power_menu();
        return;
    }
    if( ota_menu_open )
    {
        return;
    }

    const unsigned item_count = device_item_count();
    display_clear( COLOR_BACKGROUND );
    ui_chrome_draw( "DEVICE SETTINGS", COLOR_DIM );
    const unsigned page_start = ( selected_item / DEVICE_ROWS_PER_PAGE ) * DEVICE_ROWS_PER_PAGE;
    for( unsigned row = 0U; row < DEVICE_ROWS_PER_PAGE; ++row )
    {
        const unsigned item = page_start + row;
        if( item >= item_count )
        {
            break;
        }
        draw_row( UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT, item );
    }

    const char *hint = editing
                           ? "TURN CHANGE / CENTER DONE"
                           : ( item_direct_toggle( selected_item ) ? "CENTER CYCLE"
                                                                   : "TURN SELECT / CENTER ENTER" );
    char footer[ 64 ];
    snprintf(
        footer, sizeof( footer ), "%u/%u  %s", (unsigned)( selected_item + 1U ), item_count, hint );
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
    power_menu_open = false;
    power_selected_item = POWER_MENU_BACK;
    power_policy_menu_open = false;
    time_menu_open = false;
    time_selected_item = TIME_MENU_BACK;
    time_editing = false;
    ota_menu_open = false;
    refresh_edited_time();
    device_view_render();
}

static void move_selection( int a_delta )
{
    const int item_count = (int)device_item_count();
    int item = (int)selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = item_count - 1;
    }
    if( item >= item_count )
    {
        item = 0;
    }
    if( (unsigned)item != selected_item )
    {
        selected_item = (unsigned)item;
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void move_time_selection( int a_delta )
{
    const int item_count = (int)time_menu_item_count();
    int item = (int)time_selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = item_count - 1;
    }
    if( item >= item_count )
    {
        item = 0;
    }
    time_selected_item = (unsigned)item;
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

static void move_power_selection( int a_delta )
{
    int item = (int)power_selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)POWER_MENU_ITEM_COUNT - 1;
    }
    if( item >= (int)POWER_MENU_ITEM_COUNT )
    {
        item = 0;
    }
    power_selected_item = (unsigned)item;
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

static void toggle_app( rfscout_app_id_t a_app, const char *a_key )
{
    const bool old_value = device_config_app_enabled( a_app );
    if( ESP_OK == device_config_set_app_enabled( a_app, !old_value ) )
    {
        log_config_change( a_key, on_off( old_value ), on_off( !old_value ) );
        emit_device_event( !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                           !old_value ? 1 : 0 );
    }
}

static void adjust_display_timeout( bool a_off_timeout, int a_delta )
{
    const device_config_t *config = device_config_get();
    const uint16_t old_dim = config->display_dim_timeout_seconds;
    const uint16_t old_off = config->display_off_timeout_seconds;

    if( !a_off_timeout )
    {
        const uint16_t new_dim =
            next_timeout_option( DIM_TIMEOUT_OPTIONS,
                                 sizeof( DIM_TIMEOUT_OPTIONS ) / sizeof( DIM_TIMEOUT_OPTIONS[ 0 ] ),
                                 old_dim,
                                 a_delta );
        uint16_t new_off = old_off;
        if( ( 0U != new_dim ) && ( 0U != new_off ) && ( new_off <= new_dim ) )
        {
            new_off = 0U;
            for( size_t item = 1U;
                 item < sizeof( OFF_TIMEOUT_OPTIONS ) / sizeof( OFF_TIMEOUT_OPTIONS[ 0 ] );
                 ++item )
            {
                if( OFF_TIMEOUT_OPTIONS[ item ] > new_dim )
                {
                    new_off = OFF_TIMEOUT_OPTIONS[ item ];
                    break;
                }
            }
            if( ESP_OK != device_config_set_display_off_timeout( new_off ) )
            {
                return;
            }
        }
        if( ESP_OK == device_config_set_display_dim_timeout( new_dim ) )
        {
            char old_text[ 16 ];
            char new_text[ 16 ];
            format_timeout( old_dim, old_text, sizeof( old_text ) );
            format_timeout( new_dim, new_text, sizeof( new_text ) );
            log_config_change( "display.dim_timeout_seconds", old_text, new_text );
            emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
        }
        return;
    }

    uint16_t candidate = old_off;
    const size_t option_count = sizeof( OFF_TIMEOUT_OPTIONS ) / sizeof( OFF_TIMEOUT_OPTIONS[ 0 ] );
    for( size_t attempt = 0U; attempt < option_count; ++attempt )
    {
        candidate = next_timeout_option( OFF_TIMEOUT_OPTIONS, option_count, candidate, a_delta );
        if( ( 0U == candidate ) || ( 0U == old_dim ) || ( candidate > old_dim ) )
        {
            break;
        }
    }
    if( ESP_OK == device_config_set_display_off_timeout( candidate ) )
    {
        char old_text[ 16 ];
        char new_text[ 16 ];
        format_timeout( old_off, old_text, sizeof( old_text ) );
        format_timeout( candidate, new_text, sizeof( new_text ) );
        log_config_change( "display.off_timeout_seconds", old_text, new_text );
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void adjust_theme( int a_delta )
{
    const rfscout_theme_id_t old_theme = ui_theme_current();
    int theme = (int)old_theme + ( ( a_delta > 0 ) ? 1 : -1 );
    if( theme < 0 )
    {
        theme = RFSCOUT_THEME_COUNT - 1;
    }
    if( theme >= RFSCOUT_THEME_COUNT )
    {
        theme = 0;
    }
    const rfscout_theme_id_t new_theme = (rfscout_theme_id_t)theme;
    if( ( new_theme != old_theme ) && ( ESP_OK == ui_theme_set( new_theme ) ) )
    {
        log_config_change( "display.theme",
                           ui_theme_display_name( old_theme ),
                           ui_theme_display_name( new_theme ) );
        save_error = false;
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
    else if( new_theme != old_theme )
    {
        save_error = true;
    }
}

static void adjust_audio_policy( int a_delta )
{
    const rfscout_audio_policy_id_t old_policy = device_config_audio_policy();
    int policy = (int)old_policy + ( ( a_delta > 0 ) ? 1 : -1 );
    if( policy < 0 )
    {
        policy = RFSCOUT_AUDIO_POLICY_COUNT - 1;
    }
    if( policy >= RFSCOUT_AUDIO_POLICY_COUNT )
    {
        policy = 0;
    }
    const rfscout_audio_policy_id_t new_policy = (rfscout_audio_policy_id_t)policy;
    if( new_policy == old_policy || ESP_OK != device_config_set_audio_policy( new_policy ) )
    {
        return;
    }

    log_config_change( "audio.policy",
                       audio_policy_display_name( old_policy ),
                       audio_policy_display_name( new_policy ) );
    rfscout_event_emit(
        RFSCOUT_EVENT_AUDIO_POLICY_CHANGED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)new_policy );
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

static void adjust_audio_volume( rfscout_audio_category_t a_category, int a_delta )
{
    const device_config_t *config = device_config_get();
    uint8_t old_value;
    const char *key;
    switch( a_category )
    {
        case RFSCOUT_AUDIO_CATEGORY_UI:
            old_value = config->audio_interaction_gain_percent;
            key = "audio.ui_volume";
            break;
        case RFSCOUT_AUDIO_CATEGORY_EVENT:
            old_value = config->audio_notification_gain_percent;
            key = "audio.event_volume";
            break;
        case RFSCOUT_AUDIO_CATEGORY_ALERT:
            old_value = config->alert_volume_percent;
            key = "audio.alert_volume";
            break;
        case RFSCOUT_AUDIO_CATEGORY_CONTENT:
        case RFSCOUT_AUDIO_CATEGORY_COUNT:
        default:
            return;
    }

    int value =
        (int)old_value + ( ( a_delta > 0 ) ? DEVICE_AUDIO_VOLUME_STEP : -DEVICE_AUDIO_VOLUME_STEP );
    if( value < 0 )
    {
        value = 0;
    }
    if( value > 100 )
    {
        value = 100;
    }
    if( value == old_value )
    {
        return;
    }

    esp_err_t result = ESP_ERR_INVALID_ARG;
    if( RFSCOUT_AUDIO_CATEGORY_UI == a_category )
    {
        result = device_config_set_audio_gains( (uint8_t)value,
                                                config->audio_notification_gain_percent );
    }
    else if( RFSCOUT_AUDIO_CATEGORY_EVENT == a_category )
    {
        result =
            device_config_set_audio_gains( config->audio_interaction_gain_percent, (uint8_t)value );
    }
    else if( RFSCOUT_AUDIO_CATEGORY_ALERT == a_category )
    {
        result = device_config_set_alert_volume( (uint8_t)value );
    }
    if( ESP_OK != result )
    {
        return;
    }

    char old_text[ 8 ];
    char new_text[ 8 ];
    snprintf( old_text, sizeof( old_text ), "%u", (unsigned)old_value );
    snprintf( new_text, sizeof( new_text ), "%u", (unsigned)value );
    log_config_change( key, old_text, new_text );
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

static void adjust_startup_app( int a_delta )
{
    const rfscout_app_id_t old_app = device_config_startup_app();
    rfscout_app_id_t new_app = old_app;
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    for( int attempt = 0; attempt < RFSCOUT_APP_COUNT; ++attempt )
    {
        int app = (int)new_app + direction;
        if( app < 0 )
        {
            app = RFSCOUT_APP_COUNT - 1;
        }
        else if( app >= RFSCOUT_APP_COUNT )
        {
            app = 0;
        }
        new_app = (rfscout_app_id_t)app;
        if( ( RFSCOUT_APP_DEVICE == new_app ) || device_config_app_enabled( new_app ) )
        {
            break;
        }
    }
    if( ( new_app != old_app ) && ( ESP_OK == device_config_set_startup_app( new_app ) ) )
    {
        log_config_change(
            "apps.startup", startup_app_name( old_app ), startup_app_name( new_app ) );
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void adjust_timezone( int a_delta )
{
    const device_config_t *config = device_config_get();
    if( 0U == config->timezone_count )
    {
        return;
    }

    size_t current = 0U;
    for( size_t index = 0U; index < config->timezone_count; ++index )
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
    if( next == current )
    {
        return;
    }

    char old_name[ RFSCOUT_TIMEZONE_NAME_MAX ];
    snprintf( old_name, sizeof( old_name ), "%s", config->timezone_selected );
    if( ESP_OK == device_config_set_timezone( config->timezones[ next ].name ) )
    {
        (void)device_time_apply_timezone();
        log_config_change( "timezone.selected", old_name, config->timezones[ next ].name );
        refresh_edited_time();
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void adjust_time_field( unsigned a_item, int a_delta )
{
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    const struct tm before = edited_time;
    switch( a_item )
    {
        case TIME_MENU_YEAR:
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
        case TIME_MENU_MONTH:
            edited_time.tm_mon = ( edited_time.tm_mon + direction + 12 ) % 12;
            break;
        case TIME_MENU_DAY:
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
        case TIME_MENU_HOUR:
            edited_time.tm_hour = ( edited_time.tm_hour + direction + 24 ) % 24;
            break;
        case TIME_MENU_MINUTE:
            edited_time.tm_min = ( edited_time.tm_min + direction + 60 ) % 60;
            break;
        default:
            return;
    }
    if( 0 != memcmp( &before, &edited_time, sizeof( edited_time ) ) )
    {
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

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

static bool commit_manual_time( void )
{
    time_t old_epoch = 0;
    const bool old_valid = device_time_now( &old_epoch );
    struct tm requested = edited_time;
    requested.tm_sec = 0;
    requested.tm_isdst = -1;
    struct tm conversion = requested;
    const time_t new_epoch = mktime( &conversion );
    if( ( new_epoch < (time_t)1577836800 ) ||
        !manual_time_round_trip_valid( &requested, new_epoch ) ||
        ( ESP_OK != device_time_set_epoch( new_epoch ) ) )
    {
        save_error = true;
        return false;
    }

    edited_time = conversion;
    char old_text[ 32 ];
    char new_text[ 32 ];
    if( old_valid )
    {
        format_epoch( old_epoch, old_text, sizeof( old_text ) );
    }
    else
    {
        snprintf( old_text, sizeof( old_text ), "invalid" );
    }
    format_epoch( new_epoch, new_text, sizeof( new_text ) );

    rfscout_log_record_t record;
    if( log_begin( &record, "time" ) && log_str( &record, "old", old_text ) &&
        log_str( &record, "new", new_text ) && log_str( &record, "source", "manual" ) )
    {
        (void)log_end( &record );
    }
    save_error = false;
    return true;
}

static void toggle_time_mode( void )
{
    const rfscout_time_mode_t old_mode = device_time_mode();
    const rfscout_time_mode_t new_mode =
        RFSCOUT_TIME_MODE_AUTO == old_mode ? RFSCOUT_TIME_MODE_MANUAL : RFSCOUT_TIME_MODE_AUTO;
    const esp_err_t result = device_time_set_mode( new_mode );
    if( ESP_OK != result )
    {
        save_error = true;
        emit_device_event( RFSCOUT_EVENT_ERROR, (int32_t)result );
        return;
    }
    save_error = false;
    if( RFSCOUT_TIME_MODE_MANUAL == new_mode )
    {
        refresh_edited_time();
    }
    log_config_change( "time.mode",
                       RFSCOUT_TIME_MODE_AUTO == old_mode ? "auto" : "manual",
                       RFSCOUT_TIME_MODE_AUTO == new_mode ? "auto" : "manual" );
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, 1 );
}

static bool time_menu_input( bool a_pressed, int a_delta )
{
    if( a_pressed )
    {
        if( TIME_MENU_BACK == time_selected_item && !time_editing )
        {
            time_menu_open = false;
            time_selected_item = TIME_MENU_BACK;
            emit_device_event( RFSCOUT_EVENT_BACK, 0 );
            device_view_render();
            return true;
        }
        if( TIME_MENU_MODE == time_selected_item && !time_editing )
        {
            toggle_time_mode();
            if( time_selected_item >= time_menu_item_count() )
            {
                time_selected_item = time_menu_item_count() - 1U;
            }
            render_time_menu();
            return false;
        }

        bool accepted = true;
        if( time_editing && time_selected_item >= TIME_MENU_YEAR )
        {
            accepted = commit_manual_time();
        }
        if( TIME_MENU_TIMEZONE == time_selected_item ||
            ( RFSCOUT_TIME_MODE_MANUAL == device_time_mode() &&
              time_selected_item >= TIME_MENU_YEAR ) )
        {
            time_editing = !time_editing;
            if( accepted )
            {
                emit_device_event( time_editing ? RFSCOUT_EVENT_ENTER : RFSCOUT_EVENT_ACTIVATE,
                                   time_editing ? 1 : 0 );
            }
        }
        render_time_menu();
        return false;
    }

    if( 0 == a_delta )
    {
        return false;
    }
    if( time_editing )
    {
        if( TIME_MENU_TIMEZONE == time_selected_item )
        {
            adjust_timezone( a_delta );
        }
        else if( time_selected_item >= TIME_MENU_YEAR )
        {
            adjust_time_field( time_selected_item, a_delta );
        }
    }
    else
    {
        move_time_selection( a_delta );
    }
    render_time_menu();
    return false;
}

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
        emit_device_event( RFSCOUT_EVENT_DISABLE, 0 );
        return;
    }

    if( !storage_is_available() || ( ESP_OK != log_writer_init() ) )
    {
        save_error = true;
        return;
    }
    (void)device_config_set_logging_enabled( true );
    app_logging_init();
    log_config_change( "logging.enabled", "Off", "On" );
    save_error = false;
    emit_device_event( RFSCOUT_EVENT_ENABLE, 1 );
}

static void toggle_breadcrumbs( void )
{
    const bool old_value = breadcrumbs_enabled();
    const esp_err_t result = breadcrumbs_set_enabled( !old_value );
    if( ESP_OK != result )
    {
        save_error = true;
        emit_device_event( RFSCOUT_EVENT_ERROR, (int32_t)result );
        return;
    }
    log_config_change( "breadcrumbs.enabled", on_off( old_value ), on_off( !old_value ) );
    save_error = false;
    emit_device_event( !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                       !old_value ? 1 : 0 );
}

static void adjust_breadcrumb_file_limit( int a_delta )
{
    const uint16_t old_limit = breadcrumbs_file_limit();
    const uint16_t new_limit = next_timeout_option(
        BREADCRUMB_FILE_LIMIT_OPTIONS,
        sizeof( BREADCRUMB_FILE_LIMIT_OPTIONS ) / sizeof( BREADCRUMB_FILE_LIMIT_OPTIONS[ 0 ] ),
        old_limit,
        a_delta );
    if( new_limit == old_limit )
    {
        return;
    }

    const esp_err_t result = breadcrumbs_set_file_limit( new_limit );
    if( ESP_OK != result )
    {
        save_error = true;
        emit_device_event( RFSCOUT_EVENT_ERROR, (int32_t)result );
        return;
    }

    char old_text[ 8 ];
    char new_text[ 8 ];
    snprintf( old_text, sizeof( old_text ), "%u", (unsigned)old_limit );
    snprintf( new_text, sizeof( new_text ), "%u", (unsigned)new_limit );
    log_config_change( "breadcrumbs.file_limit", old_text, new_text );
    save_error = false;
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

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
    if( (rfscout_alert_mode_t)mode != old_mode &&
        ( ESP_OK == device_config_set_alert_mode( (rfscout_alert_mode_t)mode ) ) )
    {
        log_config_change( "notifications.mode",
                           alert_mode_name( old_mode ),
                           alert_mode_name( (rfscout_alert_mode_t)mode ) );
        emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void adjust_notification_character( unsigned a_item, int a_delta )
{
    const rfscout_notification_type_t type = notification_for_item( a_item );
    const char old_character = device_config_notification_character( type );
    const char new_character = next_morse_character( old_character, a_delta );
    if( ( new_character == old_character ) ||
        ( ESP_OK != device_config_set_notification_character( type, new_character ) ) )
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
    emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
}

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
            emit_device_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
        }
    }
    else if( DEVICE_ITEM_DIM_TIMEOUT == selected_item )
    {
        adjust_display_timeout( false, a_delta );
    }
    else if( DEVICE_ITEM_OFF_TIMEOUT == selected_item )
    {
        adjust_display_timeout( true, a_delta );
    }
    else if( DEVICE_ITEM_THEME == selected_item )
    {
        adjust_theme( a_delta );
    }
    else if( DEVICE_ITEM_AUDIO_POLICY == selected_item )
    {
        adjust_audio_policy( a_delta );
    }
    else if( DEVICE_ITEM_UI_VOLUME == selected_item )
    {
        adjust_audio_volume( RFSCOUT_AUDIO_CATEGORY_UI, a_delta );
    }
    else if( DEVICE_ITEM_EVENT_VOLUME == selected_item )
    {
        adjust_audio_volume( RFSCOUT_AUDIO_CATEGORY_EVENT, a_delta );
    }
    else if( DEVICE_ITEM_ALERT_MODE == selected_item )
    {
        adjust_alert_mode( a_delta );
    }
    else if( DEVICE_ITEM_ALERT_VOLUME == selected_item )
    {
        adjust_audio_volume( RFSCOUT_AUDIO_CATEGORY_ALERT, a_delta );
    }
    else if( DEVICE_ITEM_CLOCK_FORMAT == selected_item )
    {
        const bool old_value = config->clock_24h;
        if( ESP_OK == device_config_set_clock_24h( !old_value ) )
        {
            log_config_change( "display.clock_24h", on_off( old_value ), on_off( !old_value ) );
            emit_device_event( !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                               !old_value ? 1 : 0 );
        }
    }
    else if( DEVICE_ITEM_STARTUP == selected_item )
    {
        adjust_startup_app( a_delta );
    }
    else if( DEVICE_ITEM_SYSTEM_EVENTS == selected_item )
    {
        toggle_app( RFSCOUT_APP_SYSTEM_EVENTS, "apps.system_events" );
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
    else if( DEVICE_ITEM_AUDIO_SPECTRUM == selected_item )
    {
        toggle_app( RFSCOUT_APP_AUDIO_SPECTRUM, "apps.audio_spectrum" );
    }
    else if( DEVICE_ITEM_LOGGING == selected_item )
    {
        toggle_logging();
    }
    else if( DEVICE_ITEM_BREADCRUMBS == selected_item )
    {
        toggle_breadcrumbs();
    }
    else if( DEVICE_ITEM_BREADCRUMB_FILES == selected_item )
    {
        adjust_breadcrumb_file_limit( a_delta );
    }
    else if( ( selected_item >= DEVICE_ITEM_NOTIFICATION_FIRST ) &&
             ( selected_item <= DEVICE_ITEM_NOTIFICATION_LAST ) )
    {
        adjust_notification_character( selected_item, a_delta );
    }
}

bool device_view_back( void )
{
    if( ota_menu_open )
    {
        if( ota_menu_back() )
        {
            ota_menu_leave();
            ota_menu_open = false;
            device_view_render();
        }
        return true;
    }
    if( time_menu_open )
    {
        time_menu_open = false;
        time_selected_item = TIME_MENU_BACK;
        time_editing = false;
        device_view_render();
        return true;
    }
    if( power_policy_menu_open )
    {
        power_policy_menu_open = false;
        device_view_render();
        return true;
    }
    if( !power_menu_open )
    {
        return false;
    }

    power_menu_open = false;
    power_selected_item = POWER_MENU_BACK;
    device_view_render();
    return true;
}

bool device_view_input( bool a_pressed, int a_delta )
{
    if( ota_menu_open )
    {
        if( ota_menu_input( a_pressed, a_delta ) )
        {
            ota_menu_leave();
            ota_menu_open = false;
            emit_device_event( RFSCOUT_EVENT_BACK, 0 );
            device_view_render();
        }
        return false;
    }
    if( time_menu_open )
    {
        (void)time_menu_input( a_pressed, a_delta );
        return false;
    }
    if( power_policy_menu_open )
    {
        if( power_policy_view_input( a_pressed, a_delta ) )
        {
            power_policy_menu_open = false;
            device_view_render();
        }
        return false;
    }

    if( power_menu_open )
    {
        if( a_pressed )
        {
            if( POWER_MENU_BACK == power_selected_item )
            {
                power_menu_open = false;
                emit_device_event( RFSCOUT_EVENT_BACK, 0 );
            }
            else
            {
                cycle_power_action( POWER_MENU_LONG_PRESS == power_selected_item );
            }
            device_view_render();
            return false;
        }
        if( 0 != a_delta )
        {
            move_power_selection( a_delta );
            device_view_render();
        }
        return false;
    }

    if( a_pressed )
    {
        if( ( DEVICE_ITEM_BACK == selected_item ) && !editing )
        {
            emit_device_event( RFSCOUT_EVENT_BACK, 0 );
            return true;
        }
        if( ( DEVICE_ITEM_OTA == selected_item ) && !editing )
        {
            ota_menu_open = true;
            emit_device_event( RFSCOUT_EVENT_ENTER, 1 );
            ota_menu_enter();
            return false;
        }
        if( ( DEVICE_ITEM_TIME == selected_item ) && !editing )
        {
            time_menu_open = true;
            time_selected_item = TIME_MENU_BACK;
            time_editing = false;
            refresh_edited_time();
            emit_device_event( RFSCOUT_EVENT_ENTER, 1 );
            render_time_menu();
            return false;
        }
        if( ( DEVICE_ITEM_POWER_BUTTON == selected_item ) && !editing )
        {
            power_menu_open = true;
            power_selected_item = POWER_MENU_BACK;
            emit_device_event( RFSCOUT_EVENT_ENTER, 1 );
            device_view_render();
            return false;
        }
        if( ( DEVICE_ITEM_POWER_POLICY == selected_item ) && !editing )
        {
            power_policy_menu_open = true;
            emit_device_event( RFSCOUT_EVENT_ENTER, 1 );
            power_policy_view_enter();
            return false;
        }
        if( !editing && item_direct_toggle( selected_item ) )
        {
            adjust_selected( 1 );
            device_view_render();
            return false;
        }

        if( item_editable( selected_item ) )
        {
            editing = !editing;
            emit_device_event( editing ? RFSCOUT_EVENT_ENTER : RFSCOUT_EVENT_ACTIVATE,
                               editing ? 1 : 0 );
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


void device_view_poll( int64_t a_now_us )
{
    if( ota_menu_open )
    {
        ota_menu_poll( a_now_us );
    }
}

void device_view_leave( void )
{
    if( ota_menu_open )
    {
        ota_menu_leave();
        ota_menu_open = false;
    }
}
