/**
    power_policy_view.c : RFScout protective power policy settings view
    Description: Configures low-battery and high-temperature thresholds and semantic reactions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "power_policy_view.h"

#include "app_manager.h"
#include "device_config.h"
#include "display.h"
#include "event.h"
#include "log_writer.h"
#include "power.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_menu.h"

#include <stdio.h>

#define POLICY_ITEM_BACK 0U
#define POLICY_ITEM_LOW_BATTERY_THRESHOLD 1U
#define POLICY_ITEM_LOW_BATTERY_ACTION 2U
#define POLICY_ITEM_HIGH_TEMPERATURE_THRESHOLD 3U
#define POLICY_ITEM_HIGH_TEMPERATURE_ACTION 4U
#define POLICY_ITEM_COUNT 5U

static const uint8_t LOW_BATTERY_OPTIONS[] = { 0U, 5U, 10U, 15U, 20U };
static const uint8_t HIGH_TEMPERATURE_OPTIONS[] = { 0U, 70U, 80U, 90U, 100U };

static unsigned selected_item;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_DEVICE, a_value );
}

static void log_change( const char *a_key, const char *a_old, const char *a_new )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "config" ) && log_str( &record, "key", a_key ) &&
        log_str( &record, "old", a_old ) && log_str( &record, "new", a_new ) )
    {
        (void)log_end( &record );
    }
}

static uint8_t next_option( const uint8_t *a_options, size_t a_count, uint8_t a_current )
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
    return a_options[ ( index + 1U ) % a_count ];
}

static power_action_t next_policy_action( power_action_t a_current )
{
    power_action_t candidate = a_current;
    for( unsigned attempt = 0U; attempt < POWER_ACTION_COUNT; ++attempt )
    {
        candidate = (power_action_t)( ( (unsigned)candidate + 1U ) % (unsigned)POWER_ACTION_COUNT );
        if( ( POWER_ACTION_SCREEN_OFF != candidate ) && power_action_available( candidate ) )
        {
            return candidate;
        }
    }
    return POWER_ACTION_NONE;
}

static void
format_threshold( uint8_t a_value, bool a_temperature, char *a_output, size_t a_capacity )
{
    if( 0U == a_value )
    {
        snprintf( a_output, a_capacity, "Off" );
    }
    else if( a_temperature )
    {
        snprintf( a_output, a_capacity, "%u C", (unsigned)a_value );
    }
    else
    {
        snprintf( a_output, a_capacity, "%u%%", (unsigned)a_value );
    }
}

static void draw_row( int a_y, unsigned a_item, const char *a_label, const char *a_value )
{
    const bool active = selected_item == a_item;
    const ui_menu_state_t state = { .selected = active, .available = true };
    const uint16_t color = ui_menu_label_color( state );
    if( active )
    {
        ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X, a_y, ">", color );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, a_label, color, 1U );
    if( NULL != a_value )
    {
        display_draw_text( 214, a_y, a_value, ui_menu_value_color( state ), 1U );
    }
}

void power_policy_view_render( void )
{
    const device_config_t *config = device_config_get();
    const ui_theme_palette_t *theme = ui_theme_get();
    char low_threshold[ 12 ];
    char high_threshold[ 12 ];
    format_threshold(
        config->low_battery_threshold_percent, false, low_threshold, sizeof( low_threshold ) );
    format_threshold(
        config->high_temperature_threshold_c, true, high_threshold, sizeof( high_threshold ) );

    display_clear( theme->background );
    ui_chrome_draw( "POWER POLICY", theme->secondary );
    draw_row( UI_LAYOUT_MENU_START_Y, POLICY_ITEM_BACK, "Back", NULL );
    draw_row( UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
              POLICY_ITEM_LOW_BATTERY_THRESHOLD,
              "Low battery",
              low_threshold );
    draw_row( UI_LAYOUT_MENU_START_Y + 2 * UI_LAYOUT_MENU_ROW_HEIGHT,
              POLICY_ITEM_LOW_BATTERY_ACTION,
              "Low batt action",
              power_action_name( config->low_battery_action ) );
    draw_row( UI_LAYOUT_MENU_START_Y + 3 * UI_LAYOUT_MENU_ROW_HEIGHT,
              POLICY_ITEM_HIGH_TEMPERATURE_THRESHOLD,
              "High temp",
              high_threshold );
    draw_row( UI_LAYOUT_MENU_START_Y + 4 * UI_LAYOUT_MENU_ROW_HEIGHT,
              POLICY_ITEM_HIGH_TEMPERATURE_ACTION,
              "High temp action",
              power_action_name( config->high_temperature_action ) );

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       ( POLICY_ITEM_BACK == selected_item ) ? "TURN SELECT / CENTER BACK"
                                                             : "TURN SELECT / CENTER CYCLE",
                       theme->secondary,
                       1U );
}

void power_policy_view_enter( void )
{
    selected_item = POLICY_ITEM_BACK;
    power_policy_view_render();
}

static void cycle_low_battery_threshold( void )
{
    const uint8_t old_value = device_config_get()->low_battery_threshold_percent;
    const uint8_t new_value =
        next_option( LOW_BATTERY_OPTIONS,
                     sizeof( LOW_BATTERY_OPTIONS ) / sizeof( LOW_BATTERY_OPTIONS[ 0 ] ),
                     old_value );
    if( ESP_OK == device_config_set_low_battery_threshold( new_value ) )
    {
        char old_text[ 12 ];
        char new_text[ 12 ];
        format_threshold( old_value, false, old_text, sizeof( old_text ) );
        format_threshold( new_value, false, new_text, sizeof( new_text ) );
        log_change( "power.low_battery_percent", old_text, new_text );
        emit_event( RFSCOUT_EVENT_NAVIGATE, 1 );
    }
}

static void cycle_high_temperature_threshold( void )
{
    const uint8_t old_value = device_config_get()->high_temperature_threshold_c;
    const uint8_t new_value =
        next_option( HIGH_TEMPERATURE_OPTIONS,
                     sizeof( HIGH_TEMPERATURE_OPTIONS ) / sizeof( HIGH_TEMPERATURE_OPTIONS[ 0 ] ),
                     old_value );
    if( ESP_OK == device_config_set_high_temperature_threshold( new_value ) )
    {
        char old_text[ 12 ];
        char new_text[ 12 ];
        format_threshold( old_value, true, old_text, sizeof( old_text ) );
        format_threshold( new_value, true, new_text, sizeof( new_text ) );
        log_change( "power.high_temperature_c", old_text, new_text );
        emit_event( RFSCOUT_EVENT_NAVIGATE, 1 );
    }
}

static void cycle_action( bool a_high_temperature )
{
    const device_config_t *config = device_config_get();
    const power_action_t old_action =
        a_high_temperature ? config->high_temperature_action : config->low_battery_action;
    const power_action_t new_action = next_policy_action( old_action );
    const esp_err_t result = a_high_temperature
                                 ? device_config_set_high_temperature_action( new_action )
                                 : device_config_set_low_battery_action( new_action );
    if( ESP_OK == result )
    {
        log_change( a_high_temperature ? "power.high_temperature_action"
                                       : "power.low_battery_action",
                    power_action_name( old_action ),
                    power_action_name( new_action ) );
        emit_event( RFSCOUT_EVENT_NAVIGATE, 1 );
    }
}

bool power_policy_view_input( bool a_pressed, int a_delta )
{
    if( a_pressed )
    {
        switch( selected_item )
        {
            case POLICY_ITEM_BACK:
                emit_event( RFSCOUT_EVENT_BACK, 0 );
                return true;
            case POLICY_ITEM_LOW_BATTERY_THRESHOLD:
                cycle_low_battery_threshold();
                break;
            case POLICY_ITEM_LOW_BATTERY_ACTION:
                cycle_action( false );
                break;
            case POLICY_ITEM_HIGH_TEMPERATURE_THRESHOLD:
                cycle_high_temperature_threshold();
                break;
            case POLICY_ITEM_HIGH_TEMPERATURE_ACTION:
                cycle_action( true );
                break;
            default:
                break;
        }
        power_policy_view_render();
        return false;
    }

    if( 0 == a_delta )
    {
        return false;
    }
    int item = (int)selected_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)POLICY_ITEM_COUNT - 1;
    }
    if( item >= (int)POLICY_ITEM_COUNT )
    {
        item = 0;
    }
    selected_item = (unsigned)item;
    emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    power_policy_view_render();
    return false;
}
