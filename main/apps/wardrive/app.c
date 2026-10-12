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
#define ACTIVITY_CHANNELS 14U
#define ACTIVITY_IDENTITIES 64U
#define ACTIVITY_SECONDS 74U
#define CAPTURE_GPS_REFRESH_US 250000LL
#define CAPTURE_SETTINGS_MAGIC 0x57445256U
#define CAPTURE_SETTINGS_VERSION 1U
#define MENU_BACK 0U
#define MENU_START 1U
#define MENU_SOURCES 2U
#define MENU_MODE 3U
#define MENU_CHANNEL 4U
#define MENU_DWELL 5U
#define MENU_PACKETS 6U
#define MENU_GPS 7U
#define MENU_SIZE 8U
#define MENU_FILES 9U
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
static int64_t last_gps_refresh_us;
static uint64_t stored_bytes;
static uint64_t free_bytes;
static esp_err_t last_error;
static uint32_t activity_pending[ ACTIVITY_CHANNELS ];
static uint32_t activity_history[ ACTIVITY_SECONDS ][ ACTIVITY_CHANNELS ];
static uint32_t activity_totals[ ACTIVITY_CHANNELS ];
static uint32_t activity_rate[ ACTIVITY_SECONDS ];
static uint32_t activity_ble_pending;
static uint32_t activity_ble_rate[ ACTIVITY_SECONDS ];
static uint32_t activity_ap_ids[ ACTIVITY_CHANNELS ][ ACTIVITY_IDENTITIES ];
static uint32_t activity_station_ids[ ACTIVITY_CHANNELS ][ ACTIVITY_IDENTITIES ];
static uint32_t activity_aps[ ACTIVITY_CHANNELS ];
static uint32_t activity_stations[ ACTIVITY_CHANNELS ];
static unsigned activity_cursor;
static unsigned activity_samples;
static int64_t activity_last_tick_us;

/* One hash slot and one compare-exchange at most; never scan a full table in the
   promiscuous driver callback. These are lower-bound sampled identity counts. */
static void
observe_identity( uint32_t a_ids[ ACTIVITY_IDENTITIES ], uint32_t *a_count, const uint8_t *a_mac )
{
    uint32_t hash = 2166136261U;
    for( unsigned i = 0U; i < 6U; ++i )
    {
        hash = ( hash ^ a_mac[ i ] ) * 16777619U;
    }
    if( 0U == hash )
    {
        hash = 1U;
    }
    uint32_t expected = 0U;
    if( __atomic_compare_exchange_n( &a_ids[ hash % ACTIVITY_IDENTITIES ],
                                     &expected,
                                     hash,
                                     false,
                                     __ATOMIC_RELAXED,
                                     __ATOMIC_RELAXED ) )
    {
        __atomic_add_fetch( a_count, 1U, __ATOMIC_RELAXED );
    }
}

static void observe_wifi( const uint8_t *a_data, size_t a_length, int8_t a_rssi, uint8_t a_channel )
{
    if( options.wifi && NULL != a_data && a_length >= 2U && a_channel >= 1U &&
        a_channel <= ACTIVITY_CHANNELS )
    {
        const unsigned channel = a_channel - 1U;
        __atomic_add_fetch( &activity_pending[ channel ], 1U, __ATOMIC_RELAXED );
        if( a_length >= 24U )
        {
            const uint8_t type = ( a_data[ 0 ] >> 2U ) & 3U;
            const uint8_t subtype = a_data[ 0 ] >> 4U;
            if( type == 0U && ( subtype == 8U || subtype == 5U ) )
            {
                observe_identity(
                    activity_ap_ids[ channel ], &activity_aps[ channel ], a_data + 10U );
            }
            else if( ( type == 0U && ( subtype == 4U || subtype == 0U || subtype == 2U ) ) ||
                     ( type == 2U && ( a_data[ 1 ] & 3U ) == 1U ) )
            {
                observe_identity(
                    activity_station_ids[ channel ], &activity_stations[ channel ], a_data + 10U );
            }
        }
    }
    wardrive_on_wifi( a_data, a_length, a_rssi, a_channel );
}

/* Account for BLE in constant time before forwarding the original capture event. */
static void observe_ble( const uint8_t *a_data,
                         size_t a_length,
                         const uint8_t a_address[ 6 ],
                         uint8_t a_address_type,
                         uint8_t a_event_type,
                         int8_t a_rssi )
{
    if( options.ble && NULL != a_data )
    {
        __atomic_add_fetch( &activity_ble_pending, 1U, __ATOMIC_RELAXED );
    }
    wardrive_on_ble( a_data, a_length, a_address, a_address_type, a_event_type, a_rssi );
}

/* Main-task one-second rollover; callbacks only increment atomic accumulators. */
static void activity_tick( int64_t a_now_us )
{
    if( 0 == activity_last_tick_us )
    {
        activity_last_tick_us = a_now_us;
        return;
    }
    while( a_now_us - activity_last_tick_us >= 1000000LL )
    {
        uint32_t rate = 0U;
        for( unsigned channel = 0U; channel < ACTIVITY_CHANNELS; ++channel )
        {
            const uint32_t expired = activity_history[ activity_cursor ][ channel ];
            activity_totals[ channel ] -= expired;
            const uint32_t count =
                __atomic_exchange_n( &activity_pending[ channel ], 0U, __ATOMIC_RELAXED );
            activity_history[ activity_cursor ][ channel ] = count;
            activity_totals[ channel ] += count;
            rate += count;
        }
        activity_rate[ activity_cursor ] = rate;
        activity_ble_rate[ activity_cursor ] =
            __atomic_exchange_n( &activity_ble_pending, 0U, __ATOMIC_RELAXED );
        activity_cursor = ( activity_cursor + 1U ) % ACTIVITY_SECONDS;
        if( activity_samples < ACTIVITY_SECONDS )
        {
            ++activity_samples;
        }
        activity_last_tick_us += 1000000LL;
    }
}

static wardrive_config_t defaults( void )
{
    return ( wardrive_config_t ){
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
        stored.options.file_limit_bytes <= 128U * 1024U * 1024U && stored.options.channel >= 1U &&
        stored.options.channel <= 14U && stored.options.dwell_ms >= 50U &&
        stored.options.dwell_ms <= 5000U && ( stored.options.wifi || stored.options.ble ) )
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
    const bool fixed =
        gps_snapshot_get( &gps ) && gps.location_valid && gps.location_age_ms <= 5000U;
    const unsigned current = radio_monitor_channel();
    char line[ 72 ];
    uint32_t peak_rate = 1U;
    uint32_t peak_channel = 1U;
    for( unsigned i = 0U; i < ACTIVITY_SECONDS; ++i )
    {
        const uint32_t wifi = options.wifi ? activity_rate[ i ] : 0U;
        const uint32_t ble = options.ble ? activity_ble_rate[ i ] : 0U;
        const uint64_t combined = (uint64_t)wifi + ble;
        if( combined > peak_rate )
        {
            peak_rate = (uint32_t)combined;
        }
    }
    for( unsigned i = 0U; i < ACTIVITY_CHANNELS; ++i )
    {
        if( activity_totals[ i ] > peak_channel )
        {
            peak_channel = activity_totals[ i ];
        }
    }

    display_clear( theme->background );
    /* ui_chrome preserves the platform status region above y=20 (clock, SD,
       battery and other system icons); the dashboard is confined below it. */
    ui_chrome_draw( "WARDRIVING", theme->secondary );
    display_fill_rect( 156, 23, 1, 140, theme->secondary );
    display_fill_rect( 8, 82, 142, 1, theme->secondary );
    display_fill_rect( 8, 124, 142, 1, theme->secondary );
    display_fill_rect( 163, 93, 149, 1, theme->secondary );

    display_draw_text( 8, 26, "GPS POSITION", theme->secondary, 1U );
    display_draw_text(
        111, 26, fixed ? "FIX" : "NO FIX", fixed ? theme->success : theme->warning, 1U );
    if( fixed )
    {
        (void)snprintf( line, sizeof( line ), "%.6f", gps.latitude_deg );
        display_draw_text( 8, 42, line, theme->text, 1U );
        (void)snprintf( line, sizeof( line ), "%.6f", gps.longitude_deg );
        display_draw_text( 8, 58, line, theme->text, 1U );
    }
    else
    {
        display_draw_text( 8, 42, "--.------", theme->secondary, 1U );
        display_draw_text( 8, 58, "--.------", theme->secondary, 1U );
    }
    display_draw_text( 8, 88, "WIFI", theme->secondary, 1U );
    display_draw_text( 84, 88, "BLE", theme->secondary, 1U );
    (void)snprintf( line, sizeof( line ), "%lu", (unsigned long)stats.received_wifi );
    display_draw_text( 8, 103, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "%lu", (unsigned long)stats.received_ble );
    display_draw_text( 84, 103, line, theme->info, 1U );
    (void)snprintf( line, sizeof( line ), "SAVE %lu", (unsigned long)stats.written );
    display_draw_text( 8, 130, line, theme->success, 1U );
    (void)snprintf( line, sizeof( line ), "DROP %lu", (unsigned long)stats.dropped );
    display_draw_text( 81, 130, line, stats.dropped ? theme->warning : theme->text, 1U );
    display_draw_text( 8,
                       146,
                       stats.storage_error ? "SD ERROR"
                       : stats.recording   ? "REC"
                                           : "STOP",
                       stats.storage_error ? theme->error
                       : stats.recording   ? theme->success
                                           : theme->secondary,
                       1U );
    (void)snprintf(
        line, sizeof( line ), "CH %u %s", current, options.fixed_channel ? "FIXED" : "SCAN" );
    display_draw_text( 60, 146, line, theme->info, 1U );

    display_draw_text( 163, 25, "PKT/s NOW / AVG", theme->secondary, 1U );
    /* The header reports the latest completed second, not the graph's peak. */
    const unsigned latest = ( activity_cursor + ACTIVITY_SECONDS - 1U ) % ACTIVITY_SECONDS;
    const uint32_t current_rate = ( options.wifi ? activity_rate[ latest ] : 0U ) +
                                  ( options.ble ? activity_ble_rate[ latest ] : 0U );
    uint64_t rate_sum = 0U;
    for( unsigned i = 0U; i < ACTIVITY_SECONDS; ++i )
    {
        rate_sum += ( options.wifi ? activity_rate[ i ] : 0U ) +
                    ( options.ble ? activity_ble_rate[ i ] : 0U );
    }
    const uint32_t average_rate =
        activity_samples > 0U ? (uint32_t)( rate_sum / activity_samples ) : 0U;
    (void)snprintf( line,
                    sizeof( line ),
                    "%lu / %lu",
                    (unsigned long)current_rate,
                    (unsigned long)average_rate );
    display_draw_text( 258, 25, line, theme->text, 1U );
    for( unsigned i = 0U; i < ACTIVITY_SECONDS; ++i )
    {
        const unsigned slot = ( activity_cursor + i ) % ACTIVITY_SECONDS;
        const uint32_t wifi = options.wifi ? activity_rate[ slot ] : 0U;
        const uint32_t ble = options.ble ? activity_ble_rate[ slot ] : 0U;
        const uint64_t combined = (uint64_t)wifi + ble;
        const int wifi_height = (int)( (uint64_t)wifi * 46U / peak_rate );
        const int total_height = (int)( combined * 46U / peak_rate );
        const int x = 164 + (int)i * 2;
        if( options.wifi && options.ble )
        {
            if( wifi_height > 0 )
            {
                display_fill_rect( x, 89 - wifi_height, 2, wifi_height, theme->info );
            }
            if( total_height > wifi_height )
            {
                display_fill_rect(
                    x, 89 - total_height, 2, total_height - wifi_height, theme->accent );
            }
        }
        else if( total_height > 0 )
        {
            display_fill_rect( x,
                               89 - total_height,
                               2,
                               total_height,
                               combined * 4U >= (uint64_t)peak_rate * 3U ? theme->warning
                                                                         : theme->info );
        }
    }
    display_draw_text( 163, 95, "CHANNELS / LAST 74s", theme->secondary, 1U );
    for( unsigned i = 0U; i < ACTIVITY_CHANNELS; ++i )
    {
        const int x = 164 + (int)i * 11;
        const uint32_t value = activity_totals[ i ];
        const int height = (int)( (uint64_t)value * 37U / peak_channel );
        if( height > 0 )
        {
            const uint16_t color = i + 1U == current                 ? theme->success
                                   : value * 4U >= peak_channel * 3U ? theme->warning
                                                                     : theme->info;
            display_fill_rect( x, 146 - height, 8, height, color );
        }
    }
    /* Separate the channel bars from their labels without obscuring either. */
    display_fill_rect( 163, 147, 149, 1, theme->secondary );
    for( unsigned i = 0U; i < ACTIVITY_CHANNELS; i += 2U )
    {
        const int x = 164 + (int)i * 11;
        (void)snprintf( line, sizeof( line ), "%u", i + 1U );
        display_draw_text( x, 149, line, theme->text, 1U );
    }
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
            (void)snprintf( a_value,
                            a_size,
                            "%s",
                            options.wifi && options.ble ? "Both"
                            : options.wifi              ? "Wi-Fi"
                                                        : "BLE" );
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
            (void)snprintf( a_value,
                            a_size,
                            "%s",
                            options.wifi_types == 1U   ? "Mgmt"
                            : options.wifi_types == 2U ? "Ctrl"
                            : options.wifi_types == 4U ? "Data"
                                                       : "All" );
            break;
        case MENU_GPS:
            label = "GPS metadata";
            (void)snprintf( a_value, a_size, "%s", options.gps_metadata ? "On" : "Off" );
            break;
        case MENU_SIZE:
            label = "Max file";
            (void)snprintf( a_value,
                            a_size,
                            "%lu MiB",
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
        const uint16_t color = disabled           ? theme->secondary
                               : item == selected ? theme->info
                                                  : theme->text;
        display_draw_text( 8, 26 + (int)i * 20, item == selected ? ">" : " ", color, 1U );
        display_draw_text( 22, 26 + (int)i * 20, label, color, 1U );
        if( value[ 0 ] != 0 )
        {
            display_draw_text( 211, 26 + (int)i * 20, value, color, 1U );
        }
    }
    char line[ 64 ];
    const uint64_t limit = (uint64_t)options.file_limit_bytes * options.max_files;
    (void)snprintf( line,
                    sizeof( line ),
                    "CAP %luM USED %luM FREE %luM",
                    (unsigned long)( limit / 1048576U ),
                    (unsigned long)( stored_bytes / 1048576U ),
                    (unsigned long)( free_bytes / 1048576U ) );
    display_draw_text( 8, 151, line, theme->secondary, 1U );
    (void)display_present();
}

static esp_err_t apply_radio( void )
{
    esp_err_t result = options.wifi ? radio_monitor_start() : radio_monitor_stop();
    if( ESP_OK != result )
    {
        return result;
    }
    result = options.ble ? ble_monitor_start() : ble_monitor_stop();
    return result;
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
        options.channel =
            (uint8_t)( ( options.channel + ( a_direction > 0 ? 0U : 12U ) ) % 14U + 1U );
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
            (void)radio_monitor_stop();
            (void)ble_monitor_stop();
        }
        else
        {
            wardrive_capture_stop();
            last_error = apply_radio();
            if( ESP_OK == last_error )
            {
                last_error = wardrive_capture_start( &options );
                if( ESP_OK == last_error )
                {
                    wardrive_capture_update_gps();
                }
            }
            if( ESP_OK != last_error )
            {
                (void)radio_monitor_stop();
                (void)ble_monitor_stop();
                ESP_LOGW( TAG, "capture start failed: %s", esp_err_to_name( last_error ) );
            }
        }
        /* Starting capture returns to the live dashboard; leave failed starts
           in settings so the user can adjust the configuration. */
        wardrive_stats_t updated;
        wardrive_capture_stats( &updated );
        if( !stats.recording && updated.recording )
        {
            menu_open = false;
            draw_overview();
        }
        else
        {
            refresh_space();
            draw_menu();
        }
    }
    else
    {
        wardrive_stats_t stats;
        wardrive_capture_stats( &stats );
        const bool restart = stats.recording;
        if( restart )
        {
            wardrive_capture_stop();
        }
        modify_setting( selected, 1 );
        if( restart )
        {
            last_error = apply_radio();
            if( ESP_OK == last_error )
            {
                last_error = wardrive_capture_start( &options );
                if( ESP_OK == last_error )
                {
                    wardrive_capture_update_gps();
                }
            }
            if( ESP_OK != last_error )
            {
                (void)radio_monitor_stop();
                (void)ble_monitor_stop();
                ESP_LOGW( TAG, "capture restart failed: %s", esp_err_to_name( last_error ) );
            }
        }
        refresh_space();
        draw_menu();
    }
}

static esp_err_t prepare_leave( void )
{
    wardrive_stats_t stats;
    wardrive_capture_stats( &stats );
    /* Like Recorder, never interrupt a capture through app navigation. */
    return stats.recording ? ESP_ERR_INVALID_STATE : ESP_OK;
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
        memset( activity_pending, 0, sizeof( activity_pending ) );
        memset( activity_history, 0, sizeof( activity_history ) );
        memset( activity_totals, 0, sizeof( activity_totals ) );
        memset( activity_rate, 0, sizeof( activity_rate ) );
        memset( activity_ble_rate, 0, sizeof( activity_ble_rate ) );
        __atomic_store_n( &activity_ble_pending, 0U, __ATOMIC_RELAXED );
        memset( activity_aps, 0, sizeof( activity_aps ) );
        memset( activity_stations, 0, sizeof( activity_stations ) );
        activity_cursor = 0U;
        activity_samples = 0U;
        activity_last_tick_us = 0;
        memset( activity_ap_ids, 0, sizeof( activity_ap_ids ) );
        memset( activity_station_ids, 0, sizeof( activity_station_ids ) );
        selected = MENU_BACK;
        last_draw_us = 0;
        last_gps_refresh_us = 0;
        last_error = ESP_OK;
        (void)radio_monitor_stop();
        (void)ble_monitor_stop();
        draw_overview();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_ACTIVATE == a_event->type )
    {
        (void)radio_monitor_stop();
        (void)ble_monitor_stop();
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
        activity_tick( a_event->now_us );
        wardrive_stats_t capture_status;
        wardrive_capture_stats( &capture_status );
        if( capture_status.storage_error )
        {
            /* Terminate receivers and drain buffers on terminal writer failure. */
            wardrive_capture_stop();
        }
        if( a_event->now_us - last_gps_refresh_us >= CAPTURE_GPS_REFRESH_US )
        {
            last_gps_refresh_us = a_event->now_us;
            wardrive_capture_update_gps();
        }
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
    .prepare_leave = prepare_leave,
    .transition_denial_message = "STOP CAPTURE FIRST",
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_BLE_MONITOR,
    .consumers =
        {
            .wifi_raw = observe_wifi,
            .ble_raw = observe_ble,
        },
    .runtime = wardrive_runtime,
};
