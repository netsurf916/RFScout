/**
    apps/battery/app.c : Read-only battery diagnostics application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "battery.h"
#include "device_config.h"
#include "display.h"
#include "esp_timer.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdio.h>

#define BATTERY_REFRESH_INTERVAL_US BATTERY_TELEMETRY_INTERVAL_US
#define BATTERY_HISTORY_WINDOW_US \
    ( ( BATTERY_HISTORY_CAPACITY - 1U ) * BATTERY_TELEMETRY_INTERVAL_US )

static int64_t last_refresh_us;
static int64_t last_graph_sample_us;
static rfscout_style_id_t last_graph_style;
static rfscout_colors_id_t last_graph_colors;
/* This larger view buffer is static rather than a 2.9 KB local stack array. */
static battery_history_sample_t history_view_samples[ BATTERY_HISTORY_CAPACITY ];
static uint8_t battery_page;

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

static void draw_history( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const size_t count = battery_history_read( history_view_samples, BATTERY_HISTORY_CAPACITY );
    last_graph_sample_us = 0U == count ? -1 : history_view_samples[ count - 1U ].timestamp_us;
    last_graph_style = ui_style_current();
    last_graph_colors = ui_colors_current();
    char text[ 64 ];
    draw_panel( 4, UI_LAYOUT_PANEL_TOP_Y, 312, 140 );
    display_draw_text( 10, 30, "BATTERY CURRENT / 6-MIN HISTORY", theme->secondary, 1U );
    if( 0U == count )
    {
        display_draw_text( 10, 85, "WAITING FOR FRESH SAMPLES", theme->warning, 1U );
        return;
    }
    /* The newest measured sample, not wall-clock time, anchors the plot.
       X positions remain fixed between actual samples; no visual drift. */
    const int64_t latest_us = last_graph_sample_us;
    int min_ma = 0;
    int max_ma = 0;
    int64_t total_ma = 0;
    size_t visible_count = 0U;
    for( size_t i = 0U; i < count; i++ )
    {
        const int64_t age_us = latest_us - history_view_samples[ i ].timestamp_us;
        if( age_us < 0 || age_us > BATTERY_HISTORY_WINDOW_US )
        {
            continue;
        }
        const int current = history_view_samples[ i ].current_ma;
        if( current < min_ma )
        {
            min_ma = current;
        }
        if( current > max_ma )
        {
            max_ma = current;
        }
        total_ma += current;
        visible_count++;
    }
    if( 0U == visible_count )
    {
        display_draw_text( 10, 85, "NO RECENT BATTERY SAMPLES", theme->warning, 1U );
        return;
    }
    /* Round outward to meaningful 50 mA divisions, always including zero. */
    const int step = 50;
    min_ma = -( ( -min_ma + step - 1 ) / step ) * step;
    max_ma = ( ( max_ma + step - 1 ) / step ) * step;
    if( min_ma == max_ma )
    {
        max_ma += step;
    }
    const int x0 = 38;
    const int y0 = 55;
    const int graph_h = 76;
    const int graph_w = 266;
    const int range = max_ma - min_ma;
    const int zero_y = y0 + ( max_ma * graph_h ) / range;
    display_fill_rect( x0, zero_y, graph_w, 1, theme->divider );
    snprintf( text, sizeof( text ), "%d", max_ma );
    display_draw_text( 8, y0, text, theme->secondary, 1U );
    snprintf( text, sizeof( text ), "%d", min_ma );
    display_draw_text( 8, y0 + graph_h - 8, text, theme->secondary, 1U );
    for( size_t i = 0U; i < count; i++ )
    {
        const int64_t age_us = latest_us - history_view_samples[ i ].timestamp_us;
        if( age_us < 0 || age_us > BATTERY_HISTORY_WINDOW_US )
        {
            continue;
        }
        const int x =
            x0 + graph_w - 1 - (int)( age_us * ( graph_w - 1 ) / BATTERY_HISTORY_WINDOW_US );
        const int y = y0 + ( ( max_ma - history_view_samples[ i ].current_ma ) * graph_h ) / range;
        const int top = y < zero_y ? y : zero_y;
        const int height = y < zero_y ? zero_y - y : y - zero_y;
        if( 0 != history_view_samples[ i ].current_ma )
        {
            /* Match radio/spectrum graph geometry and palette in both styles. */
            ui_game_draw_column( x,
                                 top,
                                 1,
                                 height > 0 ? height : 1,
                                 history_view_samples[ i ].current_ma < 0 ? theme->graph_peak
                                                                          : theme->graph_primary );
        }
    }
    snprintf( text,
              sizeof( text ),
              "AVG %+lldmA  LAST %+dmA",
              (long long)( total_ma / (int64_t)visible_count ),
              (int)history_view_samples[ count - 1U ].current_ma );
    display_draw_text( 10, 137, text, theme->text, 1U );
    display_draw_text( 10, 151, "-6 MIN                     LAST", theme->secondary, 1U );
}

static const char *gauge_config_label( battery_gauge_config_result_t a_result )
{
    switch( a_result )
    {
        case BATTERY_GAUGE_CONFIG_PENDING:
            return "PENDING";
        case BATTERY_GAUGE_CONFIGURING:
            return "CONFIGURING";
        case BATTERY_GAUGE_CONFIG_VERIFIED:
            return "VERIFIED";
        case BATTERY_GAUGE_CONFIG_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

static void draw_gauge_configuration( const battery_status_t *a_status )
{
    battery_gauge_configuration_t config = { 0 };
    const bool available = battery_get_gauge_configuration( &config );
    const ui_theme_palette_t *theme = ui_theme_get();
    char text[ 64 ];

    draw_panel( 4, UI_LAYOUT_PANEL_TOP_Y, 312, 140 );
    display_draw_text( 10, 30, "GAUGE CONFIGURATION / PROFILE", theme->secondary, 1U );
    snprintf( text,
              sizeof( text ),
              "STATE %s  STEP %s",
              gauge_config_label( config.result ),
              config.stage ? config.stage : "NONE" );
    display_draw_text( 10,
                       48,
                       text,
                       BATTERY_GAUGE_CONFIG_FAILED == config.result ? theme->error : theme->text,
                       1U );

    if( config.profile_read )
    {
        snprintf( text,
                  sizeof( text ),
                  "PROFILE FCC %u  DESIGN %u mAh",
                  (unsigned)config.profile_full_capacity_mah,
                  (unsigned)config.profile_design_capacity_mah );
        display_draw_text( 10, 66, text, theme->accent, 1U );
    }
    else
    {
        display_draw_text(
            10, 66, available ? "PROFILE NOT YET READ" : "GAUGE UNAVAILABLE", theme->warning, 1U );
    }

    if( a_status->gauge_details_fresh )
    {
        snprintf( text,
                  sizeof( text ),
                  "LIVE FCC %u  DESIGN %u mAh",
                  (unsigned)a_status->full_capacity_mah,
                  (unsigned)a_status->design_capacity_mah );
        const bool mismatch = BATTERY_GAUGE_CONFIG_VERIFIED == config.result &&
                              config.profile_read &&
                              config.profile_design_capacity_mah != a_status->design_capacity_mah;
        display_draw_text( 10, 84, text, mismatch ? theme->warning : theme->text, 1U );
        if( mismatch )
        {
            display_draw_text( 10, 104, "PROFILE / LIVE DESIGN MISMATCH", theme->warning, 1U );
        }
    }
    else
    {
        display_draw_text( 10, 84, "LIVE CAPACITY UNAVAILABLE", theme->warning, 1U );
    }
    if( BATTERY_GAUGE_CONFIG_FAILED == config.result )
    {
        snprintf( text, sizeof( text ), "LAST ERROR %s", esp_err_to_name( config.error ) );
        display_draw_text( 10, 123, text, theme->error, 1U );
    }
    display_draw_text( 10, 149, "SELECT: BATTERY SUMMARY", theme->secondary, 1U );
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

    if( 3U == battery_page )
    {
        draw_gauge_configuration( &status );
        (void)display_present();
        return;
    }
    if( 2U == battery_page )
    {
        draw_history();
        (void)display_present();
        return;
    }
    if( 1U == battery_page )
    {
        draw_panel( 4, UI_LAYOUT_PANEL_TOP_Y, 312, 140 );
        display_draw_text( 10, 30, "CHARGER DIAGNOSTICS", theme->secondary, 1U );
        if( status.charger_details_fresh )
        {
            snprintf( text,
                      sizeof( text ),
                      "PHASE %u  INPUT %u  PG %u",
                      status.charger_phase,
                      status.charger_input_source,
                      status.charger_power_good ? 1U : 0U );
            display_draw_text( 10, 48, text, theme->text, 1U );
            snprintf( text,
                      sizeof( text ),
                      "IIN %u mA  ICHG %u mA",
                      status.charger_input_limit_ma,
                      status.charger_fast_charge_limit_ma );
            display_draw_text( 10, 64, text, theme->text, 1U );
            snprintf( text,
                      sizeof( text ),
                      "VREG %u mV  ITERM %u mA",
                      status.charger_regulation_mv,
                      status.charger_termination_ma );
            display_draw_text( 10, 80, text, theme->text, 1U );
            if( status.charger_adc_fresh )
            {
                snprintf( text,
                          sizeof( text ),
                          "VBUS %u mV  IADC %u mA",
                          (unsigned)status.charger_vbus_mv,
                          (unsigned)status.charger_adc_current_ma );
                display_draw_text( 10, 96, text, theme->text, 1U );
                snprintf( text,
                          sizeof( text ),
                          "VSYS %u mV  DPM %02X",
                          (unsigned)status.charger_system_mv,
                          (unsigned)status.charger_dpm_status );
            }
            else
            {
                display_draw_text( 10, 96, "ADC DATA UNAVAILABLE", theme->warning, 1U );
                snprintf( text, sizeof( text ), "DPM %02X", (unsigned)status.charger_dpm_status );
            }
            display_draw_text( 10, 112, text, theme->text, 1U );
            snprintf( text,
                      sizeof( text ),
                      "FAULT %02X  CTRL %u  WD %u",
                      (unsigned)status.charger_faults,
                      (unsigned)status.charger_controller_state,
                      status.charger_watchdog_enabled ? 1U : 0U );
            display_draw_text(
                10, 128, text, status.charger_faults ? theme->warning : theme->text, 1U );
        }
        else
        {
            display_draw_text( 10, 65, "CHARGER DATA UNAVAILABLE", theme->warning, 1U );
        }
        display_draw_text( 10, 149, "SELECT: CURRENT GRAPH", theme->secondary, 1U );
        (void)display_present();
        return;
    }

    /* Single managed content surface, below platform-owned chrome. */
    draw_panel( 4, UI_LAYOUT_PANEL_TOP_Y, 312, 48 );
    draw_panel( 4, 76, 152, 88 );
    draw_panel( 160, 76, 156, 88 );

    if( status.available )
    {
        snprintf( text, sizeof( text ), "%u%%", (unsigned)status.percentage );
    }
    else
    {
        snprintf( text, sizeof( text ), "--%%" );
    }
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
        ui_game_draw_meter( x + 3,
                            y + 3,
                            width - 6,
                            height - 6,
                            status.percentage,
                            100U,
                            status.percentage_fresh ? theme->success : theme->warning );
    }
    if( NULL != config && status.charger_fresh && 0U != config->low_battery_threshold_percent &&
        POWER_ACTION_NONE != config->low_battery_action &&
        config->low_battery_threshold_percent <= RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT )
    {
        const int marker =
            x + 3 + ( (int)config->low_battery_threshold_percent * ( width - 6 ) ) / 100;
        display_fill_rect( marker, y - 3, 2, height + 6, theme->warning );
    }
    snprintf( text,
              sizeof( text ),
              "%s / %s",
              charge_label( status.charge_state ),
              status.charger_fresh ? ( status.external_power ? "USB" : "BAT" ) : "POWER ?" );
    display_draw_text( 10, 60, text, status.charger_fresh ? theme->text : theme->warning, 1U );
    if( status.charger_soc_disagreement )
    {
        display_draw_text( 166, 60, "SOC / CHG MISMATCH", theme->warning, 1U );
    }

    display_draw_text( 10, 81, "GAUGE / HEALTH", theme->secondary, 1U );
    if( status.electrical_fresh )
    {
        snprintf( text,
                  sizeof( text ),
                  "%u mV  %+d mA",
                  (unsigned)status.voltage_mv,
                  (int)status.current_ma );
        display_draw_text( 10, 97, text, theme->text, 1U );
    }
    else
    {
        display_draw_text( 10, 97, "TELEMETRY STALE", theme->warning, 1U );
    }
    if( status.gauge_details_fresh )
    {
        snprintf( text,
                  sizeof( text ),
                  "SOH %u%%  CYC %u",
                  (unsigned)( status.state_of_health_percent & 0xFFU ),
                  (unsigned)status.cycle_count );
        display_draw_text( 10, 115, text, theme->accent, 1U );
        snprintf( text,
                  sizeof( text ),
                  "REM %u  FULL %u",
                  (unsigned)status.remaining_capacity_mah,
                  (unsigned)status.full_capacity_mah );
        display_draw_text( 10, 133, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "DESIGN %u mAh", (unsigned)status.design_capacity_mah );
        display_draw_text( 10, 151, text, theme->secondary, 1U );
    }
    else
    {
        display_draw_text( 10, 119, "HEALTH UNAVAILABLE", theme->warning, 1U );
    }

    display_draw_text( 166, 81, "CHARGER / LIMITS", theme->secondary, 1U );
    if( status.charger_details_fresh )
    {
        snprintf( text,
                  sizeof( text ),
                  "FAULT %02X  PG %u",
                  (unsigned)status.charger_faults,
                  status.charger_power_good ? 1U : 0U );
        display_draw_text( 166, 97, text, status.charger_faults ? theme->error : theme->text, 1U );
        snprintf( text,
                  sizeof( text ),
                  "IIN %u  CHG %u",
                  (unsigned)status.charger_input_limit_ma,
                  (unsigned)status.charger_fast_charge_limit_ma );
        display_draw_text( 166, 115, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "VREG %u", (unsigned)status.charger_regulation_mv );
        display_draw_text( 166, 133, text, theme->text, 1U );
        snprintf( text, sizeof( text ), "TERM %u mA", (unsigned)status.charger_termination_ma );
        display_draw_text( 166, 151, text, theme->secondary, 1U );
    }
    else
    {
        display_draw_text( 166, 105, "DATA UNAVAILABLE", theme->warning, 1U );
    }
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
        last_refresh_us = a_event->now_us;
        last_graph_sample_us = -1;
        battery_page = 0U;
        battery_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( 2U == battery_page )
        {
            battery_history_sample_t newest;
            const size_t count = battery_history_read( &newest, 1U );
            const int64_t latest_us = 0U == count ? -1 : newest.timestamp_us;
            if( latest_us != last_graph_sample_us ||
                last_graph_style != ui_style_current() ||
                last_graph_colors != ui_colors_current() )
            {
                last_refresh_us = a_event->now_us;
                battery_render();
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
            return RFSCOUT_APP_RUNTIME_IGNORED;
        }
        if( a_event->now_us - last_refresh_us >= BATTERY_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            battery_render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type && RFSCOUT_INPUT_SELECT == a_event->input.type )
    {
        battery_page = (uint8_t)( ( battery_page + 1U ) % 4U );
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
