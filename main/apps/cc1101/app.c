/**
    apps/cc1101/app.c : CC1101 application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_notification_menu.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "event.h"
#include "notification.h"
#include "notification_preferences.h"
#include "radio_app_view.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define CC1101_MIN_STEP_KHZ 1U
#define CC1101_MAX_STEP_KHZ 1000U
#define CC1101_MENU_BACK 0U
#define CC1101_MENU_RESET_COUNTS 1U
#define CC1101_MENU_PROFILE 2U
#define CC1101_MENU_LOW 3U
#define CC1101_MENU_HIGH 4U
#define CC1101_MENU_STEP 5U
#define CC1101_MENU_NOTIFICATIONS 6U
#define CC1101_MENU_ITEM_COUNT 7U

static const char *TAG = "cc1101_app";
static bool menu_active;
static bool menu_editing;
static bool menu_limit;
static unsigned menu_item;
static uint16_t notification_counts[ CC1101_FP_TYPE_COUNT ];
static rfscout_app_notification_menu_t notification_menu;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_CC1101, a_value );
}

static rfscout_notification_type_t notification_type( cc1101_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case CC1101_FP_NARROW_ISM:
            return RFSCOUT_NOTIFICATION_CC1101_NARROW_ISM;
        case CC1101_FP_REMOTE_SENSOR_LIKE:
            return RFSCOUT_NOTIFICATION_CC1101_REMOTE_SENSOR;
        case CC1101_FP_CONTINUOUS_CARRIER:
            return RFSCOUT_NOTIFICATION_CC1101_CARRIER;
        case CC1101_FP_BURSTY_SIGNAL:
            return RFSCOUT_NOTIFICATION_CC1101_BURSTY;
        case CC1101_FP_WIDEBAND_ACTIVITY:
        case CC1101_FP_UNKNOWN:
        case CC1101_FP_TYPE_COUNT:
        default:
            return RFSCOUT_NOTIFICATION_CC1101_WIDEBAND;
    }
}

static void notify_detections( const cc1101_fingerprint_t *a_devices, size_t a_device_count )
{
    if( NULL == a_devices )
    {
        return;
    }
    for( size_t index = 0U; index < a_device_count; ++index )
    {
        const cc1101_fingerprint_type_t type = a_devices[ index ].type;
        if( ( type <= CC1101_FP_UNKNOWN ) || ( type >= CC1101_FP_TYPE_COUNT ) )
        {
            continue;
        }
        const uint16_t count = a_devices[ index ].count;
        if( count > notification_counts[ type ] )
        {
            (void)notification_emit( notification_type( type ) );
        }
        notification_counts[ type ] = count;
    }
}

static void render_spectrum( void )
{
    uint8_t current[ CC1101_SPECTRUM_BINS ];
    cc1101_fingerprint_t devices[ CC1101_FINGERPRINT_MAX ];
    const size_t count = cc1101_activity_levels( current, CC1101_SPECTRUM_BINS );
    const size_t device_count = cc1101_activity_fingerprints( devices, CC1101_FINGERPRINT_MAX );
    notify_detections( devices, device_count );
    const cc1101_scan_config_t *config = cc1101_config();
    radio_app_view_cc1101_spectrum( current,
                                    count,
                                    config->low_khz,
                                    config->high_khz,
                                    cc1101_is_detected(),
                                    devices,
                                    device_count );
}

static void observe_spectrum( void )
{
    int8_t rssi[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( rssi, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *config = cc1101_config();
    cc1101_activity_observe_sweep( rssi, count, config->low_khz, config->high_khz );
}

static void render_menu( void )
{
    const cc1101_scan_config_t *config = cc1101_config();
    radio_app_view_cc1101_menu( menu_item,
                                menu_editing,
                                cc1101_profile_name( config->profile ),
                                config->low_khz,
                                config->high_khz,
                                config->step_khz,
                                cc1101_sweep_count(),
                                menu_limit,
                                notification_preferences_app_enabled( RFSCOUT_APP_CC1101 ) );
}

static void render_menu_callback( void *a_context )
{
    (void)a_context;
    render_menu();
}

static void close_menu( void )
{
    menu_active = false;
    menu_editing = false;
    menu_limit = false;
    render_spectrum();
}

static bool step_at_endpoint( uint32_t a_step, int a_delta )
{
    return ( ( a_delta > 0 ) && ( a_step >= CC1101_MAX_STEP_KHZ ) ) ||
           ( ( a_delta < 0 ) && ( a_step <= CC1101_MIN_STEP_KHZ ) );
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_BACK == a_input->type )
    {
        close_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }

    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( CC1101_MENU_BACK == menu_item )
        {
            close_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( CC1101_MENU_NOTIFICATIONS == menu_item )
        {
            app_notification_menu_open( &notification_menu );
            return;
        }
        if( CC1101_MENU_RESET_COUNTS == menu_item )
        {
            cc1101_activity_reset_counts();
            memset( notification_counts, 0, sizeof( notification_counts ) );
            close_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
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

    const cc1101_scan_config_t before = *cc1101_config();
    bool changed = true;
    bool endpoint = false;
    if( CC1101_MENU_PROFILE == menu_item )
    {
        int profile = (int)before.profile + direction;
        if( profile < 0 )
        {
            profile = CC1101_PROFILE_COUNT - 1;
        }
        if( profile >= CC1101_PROFILE_COUNT )
        {
            profile = 0;
        }
        cc1101_set_profile( (cc1101_profile_t)profile );
        cc1101_activity_reset();
        memset( notification_counts, 0, sizeof( notification_counts ) );
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
        endpoint = step_at_endpoint( before.step_khz, a_input->value );
        changed = cc1101_adjust_step( direction );
    }
    else
    {
        changed = false;
    }
    if( changed && ( menu_item >= CC1101_MENU_LOW ) && ( menu_item <= CC1101_MENU_STEP ) )
    {
        cc1101_activity_reset();
        memset( notification_counts, 0, sizeof( notification_counts ) );
    }
    menu_limit = !changed && !endpoint;
    render_menu();
    if( changed )
    {
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
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
        memset( notification_counts, 0, sizeof( notification_counts ) );
        app_notification_menu_init(
            &notification_menu, RFSCOUT_APP_CC1101, render_menu_callback, NULL );
        cc1101_activity_reset();
        render_spectrum();
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
        cc1101_activity_reset();
        cc1101_activity_reset_counts();
        memset( notification_counts, 0, sizeof( notification_counts ) );
        menu_active = false;
        menu_editing = false;
        menu_limit = false;
        menu_item = CC1101_MENU_BACK;
        app_notification_menu_reset( &notification_menu );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( app_notification_menu_active( &notification_menu ) )
        {
            (void)app_notification_menu_input( &notification_menu, &a_event->input );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( menu_active )
        {
            handle_menu_input( &a_event->input );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            cc1101_activity_pause();
            menu_active = true;
            menu_editing = false;
            menu_limit = false;
            menu_item = CC1101_MENU_BACK;
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type && a_event->activated && !menu_active &&
        !app_notification_menu_active( &notification_menu ) )
    {
        cc1101_poll();
        if( cc1101_take_frame_ready() )
        {
            observe_spectrum();
            render_spectrum();
        }
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_cc1101_app = {
    .id = RFSCOUT_APP_CC1101,
    .name = "CC1101",
    .persistence_key = "cc1101",
    .probe_policy = RFSCOUT_APP_PROBE_STARTUP,
    .probe = cc1101_init,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_CC1101,
    .runtime = cc1101_runtime,
};
