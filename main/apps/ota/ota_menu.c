/**
    apps/ota/ota_menu.c : Home Network and Update menu controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "ota_menu.h"

#include "app_text_entry.h"
#include "app_session.h"
#include "event.h"
#include "display.h"
#include "firmware_identity.h"
#include "ota_acquisition.h"
#include "ota_catalog.h"
#include "ota_networks.h"
#include "ota_installer.h"
#include "ota_view.h"
#include "network_session.h"
#include "wifi_station.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OTA_REFRESH_INTERVAL_US 125000LL
#define OTA_VISIBLE_MAX OTA_NETWORK_MAX_COUNT
#define OTA_ERROR_TEXT_LENGTH 48U

static const char *TAG = "ota_menu";

typedef enum
{
    OTA_SCREEN_MAIN = 0,
    OTA_SCREEN_ACTION,
    OTA_SCREEN_ADD,
    OTA_SCREEN_SAVED,
    OTA_SCREEN_PASSWORD,
    OTA_SCREEN_BUILDS,
    OTA_SCREEN_BUILD_ACTION
} ota_screen_t;

typedef enum
{
    HOME_SUBMENU_NETWORK = 0,
    HOME_SUBMENU_UPDATE
} home_submenu_t;

static ota_network_list_t saved_networks;
static rfscout_wifi_station_ap_t scan_results[ RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS ];
static ota_view_network_t view_networks[ RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS ];
static size_t scan_count;
static size_t view_network_count;
static size_t visible_saved_indices[ OTA_VISIBLE_MAX ];
static ota_screen_t screen;
static home_submenu_t submenu;
static unsigned selected_item;
static int action_saved_index;
static int action_scan_index;
static char action_ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
static char error_text[ OTA_ERROR_TEXT_LENGTH ];
static rfscout_text_entry_t password_entry;
static ota_catalog_selection_t catalog_selection;
static ota_catalog_build_t available_builds[ OTA_CATALOG_VISIBLE_BUILDS ];
static ota_view_build_t view_builds[ OTA_CATALOG_VISIBLE_BUILDS ];
static size_t available_build_count;
static int selected_build_index;
static bool open_builds_after_catalog;
static char *catalog_json;
static size_t catalog_json_capacity;
static char stable_update_status[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
static uint32_t last_acquisition_generation;
static uint32_t last_installer_generation;
static uint32_t last_scan_generation;
static rfscout_wifi_station_state_t last_station_state;
static bool network_connect_modal_active;
static int64_t last_refresh_us;
/* Borrowed from catalog_selection or available_builds while input is suppressed. */
static const ota_catalog_candidate_t *pending_install_candidate;
static bool close_requested;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_DEVICE, a_value );
}

static void set_error( esp_err_t a_error )
{
    if( ESP_OK == a_error )
    {
        error_text[ 0 ] = '\0';
        return;
    }
    snprintf( error_text, sizeof( error_text ), "%s", esp_err_to_name( a_error ) );
}

static esp_err_t ensure_catalog_json_capacity( size_t a_length )
{
    if( a_length == SIZE_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t required = a_length + 1U;
    if( required <= catalog_json_capacity )
    {
        return ESP_OK;
    }

    char *resized = app_session_resize( catalog_json, required, sizeof( *catalog_json ) );
    if( NULL == resized )
    {
        return ESP_ERR_NO_MEM;
    }

    catalog_json = resized;
    catalog_json_capacity = required;
    return ESP_OK;
}

static void release_catalog_json( void )
{
    if( NULL != catalog_json && app_session_free( catalog_json ) )
    {
        catalog_json = NULL;
        catalog_json_capacity = 0U;
    }
}

static void refresh_scan_results( void )
{
    scan_count =
        rfscout_wifi_station_scan_results( scan_results, RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS );
}

static int scan_index_for_ssid( const char *a_ssid )
{
    if( NULL == a_ssid )
    {
        return -1;
    }
    for( size_t index = 0U; index < scan_count; ++index )
    {
        if( 0 == strcmp( scan_results[ index ].ssid, a_ssid ) )
        {
            return (int)index;
        }
    }
    return -1;
}

static void refresh_action_indices( void )
{
    if( '\0' == action_ssid[ 0 ] )
    {
        action_saved_index = -1;
        action_scan_index = -1;
        return;
    }

    action_saved_index = ota_networks_find( &saved_networks, action_ssid );
    action_scan_index = scan_index_for_ssid( action_ssid );
}

static void build_visible_saved( void )
{
    view_network_count = 0U;
    for( size_t scan_index = 0U; scan_index < scan_count && view_network_count < OTA_VISIBLE_MAX;
         ++scan_index )
    {
        const int saved_index =
            ota_networks_find( &saved_networks, scan_results[ scan_index ].ssid );
        if( saved_index < 0 )
        {
            continue;
        }
        view_networks[ view_network_count ] = ( ota_view_network_t ){
            .ssid = saved_networks.entries[ saved_index ].ssid,
            .rssi = scan_results[ scan_index ].rssi,
        };
        visible_saved_indices[ view_network_count ] = (size_t)saved_index;
        ++view_network_count;
    }
}

static void build_add_list( void )
{
    view_network_count = 0U;
    for( size_t scan_index = 0U;
         scan_index < scan_count && view_network_count < RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS;
         ++scan_index )
    {
        if( ota_networks_find( &saved_networks, scan_results[ scan_index ].ssid ) >= 0 )
        {
            continue;
        }
        view_networks[ view_network_count++ ] = ( ota_view_network_t ){
            .ssid = scan_results[ scan_index ].ssid,
            .rssi = scan_results[ scan_index ].rssi,
        };
    }
}

static void build_saved_list( void )
{
    view_network_count = saved_networks.count;
    for( size_t index = 0U; index < saved_networks.count; ++index )
    {
        const int scan_index = scan_index_for_ssid( saved_networks.entries[ index ].ssid );
        view_networks[ index ] = ( ota_view_network_t ){
            .ssid = saved_networks.entries[ index ].ssid,
            .rssi = scan_index >= 0 ? scan_results[ scan_index ].rssi : INT8_MIN,
        };
    }
}

static bool install_interaction_locked( void )
{
    return ( NULL != pending_install_candidate ) || ota_installer_is_active();
}

static bool ota_operation_active( void )
{
    return install_interaction_locked() || ota_acquisition_is_active();
}

static bool install_action_available( void )
{
    return !ota_operation_active() && OTA_CATALOG_UPDATE_AVAILABLE == catalog_selection.result;
}

static const char *connection_status( void )
{
    rfscout_network_session_status_t network;
    network_session_status( &network );
    switch( network.state )
    {
        case RFSCOUT_NETWORK_SESSION_ASSOCIATING:
            return "Connecting";
        case RFSCOUT_NETWORK_SESSION_TIME_SYNCING:
            return "Setting time";
        case RFSCOUT_NETWORK_SESSION_LIMITED:
            return "Connected / limited";
        case RFSCOUT_NETWORK_SESSION_READY:
            return '\0' != network.ssid[ 0 ] ? network.ssid : "Connected";
        case RFSCOUT_NETWORK_SESSION_FAILED:
            return "Connection failed";
        case RFSCOUT_NETWORK_SESSION_IDLE:
        default:
            break;
    }

    rfscout_wifi_station_status_t station;
    rfscout_wifi_station_status( &station );
    return RFSCOUT_WIFI_STATION_SCANNING == station.state ? "Passive scan" : "Disconnected";
}

static void render_network_main( void )
{
    build_visible_saved();
    rfscout_network_session_status_t network;
    network_session_status( &network );
    rfscout_wifi_station_status_t station;
    rfscout_wifi_station_status( &station );
    const bool connected = RFSCOUT_NETWORK_SESSION_READY == network.state ||
                           RFSCOUT_NETWORK_SESSION_LIMITED == network.state;
    const bool scanning = RFSCOUT_WIFI_STATION_SCANNING == station.state;
    const unsigned item_count = 1U + (unsigned)view_network_count + ( connected ? 1U : 3U );
    if( selected_item >= item_count )
    {
        selected_item = item_count - 1U;
    }

    ota_view_render_network_main(
        view_networks, view_network_count, selected_item, scanning, connected, error_text );
}

static void render_update_main( void )
{
    const rfscout_firmware_identity_t *identity = firmware_identity();
    const bool ready = network_session_ready();
    const bool can_install = ready && install_action_available();
    const unsigned item_count = ready ? ( can_install ? 4U : 3U ) : 1U;
    if( selected_item >= item_count )
    {
        selected_item = item_count - 1U;
    }

    ota_view_render_update_main( identity->version,
                                 stable_update_status,
                                 connection_status(),
                                 selected_item,
                                 ready,
                                 can_install,
                                 error_text );
}

static void render_main( void )
{
    if( HOME_SUBMENU_NETWORK == submenu )
    {
        render_network_main();
    }
    else
    {
        render_update_main();
    }
}

static void render_action( void )
{
    refresh_action_indices();
    const char *ssid = '\0' != action_ssid[ 0 ] ? action_ssid : NULL;
    const bool saved = action_saved_index >= 0 && (size_t)action_saved_index < saved_networks.count;
    const bool visible = action_scan_index >= 0 && (size_t)action_scan_index < scan_count;
    const int8_t rssi = visible ? scan_results[ action_scan_index ].rssi : INT8_MIN;

    rfscout_network_session_status_t network;
    network_session_status( &network );
    const bool target = NULL != ssid && 0 == strcmp( network.ssid, ssid );
    const bool connected =
        target && ( RFSCOUT_NETWORK_SESSION_READY == network.state ||
                    RFSCOUT_NETWORK_SESSION_LIMITED == network.state );
    const char *status_text = error_text;
    if( target && RFSCOUT_NETWORK_SESSION_ASSOCIATING == network.state )
    {
        status_text = "Connecting...";
    }
    else if( target && RFSCOUT_NETWORK_SESSION_TIME_SYNCING == network.state )
    {
        status_text = "Connected / syncing time...";
    }
    else if( target && RFSCOUT_NETWORK_SESSION_LIMITED == network.state )
    {
        status_text = "Connected / time sync failed";
    }
    else if( connected )
    {
        status_text = "Connected / time synced";
    }
    else if( target && RFSCOUT_NETWORK_SESSION_FAILED == network.state )
    {
        status_text = ESP_ERR_NOT_FOUND == network.disconnect_reason ? "Network not found"
                                                                    : "Connection failed";
    }

    unsigned action_count = saved ? 3U : 2U;
    if( selected_item >= action_count )
    {
        selected_item = action_count - 1U;
    }
    const bool open =
        visible && action_scan_index >= 0 ? scan_results[ action_scan_index ].open : false;
    ota_view_render_network_action(
        ssid, rssi, visible, saved, open, connected, selected_item, status_text );
}

static void render_add( void )
{
    build_add_list();
    const unsigned item_count = (unsigned)view_network_count + 1U;
    if( selected_item >= item_count )
    {
        selected_item = item_count - 1U;
    }
    ota_view_render_network_list(
        "ADD NETWORK", view_networks, view_network_count, selected_item, "No new APs seen" );
}

static void render_saved( void )
{
    build_saved_list();
    const unsigned item_count = (unsigned)view_network_count + 1U;
    if( selected_item >= item_count )
    {
        selected_item = item_count - 1U;
    }
    ota_view_render_network_list(
        "SAVED NETWORKS", view_networks, view_network_count, selected_item, "No saved networks" );
}

static void render_password( void )
{
    const char *ssid = '\0' != action_ssid[ 0 ] ? action_ssid : "Network";
    ota_view_render_password( ssid,
                              password_entry.length,
                              app_text_entry_selection( &password_entry ),
                              app_text_entry_character( &password_entry ),
                              error_text );
}

static void build_build_views( void )
{
    for( size_t index = 0U; index < available_build_count; ++index )
    {
        view_builds[ index ].label = available_builds[ index ].label;
        view_builds[ index ].version = available_builds[ index ].candidate.version;
    }
}

static void render_builds( void )
{
    build_build_views();
    const unsigned item_count = (unsigned)available_build_count + 1U;
    if( selected_item >= item_count )
    {
        selected_item = item_count - 1U;
    }
    ota_view_render_build_list( firmware_identity()->version,
                                view_builds,
                                available_build_count,
                                selected_item,
                                "No compatible builds" );
}

static void render_build_action( void )
{
    if( selected_build_index < 0 || (size_t)selected_build_index >= available_build_count )
    {
        screen = OTA_SCREEN_BUILDS;
        selected_item = 0U;
        render_builds();
        return;
    }

    const ota_catalog_build_t *build = &available_builds[ selected_build_index ];
    ota_view_render_build_action( firmware_identity()->version,
                                  build->label,
                                  build->candidate.version,
                                  build->candidate.commit,
                                  selected_item );
}

static void render( void )
{
    switch( screen )
    {
        case OTA_SCREEN_ACTION:
            render_action();
            break;
        case OTA_SCREEN_ADD:
            render_add();
            break;
        case OTA_SCREEN_SAVED:
            render_saved();
            break;
        case OTA_SCREEN_PASSWORD:
            render_password();
            break;
        case OTA_SCREEN_BUILDS:
            render_builds();
            break;
        case OTA_SCREEN_BUILD_ACTION:
            render_build_action();
            break;
        case OTA_SCREEN_MAIN:
        default:
            render_main();
            break;
    }
}

static void begin_passive_scan( void )
{
    set_error( rfscout_wifi_station_passive_scan_start() );
    render();
}

static void move_selection( int a_direction, unsigned a_item_count )
{
    if( 0U == a_item_count )
    {
        selected_item = 0U;
        return;
    }
    int item = (int)selected_item + ( a_direction > 0 ? 1 : -1 );
    if( item < 0 )
    {
        item = (int)a_item_count - 1;
    }
    else if( item >= (int)a_item_count )
    {
        item = 0;
    }
    selected_item = (unsigned)item;
    emit_event( RFSCOUT_EVENT_NAVIGATE, a_direction > 0 ? 1 : -1 );
}

static void open_saved_action( size_t a_saved_index )
{
    if( a_saved_index >= saved_networks.count )
    {
        return;
    }
    snprintf(
        action_ssid, sizeof( action_ssid ), "%s", saved_networks.entries[ a_saved_index ].ssid );
    refresh_action_indices();
    selected_item = 0U;
    screen = OTA_SCREEN_ACTION;
    error_text[ 0 ] = '\0';
    render();
}

static void open_unsaved_action( size_t a_scan_index )
{
    if( a_scan_index >= scan_count )
    {
        return;
    }
    snprintf( action_ssid, sizeof( action_ssid ), "%s", scan_results[ a_scan_index ].ssid );
    refresh_action_indices();
    selected_item = 0U;
    screen = OTA_SCREEN_ACTION;
    error_text[ 0 ] = '\0';
    render();
}

static void begin_update_check( void )
{
    if( ota_operation_active() )
    {
        set_error( ESP_ERR_INVALID_STATE );
        render();
        return;
    }

    if( !network_session_ready() )
    {
        snprintf( error_text, sizeof( error_text ), "Network not ready" );
        render();
        return;
    }

    const esp_err_t result = ota_acquisition_begin_catalog();
    if( ESP_OK != result )
    {
        open_builds_after_catalog = false;
        set_error( result );
        render();
        return;
    }

    error_text[ 0 ] = '\0';
    memset( &catalog_selection, 0, sizeof( catalog_selection ) );
    available_build_count = 0U;
    selected_build_index = -1;
    snprintf( stable_update_status, sizeof( stable_update_status ), "Checking..." );
    render();
}

static void request_candidate_install( const ota_catalog_candidate_t *a_candidate )
{
    if( NULL == a_candidate )
    {
        return;
    }
    if( ota_operation_active() )
    {
        set_error( ESP_ERR_INVALID_STATE );
        render();
        return;
    }

    if( !network_session_ready() )
    {
        snprintf( error_text, sizeof( error_text ), "Network not ready" );
        render();
        return;
    }

    pending_install_candidate = a_candidate;
    error_text[ 0 ] = '\0';
    screen = OTA_SCREEN_MAIN;
    selected_item = 0U;
    display_modal_show( "OTA UPDATE", "Preparing..." );
    render();
}

static void start_pending_install( void )
{
    const ota_catalog_candidate_t *candidate = pending_install_candidate;
    if( NULL == candidate )
    {
        return;
    }
    pending_install_candidate = NULL;

    ota_installer_image_t image;
    memset( &image, 0, sizeof( image ) );
    snprintf( image.version, sizeof( image.version ), "%s", candidate->version );
    snprintf( image.sha256, sizeof( image.sha256 ), "%s", candidate->sha256 );
    image.size = candidate->artifact_size;

    esp_err_t result = ota_installer_begin( &image );
    const bool installer_started = ESP_OK == result;
    if( installer_started )
    {
        result = ota_acquisition_begin_firmware( candidate->artifact_path );
    }
    if( ESP_OK != result )
    {
        if( installer_started )
        {
            (void)ota_installer_abort();
        }
        display_modal_clear();
        set_error( result );
        render();
        return;
    }

    display_modal_show( "OTA UPDATE", "Downloading..." );
    render();
}

static void begin_update_install( void )
{
    if( OTA_CATALOG_UPDATE_AVAILABLE == catalog_selection.result )
    {
        request_candidate_install( &catalog_selection.candidate );
    }
}

static void open_available_builds( void )
{
    if( available_build_count > 0U )
    {
        screen = OTA_SCREEN_BUILDS;
        selected_item = 0U;
        render();
        return;
    }

    open_builds_after_catalog = true;
    begin_update_check();
}

static void open_saved_networks( void )
{
    selected_item = 0U;
    screen = OTA_SCREEN_SAVED;
    render();
}

static void handle_network_main_select( void )
{
    build_visible_saved();
    if( 0U == selected_item )
    {
        close_requested = true;
        return;
    }

    const unsigned menu_item = selected_item - 1U;
    if( menu_item < view_network_count )
    {
        open_saved_action( visible_saved_indices[ menu_item ] );
        return;
    }

    const unsigned action = menu_item - (unsigned)view_network_count;
    if( network_session_has_lan() )
    {
        open_saved_networks();
        return;
    }
    if( 0U == action )
    {
        begin_passive_scan();
        return;
    }
    if( 1U == action )
    {
        selected_item = 0U;
        screen = OTA_SCREEN_ADD;
        render();
        return;
    }
    open_saved_networks();
}

static void handle_update_main_select( void )
{
    if( 0U == selected_item )
    {
        close_requested = true;
        return;
    }
    if( !network_session_ready() )
    {
        return;
    }

    if( 1U == selected_item )
    {
        begin_update_check();
        return;
    }

    unsigned next_item = 2U;
    if( install_action_available() )
    {
        if( next_item == selected_item )
        {
            begin_update_install();
            return;
        }
        ++next_item;
    }
    if( next_item == selected_item )
    {
        open_available_builds();
    }
}

static void handle_main_select( void )
{
    if( HOME_SUBMENU_NETWORK == submenu )
    {
        handle_network_main_select();
    }
    else
    {
        handle_update_main_select();
    }
}

static void save_open_network( void )
{
    if( '\0' == action_ssid[ 0 ] )
    {
        set_error( ESP_ERR_INVALID_STATE );
        render();
        return;
    }
    esp_err_t result = ota_networks_upsert( &saved_networks, action_ssid, "" );
    if( ESP_OK == result )
    {
        result = ota_networks_save( &saved_networks );
    }
    set_error( result );
    if( ESP_OK == result )
    {
        refresh_action_indices();
        selected_item = 0U;
    }
    render();
}

static void begin_password_entry( void )
{
    error_text[ 0 ] = '\0';
    app_text_entry_begin( &password_entry, NULL, OTA_NETWORK_PASSWORD_MAX_LENGTH );
    screen = OTA_SCREEN_PASSWORD;
    render();
}

static void return_to_network_main( void )
{
    display_modal_clear();
    network_connect_modal_active = false;
    screen = OTA_SCREEN_MAIN;
    selected_item = 0U;
    action_saved_index = -1;
    action_scan_index = -1;
    memset( action_ssid, 0, sizeof( action_ssid ) );
    render();
}

static void cancel_network_connect_modal( void )
{
    if( !network_connect_modal_active )
    {
        return;
    }

    const esp_err_t result = network_session_release();
    if( ESP_OK != result )
    {
        set_error( result );
        display_modal_show( "NETWORK", "Cancel failed" );
        render();
        return;
    }

    error_text[ 0 ] = '\0';
    return_to_network_main();
    refresh_scan_results();
    begin_passive_scan();
}

static void connect_selected_network( void )
{
    if( action_saved_index < 0 || (size_t)action_saved_index >= saved_networks.count )
    {
        return;
    }

    const ota_network_t *network = &saved_networks.entries[ action_saved_index ];
    const esp_err_t result = network_session_begin_saved( network->ssid, network->password );
    set_error( result );
    if( ESP_OK != result )
    {
        return_to_network_main();
        return;
    }

    network_connect_modal_active = true;
    display_modal_show( "NETWORK", "Connecting..." );
    render();
}

static void forget_selected_network( void )
{
    if( action_saved_index < 0 || (size_t)action_saved_index >= saved_networks.count )
    {
        return;
    }

    const char *ssid = saved_networks.entries[ action_saved_index ].ssid;
    rfscout_network_session_status_t current;
    network_session_status( &current );
    if( RFSCOUT_NETWORK_SESSION_IDLE != current.state && 0 == strcmp( current.ssid, ssid ) )
    {
        const esp_err_t disconnect_result = network_session_release();
        if( ESP_OK != disconnect_result )
        {
            set_error( disconnect_result );
            render();
            return;
        }
    }

    esp_err_t result = ota_networks_forget( &saved_networks, (size_t)action_saved_index );
    if( ESP_OK == result )
    {
        result = ota_networks_save( &saved_networks );
    }
    set_error( result );
    screen = OTA_SCREEN_MAIN;
    selected_item = 0U;
    action_saved_index = -1;
    action_scan_index = -1;
    memset( action_ssid, 0, sizeof( action_ssid ) );
    render();
}

static void handle_action_select( void )
{
    refresh_action_indices();
    const bool saved = action_saved_index >= 0 && (size_t)action_saved_index < saved_networks.count;
    const bool visible = action_scan_index >= 0 && (size_t)action_scan_index < scan_count;

    if( 0U == selected_item )
    {
        screen = OTA_SCREEN_MAIN;
        selected_item = 0U;
        render();
        return;
    }

    if( !saved )
    {
        if( visible && scan_results[ action_scan_index ].open )
        {
            save_open_network();
        }
        else
        {
            begin_password_entry();
        }
        return;
    }

    if( 1U == selected_item )
    {
        rfscout_network_session_status_t current;
        network_session_status( &current );
        if( ( RFSCOUT_NETWORK_SESSION_READY == current.state ||
              RFSCOUT_NETWORK_SESSION_LIMITED == current.state ) &&
            0 == strcmp( current.ssid, saved_networks.entries[ action_saved_index ].ssid ) )
        {
            set_error( network_session_release() );
            render();
        }
        else
        {
            connect_selected_network();
        }
        return;
    }

    forget_selected_network();
}

static void handle_add_select( void )
{
    build_add_list();
    if( 0U == selected_item )
    {
        screen = OTA_SCREEN_MAIN;
        selected_item = 0U;
        render();
        return;
    }

    const size_t view_index = selected_item - 1U;
    if( view_index >= view_network_count )
    {
        return;
    }
    const int scan_index = scan_index_for_ssid( view_networks[ view_index ].ssid );
    if( scan_index >= 0 )
    {
        open_unsaved_action( (size_t)scan_index );
    }
}

static void handle_saved_select( void )
{
    if( 0U == selected_item )
    {
        screen = OTA_SCREEN_MAIN;
        selected_item = 0U;
        render();
        return;
    }
    const size_t saved_index = selected_item - 1U;
    if( saved_index < saved_networks.count )
    {
        open_saved_action( saved_index );
    }
}

static void handle_builds_select( void )
{
    if( 0U == selected_item )
    {
        screen = OTA_SCREEN_MAIN;
        selected_item = 0U;
        render();
        return;
    }

    const size_t build_index = selected_item - 1U;
    if( build_index >= available_build_count )
    {
        return;
    }
    selected_build_index = (int)build_index;
    selected_item = 0U;
    screen = OTA_SCREEN_BUILD_ACTION;
    render();
}

static void handle_build_action_select( void )
{
    if( 0U == selected_item )
    {
        screen = OTA_SCREEN_BUILDS;
        selected_item = 0U;
        render();
        return;
    }
    if( 1U != selected_item || selected_build_index < 0 ||
        (size_t)selected_build_index >= available_build_count )
    {
        return;
    }

    request_candidate_install( &available_builds[ selected_build_index ].candidate );
}

static void handle_password_input( const rfscout_input_event_t *a_input )
{
    const rfscout_text_entry_result_t result = app_text_entry_input( &password_entry, a_input );
    if( RFSCOUT_TEXT_ENTRY_CANCELLED == result )
    {
        app_text_entry_clear( &password_entry );
        screen = OTA_SCREEN_ACTION;
        error_text[ 0 ] = '\0';
        render();
        return;
    }
    if( RFSCOUT_TEXT_ENTRY_ACCEPTED == result )
    {
        if( '\0' == action_ssid[ 0 ] )
        {
            set_error( ESP_ERR_INVALID_STATE );
            app_text_entry_clear( &password_entry );
            screen = OTA_SCREEN_ACTION;
            render();
            return;
        }

        esp_err_t save_result =
            ota_networks_upsert( &saved_networks, action_ssid, password_entry.value );
        if( ESP_OK == save_result )
        {
            ESP_LOGI( TAG,
                      "saving OTA network credentials; stack high-water=%u bytes",
                      (unsigned)uxTaskGetStackHighWaterMark( NULL ) );
            save_result = ota_networks_save( &saved_networks );
            ESP_LOGI( TAG,
                      "saved OTA network credentials: %s; stack high-water=%u bytes",
                      esp_err_to_name( save_result ),
                      (unsigned)uxTaskGetStackHighWaterMark( NULL ) );
        }
        app_text_entry_clear( &password_entry );
        set_error( save_result );
        screen = OTA_SCREEN_ACTION;
        if( ESP_OK == save_result )
        {
            refresh_action_indices();
            selected_item = 0U;
        }
        render();
        return;
    }
    render();
}

static void handle_input( const rfscout_input_event_t *a_input )
{
    if( OTA_SCREEN_PASSWORD == screen )
    {
        handle_password_input( a_input );
        return;
    }

    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        unsigned item_count = 1U;
        if( OTA_SCREEN_MAIN == screen )
        {
            if( HOME_SUBMENU_NETWORK == submenu )
            {
                build_visible_saved();
                item_count = 1U + (unsigned)view_network_count + ( network_session_has_lan() ? 1U : 3U );
            }
            else
            {
                const bool ready = network_session_ready();
                const bool can_install = ready && install_action_available();
                item_count = ready ? ( can_install ? 4U : 3U ) : 1U;
            }
        }
        else if( OTA_SCREEN_ACTION == screen )
        {
            const bool saved =
                action_saved_index >= 0 && (size_t)action_saved_index < saved_networks.count;
            item_count = saved ? 3U : 2U;
        }
        else if( OTA_SCREEN_ADD == screen )
        {
            build_add_list();
            item_count = (unsigned)view_network_count + 1U;
        }
        else if( OTA_SCREEN_SAVED == screen )
        {
            item_count = (unsigned)saved_networks.count + 1U;
        }
        else if( OTA_SCREEN_BUILDS == screen )
        {
            item_count = (unsigned)available_build_count + 1U;
        }
        else if( OTA_SCREEN_BUILD_ACTION == screen )
        {
            item_count = 2U;
        }
        move_selection( a_input->value > 0 ? 1 : -1, item_count );
        render();
        return;
    }

    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return;
    }

    emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
    if( OTA_SCREEN_MAIN == screen )
    {
        handle_main_select();
    }
    else if( OTA_SCREEN_ACTION == screen )
    {
        handle_action_select();
    }
    else if( OTA_SCREEN_ADD == screen )
    {
        handle_add_select();
    }
    else if( OTA_SCREEN_SAVED == screen )
    {
        handle_saved_select();
    }
    else if( OTA_SCREEN_BUILDS == screen )
    {
        handle_builds_select();
    }
    else if( OTA_SCREEN_BUILD_ACTION == screen )
    {
        handle_build_action_select();
    }
}

static void reset_runtime_state( void )
{
    display_modal_clear();
    ota_networks_clear_runtime( &saved_networks );
    memset( scan_results, 0, sizeof( scan_results ) );
    memset( view_networks, 0, sizeof( view_networks ) );
    memset( visible_saved_indices, 0, sizeof( visible_saved_indices ) );
    scan_count = 0U;
    view_network_count = 0U;
    screen = OTA_SCREEN_MAIN;
    selected_item = 0U;
    action_saved_index = -1;
    action_scan_index = -1;
    memset( action_ssid, 0, sizeof( action_ssid ) );
    memset( error_text, 0, sizeof( error_text ) );
    app_text_entry_clear( &password_entry );
    memset( &catalog_selection, 0, sizeof( catalog_selection ) );
    memset( available_builds, 0, sizeof( available_builds ) );
    memset( view_builds, 0, sizeof( view_builds ) );
    available_build_count = 0U;
    selected_build_index = -1;
    open_builds_after_catalog = false;
    release_catalog_json();
    snprintf( stable_update_status, sizeof( stable_update_status ), "Not checked" );
    last_acquisition_generation = 0U;
    last_installer_generation = 0U;
    last_scan_generation = 0U;
    last_station_state = RFSCOUT_WIFI_STATION_OFF;
    network_connect_modal_active = false;
    last_refresh_us = 0;
    pending_install_candidate = NULL;
    close_requested = false;
}

void network_menu_enter( void )
{
    reset_runtime_state();
    submenu = HOME_SUBMENU_NETWORK;

    const esp_err_t load_result = ota_networks_load( &saved_networks );
    if( ESP_OK != load_result )
    {
        set_error( load_result );
    }

    refresh_scan_results();
    render();

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );
    if( RFSCOUT_WIFI_STATION_CONNECTED != current.state &&
        RFSCOUT_WIFI_STATION_CONNECTING != current.state &&
        RFSCOUT_WIFI_STATION_SEARCHING != current.state &&
        RFSCOUT_WIFI_STATION_SCANNING != current.state &&
        RFSCOUT_WIFI_STATION_DISCONNECTING != current.state )
    {
        begin_passive_scan();
    }
}

void network_menu_leave( void )
{
    reset_runtime_state();
}

void ota_menu_enter( void )
{
    reset_runtime_state();
    submenu = HOME_SUBMENU_UPDATE;
    render();
}

esp_err_t ota_menu_leave( void )
{
    esp_err_t result = ota_acquisition_stop();
    if( ESP_OK != result )
    {
        set_error( result );
        render();
        return result;
    }

    result = ota_installer_abort();
    if( ESP_OK != result )
    {
        set_error( result );
        render();
        return result;
    }

    reset_runtime_state();
    return ESP_OK;
}

static bool submenu_input( const rfscout_input_event_t *a_input )
{
    if( NULL == a_input )
    {
        return false;
    }

    if( network_connect_modal_active )
    {
        if( RFSCOUT_INPUT_SELECT == a_input->type )
        {
            cancel_network_connect_modal();
        }
        return false;
    }
    if( install_interaction_locked() )
    {
        return false;
    }

    close_requested = false;
    handle_input( a_input );
    const bool close = close_requested;
    close_requested = false;
    return close;
}

bool network_menu_input( const rfscout_input_event_t *a_input )
{
    return submenu_input( a_input );
}

bool ota_menu_input( const rfscout_input_event_t *a_input )
{
    return submenu_input( a_input );
}

static void submenu_poll( int64_t a_now_us )
{
    if( a_now_us - last_refresh_us < OTA_REFRESH_INTERVAL_US )
    {
        return;
    }

    last_refresh_us = a_now_us;
    bool redraw = false;

    if( NULL != pending_install_candidate )
    {
        start_pending_install();
        return;
    }

    network_session_poll( a_now_us );
    rfscout_network_session_status_t network;
    network_session_status( &network );

    if( network_connect_modal_active )
    {
        if( RFSCOUT_NETWORK_SESSION_READY == network.state )
        {
            error_text[ 0 ] = '\0';
            return_to_network_main();
            return;
        }
        if( RFSCOUT_NETWORK_SESSION_LIMITED == network.state )
        {
            snprintf( error_text, sizeof( error_text ), "Time sync failed" );
            return_to_network_main();
            return;
        }
        if( RFSCOUT_NETWORK_SESSION_FAILED == network.state )
        {
            snprintf( error_text,
                      sizeof( error_text ),
                      "%s",
                      ESP_ERR_NOT_FOUND == network.disconnect_reason ? "Network not found"
                                                                    : "Connection failed" );
            return_to_network_main();
            return;
        }

        display_modal_show( "NETWORK",
                            RFSCOUT_NETWORK_SESSION_TIME_SYNCING == network.state
                                ? "Setting time..."
                                : "Connecting..." );
        redraw = true;
    }

    rfscout_wifi_station_status_t current;
    rfscout_wifi_station_status( &current );

    if( current.scan_generation != last_scan_generation || current.state != last_station_state )
    {
        last_scan_generation = current.scan_generation;
        last_station_state = current.state;
        refresh_scan_results();
        redraw = true;
    }
    ota_acquisition_status_t acquisition;
    ota_acquisition_status( &acquisition );
    if( acquisition.generation != last_acquisition_generation )
    {
        last_acquisition_generation = acquisition.generation;
        if( OTA_ACQUISITION_READY == acquisition.state )
        {
            if( OTA_ACQUISITION_KIND_CATALOG == acquisition.kind )
            {
                size_t length = 0U;
                esp_err_t result = ensure_catalog_json_capacity( acquisition.length );
                if( ESP_OK == result )
                {
                    result = ota_acquisition_copy_catalog(
                        catalog_json, catalog_json_capacity, &length );
                }
                if( ESP_OK == result )
                {
                    result = ota_catalog_select(
                        catalog_json, length, firmware_identity(), &catalog_selection );
                }
                if( ESP_OK == result )
                {
                    result = ota_catalog_list( catalog_json,
                                               length,
                                               firmware_identity(),
                                               available_builds,
                                               OTA_CATALOG_VISIBLE_BUILDS,
                                               &available_build_count );
                }
                if( ESP_OK == result )
                {
                    if( OTA_CATALOG_UPDATE_AVAILABLE == catalog_selection.result )
                    {
                        snprintf( stable_update_status,
                                  sizeof( stable_update_status ),
                                  "%s",
                                  catalog_selection.candidate.version );
                    }
                    else
                    {
                        snprintf(
                            stable_update_status, sizeof( stable_update_status ), "Up to date" );
                    }
                    error_text[ 0 ] = '\0';
                    if( open_builds_after_catalog )
                    {
                        screen = OTA_SCREEN_BUILDS;
                        selected_item = 0U;
                        open_builds_after_catalog = false;
                    }
                }
                else
                {
                    open_builds_after_catalog = false;
                    set_error( result );
                    snprintf(
                        stable_update_status, sizeof( stable_update_status ), "Check failed" );
                }
            }
            else if( OTA_ACQUISITION_KIND_FIRMWARE == acquisition.kind )
            {
                display_modal_show( "OTA UPDATE", "Verifying..." );
                render();

                esp_err_t result = ota_installer_finish();
                if( ESP_OK == result )
                {
                    result = ota_installer_commit();
                }
                if( ESP_OK == result )
                {
                    display_modal_show( "OTA UPDATE", "Rebooting..." );
                    render();
                    esp_restart();
                }
                display_modal_clear();
                (void)ota_installer_abort();
                set_error( result );
            }
        }
        else if( OTA_ACQUISITION_FAILED == acquisition.state )
        {
            if( OTA_ACQUISITION_KIND_FIRMWARE == acquisition.kind )
            {
                display_modal_clear();
                (void)ota_installer_abort();
            }
            else
            {
                snprintf( stable_update_status, sizeof( stable_update_status ), "Check failed" );
            }
            if( acquisition.http_status > 0 )
            {
                snprintf( error_text, sizeof( error_text ), "HTTP %d", acquisition.http_status );
            }
            else
            {
                set_error( acquisition.error );
            }
        }
        redraw = true;
    }

    ota_installer_status_t installer;
    ota_installer_status( &installer );
    if( installer.generation != last_installer_generation )
    {
        last_installer_generation = installer.generation;
        redraw = true;
    }

    if( OTA_INSTALLER_WRITING == installer.state && installer.expected_size > 0U )
    {
        const unsigned percent =
            (unsigned)( (uint64_t)installer.bytes_written * 100U / installer.expected_size );
        char progress[ 32 ];
        snprintf( progress,
                  sizeof( progress ),
                  "%u%%  %lu / %lu KB",
                  percent,
                  (unsigned long)( installer.bytes_written / 1024U ),
                  (unsigned long)( installer.expected_size / 1024U ) );
        display_modal_show( "OTA UPDATE", progress );
        redraw = true;
    }
    else if( OTA_INSTALLER_VERIFYING == installer.state )
    {
        display_modal_show( "OTA UPDATE", "Verifying..." );
        redraw = true;
    }

    if( redraw )
    {
        render();
    }
}

void network_menu_poll( int64_t a_now_us )
{
    submenu_poll( a_now_us );
}

void ota_menu_poll( int64_t a_now_us )
{
    submenu_poll( a_now_us );
}
