/**
    app_manager.c : RFScout application view manager implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "app_manager.h"
#include "audio_spectrum.h"
#include "audio_view.h"
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
#define AUDIO_REFRESH_INTERVAL_US 40000LL
#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_RADIO_RETRY_INTERVAL_US 1000000LL
#define CC1101_MIN_STEP_KHZ 1U
#define CC1101_MAX_STEP_KHZ 1000U
#define CC1101_MENU_RESET_COUNTS 4U
#define CC1101_MENU_BACK 5U
#define CC1101_MENU_ITEM_COUNT 6U
#define NRF24_MENU_RESET_COUNTS 3U
#define NRF24_MENU_BACK 4U
#define NRF24_MENU_ITEM_COUNT 5U
#define AUDIO_MENU_BACK 2U
#define AUDIO_MENU_ITEM_COUNT 3U

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_app_refresh_us;
static int64_t last_thermal_probe_us;
static int64_t last_radio_retry_us;
static bool cc_menu, cc_edit, cc_limit;
static bool nr_menu, nr_edit;
static bool audio_menu, audio_edit;
static bool wifi_monitor_ready, ble_monitor_ready;
static unsigned cc_item, nr_item, audio_item;

static bool probe_thermal( void )
{
    const bool was = mlx90640_is_detected();
    const bool now = ( ESP_OK == mlx90640_probe() );
    if( now && !was ) ESP_LOGI( TAG, "Thermal app available" );
    else if( !now && was ) ESP_LOGW( TAG, "Thermal app unavailable" );
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
        if( !wifi_monitor_ready ) ESP_LOGW( TAG, "Wi-Fi monitor start failed: %s; retrying", esp_err_to_name( result ) );
    }
    if( !ble_monitor_ready )
    {
        result = ble_monitor_start();
        ble_monitor_ready = ( ESP_OK == result );
        if( !ble_monitor_ready ) ESP_LOGW( TAG, "BLE monitor start failed: %s; retrying", esp_err_to_name( result ) );
    }
}

static void render_cc_menu( void )
{
    const cc1101_scan_config_t *c = cc1101_config();
    radio_app_view_cc1101_menu( cc_item, cc_edit, cc1101_profile_name( c->profile ), c->low_khz, c->high_khz, c->step_khz, cc1101_sweep_count(), cc_limit );
}

static void render_cc_spectrum( void )
{
    uint8_t current[ CC1101_SPECTRUM_BINS ], peaks[ CC1101_SPECTRUM_BINS ];
    cc1101_fingerprint_t devices[ CC1101_FINGERPRINT_MAX ];
    const size_t count = cc1101_activity_levels( current, peaks, CC1101_SPECTRUM_BINS );
    const size_t device_count = cc1101_activity_fingerprints( devices, CC1101_FINGERPRINT_MAX );
    const cc1101_scan_config_t *c = cc1101_config();
    radio_app_view_cc1101_spectrum( current, peaks, count, c->low_khz, c->high_khz, cc1101_is_detected(), devices, device_count );
}

static void observe_cc_spectrum( void )
{
    int8_t rssi[ CC1101_SPECTRUM_BINS ];
    const size_t count = cc1101_spectrum( rssi, CC1101_SPECTRUM_BINS );
    const cc1101_scan_config_t *c = cc1101_config();
    cc1101_activity_observe_sweep( rssi, count, c->low_khz, c->high_khz );
}

static void render_nr_menu( void )
{
    const nrf24_scan_config_t *c = nrf24_config();
    radio_app_view_nrf24_menu( nr_item, nr_edit, c->low_channel, c->high_channel, c->dwell_us );
}

static void render_nr_spectrum( void )
{
    uint8_t fast[ NRF24_CHANNEL_COUNT ], slow[ NRF24_CHANNEL_COUNT ];
    nrf24_fingerprint_t devices[ NRF24_FINGERPRINT_MAX ];
    const size_t count = nrf24_activity_histogram( fast, slow, NRF24_CHANNEL_COUNT );
    const size_t device_count = nrf24_activity_fingerprints( devices, NRF24_FINGERPRINT_MAX );
    const nrf24_scan_config_t *c = nrf24_config();
    radio_app_view_nrf24_spectrum( fast, slow, count, c->low_channel, c->high_channel, nrf24_is_detected(), devices, device_count );
}

static void render_audio( void )
{
    static uint8_t current[ AUDIO_SPECTRUM_BINS ];
    static uint8_t peaks[ AUDIO_SPECTRUM_BINS ];
    (void)audio_spectrum_take_frame( current, peaks, AUDIO_SPECTRUM_BINS );
    audio_view_render( current, peaks );
}

static void leave_app( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_WIFI_BT == a_app )
    {
        (void)ble_monitor_stop(); (void)radio_monitor_stop();
        wifi_monitor_ready = false; ble_monitor_ready = false;
    }
    else if( RFSCOUT_APP_CC1101 == a_app )
    {
        cc1101_stop(); cc1101_activity_reset(); cc1101_activity_reset_counts(); cc_item = 0U;
    }
    else if( RFSCOUT_APP_NRF24 == a_app )
    {
        nrf24_stop(); nrf24_activity_reset(); nr_item = 0U;
    }
    else if( RFSCOUT_APP_AUDIO == a_app )
    {
        audio_spectrum_stop(); audio_item = 0U;
    }
    cc_menu = cc_edit = cc_limit = false;
    nr_menu = nr_edit = false;
    audio_menu = audio_edit = false;
}

static void enter_app( rfscout_app_id_t a_app )
{
    last_app_refresh_us = 0;
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            wifi_monitor_ready = false; ble_monitor_ready = false;
            last_radio_retry_us = esp_timer_get_time(); start_wifi_bt_collectors(); break;
        case RFSCOUT_APP_CC1101:
            cc_item = 0U; cc_menu = cc_edit = cc_limit = false; cc1101_activity_reset();
            if( ESP_OK != cc1101_start() ) ESP_LOGW( TAG, "CC1101 unavailable" );
            render_cc_spectrum(); break;
        case RFSCOUT_APP_NRF24:
            nr_item = 0U; nr_menu = nr_edit = false; nrf24_activity_reset();
            if( ESP_OK != nrf24_start() ) ESP_LOGW( TAG, "nRF24 unavailable" );
            render_nr_spectrum(); break;
        case RFSCOUT_APP_AUDIO:
            audio_item = 0U; audio_menu = audio_edit = false;
            if( ESP_OK != audio_spectrum_start() ) ESP_LOGW( TAG, "Audio microphone unavailable" );
            render_audio(); break;
        case RFSCOUT_APP_THERMAL: thermal_view_enter(); break;
        default: break;
    }
    ESP_LOGI( TAG, "switched app=%d", (int)a_app );
}

static rfscout_app_id_t next_available_app( int a_direction, int64_t a_now )
{
    int next = (int)active_app;
    for( int attempts = 0; attempts < RFSCOUT_APP_COUNT; ++attempts )
    {
        next += a_direction;
        if( next < 0 ) next = RFSCOUT_APP_COUNT - 1;
        else if( next >= RFSCOUT_APP_COUNT ) next = 0;
        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;
        if( ( RFSCOUT_APP_THERMAL == candidate ) && !app_available( candidate ) )
        {
            last_thermal_probe_us = a_now;
            if( !probe_thermal() ) continue;
        }
        return candidate;
    }
    return active_app;
}

static bool switch_app_for_delta( int a_delta, int64_t a_now )
{
    if( 0 == a_delta ) return false;
    const rfscout_app_id_t candidate = next_available_app( a_delta > 0 ? 1 : -1, a_now );
    if( candidate == active_app ) return false;
    leave_app( active_app ); active_app = candidate; enter_app( active_app ); return true;
}

static bool cc_step_at_endpoint( uint32_t a_step_khz, int a_delta )
{
    return ( ( a_delta > 0 ) && ( a_step_khz >= CC1101_MAX_STEP_KHZ ) ) || ( ( a_delta < 0 ) && ( a_step_khz <= CC1101_MIN_STEP_KHZ ) );
}

static void close_cc_menu( void ) { cc_menu = cc_edit = cc_limit = false; render_cc_spectrum(); }
static void cc_input( bool a_pressed, int a_delta )
{
    if( !cc_menu ) { if( a_pressed ) { cc_menu = true; cc_edit = cc_limit = false; render_cc_menu(); } return; }
    if( a_pressed )
    {
        if( CC1101_MENU_RESET_COUNTS == cc_item ) { cc1101_activity_reset_counts(); close_cc_menu(); return; }
        if( CC1101_MENU_BACK == cc_item ) { close_cc_menu(); return; }
        cc_edit = !cc_edit; cc_limit = false; render_cc_menu(); return;
    }
    if( 0 == a_delta ) return;
    if( !cc_edit )
    {
        int item = (int)cc_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 ) item = (int)CC1101_MENU_ITEM_COUNT - 1;
        if( item >= (int)CC1101_MENU_ITEM_COUNT ) item = 0;
        cc_item = (unsigned)item; cc_limit = false;
    }
    else
    {
        const cc1101_scan_config_t *c = cc1101_config(); bool changed = true, endpoint = false;
        if( 0U == cc_item ) { int p = (int)c->profile + ( a_delta > 0 ? 1 : -1 ); if( p < 0 ) p = CC1101_PROFILE_COUNT - 1; if( p >= CC1101_PROFILE_COUNT ) p = 0; cc1101_set_profile( (cc1101_profile_t)p ); cc1101_activity_reset(); }
        else if( 1U == cc_item ) changed = cc1101_adjust_low( a_delta > 0 ? 1 : -1 );
        else if( 2U == cc_item ) changed = cc1101_adjust_high( a_delta > 0 ? 1 : -1 );
        else if( 3U == cc_item ) { endpoint = cc_step_at_endpoint( c->step_khz, a_delta ); changed = cc1101_adjust_step( a_delta > 0 ? 1 : -1 ); }
        if( changed && ( cc_item > 0U ) && ( cc_item < 4U ) ) cc1101_activity_reset();
        cc_limit = !changed && !endpoint;
    }
    render_cc_menu();
}

static void close_nr_menu( void ) { nr_menu = nr_edit = false; render_nr_spectrum(); }
static void nr_input( bool a_pressed, int a_delta )
{
    if( !nr_menu ) { if( a_pressed ) { nr_menu = true; nr_edit = false; render_nr_menu(); } return; }
    if( a_pressed )
    {
        if( NRF24_MENU_RESET_COUNTS == nr_item ) { nrf24_activity_reset_counts(); close_nr_menu(); return; }
        if( NRF24_MENU_BACK == nr_item ) { close_nr_menu(); return; }
        nr_edit = !nr_edit; render_nr_menu(); return;
    }
    if( 0 == a_delta ) return;
    if( !nr_edit )
    {
        int item = (int)nr_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 ) item = (int)NRF24_MENU_ITEM_COUNT - 1;
        if( item >= (int)NRF24_MENU_ITEM_COUNT ) item = 0;
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

static void close_audio_menu( void ) { audio_menu = audio_edit = false; render_audio(); }
static void audio_input( bool a_pressed, int a_delta )
{
    if( !audio_menu ) { if( a_pressed ) { audio_menu = true; audio_edit = false; audio_view_menu( audio_item, audio_edit ); } return; }
    if( a_pressed )
    {
        if( AUDIO_MENU_BACK == audio_item ) { close_audio_menu(); return; }
        audio_edit = !audio_edit; audio_view_menu( audio_item, audio_edit ); return;
    }
    if( 0 == a_delta ) return;
    if( !audio_edit )
    {
        int item = (int)audio_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 ) item = (int)AUDIO_MENU_ITEM_COUNT - 1;
        if( item >= (int)AUDIO_MENU_ITEM_COUNT ) item = 0;
        audio_item = (unsigned)item;
    }
    else if( 0U == audio_item )
    {
        int range = (int)audio_spectrum_range() + ( a_delta > 0 ? 1 : -1 );
        if( range < 0 ) range = AUDIO_RANGE_COUNT - 1;
        if( range >= AUDIO_RANGE_COUNT ) range = 0;
        audio_spectrum_set_range( (audio_range_t)range );
    }
    else if( 1U == audio_item )
    {
        int channel = (int)audio_spectrum_channel() + ( a_delta > 0 ? 1 : -1 );
        if( channel < 0 ) channel = AUDIO_CHANNEL_COUNT - 1;
        if( channel >= AUDIO_CHANNEL_COUNT ) channel = 0;
        audio_spectrum_set_channel( (audio_channel_t)channel );
    }
    audio_view_menu( audio_item, audio_edit );
}

esp_err_t app_manager_init( void )
{
    active_app = RFSCOUT_APP_WIFI_BT; last_app_refresh_us = 0; last_thermal_probe_us = esp_timer_get_time(); last_radio_retry_us = 0;
    cc_menu = cc_edit = cc_limit = false; nr_menu = nr_edit = false; audio_menu = audio_edit = false;
    cc_item = nr_item = audio_item = 0U; wifi_monitor_ready = ble_monitor_ready = true;
    const esp_err_t result = ui_init(); if( ESP_OK != result ) return result;
    (void)mlx90640_init(); return ESP_OK;
}

rfscout_app_id_t app_manager_current( void ) { return active_app; }

void app_manager_poll( void )
{
    const int64_t now = esp_timer_get_time();
    if( ( RFSCOUT_APP_THERMAL != active_app ) && ( now - last_thermal_probe_us >= APP_FEATURE_PROBE_INTERVAL_US ) ) { (void)probe_thermal(); last_thermal_probe_us = now; }
    const bool pressed = platform_encoder_button_pressed(); const int delta = platform_encoder_delta();

    if( RFSCOUT_APP_CC1101 == active_app )
    {
        if( cc_menu ) { cc_input( pressed, delta ); return; }
        if( pressed ) { cc_input( true, 0 ); return; }
        if( switch_app_for_delta( delta, now ) ) return;
        cc1101_poll(); if( cc1101_take_frame_ready() ) { observe_cc_spectrum(); render_cc_spectrum(); } return;
    }
    if( RFSCOUT_APP_NRF24 == active_app )
    {
        if( nr_menu ) { nr_input( pressed, delta ); return; }
        if( pressed ) { nr_input( true, 0 ); return; }
        if( switch_app_for_delta( delta, now ) ) return;
        nrf24_poll(); if( nrf24_take_frame_ready() ) render_nr_spectrum(); return;
    }
    if( RFSCOUT_APP_AUDIO == active_app )
    {
        if( audio_menu ) { audio_input( pressed, delta ); return; }
        if( pressed ) { audio_input( true, 0 ); return; }
        if( switch_app_for_delta( delta, now ) ) return;
        if( now - last_app_refresh_us >= AUDIO_REFRESH_INTERVAL_US ) { last_app_refresh_us = now; render_audio(); }
        return;
    }
    if( switch_app_for_delta( delta, now ) ) return;
    if( RFSCOUT_APP_WIFI_BT == active_app )
    {
        if( ( !wifi_monitor_ready || !ble_monitor_ready ) && ( now - last_radio_retry_us >= APP_RADIO_RETRY_INTERVAL_US ) ) { last_radio_retry_us = now; start_wifi_bt_collectors(); }
        radio_monitor_step(); ui_poll(); return;
    }
    if( RFSCOUT_APP_THERMAL != active_app ) return;
    if( !mlx90640_is_detected() ) { leave_app( active_app ); active_app = RFSCOUT_APP_WIFI_BT; enter_app( active_app ); return; }
    if( now - last_app_refresh_us < APP_REFRESH_INTERVAL_US ) return;
    last_app_refresh_us = now;
    const esp_err_t result = thermal_view_render();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) && !mlx90640_is_detected() ) { leave_app( active_app ); active_app = RFSCOUT_APP_WIFI_BT; enter_app( active_app ); }
}