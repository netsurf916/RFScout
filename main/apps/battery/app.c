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
#include "ui_theme.h"

#include <stddef.h>
#include <stdio.h>

#define BATTERY_REFRESH_INTERVAL_US 1000000LL

static int64_t last_refresh_us;
static bool show_details;

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
        case BATTERY_CHARGE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

static void battery_render( void )
{
    battery_poll();
    battery_status_t status = { 0 };
    (void)battery_get_status( &status );
    const ui_theme_palette_t *theme = ui_theme_get();
    char line[ 64 ];
    display_clear( theme->background );
    ui_chrome_draw( show_details ? "BATTERY / STATUS" : "BATTERY / LEVEL", theme->secondary );

    if( show_details )
    {
        snprintf( line, sizeof( line ), "GAUGE: %s%s",
                  status.gauge_available ? "ONLINE" : "OFFLINE",
                  status.percentage_fresh ? "" : " (STALE)" );
        display_draw_text( 12, 43, line, status.percentage_fresh ? theme->text : theme->warning, 1U );
        snprintf( line, sizeof( line ), "CHARGER: %s%s",
                  status.charger_available ? "ONLINE" : "OFFLINE",
                  status.charger_fresh ? "" : " (STALE)" );
        display_draw_text( 12, 65, line, status.charger_fresh ? theme->text : theme->warning, 1U );
        snprintf( line, sizeof( line ), "PHASE: %s", charge_label( status.charge_state ) );
        display_draw_text( 12, 87, line, theme->accent, 1U );
        snprintf( line, sizeof( line ), "USB: %s",
                  status.charger_fresh ? ( status.external_power ? "PRESENT" : "ABSENT" ) : "UNKNOWN" );
        display_draw_text( 12, 109, line, theme->text, 1U );
    }
    else
    {
        if( status.available )
        {
            snprintf( line, sizeof( line ), "%u%%", (unsigned)status.percentage );
        }
        else
        {
            snprintf( line, sizeof( line ), "--%%" );
        }
        display_draw_text( 12, 42, line, status.percentage_fresh ? theme->accent : theme->warning, 2U );
        const int x = 105;
        const int y = 39;
        const int width = 185;
        const int height = 44;
        display_fill_rect( x, y, width, 2, theme->text );
        display_fill_rect( x, y + height - 2, width, 2, theme->text );
        display_fill_rect( x, y, 2, height, theme->text );
        display_fill_rect( x + width - 2, y, 2, height, theme->text );
        display_fill_rect( x + width, y + 14, 7, 16, theme->text );
        if( status.available )
        {
            const int fill = ( (int)status.percentage * ( width - 8 ) ) / 100;
            display_fill_rect( x + 4, y + 4, fill, height - 8,
                               status.percentage_fresh ? theme->success : theme->warning );
        }
        const device_config_t *config = device_config_get();
        if( NULL != config && config->low_battery_threshold_percent <= 100U )
        {
            const int marker = x + 4 +
                ( (int)config->low_battery_threshold_percent * ( width - 8 ) ) / 100;
            display_fill_rect( marker, y - 6, 2, height + 12, theme->warning );
            snprintf( line, sizeof( line ), "LOW %u%%",
                      (unsigned)config->low_battery_threshold_percent );
            display_draw_text( 105, 96, line, theme->warning, 1U );
        }
        snprintf( line, sizeof( line ), "%u mV / %d mA",
                  (unsigned)status.voltage_mv, (int)status.current_ma );
        display_draw_text( 12, 121, status.percentage_fresh ? line : "MEASUREMENT STALE",
                           status.percentage_fresh ? theme->text : theme->warning, 1U );
    }
    display_draw_text( 12, 151, "CENTER: DETAILS", theme->secondary, 1U );
    (void)display_present();
}

static rfscout_app_runtime_result_t battery_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        show_details = false;
        last_refresh_us = 0;
        battery_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            show_details = !show_details;
            battery_render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type &&
        a_event->now_us - last_refresh_us >= BATTERY_REFRESH_INTERVAL_US )
    {
        last_refresh_us = a_event->now_us;
        battery_render();
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
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
