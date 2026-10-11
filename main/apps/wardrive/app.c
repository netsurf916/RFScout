/** apps/wardrive/app.c : RFScout raw wireless PCAPNG capture application. */
#include "app_descriptor.h"

#include "capture.h"
#include "ble_monitor.h"
#include "display.h"
#include "gps.h"
#include "media_store.h"
#include "persistent_store.h"
#include "radio_monitor.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#define CAPTURE_REFRESH_US 500000LL
#define CAPTURE_SETTINGS_MAGIC 0x57445256U
#define CAPTURE_SETTINGS_VERSION 1U
#define MENU_START 0U
#define MENU_SOURCES 1U
#define MENU_MODE 2U
#define MENU_CHANNEL 3U
#define MENU_DWELL 4U
#define MENU_PACKETS 5U
#define MENU_GPS 6U
#define MENU_SIZE 7U
#define MENU_FILES 8U
#define MENU_BACK 9U
#define MENU_ITEMS 10U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    wardrive_config_t options;
} saved_settings_t;

static const char *TAG = "wardrive_app";
static wardrive_config_t options;
static unsigned selected;
static bool menu_open;
static int64_t last_draw_us;
static uint64_t stored_bytes;
static uint64_t free_bytes;
static esp_err_t last_error;

static wardrive_config_t defaults( void )
{
    return (wardrive_config_t){
        .wifi = true,
        .ble = true,
        .fixed_channel = false,
        .channel = 6U,
        .dwell_ms = 200U,
        .file_limit_bytes = 16U * 1024U * 1024U,
        .max_files = 16U,
        .gps_metadata = true,
        .wifi_types = 7U,
    };
}

static void load_settings( void )
{
    saved_settings_t stored;
    size_t length = 0U;
    options = defaults();
    if( ESP_OK == app_store_read( "capture.settings", &stored, sizeof( stored ), &length ) &&
        length == sizeof( stored ) && stored.magic == CAPTURE_SETTINGS_MAGIC &&
        stored.version == CAPTURE_SETTINGS_VERSION && stored.options.max_files > 0U &&
        stored.options.file_limit_bytes >= 1024U * 1024U &&
        stored.options.file_limit_bytes <= 128U * 1024U * 1024U &&
        stored.options.channel >= 1U && stored.options.channel <= 14U &&
        stored.options.dwell_ms >= 50U && stored.options.dwell_ms <= 5000U &&
        ( stored.options.wifi || stored.options.ble ) )
    {
        options = stored.options;
    }
}

static void save_settings( void )
{
    const saved_settings_t stored = {
        .magic = CAPTURE_SETTINGS_MAGIC,
        .version = CAPTURE_SETTINGS_VERSION,
        .options = options,
    };
    last_error = app_store_write( "capture.settings", &stored, sizeof( stored ) );
}

static bool usage_visitor( const rfscout_media_entry_t *a_entry, void *a_context )
{
    uint64_t *used = a_context;
    unsigned long sequence;
    char trailing;
    if( 1 == sscanf( a_entry->name, "drive-%8lu.pcapng%c", &sequence, &trailing ) )
    {
        *used += a_entry->size_bytes;
    }
    return true;
}

static void refresh_space( void )
{
    stored_bytes = 0U;
    free_bytes = 0U;
    rfscout_media_collection_t collection;
    if( ESP_OK == media_app_collection( "captures", &collection ) )
    {
        (void)media_list( &collection, usage_visitor, &stored_bytes );
    }
    (void)media_store_space( NULL, &free_bytes );
}

static void draw_overview( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    wardrive_stats_t stats;
    wardrive_capture_stats( &stats );
    rfscout_gps_snapshot_t gps = { 0 };
    const bool fixed = gps_snapshot_get( &gps ) && gps.location_valid &&
                       gps.location_age_ms <= 5000U;
    char line[ 72 ];

    display_clear( theme->background );
    ui_chrome_draw( "WARDIVING", theme->secondary );
    (void)snprintf( line, sizeof( line ), "STATE: %s",
                    stats.storage_error ? "SD ERROR" : stats.recording ? "RECORDING" : "STOPPED" );
    display_draw_text( 8, 27, line, stats.storage_error ? theme->warning : theme->text, 1U );
    (void)snprintf( line, sizeof( line ), "RADIO: %s%s%s", options.wifi ? "Wi-Fi" : "",
                    options.wifi && options.ble ? " + " : "", options.ble ? "BLE" : "" );
    display_draw_text( 8, 47, line, theme->text, 1U );
    (void)snprintf( line, sizeof( line ), "CHANNEL: %s",
                    options.fixed_channel ? "FIXED" : "SCANNING" );
    display_draw_text( 8, 65, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "GPS: %s", fixed ? "FIX" : "NO FIX" );
    display_draw_text( 202, 65, line, fixed ? theme->success : theme->warning, 1U );
    (void)snprintf( line, sizeof( line ), "Wi-Fi: %lu   BLE: %lu",
                    (unsigned long)stats.received_wifi, (unsigned long)stats.received_ble );
    display_draw_text( 8, 88, line, theme->text, 1U );
    (void)snprintf( line, sizeof( line ), "SAVED: %lu  DROPPED: %lu",
                    (unsigned long)stats.written, (unsigned long)stats.dropped );
    display_draw_text( 8, 108, line, theme->text, 1U );
    (void)snprintf( line, sizeof( line ), "WRITTEN: %lu KiB", (unsigned long)( stats.bytes / 1024U ) );
    display_draw_text( 8, 128, line, theme->info, 1U );
    display_draw_text( 8, 152, "CENTER: SETTINGS", theme->secondary, 1U );
    (void)display_present();
}

static void describe_setting( unsigned a_index, const char **a_label, char *a_value, size_t a_size )
{
    const char *label = "";
    switch( a_index )
    {
        case MENU_START:
        {
            wardrive_stats_t stats;
            wardrive_capture_stats( &stats );
            label = stats.recording ? "Stop capture" : "Start capture";
            break;
        }
        case MENU_SOURCES:
            label = "Sources";
            (void)snprintf( a_value, a_size, "%s", options.wifi && options.ble
                                                      ? "Both"
                                                      : options.wifi ? "Wi-Fi" : "BLE" );
            break;
        case MENU_MODE:
            label = "Wi-Fi mode";
            (void)snprintf( a_value, a_size, "%s", options.fixed_channel ? "Fixed" : "Scan" );
            break;
        case MENU_CHANNEL:
            label = "Channel";
            (void)snprintf( a_value, a_size, "%u", (unsigned)options.channel );
            break;
        case MENU_DWELL:
            label = "Dwell";
            (void)snprintf( a_value, a_size, "%lu ms", (unsigned long)options.dwell_ms );
            break;
        case MENU_PACKETS:
            label = "Wi-Fi frames";
            (void)snprintf( a_value, a_size, "%s",
                            options.wifi_types == 1U ? "Mgmt" :
                            options.wifi_types == 2U ? "Ctrl" :
                            options.wifi_types == 4U ? "Data" : "All" );
            break;
        case MENU_GPS:
            label = "GPS metadata";
            (void)snprintf( a_value, a_size, "%s", options.gps_metadata ? "On" : "Off" );
            break;
        case MENU_SIZE:
            label = "Max file";
            (void)snprintf( a_value, a_size, "%lu MiB",
                            (unsigned long)( options.file_limit_bytes / ( 1024U * 1024U ) ) );
            break;
        case MENU_FILES:
            label = "Max files";
            (void)snprintf( a_value, a_size, "%u", (unsigned)options.max_files );
            break;
        case MENU_BACK:
            label = "Back";
            break;
        default:
            break;
    }
    *a_label = label;
}

static void draw_menu( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "CAPTURE SETTINGS", theme->secondary );
    const unsigned page = selected / 6U;
    for( unsigned i = 0U; i < 6U; ++i )
    {
        const unsigned item = page * 6U + i;
        if( item >= MENU_ITEMS )
        {
            break;
        }
        const char *label;
        char value[ 36 ] = "";
        describe_setting( item, &label, value, sizeof( value ) );
        const bool disabled = ( item >= MENU_MODE && item <= MENU_PACKETS && !options.wifi ) ||
                              ( item == MENU_CHANNEL && !options.fixed_channel ) ||
                              ( item == MENU_DWELL && options.fixed_channel );
        const uint16_t color = disabled ? theme->secondary :
                               item == selected ? theme->info : theme->text;
        display_draw_text( 8, 26 + (int)i * 20, item == selected ? ">" : " ", color, 1U );
        display_draw_text( 22, 26 + (int)i * 20, label, color, 1U );
        if( value[ 0 ] != 0 )
        {
            display_draw_text( 211, 26 + (int)i * 20, value, color, 1U );
        }
    }
    char line[ 64 ];
    const uint64_t limit = (uint64_t)options.file_limit_bytes * options.max_files;
    (void)snprintf( line, sizeof( line ), "CAP %lu MiB USED %lu MiB",
                    (unsigned long)( limit / 1048576U ),
                    (unsigned long)( stored_bytes / 1048576U ) );
    display_draw_text( 8, 151, line, theme->secondary, 1U );
    (void)display_present();
}

static void apply_radio( void )
{
    if( options.wifi )
    {
        (void)radio_monitor_start();
    }
    else
    {
        (void)radio_monitor_stop();
    }
    if( options.ble )
    {
        (void)ble_monitor_start();
    }
    else
    {
        (void)ble_monitor_stop();
    }
}

static void modify_setting( unsigned a_item, int a_direction )
{
    static const uint32_t sizes[] = { 4U, 8U, 16U, 32U, 64U, 128U };
    static const uint8_t files[] = { 4U, 8U, 16U, 32U, 64U };
    static const uint32_t dwells[] = { 100U, 200U, 500U, 1000U };
    if( a_item == MENU_SOURCES )
    {
        unsigned index = options.wifi && options.ble ? 0U : options.wifi ? 1U : 2U;
        index = ( index + ( a_direction > 0 ? 1U : 2U ) ) % 3U;
        options.wifi = index != 2U;
        options.ble = index != 1U;
    }
    else if( a_item == MENU_MODE && options.wifi )
    {
        options.fixed_channel = !options.fixed_channel;
    }
    else if( a_item == MENU_CHANNEL && options.wifi && options.fixed_channel )
    {
        options.channel = (uint8_t)( ( options.channel + ( a_direction > 0 ? 0U : 12U ) ) % 14U + 1U );
    }
    else if( a_item == MENU_DWELL && options.wifi && !options.fixed_channel )
    {
        unsigned i = 0U;
        while( i + 1U < 4U && dwells[ i ] != options.dwell_ms )
        {
            ++i;
        }
        options.dwell_ms = dwells[ ( i + ( a_direction > 0 ? 1U : 3U ) ) % 4U ];
    }
    else if( a_item == MENU_PACKETS && options.wifi )
    {
        const uint8_t sequence[] = { 7U, 1U, 4U, 2U };
        unsigned i = 0U;
        while( i + 1U < 4U && sequence[ i ] != options.wifi_types )
        {
            ++i;
        }
        options.wifi_types = sequence[ ( i + ( a_direction > 0 ? 1U : 3U ) ) % 4U ];
    }
    else if( a_item == MENU_GPS )
    {
        options.gps_metadata = !options.gps_metadata;
    }
    else if( a_item == MENU_SIZE )
    {
        unsigned i = 0U;
        while( i + 1U < 6U && sizes[ i ] * 1048576U != options.file_limit_bytes )
        {
            ++i;
        }
        options.file_limit_bytes = sizes[ ( i + ( a_direction > 0 ? 1U : 5U ) ) % 6U ] * 1048576U;
    }
    else if( a_item == MENU_FILES )
    {
        unsigned i = 0U;
        while( i + 1U < 5U && files[ i ] != options.max_files )
        {
            ++i;
        }
        options.max_files = files[ ( i + ( a_direction > 0 ? 1U : 4U ) ) % 5U ];
    }
    save_settings();
}

static void act_on_selection( void )
{
    if( selected == MENU_BACK )
    {
        menu_open = false;
        draw_overview();
        return;
    }
    if( selected == MENU_START )
    {
        wardrive_stats_t stats;
        wardrive_capture_stats( &stats );
        if( stats.recording )
        {
            wardrive_capture_stop();
        }
        else
        {
            apply_radio();
            last_error = wardrive_capture_start( &options );
            if( ESP_OK != last_error )
            {
                ESP_LOGW( TAG, "capture start failed: %s", esp_err_to_name( last_error ) );
            }
        }
        refresh_space();
        draw_menu();
    }
    else
    {
        wardrive_stats_t stats;
        wardrive_capture_stats( &stats );
        if( !stats.recording )
        {
            modify_setting( selected, 1 );
            draw_menu();
        }
    }
}

static rfscout_app_runtime_result_t wardrive_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        load_settings();
        menu_open = false;
        selected = MENU_START;
        last_draw_us = 0;
        last_error = ESP_OK;
        draw_overview();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        wardrive_capture_stop();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            if( menu_open )
            {
                act_on_selection();
            }
            else
            {
                menu_open = true;
                refresh_space();
                draw_menu();
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_NAVIGATION == a_event->input.type && menu_open )
        {
            if( a_event->input.value > 0 )
            {
                selected = ( selected + 1U ) % MENU_ITEMS;
            }
            else
            {
                selected = ( selected + MENU_ITEMS - 1U ) % MENU_ITEMS;
            }
            draw_menu();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_LONG_PRESS == a_event->input.type && menu_open )
        {
            menu_open = false;
            draw_overview();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( a_event->activated && options.wifi && radio_monitor_is_active() )
        {
            radio_monitor_step();
        }
        if( !menu_open && ( a_event->now_us - last_draw_us >= CAPTURE_REFRESH_US ) )
        {
            last_draw_us = a_event->now_us;
            draw_overview();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_wardrive_app = {
    .id = RFSCOUT_APP_WARDRIVE,
    .name = "Wardriving",
    .persistence_key = "wardrive",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_BLE_MONITOR,
    .runtime = wardrive_runtime,
};
