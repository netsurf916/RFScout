/**
    preview.c : RFScout host framebuffer acceptance and preview fixtures
    Description: Uses production views and drawing code with deterministic service data.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "display.h"
#include "ui_game.h"
#include "ui_theme.h"
#include "ui_chrome.h"
#include "device_config.h"
#include "event.h"
#include "power.h"
#include "system_events.h"
#include "recorder.h"
#include "gps.h"
#include "gps_view.h"
#include "recorder_view.h"
#include "hotspots_view.h"
#include "ota_view.h"
#include "cc1101_view.h"
#include "radio_app_view.h"
#include "nrf24_activity.h"
#include "wifi_settings_view.h"
#include "battery.h"
#include "battery_indicator.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DISPLAY_PIXEL_COUNT ( 320 * 170 )
static uint16_t pixels[ DISPLAY_PIXEL_COUNT ];
static uint16_t presented[ DISPLAY_PIXEL_COUNT ];
static uint16_t *framebuffer = pixels;
static bool display_dirty;
static rfscout_theme_id_t preference = RFSCOUT_THEME_PIXEL_QUEST;
static power_status_t power_status;
static rfscout_system_event_notification_t notification;
static rfscout_recorder_state_t recording_state;
static battery_status_t battery_fixture = {
    .available = true, .percentage = 75U, .charge_state = BATTERY_CHARGE_DISCHARGING };
static int64_t now = 123456000;
static const rfscout_board_profile_t profile = { .display_width = 320, .display_height = 170 };

const rfscout_board_profile_t *board_profile( void )
{
    return &profile;
}
void display_invalidate( void )
{
    display_dirty = true;
}
bool display_is_dirty( void )
{
    return display_dirty;
}
void display_mark_clean( void )
{
    display_dirty = false;
}
bool display_output_enabled( void )
{
    return true;
}
esp_err_t display_flush( void )
{
    memcpy( presented, pixels, sizeof( pixels ) );
    display_dirty = false;
    return ESP_OK;
}
esp_err_t device_config_set_theme( rfscout_theme_id_t a_theme )
{
    preference = a_theme;
    return ESP_OK;
}
rfscout_theme_id_t device_config_theme( void )
{
    return preference;
}
esp_err_t device_config_save( void )
{
    return ESP_OK;
}
const char *esp_err_to_name( esp_err_t a_error )
{
    (void)a_error;
    return "mock";
}
void rfscout_event_emit( rfscout_event_type_t a_type, uint8_t a_source, int32_t a_value )
{
    (void)a_type;
    (void)a_source;
    (void)a_value;
}
int64_t esp_timer_get_time( void )
{
    return now;
}
void power_get_status( power_status_t *a_status )
{
    *a_status = power_status;
}
bool system_events_notification_get( rfscout_system_event_notification_t *a_out )
{
    *a_out = notification;
    return notification.active;
}
void status_bar_draw( void )
{
    display_draw_text( 188, 7, "17:46 SD", ui_theme_get()->info, 1U );
}
/** @brief Host replacement for ESP-IDF's bounded string copy. */
size_t strlcpy( char *a_destination, const char *a_source, size_t a_capacity )
{
    if( 0U != a_capacity )
    {
        snprintf( a_destination, a_capacity, "%s", a_source );
    }
    return strlen( a_source );
}
void battery_poll( void )
{
    /* Fixture data is already current; the host owns no gauge/charger hardware. */
}
bool battery_get_status( battery_status_t *a_status )
{
    *a_status = battery_fixture;
    return true;
}
void audio_spectrum_view_render( void );
size_t recorder_monitor_bins( uint8_t *a_bins, size_t a_capacity )
{
    const size_t count = a_capacity < 64U ? a_capacity : 64U;
    for( size_t i = 0; i < count; ++i )
        a_bins[ i ] = (uint8_t)( 15U + ( i * 17U % 45U ) );
    if( count > 36U )
    {
        a_bins[ 13 ] = 93U;
        a_bins[ 35 ] = 82U;
    }
    return count;
}
uint32_t recorder_monitor_bin_frequency_hz( size_t a_bin )
{
    return (uint32_t)a_bin * 125U;
}
float recorder_monitor_range_db( void )
{
    return 52U;
}
rfscout_recorder_state_t recorder_state( void )
{
    return recording_state;
}
const char *recorder_active_name( void )
{
    return "field-0000042.wav";
}
const char *recorder_input_slot_name( void )
{
    return "MIC";
}
uint32_t recorder_recording_elapsed_seconds( void )
{
    return 42U;
}
size_t recorder_file_count( void )
{
    return 3U;
}
const rfscout_recording_t *recorder_file( size_t a_index )
{
    (void)a_index;
    return NULL;
}
bool gps_available( void )
{
    return true;
}
double gps_trip_distance_m( void )
{
    return 4270.0;
}
bool gps_snapshot_get( rfscout_gps_snapshot_t *a_gps )
{
    *a_gps = ( rfscout_gps_snapshot_t ){ .available = true,
                                         .location_valid = true,
                                         .satellites_valid = true,
                                         .altitude_valid = true,
                                         .speed_valid = true,
                                         .latitude_deg = 39.099727,
                                         .longitude_deg = -94.578567,
                                         .satellites = 9,
                                         .altitude_m = 270.0f,
                                         .speed_mps = 2.1f,
                                         .location_age_ms = 180 };
    return true;
}
size_t nrf24_activity_recent_packets( nrf24_recent_packet_t *a_out, size_t a_capacity )
{
    (void)a_out;
    (void)a_capacity;
    return 0;
}
const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type )
{
    (void)a_type;
    return "UNKNOWN";
}

/* DRIVER_DRAWING */

static void save( const char *directory, const char *name )
{
    char path[ 1024 ];
    snprintf( path, sizeof( path ), "%s/%s.ppm", directory, name );
    FILE *file = fopen( path, "wb" );
    assert( NULL != file );
    fprintf( file, "P6\n320 170\n255\n" );
    for( size_t i = 0; i < DISPLAY_PIXEL_COUNT; ++i )
    {
        uint16_t c = presented[ i ];
        const unsigned char rgb[ 3 ] = { (unsigned char)( ( ( c >> 11 ) & 31 ) * 255 / 31 ),
                                         (unsigned char)( ( ( c >> 5 ) & 63 ) * 255 / 63 ),
                                         (unsigned char)( ( c & 31 ) * 255 / 31 ) };
        assert( 3U == fwrite( rgb, 1, 3, file ) );
    }
    fclose( file );
}

static void check_overlay_restoration( bool a_modal )
{
    for( size_t i = 0; i < DISPLAY_PIXEL_COUNT; ++i )
        pixels[ i ] = (uint16_t)i;
    if( a_modal )
        display_modal_show( "A VERY LONG TRANSACTION TITLE", "CANCEL WITH CENTER" );
    assert( ESP_OK == display_present() );
    for( int y = 0; y < 170; ++y )
        for( int x = 0; x < 320; ++x )
        {
            size_t i = (size_t)y * 320U + (unsigned)x;
            assert( pixels[ i ] == (uint16_t)i );
            if( x < 42 || x >= 278 || y < 50 || y >= 120 )
                assert( presented[ i ] == pixels[ i ] );
        }
    display_modal_clear();
}

/** @brief App-specific label colors must not change the shared blue navigation/edit cursor. */
static void check_menu_cursor( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t colors[] = { theme->info, theme->selected, theme->accent, theme->secondary };
    const char *markers[] = { ">", "*", ">*" };
    uint16_t reference[ 320 * 16 ];
    uint16_t candidate[ 320 * 16 ];
    for( unsigned marker = 0U; marker < 3U; ++marker )
    {
        display_clear( theme->background );
        ui_game_draw_menu_marker( 8, 30, markers[ marker ], theme->info );
        assert( ESP_OK == display_read_rect( 0, 26, 320, 16, reference, 320U * 16U ) );
        assert( pixels[ 27U * 320U + 4U ] == theme->info );
        assert( pixels[ 31U * 320U + 12U ] == theme->info );
        for( unsigned color = 0U; color < 4U; ++color )
        {
            display_clear( theme->background );
            ui_game_draw_menu_marker( 8, 30, markers[ marker ], colors[ color ] );
            assert( ESP_OK == display_read_rect( 0, 26, 320, 16, candidate, 320U * 16U ) );
            assert( 0 == memcmp( reference, candidate, sizeof( reference ) ) );
        }
    }
}

/** @brief Instrument frames, icons, and energy meters must not modify neighboring app pixels. */
static void check_hud_bounds( void )
{
    for( unsigned fixture = 0U; fixture < 3U; ++fixture )
    {
        for( size_t i = 0U; i < DISPLAY_PIXEL_COUNT; ++i )
        {
            pixels[ i ] = (uint16_t)i;
        }
        if( 0U == fixture )
        {
            ui_game_draw_panel( 17, 38, 29, 13, ui_theme_get()->accent );
        }
        else if( 1U == fixture )
        {
            ui_game_draw_icon( UI_GAME_ICON_SCOUT, 17, 38, 1U, ui_theme_get()->accent );
        }
        else
        {
            ui_game_draw_meter( 17, 38, 29, 13, 50U, 100U, ui_theme_get()->success );
        }
        for( int y = 0; y < 170; ++y )
        {
            for( int x = 0; x < 320; ++x )
            {
                if( x < 17 || x >= 46 || y < 38 || y >= 51 )
                {
                    const size_t pixel = (size_t)y * 320U + (unsigned)x;
                    assert( pixels[ pixel ] == (uint16_t)pixel );
                }
            }
        }
    }
    uint16_t full_meter[ 32 * 12 ];
    uint16_t clamped_meter[ 32 * 12 ];
    ui_game_draw_meter( 17, 38, 32, 12, 100U, 100U, ui_theme_get()->success );
    assert( ESP_OK == display_read_rect( 17, 38, 32, 12, full_meter, 32U * 12U ) );
    ui_game_draw_meter( 17, 38, 32, 12, 500U, 100U, ui_theme_get()->success );
    assert( ESP_OK == display_read_rect( 17, 38, 32, 12, clamped_meter, 32U * 12U ) );
    assert( 0 == memcmp( full_meter, clamped_meter, sizeof( full_meter ) ) );
}

/** @brief Unknown/zero/charging battery states leave capacity cells unlit and preserve the bolt. */
static void check_battery_states( void )
{
    for( unsigned state = 0U; state < 3U; ++state )
    {
        battery_fixture = ( battery_status_t ){
            .available = 0U != state,
            .percentage = 2U == state ? 75U : 0U,
            .charge_state = 2U == state ? BATTERY_CHARGE_CHARGING : BATTERY_CHARGE_UNKNOWN };
        display_clear( 0 );
        battery_indicator_draw();
        unsigned bolt_pixels = 0U;
        for( int y = 6; y < 16; ++y )
        {
            for( int x = 286; x < 309; ++x )
            {
                const uint16_t pixel = pixels[ y * 320 + x ];
                if( 2U == state && x >= 292 && x < 300 )
                {
                    bolt_pixels += pixel == ui_theme_get()->warning ? 1U : 0U;
                }
                else
                {
                    assert( pixel == ui_theme_get()->background || pixel == ui_theme_get()->divider );
                }
            }
        }
        if( 2U == state )
        {
            assert( bolt_pixels > 0U );
        }
    }
    battery_fixture = ( battery_status_t ){
        .available = true, .percentage = 75U, .charge_state = BATTERY_CHARGE_DISCHARGING };
}

static void checks( void )
{
    assert( 0 == RFSCOUT_THEME_INDOOR && 1 == RFSCOUT_THEME_OUTDOOR && 2 == RFSCOUT_THEME_NIGHT );
    for( unsigned id = 0; id < RFSCOUT_THEME_COUNT; ++id )
    {
        rfscout_theme_id_t parsed;
        assert(
            ui_theme_from_config_name( ui_theme_config_name( (rfscout_theme_id_t)id ), &parsed ) );
        assert( parsed == (rfscout_theme_id_t)id );
        assert( ESP_OK == ui_theme_set( parsed ) );
        assert( ui_theme_current() == preference );
        check_overlay_restoration( true );
    }
    assert( ESP_ERR_INVALID_ARG == ui_theme_set( (rfscout_theme_id_t)RFSCOUT_THEME_COUNT ) );
    assert( ESP_ERR_INVALID_ARG == ui_theme_set( (rfscout_theme_id_t)-1 ) );
    assert( ESP_OK == ui_theme_set( RFSCOUT_THEME_PIXEL_QUEST ) );
    display_clear( 0 );
    ui_chrome_draw( "AN EXTREMELY LONG APPLICATION TITLE", 0 );
    /* A title must not paint over the status region, including with a long caller title. */
    for( int y = 0; y < 20; ++y )
        for( int x = 142; x < 188; ++x )
            assert( pixels[ y * 320 + x ] == ui_theme_get()->background );
    display_clear( 0 );
    ui_chrome_draw( "HOME", 0 );
    uint16_t divider = pixels[ 21 * 320 + 20 ];
    ui_game_draw_menu_marker( 8, 24, ">", ui_theme_get()->selected );
    assert( pixels[ 21 * 320 + 20 ] == divider );
    display_clear( 0 );
    ui_game_draw_column( 8, 40, 2, 21, ui_theme_get()->success );
    assert( pixels[ 40 * 320 + 8 ] == ui_theme_get()->success );
    assert( pixels[ 60 * 320 + 8 ] == ui_theme_get()->success );
    ui_game_draw_meter( 20, 40, 24, 10, 500U, 100U, ui_theme_get()->success );
    assert( pixels[ 40 * 320 + 43 ] == ui_theme_get()->background );
    ui_game_draw_meter( 20, 40, 24, 10, 1U, 0U, ui_theme_get()->success );
    /* Zero maximum must leave every cell unlit, including after a full-value draw. */
    for( int x = 24; x < 43; ++x )
    {
        assert( pixels[ 42 * 320 + x ] == ui_theme_get()->background ||
                pixels[ 42 * 320 + x ] == ui_theme_get()->divider );
    }
    display_clear( 0 );
    ui_game_draw_icon( (ui_game_icon_t)-1, 0, 0, 1U, 0xFFFFU );
    for( size_t i = 0; i < DISPLAY_PIXEL_COUNT; ++i )
        assert( pixels[ i ] == ui_theme_get()->background );
}

int main( int a_argc, char **a_argv )
{
    assert( a_argc == 2 );
    assert( ESP_OK == ui_theme_init() );
    checks();
    check_menu_cursor();
    check_hud_bounds();
    check_battery_states();
    display_clear( 0 );
    ui_chrome_draw( "HOME", 0 );
    ui_game_draw_home();
    display_present();
    save( a_argv[ 1 ], "home" );
    wifi_settings_view_render(
        5U, true, false, true, RFSCOUT_EVIDENCE_STRENGTH_PROBABLE, 60U, 60U );
    save( a_argv[ 1 ], "settings" );
    audio_spectrum_view_render();
    /* The spectrum reaches both sides while retaining equal gutters and a quiet bottom edge. */
    bool left_bar = false;
    bool right_bar = false;
    for( int y = 32; y < 160; ++y )
    {
        left_bar |= pixels[ y * 320 + 8 ] == ui_theme_get()->graph_primary;
        right_bar |= pixels[ y * 320 + 310 ] == ui_theme_get()->graph_primary;
    }
    assert( left_bar && right_bar );
    for( int x = 0; x < 320; ++x )
        assert( pixels[ 165 * 320 + x ] == ui_theme_get()->background );
    save( a_argv[ 1 ], "audio" );
    gps_view_render( false, false, false );
    save( a_argv[ 1 ], "gps" );
    recording_state = RFSCOUT_RECORDER_RECORDING;
    recorder_view_landing( NULL, 80U );
    save( a_argv[ 1 ], "recorder" );
    recording_state = RFSCOUT_RECORDER_PLAYING;
    recorder_view_landing( "field-0000042.wav", 65U );
    save( a_argv[ 1 ], "playback" );
    hotspots_view_row_t rows[] = {
        { "FIELD NETWORK", "ONLINE", HOTSPOTS_TONE_GOOD, -42, true },
        { "GUEST", "LIMIT", HOTSPOTS_TONE_LIMITED, -67, true },
        { "CAFE", "NEW", HOTSPOTS_TONE_NEUTRAL, -73, true },
    };
    hotspots_view_browser( rows, 3U, 2U, false, "CENTER: INSPECT" );
    save( a_argv[ 1 ], "hotspots" );
    ota_view_render_update_main( "0.3.0", "0.3.1 available", "FIELD NETWORK", 2U, true, true, "" );
    save( a_argv[ 1 ], "update" );
    cc1101_scan_config_t scan = {
        .profile = CC1101_PROFILE_433, .low_khz = 433050U, .high_khz = 434790U, .step_khz = 25U };
    int8_t rssi[ 64 ];
    for( unsigned i = 0; i < 64U; ++i )
        rssi[ i ] = (int8_t)( -105 + i * 7U % 45U );
    rssi[ 24 ] = -34;
    cc1101_view_discovery( rssi, 64U, &scan, 433650U, -34, true, false );
    save( a_argv[ 1 ], "cc1101" );
    uint8_t fast[ 126 ];
    for( unsigned i = 0; i < 126U; ++i )
        fast[ i ] = (uint8_t)( i * 13U % 160U );
    radio_app_view_nrf24_spectrum( fast, 126U, 0U, 125U, true, NULL, 0 );
    save( a_argv[ 1 ], "nrf24" );
    display_modal_show( "SETTING TIME...", "CENTER: CANCEL" );
    display_present();
    save( a_argv[ 1 ], "dialog" );
    display_modal_clear();
    notification.active = true;
    notification.severity = RFSCOUT_SYSTEM_EVENT_WARNING;
    snprintf( notification.title, sizeof( notification.title ), "%s", "STORAGE UNAVAILABLE" );
    memset( notification.detail, 'W', sizeof( notification.detail ) - 1U );
    notification.detail[ sizeof( notification.detail ) - 1U ] = '\0';
    check_overlay_restoration( false );
    puts( "UI host checks passed; previews use simulated service data." );
    return 0;
}
