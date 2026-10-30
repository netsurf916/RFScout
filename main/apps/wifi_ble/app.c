/**
    apps/wifi_ble/app.c : WiFi/BLE application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_manager.h"

#include "app_notification_menu.h"
#include "ble_monitor.h"
#include "device_config.h"
#include "event.h"
#include "notification.h"
#include "notification_preferences.h"
#include "platform.h"
#include "persistent_store.h"
#include "display.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_theme.h"
#include <stdio.h>
#include "observation_store.h"
#include "radio_monitor.h"
#include "surveillance_history.h"
#include "ui.h"
#include "wifi_settings_view.h"

#include "esp_log.h"

#include <stddef.h>

#define WIFI_RADIO_RETRY_INTERVAL_US 1000000LL
#define WIFI_JOURNAL_FLUSH_INTERVAL_US 500000LL

#define WIFI_MENU_BACK 0U
#define WIFI_MENU_NOTIFICATIONS 1U
#define WIFI_MENU_EVIDENCE_THRESHOLD 2U
#define WIFI_MENU_WIFI_TIMEOUT 3U
#define WIFI_MENU_BLE_TIMEOUT 4U
#define WIFI_MENU_SOURCES 5U
#define WIFI_MENU_ITEM_COUNT 6U

static bool menu_active;
static bool menu_editing;
static const char *TAG = "wifi_ble_app";
static unsigned menu_item;
static bool wifi_source = true;
static bool ble_source = true;
static bool collectors_activated;
static bool manual_channels;
static bool ap_dwell_enabled = true;
static bool shuffle_channels;
static bool ble_active_scan;
static bool ble_dedupe;
static unsigned ble_duty;
static const uint16_t ble_intervals[] = { 0x0040U, 0x0080U, 0x00A0U };
static const uint16_t ble_windows[] = { 0x0040U, 0x0040U, 0x0020U };
static uint16_t selected_channels = 0x3FFFU;
static unsigned dwell_choice = 1U;
static const uint32_t dwell_choices[] = { 100U, 200U, 250U, 500U, 1000U };
static unsigned radio_page;
static unsigned radio_selected;
typedef struct
{
    uint32_t magic;
    uint16_t channels;
    uint8_t wifi;
    uint8_t ble;
    uint8_t manual;
    uint8_t shuffled;
    uint8_t dwell;
    uint8_t ble_active;
    uint8_t ble_duplicates;
    uint8_t ble_duty;
} app_radio_settings_t;

static bool radio_settings_valid( const void *a_record )
{
    const app_radio_settings_t *stored = a_record;
    return stored->magic == 0x52575032U && stored->dwell < 5U && stored->ble_duty < 3U;
}

static void load_radio_settings( void )
{
    app_radio_settings_t stored;
    wifi_source = true;
    ble_source = true;
    selected_channels = 0x3FFFU;
    manual_channels = false;
    ap_dwell_enabled = true;
    shuffle_channels = false;
    dwell_choice = 1U;
    ble_active_scan = false;
    ble_dedupe = false;
    ble_duty = 0U;
    /* Retired per-source record; radio.settings owns source enablement. */
    (void)app_store_remove( "sources.settings" );
    if( ESP_OK ==
        app_store_load( "radio.settings", &stored, sizeof( stored ), radio_settings_valid ) )
    {
        wifi_source = stored.wifi != 0U;
        ble_source = stored.ble != 0U;
        selected_channels = stored.channels & 0x3FFFU;
        manual_channels = stored.manual != 0U;
        shuffle_channels = stored.shuffled != 0U;
        dwell_choice = stored.dwell;
        ble_active_scan = stored.ble_active != 0U;
        ble_dedupe = stored.ble_duplicates != 0U;
        ble_duty = stored.ble_duty;
    }
    uint8_t stored_ap_dwell = 1U;
    if( ESP_OK ==
        app_store_load( "ap_dwell.settings", &stored_ap_dwell, sizeof( stored_ap_dwell ), NULL ) )
    {
        ap_dwell_enabled = 0U != stored_ap_dwell;
    }
    radio_monitor_set_ap_dwell_enabled( ap_dwell_enabled );
}

static void save_radio_settings( void )
{
    const app_radio_settings_t stored = {
        .magic = 0x52575032U,
        .channels = selected_channels,
        .wifi = wifi_source,
        .ble = ble_source,
        .manual = manual_channels,
        .shuffled = shuffle_channels,
        .dwell = (uint8_t)dwell_choice,
        .ble_active = ble_active_scan,
        .ble_duplicates = ble_dedupe,
        .ble_duty = (uint8_t)ble_duty,
    };
    const esp_err_t result = app_store_write( "radio.settings", &stored, sizeof( stored ) );
    const uint8_t ap_dwell_setting = ap_dwell_enabled ? 1U : 0U;
    const esp_err_t ap_dwell_result =
        app_store_write( "ap_dwell.settings", &ap_dwell_setting, sizeof( ap_dwell_setting ) );
    if( ESP_OK != ap_dwell_result )
    {
        ESP_LOGW( TAG, "AP dwell setting save failed: %s", esp_err_to_name( ap_dwell_result ) );
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "radio settings save failed: %s", esp_err_to_name( result ) );
    }
}

static int64_t last_radio_retry_us;
static int64_t last_journal_flush_us;
static rfscout_app_notification_menu_t notification_menu;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_WIFI_BT, a_value );
}

static void render_menu( void );

static void render_radio_menu( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool channels = radio_page == 3U;
    const bool wifi = radio_page == 1U;
    const unsigned count = channels ? 15U : radio_page == 0U ? 5U : wifi ? 6U : 4U;
    const unsigned visible = 7U;
    const unsigned first = radio_selected >= visible ? radio_selected - visible + 1U : 0U;
    display_clear( theme->background );
    ui_chrome_draw( channels           ? "WI-FI CHANNELS"
                    : wifi             ? "WI-FI SETTINGS"
                    : radio_page == 2U ? "BLE SETTINGS"
                                       : "SOURCES",
                    theme->secondary );
    for( unsigned i = first; i < first + visible && i < count; ++i )
    {
        const char *label = "";
        char channel_name[ 20 ];
        char value[ 20 ] = "";
        if( channels )
        {
            if( i > 0U )
            {
                (void)snprintf( channel_name, sizeof( channel_name ), "Channel %u", i );
                label = channel_name;
                (void)snprintf( value,
                                sizeof( value ),
                                "%s",
                                ( selected_channels & ( 1U << ( i - 1U ) ) ) ? "On" : "Off" );
            }
        }
        else if( radio_page == 0U )
        {
            static const char *names[] = {
                "Back", "Wi-Fi", "BLE", "Wi-Fi settings", "BLE settings" };
            label = names[ i ];
            if( i == 1U || i == 2U )
            {
                (void)snprintf( value,
                                sizeof( value ),
                                "%s",
                                ( i == 1U ? wifi_source : ble_source ) ? "On" : "Off" );
            }
        }
        else if( wifi )
        {
            static const char *names[] = { "Back",
                                           "Channel policy",
                                           "Channels",
                                           "Scan order",
                                           "Dwell time",
                                           "AP dwell boost" };
            label = names[ i ];
            if( i == 1U )
            {
                (void)snprintf( value, sizeof( value ), "%s", manual_channels ? "Manual" : "Auto" );
            }
            else if( i == 3U )
            {
                (void)snprintf(
                    value, sizeof( value ), "%s", shuffle_channels ? "Shuffled" : "Sequential" );
            }
            else if( i == 4U )
            {
                (void)snprintf( value,
                                sizeof( value ),
                                "%lu ms",
                                (unsigned long)dwell_choices[ dwell_choice ] );
            }
            else if( i == 5U )
            {
                (void)snprintf( value, sizeof( value ), "%s", ap_dwell_enabled ? "On" : "Off" );
            }
        }
        else
        {
            static const char *names[] = { "Back", "Discovery", "Duplicate filter", "Scan duty" };
            label = names[ i ];
            if( i == 1U )
            {
                (void)snprintf(
                    value, sizeof( value ), "%s", ble_active_scan ? "Active" : "Passive" );
            }
            else if( i == 2U )
            {
                (void)snprintf( value, sizeof( value ), "%s", ble_dedupe ? "On" : "Off" );
            }
            else if( i == 3U )
            {
                (void)snprintf( value,
                                sizeof( value ),
                                "%s",
                                ble_duty == 0U   ? "High"
                                : ble_duty == 1U ? "Medium"
                                                 : "Low" );
            }
        }
        if( i == 0U )
        {
            label = "Back";
        }
        const bool inactive = ( radio_page == 0U && i == 3U && !wifi_source ) ||
                              ( radio_page == 0U && i == 4U && !ble_source ) ||
                              ( wifi && !manual_channels && i >= 2U && i <= 4U );
        const uint16_t color = inactive              ? theme->secondary
                               : i == radio_selected ? theme->info
                                                     : theme->text;
        const int y = 24 + (int)( i - first ) * 18;
        ui_game_draw_menu_marker( 8, y, i == radio_selected ? ">" : " ", color );
        display_draw_text( 22, y, label, color, 1U );
        display_draw_text( 215, y, value, color, 1U );
    }
    (void)display_present();
}

static void handle_radio_menu( const rfscout_input_event_t *a_input )
{
    const unsigned count = radio_page == 3U   ? 15U
                           : radio_page == 0U ? 5U
                           : radio_page == 1U ? 6U
                                              : 4U;
    if( a_input->type == RFSCOUT_INPUT_NAVIGATION )
    {
        radio_selected = ( radio_selected + ( a_input->value > 0 ? 1U : count - 1U ) ) % count;
        render_radio_menu();
        return;
    }
    if( a_input->type != RFSCOUT_INPUT_SELECT )
    {
        return;
    }
    if( radio_selected == 0U )
    {
        if( radio_page == 0U )
        {
            radio_page = 4U;
            render_menu();
            return;
        }
        radio_page = radio_page == 3U ? 1U : 0U;
        radio_selected = 0U;
        render_radio_menu();
        return;
    }
    if( radio_page == 0U )
    {
        if( radio_selected == 1U || radio_selected == 2U )
        {
            bool *enabled = radio_selected == 1U ? &wifi_source : &ble_source;
            const bool next = !*enabled;
            if( next && radio_selected == 1U )
            {
                if( selected_channels == 0U )
                {
                    selected_channels = 0x3FFFU;
                }
                (void)radio_monitor_set_manual_channels( manual_channels,
                                                         selected_channels,
                                                         shuffle_channels,
                                                         dwell_choices[ dwell_choice ] );
            }
            if( next && radio_selected == 2U )
            {
                (void)ble_monitor_set_scan_preferences( ble_active_scan,
                                                        ble_dedupe,
                                                        ble_intervals[ ble_duty ],
                                                        ble_windows[ ble_duty ] );
            }
            const esp_err_t result = radio_selected == 1U ? ( next && !collectors_activated ? ESP_OK
                                                              : next ? radio_monitor_start()
                                                                     : radio_monitor_stop() )
                                                          : ( next && !collectors_activated ? ESP_OK
                                                              : next ? ble_monitor_start()
                                                                     : ble_monitor_stop() );
            if( ESP_OK == result )
            {
                *enabled = next;
                if( !next )
                {
                    observation_store_clear_source( radio_selected == 1U ? RF_SOURCE_WIFI
                                                                         : RF_SOURCE_BLE );
                }
                save_radio_settings();
            }
        }
        else if( radio_selected == 3U && wifi_source )
        {
            radio_page = 1U;
            radio_selected = 0U;
        }
        else if( radio_selected == 4U && ble_source )
        {
            radio_page = 2U;
            radio_selected = 0U;
        }
    }
    else if( radio_page == 3U )
    {
        const uint16_t prior_mask = selected_channels;
        selected_channels ^= (uint16_t)( 1U << ( radio_selected - 1U ) );
        if( selected_channels == 0U )
        {
            if( ESP_OK == radio_monitor_stop() )
            {
                wifi_source = false;
                observation_store_clear_source( RF_SOURCE_WIFI );
                radio_page = 4U;
                menu_active = false;
            }
            else
            {
                selected_channels = prior_mask;
            }
        }
        else
        {
            const esp_err_t result =
                radio_monitor_set_manual_channels( manual_channels,
                                                   selected_channels,
                                                   shuffle_channels,
                                                   dwell_choices[ dwell_choice ] );
            if( ESP_OK != result )
            {
                selected_channels = prior_mask;
            }
        }
        save_radio_settings();
    }
    else if( radio_page == 1U )
    {
        if( radio_selected == 1U )
        {
            manual_channels = !manual_channels;
        }
        else if( radio_selected == 2U && manual_channels )
        {
            radio_page = 3U;
            radio_selected = 0U;
        }
        else if( radio_selected == 3U && manual_channels )
        {
            shuffle_channels = !shuffle_channels;
        }
        else if( radio_selected == 4U && manual_channels )
        {
            dwell_choice = ( dwell_choice + 1U ) % 5U;
        }
        else if( radio_selected == 5U )
        {
            ap_dwell_enabled = !ap_dwell_enabled;
            radio_monitor_set_ap_dwell_enabled( ap_dwell_enabled );
        }
        (void)radio_monitor_set_manual_channels(
            manual_channels, selected_channels, shuffle_channels, dwell_choices[ dwell_choice ] );
        save_radio_settings();
    }
    else if( radio_page == 2U )
    {
        const bool was_active = ble_source && ble_monitor_is_active();
        if( was_active && ESP_OK != ble_monitor_stop() )
        {
            render_radio_menu();
            return;
        }
        if( radio_selected == 1U )
        {
            ble_active_scan = !ble_active_scan;
        }
        else if( radio_selected == 2U )
        {
            ble_dedupe = !ble_dedupe;
        }
        else if( radio_selected == 3U )
        {
            ble_duty = ( ble_duty + 1U ) % 3U;
        }
        if( ESP_OK ==
            ble_monitor_set_scan_preferences(
                ble_active_scan, ble_dedupe, ble_intervals[ ble_duty ], ble_windows[ ble_duty ] ) )
        {
            save_radio_settings();
            if( was_active )
            {
                (void)ble_monitor_start();
            }
        }
    }
    if( menu_active && radio_page != 4U )
    {
        render_radio_menu();
    }
}

static void render_menu( void )
{
    if( radio_page != 4U )
    {
        render_radio_menu();
        return;
    }
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
    if( !wifi_source && ( radio_monitor_is_active() || radio_monitor_cleanup_pending() ) )
    {
        result = radio_monitor_stop();
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "Wi-Fi monitor stop failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
    if( !ble_source && ble_monitor_is_active() )
    {
        (void)ble_monitor_stop();
    }
    if( wifi_source && !radio_monitor_is_active() )
    {
        result = radio_monitor_set_manual_channels(
            manual_channels, selected_channels, shuffle_channels, dwell_choices[ dwell_choice ] );
        if( ESP_OK == result )
        {
            result = radio_monitor_start();
        }
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "Wi-Fi monitor start failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
    if( ble_source && !ble_monitor_is_active() )
    {
        result = ble_monitor_set_scan_preferences(
            ble_active_scan, ble_dedupe, ble_intervals[ ble_duty ], ble_windows[ ble_duty ] );
        if( ESP_OK == result )
        {
            result = ble_monitor_start();
        }
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
    radio_page = 4U;
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
    if( radio_page != 4U )
    {
        handle_radio_menu( a_input );
        return;
    }
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( menu_editing )
        {
            menu_editing = false;
            render_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( WIFI_MENU_SOURCES == menu_item )
        {
            radio_page = 0U;
            radio_selected = 0U;
            render_menu();
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
        load_radio_settings();
        ui_reset();
        app_manager_request_render( RFSCOUT_APP_WIFI_BT, true );
        collectors_activated = false;
        menu_active = false;
        radio_page = 4U;
        menu_editing = false;
        menu_item = WIFI_MENU_BACK;
        surveillance_history_open();
        observation_store_set_location_sink( surveillance_history_record );
        last_radio_retry_us = 0;
        last_journal_flush_us = 0;
        app_notification_menu_init(
            &notification_menu, RFSCOUT_APP_WIFI_BT, render_menu_callback, NULL );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        collectors_activated = true;
        last_radio_retry_us = a_event->now_us;
        start_collectors();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        collectors_activated = false;
        menu_active = false;
        radio_page = 4U;
        menu_editing = false;
        menu_item = WIFI_MENU_BACK;
        observation_store_set_location_sink( NULL );
        surveillance_history_flush_final();
        surveillance_history_close();
        app_notification_menu_reset( &notification_menu );
        (void)radio_monitor_set_manual_channels( false, 0U, false, 250U );
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
    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        if( !menu_active && !app_notification_menu_active( &notification_menu ) )
        {
            ui_render();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
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

    if( ( ( wifi_source && !radio_monitor_is_active() ) ||
          ( !wifi_source && ( radio_monitor_is_active() || radio_monitor_cleanup_pending() ) ) ||
          ( ble_source && !ble_monitor_is_active() ) ) &&
        ( a_event->now_us - last_radio_retry_us >= WIFI_RADIO_RETRY_INTERVAL_US ) )
    {
        last_radio_retry_us = a_event->now_us;
        start_collectors();
    }
    const uint16_t ap_mask = observation_store_wifi_ap_channel_mask();
    radio_monitor_set_active_ap_channels( ap_mask );
    radio_monitor_step();

    /* Report only actual changes to the retained 14-channel AP state. */
    static uint16_t previous_ap_mask;
    if( ap_mask != previous_ap_mask )
    {
        ESP_LOGI( TAG,
                  "AP channels: 0x%04X -> 0x%04X changed=0x%04X",
                  (unsigned)previous_ap_mask,
                  (unsigned)ap_mask,
                  (unsigned)( previous_ap_mask ^ ap_mask ) );
        previous_ap_mask = ap_mask;
    }
    if( a_event->now_us - last_journal_flush_us >= WIFI_JOURNAL_FLUSH_INTERVAL_US )
    {
        last_journal_flush_us = a_event->now_us;
        surveillance_history_flush();
    }
    if( !menu_active && !app_notification_menu_active( &notification_menu ) )
    {
        ui_poll();
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

extern void rfscout_observation_consume( rf_device_t *a_observation );

const rfscout_app_descriptor_t rfscout_wifi_ble_app = {
    .id = RFSCOUT_APP_WIFI_BT,
    .name = "WiFi / BT",
    .persistence_key = "wifi_ble",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_BLE_MONITOR,
    .consumers =
        {
            .wifi_observation = rfscout_observation_consume,
            .ble_observation = rfscout_observation_consume,
        },
    .runtime = wifi_ble_runtime,
};
