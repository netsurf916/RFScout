/**
    app_manager.c : RFScout application view manager implementation
    Description: Owns application navigation, lifecycle transitions, menus, semantic input, and active rendering.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "audio_spectrum.h"
#include "audio_view.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "device_config.h"
#include "device_view.h"
#include "event.h"
#include "mlx90640.h"
#include "morse.h"
#include "notification.h"
#include "notification_preferences.h"
#include "notification_settings_view.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "nrf24_packet_view.h"
#include "observation_store.h"
#include "platform.h"
#include "radio_app_view.h"
#include "radio_monitor.h"
#include "recorder.h"
#include "session_state.h"
#include "surveillance_history.h"
#include "thermal_view.h"
#include "ui.h"
#include "wifi_settings_view.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#define APP_REFRESH_INTERVAL_US 125000LL
#define AUDIO_REFRESH_INTERVAL_US 100000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_RADIO_RETRY_INTERVAL_US 1000000LL
#define APP_ACTIVATION_DELAY_US 500000LL

#define WIFI_MENU_BACK 0U
#define WIFI_MENU_NOTIFICATIONS 1U
#define WIFI_MENU_WIFI_TIMEOUT 2U
#define WIFI_MENU_BLE_TIMEOUT 3U
#define WIFI_MENU_SURVEILLANCE_TIMEOUT 4U
#define WIFI_MENU_RESET 5U
#define WIFI_MENU_ITEM_COUNT 6U

#define CC1101_MIN_STEP_KHZ 1U
#define CC1101_MAX_STEP_KHZ 1000U
#define CC1101_MENU_BACK 0U
#define CC1101_MENU_PROFILE 1U
#define CC1101_MENU_LOW 2U
#define CC1101_MENU_HIGH 3U
#define CC1101_MENU_STEP 4U
#define CC1101_MENU_NOTIFICATIONS 5U
#define CC1101_MENU_RESET_COUNTS 6U
#define CC1101_MENU_ITEM_COUNT 7U

#define NRF24_MENU_BACK 0U
#define NRF24_MENU_LOW 1U
#define NRF24_MENU_HIGH 2U
#define NRF24_MENU_DWELL 3U
#define NRF24_MENU_PACKETS 4U
#define NRF24_MENU_NOTIFICATIONS 5U
#define NRF24_MENU_RESET_COUNTS 6U
#define NRF24_MENU_ITEM_COUNT 7U

#define NOTIFICATION_MENU_BACK 0U
#define NOTIFICATION_MENU_ENABLED 1U
#define NOTIFICATION_MENU_FIRST_MAPPING 2U
#define NOTIFICATION_MENU_WIFI_EVENT_COUNT 2U

#define AUDIO_BROWSER_BACK 0U
#define AUDIO_BROWSER_NEW 1U
#define AUDIO_BROWSER_MONITOR 2U
#define AUDIO_BROWSER_FILE_BASE 3U

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static int64_t last_radio_retry_us;
static int64_t app_entered_us;
static bool app_activated;
static bool wifi_menu;
static bool wifi_edit;
static bool cc_menu;
static bool cc_edit;
static bool cc_limit;
static bool nr_menu;
static bool nr_edit;
static bool nr_packet_browser;
static bool nr_packet_detail;
static bool audio_menu;
static bool audio_edit;
static bool audio_delete_confirm;
static bool device_menu;
static bool notification_menu;
static bool notification_edit;
static bool wifi_monitor_ready;
static bool ble_monitor_ready;
static unsigned wifi_item;
static unsigned cc_item;
static unsigned nr_item;
static unsigned nr_packet_item;
static unsigned audio_item;
static unsigned notification_item;
static rfscout_recorder_state_t audio_last_state = RFSCOUT_RECORDER_IDLE;
static uint16_t cc_notification_counts[ CC1101_FP_TYPE_COUNT ];
static uint32_t nr_notification_counts[ NRF24_FP_LOGITECH_LIKE + 1U ];

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)active_app, a_value );
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

static bool probe_thermal( void )
{
    const bool was_detected = mlx90640_is_detected();
    const bool now_detected = ( ESP_OK == mlx90640_probe() );
    if( now_detected && !was_detected )
    {
        ESP_LOGI( TAG, "Thermal app available" );
    }
    else if( !now_detected && was_detected )
    {
        ESP_LOGW( TAG, "Thermal app unavailable" );
    }
    return now_detected;
}

static bool app_available( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_DEVICE == a_app )
    {
        return true;
    }
    if( !device_config_app_enabled( a_app ) )
    {
        return false;
    }
    return ( RFSCOUT_APP_THERMAL != a_app ) || mlx90640_is_detected();
}

static void start_wifi_bt_collectors( void )
{
    esp_err_t result;
    if( !wifi_monitor_ready )
    {
        result = radio_monitor_start();
        wifi_monitor_ready = ( ESP_OK == result );
        if( !wifi_monitor_ready )
        {
            ESP_LOGW( TAG, "Wi-Fi monitor start failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
    if( !ble_monitor_ready )
    {
        result = ble_monitor_start();
        ble_monitor_ready = ( ESP_OK == result );
        if( !ble_monitor_ready )
        {
            ESP_LOGW( TAG, "BLE monitor start failed: %s; retrying", esp_err_to_name( result ) );
        }
    }
}

static bool app_requires_activation( rfscout_app_id_t a_app )
{
    return ( RFSCOUT_APP_WIFI_BT == a_app ) || ( RFSCOUT_APP_CC1101 == a_app ) ||
           ( RFSCOUT_APP_NRF24 == a_app ) || ( RFSCOUT_APP_THERMAL == a_app );
}

static void log_lifecycle( const char *a_phase,
                           rfscout_app_id_t a_from,
                           rfscout_app_id_t a_to,
                           int64_t a_elapsed_us )
{
    ESP_LOGI( TAG,
              "%s from=%d to=%d elapsed=%lldus heap=%u min=%u largest=%u",
              a_phase,
              (int)a_from,
              (int)a_to,
              (long long)a_elapsed_us,
              (unsigned)heap_caps_get_free_size( MALLOC_CAP_8BIT ),
              (unsigned)heap_caps_get_minimum_free_size( MALLOC_CAP_8BIT ),
              (unsigned)heap_caps_get_largest_free_block( MALLOC_CAP_8BIT ) );
}

static void activate_app( void )
{
    if( app_activated )
    {
        return;
    }

    const int64_t started = esp_timer_get_time();
    app_activated = true;
    switch( active_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            last_radio_retry_us = started;
            start_wifi_bt_collectors();
            break;
        case RFSCOUT_APP_CC1101:
            if( ESP_OK != cc1101_start() )
            {
                ESP_LOGW( TAG, "CC1101 unavailable" );
            }
            break;
        case RFSCOUT_APP_NRF24:
            if( ESP_OK != nrf24_start() )
            {
                ESP_LOGW( TAG, "nRF24 unavailable" );
            }
            break;
        case RFSCOUT_APP_THERMAL:
        case RFSCOUT_APP_AUDIO:
        case RFSCOUT_APP_DEVICE:
        case RFSCOUT_APP_COUNT:
        default:
            break;
    }
    log_lifecycle( "activate", active_app, active_app, esp_timer_get_time() - started );
}

static void activate_app_if_ready( int64_t a_now )
{
    if( !app_activated && ( a_now - app_entered_us >= APP_ACTIVATION_DELAY_US ) )
    {
        activate_app();
    }
}

static void notify_detection( rfscout_notification_type_t a_type )
{
    if( !notification_emit( a_type ) )
    {
        ESP_LOGW( TAG, "invalid notification type=%d", (int)a_type );
    }
}

static rfscout_notification_type_t cc_notification_type( cc1101_fingerprint_type_t a_type )
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

static rfscout_notification_type_t nr_notification_type( nrf24_fingerprint_type_t a_type )
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

static void render_wifi_menu( void )
{
    wifi_settings_view_render( wifi_item,
                               wifi_edit,
                               notification_preferences_app_enabled( RFSCOUT_APP_WIFI_BT ),
                               device_config_wifi_timeout_seconds(),
                               device_config_ble_timeout_seconds(),
                               device_config_surveillance_timeout_seconds() );
}

static void render_cc_menu( void )
{
    const cc1101_scan_config_t *config = cc1101_config();
    radio_app_view_cc1101_menu( cc_item,
                                cc_edit,
                                cc1101_profile_name( config->profile ),
                                config->low_khz,
                                config->high_khz,
                                config->step_khz,
                                cc1101_sweep_count(),
                                cc_limit,
                                notification_preferences_app_enabled( RFSCOUT_APP_CC1101 ) );
}

static void render_nr_menu( void )
{
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_menu( nr_item,
                               nr_edit,
                               config->low_channel,
                               config->high_channel,
                               config->dwell_us,
                               notification_preferences_app_enabled( RFSCOUT_APP_NRF24 ) );
}

static void render_parent_menu( void )
{
    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        render_wifi_menu();
    }
    else if( RFSCOUT_APP_CC1101 == active_app )
    {
        render_cc_menu();
    }
    else if( RFSCOUT_APP_NRF24 == active_app )
    {
        render_nr_menu();
    }
}

static unsigned notification_mapping_base( void )
{
    return NOTIFICATION_MENU_FIRST_MAPPING +
           ( ( RFSCOUT_APP_WIFI_BT == active_app ) ? NOTIFICATION_MENU_WIFI_EVENT_COUNT : 0U );
}

static void open_notification_menu( void )
{
    notification_menu = true;
    notification_edit = false;
    notification_item = NOTIFICATION_MENU_BACK;
    notification_settings_view_render( active_app, notification_item, notification_edit );
    emit_event( RFSCOUT_EVENT_ENTER, 0 );
}

static bool close_notification_menu( void )
{
    const esp_err_t save_result = device_config_save( false, 0, NULL );
    if( ESP_OK != save_result && ESP_ERR_INVALID_STATE != save_result )
    {
        ESP_LOGW( TAG, "notification settings save failed: %s", esp_err_to_name( save_result ) );
        notification_settings_view_render( active_app, notification_item, notification_edit );
        emit_event( RFSCOUT_EVENT_ERROR, (int32_t)save_result );
        return false;
    }

    notification_menu = false;
    notification_edit = false;
    notification_item = NOTIFICATION_MENU_BACK;
    render_parent_menu();
    return true;
}

static void notification_input( bool a_pressed, int a_delta )
{
    const size_t notification_count = notification_count_for_app( active_app );
    const unsigned first_mapping = notification_mapping_base();
    const unsigned item_count = first_mapping + (unsigned)notification_count;
    if( item_count <= first_mapping )
    {
        (void)close_notification_menu();
        return;
    }

    if( a_pressed )
    {
        if( notification_edit )
        {
            notification_edit = false;
            notification_settings_view_render( active_app, notification_item, false );
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( NOTIFICATION_MENU_BACK == notification_item )
        {
            if( close_notification_menu() )
            {
                emit_event( RFSCOUT_EVENT_BACK, 0 );
            }
            return;
        }
        if( NOTIFICATION_MENU_ENABLED == notification_item )
        {
            const bool old_value = notification_preferences_app_enabled( active_app );
            if( ESP_OK == notification_preferences_set_app_enabled( active_app, !old_value ) )
            {
                notification_settings_view_render( active_app, notification_item, false );
                emit_event( !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                            !old_value ? 1 : 0 );
            }
            return;
        }
        if( ( RFSCOUT_APP_WIFI_BT == active_app ) &&
            ( notification_item >= NOTIFICATION_MENU_FIRST_MAPPING ) &&
            ( notification_item < first_mapping ) )
        {
            const rfscout_wifi_detection_event_t event =
                (rfscout_wifi_detection_event_t)( notification_item -
                                                  NOTIFICATION_MENU_FIRST_MAPPING );
            const bool old_value = notification_preferences_wifi_detection_enabled( event );
            if( ESP_OK == notification_preferences_set_wifi_detection_enabled( event, !old_value ) )
            {
                notification_settings_view_render( active_app, notification_item, false );
                emit_event( !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                            !old_value ? 1 : 0 );
            }
            return;
        }
        notification_edit = true;
        notification_settings_view_render( active_app, notification_item, true );
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return;
    }

    if( 0 == a_delta )
    {
        return;
    }
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    if( notification_edit )
    {
        if( notification_item < first_mapping )
        {
            return;
        }
        const rfscout_notification_descriptor_t *descriptor =
            notification_descriptor_for_app( active_app, notification_item - first_mapping );
        if( NULL == descriptor )
        {
            return;
        }
        const char old_character = device_config_notification_character( descriptor->type );
        const char new_character = next_morse_character( old_character, direction );
        if( ( new_character != old_character ) &&
            ( ESP_OK ==
              device_config_set_notification_character( descriptor->type, new_character ) ) )
        {
            notification_settings_view_render( active_app, notification_item, true );
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    int item = (int)notification_item + direction;
    if( item < 0 )
    {
        item = (int)item_count - 1;
    }
    if( item >= (int)item_count )
    {
        item = 0;
    }
    if( (unsigned)item != notification_item )
    {
        notification_item = (unsigned)item;
        notification_settings_view_render( active_app, notification_item, false );
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void close_wifi_menu( void )
{
    const esp_err_t save_result = device_config_save_if_dirty();
    if( ( ESP_OK != save_result ) && ( ESP_ERR_INVALID_STATE != save_result ) )
    {
        ESP_LOGW( TAG, "WiFi/BT settings save failed: %s", esp_err_to_name( save_result ) );
    }
    wifi_menu = false;
    wifi_edit = false;
    wifi_item = WIFI_MENU_BACK;
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

static void wifi_input( bool a_pressed, int a_delta )
{
    if( !wifi_menu )
    {
        if( a_pressed )
        {
            wifi_menu = true;
            wifi_edit = false;
            wifi_item = WIFI_MENU_BACK;
            render_wifi_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
        }
        return;
    }
    if( a_pressed )
    {
        if( wifi_edit )
        {
            wifi_edit = false;
            render_wifi_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( WIFI_MENU_BACK == wifi_item )
        {
            close_wifi_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( WIFI_MENU_NOTIFICATIONS == wifi_item )
        {
            open_notification_menu();
            return;
        }
        if( ( WIFI_MENU_WIFI_TIMEOUT == wifi_item ) || ( WIFI_MENU_BLE_TIMEOUT == wifi_item ) ||
            ( WIFI_MENU_SURVEILLANCE_TIMEOUT == wifi_item ) )
        {
            wifi_edit = true;
            render_wifi_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        }
        if( WIFI_MENU_RESET == wifi_item )
        {
            observation_store_reset();
            surveillance_history_reset();
            close_wifi_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
    }
    if( 0 == a_delta )
    {
        return;
    }

    const int direction = ( a_delta > 0 ) ? 1 : -1;
    if( wifi_edit )
    {
        uint16_t old_value = 0U;
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if( WIFI_MENU_WIFI_TIMEOUT == wifi_item )
        {
            old_value = device_config_wifi_timeout_seconds();
            result = device_config_set_wifi_timeout_seconds(
                adjust_timeout_seconds( old_value, direction ) );
        }
        else if( WIFI_MENU_BLE_TIMEOUT == wifi_item )
        {
            old_value = device_config_ble_timeout_seconds();
            result = device_config_set_ble_timeout_seconds(
                adjust_timeout_seconds( old_value, direction ) );
        }
        else if( WIFI_MENU_SURVEILLANCE_TIMEOUT == wifi_item )
        {
            old_value = device_config_surveillance_timeout_seconds();
            result = device_config_set_surveillance_timeout_seconds(
                adjust_timeout_seconds( old_value, direction ) );
        }
        if( ESP_OK == result )
        {
            uint16_t new_value = old_value;
            if( WIFI_MENU_WIFI_TIMEOUT == wifi_item )
            {
                new_value = device_config_wifi_timeout_seconds();
            }
            else if( WIFI_MENU_BLE_TIMEOUT == wifi_item )
            {
                new_value = device_config_ble_timeout_seconds();
            }
            else if( WIFI_MENU_SURVEILLANCE_TIMEOUT == wifi_item )
            {
                new_value = device_config_surveillance_timeout_seconds();
            }
            if( new_value != old_value )
            {
                render_wifi_menu();
                emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
            }
        }
        return;
    }

    int item = (int)wifi_item + direction;
    if( item < 0 )
    {
        item = (int)WIFI_MENU_ITEM_COUNT - 1;
    }
    if( item >= (int)WIFI_MENU_ITEM_COUNT )
    {
        item = 0;
    }
    if( (unsigned)item != wifi_item )
    {
        wifi_item = (unsigned)item;
        render_wifi_menu();
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void notify_cc_detections( const cc1101_fingerprint_t *a_devices, size_t a_device_count )
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
        if( count > cc_notification_counts[ type ] )
        {
            notify_detection( cc_notification_type( type ) );
        }
        cc_notification_counts[ type ] = count;
    }
}

static void render_cc_spectrum( void )
{
    uint8_t current[ CC1101_SPECTRUM_BINS ];
    cc1101_fingerprint_t devices[ CC1101_FINGERPRINT_MAX ];
    const size_t count = cc1101_activity_levels( current, CC1101_SPECTRUM_BINS );
    const size_t device_count = cc1101_activity_fingerprints( devices, CC1101_FINGERPRINT_MAX );
    notify_cc_detections( devices, device_count );
    const cc1101_scan_config_t *config = cc1101_config();
    radio_app_view_cc1101_spectrum( current,
                                    count,
                                    config->low_khz,
                                    config->high_khz,
                                    cc1101_is_detected(),
                                    devices,
                                    device_count );
}

static void observe_cc_spectrum( void )
{
    int8_t rssi[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( rssi, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *config = cc1101_config();
    cc1101_activity_observe_sweep( rssi, count, config->low_khz, config->high_khz );
}

static void notify_nr_detections( void )
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
        if( count > nr_notification_counts[ type ] )
        {
            notify_detection( nr_notification_type( type ) );
        }
        nr_notification_counts[ type ] = count;
    }
}

static void render_nr_spectrum( void )
{
    uint8_t fast[ NRF24_CHANNEL_COUNT ];
    nrf24_fingerprint_t devices[ NRF24_FINGERPRINT_MAX ];
    const size_t count = nrf24_activity_histogram( fast, NRF24_CHANNEL_COUNT );
    const size_t device_count = nrf24_activity_fingerprints( devices, NRF24_FINGERPRINT_MAX );
    notify_nr_detections();
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_spectrum( fast,
                                   count,
                                   config->low_channel,
                                   config->high_channel,
                                   nrf24_is_detected(),
                                   devices,
                                   device_count );
}

static size_t nr_packet_count( void )
{
    nrf24_recent_packet_t packets[ NRF24_RECENT_PACKET_MAX ];
    return nrf24_activity_recent_packets( packets, NRF24_RECENT_PACKET_MAX ) + 1U;
}

static void render_audio( void )
{
    audio_view_render( NULL, NULL );
}

static bool leave_app( rfscout_app_id_t a_app )
{
    bool quiesced = true;
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        if( app_activated )
        {
            if( ESP_OK != ble_monitor_stop() )
            {
                quiesced = false;
            }
            if( ESP_OK != radio_monitor_stop() )
            {
                quiesced = false;
            }
        }
        wifi_monitor_ready = false;
        ble_monitor_ready = false;
        wifi_item = WIFI_MENU_BACK;
    }
    else if( RFSCOUT_APP_CC1101 == a_app )
    {
        if( app_activated && !cc1101_stop() )
        {
            return false;
        }
        cc1101_activity_reset();
        cc1101_activity_reset_counts();
        memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
        cc_item = CC1101_MENU_BACK;
    }
    else if( RFSCOUT_APP_NRF24 == a_app )
    {
        if( app_activated && !nrf24_stop() )
        {
            return false;
        }
        nrf24_activity_reset();
        memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
        nr_item = NRF24_MENU_BACK;
        nr_packet_item = 0U;
    }
    else if( RFSCOUT_APP_AUDIO == a_app )
    {
        recorder_stop();
        audio_spectrum_stop();
        audio_item = AUDIO_BROWSER_BACK;
    }
    wifi_menu = false;
    wifi_edit = false;
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
    nr_packet_browser = false;
    nr_packet_detail = false;
    audio_menu = false;
    audio_edit = false;
    audio_delete_confirm = false;
    device_menu = false;
    notification_menu = false;
    notification_edit = false;
    return quiesced;
}

static void enter_app( rfscout_app_id_t a_app )
{
    last_app_refresh_us = 0;
    app_entered_us = esp_timer_get_time();
    app_activated = !app_requires_activation( a_app );
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            wifi_item = WIFI_MENU_BACK;
            wifi_menu = false;
            wifi_edit = false;
            wifi_monitor_ready = false;
            ble_monitor_ready = false;
            last_radio_retry_us = 0;
            break;
        case RFSCOUT_APP_CC1101:
            cc_item = CC1101_MENU_BACK;
            cc_menu = false;
            cc_edit = false;
            cc_limit = false;
            memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
            cc1101_activity_reset();
            render_cc_spectrum();
            break;
        case RFSCOUT_APP_NRF24:
            nr_item = NRF24_MENU_BACK;
            nr_menu = false;
            nr_edit = false;
            nr_packet_browser = false;
            nr_packet_detail = false;
            nr_packet_item = 0U;
            memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
            nrf24_activity_reset();
            render_nr_spectrum();
            break;
        case RFSCOUT_APP_AUDIO:
            audio_item = AUDIO_BROWSER_BACK;
            audio_menu = false;
            audio_edit = false;
            audio_last_state = RFSCOUT_RECORDER_IDLE;
            if( ESP_OK != recorder_init() )
            {
                ESP_LOGW( TAG, "Recorder initialization unavailable" );
            }
            render_audio();
            break;
        case RFSCOUT_APP_THERMAL:
            thermal_view_enter();
            break;
        case RFSCOUT_APP_DEVICE:
            device_menu = false;
            device_view_landing();
            break;
        case RFSCOUT_APP_COUNT:
        default:
            break;
    }
    ESP_LOGI( TAG, "switched app=%d", (int)a_app );
}

static rfscout_app_id_t next_available_app( int a_direction, int64_t a_now )
{
    int next = (int)active_app;
    for( int attempts = 0; attempts < RFSCOUT_APP_COUNT; ++attempts )
    {
        next += a_direction;
        if( next < 0 )
        {
            next = RFSCOUT_APP_COUNT - 1;
        }
        else if( next >= RFSCOUT_APP_COUNT )
        {
            next = 0;
        }
        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;
        if( !app_available( candidate ) )
        {
            if( RFSCOUT_APP_THERMAL == candidate )
            {
                last_thermal_probe_us = a_now;
                if( device_config_app_enabled( candidate ) && probe_thermal() )
                {
                    return candidate;
                }
            }
            continue;
        }
        return candidate;
    }
    return active_app;
}

static bool switch_app_for_delta( int a_delta, int64_t a_now )
{
    if( 0 == a_delta )
    {
        return false;
    }
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    const rfscout_app_id_t candidate = next_available_app( direction, a_now );
    if( candidate == active_app )
    {
        return false;
    }
    const rfscout_app_id_t previous = active_app;
    const int64_t transition_started = esp_timer_get_time();
    if( !leave_app( previous ) )
    {
        log_lifecycle(
            "quiesce-timeout", previous, candidate, esp_timer_get_time() - transition_started );
        return false;
    }
    active_app = candidate;
    enter_app( active_app );
    log_lifecycle( "transition", previous, active_app, esp_timer_get_time() - transition_started );
    rfscout_event_emit( RFSCOUT_EVENT_NAVIGATE, (uint8_t)active_app, direction );
    return true;
}

static bool cc_step_at_endpoint( uint32_t a_step, int a_delta )
{
    return ( ( a_delta > 0 ) && ( a_step >= CC1101_MAX_STEP_KHZ ) ) ||
           ( ( a_delta < 0 ) && ( a_step <= CC1101_MIN_STEP_KHZ ) );
}

static void close_cc_menu( void )
{
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    render_cc_spectrum();
}

static void cc_input( bool a_pressed, int a_delta )
{
    if( !cc_menu )
    {
        if( a_pressed )
        {
            cc_menu = true;
            cc_edit = false;
            cc_limit = false;
            cc_item = CC1101_MENU_BACK;
            render_cc_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
        }
        return;
    }
    if( a_pressed )
    {
        if( CC1101_MENU_BACK == cc_item )
        {
            close_cc_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( CC1101_MENU_NOTIFICATIONS == cc_item )
        {
            open_notification_menu();
            return;
        }
        if( CC1101_MENU_RESET_COUNTS == cc_item )
        {
            cc1101_activity_reset_counts();
            memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
            close_cc_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        cc_edit = !cc_edit;
        cc_limit = false;
        render_cc_menu();
        emit_event( RFSCOUT_EVENT_ACTIVATE, cc_edit ? 1 : 0 );
        return;
    }
    if( 0 == a_delta )
    {
        return;
    }
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    if( !cc_edit )
    {
        int item = (int)cc_item + direction;
        if( item < 0 )
        {
            item = (int)CC1101_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)CC1101_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        if( (unsigned)item != cc_item )
        {
            cc_item = (unsigned)item;
            cc_limit = false;
            render_cc_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    const cc1101_scan_config_t before = *cc1101_config();
    bool changed = true;
    bool endpoint = false;
    if( CC1101_MENU_PROFILE == cc_item )
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
        memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
    }
    else if( CC1101_MENU_LOW == cc_item )
    {
        changed = cc1101_adjust_low( direction );
    }
    else if( CC1101_MENU_HIGH == cc_item )
    {
        changed = cc1101_adjust_high( direction );
    }
    else if( CC1101_MENU_STEP == cc_item )
    {
        endpoint = cc_step_at_endpoint( before.step_khz, a_delta );
        changed = cc1101_adjust_step( direction );
    }
    else
    {
        changed = false;
    }
    if( changed && ( cc_item >= CC1101_MENU_LOW ) && ( cc_item <= CC1101_MENU_STEP ) )
    {
        cc1101_activity_reset();
        memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
    }
    cc_limit = !changed && !endpoint;
    render_cc_menu();
    if( changed )
    {
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void close_nr_menu( void )
{
    nr_menu = false;
    nr_edit = false;
    nr_packet_browser = false;
    nr_packet_detail = false;
    nr_packet_item = 0U;
    render_nr_spectrum();
}

static void nr_input( bool a_pressed, int a_delta )
{
    if( nr_packet_browser )
    {
        const size_t item_count = nr_packet_count();
        if( nr_packet_detail )
        {
            if( a_pressed )
            {
                nr_packet_detail = false;
                (void)nrf24_packet_view_list( nr_packet_item );
                emit_event( RFSCOUT_EVENT_BACK, 0 );
            }
            return;
        }
        if( a_pressed )
        {
            if( 0U == nr_packet_item )
            {
                nr_packet_browser = false;
                nr_packet_item = 0U;
                render_nr_menu();
                emit_event( RFSCOUT_EVENT_BACK, 0 );
                return;
            }
            if( item_count > 1U )
            {
                if( nr_packet_item >= item_count )
                {
                    nr_packet_item = (unsigned)item_count - 1U;
                }
                nr_packet_detail = true;
                nrf24_packet_view_detail( nr_packet_item - 1U );
                emit_event( RFSCOUT_EVENT_ENTER, (int32_t)( nr_packet_item - 1U ) );
            }
            return;
        }
        if( 0 == a_delta )
        {
            return;
        }
        const int direction = ( a_delta > 0 ) ? 1 : -1;
        int item = (int)nr_packet_item + direction;
        if( item < 0 )
        {
            item = (int)item_count - 1;
        }
        if( item >= (int)item_count )
        {
            item = 0;
        }
        if( (unsigned)item != nr_packet_item )
        {
            nr_packet_item = (unsigned)item;
            (void)nrf24_packet_view_list( nr_packet_item );
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    if( !nr_menu )
    {
        if( a_pressed )
        {
            nr_menu = true;
            nr_edit = false;
            nr_item = NRF24_MENU_BACK;
            render_nr_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
        }
        return;
    }
    if( a_pressed )
    {
        if( NRF24_MENU_BACK == nr_item )
        {
            close_nr_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( NRF24_MENU_PACKETS == nr_item )
        {
            nr_packet_browser = true;
            nr_packet_detail = false;
            nr_packet_item = 0U;
            (void)nrf24_packet_view_list( nr_packet_item );
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        }
        if( NRF24_MENU_NOTIFICATIONS == nr_item )
        {
            open_notification_menu();
            return;
        }
        if( NRF24_MENU_RESET_COUNTS == nr_item )
        {
            nrf24_activity_reset_counts();
            memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
            close_nr_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        nr_edit = !nr_edit;
        render_nr_menu();
        emit_event( RFSCOUT_EVENT_ACTIVATE, nr_edit ? 1 : 0 );
        return;
    }
    if( 0 == a_delta )
    {
        return;
    }
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    if( !nr_edit )
    {
        int item = (int)nr_item + direction;
        if( item < 0 )
        {
            item = (int)NRF24_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)NRF24_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        if( (unsigned)item != nr_item )
        {
            nr_item = (unsigned)item;
            render_nr_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return;
    }

    const nrf24_scan_config_t before = *nrf24_config();
    if( NRF24_MENU_LOW == nr_item )
    {
        nrf24_adjust_low( direction );
    }
    else if( NRF24_MENU_HIGH == nr_item )
    {
        nrf24_adjust_high( direction );
    }
    else if( NRF24_MENU_DWELL == nr_item )
    {
        nrf24_adjust_dwell( direction );
    }
    else
    {
        return;
    }
    const nrf24_scan_config_t after = *nrf24_config();
    render_nr_menu();
    if( ( before.low_channel != after.low_channel ) ||
        ( before.high_channel != after.high_channel ) || ( before.dwell_us != after.dwell_us ) )
    {
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
    }
}

static void close_audio_menu( void )
{
    audio_menu = false;
    audio_edit = false;
    audio_delete_confirm = false;
    audio_item = AUDIO_BROWSER_BACK;
    render_audio();
}

static void show_audio_menu( void )
{
    audio_view_menu( audio_item, audio_edit, audio_delete_confirm );
}

static void save_recorder_volume( void )
{
    const esp_err_t result = device_config_save( false, 0, NULL );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not save Recorder volume: %s", esp_err_to_name( result ) );
    }
}

static void audio_input( bool a_pressed, bool a_long_pressed, int a_delta )
{
    if( !audio_menu )
    {
        if( a_pressed )
        {
            audio_menu = true;
            audio_item = AUDIO_BROWSER_BACK;
            (void)recorder_refresh();
            show_audio_menu();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
        }
        return;
    }
    const rfscout_recorder_state_t state = recorder_state();
    if( audio_edit )
    {
        if( 0 != a_delta )
        {
            audio_delete_confirm = !audio_delete_confirm;
            show_audio_menu();
            emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
            return;
        }
        if( a_pressed )
        {
            bool activated = false;
            if( audio_delete_confirm && ( audio_item >= AUDIO_BROWSER_FILE_BASE ) )
            {
                const size_t index = audio_item - AUDIO_BROWSER_FILE_BASE;
                const esp_err_t result = recorder_delete_file( index );
                activated = ( ESP_OK == result );
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG, "could not delete recording: %s", esp_err_to_name( result ) );
                }
                const unsigned item_count =
                    AUDIO_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
                if( audio_item >= item_count )
                {
                    audio_item =
                        item_count > AUDIO_BROWSER_FILE_BASE ? item_count - 1U : AUDIO_BROWSER_NEW;
                }
            }
            audio_edit = false;
            audio_delete_confirm = false;
            show_audio_menu();
            emit_event( activated ? RFSCOUT_EVENT_ACTIVATE : RFSCOUT_EVENT_BACK, 0 );
        }
        return;
    }
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        if( a_pressed || a_long_pressed )
        {
            recorder_request_stop_recording();
            audio_last_state = RFSCOUT_RECORDER_SAVING;
            show_audio_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        return;
    }
    if( RFSCOUT_RECORDER_SAVING == state )
    {
        return;
    }
    if( RFSCOUT_RECORDER_MONITORING == state )
    {
        if( a_pressed || a_long_pressed )
        {
            recorder_stop_monitor();
            save_recorder_volume();
            show_audio_menu();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            return;
        }
        if( 0 != a_delta )
        {
            const device_config_t *config = device_config_get();
            const int old_gain = ( NULL != config ) ? (int)config->recorder_input_gain_db : 12;
            int gain = old_gain + ( ( a_delta > 0 ) ? 3 : -3 );
            if( gain < 0 )
            {
                gain = 0;
            }
            if( gain > 24 )
            {
                gain = 24;
            }
            if( ( gain != old_gain ) &&
                ( ESP_OK == device_config_set_recorder_input_gain( (uint8_t)gain ) ) )
            {
                recorder_set_input_gain_db( (uint8_t)gain );
                (void)device_config_save( false, 0, NULL );
                emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
            }
        }
        show_audio_menu();
        return;
    }
    if( RFSCOUT_RECORDER_PLAYING == state )
    {
        if( a_pressed )
        {
            recorder_stop();
            save_recorder_volume();
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        else if( 0 != a_delta )
        {
            const device_config_t *config = device_config_get();
            const int old_volume = ( NULL != config ) ? (int)config->recorder_volume_percent : 50;
            int volume = old_volume + ( ( a_delta > 0 ) ? 5 : -5 );
            if( volume < 0 )
            {
                volume = 0;
            }
            else if( volume > 100 )
            {
                volume = 100;
            }
            if( ( volume != old_volume ) &&
                ( ESP_OK == device_config_set_recorder_volume( (uint8_t)volume ) ) )
            {
                recorder_set_playback_volume( (uint8_t)volume );
                emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
            }
        }
        show_audio_menu();
        return;
    }
    if( a_long_pressed && ( audio_item >= AUDIO_BROWSER_FILE_BASE ) )
    {
        audio_edit = true;
        audio_delete_confirm = false;
        show_audio_menu();
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return;
    }
    if( a_pressed )
    {
        if( AUDIO_BROWSER_BACK == audio_item )
        {
            close_audio_menu();
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        }
        if( AUDIO_BROWSER_NEW == audio_item )
        {
            const device_config_t *config = device_config_get();
            recorder_set_input_gain_db( NULL != config ? config->recorder_input_gain_db : 12U );
            if( ESP_OK == recorder_start_recording() )
            {
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            show_audio_menu();
            return;
        }
        if( AUDIO_BROWSER_MONITOR == audio_item )
        {
            const device_config_t *config = device_config_get();
            if( ESP_OK ==
                recorder_start_monitor( NULL != config ? config->recorder_input_gain_db : 12U ) )
            {
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            show_audio_menu();
            return;
        }
        const size_t index = audio_item - AUDIO_BROWSER_FILE_BASE;
        const device_config_t *config = device_config_get();
        const uint8_t volume = ( NULL != config ) ? config->recorder_volume_percent : 50U;
        if( ESP_OK == recorder_start_playback( index, volume ) )
        {
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        show_audio_menu();
        return;
    }
    if( 0 == a_delta )
    {
        return;
    }
    const unsigned item_count = AUDIO_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
    int item = (int)audio_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)item_count - 1;
    }
    if( item >= (int)item_count )
    {
        item = 0;
    }
    if( (unsigned)item != audio_item )
    {
        audio_item = (unsigned)item;
        show_audio_menu();
        emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    }
}

static void close_active_menu_for_back( void )
{
    if( notification_menu )
    {
        if( notification_edit )
        {
            notification_edit = false;
            notification_settings_view_render( active_app, notification_item, false );
            emit_event( RFSCOUT_EVENT_BACK, 0 );
        }
        else if( close_notification_menu() )
        {
            emit_event( RFSCOUT_EVENT_BACK, 0 );
        }
        return;
    }
    if( RFSCOUT_APP_DEVICE == active_app && device_menu )
    {
        device_menu = false;
        device_view_landing();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_WIFI_BT == active_app && wifi_menu )
    {
        if( wifi_edit )
        {
            wifi_edit = false;
            render_wifi_menu();
        }
        else
        {
            close_wifi_menu();
        }
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_CC1101 == active_app && cc_menu )
    {
        close_cc_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_NRF24 == active_app && nr_packet_detail )
    {
        nr_packet_detail = false;
        (void)nrf24_packet_view_list( nr_packet_item );
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_NRF24 == active_app && nr_packet_browser )
    {
        nr_packet_browser = false;
        nr_packet_item = 0U;
        render_nr_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_NRF24 == active_app && nr_menu )
    {
        close_nr_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_AUDIO == active_app && audio_menu )
    {
        close_audio_menu();
        emit_event( RFSCOUT_EVENT_BACK, 0 );
    }
    else if( RFSCOUT_APP_DEVICE != active_app )
    {
        if( !leave_app( active_app ) )
        {
            emit_event( RFSCOUT_EVENT_ERROR, ESP_ERR_TIMEOUT );
            return;
        }
        active_app = RFSCOUT_APP_DEVICE;
        enter_app( active_app );
        rfscout_event_emit( RFSCOUT_EVENT_BACK, (uint8_t)RFSCOUT_APP_DEVICE, 0 );
    }
}

esp_err_t app_manager_init( void )
{
    active_app = device_config_startup_app();
    rfscout_session_state_t restored_session;
    const bool have_restored_session = session_state_restore( &restored_session );
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();
    last_radio_retry_us = 0;
    app_entered_us = 0;
    app_activated = false;
    wifi_menu = false;
    wifi_edit = false;
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
    nr_packet_browser = false;
    nr_packet_detail = false;
    audio_menu = false;
    audio_edit = false;
    audio_delete_confirm = false;
    device_menu = false;
    notification_menu = false;
    notification_edit = false;
    wifi_item = WIFI_MENU_BACK;
    cc_item = CC1101_MENU_BACK;
    nr_item = NRF24_MENU_BACK;
    nr_packet_item = 0U;
    audio_item = AUDIO_BROWSER_BACK;
    notification_item = NOTIFICATION_MENU_BACK;
    wifi_monitor_ready = false;
    ble_monitor_ready = false;
    memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
    memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );

    const esp_err_t result = ui_init();
    if( ESP_OK != result )
    {
        return result;
    }
    (void)mlx90640_init();
    if( have_restored_session && app_available( restored_session.active_app ) )
    {
        active_app = restored_session.active_app;
    }
    if( !app_available( active_app ) )
    {
        ESP_LOGW( TAG, "configured or retained app unavailable; using Home" );
        active_app = RFSCOUT_APP_DEVICE;
    }
    if( RFSCOUT_APP_DEVICE == active_app )
    {
        device_view_landing();
    }
    else
    {
        enter_app( active_app );
    }
    return ESP_OK;
}

rfscout_app_id_t app_manager_current( void )
{
    return active_app;
}

void app_manager_poll( void )
{
    const int64_t now = esp_timer_get_time();
    if( ( RFSCOUT_APP_THERMAL != active_app ) && device_config_app_enabled( RFSCOUT_APP_THERMAL ) &&
        ( now - last_thermal_probe_us >= APP_FEATURE_PROBE_INTERVAL_US ) )
    {
        (void)probe_thermal();
        last_thermal_probe_us = now;
    }

    bool pressed = false;
    bool long_pressed = false;
    bool back = false;
    int delta = 0;
    rfscout_input_event_t input;
    if( platform_input_get( &input ) )
    {
        switch( input.type )
        {
            case RFSCOUT_INPUT_NAVIGATION:
                delta = (int)input.value;
                break;
            case RFSCOUT_INPUT_SELECT:
                pressed = true;
                break;
            case RFSCOUT_INPUT_LONG_SELECT:
                long_pressed = true;
                break;
            case RFSCOUT_INPUT_BACK:
                back = true;
                break;
            case RFSCOUT_INPUT_CHARACTER:
            case RFSCOUT_INPUT_NONE:
            default:
                break;
        }
    }

    if( back )
    {
        close_active_menu_for_back();
        return;
    }
    if( notification_menu )
    {
        notification_input( pressed, delta );
        return;
    }

    if( RFSCOUT_APP_DEVICE == active_app )
    {
        if( device_menu )
        {
            if( device_view_input( pressed, delta ) )
            {
                device_menu = false;
                device_view_landing();
            }
            return;
        }
        if( pressed )
        {
            device_menu = true;
            device_view_enter();
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        }
        (void)switch_app_for_delta( delta, now );
        return;
    }

    if( RFSCOUT_APP_CC1101 == active_app )
    {
        if( cc_menu )
        {
            activate_app_if_ready( now );
            cc_input( pressed, delta );
            return;
        }
        if( pressed )
        {
            activate_app_if_ready( now );
            cc_input( true, 0 );
            return;
        }
        if( switch_app_for_delta( delta, now ) )
        {
            return;
        }
        activate_app_if_ready( now );
        if( !app_activated )
        {
            return;
        }
        cc1101_poll();
        if( cc1101_take_frame_ready() )
        {
            observe_cc_spectrum();
            render_cc_spectrum();
        }
        return;
    }

    if( RFSCOUT_APP_NRF24 == active_app )
    {
        if( nr_menu )
        {
            activate_app_if_ready( now );
            nr_input( pressed, delta );
            return;
        }
        if( pressed )
        {
            activate_app_if_ready( now );
            nr_input( true, 0 );
            return;
        }
        if( switch_app_for_delta( delta, now ) )
        {
            return;
        }
        activate_app_if_ready( now );
        if( !app_activated )
        {
            return;
        }
        nrf24_poll();
        if( nrf24_take_frame_ready() )
        {
            render_nr_spectrum();
        }
        return;
    }

    if( RFSCOUT_APP_AUDIO == active_app )
    {
        if( audio_menu )
        {
            audio_input( pressed, long_pressed, delta );
            const rfscout_recorder_state_t recorder_now = recorder_state();
            if( recorder_now != audio_last_state )
            {
                if( ( RFSCOUT_RECORDER_PLAYING == audio_last_state ) &&
                    ( RFSCOUT_RECORDER_IDLE == recorder_now ) )
                {
                    save_recorder_volume();
                }
                if( ( RFSCOUT_RECORDER_SAVING == audio_last_state ) &&
                    ( RFSCOUT_RECORDER_IDLE == recorder_now ) )
                {
                    audio_item =
                        recorder_file_count() > 0U ? AUDIO_BROWSER_FILE_BASE : AUDIO_BROWSER_NEW;
                }
                audio_last_state = recorder_now;
                last_app_refresh_us = now;
                show_audio_menu();
                return;
            }
            if( now - last_app_refresh_us >= AUDIO_REFRESH_INTERVAL_US )
            {
                last_app_refresh_us = now;
                if( ( RFSCOUT_RECORDER_RECORDING == recorder_now ) ||
                    ( RFSCOUT_RECORDER_SAVING == recorder_now ) ||
                    ( RFSCOUT_RECORDER_MONITORING == recorder_now ) ||
                    ( RFSCOUT_RECORDER_PLAYING == recorder_now ) )
                {
                    show_audio_menu();
                }
            }
            return;
        }
        if( pressed )
        {
            audio_input( true, false, 0 );
            return;
        }
        if( switch_app_for_delta( delta, now ) )
        {
            return;
        }
        if( now - last_app_refresh_us >= AUDIO_REFRESH_INTERVAL_US )
        {
            last_app_refresh_us = now;
            render_audio();
        }
        return;
    }

    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        if( wifi_menu )
        {
            activate_app_if_ready( now );
            wifi_input( pressed, delta );
        }
        else if( pressed )
        {
            activate_app_if_ready( now );
            wifi_input( true, 0 );
        }
        else if( switch_app_for_delta( delta, now ) )
        {
            return;
        }
        else
        {
            activate_app_if_ready( now );
        }
        if( !app_activated )
        {
            if( !wifi_menu )
            {
                ui_poll();
            }
            return;
        }
        if( ( !wifi_monitor_ready || !ble_monitor_ready ) &&
            ( now - last_radio_retry_us >= APP_RADIO_RETRY_INTERVAL_US ) )
        {
            last_radio_retry_us = now;
            start_wifi_bt_collectors();
        }
        radio_monitor_step();
        if( !wifi_menu )
        {
            ui_poll();
        }
        return;
    }

    if( switch_app_for_delta( delta, now ) )
    {
        return;
    }
    if( RFSCOUT_APP_THERMAL != active_app )
    {
        return;
    }
    activate_app_if_ready( now );
    if( !app_activated )
    {
        return;
    }
    if( !mlx90640_is_detected() )
    {
        leave_app( active_app );
        active_app = next_available_app( 1, now );
        enter_app( active_app );
        return;
    }
    if( now - last_app_refresh_us < APP_REFRESH_INTERVAL_US )
    {
        return;
    }
    last_app_refresh_us = now;
    const esp_err_t result = thermal_view_render();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) && !mlx90640_is_detected() )
    {
        leave_app( active_app );
        active_app = next_available_app( 1, now );
        enter_app( active_app );
    }
}
