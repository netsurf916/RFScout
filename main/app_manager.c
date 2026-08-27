/**
    app_manager.c : RFScout application view manager implementation
    Description: Owns application navigation, lifecycle transitions, menus, and active rendering.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "alert.h"
#include "audio_spectrum.h"
#include "audio_view.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "device_config.h"
#include "device_view.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "observation_store.h"
#include "platform.h"
#include "radio_app_view.h"
#include "radio_monitor.h"
#include "surveillance_history.h"
#include "thermal_view.h"
#include "ui.h"
#include "wifi_settings_view.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#define APP_REFRESH_INTERVAL_US 125000LL
#define AUDIO_REFRESH_INTERVAL_US 40000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_RADIO_RETRY_INTERVAL_US 1000000LL
#define WIFI_MENU_BACK 0U
#define WIFI_MENU_RESET 1U
#define WIFI_MENU_ITEM_COUNT 2U
#define CC1101_MIN_STEP_KHZ 1U
#define CC1101_MAX_STEP_KHZ 1000U
#define CC1101_MENU_BACK 0U
#define CC1101_MENU_PROFILE 1U
#define CC1101_MENU_LOW 2U
#define CC1101_MENU_HIGH 3U
#define CC1101_MENU_STEP 4U
#define CC1101_MENU_PEAK_HOLD 5U
#define CC1101_MENU_RESET_COUNTS 6U
#define CC1101_MENU_ITEM_COUNT 7U
#define NRF24_MENU_BACK 0U
#define NRF24_MENU_LOW 1U
#define NRF24_MENU_HIGH 2U
#define NRF24_MENU_DWELL 3U
#define NRF24_MENU_PEAK_HOLD 4U
#define NRF24_MENU_RESET_COUNTS 5U
#define NRF24_MENU_ITEM_COUNT 6U
#define AUDIO_MENU_BACK 0U
#define AUDIO_MENU_RECORD 1U
#define AUDIO_MENU_FILE 2U
#define AUDIO_MENU_PLAY 3U
#define AUDIO_MENU_VOLUME 4U
#define AUDIO_MENU_ITEM_COUNT 5U

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static int64_t last_radio_retry_us;
static bool wifi_menu;
static bool wifi_edit;
static bool cc_menu;
static bool cc_edit;
static bool cc_limit;
static bool nr_menu;
static bool nr_edit;
static bool audio_menu;
static bool audio_edit;
static bool device_menu;
static bool wifi_monitor_ready;
static bool ble_monitor_ready;
static bool cc_peak_hold = true;
static bool nr_peak_hold = true;
static bool audio_peak_hold = false;
static unsigned wifi_item;
static unsigned cc_item;
static unsigned nr_item;
static unsigned audio_item;
static uint16_t cc_notification_counts[ CC1101_FP_TYPE_COUNT ];
static uint32_t nr_notification_counts[ NRF24_FP_LOGITECH_LIKE + 1U ];

/** @brief Probe optional thermal hardware and report availability changes. */
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

/** @brief Return whether a top-level application can currently be selected. */
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

/** @brief Start or retry the Wi-Fi and BLE collectors independently. */
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

/** @brief Queue one configured global detection notification. */
static void notify_detection( rfscout_notification_type_t a_type )
{
    const char character = device_config_notification_character( a_type );
    if( !alert_request_character( character ) )
    {
        ESP_LOGW( TAG,
                  "notification dropped type=%s morse=%c",
                  alert_notification_name( a_type ),
                  character );
    }
}

/** @brief Map one CC1101 classifier category to its global notification category. */
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

/** @brief Map one nRF24 classifier category to its global notification category. */
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

/** @brief Close WiFi/BT settings and resume the live summary. */
static void close_wifi_menu( void )
{
    wifi_menu = false;
    wifi_edit = false;
}

/** @brief Handle WiFi/BT action-menu input. */
static void wifi_input( bool a_pressed, int a_delta )
{
    if( !wifi_menu )
    {
        if( a_pressed )
        {
            wifi_menu = true;
            wifi_edit = false;
            wifi_item = WIFI_MENU_BACK;
            wifi_settings_view_render( wifi_item, false );
        }
        return;
    }

    if( a_pressed )
    {
        if( WIFI_MENU_BACK == wifi_item )
        {
            close_wifi_menu();
            return;
        }
        if( WIFI_MENU_RESET == wifi_item )
        {
            observation_store_reset();
            surveillance_history_reset();
            close_wifi_menu();
            return;
        }
    }

    if( 0 == a_delta )
    {
        return;
    }

    int item = (int)wifi_item + ( ( a_delta > 0 ) ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)WIFI_MENU_ITEM_COUNT - 1;
    }
    if( item >= (int)WIFI_MENU_ITEM_COUNT )
    {
        item = 0;
    }
    wifi_item = (unsigned)item;
    wifi_settings_view_render( wifi_item, false );
}

/** @brief Render the current CC1101 settings state. */
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
                                cc_peak_hold );
}

/** @brief Generate notifications for newly counted CC1101 detections. */
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

/** @brief Render the current CC1101 spectrum and detection rows. */
static void render_cc_spectrum( void )
{
    uint8_t current[ CC1101_SPECTRUM_BINS ];
    uint8_t peaks[ CC1101_SPECTRUM_BINS ];
    cc1101_fingerprint_t devices[ CC1101_FINGERPRINT_MAX ];

    const size_t count = cc1101_activity_levels( current, peaks, CC1101_SPECTRUM_BINS );
    if( !cc_peak_hold )
    {
        memset( peaks, 0, sizeof( peaks ) );
    }

    const size_t device_count = cc1101_activity_fingerprints( devices, CC1101_FINGERPRINT_MAX );
    notify_cc_detections( devices, device_count );
    const cc1101_scan_config_t *config = cc1101_config();

    radio_app_view_cc1101_spectrum( current,
                                    peaks,
                                    count,
                                    config->low_khz,
                                    config->high_khz,
                                    cc1101_is_detected(),
                                    devices,
                                    device_count );
}

/** @brief Feed the latest completed CC1101 sweep into activity classification. */
static void observe_cc_spectrum( void )
{
    int8_t rssi[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( rssi, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *config = cc1101_config();

    cc1101_activity_observe_sweep( rssi, count, config->low_khz, config->high_khz );
}

/** @brief Render the current nRF24 settings state. */
static void render_nr_menu( void )
{
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_menu( nr_item,
                               nr_edit,
                               config->low_channel,
                               config->high_channel,
                               config->dwell_us,
                               nr_peak_hold );
}

/** @brief Generate notifications for newly counted nRF24 detections. */
static void notify_nr_detections( void )
{
    static const nrf24_fingerprint_type_t types[] = { NRF24_FP_ESB,
                                                      NRF24_FP_INPUT,
                                                      NRF24_FP_SENSOR,
                                                      NRF24_FP_WIFI_LIKE,
                                                      NRF24_FP_BLE_LIKE,
                                                      NRF24_FP_LOGITECH_LIKE };

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

/** @brief Render the current nRF24 activity and detection rows. */
static void render_nr_spectrum( void )
{
    uint8_t fast[ NRF24_CHANNEL_COUNT ];
    uint8_t slow[ NRF24_CHANNEL_COUNT ];
    nrf24_fingerprint_t devices[ NRF24_FINGERPRINT_MAX ];

    const size_t count = nrf24_activity_histogram( fast, slow, NRF24_CHANNEL_COUNT );
    if( !nr_peak_hold )
    {
        memset( slow, 0, sizeof( slow ) );
    }

    const size_t device_count = nrf24_activity_fingerprints( devices, NRF24_FINGERPRINT_MAX );
    notify_nr_detections();
    const nrf24_scan_config_t *config = nrf24_config();

    radio_app_view_nrf24_spectrum( fast,
                                   slow,
                                   count,
                                   config->low_channel,
                                   config->high_channel,
                                   nrf24_is_detected(),
                                   devices,
                                   device_count );
}

/** @brief Render the most recent Audio spectrum frame. */
static void render_audio( void )
{
    static uint8_t current[ AUDIO_SPECTRUM_BINS ];
    static uint8_t peaks[ AUDIO_SPECTRUM_BINS ];

    (void)audio_spectrum_take_frame( current, peaks, AUDIO_SPECTRUM_BINS );
    if( !audio_peak_hold )
    {
        memset( peaks, 0, sizeof( peaks ) );
    }
    audio_view_render( current, peaks );
}

/** @brief Stop resources and clear runtime state owned by an application. */
static void leave_app( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        (void)ble_monitor_stop();
        (void)radio_monitor_stop();
        wifi_monitor_ready = false;
        ble_monitor_ready = false;
        wifi_item = WIFI_MENU_BACK;
    }
    else if( RFSCOUT_APP_CC1101 == a_app )
    {
        cc1101_stop();
        cc1101_activity_reset();
        cc1101_activity_reset_counts();
        memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
        cc_item = CC1101_MENU_BACK;
    }
    else if( RFSCOUT_APP_NRF24 == a_app )
    {
        nrf24_stop();
        nrf24_activity_reset();
        memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
        nr_item = NRF24_MENU_BACK;
    }
    else if( RFSCOUT_APP_AUDIO == a_app )
    {
        audio_spectrum_stop();
        audio_item = AUDIO_MENU_BACK;
    }

    wifi_menu = false;
    wifi_edit = false;
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
    audio_menu = false;
    audio_edit = false;
    device_menu = false;
}

/** @brief Start resources and render the initial state for an application. */
static void enter_app( rfscout_app_id_t a_app )
{
    last_app_refresh_us = 0;

    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            wifi_item = WIFI_MENU_BACK;
            wifi_menu = false;
            wifi_edit = false;
            wifi_monitor_ready = false;
            ble_monitor_ready = false;
            last_radio_retry_us = esp_timer_get_time();
            start_wifi_bt_collectors();
            break;

        case RFSCOUT_APP_CC1101:
            cc_item = CC1101_MENU_BACK;
            cc_menu = false;
            cc_edit = false;
            cc_limit = false;
            memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
            cc1101_activity_reset();
            if( ESP_OK != cc1101_start() )
            {
                ESP_LOGW( TAG, "CC1101 unavailable" );
            }
            render_cc_spectrum();
            break;

        case RFSCOUT_APP_NRF24:
            nr_item = NRF24_MENU_BACK;
            nr_menu = false;
            nr_edit = false;
            memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
            nrf24_activity_reset();
            if( ESP_OK != nrf24_start() )
            {
                ESP_LOGW( TAG, "nRF24 unavailable" );
            }
            render_nr_spectrum();
            break;

        case RFSCOUT_APP_AUDIO:
            audio_item = AUDIO_MENU_BACK;
            audio_menu = false;
            audio_edit = false;
            if( ESP_OK != audio_spectrum_start() )
            {
                ESP_LOGW( TAG, "Audio microphone unavailable" );
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

        default:
            break;
    }

    ESP_LOGI( TAG, "switched app=%d", (int)a_app );
}

/** @brief Find the next available application in the requested direction. */
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

/** @brief Switch applications for one nonzero encoder delta. */
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

    leave_app( active_app );
    active_app = candidate;
    enter_app( active_app );
    return true;
}

/** @brief Return true when a step adjustment is blocked only by the step table endpoint. */
static bool cc_step_at_endpoint( uint32_t a_step, int a_delta )
{
    return ( ( a_delta > 0 ) && ( a_step >= CC1101_MAX_STEP_KHZ ) ) ||
           ( ( a_delta < 0 ) && ( a_step <= CC1101_MIN_STEP_KHZ ) );
}

/** @brief Close the CC1101 menu and return to the live spectrum. */
static void close_cc_menu( void )
{
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    render_cc_spectrum();
}

/** @brief Handle CC1101 center-button and encoder input while its app is active. */
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
        }
        return;
    }

    if( a_pressed )
    {
        if( CC1101_MENU_BACK == cc_item )
        {
            close_cc_menu();
            return;
        }
        if( CC1101_MENU_RESET_COUNTS == cc_item )
        {
            cc1101_activity_reset_counts();
            memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
            close_cc_menu();
            return;
        }

        cc_edit = !cc_edit;
        cc_limit = false;
        render_cc_menu();
        return;
    }

    if( 0 == a_delta )
    {
        return;
    }

    if( !cc_edit )
    {
        int item = (int)cc_item + ( ( a_delta > 0 ) ? 1 : -1 );
        if( item < 0 )
        {
            item = (int)CC1101_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)CC1101_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        cc_item = (unsigned)item;
        cc_limit = false;
    }
    else if( CC1101_MENU_PEAK_HOLD == cc_item )
    {
        cc_peak_hold = !cc_peak_hold;
    }
    else
    {
        const cc1101_scan_config_t *config = cc1101_config();
        bool changed = true;
        bool endpoint = false;
        const int direction = ( a_delta > 0 ) ? 1 : -1;

        if( CC1101_MENU_PROFILE == cc_item )
        {
            int profile = (int)config->profile + direction;
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
            endpoint = cc_step_at_endpoint( config->step_khz, a_delta );
            changed = cc1101_adjust_step( direction );
        }

        if( changed && ( cc_item >= CC1101_MENU_LOW ) && ( cc_item <= CC1101_MENU_STEP ) )
        {
            cc1101_activity_reset();
            memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
        }
        cc_limit = !changed && !endpoint;
    }

    render_cc_menu();
}

/** @brief Close the nRF24 menu and return to the live analyzer. */
static void close_nr_menu( void )
{
    nr_menu = false;
    nr_edit = false;
    render_nr_spectrum();
}

/** @brief Handle nRF24 center-button and encoder input while its app is active. */
static void nr_input( bool a_pressed, int a_delta )
{
    if( !nr_menu )
    {
        if( a_pressed )
        {
            nr_menu = true;
            nr_edit = false;
            nr_item = NRF24_MENU_BACK;
            render_nr_menu();
        }
        return;
    }

    if( a_pressed )
    {
        if( NRF24_MENU_BACK == nr_item )
        {
            close_nr_menu();
            return;
        }
        if( NRF24_MENU_RESET_COUNTS == nr_item )
        {
            nrf24_activity_reset_counts();
            memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );
            close_nr_menu();
            return;
        }

        nr_edit = !nr_edit;
        render_nr_menu();
        return;
    }

    if( 0 == a_delta )
    {
        return;
    }

    if( !nr_edit )
    {
        int item = (int)nr_item + ( ( a_delta > 0 ) ? 1 : -1 );
        if( item < 0 )
        {
            item = (int)NRF24_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)NRF24_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        nr_item = (unsigned)item;
    }
    else if( NRF24_MENU_PEAK_HOLD == nr_item )
    {
        nr_peak_hold = !nr_peak_hold;
    }
    else
    {
        const int direction = ( a_delta > 0 ) ? 1 : -1;
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
    }

    render_nr_menu();
}

/** @brief Close the Audio menu and return to the live spectrum. */
static void close_audio_menu( void )
{
    audio_menu = false;
    audio_edit = false;
    render_audio();
}

/** @brief Render the current Audio settings state. */
static void show_audio_menu( void )
{
    audio_view_menu( audio_item, audio_edit, audio_peak_hold );
}

/** @brief Handle Audio center-button and encoder input while its app is active. */
static void audio_input( bool a_pressed, int a_delta )
{
    if( !audio_menu )
    {
        if( a_pressed )
        {
            audio_menu = true;
            audio_edit = false;
            audio_item = AUDIO_MENU_BACK;
            show_audio_menu();
        }
        return;
    }

    if( a_pressed )
    {
        if( AUDIO_MENU_BACK == audio_item )
        {
            close_audio_menu();
            return;
        }

        const bool leaving_volume_edit = audio_edit && ( AUDIO_MENU_VOLUME == audio_item );
        audio_edit = !audio_edit;
        if( leaving_volume_edit )
        {
            const esp_err_t result = device_config_save( false, 0, NULL );
            if( ESP_OK != result )
            {
                ESP_LOGW( TAG, "could not save Recorder volume: %s", esp_err_to_name( result ) );
            }
        }
        show_audio_menu();
        return;
    }

    if( 0 == a_delta )
    {
        return;
    }

    if( !audio_edit )
    {
        int item = (int)audio_item + ( ( a_delta > 0 ) ? 1 : -1 );
        if( item < 0 )
        {
            item = (int)AUDIO_MENU_ITEM_COUNT - 1;
        }
        if( item >= (int)AUDIO_MENU_ITEM_COUNT )
        {
            item = 0;
        }
        audio_item = (unsigned)item;
    }
    else if( AUDIO_MENU_FILE == audio_item )
    {
        int channel = (int)audio_spectrum_channel() + ( ( a_delta > 0 ) ? 1 : -1 );
        if( channel < 0 )
        {
            channel = AUDIO_CHANNEL_COUNT - 1;
        }
        if( channel >= AUDIO_CHANNEL_COUNT )
        {
            channel = 0;
        }
        audio_spectrum_set_channel( (audio_channel_t)channel );
    }
    else if( AUDIO_MENU_VOLUME == audio_item )
    {
        const device_config_t *config = device_config_get();
        int volume = ( NULL != config ) ? (int)config->recorder_volume_percent : 50;
        volume += ( a_delta > 0 ) ? 5 : -5;
        if( volume < 0 )
        {
            volume = 0;
        }
        else if( volume > 100 )
        {
            volume = 100;
        }
        (void)device_config_set_recorder_volume( (uint8_t)volume );
    }

    show_audio_menu();
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_WIFI_BT;
    if( !device_config_app_enabled( active_app ) )
    {
        active_app = RFSCOUT_APP_DEVICE;
    }
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();
    last_radio_retry_us = 0;
    wifi_menu = false;
    wifi_edit = false;
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
    audio_menu = false;
    audio_edit = false;
    device_menu = false;
    wifi_item = WIFI_MENU_BACK;
    cc_item = CC1101_MENU_BACK;
    nr_item = NRF24_MENU_BACK;
    audio_item = AUDIO_MENU_BACK;
    wifi_monitor_ready = true;
    ble_monitor_ready = true;
    cc_peak_hold = true;
    nr_peak_hold = true;
    audio_peak_hold = false;
    memset( cc_notification_counts, 0, sizeof( cc_notification_counts ) );
    memset( nr_notification_counts, 0, sizeof( nr_notification_counts ) );

    const esp_err_t result = ui_init();
    if( ESP_OK != result )
    {
        return result;
    }

    (void)mlx90640_init();
    if( RFSCOUT_APP_DEVICE == active_app )
    {
        device_view_landing();
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

    const bool pressed = platform_encoder_button_pressed();
    const int delta = platform_encoder_delta();

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
            return;
        }
        (void)switch_app_for_delta( delta, now );
        return;
    }

    if( RFSCOUT_APP_CC1101 == active_app )
    {
        if( cc_menu )
        {
            cc_input( pressed, delta );
            return;
        }
        if( pressed )
        {
            cc_input( true, 0 );
            return;
        }
        if( switch_app_for_delta( delta, now ) )
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
            nr_input( pressed, delta );
            return;
        }
        if( pressed )
        {
            nr_input( true, 0 );
            return;
        }
        if( switch_app_for_delta( delta, now ) )
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
            audio_input( pressed, delta );
            return;
        }
        if( pressed )
        {
            audio_input( true, 0 );
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
            wifi_input( pressed, delta );
        }
        else if( pressed )
        {
            wifi_input( true, 0 );
        }
        else if( switch_app_for_delta( delta, now ) )
        {
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
