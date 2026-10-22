/**
    apps/battery/app.c : Read-only battery diagnostics application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "battery.h"
#include "device_config.h"
#include "display.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdio.h>

#define BATTERY_REFRESH_INTERVAL_US 1000000LL

static int64_t last_refresh_us;

static const char *charge_label( battery_charge_state_t a_state )
{
    switch( a_state )
    {
        case BATTERY_CHARGE_CHARGING:
            return "CHARGING";
        case BATTERY_CHARGE_COMPLETE:
            return "COMPLETE";
        case BATTERY_CHARGE_DISCHARGING:
            return "DISCHARGING";
        default:
            return "UNKNOWN";
    }
}

/* Match the established GPS/Thermal dashboard panel treatment. */
static void draw_panel( int a_x, int a_y, int a_width, int a_height )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ui_game_active() )
    {
        ui_game_draw_panel( a_x, a_y, a_width, a_height, theme->divider );
        return;
    }
    display_fill_rect( a_x, a_y, a_width, a_height, theme->surface );
    display_fill_rect( a_x, a_y, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y + a_height - 1, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y, 1, a_height, theme->divider );
    display_fill_rect( a_x + a_width - 1, a_y, 1, a_height, theme->divider );
}

static void battery_render( void )
{
    battery_poll();
    battery_status_t status = { 0 };
    (void)battery_get_status( &status );
    const ui_theme_palette_t *theme = ui_theme_get();
    const device_config_t *config = device_config_get();
    char text[ 64 ];

    display_clear( theme->background );
    ui_chrome_draw( "BATTERY", theme->secondary );

    /* Single managed content surface, below platform-owned chrome. */
    draw_panel( 4, UI_LAYOUT_PANEL_TOP_Y, 312, 48 );
    draw_panel( 4, 76, 152, 88 );
    draw_panel( 160, 76, 156, 88 );

    snprintf( text, sizeof( text ), status.available ? "%u%%" : "--%%",
              (unsigned)status.percentage );
    display_draw_text( 10, 33, text, status.percentage_fresh ? theme->accent : theme->warning, 2U );

    const int x = 108;
    const int y = 33;
    const int width = 195;
    const int height = 23;
    display_fill_rect( x, y, width, 1, theme->text );
    display_fill_rect( x, y + height - 1, width, 1, theme->text );
    display_fill_rect( x, y, 1, height, theme->text );
    display_fill_rect( x + width - 1, y, 1, height, theme->text );
    display_fill_rect( x + width, y + 7, 5, 9, theme->text );
    if( status.available )
    {
        const int fill = ( (int)status.percentage * ( width - 6 ) ) / 100;
        display_fill_rect( x + 3, y + 3, fill, height - 6,
                           status.percentage_fresh ? theme->success : theme->warning );
    }
    if( NULL != config && POWER_ACTION_NONE != config->low_battery_action &&
        config->low_battery_threshold_percent <= RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT )
    {
        const int marker = x + 3 +
            ( (int)config->low_battery_threshold_percent * ( width - 6 ) ) / 100;
        display_fill_rect( marker, y - 3, 2, height + 6, theme->warning );
    }
    snprintf( text, sizeof( text ), "%s / %s", charge_label( status.charge_state ),
              status.charger_fresh ? ( status.external_power ? "USB" : "BAT" ) : "POWER ?" );
    display_draw_text( 10, 60, text, status.charger_fresh ? theme->text : theme->warning, 1U );

    display_draw_text( 10, 81, "GAUGE / HEALTH", theme->secondary, 1U );
    if( status.percentage_fresh )
    {
        snprintf( text, sizeof( text ), "%u mV  %+d mA",
                  (unsigned)status.voltage_mv, (int)status.current_ma );
        display_draw_text( 10, 97, text, theme->text, 1U );
    }
    else
    {
        display_draw_text( 10, 97, "TELEMETRY STALE", theme->warning, 1U );
    }
    if( status.gauge_details_fresh )
    {
        snprintf( text, sizeof( text ), "SOH %u%%  CYC %u",
                  (unsigned)( status.state_of_health_percent & 0xFFU ),
                  (unsigned)status.cycle_count );
        display_draw_text( 10, 115, text, theme->accent, 1U );
        snprintf( text, sizeof( text ), "REM %u  FULL %u",
                  (unsigned)status.remaining_capacity_mah,
                  (unsigned)status.full_capacity_mah );
        display_draw_text( 10, 133, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "DESIGN %u mAh",
                  (unsigned)status.design_capacity_mah );
        display_draw_text( 10, 151, text, theme->secondary, 1U );
    }
    else
    {
        display_draw_text( 10, 119, "HEALTH UNAVAILABLE", theme->warning, 1U );
    }

    display_draw_text( 166, 81, "CHARGER / LIMITS", theme->secondary, 1U );
    if( status.charger_details_fresh )
    {
        snprintf( text, sizeof( text ), "FAULT %02X  IN %02X",
                  (unsigned)status.charger_faults, (unsigned)status.charger_input_control );
        display_draw_text( 166, 97, text, status.charger_faults ? theme->error : theme->text, 1U );
        snprintf( text, sizeof( text ), "CHG %02X  CUR %02X",
                  (unsigned)status.charger_charge_control,
                  (unsigned)status.charger_current_control );
        display_draw_text( 166, 115, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "VREG %02X  TERM %02X",
                  (unsigned)status.charger_voltage_control,
                  (unsigned)status.charger_precharge_control );
        display_draw_text( 166, 133, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "TIMER %02X",
                  (unsigned)status.charger_timer_control );
        display_draw_text( 166, 151, text, theme->secondary, 1U );
    }
    else
    {
        display_draw_text( 166, 105, "DATA UNAVAILABLE", theme->warning, 1U );
    }
    (void)display_present();
}

static rfscout_app_runtime_result_t
battery_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = a_event->now_us;
        battery_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type &&
        a_event->now_us - last_refresh_us >= BATTERY_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_event->now_us;
        battery_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    return RFSCOUT_APP_RUNTIME_IGNORED;
}

const rfscout_app_descriptor_t rfscout_battery_app = {
    .id = RFSCOUT_APP_BATTERY,
    .name = "Battery",
    .persistence_key = "battery",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = battery_runtime,
};
