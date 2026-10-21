/**
    apps/cc1101/app.c : CC1101/Sub-GHz application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_framework.h"
#include "app_manager.h"

#include "app_session.h"
#include "cc1101.h"
#include "cc1101_view.h"
#include "event.h"
#include "subghz_decode.h"

#include "esp_log.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CC1101_MENU_BACK 0U
#define CC1101_MENU_APP_LOCK 1U
#define CC1101_MENU_MODE 2U
#define CC1101_MENU_SCAN_PROFILE 3U
#define CC1101_MENU_RX_FREQUENCY 4U
#define CC1101_MENU_RX_PRESET 5U
#define CC1101_MENU_LOW 6U
#define CC1101_MENU_HIGH 7U
#define CC1101_MENU_STEP 8U
#define CC1101_MENU_ITEM_COUNT 9U
#define CC1101_DISCOVERY_LOCK_THRESHOLD_DBM ( -105 )

static const char *TAG = "cc1101_app";

static bool menu_active;
static bool menu_editing;
static bool menu_limit;
static unsigned menu_item;
static cc1101_view_mode_t viewer;
static bool rendered_app_lock;

static int8_t discovery_rssi[ CC1101_SPECTRUM_BINS ];
static uint32_t discovery_frequency_khz[ CC1101_SPECTRUM_BINS ];
static size_t discovery_count;
static uint32_t discovery_peak_khz;
static int8_t discovery_peak_rssi;
static bool discovery_peak_valid;

static rfscout_subghz_analysis_t latest_analysis;
static rfscout_subghz_symbol_t *raw_snapshot;
static size_t raw_snapshot_count;
static size_t raw_snapshot_capacity;
static uint32_t raw_snapshot_resolution_hz;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_CC1101, a_value );
}

static void clear_raw_snapshot( void )
{
    if( NULL != raw_snapshot )
    {
        (void)app_session_free( raw_snapshot );
    }
    raw_snapshot = NULL;
    raw_snapshot_count = 0U;
    raw_snapshot_capacity = 0U;
    raw_snapshot_resolution_hz = 0U;
}

static void reset_receive_results( void )
{
    memset( &latest_analysis, 0, sizeof( latest_analysis ) );
    clear_raw_snapshot();
}

static void render_discovery( void )
{
    cc1101_scan_config_t config;
    cc1101_scan_config( &config );
    cc1101_view_discovery( discovery_rssi,
                           discovery_count,
                           &config,
                           discovery_peak_khz,
                           discovery_peak_rssi,
                           cc1101_is_detected(),
                           app_manager_locked() );
}

static void render_receive( void )
{
    cc1101_receive_config_t config;
    cc1101_capture_stats_t stats;
    cc1101_receive_config( &config );
    cc1101_capture_stats( &stats );
    cc1101_view_receive( viewer,
                         &config,
                         &latest_analysis,
                         &stats,
                         raw_snapshot,
                         raw_snapshot_count,
                         raw_snapshot_resolution_hz,
                         app_manager_locked() );
}

static void render_current( void )
{
    rendered_app_lock = app_manager_locked();
    if( CC1101_ACQUISITION_RECEIVE == cc1101_acquisition_mode() )
    {
        render_receive();
    }
    else
    {
        render_discovery();
    }
}

static void render_menu( void )
{
    rendered_app_lock = app_manager_locked();
    cc1101_scan_config_t scan;
    cc1101_receive_config_t receive;
    cc1101_scan_config( &scan );
    cc1101_receive_config( &receive );
    cc1101_view_menu( menu_item,
                      menu_editing,
                      menu_limit,
                      app_manager_locked(),
                      cc1101_acquisition_mode(),
                      &scan,
                      &receive );
}

static void close_menu( void )
{
    menu_active = false;
    menu_editing = false;
    menu_limit = false;
    render_current();
}

static void update_discovery_peak( void )
{
    discovery_peak_valid = false;
    discovery_peak_rssi = INT8_MIN;
    discovery_peak_khz = 0U;

    for( size_t index = 0U; index < discovery_count; ++index )
    {
        if( discovery_rssi[ index ] > discovery_peak_rssi )
        {
            discovery_peak_rssi = discovery_rssi[ index ];
            discovery_peak_khz = discovery_frequency_khz[ index ];
        }
    }

    discovery_peak_valid = ( 0U != discovery_peak_khz ) &&
                           ( discovery_peak_rssi >= CC1101_DISCOVERY_LOCK_THRESHOLD_DBM );
}

static void consume_discovery_frame( bool a_render )
{
    discovery_count =
        cc1101_spectrum( discovery_rssi, discovery_frequency_khz, CC1101_SPECTRUM_BINS );
    update_discovery_peak();
    if( a_render )
    {
        render_discovery();
    }
}

static bool retain_raw_snapshot( const rfscout_subghz_capture_t *a_capture )
{
    if( ( NULL == a_capture ) || ( NULL == a_capture->symbols ) ||
        ( 0U == a_capture->symbol_count ) )
    {
        return false;
    }

    if( a_capture->symbol_count > raw_snapshot_capacity )
    {
        rfscout_subghz_symbol_t *replacement =
            app_session_resize( raw_snapshot, a_capture->symbol_count, sizeof( *raw_snapshot ) );
        if( NULL == replacement )
        {
            clear_raw_snapshot();
            return false;
        }
        raw_snapshot = replacement;
        raw_snapshot_capacity = a_capture->symbol_count;
    }

    memcpy( raw_snapshot, a_capture->symbols, a_capture->symbol_count * sizeof( *raw_snapshot ) );
    raw_snapshot_count = a_capture->symbol_count;
    raw_snapshot_resolution_hz = a_capture->resolution_hz;
    return true;
}

static bool consume_receive_captures( bool a_render )
{
    bool consumed = false;
    rfscout_subghz_capture_t capture;
    uint8_t lease = 0U;

    while( cc1101_capture_acquire( &capture, &lease ) )
    {
        if( capture.symbol_count > 0U )
        {
            rfscout_subghz_decode( &capture, &latest_analysis );
            if( CC1101_VIEW_WAVEFORM == viewer )
            {
                (void)retain_raw_snapshot( &capture );
            }
            consumed = true;
        }
        cc1101_capture_release( lease );
    }

    if( consumed && a_render )
    {
        render_receive();
    }
    return consumed;
}

static void switch_to_discovery( void )
{
    if( ESP_OK == cc1101_set_acquisition_mode( CC1101_ACQUISITION_DISCOVERY ) )
    {
        reset_receive_results();
        viewer = CC1101_VIEW_DECODED;
        discovery_count = 0U;
        discovery_peak_khz = 0U;
        discovery_peak_rssi = INT8_MIN;
        discovery_peak_valid = false;
        memset( discovery_rssi, -120, sizeof( discovery_rssi ) );
        memset( discovery_frequency_khz, 0, sizeof( discovery_frequency_khz ) );
        render_discovery();
    }
}

static uint32_t preferred_receive_frequency( void )
{
    if( discovery_peak_valid )
    {
        return discovery_peak_khz;
    }

    cc1101_receive_config_t receive;
    cc1101_receive_config( &receive );
    return receive.frequency_khz;
}

static bool switch_to_receive( cc1101_view_mode_t a_view )
{
    if( ESP_OK != cc1101_set_receive_frequency( preferred_receive_frequency() ) )
    {
        return false;
    }

    if( ESP_OK != cc1101_set_acquisition_mode( CC1101_ACQUISITION_RECEIVE ) )
    {
        return false;
    }

    reset_receive_results();
    viewer = a_view;
    render_receive();
    return true;
}

static bool adjust_receive_frequency( int a_direction )
{
    cc1101_scan_config_t scan;
    cc1101_receive_config_t receive;
    cc1101_scan_config( &scan );
    cc1101_receive_config( &receive );

    const int64_t candidate =
        (int64_t)receive.frequency_khz + ( (int64_t)a_direction * scan.step_khz );
    if( ( candidate < 0 ) || ( candidate > UINT32_MAX ) )
    {
        return false;
    }
    if( ESP_OK != cc1101_set_receive_frequency( (uint32_t)candidate ) )
    {
        return false;
    }

    reset_receive_results();
    return true;
}

static bool change_mode( int a_direction )
{
    (void)a_direction;
    const cc1101_acquisition_mode_t target =
        ( CC1101_ACQUISITION_RECEIVE == cc1101_acquisition_mode() ) ? CC1101_ACQUISITION_DISCOVERY
                                                                    : CC1101_ACQUISITION_RECEIVE;

    if( ESP_OK != cc1101_set_acquisition_mode( target ) )
    {
        return false;
    }

    reset_receive_results();
    viewer = CC1101_VIEW_DECODED;
    return true;
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( CC1101_MENU_BACK == menu_item )
        {
            close_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }

        if( CC1101_MENU_APP_LOCK == menu_item )
        {
            (void)app_manager_set_locked( !app_manager_locked() );
            menu_editing = false;
            menu_limit = false;
            render_menu();
            return;
        }

        menu_editing = !menu_editing;
        menu_limit = false;
        render_menu();
        emit_event( RFSCOUT_EVENT_ACTIVATE, menu_editing ? 1 : 0 );
        return;
    }

    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return;
    }

    const int direction = ( a_input->value > 0 ) ? 1 : -1;
    if( !menu_editing )
    {
        int item = (int)menu_item + direction;
        if( item < 0 )
        {
            item = (int)CC1101_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)CC1101_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        if( (unsigned)item != menu_item )
        {
            menu_item = (unsigned)item;
            menu_limit = false;
            render_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    bool changed = false;
    if( CC1101_MENU_MODE == menu_item )
    {
        changed = change_mode( direction );
    }
    else if( CC1101_MENU_SCAN_PROFILE == menu_item )
    {
        cc1101_scan_config_t scan;
        cc1101_scan_config( &scan );
        int profile = (int)scan.profile + direction;
        if( profile < 0 )
        {
            profile = CC1101_PROFILE_COUNT - 1;
        }
        if( profile >= CC1101_PROFILE_COUNT )
        {
            profile = 0;
        }
        cc1101_set_profile( (cc1101_profile_t)profile );
        changed = true;
    }
    else if( CC1101_MENU_RX_FREQUENCY == menu_item )
    {
        changed = adjust_receive_frequency( direction );
    }
    else if( CC1101_MENU_RX_PRESET == menu_item )
    {
        cc1101_receive_config_t receive;
        cc1101_receive_config( &receive );
        const cc1101_receive_preset_t preset = ( CC1101_RECEIVE_OOK_650 == receive.preset )
                                                   ? CC1101_RECEIVE_OOK_270
                                                   : CC1101_RECEIVE_OOK_650;
        changed = ESP_OK == cc1101_set_receive_preset( preset );
        if( changed )
        {
            reset_receive_results();
        }
    }
    else if( CC1101_MENU_LOW == menu_item )
    {
        changed = cc1101_adjust_low( direction );
    }
    else if( CC1101_MENU_HIGH == menu_item )
    {
        changed = cc1101_adjust_high( direction );
    }
    else if( CC1101_MENU_STEP == menu_item )
    {
        changed = cc1101_adjust_step( direction );
    }

    menu_limit = !changed;
    render_menu();
    if( changed )
    {
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static bool set_receive_view( cc1101_view_mode_t a_view )
{
    if( a_view >= CC1101_VIEW_COUNT )
    {
        return false;
    }

    if( CC1101_ACQUISITION_DISCOVERY == cc1101_acquisition_mode() )
    {
        return switch_to_receive( a_view );
    }

    if( viewer == a_view )
    {
        return true;
    }

    viewer = a_view;
    if( CC1101_VIEW_WAVEFORM != viewer )
    {
        clear_raw_snapshot();
    }
    else
    {
        raw_snapshot_count = 0U;
        raw_snapshot_resolution_hz = 0U;
    }
    render_receive();
    return true;
}

static bool navigate_surface( int a_direction )
{
    const int surface_count = (int)CC1101_VIEW_COUNT + 1;
    int surface = CC1101_ACQUISITION_DISCOVERY == cc1101_acquisition_mode() ? 0 : (int)viewer + 1;
    surface += a_direction > 0 ? 1 : -1;
    if( surface < 0 )
    {
        surface = surface_count - 1;
    }
    else if( surface >= surface_count )
    {
        surface = 0;
    }

    if( 0 == surface )
    {
        switch_to_discovery();
        return CC1101_ACQUISITION_DISCOVERY == cc1101_acquisition_mode();
    }

    return set_receive_view( (cc1101_view_mode_t)( surface - 1 ) );
}

static rfscout_app_runtime_result_t cc1101_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        menu_limit = false;
        menu_item = CC1101_MENU_BACK;
        viewer = CC1101_VIEW_DECODED;
        rendered_app_lock = app_manager_locked();

        raw_snapshot = NULL;
        raw_snapshot_count = 0U;
        raw_snapshot_capacity = 0U;
        raw_snapshot_resolution_hz = 0U;
        memset( &latest_analysis, 0, sizeof( latest_analysis ) );
        memset( discovery_rssi, -120, sizeof( discovery_rssi ) );
        memset( discovery_frequency_khz, 0, sizeof( discovery_frequency_khz ) );
        discovery_count = 0U;
        discovery_peak_khz = 0U;
        discovery_peak_rssi = INT8_MIN;
        discovery_peak_valid = false;

        (void)cc1101_set_acquisition_mode( CC1101_ACQUISITION_DISCOVERY );
        render_discovery();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        if( ESP_OK != cc1101_start() )
        {
            ESP_LOGW( TAG, "CC1101 unavailable" );
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        clear_raw_snapshot();
        memset( &latest_analysis, 0, sizeof( latest_analysis ) );
        menu_active = false;
        menu_editing = false;
        menu_limit = false;
        menu_item = CC1101_MENU_BACK;
        viewer = CC1101_VIEW_DECODED;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( menu_active )
        {
            handle_menu_input( &a_event->input );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }

        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            menu_active = true;
            menu_editing = false;
            menu_limit = false;
            menu_item = CC1101_MENU_BACK;
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }

        if( RFSCOUT_INPUT_LONG_PRESS == a_event->input.type )
        {
            if( navigate_surface( 1 ) )
            {
                emit_event( RFSCOUT_EVENT_NAVIGATE, 1 );
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }

        if( ( RFSCOUT_INPUT_NAVIGATION == a_event->input.type ) && app_manager_locked() )
        {
            const int direction = a_event->input.value > 0 ? 1 : -1;
            if( navigate_surface( direction ) )
            {
                emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }

        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( ( RFSCOUT_APP_RUNTIME_POLL == a_event->type ) && a_event->activated )
    {
        if( !cc1101_is_detected() )
        {
            app_framework_report_availability( RFSCOUT_APP_CC1101,
                                               RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
            return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
        }

        if( rendered_app_lock != app_manager_locked() )
        {
            if( menu_active )
            {
                render_menu();
            }
            else
            {
                render_current();
            }
        }

        cc1101_poll();

        if( CC1101_ACQUISITION_RECEIVE == cc1101_acquisition_mode() )
        {
            (void)consume_receive_captures( !menu_active );
        }
        else if( cc1101_take_frame_ready() )
        {
            consume_discovery_frame( !menu_active );
        }
    }

    return RFSCOUT_APP_RUNTIME_HANDLED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    cc1101_scan_config_t scan;
    cc1101_receive_config_t receive;
    cc1101_capture_stats_t stats;
    cc1101_scan_config( &scan );
    cc1101_receive_config( &receive );
    cc1101_capture_stats( &stats );

    const cc1101_acquisition_mode_t mode = cc1101_acquisition_mode();
    bool success = log_uint( a_record, "mode", mode ) &&
                   log_bool( a_record, "detected", cc1101_is_detected() );

    if( success && ( CC1101_ACQUISITION_RECEIVE == mode ) )
    {
        success = log_uint( a_record, "frequency_khz", receive.frequency_khz ) &&
                  log_uint( a_record, "preset", receive.preset ) &&
                  log_uint( a_record, "captures", stats.captures_completed ) &&
                  log_uint( a_record, "truncated", stats.captures_truncated ) &&
                  log_uint( a_record, "overruns", stats.capture_overruns ) &&
                  log_uint( a_record, "errors", stats.capture_errors );
    }
    else if( success )
    {
        success = log_uint( a_record, "low_khz", scan.low_khz ) &&
                  log_uint( a_record, "high_khz", scan.high_khz ) &&
                  log_uint( a_record, "step_khz", scan.step_khz );
    }
    return success;
}

const rfscout_app_descriptor_t rfscout_cc1101_app = {
    .id = RFSCOUT_APP_CC1101,
    .name = "CC1101",
    .persistence_key = "cc1101",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = cc1101_probe,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_CC1101,
    .runtime = cc1101_runtime,
    .telemetry = app_telemetry,
};
