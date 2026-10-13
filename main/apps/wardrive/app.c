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
#define ACTIVITY_SECONDS 74U
#define CAPTURE_GPS_REFRESH_US 250000LL
#define CAPTURE_SETTINGS_MAGIC 0x57445256U
#define CAPTURE_SETTINGS_VERSION 1U
#define MENU_BACK 0U
#define MENU_START 1U
#define MENU_SOURCES 2U
#define MENU_PACKETS 3U
#define MENU_GPS 4U
#define MENU_SIZE 5U
#define MENU_FILES 6U
#define MENU_ITEMS 7U

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
static bool sources_menu_open;
static unsigned sources_selected;
static bool wifi_menu_open;
static bool ble_menu_open;
static bool channels_menu_open;
static unsigned radio_selected;
static uint16_t selected_channels = 0x3FFFU;
static bool manual_channels;
static bool shuffle_channels;
static bool ble_active_scan;
static bool ble_dedupe;
static unsigned ble_duty;
static const uint16_t ble_intervals[] = { 0x0040U, 0x0080U, 0x00A0U };
static const uint16_t ble_windows[] = { 0x0040U, 0x0040U, 0x0020U };
static unsigned dwell_choice = 1U;
static const uint32_t dwell_choices[] = { 100U, 200U, 250U, 500U, 1000U };
typedef struct
{
    uint32_t magic;
    uint16_t channels;
    uint8_t manual;
    uint8_t shuffled;
    uint8_t dwell_index;
    uint8_t ble_active;
    uint8_t ble_duplicates;
    uint8_t ble_duty;
} radio_settings_t;

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
static unsigned activity_cursor;
static unsigned activity_samples;
static int64_t activity_last_tick_us;

static void observe_wifi( const uint8_t *a_data, size_t a_length, int8_t a_rssi, uint8_t a_channel )
{
    if( options.wifi && NULL != a_data && a_length >= 2U && a_channel >= 1U &&
        a_channel <= ACTIVITY_CHANNELS )
    {
        const unsigned channel = a_channel - 1U;
        __atomic_add_fetch( &activity_pending[ channel ], 1U, __ATOMIC_RELAXED );
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
    const uint64_t elapsed = (uint64_t)( a_now_us - activity_last_tick_us ) / 1000000ULL;
    if( 0U == elapsed )
    {
        return;
    }
    /* Keep only buckets still inside the window. Counts gathered across a poll
       stall have no individual timestamps, so apportion them over elapsed time
       rather than assigning the entire interval to the oldest missed second. */
    uint32_t pending[ ACTIVITY_CHANNELS ];
    for( unsigned channel = 0U; channel < ACTIVITY_CHANNELS; ++channel )
    {
        pending[ channel ] =
            __atomic_exchange_n( &activity_pending[ channel ], 0U, __ATOMIC_RELAXED );
    }
    const uint32_t ble_pending = __atomic_exchange_n( &activity_ble_pending, 0U, __ATOMIC_RELAXED );
    const unsigned steps = elapsed < ACTIVITY_SECONDS ? (unsigned)elapsed : ACTIVITY_SECONDS;
    const uint64_t first = elapsed - steps;
    if( elapsed > ACTIVITY_SECONDS )
    {
        /* All prior history has expired; start with the oldest retained bucket. */
        memset( activity_history, 0, sizeof( activity_history ) );
        memset( activity_totals, 0, sizeof( activity_totals ) );
        memset( activity_rate, 0, sizeof( activity_rate ) );
        memset( activity_ble_rate, 0, sizeof( activity_ble_rate ) );
        activity_cursor = 0U;
        activity_samples = 0U;
    }
    for( unsigned step = 0U; step < steps; ++step )
    {
        const uint64_t offset = first + step;
        uint32_t rate = 0U;
        for( unsigned channel = 0U; channel < ACTIVITY_CHANNELS; ++channel )
        {
            activity_totals[ channel ] -= activity_history[ activity_cursor ][ channel ];
            const uint32_t count =
                (uint32_t)( (uint64_t)pending[ channel ] * ( offset + 1U ) / elapsed -
                            (uint64_t)pending[ channel ] * offset / elapsed );
            activity_history[ activity_cursor ][ channel ] = count;
            activity_totals[ channel ] += count;
            rate += count;
        }
        activity_rate[ activity_cursor ] = rate;
        activity_ble_rate[ activity_cursor ] =
            (uint32_t)( (uint64_t)ble_pending * ( offset + 1U ) / elapsed -
                        (uint64_t)ble_pending * offset / elapsed );
        activity_cursor = ( activity_cursor + 1U ) % ACTIVITY_SECONDS;
        if( activity_samples < ACTIVITY_SECONDS )
        {
            ++activity_samples;
        }
    }
    activity_last_tick_us += (int64_t)( elapsed * 1000000ULL );
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
        stored.options.dwell_ms <= 5000U )
    {
        options = stored.options;
    }
}

static void load_radio_settings( void )
{
    radio_settings_t stored;
    size_t length = 0U;
    selected_channels = 0x3FFFU;
    /* Migrate legacy fixed-channel captures when no independent radio profile exists. */
    manual_channels = options.fixed_channel;
    if( manual_channels && options.channel >= 1U && options.channel <= 14U )
    {
        selected_channels = (uint16_t)( 1U << ( options.channel - 1U ) );
    }
    shuffle_channels = false;
    dwell_choice = 1U;
    ble_active_scan = false;
    ble_dedupe = false;
    ble_duty = 0U;
    if( ESP_OK == app_store_read( "capture.radio", &stored, sizeof( stored ), &length ) &&
        length == sizeof( stored ) && stored.magic == 0x52575031U && stored.dwell_index < 5U &&
        stored.ble_duty < 3U )
    {
        selected_channels = stored.channels & 0x3FFFU;
        manual_channels = stored.manual != 0U;
        shuffle_channels = stored.shuffled != 0U;
        dwell_choice = stored.dwell_index;
        ble_active_scan = stored.ble_active != 0U;
        ble_dedupe = stored.ble_duplicates != 0U;
        ble_duty = stored.ble_duty;
    }
}

static void save_radio_settings( void )
{
    const radio_settings_t stored = {
        .magic = 0x52575031U,
        .channels = selected_channels,
        .manual = manual_channels,
        .shuffled = shuffle_channels,
        .dwell_index = (uint8_t)dwell_choice,
        .ble_active = ble_active_scan,
        .ble_duplicates = ble_dedupe,
        .ble_duty = (uint8_t)ble_duty,
    };
    last_error = app_store_write( "capture.radio", &stored, sizeof( stored ) );
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

static void format_packet_rate( char *a_buffer, size_t a_size, uint32_t a_rate )
{
    if( a_rate >= 1000000U )
    {
        (void)snprintf( a_buffer, a_size, "%.1fM", (double)a_rate / 1000000.0 );
    }
    else if( a_rate >= 10000U )
    {
        (void)snprintf( a_buffer, a_size, "%.1fk", (double)a_rate / 1000.0 );
    }
    else
    {
        (void)snprintf( a_buffer, a_size, "%lu", (unsigned long)a_rate );
    }
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
    /* Fixed-width compact counts keep both labels inside their 72px columns. */
    const uint32_t saved = stats.written;
    const uint32_t dropped = stats.dropped;
    char count[ 16 ];
    format_packet_rate( count, sizeof( count ), saved );
    (void)snprintf( line, sizeof( line ), "SAVE %s", count );
    display_draw_text( 8, 130, line, theme->success, 1U );
    format_packet_rate( count, sizeof( count ), dropped );
    (void)snprintf( line, sizeof( line ), "DROP %s", count );
    display_draw_text( 81, 130, line, dropped ? theme->warning : theme->text, 1U );
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
    char latest_text[ 16 ];
    char average_text[ 16 ];
    format_packet_rate( latest_text, sizeof( latest_text ), current_rate );
    format_packet_rate( average_text, sizeof( average_text ), average_rate );
    (void)snprintf( line, sizeof( line ), "PKT/s: %s (%s avg)", latest_text, average_text );
    display_draw_text( 163, 25, line, theme->text, 1U );
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
                            : options.ble               ? "BLE"
                                                        : "Off" );
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

/* Source selection is a submenu rather than a cycling single-value control.
 * Wi-Fi and BLE remain independent settings within Wardriving's own profile. */
static void draw_radio_submenu( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool wifi = wifi_menu_open;
    display_clear( theme->background );
    ui_chrome_draw( channels_menu_open ? "WI-FI CHANNELS"
                    : wifi             ? "WI-FI SETTINGS"
                                       : "BLE SETTINGS",
                    theme->secondary );
    const unsigned items = channels_menu_open ? 15U : wifi ? 5U : 4U;
    const unsigned visible = 7U;
    const unsigned first = radio_selected >= visible ? radio_selected - visible + 1U : 0U;
    for( unsigned item = first; item < first + visible && item < items; ++item )
    {
        char label[ 28 ] = "";
        char value[ 24 ] = "";
        if( item == 0U )
        {
            (void)snprintf( label, sizeof( label ), "Back" );
        }
        else if( channels_menu_open )
        {
            (void)snprintf( label, sizeof( label ), "Channel %u", item );
            (void)snprintf( value,
                            sizeof( value ),
                            "%s",
                            selected_channels & ( 1U << ( item - 1U ) ) ? "On" : "Off" );
        }
        else if( wifi )
        {
            static const char *names[] = {
                "", "Channel policy", "Channels", "Scan order", "Dwell time" };
            (void)snprintf( label, sizeof( label ), "%s", names[ item ] );
            if( item == 1U )
            {
                (void)snprintf( value, sizeof( value ), "%s", manual_channels ? "Manual" : "Auto" );
            }
            else if( item == 3U )
            {
                (void)snprintf(
                    value, sizeof( value ), "%s", shuffle_channels ? "Shuffled" : "Sequential" );
            }
            else if( item == 4U )
            {
                (void)snprintf( value,
                                sizeof( value ),
                                "%lu ms",
                                (unsigned long)dwell_choices[ dwell_choice ] );
            }
        }
        else
        {
            static const char *names[] = { "", "Discovery", "Duplicate filter", "Scan duty" };
            (void)snprintf( label, sizeof( label ), "%s", names[ item ] );
            (void)snprintf( value,
                            sizeof( value ),
                            "%s",
                            item == 1U       ? ( ble_active_scan ? "Active" : "Passive" )
                            : item == 2U     ? ( ble_dedupe ? "On" : "Off" )
                            : ble_duty == 0U ? "High"
                            : ble_duty == 1U ? "Medium"
                                             : "Low" );
        }
        const bool enabled = channels_menu_open || !wifi || manual_channels || item < 2U;
        const uint16_t color = !enabled                ? theme->secondary
                               : item == radio_selected ? theme->info
                                                        : theme->text;
        const int y = 24 + (int)( item - first ) * 18;
        display_draw_text( 8, y, item == radio_selected ? ">" : " ", color, 1U );
        display_draw_text( 22, y, label, color, 1U );
        display_draw_text( 214, y, value, color, 1U );
    }
    (void)display_present();
}

static void draw_sources_menu( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    static const char *labels[] = { "Back", "Wi-Fi", "BLE", "Wi-Fi settings", "BLE settings" };
    display_clear( theme->background );
    ui_chrome_draw( "SOURCES", theme->secondary );
    for( unsigned item = 0U; item < 5U; ++item )
    {
        const int y = 26 + (int)item * 20;
        const bool available = ( item != 3U || options.wifi ) && ( item != 4U || options.ble );
        const uint16_t color = !available                ? theme->secondary
                               : item == sources_selected ? theme->info
                                                          : theme->text;
        display_draw_text( 8, y, item == sources_selected ? ">" : " ", color, 1U );
        display_draw_text( 22, y, labels[ item ], color, 1U );
        if( item == 1U || item == 2U )
        {
            const bool enabled = item == 1U ? options.wifi : options.ble;
            display_draw_text( 232, y, enabled ? "On" : "Off", color, 1U );
        }
    }
    display_draw_text( 8, 151, "CENTER: TOGGLE / BACK", theme->secondary, 1U );
    (void)display_present();
}

static void draw_menu( void )
{
    if( wifi_menu_open || ble_menu_open || channels_menu_open )
    {
        draw_radio_submenu();
        return;
    }
    if( sources_menu_open )
    {
        draw_sources_menu();
        return;
    }
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
        const bool disabled = ( item == MENU_PACKETS && !options.wifi );
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
    options.fixed_channel = false;
    if( options.wifi && manual_channels )
    {
        options.dwell_ms = dwell_choices[ dwell_choice ];
    }
    esp_err_t result = radio_monitor_set_manual_channels( options.wifi && manual_channels,
                                                          selected_channels,
                                                          shuffle_channels,
                                                          dwell_choices[ dwell_choice ] );
    if( ESP_OK != result )
    {
        return result;
    }
    result = options.wifi ? radio_monitor_start() : radio_monitor_stop();
    if( ESP_OK != result )
    {
        return result;
    }
    if( options.ble )
    {
        result = ble_monitor_set_scan_preferences(
            ble_active_scan, ble_dedupe, ble_intervals[ ble_duty ], ble_windows[ ble_duty ] );
        if( ESP_OK != result )
        {
            return result;
        }
    }
    result = options.ble ? ble_monitor_start() : ble_monitor_stop();
    return result;
}

static void modify_setting( unsigned a_item, int a_direction )
{
    static const uint32_t sizes[] = { 4U, 8U, 16U, 32U, 64U, 128U };
    static const uint8_t files[] = { 4U, 8U, 16U, 32U, 64U };
    if( a_item == MENU_PACKETS && options.wifi )
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
    if( wifi_menu_open || ble_menu_open || channels_menu_open )
    {
        wardrive_stats_t capture_status;
        wardrive_capture_stats( &capture_status );
        if( capture_status.recording && radio_selected != 0U )
        {
            draw_menu();
            return;
        }
        if( radio_selected == 0U )
        {
            if( channels_menu_open )
            {
                channels_menu_open = false;
                radio_selected = 2U;
            }
            else
            {
                wifi_menu_open = false;
                ble_menu_open = false;
                radio_selected = 0U;
            }
        }
        else if( channels_menu_open )
        {
            selected_channels ^= (uint16_t)( 1U << ( radio_selected - 1U ) );
            save_radio_settings();
            if( selected_channels == 0U )
            {
                options.wifi = false;
                save_settings();
                channels_menu_open = false;
                wifi_menu_open = false;
                sources_menu_open = false;
            }
        }
        else if( wifi_menu_open )
        {
            if( radio_selected == 1U )
            {
                manual_channels = !manual_channels;
            }
            else if( radio_selected == 2U && manual_channels )
            {
                channels_menu_open = true;
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
            save_radio_settings();
        }
        else if( ble_menu_open )
        {
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
            save_radio_settings();
        }
        draw_menu();
        return;
    }
    if( sources_menu_open )
    {
        if( sources_selected == 0U )
        {
            sources_menu_open = false;
        }
        else if( sources_selected >= 3U )
        {
            if( sources_selected == 3U && options.wifi )
            {
                wifi_menu_open = true;
                radio_selected = 0U;
            }
            else if( sources_selected == 4U && options.ble )
            {
                ble_menu_open = true;
                radio_selected = 0U;
            }
        }
        else
        {
            wardrive_stats_t stats;
            wardrive_capture_stats( &stats );
            /* Do not change an active recording's radio ownership as a side effect
             * of browsing a menu. Stop capture explicitly before changing sources. */
            if( !stats.recording )
            {
                if( sources_selected == 1U )
                {
                    options.wifi = !options.wifi;
                }
                else
                {
                    options.ble = !options.ble;
                }
                if( options.wifi && selected_channels == 0U )
                {
                    selected_channels = 0x3FFFU;
                    save_radio_settings();
                }
                save_settings();
            }
        }
        draw_menu();
        return;
    }
    if( selected == MENU_SOURCES )
    {
        sources_menu_open = true;
        sources_selected = 0U;
        draw_menu();
        return;
    }
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
        else if( !options.wifi && !options.ble )
        {
            last_error = ESP_ERR_INVALID_STATE;
            ESP_LOGW( TAG, "cannot start capture: both radio sources disabled" );
            draw_menu();
            return;
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
        load_radio_settings();
        menu_open = false;
        sources_menu_open = false;
        wifi_menu_open = false;
        ble_menu_open = false;
        channels_menu_open = false;
        memset( activity_pending, 0, sizeof( activity_pending ) );
        memset( activity_history, 0, sizeof( activity_history ) );
        memset( activity_totals, 0, sizeof( activity_totals ) );
        memset( activity_rate, 0, sizeof( activity_rate ) );
        memset( activity_ble_rate, 0, sizeof( activity_ble_rate ) );
        __atomic_store_n( &activity_ble_pending, 0U, __ATOMIC_RELAXED );
        activity_cursor = 0U;
        activity_samples = 0U;
        activity_last_tick_us = 0;
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
        (void)radio_monitor_set_manual_channels( false, 0U, false, 250U );
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
            if( wifi_menu_open || ble_menu_open || channels_menu_open )
            {
                const unsigned count = channels_menu_open ? 15U : wifi_menu_open ? 5U : 4U;
                radio_selected =
                    ( radio_selected + ( a_event->input.value > 0 ? 1U : count - 1U ) ) % count;
                draw_menu();
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
            if( sources_menu_open )
            {
                sources_selected =
                    ( sources_selected + ( a_event->input.value > 0 ? 1U : 4U ) ) % 5U;
                draw_menu();
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
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
            if( wifi_menu_open || ble_menu_open || channels_menu_open )
            {
                if( channels_menu_open )
                {
                    channels_menu_open = false;
                }
                else
                {
                    wifi_menu_open = false;
                    ble_menu_open = false;
                }
                radio_selected = 0U;
                draw_menu();
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
            if( sources_menu_open )
            {
                sources_menu_open = false;
                draw_menu();
                return RFSCOUT_APP_RUNTIME_HANDLED;
            }
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
