/**
    apps/nrf24/app.c : nRF24 application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_notification_menu.h"
#include "event.h"
#include "notification.h"
#include "notification_preferences.h"
#include "nrf24.h"
#include "gps.h"
#include "nrf24_activity.h"
#include "nrf24_packet_view.h"
#include "radio_app_view.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define NRF24_MENU_BACK 0U
#define NRF24_MENU_RESET_COUNTS 1U
#define NRF24_MENU_LOW 2U
#define NRF24_MENU_HIGH 3U
#define NRF24_MENU_DWELL 4U
#define NRF24_MENU_PACKETS 5U
#define NRF24_MENU_NOTIFICATIONS 6U
#define NRF24_MENU_ITEM_COUNT 7U

static const char *TAG = "nrf24_app";
static bool menu_active;
static bool menu_editing;
static bool packet_browser;
static bool packet_detail;
static unsigned menu_item;
static unsigned packet_item;
static uint32_t notification_counts[ NRF24_FP_LOGITECH_LIKE + 1U ];
static rfscout_app_notification_menu_t notification_menu;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_NRF24, a_value );
}

static rfscout_notification_type_t notification_type( nrf24_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case NRF24_FP_ESB:
            return RFSCOUT_NOTIFICATION_NRF24_ESB;
        case NRF24_FP_INPUT:
            return RFSCOUT_NOTIFICATION_NRF24_INPUT;
        case NRF24_FP_SENSOR:
            return RFSCOUT_NOTIFICATION_NRF24_SENSOR;
        case NRF24_FP_WIFI_LIKE:
            return RFSCOUT_NOTIFICATION_NRF24_WIFI_LIKE;
        case NRF24_FP_BLE_LIKE:
            return RFSCOUT_NOTIFICATION_NRF24_BLE_LIKE;
        case NRF24_FP_LOGITECH_LIKE:
        case NRF24_FP_UNKNOWN:
        default:
            return RFSCOUT_NOTIFICATION_NRF24_LOGITECH_LIKE;
    }
}

static void notify_detections( void )
{
    static const nrf24_fingerprint_type_t types[] = {
        NRF24_FP_ESB,
        NRF24_FP_INPUT,
        NRF24_FP_SENSOR,
        NRF24_FP_WIFI_LIKE,
        NRF24_FP_BLE_LIKE,
        NRF24_FP_LOGITECH_LIKE,
    };
    for( size_t index = 0U; index < sizeof( types ) / sizeof( types[ 0 ] ); ++index )
    {
        const nrf24_fingerprint_type_t type = types[ index ];
        const uint32_t count = nrf24_activity_category_count( type );
        if( count > notification_counts[ type ] )
        {
            (void)notification_emit( notification_type( type ) );
        }
        notification_counts[ type ] = count;
    }
}

static void render_spectrum( void )
{
    uint8_t fast[ NRF24_CHANNEL_COUNT ];
    nrf24_fingerprint_t devices[ NRF24_FINGERPRINT_MAX ];
    const size_t count = nrf24_activity_histogram( fast, NRF24_CHANNEL_COUNT );
    const size_t device_count = nrf24_activity_fingerprints( devices, NRF24_FINGERPRINT_MAX );
    notify_detections();
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_spectrum( fast,
                                   count,
                                   config->low_channel,
                                   config->high_channel,
                                   nrf24_is_detected(),
                                   devices,
                                   device_count );
}

static void render_menu( void )
{
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_menu( menu_item,
                               menu_editing,
                               config->low_channel,
                               config->high_channel,
                               config->dwell_us,
                               notification_preferences_app_enabled( RFSCOUT_APP_NRF24 ) );
}

static void render_menu_callback( void *a_context )
{
    (void)a_context;
    render_menu();
}

static size_t packet_count( void )
{
    nrf24_recent_packet_t packets[ NRF24_RECENT_PACKET_MAX ];
    return nrf24_activity_recent_packets( packets, NRF24_RECENT_PACKET_MAX ) + 1U;
}

static void close_menu( void )
{
    menu_active = false;
    menu_editing = false;
    packet_browser = false;
    packet_detail = false;
    packet_item = 0U;
    render_spectrum();
}

static void handle_packet_input( const rfscout_input_event_t *a_input )
{
    const size_t item_count = packet_count();
    if( packet_detail )
    {
        if( RFSCOUT_INPUT_SELECT == a_input->type )
        {
            packet_detail = false;
            (void)nrf24_packet_view_list( packet_item );
            emit_event( RFSCOUT_EVENT_BACK, 0 );
        }
        return;
    }

    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( 0U == packet_item )
        {
            packet_browser = false;
            packet_item = 0U;
            render_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( item_count > 1U )
        {
            if( packet_item >= item_count )
            {
                packet_item = (unsigned)item_count - 1U;
            }
            packet_detail = true;
            nrf24_packet_view_detail( packet_item - 1U );
            emit_event( RFSCOUT_EVENT_ENTER, (int32_t)( packet_item - 1U ) );
        }
        return;
    }
    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return;
    }

    const int direction = ( a_input->value > 0 ) ? 1 : -1;
    int item = (int)packet_item + direction;
    if( item < 0 )
    {
        item = (int)item_count - 1;
    }
    if( item >= (int)item_count )
    {
        item = 0;
    }
    if( (unsigned)item != packet_item )
    {
        packet_item = (unsigned)item;
        (void)nrf24_packet_view_list( packet_item );
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    if( packet_browser )
    {
        handle_packet_input( a_input );
        return;
    }
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( NRF24_MENU_BACK == menu_item )
        {
            close_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( NRF24_MENU_PACKETS == menu_item )
        {
            packet_browser = true;
            packet_detail = false;
            packet_item = 0U;
            (void)nrf24_packet_view_list( packet_item );
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        }
        if( NRF24_MENU_NOTIFICATIONS == menu_item )
        {
            app_notification_menu_open( &notification_menu );
            return;
        }
        if( NRF24_MENU_RESET_COUNTS == menu_item )
        {
            nrf24_activity_reset_counts();
            memset( notification_counts, 0, sizeof( notification_counts ) );
            close_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        menu_editing = !menu_editing;
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
            item = (int)NRF24_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)NRF24_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        if( (unsigned)item != menu_item )
        {
            menu_item = (unsigned)item;
            render_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    const nrf24_scan_config_t before = *nrf24_config();
    if( NRF24_MENU_LOW == menu_item )
    {
        nrf24_adjust_low( direction );
    }
    else if( NRF24_MENU_HIGH == menu_item )
    {
        nrf24_adjust_high( direction );
    }
    else if( NRF24_MENU_DWELL == menu_item )
    {
        nrf24_adjust_dwell( direction );
    }
    else
    {
        return;
    }
    const nrf24_scan_config_t after = *nrf24_config();
    render_menu();
    if( ( before.low_channel != after.low_channel ) ||
        ( before.high_channel != after.high_channel ) || ( before.dwell_us != after.dwell_us ) )
    {
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static rfscout_app_runtime_result_t nrf24_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        packet_browser = false;
        packet_detail = false;
        menu_item = NRF24_MENU_BACK;
        packet_item = 0U;
        memset( notification_counts, 0, sizeof( notification_counts ) );
        app_notification_menu_init(
            &notification_menu, RFSCOUT_APP_NRF24, render_menu_callback, NULL );
        nrf24_activity_reset();
        render_spectrum();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        if( ESP_OK != nrf24_start() )
        {
            ESP_LOGW( TAG, "nRF24 unavailable" );
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        nrf24_activity_reset();
        memset( notification_counts, 0, sizeof( notification_counts ) );
        menu_active = false;
        menu_editing = false;
        packet_browser = false;
        packet_detail = false;
        menu_item = NRF24_MENU_BACK;
        packet_item = 0U;
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
            menu_active = true;
            menu_editing = false;
            menu_item = NRF24_MENU_BACK;
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type && a_event->activated && !nrf24_is_active() &&
        nrf24_is_detected() && !gps_detected() && gps_probe_complete() )
    {
        (void)nrf24_start();
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type && a_event->activated && !menu_active &&
        !app_notification_menu_active( &notification_menu ) )
    {
        nrf24_poll();
        if( nrf24_take_frame_ready() )
        {
            render_spectrum();
        }
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    const nrf24_scan_config_t *config = nrf24_config();
    nrf24_fingerprint_t fingerprints[ NRF24_FINGERPRINT_MAX ];
    const size_t count = nrf24_activity_fingerprints( fingerprints, NRF24_FINGERPRINT_MAX );
    bool success = log_uint( a_record, "low_channel", config->low_channel ) &&
                   log_uint( a_record, "high_channel", config->high_channel ) &&
                   log_uint( a_record, "dwell_us", config->dwell_us ) &&
                   log_uint( a_record, "likely", count ) &&
                   log_bool( a_record, "detected", nrf24_is_detected() );
    return success;
}

const rfscout_app_descriptor_t rfscout_nrf24_app = {
    .id = RFSCOUT_APP_NRF24,
    .name = "nRF24",
    .persistence_key = "nrf24",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = nrf24_probe,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_NRF24,
    .runtime = nrf24_runtime,
    .telemetry = app_telemetry,
};
