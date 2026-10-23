/**
    apps/hotspots/hotspots_ui.c : Hotspots screens and navigation
    Description: Builds grouped view models from the live table and durable history and routes
                 rotary input. With app lock on, landing-level rotary switches between views.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspots_ui.h"

#include "app_descriptor.h"
#include "app_manager.h"
#include "app_session.h"
#include "event.h"
#include "geo.h"
#include "gps.h"
#include "hotspot_groups.h"
#include "hotspot_history.h"
#include "hotspots_internal.h"
#include "hotspots_view.h"
#include "network_credentials.h"
#include "wifi_station.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UI_LINE_MAX 72U
#define UI_LABEL_MAX ( RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 8U )
#define UI_VALUE_MAX 16U
#define UI_FOOTER_MAX 64U
#define UI_BSSID_TEXT_MAX 18U

typedef enum
{
    VIEW_LIVE = 0,
    VIEW_HISTORY,
    VIEW_SAVED,
    VIEW_COUNT
} hotspots_ui_view_t;

typedef enum
{
    SCREEN_LANDING = 0,
    SCREEN_MENU,
    SCREEN_SETTINGS,
    SCREEN_MEMBERS,
    SCREEN_DETAIL,
    SCREEN_CREDENTIALS
} hotspots_ui_screen_t;

typedef enum
{
    MENU_BACK = 0,
    MENU_LOCK,
    MENU_AUTO,
    MENU_SETTINGS,
    MENU_FIXED
} menu_item_t;

typedef enum
{
    SETTING_BACK = 0,
    SETTING_MATCH,
    SETTING_COMPACT,
    SETTING_COUNT
} setting_item_t;

typedef enum
{
    ACTION_BACK = 0,
    ACTION_RECHECK,
    ACTION_PASSWORD,
    ACTION_JOIN,
    ACTION_ADD
} detail_action_t;

static const char *const VIEW_TITLES[ VIEW_COUNT ] = {
    "HOTSPOTS LIVE",
    "HOTSPOTS HISTORY",
    "HOTSPOTS SAVED",
};

static const char *TAG = "hotspots";

/* View-model scratch in app-session memory, sized for the larger of the table and history. */
static struct
{
    size_t capacity;
    hotspot_group_input_t *inputs;
    size_t *preference;
    size_t *order;
    hotspot_group_t *groups;
    size_t *sorted;
    size_t *rank;
    characterization_group_member_t *members;
    characterization_group_cluster_t *clusters;
    double *distance; /**< metres from the current fix to the member's center; < 0 unknown */
    double *bearing;
    hotspots_view_row_t *rows;
    char ( *labels )[ UI_LABEL_MAX ];
    char ( *values )[ UI_VALUE_MAX ];
} scratch;

static size_t model_count;
static size_t group_count;

static hotspots_ui_view_t view;
static hotspots_ui_screen_t screen;
static hotspots_ui_screen_t credentials_return;
static unsigned menu_selected;
static unsigned settings_selected;
static bool settings_editing;
static char settings_error[ 32 ];
static unsigned members_selected;
static unsigned detail_selected;
/* Selections follow identities while lists re-sort: a member BSSID, or a profile ID. */
static bool group_selected;
static uint8_t group_bssid[ 6 ];
static bool member_selected;
static uint8_t member_bssid[ 6 ];
static uint32_t profile_selected;
/** Add-password from a Live detail queues a check with the new profile once saved. */
static bool check_after_save;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_HOTSPOTS, a_value );
}

static unsigned step_index( unsigned a_index, unsigned a_count, int a_direction )
{
    if( 0U == a_count )
    {
        return 0U;
    }
    return a_direction > 0 ? ( a_index + 1U ) % a_count : ( a_index + a_count - 1U ) % a_count;
}

/* ---- Formatting -------------------------------------------------------------------------- */

static void format_bssid( const uint8_t a_mac[ 6 ], char *a_output, size_t a_capacity )
{
    snprintf( a_output,
              a_capacity,
              "%02X:%02X:%02X:%02X:%02X:%02X",
              a_mac[ 0 ],
              a_mac[ 1 ],
              a_mac[ 2 ],
              a_mac[ 3 ],
              a_mac[ 4 ],
              a_mac[ 5 ] );
}

static void format_ipv4( uint32_t a_address, char *a_output, size_t a_capacity )
{
    if( 0U == a_address )
    {
        snprintf( a_output, a_capacity, "--" );
        return;
    }
    /* lwIP stores IPv4 addresses in network byte order; on ESP32 the first octet is the low byte. */
    snprintf( a_output,
              a_capacity,
              "%u.%u.%u.%u",
              (unsigned)( a_address & 0xffU ),
              (unsigned)( ( a_address >> 8U ) & 0xffU ),
              (unsigned)( ( a_address >> 16U ) & 0xffU ),
              (unsigned)( ( a_address >> 24U ) & 0xffU ) );
}

static void format_distance( double a_meters, double a_bearing, char *a_output, size_t a_capacity )
{
    if( a_meters < 0.0 )
    {
        snprintf( a_output, a_capacity, "--" );
        return;
    }
    char distance[ UI_VALUE_MAX ];
    geo_format_distance( a_meters, distance, sizeof( distance ) );
    snprintf( a_output, a_capacity, "%s %s", distance, geo_compass_label( a_bearing ) );
}

static void
format_egress( const char *a_public_ip, const char *a_colo, char *a_output, size_t a_capacity )
{
    if( '\0' == a_public_ip[ 0 ] )
    {
        snprintf( a_output, a_capacity, "Public --" );
        return;
    }
    snprintf( a_output,
              a_capacity,
              "Public %.45s (%.15s)",
              a_public_ip,
              '\0' != a_colo[ 0 ] ? a_colo : "?" );
}

static void format_label( const char *a_ssid,
                          const hotspot_group_t *a_group,
                          char *a_output,
                          size_t a_capacity )
{
    if( NULL != a_group && a_group->networks > 1U )
    {
        snprintf( a_output, a_capacity, "%.32s #%u", a_ssid, (unsigned)a_group->network );
    }
    else
    {
        snprintf( a_output, a_capacity, "%.32s", a_ssid );
    }
}

static void profile_label( uint32_t a_profile_id, char *a_output, size_t a_capacity )
{
    size_t count = 0U;
    const saved_network_profile_t *profiles = hotspots_profiles( &count );
    snprintf( a_output, a_capacity, "--" );
    for( size_t index = 0U; index < count; ++index )
    {
        if( profiles[ index ].profile_id != a_profile_id )
        {
            continue;
        }
        bool shared = false;
        for( size_t other = 0U; other < count; ++other )
        {
            shared = shared || ( other != index &&
                                 0 == strcmp( profiles[ other ].ssid, profiles[ index ].ssid ) );
        }
        network_credentials_label(
            profiles[ index ].ssid, profiles[ index ].ordinal, shared, a_output, a_capacity );
        return;
    }
}

/* ---- Scratch ----------------------------------------------------------------------------- */

static void *grow_buffer( void *a_buffer, size_t a_capacity, size_t a_size )
{
    return NULL == a_buffer ? app_session_calloc( a_capacity, a_size )
                            : app_session_resize( a_buffer, a_capacity, a_size );
}

#define GROW_FIELD( a_field )                                                                      \
    do                                                                                             \
    {                                                                                              \
        void *grown = grow_buffer( scratch.a_field, capacity, sizeof( *scratch.a_field ) );        \
        if( NULL == grown )                                                                        \
        {                                                                                          \
            return false;                                                                          \
        }                                                                                          \
        scratch.a_field = grown;                                                                   \
    } while( 0 )

static bool ensure_scratch( size_t a_count )
{
    if( a_count <= scratch.capacity )
    {
        return true;
    }
    size_t capacity = scratch.capacity > 0U ? scratch.capacity : 16U;
    while( capacity < a_count )
    {
        capacity *= 2U;
    }
    GROW_FIELD( inputs );
    GROW_FIELD( preference );
    GROW_FIELD( order );
    GROW_FIELD( groups );
    GROW_FIELD( sorted );
    GROW_FIELD( rank );
    GROW_FIELD( members );
    GROW_FIELD( clusters );
    GROW_FIELD( distance );
    GROW_FIELD( bearing );
    GROW_FIELD( rows );
    GROW_FIELD( labels );
    GROW_FIELD( values );
    scratch.capacity = capacity;
    return true;
}

static bool build_groups( void )
{
    const hotspot_group_scratch_t group_scratch = {
        .sorted = scratch.sorted,
        .rank = scratch.rank,
        .members = scratch.members,
        .clusters = scratch.clusters,
    };
    group_count = hotspot_groups_build( scratch.inputs,
                                        model_count,
                                        scratch.preference,
                                        scratch.order,
                                        scratch.groups,
                                        &group_scratch );
    return group_count > 0U;
}

/* ---- Live model -------------------------------------------------------------------------- */

static const char *entry_tag( const hotspot_entry_t *a_entry, hotspots_view_tone_t *a_tone )
{
    const hotspot_stage_t stage = hotspots_attempt_stage( a_entry );
    if( HOTSPOT_STAGE_NONE != stage )
    {
        *a_tone = HOTSPOTS_TONE_ACTIVE;
        return HOTSPOT_STAGE_JOIN == stage ? "JOIN" : "HTTPS";
    }
    if( a_entry->manual_requested )
    {
        *a_tone = HOTSPOTS_TONE_ACTIVE;
        return "Q";
    }
    switch( a_entry->status )
    {
        case HOTSPOT_STATUS_GREEN:
            *a_tone = HOTSPOTS_TONE_GOOD;
            return "OK";
        case HOTSPOT_STATUS_YELLOW:
            *a_tone = HOTSPOTS_TONE_LIMITED;
            return "LTD";
        case HOTSPOT_STATUS_RED:
            *a_tone = HOTSPOTS_TONE_BAD;
            return "FAIL";
        case HOTSPOT_STATUS_UNCHECKED:
        default:
            break;
    }
    *a_tone = HOTSPOTS_TONE_NEUTRAL;
    switch( a_entry->credential )
    {
        case HOTSPOT_CREDENTIAL_NONE:
            return "KEY?";
        case HOTSPOT_CREDENTIAL_AMBIGUOUS:
        case HOTSPOT_CREDENTIAL_UNMATCHED:
            return "PICK";
        case HOTSPOT_CREDENTIAL_REJECTED:
            return "DENY";
        case HOTSPOT_CREDENTIAL_UNSUPPORTED:
            return "N/A";
        default:
            break;
    }
    if( hotspots_auto_enabled() && a_entry->visible && !hotspot_auto_gate_open( a_entry ) )
    {
        return "<-80";
    }
    return "-";
}

/** Higher values summarize a network row: activity first, then the best result. */
static unsigned entry_priority( const hotspot_entry_t *a_entry )
{
    if( HOTSPOT_STAGE_NONE != hotspots_attempt_stage( a_entry ) )
    {
        return 6U;
    }
    if( a_entry->manual_requested )
    {
        return 5U;
    }
    switch( a_entry->status )
    {
        case HOTSPOT_STATUS_GREEN:
            return 4U;
        case HOTSPOT_STATUS_YELLOW:
            return 3U;
        case HOTSPOT_STATUS_RED:
            return 2U;
        default:
            return 1U;
    }
}

static void evidence_from_network( const hotspot_history_network_t *a_network,
                                   characterization_network_evidence_t *a_evidence )
{
    a_evidence->subnet_valid = 0U != a_network->gateway && 0U != a_network->netmask;
    a_evidence->gateway = a_network->gateway;
    a_evidence->netmask = a_network->netmask;
    a_evidence->gateway_mac_valid = a_network->gateway_mac_valid;
    memcpy( a_evidence->gateway_mac, a_network->gateway_mac, sizeof( a_evidence->gateway_mac ) );
}

static void member_location( const geo_bounds_t *a_bounds,
                             characterization_group_member_t *a_member )
{
    a_member->location_valid =
        geo_bounds_center( a_bounds, &a_member->latitude_deg, &a_member->longitude_deg );
}

static bool build_live( void )
{
    hotspot_table_t *table = hotspots_table();
    model_count = 0U;
    group_count = 0U;
    if( 0U == table->count || !ensure_scratch( table->count ) )
    {
        return false;
    }
    model_count = hotspot_table_order( table, scratch.preference, scratch.capacity );
    for( size_t index = 0U; index < model_count; ++index )
    {
        const hotspot_entry_t *entry = &table->entries[ index ];
        const hotspot_history_entry_t *history = hotspot_history_find( &entry->bss );
        hotspot_group_input_t *input = &scratch.inputs[ index ];
        memset( input, 0, sizeof( *input ) );
        input->ssid = entry->bss.ssid;
        input->auth_mode = entry->bss.auth_mode;
        /* A BSSID that refused every same-SSID passphrase is not those profiles' network. */
        input->partition = HOTSPOT_CREDENTIAL_REJECTED == entry->credential ? 1U : 0U;
        if( entry->has_success )
        {
            const hotspot_result_t *success = &entry->success;
            input->member.evidence = ( characterization_network_evidence_t ){
                .subnet_valid = 0U != success->gateway && 0U != success->netmask,
                .gateway = success->gateway,
                .netmask = success->netmask,
                .gateway_mac_valid = success->gateway_mac_valid,
            };
            memcpy( input->member.evidence.gateway_mac, success->gateway_mac, 6U );
        }
        else if( NULL != history )
        {
            evidence_from_network( &history->network, &input->member.evidence );
        }
        if( NULL != history )
        {
            member_location( &history->bounds, &input->member );
        }
    }
    return build_groups();
}

static void live_group_row( size_t a_group, hotspots_view_row_t *a_row )
{
    const hotspot_table_t *table = hotspots_table();
    const hotspot_group_t *group = &scratch.groups[ a_group ];
    const hotspot_entry_t *summary = &table->entries[ scratch.order[ group->first ] ];
    const hotspot_entry_t *strongest = NULL;
    for( size_t member = 0U; member < group->count; ++member )
    {
        const hotspot_entry_t *entry = &table->entries[ scratch.order[ group->first + member ] ];
        if( entry_priority( entry ) > entry_priority( summary ) )
        {
            summary = entry;
        }
        if( entry->visible && ( NULL == strongest || entry->bss.rssi > strongest->bss.rssi ) )
        {
            strongest = entry;
        }
    }
    format_label( summary->bss.ssid, group, scratch.labels[ a_group ], UI_LABEL_MAX );
    if( NULL != strongest )
    {
        snprintf( scratch.values[ a_group ], UI_VALUE_MAX, "%d", (int)strongest->bss.rssi );
    }
    else
    {
        snprintf( scratch.values[ a_group ], UI_VALUE_MAX, "--" );
    }
    *a_row = ( hotspots_view_row_t ){
        .label = scratch.labels[ a_group ],
        .value = scratch.values[ a_group ],
        .members = (uint16_t)group->count,
        .visible = NULL != strongest,
    };
    a_row->tag = entry_tag( summary, &a_row->tone );
}

/* ---- History model ----------------------------------------------------------------------- */

static int compare_history( const void *a_left, const void *a_right )
{
    const size_t left = *(const size_t *)a_left;
    const size_t right = *(const size_t *)a_right;
    /* Nearest first when the current fix gives a distance, then green first, newest first. */
    const bool left_known = scratch.distance[ left ] >= 0.0;
    const bool right_known = scratch.distance[ right ] >= 0.0;
    if( left_known != right_known )
    {
        return left_known ? -1 : 1;
    }
    if( left_known && scratch.distance[ left ] != scratch.distance[ right ] )
    {
        return scratch.distance[ left ] < scratch.distance[ right ] ? -1 : 1;
    }
    const hotspot_history_entry_t *a = hotspot_history_at( left );
    const hotspot_history_entry_t *b = hotspot_history_at( right );
    if( a->status != b->status )
    {
        return HOTSPOT_STATUS_GREEN == a->status ? -1 : 1;
    }
    if( a->validated_utc != b->validated_utc )
    {
        return a->validated_utc > b->validated_utc ? -1 : 1;
    }
    return left < right ? -1 : ( left > right ? 1 : 0 );
}

static bool build_history( void )
{
    model_count = 0U;
    group_count = 0U;
    const size_t count = hotspot_history_count();
    if( 0U == count || !ensure_scratch( count ) )
    {
        return false;
    }
    rfscout_gps_snapshot_t gps = { 0 };
    const bool fix = gps_snapshot_get( &gps ) && gps_location_fresh( &gps );
    for( size_t index = 0U; index < count; ++index )
    {
        const hotspot_history_entry_t *history = hotspot_history_at( index );
        hotspot_group_input_t *input = &scratch.inputs[ index ];
        memset( input, 0, sizeof( *input ) );
        input->ssid = history->ssid;
        input->auth_mode = history->auth_mode;
        evidence_from_network( &history->network, &input->member.evidence );
        member_location( &history->bounds, &input->member );
        scratch.distance[ index ] = -1.0;
        scratch.bearing[ index ] = 0.0;
        if( fix && input->member.location_valid )
        {
            scratch.distance[ index ] = geo_distance_m( gps.latitude_deg,
                                                        gps.longitude_deg,
                                                        input->member.latitude_deg,
                                                        input->member.longitude_deg );
            scratch.bearing[ index ] = geo_bearing_deg( gps.latitude_deg,
                                                        gps.longitude_deg,
                                                        input->member.latitude_deg,
                                                        input->member.longitude_deg );
        }
        scratch.preference[ index ] = index;
    }
    model_count = count;
    qsort( scratch.preference, count, sizeof( scratch.preference[ 0 ] ), compare_history );
    return build_groups();
}

static const char *history_tag( const hotspot_history_entry_t *a_entry,
                                hotspots_view_tone_t *a_tone )
{
    const bool green = HOTSPOT_STATUS_GREEN == a_entry->status;
    *a_tone = green ? HOTSPOTS_TONE_GOOD : HOTSPOTS_TONE_LIMITED;
    return green ? "OK" : "LTD";
}

static void history_group_row( size_t a_group, hotspots_view_row_t *a_row )
{
    const hotspot_group_t *group = &scratch.groups[ a_group ];
    const size_t nearest = scratch.order[ group->first ];
    const hotspot_history_entry_t *summary = hotspot_history_at( nearest );
    for( size_t member = 0U; member < group->count; ++member )
    {
        const hotspot_history_entry_t *entry =
            hotspot_history_at( scratch.order[ group->first + member ] );
        if( HOTSPOT_STATUS_GREEN == entry->status )
        {
            summary = entry;
            break;
        }
    }
    format_label( summary->ssid, group, scratch.labels[ a_group ], UI_LABEL_MAX );
    format_distance( scratch.distance[ nearest ],
                     scratch.bearing[ nearest ],
                     scratch.values[ a_group ],
                     UI_VALUE_MAX );
    *a_row = ( hotspots_view_row_t ){
        .label = scratch.labels[ a_group ],
        .value = scratch.values[ a_group ],
        .members = (uint16_t)group->count,
        .visible = true,
    };
    a_row->tag = history_tag( summary, &a_row->tone );
}

static const hotspot_history_entry_t *history_by_bssid( const uint8_t a_bssid[ 6 ],
                                                        size_t *a_index )
{
    for( size_t index = 0U; index < model_count; ++index )
    {
        const hotspot_history_entry_t *entry = hotspot_history_at( index );
        if( NULL != entry && 0 == memcmp( entry->bssid, a_bssid, 6U ) )
        {
            *a_index = index;
            return entry;
        }
    }
    return NULL;
}

/* ---- Shared model access ----------------------------------------------------------------- */

static bool build_model( void )
{
    return VIEW_LIVE == view ? build_live() : build_history();
}

static const uint8_t *model_bssid( size_t a_index )
{
    return VIEW_LIVE == view ? hotspots_table()->entries[ a_index ].bss.bssid
                             : hotspot_history_at( a_index )->bssid;
}

/** @brief Group containing a BSSID, or group_count when it is gone. */
static size_t find_group( const uint8_t a_bssid[ 6 ] )
{
    for( size_t group = 0U; group < group_count; ++group )
    {
        for( size_t member = 0U; member < scratch.groups[ group ].count; ++member )
        {
            if( 0 == memcmp( model_bssid( scratch.order[ scratch.groups[ group ].first + member ] ),
                             a_bssid,
                             6U ) )
            {
                return group;
            }
        }
    }
    return group_count;
}

static size_t build_group_rows( void )
{
    for( size_t group = 0U; group < group_count; ++group )
    {
        if( VIEW_LIVE == view )
        {
            live_group_row( group, &scratch.rows[ group ] );
        }
        else
        {
            history_group_row( group, &scratch.rows[ group ] );
        }
    }
    return group_count;
}

/** @brief Member rows of one group; labels hold BSSIDs. */
static size_t build_member_rows( size_t a_group )
{
    const hotspot_group_t *group = &scratch.groups[ a_group ];
    for( size_t member = 0U; member < group->count; ++member )
    {
        const size_t index = scratch.order[ group->first + member ];
        hotspots_view_row_t *row = &scratch.rows[ member ];
        format_bssid( model_bssid( index ), scratch.labels[ member ], UI_LABEL_MAX );
        *row = ( hotspots_view_row_t ){ .label = scratch.labels[ member ],
                                        .value = scratch.values[ member ] };
        if( VIEW_LIVE == view )
        {
            const hotspot_entry_t *entry = &hotspots_table()->entries[ index ];
            row->tag = entry_tag( entry, &row->tone );
            row->visible = entry->visible;
            if( entry->visible )
            {
                snprintf( scratch.values[ member ], UI_VALUE_MAX, "%d", (int)entry->bss.rssi );
            }
            else
            {
                snprintf( scratch.values[ member ], UI_VALUE_MAX, "--" );
            }
        }
        else
        {
            row->tag = history_tag( hotspot_history_at( index ), &row->tone );
            row->visible = true;
            format_distance( scratch.distance[ index ],
                             scratch.bearing[ index ],
                             scratch.values[ member ],
                             UI_VALUE_MAX );
        }
    }
    return group->count;
}

/* ---- Saved model ------------------------------------------------------------------------- */

static size_t build_saved_rows( void )
{
    size_t count = 0U;
    const saved_network_profile_t *profiles = hotspots_profiles( &count );
    if( 0U == count || !ensure_scratch( count ) )
    {
        return 0U;
    }
    for( size_t index = 0U; index < count; ++index )
    {
        const saved_network_profile_t *profile = &profiles[ index ];
        uint16_t associated = 0U;
        for( size_t bssid = 0U; bssid < profile->bssid_count; ++bssid )
        {
            associated +=
                SAVED_NETWORK_BSSID_ASSOCIATED == profile->bssids[ bssid ].state ? 1U : 0U;
        }
        profile_label( profile->profile_id, scratch.labels[ index ], UI_LABEL_MAX );
        snprintf( scratch.values[ index ],
                  UI_VALUE_MAX,
                  "%s",
                  SAVED_NETWORK_AUTH_UNKNOWN == profile->auth_mode
                      ? "--"
                      : rfscout_wifi_station_auth_label( profile->auth_mode ) );
        scratch.rows[ index ] = ( hotspots_view_row_t ){
            .label = scratch.labels[ index ],
            .tag = profile->has_password ? "" : "OPEN",
            .tone = HOTSPOTS_TONE_NEUTRAL,
            .value = scratch.values[ index ],
            .members = associated,
            .visible = true,
        };
    }
    return count;
}

/* ---- Footers ----------------------------------------------------------------------------- */

static void format_footer( char *a_footer, size_t a_capacity )
{
    if( VIEW_LIVE == view )
    {
        hotspots_format_status( a_footer, a_capacity );
        return;
    }
    if( VIEW_SAVED == view )
    {
        size_t count = 0U;
        (void)hotspots_profiles( &count );
        snprintf( a_footer,
                  a_capacity,
                  "SAVED %u/%u  SHARED WITH HOME",
                  (unsigned)count,
                  (unsigned)SAVED_NETWORK_MAX_COUNT );
        return;
    }
    rfscout_gps_snapshot_t gps = { 0 };
    const bool fix = gps_snapshot_get( &gps ) && gps_location_fresh( &gps );
    if( !hotspot_history_ready() )
    {
        snprintf( a_footer, a_capacity, "LOAD %u%%", (unsigned)hotspot_history_progress() );
        return;
    }
    snprintf( a_footer,
              a_capacity,
              "%s%u BSSIDS  GPS %s",
              hotspot_history_compacting() ? "COMPACT " : "",
              (unsigned)hotspot_history_count(),
              fix ? "FIX" : "--" );
    if( 0U != hotspot_history_skipped() )
    {
        char skipped[ 20 ];
        snprintf( skipped, sizeof( skipped ), "  %u BAD", (unsigned)hotspot_history_skipped() );
        strlcat( a_footer, skipped, a_capacity );
    }
    if( hotspot_history_degraded() )
    {
        strlcat( a_footer, "  SD!", a_capacity );
    }
}

static const char *history_empty_text( void )
{
    if( !hotspot_history_ready() )
    {
        return "Loading history...";
    }
    return hotspot_history_degraded() ? "SD history unavailable" : "No known hotspots";
}

/* ---- Detail ------------------------------------------------------------------------------ */

static void describe_status( hotspot_status_t a_status, char *a_line, size_t a_capacity )
{
    switch( a_status )
    {
        case HOTSPOT_STATUS_GREEN:
            snprintf( a_line, a_capacity, "OK - Internet reachable" );
            break;
        case HOTSPOT_STATUS_YELLOW:
            snprintf( a_line, a_capacity, "LIMITED - joined, Internet not verified" );
            break;
        case HOTSPOT_STATUS_RED:
            snprintf( a_line, a_capacity, "FAILED - could not join" );
            break;
        case HOTSPOT_STATUS_UNCHECKED:
        default:
            snprintf( a_line, a_capacity, "Not checked" );
            break;
    }
}

static void describe_last_attempt( const hotspot_entry_t *a_entry, char *a_line, size_t a_capacity )
{
    if( !a_entry->has_result )
    {
        snprintf( a_line, a_capacity, "Last check: none" );
        return;
    }
    const hotspot_result_t *last = &a_entry->last;
    switch( last->outcome )
    {
        case HOTSPOT_STATUS_GREEN:
            snprintf( a_line, a_capacity, "Last: time ready, HTTPS 200" );
            break;
        case HOTSPOT_STATUS_YELLOW:
            if( HOTSPOT_STAGE_NTP == last->failed_stage )
            {
                snprintf( a_line, a_capacity, "Last: NTP failed, HTTPS skipped" );
            }
            else if( last->http_status > 0 && ESP_FAIL == last->error )
            {
                snprintf( a_line, a_capacity, "Last: time ready, HTTPS %d", last->http_status );
            }
            else if( ESP_OK != last->error )
            {
                snprintf( a_line,
                          a_capacity,
                          "Last: HTTPS %s",
                          esp_err_to_name( last->error ) );
            }
            else
            {
                snprintf( a_line, a_capacity, "Last: HTTPS no response" );
            }
            break;
        default:
            if( rfscout_wifi_station_reason_auth_rejected( last->disconnect_reason ) )
            {
                snprintf( a_line, a_capacity, "Last: password rejected" );
            }
            else if( 0 != last->disconnect_reason )
            {
                snprintf( a_line,
                          a_capacity,
                          "Last: join failed (reason %d)",
                          (int)last->disconnect_reason );
            }
            else
            {
                snprintf(
                    a_line, a_capacity, "Last: join failed (%s)", esp_err_to_name( last->error ) );
            }
            break;
    }
}

static void describe_credential( const hotspot_entry_t *a_entry, char *a_line, size_t a_capacity )
{
    char label[ UI_LABEL_MAX ];
    switch( a_entry->credential )
    {
        case HOTSPOT_CREDENTIAL_OPEN:
            snprintf( a_line, a_capacity, "No password needed" );
            break;
        case HOTSPOT_CREDENTIAL_KNOWN:
            profile_label( a_entry->profile_id, label, sizeof( label ) );
            snprintf( a_line, a_capacity, "Key %.36s (known)", label );
            break;
        case HOTSPOT_CREDENTIAL_INFERRED:
            profile_label( a_entry->profile_id, label, sizeof( label ) );
            snprintf( a_line, a_capacity, "Key %.36s (nearby)", label );
            break;
        case HOTSPOT_CREDENTIAL_AMBIGUOUS:
            snprintf( a_line, a_capacity, "Several profiles match: Join to pick" );
            break;
        case HOTSPOT_CREDENTIAL_UNMATCHED:
            snprintf( a_line, a_capacity, "Profiles not proven here: Join to pick" );
            break;
        case HOTSPOT_CREDENTIAL_REJECTED:
            snprintf( a_line, a_capacity, "Saved passwords were rejected" );
            break;
        case HOTSPOT_CREDENTIAL_UNSUPPORTED:
            snprintf( a_line, a_capacity, "Security mode not supported" );
            break;
        case HOTSPOT_CREDENTIAL_NONE:
        default:
            snprintf( a_line, a_capacity, "No saved password" );
            break;
    }
}

/** @brief Actions offered for one live BSSID; render and input share this list. */
static size_t
live_actions( const hotspot_entry_t *a_entry, ui_menu_row_t *a_rows, detail_action_t *a_ids )
{
    const bool idle = a_entry->visible && HOTSPOT_STAGE_NONE == hotspots_attempt_stage( a_entry );
    size_t count = 0U;
    a_ids[ count ] = ACTION_BACK;
    a_rows[ count++ ] = ( ui_menu_row_t ){ .label = "Back", .state = { .available = true } };

    if( HOTSPOT_CREDENTIAL_UNSUPPORTED == a_entry->credential )
    {
        return count;
    }
    const bool secured = RFSCOUT_WIFI_STATION_SECURITY_SECURED == a_entry->bss.security;
    const bool needs_pick = HOTSPOT_CREDENTIAL_AMBIGUOUS == a_entry->credential ||
                            HOTSPOT_CREDENTIAL_UNMATCHED == a_entry->credential ||
                            HOTSPOT_CREDENTIAL_REJECTED == a_entry->credential;
    if( secured && needs_pick )
    {
        a_ids[ count ] = ACTION_JOIN;
        a_rows[ count++ ] =
            ( ui_menu_row_t ){ .label = "Join with profile", .state = { .available = idle } };
    }
    else if( !secured || HOTSPOT_CREDENTIAL_NONE != a_entry->credential )
    {
        a_ids[ count ] = ACTION_RECHECK;
        a_rows[ count++ ] = ( ui_menu_row_t ){
            .label = "Recheck",
            .value = a_entry->manual_requested ? "Queued" : ( a_entry->visible ? NULL : "Gone" ),
            .state = { .available = idle && hotspot_entry_checkable( a_entry ) },
        };
    }
    if( secured && ( HOTSPOT_CREDENTIAL_KNOWN == a_entry->credential ||
                     HOTSPOT_CREDENTIAL_INFERRED == a_entry->credential ) )
    {
        a_ids[ count ] = ACTION_PASSWORD;
        a_rows[ count++ ] =
            ( ui_menu_row_t ){ .label = "Password", .state = { .available = true } };
    }
    else if( secured )
    {
        a_ids[ count ] = ACTION_ADD;
        a_rows[ count++ ] =
            ( ui_menu_row_t ){ .label = "Add password", .state = { .available = true } };
    }
    return count;
}

static void render_live_detail( const hotspot_entry_t *a_entry, const char *a_label )
{
    static char lines[ HOTSPOTS_VIEW_DETAIL_LINES ][ UI_LINE_MAX ];
    char bssid[ UI_BSSID_TEXT_MAX ];
    format_bssid( a_entry->bss.bssid, bssid, sizeof( bssid ) );
    snprintf( lines[ 0 ], UI_LINE_MAX, "%s", a_label );
    snprintf( lines[ 1 ], UI_LINE_MAX, "BSSID %s  CH %u", bssid, (unsigned)a_entry->bss.channel );
    if( a_entry->visible )
    {
        snprintf( lines[ 2 ],
                  UI_LINE_MAX,
                  "RSSI %d dBm  %s  checks %u",
                  (int)a_entry->bss.rssi,
                  rfscout_wifi_station_auth_label( a_entry->bss.auth_mode ),
                  (unsigned)a_entry->attempts );
    }
    else
    {
        snprintf( lines[ 2 ],
                  UI_LINE_MAX,
                  "Not visible (missed %u)  %s",
                  (unsigned)a_entry->missed_scans,
                  rfscout_wifi_station_auth_label( a_entry->bss.auth_mode ) );
    }
    describe_credential( a_entry, lines[ 3 ], UI_LINE_MAX );

    hotspots_view_tone_t tone = HOTSPOTS_TONE_NEUTRAL;
    const char *tag = entry_tag( a_entry, &tone );
    if( HOTSPOT_STAGE_NONE != hotspots_attempt_stage( a_entry ) )
    {
        snprintf( lines[ 4 ], UI_LINE_MAX, "Checking: %s", tag );
    }
    else if( HOTSPOT_STATUS_UNCHECKED == a_entry->status && a_entry->manual_requested )
    {
        snprintf( lines[ 4 ], UI_LINE_MAX, "Queued for check" );
    }
    else
    {
        describe_status( a_entry->status, lines[ 4 ], UI_LINE_MAX );
    }
    describe_last_attempt( a_entry, lines[ 5 ], UI_LINE_MAX );

    /* Show validated network details even when the latest recheck failed. */
    const hotspot_result_t *evidence = a_entry->has_success ? &a_entry->success : &a_entry->last;
    char client[ 20 ];
    char gateway[ 20 ];
    char netmask[ 20 ];
    char dns_primary[ 20 ];
    char dns_backup[ 20 ];
    char gateway_mac[ UI_BSSID_TEXT_MAX ] = "--";
    format_ipv4( evidence->address, client, sizeof( client ) );
    format_ipv4( evidence->gateway, gateway, sizeof( gateway ) );
    format_ipv4( evidence->netmask, netmask, sizeof( netmask ) );
    format_ipv4( evidence->dns_primary, dns_primary, sizeof( dns_primary ) );
    format_ipv4( evidence->dns_backup, dns_backup, sizeof( dns_backup ) );
    if( evidence->gateway_mac_valid )
    {
        format_bssid( evidence->gateway_mac, gateway_mac, sizeof( gateway_mac ) );
    }
    snprintf( lines[ 6 ], UI_LINE_MAX, "IP %s  GW %s", client, gateway );
    snprintf( lines[ 7 ], UI_LINE_MAX, "Mask %s  MAC %s", netmask, gateway_mac );
    snprintf( lines[ 8 ], UI_LINE_MAX, "DNS %s / %s", dns_primary, dns_backup );
    format_egress( evidence->public_ip, evidence->colo, lines[ 9 ], UI_LINE_MAX );

    hotspots_view_detail_t detail = {
        .line_count = HOTSPOTS_VIEW_DETAIL_LINES,
        .status_line = 4U,
        .status_tone = tone,
    };
    for( size_t line = 0U; line < HOTSPOTS_VIEW_DETAIL_LINES; ++line )
    {
        detail.lines[ line ] = lines[ line ];
    }
    detail_action_t ids[ HOTSPOTS_VIEW_DETAIL_ACTIONS ];
    detail.action_count = live_actions( a_entry, detail.actions, ids );
    if( detail_selected >= detail.action_count )
    {
        detail_selected = 0U;
    }
    hotspots_view_detail( VIEW_TITLES[ view ], &detail, detail_selected );
}

static void
render_history_detail( const hotspot_history_entry_t *a_entry, size_t a_index, const char *a_label )
{
    static char lines[ HOTSPOTS_VIEW_DETAIL_LINES ][ UI_LINE_MAX ];
    char bssid[ UI_BSSID_TEXT_MAX ];
    format_bssid( a_entry->bssid, bssid, sizeof( bssid ) );
    snprintf( lines[ 0 ], UI_LINE_MAX, "%s", a_label );
    snprintf( lines[ 1 ],
              UI_LINE_MAX,
              "BSSID %s  %s",
              bssid,
              rfscout_wifi_station_auth_label( a_entry->auth_mode ) );
    describe_status( a_entry->status, lines[ 2 ], UI_LINE_MAX );
    if( 0 != a_entry->validated_utc )
    {
        const time_t utc = (time_t)a_entry->validated_utc;
        struct tm parts;
        gmtime_r( &utc, &parts );
        char when[ 24 ];
        strftime( when, sizeof( when ), "%Y-%m-%d %H:%M", &parts );
        snprintf(
            lines[ 3 ], UI_LINE_MAX, "Validated %s UTC  x%u", when, (unsigned)a_entry->successes );
    }
    else
    {
        snprintf( lines[ 3 ], UI_LINE_MAX, "Validated --  x%u", (unsigned)a_entry->successes );
    }
    snprintf( lines[ 4 ], UI_LINE_MAX, "RSSI %d dBm at last success", (int)a_entry->rssi );

    char gateway[ 20 ];
    char netmask[ 20 ];
    char dns_primary[ 20 ];
    char dns_backup[ 20 ];
    char gateway_mac[ UI_BSSID_TEXT_MAX ] = "--";
    format_ipv4( a_entry->network.gateway, gateway, sizeof( gateway ) );
    format_ipv4( a_entry->network.netmask, netmask, sizeof( netmask ) );
    format_ipv4( a_entry->network.dns_primary, dns_primary, sizeof( dns_primary ) );
    format_ipv4( a_entry->network.dns_backup, dns_backup, sizeof( dns_backup ) );
    if( a_entry->network.gateway_mac_valid )
    {
        format_bssid( a_entry->network.gateway_mac, gateway_mac, sizeof( gateway_mac ) );
    }
    snprintf( lines[ 5 ], UI_LINE_MAX, "GW %s  Mask %s", gateway, netmask );
    snprintf( lines[ 6 ], UI_LINE_MAX, "Gateway MAC %s", gateway_mac );
    snprintf( lines[ 7 ], UI_LINE_MAX, "DNS %s / %s", dns_primary, dns_backup );
    format_egress( a_entry->public_ip, a_entry->colo, lines[ 8 ], UI_LINE_MAX );

    double latitude = 0.0;
    double longitude = 0.0;
    if( geo_bounds_center( &a_entry->bounds, &latitude, &longitude ) )
    {
        char distance[ UI_VALUE_MAX ];
        format_distance(
            scratch.distance[ a_index ], scratch.bearing[ a_index ], distance, sizeof( distance ) );
        snprintf( lines[ 9 ], UI_LINE_MAX, "Center %.5f,%.5f  %s", latitude, longitude, distance );
    }
    else
    {
        snprintf( lines[ 9 ], UI_LINE_MAX, "Location --" );
    }

    hotspots_view_detail_t detail = {
        .line_count = HOTSPOTS_VIEW_DETAIL_LINES,
        .status_line = 2U,
        .action_count = 1U,
    };
    (void)history_tag( a_entry, &detail.status_tone );
    for( size_t line = 0U; line < HOTSPOTS_VIEW_DETAIL_LINES; ++line )
    {
        detail.lines[ line ] = lines[ line ];
    }
    detail.actions[ 0 ] = ( ui_menu_row_t ){ .label = "Back", .state = { .available = true } };
    detail_selected = 0U;
    hotspots_view_detail( VIEW_TITLES[ view ], &detail, detail_selected );
}

/* ---- Rendering --------------------------------------------------------------------------- */

static void menu_fixed_rows( ui_menu_row_t a_rows[ MENU_FIXED ] )
{
    a_rows[ MENU_BACK ] = ( ui_menu_row_t ){ .label = "Back", .state = { .available = true } };
    a_rows[ MENU_LOCK ] = ( ui_menu_row_t ){ .label = "App lock",
                                             .value = app_manager_locked() ? "On" : "Off",
                                             .state = { .available = true } };
    a_rows[ MENU_AUTO ] = ( ui_menu_row_t ){ .label = "Auto check",
                                             .value = hotspots_auto_enabled() ? "On" : "Off",
                                             .state = { .available = true } };
    a_rows[ MENU_SETTINGS ] =
        ( ui_menu_row_t ){ .label = "Settings", .state = { .available = true } };
}

/** @brief Re-attach the menu selection to its network or profile after the list changed. */
static void resolve_menu_selection( size_t a_row_count )
{
    if( VIEW_SAVED == view && 0U != profile_selected )
    {
        size_t count = 0U;
        const saved_network_profile_t *profiles = hotspots_profiles( &count );
        for( size_t index = 0U; index < count; ++index )
        {
            if( profiles[ index ].profile_id == profile_selected )
            {
                menu_selected = MENU_FIXED + (unsigned)index;
                return;
            }
        }
        profile_selected = 0U;
    }
    else if( VIEW_SAVED != view && group_selected )
    {
        const size_t group = find_group( group_bssid );
        if( group < group_count )
        {
            menu_selected = MENU_FIXED + (unsigned)group;
            return;
        }
        group_selected = false;
    }
    if( menu_selected >= MENU_FIXED + a_row_count )
    {
        menu_selected = (unsigned)( MENU_FIXED + a_row_count ) - 1U;
    }
}

static void capture_menu_selection( void )
{
    group_selected = false;
    profile_selected = 0U;
    if( menu_selected < MENU_FIXED )
    {
        return;
    }
    const size_t row = menu_selected - MENU_FIXED;
    if( VIEW_SAVED == view )
    {
        size_t count = 0U;
        const saved_network_profile_t *profiles = hotspots_profiles( &count );
        profile_selected = row < count ? profiles[ row ].profile_id : 0U;
        return;
    }
    if( row < group_count )
    {
        memcpy( group_bssid, model_bssid( scratch.order[ scratch.groups[ row ].first ] ), 6U );
        group_selected = true;
    }
}

static void render_settings( char *a_footer )
{
    const uint16_t distance = hotspots_match_distance_m();
    char match[ UI_VALUE_MAX ];
    if( 0U == distance )
    {
        snprintf( match, sizeof( match ), "Off" );
    }
    else
    {
        snprintf( match, sizeof( match ), "%um", (unsigned)distance );
    }
    const bool compact_available = hotspot_history_ready() && !hotspot_history_compacting();
    const ui_menu_row_t rows[ SETTING_COUNT ] = {
        [SETTING_BACK] = { .label = "Back", .state = { .available = true } },
        [SETTING_MATCH] = { .label = "Match distance",
                            .value = match,
                            .state = { .available = gps_detected(), .editing = settings_editing } },
        [SETTING_COMPACT] = { .label = "Compact history",
                              .value = hotspot_history_compacting() ? "Busy" : NULL,
                              .state = { .available = compact_available } },
    };
    if( '\0' != settings_error[ 0 ] )
    {
        snprintf( a_footer, UI_FOOTER_MAX, "%s", settings_error );
    }
    else if( SETTING_MATCH == settings_selected && !gps_detected() )
    {
        snprintf( a_footer, UI_FOOTER_MAX, "Match distance needs GPS" );
    }
    hotspots_view_menu(
        VIEW_TITLES[ view ], rows, SETTING_COUNT, NULL, 0U, settings_selected, a_footer );
}

static void render_detail( void )
{
    if( !member_selected )
    {
        screen = SCREEN_MEMBERS;
        return;
    }
    const size_t group = find_group( member_bssid );
    char label[ UI_LABEL_MAX ];
    if( VIEW_LIVE == view )
    {
        const hotspot_entry_t *entry = hotspot_table_find( hotspots_table(), member_bssid );
        if( NULL != entry )
        {
            format_label( entry->bss.ssid,
                          group < group_count ? &scratch.groups[ group ] : NULL,
                          label,
                          sizeof( label ) );
            render_live_detail( entry, label );
            return;
        }
    }
    else
    {
        size_t index = 0U;
        const hotspot_history_entry_t *entry = history_by_bssid( member_bssid, &index );
        if( NULL != entry )
        {
            format_label( entry->ssid,
                          group < group_count ? &scratch.groups[ group ] : NULL,
                          label,
                          sizeof( label ) );
            render_history_detail( entry, index, label );
            return;
        }
    }
    /* The BSSID's observation session ended or its history was replaced. */
    member_selected = false;
    screen = SCREEN_MEMBERS;
}

static void render_members( char *a_footer )
{
    const size_t group = group_selected ? find_group( group_bssid ) : group_count;
    if( group >= group_count )
    {
        group_selected = false;
        screen = SCREEN_MENU;
        return;
    }
    const size_t count = build_member_rows( group );
    if( member_selected )
    {
        bool found = false;
        for( size_t member = 0U; member < count; ++member )
        {
            if( 0 == memcmp( model_bssid( scratch.order[ scratch.groups[ group ].first + member ] ),
                             member_bssid,
                             6U ) )
            {
                members_selected = 1U + (unsigned)member;
                found = true;
                break;
            }
        }
        if( !found )
        {
            /* A disappeared or regrouped BSSID must not select a different row. */
            member_selected = false;
            members_selected = 0U;
        }
    }
    if( members_selected > count )
    {
        members_selected = 0U;
        member_selected = false;
    }
    /* The footer names the network, since the title keeps the view name. */
    char label[ UI_LABEL_MAX ];
    format_label(
        VIEW_LIVE == view
            ? hotspots_table()->entries[ scratch.order[ scratch.groups[ group ].first ] ].bss.ssid
            : hotspot_history_at( scratch.order[ scratch.groups[ group ].first ] )->ssid,
        &scratch.groups[ group ],
        label,
        sizeof( label ) );
    snprintf( a_footer, UI_FOOTER_MAX, "%.40s  %u BSSID", label, (unsigned)count );
    const ui_menu_row_t back = { .label = "Back", .state = { .available = true } };
    hotspots_view_menu(
        VIEW_TITLES[ view ], &back, 1U, scratch.rows, count, members_selected, a_footer );
}

void hotspots_ui_render( void )
{
    char footer[ UI_FOOTER_MAX ];
    format_footer( footer, sizeof( footer ) );

    if( SCREEN_CREDENTIALS == screen )
    {
        network_credentials_render( VIEW_TITLES[ view ] );
        return;
    }
    if( SCREEN_SETTINGS == screen )
    {
        render_settings( footer );
        return;
    }

    size_t row_count = 0U;
    if( VIEW_SAVED == view )
    {
        row_count = build_saved_rows();
        if( SCREEN_LANDING != screen && SCREEN_MENU != screen )
        {
            screen = SCREEN_MENU;
        }
    }
    else
    {
        (void)build_model();
        if( SCREEN_DETAIL == screen )
        {
            render_detail();
            if( SCREEN_DETAIL == screen )
            {
                return;
            }
        }
        if( SCREEN_MEMBERS == screen )
        {
            render_members( footer );
            if( SCREEN_MEMBERS == screen )
            {
                return;
            }
        }
        row_count = build_group_rows();
    }

    if( SCREEN_MENU == screen )
    {
        ui_menu_row_t fixed[ MENU_FIXED ];
        menu_fixed_rows( fixed );
        resolve_menu_selection( row_count );
        hotspots_view_menu( VIEW_TITLES[ view ],
                            fixed,
                            MENU_FIXED,
                            scratch.rows,
                            row_count,
                            menu_selected,
                            footer );
        return;
    }

    const char *empty = VIEW_LIVE == view    ? "No networks visible"
                        : VIEW_SAVED == view ? "No saved networks"
                                             : history_empty_text();
    hotspots_view_landing( VIEW_TITLES[ view ], scratch.rows, row_count, empty, footer );
}

/* ---- Input ------------------------------------------------------------------------------- */

static void open_credentials( void )
{
    credentials_return = screen;
    screen = SCREEN_CREDENTIALS;
    emit_event( RFSCOUT_EVENT_ENTER, 0 );
}

static void handle_credentials_input( const rfscout_input_event_t *a_input )
{
    network_credentials_outcome_t outcome = { 0 };
    if( !network_credentials_input( a_input, &outcome ) )
    {
        return;
    }
    screen = credentials_return;
    hotspots_credentials_changed( &outcome );
    hotspot_entry_t *entry =
        member_selected ? hotspot_table_find( hotspots_table(), member_bssid ) : NULL;
    const bool queue =
        SCREEN_DETAIL == credentials_return &&
        ( NETWORK_CREDENTIALS_RESULT_PICKED == outcome.result ||
          ( check_after_save && NETWORK_CREDENTIALS_RESULT_SAVED == outcome.result ) );
    if( queue && NULL != entry && !hotspots_request_check( entry, outcome.profile_id ) )
    {
        ESP_LOGW( TAG, "chosen profile could not be queued for this BSSID" );
    }
    if( NETWORK_CREDENTIALS_RESULT_FORGOTTEN == outcome.result )
    {
        profile_selected = 0U;
    }
    check_after_save = false;
    emit_event( RFSCOUT_EVENT_BACK, 0 );
}

static void handle_landing_input( const rfscout_input_event_t *a_input, bool *a_handled )
{
    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        screen = SCREEN_MENU;
        menu_selected = MENU_BACK;
        group_selected = false;
        profile_selected = 0U;
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return;
    }
    const bool rotate = RFSCOUT_INPUT_NAVIGATION == a_input->type && app_manager_locked();
    if( RFSCOUT_INPUT_LONG_PRESS == a_input->type || rotate )
    {
        const int direction =
            ( RFSCOUT_INPUT_NAVIGATION == a_input->type && a_input->value < 0 ) ? -1 : 1;
        view = (hotspots_ui_view_t)step_index( view, VIEW_COUNT, direction );
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        return;
    }
    *a_handled = false;
}

static void handle_menu_input( const rfscout_input_event_t *a_input )
{
    size_t rows = VIEW_SAVED == view ? 0U : group_count;
    if( VIEW_SAVED == view )
    {
        (void)hotspots_profiles( &rows );
    }
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        menu_selected = step_index( menu_selected, MENU_FIXED + (unsigned)rows, a_input->value );
        capture_menu_selection();
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    if( RFSCOUT_INPUT_LONG_PRESS == a_input->type )
    {
        screen = SCREEN_LANDING;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return;
    }
    switch( menu_selected )
    {
        case MENU_BACK:
            screen = SCREEN_LANDING;
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return;
        case MENU_LOCK:
        {
            const bool locked = app_manager_set_locked( !app_manager_locked() );
            ESP_LOGI( TAG, "application navigation %s", locked ? "locked" : "unlocked" );
            return;
        }
        case MENU_AUTO:
            hotspots_set_auto_enabled( !hotspots_auto_enabled() );
            emit_event( hotspots_auto_enabled() ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                        hotspots_auto_enabled() ? 1 : 0 );
            return;
        case MENU_SETTINGS:
            screen = SCREEN_SETTINGS;
            settings_selected = SETTING_BACK;
            settings_editing = false;
            settings_error[ 0 ] = '\0';
            emit_event( RFSCOUT_EVENT_ENTER, 0 );
            return;
        default:
            break;
    }
    capture_menu_selection();
    if( VIEW_SAVED == view )
    {
        if( 0U != profile_selected )
        {
            network_credentials_open_profile( profile_selected );
            open_credentials();
        }
        return;
    }
    if( group_selected )
    {
        screen = SCREEN_MEMBERS;
        members_selected = 0U;
        member_selected = false;
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
    }
}

static void handle_settings_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        if( settings_editing )
        {
            const esp_err_t result = hotspots_step_match_distance( a_input->value );
            snprintf( settings_error,
                      sizeof( settings_error ),
                      "%s",
                      ESP_OK == result ? "" : "Not saved: storage error" );
        }
        else
        {
            settings_selected = step_index( settings_selected, SETTING_COUNT, a_input->value );
        }
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    const bool back =
        RFSCOUT_INPUT_LONG_PRESS == a_input->type ||
        ( RFSCOUT_INPUT_SELECT == a_input->type && SETTING_BACK == settings_selected );
    if( back )
    {
        settings_editing = false;
        screen = SCREEN_MENU;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return;
    }
    if( SETTING_MATCH == settings_selected && gps_detected() )
    {
        settings_editing = !settings_editing;
        emit_event( RFSCOUT_EVENT_ACTIVATE, settings_editing ? 1 : 0 );
    }
    else if( SETTING_COMPACT == settings_selected && hotspot_history_ready() &&
             !hotspot_history_compacting() )
    {
        hotspot_history_request_compaction();
        emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
    }
}

static void handle_members_input( const rfscout_input_event_t *a_input )
{
    const size_t group = group_selected ? find_group( group_bssid ) : group_count;
    const size_t count = group < group_count ? scratch.groups[ group ].count : 0U;
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        members_selected = step_index( members_selected, 1U + (unsigned)count, a_input->value );
        member_selected = members_selected > 0U && members_selected <= count;
        if( member_selected )
        {
            memcpy( member_bssid,
                    model_bssid(
                        scratch.order[ scratch.groups[ group ].first + members_selected - 1U ] ),
                    6U );
        }
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    const bool back = RFSCOUT_INPUT_LONG_PRESS == a_input->type ||
                      ( RFSCOUT_INPUT_SELECT == a_input->type && 0U == members_selected );
    if( back )
    {
        member_selected = false;
        screen = SCREEN_MENU;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT == a_input->type && member_selected )
    {
        screen = SCREEN_DETAIL;
        detail_selected = 0U;
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
    }
}

static void handle_detail_input( const rfscout_input_event_t *a_input )
{
    hotspot_entry_t *entry =
        VIEW_LIVE == view ? hotspot_table_find( hotspots_table(), member_bssid ) : NULL;
    ui_menu_row_t rows[ HOTSPOTS_VIEW_DETAIL_ACTIONS ];
    detail_action_t ids[ HOTSPOTS_VIEW_DETAIL_ACTIONS ] = { ACTION_BACK };
    const size_t count = NULL != entry ? live_actions( entry, rows, ids ) : 1U;

    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        detail_selected = step_index( detail_selected, (unsigned)count, a_input->value );
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    if( detail_selected >= count )
    {
        detail_selected = 0U;
    }
    const bool back =
        RFSCOUT_INPUT_LONG_PRESS == a_input->type ||
        ( RFSCOUT_INPUT_SELECT == a_input->type && ACTION_BACK == ids[ detail_selected ] );
    if( back )
    {
        screen = SCREEN_MEMBERS;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type || NULL == entry ||
        !rows[ detail_selected ].state.available )
    {
        return;
    }
    switch( ids[ detail_selected ] )
    {
        case ACTION_RECHECK:
            if( hotspots_request_check( entry, 0U ) )
            {
                emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
            }
            break;
        case ACTION_PASSWORD:
            network_credentials_open_profile( entry->profile_id );
            open_credentials();
            break;
        case ACTION_JOIN:
            network_credentials_open_pick( entry->bss.ssid, entry->bss.auth_mode );
            open_credentials();
            break;
        case ACTION_ADD:
            check_after_save = entry->visible;
            network_credentials_open_add( entry->bss.ssid );
            open_credentials();
            break;
        default:
            break;
    }
}

bool hotspots_ui_input( const rfscout_input_event_t *a_input )
{
    if( NULL == a_input )
    {
        return false;
    }
    /* Inputs act on what is on screen now; history may have grown since the last render. */
    if( VIEW_SAVED != view && SCREEN_CREDENTIALS != screen )
    {
        (void)build_model();
    }
    bool handled = true;
    switch( screen )
    {
        case SCREEN_LANDING:
            handle_landing_input( a_input, &handled );
            break;
        case SCREEN_MENU:
            handle_menu_input( a_input );
            break;
        case SCREEN_SETTINGS:
            handle_settings_input( a_input );
            break;
        case SCREEN_MEMBERS:
            handle_members_input( a_input );
            break;
        case SCREEN_DETAIL:
            handle_detail_input( a_input );
            break;
        case SCREEN_CREDENTIALS:
            handle_credentials_input( a_input );
            break;
        default:
            screen = SCREEN_LANDING;
            break;
    }
    return handled;
}

void hotspots_ui_close( void )
{
    network_credentials_close();
}

void hotspots_ui_reset( void )
{
    memset( &scratch, 0, sizeof( scratch ) );
    model_count = 0U;
    group_count = 0U;
    view = VIEW_LIVE;
    screen = SCREEN_LANDING;
    credentials_return = SCREEN_LANDING;
    menu_selected = MENU_BACK;
    settings_selected = SETTING_BACK;
    settings_editing = false;
    settings_error[ 0 ] = '\0';
    members_selected = 0U;
    detail_selected = 0U;
    group_selected = false;
    member_selected = false;
    profile_selected = 0U;
    check_after_save = false;
}
