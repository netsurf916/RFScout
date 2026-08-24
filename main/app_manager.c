/**
    app_manager.c : RFScout application view manager implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "app_manager.h"
#include "ble_monitor.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "platform.h"
#include "radio_app_view.h"
#include "radio_monitor.h"
#include "thermal_view.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"

#define APP_REFRESH_INTERVAL_US 125000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_RADIO_RETRY_INTERVAL_US 1000000LL
#define CC1101_MIN_STEP_KHZ 10U
#define CC1101_MAX_STEP_KHZ 1000U

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static int64_t last_radio_retry_us;
static bool cc_menu;
static bool cc_edit;
static bool cc_limit;
static bool nr_menu;
static bool nr_edit;
static bool wifi_monitor_ready;
static bool ble_monitor_ready;
static unsigned cc_item;
static unsigned nr_item;

static bool probe_thermal( void )
{
    const bool was = mlx90640_is_detected();
    const bool now = ( ESP_OK == mlx90640_probe() );
    if( now && !was )
    {
        ESP_LOGI( TAG, "Thermal app available" );
    }
    else if( !now && was )
    {
        ESP_LOGW( TAG, "Thermal app unavailable" );
    }
    return now;
}

static bool app_available( rfscout_app_id_t a_app )
{
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
            ESP_LOGW( TAG,
                      "Wi-Fi monitor start failed: %s; retrying",
                      esp_err_to_name( result ) );
        }
    }
    if( !ble_monitor_ready )
    {
        result = ble_monitor_start();
        ble_monitor_ready = ( ESP_OK == result );
        if( !ble_monitor_ready )
        {
            ESP_LOGW( TAG,
                      "BLE monitor start failed: %s; retrying",
                      esp_err_to_name( result ) );
        }
    }
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
                                cc_limit );
}

static void render_cc_spectrum( void )
{
    uint8_t current[ CC1101_SPECTRUM_BINS ];
    uint8_t peaks[ CC1101_SPECTRUM_BINS ];
    cc1101_fingerprint_t devices[ CC1101_FINGERPRINT_MAX ];
    const size_t count = cc1101_activity_levels( current,
                                                 peaks,
                                                 CC1101_SPECTRUM_BINS );
    const size_t device_count =
        cc1101_activity_fingerprints( devices, CC1101_FINGERPRINT_MAX );
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

static void observe_cc_spectrum( void )
{
    int8_t rssi[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( rssi, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *config = cc1101_config();
    cc1101_activity_observe_sweep( rssi,
                                   count,
                                   config->low_khz,
                                   config->high_khz );
}

static void render_nr_menu( void )
{
    const nrf24_scan_config_t *config = nrf24_config();
    radio_app_view_nrf24_menu( nr_item,
                               nr_edit,
                               config->low_channel,
                               config->high_channel,
                               config->dwell_us );
}

static void render_nr_spectrum( void )
{
    uint8_t fast[ NRF24_CHANNEL_COUNT ];
    uint8_t slow[ NRF24_CHANNEL_COUNT ];
    nrf24_fingerprint_t devices[ NRF24_FINGERPRINT_MAX ];
    const size_t count =
        nrf24_activity_histogram( fast, slow, NRF24_CHANNEL_COUNT );
    const size_t device_count =
        nrf24_activity_fingerprints( devices, NRF24_FINGERPRINT_MAX );
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

static void leave_app( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        (void)ble_monitor_stop();
        (void)radio_monitor_stop();
        wifi_monitor_ready = false;
        ble_monitor_ready = false;
    }
    else if( RFSCOUT_APP_CC1101 == a_app )
    {
        cc1101_stop();
    }
    else if( RFSCOUT_APP_NRF24 == a_app )
    {
        nrf24_stop();
    }
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
}

static void enter_app( rfscout_app_id_t a_app )
{
    last_app_refresh_us = 0;
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            wifi_monitor_ready = false;
            ble_monitor_ready = false;
            last_radio_retry_us = esp_timer_get_time();
            start_wifi_bt_collectors();
            break;
        case RFSCOUT_APP_CC1101:
            cc_item = 0U;
            cc_menu = false;
            cc_edit = false;
            cc_limit = false;
            cc1101_activity_reset();
            if( ESP_OK != cc1101_start() )
            {
                ESP_LOGW( TAG, "CC1101 unavailable" );
            }
            render_cc_spectrum();
            break;
        case RFSCOUT_APP_NRF24:
            nr_item = 0U;
            nr_menu = false;
            nr_edit = false;
            nrf24_activity_reset();
            if( ESP_OK != nrf24_start() )
            {
                ESP_LOGW( TAG, "nRF24 unavailable" );
            }
            render_nr_spectrum();
            break;
        case RFSCOUT_APP_THERMAL:
            thermal_view_enter();
            break;
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
        if( ( RFSCOUT_APP_THERMAL == candidate ) &&
            !app_available( candidate ) )
        {
            last_thermal_probe_us = a_now;
            if( !probe_thermal() )
            {
                continue;
            }
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
    const rfscout_app_id_t candidate =
        next_available_app( a_delta > 0 ? 1 : -1, a_now );
    if( candidate == active_app )
    {
        return false;
    }
    leave_app( active_app );
    active_app = candidate;
    enter_app( active_app );
    return true;
}

static bool cc_step_at_endpoint( uint32_t a_step_khz, int a_delta )
{
    return ( ( a_delta > 0 ) && ( a_step_khz >= CC1101_MAX_STEP_KHZ ) ) ||
           ( ( a_delta < 0 ) && ( a_step_khz <= CC1101_MIN_STEP_KHZ ) );
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
            render_cc_menu();
        }
        return;
    }

    if( a_pressed )
    {
        if( 4U == cc_item )
        {
            cc_menu = false;
            cc_edit = false;
            cc_limit = false;
            render_cc_spectrum();
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
        int item = (int)cc_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 ) item = 4;
        if( item > 4 ) item = 0;
        cc_item = (unsigned)item;
        cc_limit = false;
    }
    else
    {
        const cc1101_scan_config_t *config = cc1101_config();
        bool changed = true;
        bool endpoint = false;

        if( 0U == cc_item )
        {
            int profile = (int)config->profile + ( a_delta > 0 ? 1 : -1 );
            if( profile < 0 ) profile = CC1101_PROFILE_COUNT - 1;
            if( profile >= CC1101_PROFILE_COUNT ) profile = 0;
            cc1101_set_profile( (cc1101_profile_t)profile );
            cc1101_activity_reset();
        }
        else if( 1U == cc_item )
        {
            changed = cc1101_adjust_low( a_delta > 0 ? 1 : -1 );
        }
        else if( 2U == cc_item )
        {
            changed = cc1101_adjust_high( a_delta > 0 ? 1 : -1 );
        }
        else if( 3U == cc_item )
        {
            endpoint = cc_step_at_endpoint( config->step_khz, a_delta );
            changed = cc1101_adjust_step( a_delta > 0 ? 1 : -1 );
        }

        if( changed && ( cc_item > 0U ) && ( cc_item < 4U ) )
        {
            cc1101_activity_reset();
        }

        /* A failed range/step change means the 128-sample budget was hit,
           except when STEP simply reached the end of its supported values. */
        cc_limit = !changed && !endpoint;
    }

    render_cc_menu();
}

static void nr_input( bool a_pressed, int a_delta )
{
    if( !nr_menu )
    {
        if( a_pressed )
        {
            nr_menu = true;
            nr_edit = false;
            render_nr_menu();
        }
        return;
    }
    if( a_pressed )
    {
        if( 3U == nr_item )
        {
            nr_menu = false;
            nr_edit = false;
            render_nr_spectrum();
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
        int item = (int)nr_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 ) item = 3;
        if( item > 3 ) item = 0;
        nr_item = (unsigned)item;
    }
    else
    {
        if( 0U == nr_item ) nrf24_adjust_low( a_delta > 0 ? 1 : -1 );
        else if( 1U == nr_item ) nrf24_adjust_high( a_delta > 0 ? 1 : -1 );
        else if( 2U == nr_item ) nrf24_adjust_dwell( a_delta > 0 ? 1 : -1 );
    }
    render_nr_menu();
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_WIFI_BT;
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();
    last_radio_retry_us = 0;
    cc_menu = false;
    cc_edit = false;
    cc_limit = false;
    nr_menu = false;
    nr_edit = false;
    cc_item = 0U;
    nr_item = 0U;
    wifi_monitor_ready = true;
    ble_monitor_ready = true;

    const esp_err_t result = ui_init();
    if( ESP_OK != result )
    {
        return result;
    }
    (void)mlx90640_init();
    return ESP_OK;
}

rfscout_app_id_t app_manager_current( void )
{
    return active_app;
}

void app_manager_poll( void )
{
    const int64_t now = esp_timer_get_time();
    if( ( RFSCOUT_APP_THERMAL != active_app ) &&
        ( now - last_thermal_probe_us >= APP_FEATURE_PROBE_INTERVAL_US ) )
    {
        (void)probe_thermal();
        last_thermal_probe_us = now;
    }

    const bool pressed = platform_encoder_button_pressed();
    const int delta = platform_encoder_delta();

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
        if( switch_app_for_delta( delta, now ) ) return;
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
        if( switch_app_for_delta( delta, now ) ) return;
        nrf24_poll();
        if( nrf24_take_frame_ready() ) render_nr_spectrum();
        return;
    }

    if( switch_app_for_delta( delta, now ) ) return;

    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        if( ( !wifi_monitor_ready || !ble_monitor_ready ) &&
            ( now - last_radio_retry_us >= APP_RADIO_RETRY_INTERVAL_US ) )
        {
            last_radio_retry_us = now;
            start_wifi_bt_collectors();
        }
        if( now - last_app_refresh_us >= APP_REFRESH_INTERVAL_US )
        {
            radio_app_view_render();
            last_app_refresh_us = now;
        }
    }
    else if( RFSCOUT_APP_THERMAL == active_app )
    {
        thermal_view_poll();
    }
}
