/**
    app_manager.c : RFScout application view manager implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "ble_monitor.h"
#include "cc1101.h"
#include "mlx90640.h"
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

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static int64_t last_radio_retry_us;
static bool cc_menu;
static bool cc_edit;
static bool wifi_monitor_ready;
static bool ble_monitor_ready;
static unsigned cc_item;

static bool probe_thermal( void )
{
    const bool was_available = mlx90640_is_detected();
    const bool available = ( ESP_OK == mlx90640_probe() );

    if( available && !was_available )
    {
        ESP_LOGI( TAG, "Thermal app available" );
    }
    else if( !available && was_available )
    {
        ESP_LOGW( TAG, "Thermal app unavailable" );
    }

    return available;
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
                                config->step_khz );
}

static void render_cc_spectrum( void )
{
    int8_t bins[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( bins, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *config = cc1101_config();

    radio_app_view_cc1101_spectrum( bins,
                                    count,
                                    config->low_khz,
                                    config->high_khz,
                                    cc1101_is_detected() );
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

    cc_menu = false;
    cc_edit = false;
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
            if( ESP_OK != cc1101_start() )
            {
                ESP_LOGW( TAG, "CC1101 unavailable" );
            }
            render_cc_spectrum();
            break;

        case RFSCOUT_APP_NRF24:
            radio_app_view_nrf24();
            break;

        case RFSCOUT_APP_THERMAL:
            thermal_view_enter();
            break;

        default:
            break;
    }

    ESP_LOGI( TAG, "switched app=%d", (int)a_app );
}

static rfscout_app_id_t next_available_app( int a_direction,
                                             int64_t a_now_us )
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
            last_thermal_probe_us = a_now_us;
            if( !probe_thermal() )
            {
                continue;
            }
        }

        return candidate;
    }

    return active_app;
}

static bool switch_app_for_delta( int a_delta, int64_t a_now_us )
{
    if( 0 == a_delta )
    {
        return false;
    }

    const rfscout_app_id_t candidate =
        next_available_app( ( a_delta > 0 ) ? 1 : -1, a_now_us );

    if( candidate == active_app )
    {
        return false;
    }

    leave_app( active_app );
    active_app = candidate;
    enter_app( active_app );
    return true;
}

static void cc_input( bool a_pressed, int a_delta )
{
    if( !cc_menu )
    {
        if( a_pressed )
        {
            cc_menu = true;
            cc_edit = false;
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
            render_cc_spectrum();
            return;
        }

        cc_edit = !cc_edit;
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
            item = 4;
        }
        if( item > 4 )
        {
            item = 0;
        }
        cc_item = (unsigned)item;
    }
    else
    {
        const cc1101_scan_config_t *config = cc1101_config();

        if( 0U == cc_item )
        {
            int profile = (int)config->profile + ( ( a_delta > 0 ) ? 1 : -1 );
            if( profile < 0 )
            {
                profile = CC1101_PROFILE_COUNT - 1;
            }
            if( profile >= CC1101_PROFILE_COUNT )
            {
                profile = 0;
            }
            cc1101_set_profile( (cc1101_profile_t)profile );
        }
        else if( 1U == cc_item )
        {
            cc1101_adjust_low( ( a_delta > 0 ) ? 1 : -1 );
        }
        else if( 2U == cc_item )
        {
            cc1101_adjust_high( ( a_delta > 0 ) ? 1 : -1 );
        }
        else if( 3U == cc_item )
        {
            cc1101_adjust_step( ( a_delta > 0 ) ? 1 : -1 );
        }
    }

    render_cc_menu();
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_WIFI_BT;
    last_app_refresh_us = 0;
    last_thermal_probe_us = esp_timer_get_time();
    last_radio_retry_us = 0;
    cc_menu = false;
    cc_edit = false;
    cc_item = 0U;
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
    const int64_t now_us = esp_timer_get_time();

    if( ( RFSCOUT_APP_THERMAL != active_app ) &&
        ( ( now_us - last_thermal_probe_us ) >= APP_FEATURE_PROBE_INTERVAL_US ) )
    {
        (void)probe_thermal();
        last_thermal_probe_us = now_us;
    }

    const bool pressed = platform_encoder_button_pressed();
    const int delta = platform_encoder_delta();

    if( RFSCOUT_APP_CC1101 == active_app )
    {
        /* The encoder belongs to global app navigation while the spectrum is
           visible. It belongs to the CC1101 menu only after center opens it. */
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

        if( switch_app_for_delta( delta, now_us ) )
        {
            return;
        }

        cc1101_poll();
        if( ( now_us - last_app_refresh_us ) >= APP_REFRESH_INTERVAL_US )
        {
            last_app_refresh_us = now_us;
            render_cc_spectrum();
        }
        return;
    }

    if( switch_app_for_delta( delta, now_us ) )
    {
        return;
    }

    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        if( ( !wifi_monitor_ready || !ble_monitor_ready ) &&
            ( ( now_us - last_radio_retry_us ) >= APP_RADIO_RETRY_INTERVAL_US ) )
        {
            last_radio_retry_us = now_us;
            start_wifi_bt_collectors();
        }

        radio_monitor_step();
        ui_poll();
        return;
    }

    if( RFSCOUT_APP_NRF24 == active_app )
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
        active_app = RFSCOUT_APP_WIFI_BT;
        enter_app( active_app );
        return;
    }

    if( ( now_us - last_app_refresh_us ) < APP_REFRESH_INTERVAL_US )
    {
        return;
    }

    last_app_refresh_us = now_us;
    const esp_err_t result = thermal_view_render();
    if( ( ESP_OK != result ) &&
        ( ESP_ERR_NOT_FINISHED != result ) &&
        !mlx90640_is_detected() )
    {
        leave_app( active_app );
        active_app = RFSCOUT_APP_WIFI_BT;
        enter_app( active_app );
    }
}
