/**
    apps/hotspots/hotspot_history.c : Hotspots durable characterization history
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_history.h"

#include "app_session.h"
#include "device_config.h"
#include "media_journal.h"

#include "cJSON.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HISTORY_COLLECTION "history"
#define HISTORY_PREFIX "hotspots"
#define HISTORY_SEGMENT_BYTES ( 256U * 1024U )
#define HISTORY_MAX_SEGMENTS 32U
/* Raw history beyond this is condensed automatically after replay. */
#define HISTORY_COMPACT_BYTES ( 1024U * 1024U )
#define HISTORY_REPLAY_LINES_PER_POLL 24U
#define HISTORY_COMPACT_ENTRIES_PER_POLL 16U
#define HISTORY_FLUSH_INTERVAL_US 1000000LL

typedef enum
{
    HISTORY_RECORD_CHARACTERIZATION = 0,
    HISTORY_RECORD_LOCATION,
} history_record_kind_t;

/** One journal record; applied to the index unless it arrived during compaction. */
typedef struct
{
    history_record_kind_t kind;
    bool applied;
    uint8_t bssid[ 6 ];
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    int32_t auth_mode;
    int64_t utc;
    int8_t rssi;
    hotspot_status_t status;
    bool location_valid;
    double latitude;
    double longitude;
    hotspot_history_network_t network;
    char public_ip[ HOTSPOT_PUBLIC_IP_MAX_LENGTH + 1U ];
    char colo[ HOTSPOT_COLO_MAX_LENGTH + 1U ];
} history_record_t;

typedef enum
{
    HISTORY_STATE_CLOSED = 0,
    HISTORY_STATE_REPLAYING,
    HISTORY_STATE_READY,
    HISTORY_STATE_COMPACTING,
} history_state_t;

static const char *TAG = "hotspot_history";
static media_journal_t journal;
static media_journal_reader_t reader;
static history_state_t state;
static bool journal_bound;
static bool degraded;
static bool compaction_requested;
static size_t compact_cursor;
static uint64_t replay_total_bytes;
static uint64_t replay_read_bytes;
static size_t replay_malformed;
static int64_t next_flush_us;

/* App-session memory. */
static hotspot_history_entry_t *entries;
static size_t entry_count;
static size_t entry_capacity;
static history_record_t *pending;
static size_t pending_count;
static size_t pending_capacity;
static char *line_buffer;

static bool grow( void **a_items, size_t *a_capacity, size_t a_needed, size_t a_size )
{
    if( a_needed <= *a_capacity )
    {
        return true;
    }
    size_t capacity = 0U != *a_capacity ? *a_capacity : 16U;
    while( capacity < a_needed )
    {
        capacity *= 2U;
    }
    void *items = NULL == *a_items ? app_session_calloc( capacity, a_size )
                                   : app_session_resize( *a_items, capacity, a_size );
    if( NULL == items )
    {
        return false;
    }
    *a_items = items;
    *a_capacity = capacity;
    return true;
}

static hotspot_history_entry_t *find_bssid( const uint8_t a_bssid[ 6 ] )
{
    for( size_t index = 0U; index < entry_count; ++index )
    {
        if( 0 == memcmp( entries[ index ].bssid, a_bssid, 6U ) )
        {
            return &entries[ index ];
        }
    }
    return NULL;
}

static hotspot_history_entry_t *find_or_create( const uint8_t a_bssid[ 6 ] )
{
    hotspot_history_entry_t *entry = find_bssid( a_bssid );
    if( NULL != entry )
    {
        return entry;
    }
    if( !grow( (void **)&entries, &entry_capacity, entry_count + 1U, sizeof( *entries ) ) )
    {
        return NULL;
    }
    entry = &entries[ entry_count++ ];
    memset( entry, 0, sizeof( *entry ) );
    memcpy( entry->bssid, a_bssid, 6U );
    return entry;
}

/** A changed SSID, auth mode or network configuration starts a new epoch. */
static bool same_epoch( const hotspot_history_entry_t *a_entry,
                        const char *a_ssid,
                        int32_t a_auth_mode,
                        const hotspot_history_network_t *a_network )
{
    return 0 == strcmp( a_entry->ssid, a_ssid ) && a_entry->auth_mode == a_auth_mode &&
           a_entry->network.gateway == a_network->gateway &&
           a_entry->network.netmask == a_network->netmask &&
           a_entry->network.dns_primary == a_network->dns_primary &&
           a_entry->network.dns_backup == a_network->dns_backup;
}

static void begin_epoch( hotspot_history_entry_t *a_entry, const char *a_ssid, int32_t a_auth_mode )
{
    uint8_t bssid[ 6 ];
    memcpy( bssid, a_entry->bssid, sizeof( bssid ) );
    memset( a_entry, 0, sizeof( *a_entry ) );
    memcpy( a_entry->bssid, bssid, sizeof( bssid ) );
    snprintf( a_entry->ssid, sizeof( a_entry->ssid ), "%s", a_ssid );
    a_entry->auth_mode = a_auth_mode;
}

static void apply_characterization( const history_record_t *a_record )
{
    hotspot_history_entry_t *entry = find_or_create( a_record->bssid );
    if( NULL == entry )
    {
        return;
    }
    if( 0U == entry->successes ||
        !same_epoch( entry, a_record->ssid, a_record->auth_mode, &a_record->network ) )
    {
        begin_epoch( entry, a_record->ssid, a_record->auth_mode );
    }
    /* Gateway MAC, egress IP and colo are enrichment: a later record without them keeps them. */
    const hotspot_history_network_t previous = entry->network;
    entry->network = a_record->network;
    if( !a_record->network.gateway_mac_valid && previous.gateway_mac_valid )
    {
        entry->network.gateway_mac_valid = true;
        memcpy( entry->network.gateway_mac, previous.gateway_mac, 6U );
    }
    if( '\0' != a_record->public_ip[ 0 ] )
    {
        memcpy( entry->public_ip, a_record->public_ip, sizeof( entry->public_ip ) );
        memcpy( entry->colo, a_record->colo, sizeof( entry->colo ) );
    }
    entry->status = a_record->status;
    entry->validated_utc = a_record->utc;
    entry->rssi = a_record->rssi;
    ++entry->successes;
    if( a_record->location_valid )
    {
        geo_bounds_extend( &entry->bounds, a_record->latitude, a_record->longitude );
        entry->gate = ( geo_movement_gate_t ){ true, a_record->latitude, a_record->longitude };
    }
}

static void apply_location( const history_record_t *a_record )
{
    hotspot_history_entry_t *entry = find_bssid( a_record->bssid );
    if( NULL != entry && 0U != entry->successes && 0 == strcmp( entry->ssid, a_record->ssid ) &&
        entry->auth_mode == a_record->auth_mode && a_record->location_valid )
    {
        geo_bounds_extend( &entry->bounds, a_record->latitude, a_record->longitude );
    }
}

static void apply_record( history_record_t *a_record )
{
    if( HISTORY_RECORD_CHARACTERIZATION == a_record->kind )
    {
        apply_characterization( a_record );
    }
    else
    {
        apply_location( a_record );
    }
    a_record->applied = true;
}

/* ------------------------------------------------------------------ formatting */

static int append_string( char *a_line, size_t a_capacity, int a_length, const char *a_text )
{
    if( a_length < 0 || (size_t)a_length >= a_capacity )
    {
        return -1;
    }
    const int written = media_journal_json_string(
        &a_line[ a_length ], a_capacity - (size_t)a_length, a_text, strlen( a_text ) );
    return written < 0 ? -1 : a_length + written;
}

static int format_identity( char *a_line,
                            size_t a_capacity,
                            const char *a_type,
                            int64_t a_utc,
                            const uint8_t a_bssid[ 6 ],
                            const char *a_ssid,
                            int32_t a_auth_mode )
{
    int length = media_journal_printf( a_line, a_capacity, 0, "{\"type\":\"%s\"", a_type );
    if( 0 != a_utc )
    {
        length =
            media_journal_printf( a_line, a_capacity, length, ",\"utc\":%lld", (long long)a_utc );
    }
    length = media_journal_printf( a_line,
                                   a_capacity,
                                   length,
                                   ",\"bssid\":\"%02X%02X%02X%02X%02X%02X\",\"ssid\":",
                                   a_bssid[ 0 ],
                                   a_bssid[ 1 ],
                                   a_bssid[ 2 ],
                                   a_bssid[ 3 ],
                                   a_bssid[ 4 ],
                                   a_bssid[ 5 ] );
    length = append_string( a_line, a_capacity, length, a_ssid );
    return media_journal_printf(
        a_line, a_capacity, length, ",\"auth_mode\":%ld", (long)a_auth_mode );
}

static int format_network( char *a_line,
                           size_t a_capacity,
                           int a_length,
                           const hotspot_history_network_t *a_network,
                           const char *a_public_ip,
                           const char *a_colo )
{
    int length = media_journal_printf( a_line,
                                       a_capacity,
                                       a_length,
                                       ",\"gateway\":%lu,\"netmask\":%lu,\"dns\":[%lu,%lu]",
                                       (unsigned long)a_network->gateway,
                                       (unsigned long)a_network->netmask,
                                       (unsigned long)a_network->dns_primary,
                                       (unsigned long)a_network->dns_backup );
    if( a_network->gateway_mac_valid )
    {
        const uint8_t *mac = a_network->gateway_mac;
        length = media_journal_printf( a_line,
                                       a_capacity,
                                       length,
                                       ",\"gateway_mac\":\"%02X%02X%02X%02X%02X%02X\"",
                                       mac[ 0 ],
                                       mac[ 1 ],
                                       mac[ 2 ],
                                       mac[ 3 ],
                                       mac[ 4 ],
                                       mac[ 5 ] );
    }
    if( '\0' != a_public_ip[ 0 ] )
    {
        length = media_journal_printf( a_line, a_capacity, length, ",\"public_ip\":" );
        length = append_string( a_line, a_capacity, length, a_public_ip );
        length = media_journal_printf( a_line, a_capacity, length, ",\"colo\":" );
        length = append_string( a_line, a_capacity, length, a_colo );
    }
    return length;
}

static const char *status_name( hotspot_status_t a_status )
{
    return HOTSPOT_STATUS_GREEN == a_status ? "green" : "yellow";
}

static int format_record( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    const history_record_t *record = &( (const history_record_t *)a_context )[ a_index ];
    const bool location = HISTORY_RECORD_LOCATION == record->kind;
    int length = format_identity( a_line,
                                  a_capacity,
                                  location ? "location" : "characterization",
                                  record->utc,
                                  record->bssid,
                                  record->ssid,
                                  record->auth_mode );
    length = media_journal_printf( a_line, a_capacity, length, ",\"rssi\":%d", (int)record->rssi );
    if( !location )
    {
        length = media_journal_printf(
            a_line, a_capacity, length, ",\"status\":\"%s\"", status_name( record->status ) );
        length = format_network(
            a_line, a_capacity, length, &record->network, record->public_ip, record->colo );
    }
    if( record->location_valid )
    {
        length = media_journal_printf( a_line,
                                       a_capacity,
                                       length,
                                       ",\"lat\":%.7f,\"lon\":%.7f",
                                       record->latitude,
                                       record->longitude );
    }
    return media_journal_printf( a_line, a_capacity, length, "}" );
}

/** A compacted base holds one summary per BSSID; its range is still computable offline. */
static int format_summary( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    const hotspot_history_entry_t *entry =
        &( (const hotspot_history_entry_t *)a_context )[ a_index ];
    int length = format_identity( a_line,
                                  a_capacity,
                                  "summary",
                                  entry->validated_utc,
                                  entry->bssid,
                                  entry->ssid,
                                  entry->auth_mode );
    length = media_journal_printf( a_line,
                                   a_capacity,
                                   length,
                                   ",\"rssi\":%d,\"status\":\"%s\",\"successes\":%lu",
                                   (int)entry->rssi,
                                   status_name( entry->status ),
                                   (unsigned long)entry->successes );
    length = format_network(
        a_line, a_capacity, length, &entry->network, entry->public_ip, entry->colo );
    if( entry->bounds.valid )
    {
        length = media_journal_printf( a_line,
                                       a_capacity,
                                       length,
                                       ",\"min_lat\":%.7f,\"max_lat\":%.7f,\"min_lon\":%.7f,"
                                       "\"max_lon\":%.7f,\"samples\":%lu",
                                       entry->bounds.min_latitude,
                                       entry->bounds.max_latitude,
                                       entry->bounds.min_longitude,
                                       entry->bounds.max_longitude,
                                       (unsigned long)entry->bounds.samples );
    }
    return media_journal_printf( a_line, a_capacity, length, "}" );
}

/* ------------------------------------------------------------------ replay */

static bool parse_mac( const char *a_text, uint8_t a_mac[ 6 ] )
{
    unsigned value[ 6 ];
    if( NULL == a_text || 12U != strlen( a_text ) ||
        6 != sscanf( a_text,
                     "%2x%2x%2x%2x%2x%2x",
                     &value[ 0 ],
                     &value[ 1 ],
                     &value[ 2 ],
                     &value[ 3 ],
                     &value[ 4 ],
                     &value[ 5 ] ) )
    {
        return false;
    }
    for( size_t index = 0U; index < 6U; ++index )
    {
        a_mac[ index ] = (uint8_t)value[ index ];
    }
    return true;
}

static const char *json_text( const cJSON *a_root, const char *a_key )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_root, a_key );
    return cJSON_IsString( item ) ? item->valuestring : NULL;
}

static bool json_number( const cJSON *a_root, const char *a_key, double *a_value )
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive( a_root, a_key );
    if( !cJSON_IsNumber( item ) )
    {
        return false;
    }
    *a_value = item->valuedouble;
    return true;
}

static void parse_network( const cJSON *a_root, history_record_t *a_record )
{
    double value = 0.0;
    a_record->network.gateway = json_number( a_root, "gateway", &value ) ? (uint32_t)value : 0U;
    a_record->network.netmask = json_number( a_root, "netmask", &value ) ? (uint32_t)value : 0U;
    const cJSON *dns = cJSON_GetObjectItemCaseSensitive( a_root, "dns" );
    const cJSON *primary = cJSON_GetArrayItem( dns, 0 );
    const cJSON *backup = cJSON_GetArrayItem( dns, 1 );
    a_record->network.dns_primary = cJSON_IsNumber( primary ) ? (uint32_t)primary->valuedouble : 0U;
    a_record->network.dns_backup = cJSON_IsNumber( backup ) ? (uint32_t)backup->valuedouble : 0U;
    a_record->network.gateway_mac_valid =
        parse_mac( json_text( a_root, "gateway_mac" ), a_record->network.gateway_mac );
    const char *public_ip = json_text( a_root, "public_ip" );
    const char *colo = json_text( a_root, "colo" );
    snprintf( a_record->public_ip,
              sizeof( a_record->public_ip ),
              "%s",
              NULL != public_ip ? public_ip : "" );
    snprintf( a_record->colo, sizeof( a_record->colo ), "%s", NULL != colo ? colo : "" );
}

/** Replace an entry's epoch with a compacted summary. */
static void apply_summary( const cJSON *a_root, const history_record_t *a_record )
{
    hotspot_history_entry_t *entry = find_or_create( a_record->bssid );
    if( NULL == entry )
    {
        return;
    }
    begin_epoch( entry, a_record->ssid, a_record->auth_mode );
    entry->network = a_record->network;
    memcpy( entry->public_ip, a_record->public_ip, sizeof( entry->public_ip ) );
    memcpy( entry->colo, a_record->colo, sizeof( entry->colo ) );
    entry->status = a_record->status;
    entry->validated_utc = a_record->utc;
    entry->rssi = a_record->rssi;
    double value = 0.0;
    entry->successes =
        json_number( a_root, "successes", &value ) && value >= 1.0 ? (uint32_t)value : 1U;
    double min_lat = 0.0;
    double max_lat = 0.0;
    double min_lon = 0.0;
    double max_lon = 0.0;
    if( json_number( a_root, "min_lat", &min_lat ) && json_number( a_root, "max_lat", &max_lat ) &&
        json_number( a_root, "min_lon", &min_lon ) && json_number( a_root, "max_lon", &max_lon ) )
    {
        entry->bounds = ( geo_bounds_t ){
            .valid = true,
            .min_latitude = min_lat,
            .max_latitude = max_lat,
            .min_longitude = min_lon,
            .max_longitude = max_lon,
            .samples = json_number( a_root, "samples", &value ) ? (uint32_t)value : 1U,
        };
    }
}

static bool replay_line( const char *a_line, size_t a_length )
{
    cJSON *root = cJSON_ParseWithLength( a_line, a_length );
    const char *type = json_text( root, "type" );
    const char *ssid = json_text( root, "ssid" );
    const char *status = json_text( root, "status" );
    history_record_t record = { 0 };
    double value = 0.0;
    bool valid = NULL != type && NULL != ssid && strlen( ssid ) < sizeof( record.ssid ) &&
                 parse_mac( json_text( root, "bssid" ), record.bssid ) &&
                 json_number( root, "auth_mode", &value );
    if( valid )
    {
        snprintf( record.ssid, sizeof( record.ssid ), "%s", ssid );
        record.auth_mode = (int32_t)value;
        record.utc = json_number( root, "utc", &value ) ? (int64_t)value : 0;
        record.rssi = json_number( root, "rssi", &value ) ? (int8_t)value : 0;
        record.status = NULL != status && 0 == strcmp( status, "green" ) ? HOTSPOT_STATUS_GREEN
                                                                         : HOTSPOT_STATUS_YELLOW;
        double latitude = 0.0;
        double longitude = 0.0;
        record.location_valid =
            json_number( root, "lat", &latitude ) && json_number( root, "lon", &longitude );
        record.latitude = latitude;
        record.longitude = longitude;
        parse_network( root, &record );
        if( 0 == strcmp( type, "characterization" ) )
        {
            record.kind = HISTORY_RECORD_CHARACTERIZATION;
            apply_record( &record );
        }
        else if( 0 == strcmp( type, "location" ) )
        {
            record.kind = HISTORY_RECORD_LOCATION;
            apply_record( &record );
        }
        else if( 0 == strcmp( type, "summary" ) )
        {
            apply_summary( root, &record );
        }
        else
        {
            valid = false;
        }
    }
    cJSON_Delete( root );
    return valid;
}

static void finish_replay( void )
{
    media_journal_read_end( &reader );
    if( 0U != replay_malformed )
    {
        ESP_LOGW( TAG, "skipped %u malformed history lines", (unsigned)replay_malformed );
    }
    ESP_LOGI( TAG, "history ready: %u BSSIDs", (unsigned)entry_count );
    state = HISTORY_STATE_READY;
    if( replay_total_bytes > HISTORY_COMPACT_BYTES )
    {
        compaction_requested = true;
    }
}

static void replay_step( void )
{
    if( 0U == replay_read_bytes && 0U == replay_total_bytes )
    {
        /* First step: resolve the journal; missing media means an empty, degraded history. */
        if( ESP_OK != media_journal_size( &journal, &replay_total_bytes ) ||
            ESP_OK != media_journal_read_begin( &journal, &reader ) )
        {
            degraded = true;
            replay_total_bytes = 0U;
            finish_replay();
            return;
        }
        if( 0U == replay_total_bytes )
        {
            finish_replay();
            return;
        }
    }
    for( size_t line = 0U; line < HISTORY_REPLAY_LINES_PER_POLL; ++line )
    {
        size_t length = 0U;
        const esp_err_t result = media_journal_read_line(
            &reader, line_buffer, RFSCOUT_MEDIA_JOURNAL_LINE_MAX, &length );
        if( ESP_ERR_NOT_FOUND == result )
        {
            finish_replay();
            return;
        }
        if( ESP_OK != result )
        {
            ++replay_malformed;
            if( ESP_ERR_INVALID_SIZE != result )
            {
                degraded = true;
                finish_replay();
                return;
            }
            continue;
        }
        replay_read_bytes += length + 1U;
        if( !replay_line( line_buffer, length ) )
        {
            ++replay_malformed;
        }
    }
}

/* ------------------------------------------------------------------ writing */

static void queue_record( history_record_t *a_record )
{
    /* During compaction the index is being written out; apply afterwards to avoid double counts. */
    if( HISTORY_STATE_COMPACTING != state )
    {
        apply_record( a_record );
    }
    if( !grow( (void **)&pending, &pending_capacity, pending_count + 1U, sizeof( *pending ) ) )
    {
        ESP_LOGW( TAG, "history record dropped: no memory" );
        degraded = true;
        return;
    }
    pending[ pending_count++ ] = *a_record;
}

static void flush( void )
{
    if( 0U == pending_count )
    {
        return;
    }
    const esp_err_t result =
        media_journal_append( &journal, format_record, pending, pending_count );
    if( ESP_OK == result || media_journal_outcome_uncertain( &journal ) )
    {
        /* An uncertain batch is dropped rather than duplicated in the next segment. */
        pending_count = 0U;
    }
    degraded = ESP_OK != result;
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "history flush failed: %s", esp_err_to_name( result ) );
    }
}

static void finish_compaction( bool a_committed )
{
    (void)a_committed; /* Host fixtures compile ESP_LOGI away. */
    state = HISTORY_STATE_READY;
    for( size_t index = 0U; index < pending_count; ++index )
    {
        if( !pending[ index ].applied )
        {
            apply_record( &pending[ index ] );
        }
    }
    ESP_LOGI( TAG, "history compaction %s", a_committed ? "committed" : "abandoned" );
}

static void compaction_step( void )
{
    if( HISTORY_STATE_READY == state )
    {
        /* Start only from a fully flushed journal, so the base supersedes every raw record. */
        if( 0U != pending_count || ESP_OK != media_journal_compact_begin( &journal ) )
        {
            return;
        }
        compaction_requested = false;
        compact_cursor = 0U;
        state = HISTORY_STATE_COMPACTING;
        return;
    }
    const size_t remaining = entry_count - compact_cursor;
    const size_t chunk =
        remaining < HISTORY_COMPACT_ENTRIES_PER_POLL ? remaining : HISTORY_COMPACT_ENTRIES_PER_POLL;
    if( 0U != chunk )
    {
        if( ESP_OK != media_journal_compact_write(
                          &journal, format_summary, &entries[ compact_cursor ], chunk ) )
        {
            finish_compaction( false );
            return;
        }
        compact_cursor += chunk;
        return;
    }
    const esp_err_t result = media_journal_compact_commit( &journal );
    degraded = ESP_OK != result;
    finish_compaction( ESP_OK == result );
}

/* ------------------------------------------------------------------ public API */

esp_err_t hotspot_history_open( void )
{
    hotspot_history_close();
    memset( &reader, 0, sizeof( reader ) );
    line_buffer = app_session_calloc( RFSCOUT_MEDIA_JOURNAL_LINE_MAX, 1U );
    rfscout_media_collection_t collection;
    esp_err_t result = NULL == line_buffer
                           ? ESP_ERR_NO_MEM
                           : media_app_collection( HISTORY_COLLECTION, &collection );
    if( ESP_OK == result )
    {
        result = media_journal_init(
            &journal, &collection, HISTORY_PREFIX, HISTORY_SEGMENT_BYTES, HISTORY_MAX_SEGMENTS );
    }
    journal_bound = ESP_OK == result;
    degraded = !journal_bound;
    state = journal_bound ? HISTORY_STATE_REPLAYING : HISTORY_STATE_READY;
    return result;
}

void hotspot_history_close( void )
{
    if( journal_bound )
    {
        if( HISTORY_STATE_REPLAYING == state )
        {
            media_journal_read_end( &reader );
        }
        if( HISTORY_STATE_COMPACTING == state )
        {
            media_journal_compact_abort( &journal );
            finish_compaction( false );
        }
        flush();
    }
    journal_bound = false;
    state = HISTORY_STATE_CLOSED;
    entries = NULL;
    entry_count = 0U;
    entry_capacity = 0U;
    pending = NULL;
    pending_count = 0U;
    pending_capacity = 0U;
    line_buffer = NULL;
    degraded = false;
    compaction_requested = false;
    compact_cursor = 0U;
    replay_total_bytes = 0U;
    replay_read_bytes = 0U;
    replay_malformed = 0U;
    next_flush_us = 0;
}

void hotspot_history_poll( int64_t a_now_us )
{
    if( !journal_bound )
    {
        return;
    }
    switch( state )
    {
        case HISTORY_STATE_REPLAYING:
            replay_step();
            break;
        case HISTORY_STATE_COMPACTING:
            compaction_step();
            break;
        case HISTORY_STATE_READY:
            if( a_now_us >= next_flush_us )
            {
                next_flush_us = a_now_us + HISTORY_FLUSH_INTERVAL_US;
                flush();
                if( compaction_requested )
                {
                    compaction_step();
                }
            }
            break;
        default:
            break;
    }
}

bool hotspot_history_ready( void )
{
    return HISTORY_STATE_READY == state || HISTORY_STATE_COMPACTING == state;
}

uint8_t hotspot_history_progress( void )
{
    if( hotspot_history_ready() || 0U == replay_total_bytes )
    {
        return hotspot_history_ready() ? 100U : 0U;
    }
    const uint64_t percent = replay_read_bytes * 100U / replay_total_bytes;
    return percent > 99U ? 99U : (uint8_t)percent;
}

bool hotspot_history_degraded( void )
{
    return degraded;
}

size_t hotspot_history_skipped( void )
{
    return replay_malformed;
}

void hotspot_history_request_compaction( void )
{
    compaction_requested = journal_bound;
}

bool hotspot_history_compacting( void )
{
    return HISTORY_STATE_COMPACTING == state || compaction_requested;
}

size_t hotspot_history_count( void )
{
    return entry_count;
}

const hotspot_history_entry_t *hotspot_history_at( size_t a_index )
{
    return a_index < entry_count ? &entries[ a_index ] : NULL;
}

const hotspot_history_entry_t *hotspot_history_find( const rfscout_wifi_station_bss_t *a_bss )
{
    if( NULL == a_bss )
    {
        return NULL;
    }
    const hotspot_history_entry_t *entry = find_bssid( a_bss->bssid );
    return NULL != entry && 0U != entry->successes && 0 == strcmp( entry->ssid, a_bss->ssid ) &&
                   entry->auth_mode == a_bss->auth_mode
               ? entry
               : NULL;
}

static void fill_identity( history_record_t *a_record,
                           history_record_kind_t a_kind,
                           const rfscout_wifi_station_bss_t *a_bss,
                           const rfscout_gps_snapshot_t *a_gps )
{
    memset( a_record, 0, sizeof( *a_record ) );
    a_record->kind = a_kind;
    memcpy( a_record->bssid, a_bss->bssid, sizeof( a_record->bssid ) );
    snprintf( a_record->ssid, sizeof( a_record->ssid ), "%s", a_bss->ssid );
    a_record->auth_mode = a_bss->auth_mode;
    a_record->rssi = a_bss->rssi;
    a_record->utc =
        NULL != a_gps && a_gps->time_valid ? (int64_t)a_gps->utc_epoch : (int64_t)time( NULL );
    if( a_record->utc < 1700000000LL )
    {
        a_record->utc = 0; /* the device clock has not been set */
    }
    if( gps_location_fresh( a_gps ) )
    {
        a_record->location_valid = true;
        a_record->latitude = a_gps->latitude_deg;
        a_record->longitude = a_gps->longitude_deg;
    }
}

void hotspot_history_record_result( const rfscout_wifi_station_bss_t *a_bss,
                                    const hotspot_result_t *a_result,
                                    const rfscout_gps_snapshot_t *a_gps )
{
    if( !journal_bound || NULL == a_bss || NULL == a_result ||
        ( HOTSPOT_STATUS_GREEN != a_result->outcome &&
          HOTSPOT_STATUS_YELLOW != a_result->outcome ) )
    {
        return;
    }
    history_record_t record;
    fill_identity( &record, HISTORY_RECORD_CHARACTERIZATION, a_bss, a_gps );
    record.rssi = a_result->start_rssi;
    record.status = a_result->outcome;
    record.network = ( hotspot_history_network_t ){
        .gateway = a_result->gateway,
        .netmask = a_result->netmask,
        .dns_primary = a_result->dns_primary,
        .dns_backup = a_result->dns_backup,
        .gateway_mac_valid = a_result->gateway_mac_valid,
    };
    memcpy(
        record.network.gateway_mac, a_result->gateway_mac, sizeof( record.network.gateway_mac ) );
    memcpy( record.public_ip, a_result->public_ip, sizeof( record.public_ip ) );
    memcpy( record.colo, a_result->colo, sizeof( record.colo ) );
    queue_record( &record );
}

void hotspot_history_observe( const rfscout_wifi_station_bss_t *a_bss,
                              const rfscout_gps_snapshot_t *a_gps )
{
    if( !journal_bound || NULL == a_bss || !gps_location_fresh( a_gps ) )
    {
        return;
    }
    hotspot_history_entry_t *entry = find_bssid( a_bss->bssid );
    if( NULL == entry || 0U == entry->successes || 0 != strcmp( entry->ssid, a_bss->ssid ) ||
        entry->auth_mode != a_bss->auth_mode ||
        !geo_movement_gate_accept( &entry->gate,
                                   a_gps->latitude_deg,
                                   a_gps->longitude_deg,
                                   device_config_get()->gps_movement_m ) )
    {
        return;
    }
    history_record_t record;
    fill_identity( &record, HISTORY_RECORD_LOCATION, a_bss, a_gps );
    queue_record( &record );
}
