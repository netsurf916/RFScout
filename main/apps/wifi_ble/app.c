/**
    apps/wifi_ble/app.c : WiFi/BLE application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_notification_menu.h"
#include "ble_monitor.h"
#include "device_config.h"
#include "event.h"
#include "notification.h"
#include "notification_preferences.h"
#include "platform.h"
#include "observation_store.h"
#include "radio_monitor.h"
#include "ui.h"
#include "wifi_settings_view.h"

#include "esp_log.h"

#include <stddef.h>

#define WIFI_RADIO_RETRY_INTERVAL_US 1000000LL

#define WIFI_MENU_BACK 0U
#define WIFI_MENU_RESET 1U
#define WIFI_MENU_NOTIFICATIONS 2U
#define WIFI_MENU_EVIDENCE_THRESHOLD 3U
#define WIFI_MENU_WIFI_TIMEOUT 4U
#define WIFI_MENU_BLE_TIMEOUT 5U
#define WIFI_MENU_ITEM_COUNT 6U

static const char *TAG = "wifi_ble_app";
static bool menu_active;
static bool menu_editing;
static unsigned menu_item;
static int64_t last_radio_retry_us;
static rfscout_app_notification_menu_t notification_menu;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_WIFI_BT, a_value );
}

static void render_menu( void )
{
    wifi_settings_view_render( menu_item,
                               menu_editing,
                               notification_preferences_app_enabled( RFSCOUT_APP_WIFI_BT ),
                               device_config_evidence_threshold(),
                               device_config_wifi_timeout_seconds(),
                               device_config_ble_timeout_seconds() );
}

static void render_menu_callback( void *a_context )
{
    (void)a_context;
    render_menu();
}

static void start_collectors( void )
{
    esp_err_t result;
    if( !radio_monitor_is_active() )
    {
        result = radio_monitor_start();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "Wi-Fi monitor start failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
    if( !ble_monitor_is_active() )
    {
        result = ble_monitor_start();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "BLE monitor start failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
}

static void close_menu( void )
{
    const esp_err_t save_result = device_config_save_if_dirty();
    if( ( ESP_OK != save_result ) && ( ESP_ERR_INVALID_STATE != save_result ) )
    {
        ESP_LOGW( TAG, "WiFi/BT settings save failed: %s", esp_err_to_name( save_result ) );
    }
    menu_active = false;
    menu_editing = false;
    menu_item = WIFI_MENU_BACK;
}

static uint16_t adjust_timeout_seconds( uint16_t a_current, int a_direction )
{
    int value =
        (int)a_current + ( ( a_direction > 0 ) ? (int)RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS
                                               : -(int)RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS );
    if( value < (int)RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS )
    {
        value = RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS;
    }
    if( value > (int)RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS )
    {
        value = RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS;
    }
    return (uint16_t)value;
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( menu_editing )
        {
            menu_editing = false;
            render_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( WIFI_MENU_BACK == menu_item )
        {
            close_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( WIFI_MENU_NOTIFICATIONS == menu_item )
        {
            app_notification_menu_open( &notification_menu );
            return;
        }
        if( WIFI_MENU_EVIDENCE_THRESHOLD == menu_item )
        {
            rfscout_evidence_strength_t threshold = device_config_evidence_threshold();
            threshold = (rfscout_evidence_strength_t)( ( (unsigned)threshold + 1U ) %
                                                       (unsigned)RFSCOUT_EVIDENCE_STRENGTH_COUNT );
            if( ESP_OK == device_config_set_evidence_threshold( threshold ) )
            {
                size_t newly_qualified[ SURVEILLANCE_OTHER + 1U ] = { 0 };
                observation_store_reevaluate_surveillance(
                    newly_qualified, sizeof( newly_qualified ) / sizeof( newly_qualified[ 0 ] ) );
                for( size_t classification = (size_t)SURVEILLANCE_ALPR;
                     classification <= (size_t)SURVEILLANCE_OTHER;
                     ++classification )
                {
                    const rfscout_notification_type_t notification =
                        notification_for_surveillance( (surveillance_class_t)classification );
                    for( size_t count = 0U; count < newly_qualified[ classification ]; ++count )
                    {
                        platform_set_alert_led( true );
                        (void)notification_emit( notification );
                        platform_set_alert_led( false );
                    }
                }
                render_menu();
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            return;
        }
        if( ( WIFI_MENU_WIFI_TIMEOUT == menu_item ) || ( WIFI_MENU_BLE_TIMEOUT == menu_item ) )
        {
            menu_editing = true;
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        }
        if( WIFI_MENU_RESET == menu_item )
        {
            observation_store_reset();
            close_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        return;
    }

    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return;
    }

    const int direction = ( a_input->value > 0 ) ? 1 : -1;
    if( menu_editing )
    {
        uint16_t old_value = 0U;
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if( WIFI_MENU_WIFI_TIMEOUT == menu_item )
        {
            old_value = device_config_wifi_timeout_seconds();
            result = device_config_set_wifi_timeout_seconds(
                adjust_timeout_seconds( old_value, direction ) );
        }
        else if( WIFI_MENU_BLE_TIMEOUT == menu_item )
        {
            old_value = device_config_ble_timeout_seconds();
            result = device_config_set_ble_timeout_seconds(
                adjust_timeout_seconds( old_value, direction ) );
        }
        if( ESP_OK == result )
        {
            uint16_t new_value = old_value;
            if( WIFI_MENU_WIFI_TIMEOUT == menu_item )
            {
                new_value = device_config_wifi_timeout_seconds();
            }
            else if( WIFI_MENU_BLE_TIMEOUT == menu_item )
            {
                new_value = device_config_ble_timeout_seconds();
            }
            if( new_value != old_value )
            {
                render_menu();
                emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
            }
        }
        return;
    }

    int item = (int)menu_item + direction;
    if( item < 0 )
    {
        item = (int)WIFI_MENU_ITEM_COUNT - 1;
    }
    if( item >= (int)WIFI_MENU_ITEM_COUNT )
    {
        item = 0;
    }
    if( (unsigned)item != menu_item )
    {
        menu_item = (unsigned)item;
        render_menu();
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static rfscout_app_runtime_result_t wifi_ble_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        menu_item = WIFI_MENU_BACK;
        last_radio_retry_us = 0;
        app_notification_menu_init(
            &notification_menu, RFSCOUT_APP_WIFI_BT, render_menu_callback, NULL );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        last_radio_retry_us = a_event->now_us;
        start_collectors();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        menu_active = false;
        menu_editing = false;
        menu_item = WIFI_MENU_BACK;
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
            menu_item = WIFI_MENU_BACK;
            render_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( !a_event->activated )
    {
        if( !menu_active && !app_notification_menu_active( &notification_menu ) )
        {
            ui_poll();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( ( !radio_monitor_is_active() || !ble_monitor_is_active() ) &&
        ( a_event->now_us - last_radio_retry_us >= WIFI_RADIO_RETRY_INTERVAL_US ) )
    {
        last_radio_retry_us = a_event->now_us;
        start_collectors();
    }
    radio_monitor_set_active_ap_channels( observation_store_wifi_ap_channel_mask() );
    radio_monitor_step();
    if( !menu_active && !app_notification_menu_active( &notification_menu ) )
    {
        ui_poll();
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_wifi_ble_app = {
    .id = RFSCOUT_APP_WIFI_BT,
    .name = "WiFi / BT",
    .persistence_key = "wifi_ble",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_BLE_MONITOR,
    .runtime = wifi_ble_runtime,
};
