/**
    apps/hotspots/app.c : Hotspots application descriptor and runtime
    Description: Alternates shared Wi-Fi monitor sweeps with credential-aware connectivity checks
                 (join, NTP, verified HTTPS); screens live in hotspots_ui.c.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_session.h"
#include "connectivity_probe.h"
#include "gps.h"
#include "hotspot_history.h"
#include "hotspot_profiles.h"
#include "hotspot_table.h"
#include "hotspots_app.h"
#include "hotspots_internal.h"
#include "hotspots_ui.h"
#include "log_writer.h"
#include "radio_monitor.h"
#include "network_session.h"
#include "persistent_store.h"
#include "saved_networks.h"
#include "wifi_station.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include <stdio.h>
#include <string.h>

#define HOTSPOTS_CHECK_WINDOW_US 2800000LL
#define HOTSPOTS_HTTPS_TIMEOUT_MS 10000U
#define HOTSPOTS_HTTPS_GUARD_US 12000000LL
#define HOTSPOTS_SETTLE_US 300000LL
#define HOTSPOTS_MONITOR_SWEEP_US 3200000LL
#define HOTSPOTS_SCAN_RETRY_US 1000000LL
#define HOTSPOTS_AP_QUEUE_CAPACITY 64U
#define HOTSPOTS_AP_DRAIN_BATCH 16U
#define HOTSPOTS_RENDER_INTERVAL_US 500000LL
#define HOTSPOTS_SETTINGS_KEY "settings"
#define HOTSPOTS_SETTINGS_MAGIC 0x48535333U
#define HOTSPOTS_EVICTION_DISTANCE_MAX_STEPS 20U
#define HOTSPOTS_EVICTION_DISTANCE_DEFAULT_STEPS 5U

/** Distances (m) within which a profile's known BSSID makes a new BSSID a candidate; 0 is Off. */
static const uint16_t MATCH_DISTANCE_OPTIONS[] = { 0U, 50U, 100U, 200U, 500U };
#define MATCH_DISTANCE_DEFAULT_INDEX 2U
static const uint16_t RETENTION_SECONDS_OPTIONS[] = { 30U, 60U, 90U, 120U, 180U, 240U, 300U };
#define RETENTION_SECONDS_DEFAULT_INDEX 1U
#define RETENTION_SECONDS_OPTION_COUNT                                                             \
    ( sizeof( RETENTION_SECONDS_OPTIONS ) / sizeof( RETENTION_SECONDS_OPTIONS[ 0 ] ) )
#define MATCH_DISTANCE_OPTION_COUNT                                                                \
    ( sizeof( MATCH_DISTANCE_OPTIONS ) / sizeof( MATCH_DISTANCE_OPTIONS[ 0 ] ) )

typedef struct
{
    uint32_t magic;
    uint8_t match_distance_index;
    uint8_t retention_seconds_index;
    uint8_t eviction_distance_steps; /**< 0 disables; each step represents 100 metres. */
} hotspots_settings_t;

/** @brief Scheduler phase: alternating passive scan and check window, plus check stages. */
typedef enum
{
    HOTSPOTS_PHASE_IDLE = 0,
    HOTSPOTS_PHASE_SCAN_START,
    HOTSPOTS_PHASE_SCANNING,
    HOTSPOTS_PHASE_MONITOR_STOP,
    HOTSPOTS_PHASE_CHECK,
    HOTSPOTS_PHASE_NETWORK,
    HOTSPOTS_PHASE_HTTPS,
    HOTSPOTS_PHASE_CHECK_TEARDOWN,
    HOTSPOTS_PHASE_SETTLE
} hotspots_phase_t;

static const char *TAG = "hotspots";

/* Session-scoped state. Pointers reference app-session memory and are cleared on ENTER/LEAVE. */
static hotspot_table_t table;
/* Wi-Fi callbacks only copy bounded metadata into this queue. All policy, allocation and
   history work remains on the application task after the callback has returned. */
static rfscout_wifi_ap_observation_t ap_queue[ HOTSPOTS_AP_QUEUE_CAPACITY ];
static portMUX_TYPE ap_queue_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t ap_queue_count;
static uint32_t ap_queue_dropped;
static uint32_t observations_rejected;
static bool collecting_ap_observations;
/* Logical time advances only while receive-only monitoring is active; join/TLS is excluded. */
static uint32_t monitor_clock_ms;
static int64_t monitor_last_tick_us;
/* Non-secret saved-profile snapshot, refreshed after each completed scan. */
static saved_network_profile_t *profiles;
static size_t profile_count;

static hotspots_phase_t phase;
static int64_t phase_deadline_us;
static int64_t window_end_us;
static int64_t next_scan_us;
static rfscout_wifi_station_bss_t attempt_bss;
static hotspot_result_t attempt_result;
static size_t checks_completed;

static bool auto_enabled;
static hotspots_settings_t settings;
/** Profile used by the running secured attempt; learned outcomes are recorded against it. */
static uint32_t attempt_profile_id;
static bool render_due;
static int64_t last_render_us;

static void reset_session_state( void )
{
    memset( &table, 0, sizeof( table ) );
    portENTER_CRITICAL( &ap_queue_lock );
    ap_queue_count = 0U;
    ap_queue_dropped = 0U;
    portEXIT_CRITICAL( &ap_queue_lock );
    __atomic_store_n( &collecting_ap_observations, false, __ATOMIC_RELEASE );
    observations_rejected = 0U;
    monitor_clock_ms = 0U;
    monitor_last_tick_us = 0;
    profiles = NULL;
    profile_count = 0U;
    phase = HOTSPOTS_PHASE_IDLE;
    phase_deadline_us = 0;
    window_end_us = 0;
    next_scan_us = 0;
    memset( &attempt_bss, 0, sizeof( attempt_bss ) );
    memset( &attempt_result, 0, sizeof( attempt_result ) );
    checks_completed = 0U;
    auto_enabled = true;
    attempt_profile_id = 0U;
    render_due = true;
    last_render_us = 0;
}

/**
 * @brief Callback from the framework radio monitor; never allocates or touches app-session
 *        state outside this bounded copy. A later app-task poll owns all Hotspots mutations.
 */
static void observe_ap( const rfscout_wifi_ap_observation_t *a_ap )
{
    if( NULL == a_ap || 0U == a_ap->ssid_length ||
        !__atomic_load_n( &collecting_ap_observations, __ATOMIC_ACQUIRE ) )
    {
        return;
    }
    portENTER_CRITICAL( &ap_queue_lock );
    for( size_t index = 0U; index < ap_queue_count; ++index )
    {
        if( 0 == memcmp( ap_queue[ index ].bssid, a_ap->bssid, sizeof( a_ap->bssid ) ) )
        {
            /* Prefer valid security evidence over later ambiguous frames. */
            if( RFSCOUT_WIFI_AP_AUTH_UNKNOWN != a_ap->auth ||
                RFSCOUT_WIFI_AP_AUTH_UNKNOWN == ap_queue[ index ].auth )
            {
                ap_queue[ index ] = *a_ap;
            }
            portEXIT_CRITICAL( &ap_queue_lock );
            return;
        }
    }
    if( ap_queue_count < HOTSPOTS_AP_QUEUE_CAPACITY )
    {
        ap_queue[ ap_queue_count++ ] = *a_ap;
    }
    else
    {
        ++ap_queue_dropped;
    }
    portEXIT_CRITICAL( &ap_queue_lock );
}

/** @brief Read a callback-owned counter without racing the Wi-Fi receive task. */
static uint32_t ap_drops_snapshot( void )
{
    portENTER_CRITICAL( &ap_queue_lock );
    const uint32_t dropped = ap_queue_dropped;
    portEXIT_CRITICAL( &ap_queue_lock );
    return dropped;
}

static void tick_monitor_clock( int64_t a_now_us )
{
    if( a_now_us > monitor_last_tick_us && monitor_last_tick_us > 0 )
    {
        monitor_clock_ms += (uint32_t)( ( a_now_us - monitor_last_tick_us ) / 1000LL );
    }
    monitor_last_tick_us = a_now_us;
}

/** @brief Drain at most one bounded callback queue per application poll. */
static void drain_ap_queue( void )
{
    rfscout_gps_snapshot_t gps = { 0 };
    (void)gps_snapshot_get( &gps );

    for( unsigned batch = 0U; batch < ( HOTSPOTS_AP_QUEUE_CAPACITY / HOTSPOTS_AP_DRAIN_BATCH );
         ++batch )
    {
        rfscout_wifi_ap_observation_t ready[ HOTSPOTS_AP_DRAIN_BATCH ];
        portENTER_CRITICAL( &ap_queue_lock );
        const size_t count =
            ap_queue_count < HOTSPOTS_AP_DRAIN_BATCH ? ap_queue_count : HOTSPOTS_AP_DRAIN_BATCH;
        if( count > 0U )
        {
            memcpy( ready, ap_queue, count * sizeof( ready[ 0 ] ) );
            ap_queue_count -= count;
            if( ap_queue_count > 0U )
            {
                memmove( ap_queue, &ap_queue[ count ], ap_queue_count * sizeof( ap_queue[ 0 ] ) );
            }
        }
        portEXIT_CRITICAL( &ap_queue_lock );

        if( 0U == count )
        {
            break;
        }
        for( size_t index = 0U; index < count; ++index )
        {
            rfscout_wifi_station_bss_t bss = { 0 };
            if( !rfscout_wifi_station_bss_from_observation( &ready[ index ], &bss ) )
            {
                continue;
            }
            if( ESP_OK != hotspot_table_observe( &table, &bss, monitor_clock_ms, &gps ) )
            {
                ++observations_rejected;
            }
        }
    }
}

static bool attempt_running( void )
{
    return ( HOTSPOTS_PHASE_NETWORK == phase ) || ( HOTSPOTS_PHASE_HTTPS == phase );
}

hotspot_table_t *hotspots_table( void )
{
    return &table;
}

hotspot_stage_t hotspots_attempt_stage( const hotspot_entry_t *a_entry )
{
    if( NULL == a_entry || !attempt_running() ||
        0 != memcmp( a_entry->bss.bssid, attempt_bss.bssid, 6U ) )
    {
        return HOTSPOT_STAGE_NONE;
    }
    return HOTSPOTS_PHASE_NETWORK == phase ? HOTSPOT_STAGE_JOIN : HOTSPOT_STAGE_HTTPS;
}

bool hotspots_auto_enabled( void )
{
    return auto_enabled;
}

void hotspots_set_auto_enabled( bool a_enabled )
{
    auto_enabled = a_enabled;
    ESP_LOGI( TAG, "automatic checks %s", auto_enabled ? "enabled" : "disabled" );
    render_due = true;
}

bool hotspots_request_check( hotspot_entry_t *a_entry, uint32_t a_profile_id )
{
    if( NULL == a_entry || !a_entry->visible ||
        HOTSPOT_STAGE_NONE != hotspots_attempt_stage( a_entry ) )
    {
        return false;
    }
    a_entry->manual_profile_id = a_profile_id;
    a_entry->manual_requested = true;
    if( !hotspot_entry_checkable( a_entry ) )
    {
        a_entry->manual_requested = false;
        a_entry->manual_profile_id = 0U;
        return false;
    }
    render_due = true;
    return true;
}

static const char *outcome_name( hotspot_status_t a_status )
{
    switch( a_status )
    {
        case HOTSPOT_STATUS_GREEN:
            return "green";
        case HOTSPOT_STATUS_YELLOW:
            return "yellow";
        case HOTSPOT_STATUS_RED:
            return "red";
        case HOTSPOT_STATUS_UNCHECKED:
        default:
            return "unchecked";
    }
}

static const char *stage_name( hotspot_stage_t a_stage )
{
    switch( a_stage )
    {
        case HOTSPOT_STAGE_JOIN:
            return "join";
        case HOTSPOT_STAGE_NTP:
            return "ntp";
        case HOTSPOT_STAGE_HTTPS:
            return "https";
        case HOTSPOT_STAGE_NONE:
        default:
            return "none";
    }
}

void hotspots_format_status( char *a_footer, size_t a_capacity )
{
    if( attempt_running() )
    {
        snprintf( a_footer,
                  a_capacity,
                  "%s %.32s",
                  ( HOTSPOTS_PHASE_NETWORK == phase ) ? "NETWORK" : "HTTPS",
                  attempt_bss.ssid );
        return;
    }
    if( HOTSPOTS_PHASE_IDLE == phase )
    {
        snprintf( a_footer, a_capacity, "STARTING..." );
        return;
    }
    char label[ 16 ];
    if( !hotspot_history_ready() )
    {
        snprintf( label, sizeof( label ), "LOAD %u%%", (unsigned)hotspot_history_progress() );
    }
    else
    {
        snprintf( label,
                  sizeof( label ),
                  "%s%s",
                  ( HOTSPOTS_PHASE_SCANNING == phase ) ? "SCAN"
                                                       : ( auto_enabled ? "AUTO" : "MANUAL" ),
                  hotspot_history_degraded() ? " SD!" : "" );
    }
    snprintf( a_footer,
              a_capacity,
              "%s OPEN %u OWE %u SECURED %u",
              label,
              (unsigned)table.open_count,
              (unsigned)table.owe_count,
              (unsigned)table.secured_count );
}

static void render( int64_t a_now_us )
{
    render_due = false;
    last_render_us = a_now_us;
    hotspots_ui_render();
}

static void log_result( void )
{
    rfscout_log_record_t record;
    if( !log_begin( &record, "hotspot" ) )
    {
        return;
    }
    const bool success =
        log_hex( &record, "bssid", attempt_bss.bssid, sizeof( attempt_bss.bssid ) ) &&
        log_uint( &record, "channel", attempt_bss.channel ) &&
        log_int( &record, "rssi", attempt_result.start_rssi ) &&
        log_str( &record, "security", rfscout_wifi_station_auth_label( attempt_bss.auth_mode ) ) &&
        log_str( &record, "outcome", outcome_name( attempt_result.outcome ) ) &&
        log_str( &record, "stage", stage_name( attempt_result.failed_stage ) ) &&
        log_bool( &record, "ntp", attempt_result.ntp_ok ) &&
        log_int( &record, "http", attempt_result.http_status ) &&
        log_bool( &record, "manual", attempt_result.manual );
    if( success )
    {
        (void)log_end( &record );
    }
}

/** @brief Teach the profile used by a secured check whether this BSSID accepted it. */
static void record_profile_outcome( bool a_joined )
{
    if( 0U == attempt_profile_id )
    {
        return;
    }
    const bool rejected =
        !a_joined && rfscout_wifi_station_reason_auth_rejected( attempt_result.disconnect_reason );
    if( a_joined || rejected )
    {
        const esp_err_t result = saved_networks_record_bssid(
            attempt_profile_id,
            attempt_bss.bssid,
            attempt_bss.auth_mode,
            a_joined ? SAVED_NETWORK_BSSID_ASSOCIATED : SAVED_NETWORK_BSSID_REJECTED );
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "could not record BSSID outcome: %s", esp_err_to_name( result ) );
        }
    }
    attempt_profile_id = 0U;
}

/** @brief Quiesce TLS before releasing station; never stop Wi-Fi under a live probe. */
static esp_err_t release_check_services( void )
{
    const esp_err_t probe_result = connectivity_probe_stop();
    if( ESP_OK != probe_result )
    {
        return probe_result;
    }
    return network_session_release();
}

/** @brief Close one check: record once, then wait for confirmed network teardown. */
static void finish_attempt( hotspot_status_t a_outcome,
                            hotspot_stage_t a_failed_stage,
                            esp_err_t a_error,
                            int64_t a_now_us )
{
    rfscout_network_session_status_t session;
    network_session_status( &session );
    const bool joined = HOTSPOT_STATUS_GREEN == a_outcome || HOTSPOT_STATUS_YELLOW == a_outcome;
    const bool associated = session.associated;
    attempt_result.gateway_mac_valid =
        joined && ESP_OK == rfscout_wifi_station_gateway_mac( attempt_result.gateway_mac );
    const esp_err_t release_result = release_check_services();

    attempt_result.outcome = a_outcome;
    attempt_result.failed_stage = a_failed_stage;
    attempt_result.error = a_error;
    hotspot_entry_t *entry = hotspot_table_find( &table, attempt_bss.bssid );
    if( NULL != entry )
    {
        hotspot_entry_record_result( entry, &attempt_result );
    }
    ++checks_completed;
    rfscout_gps_snapshot_t gps = { 0 };
    (void)gps_snapshot_get( &gps );
    hotspot_history_record_result( &attempt_bss, &attempt_result, &gps );
    record_profile_outcome( associated );

    const uint8_t *mac = attempt_bss.bssid;
    ESP_LOGI( TAG,
              "check %02X:%02X:%02X:%02X:%02X:%02X outcome=%s stage=%s ntp=%d http=%d error=%s",
              mac[ 0 ],
              mac[ 1 ],
              mac[ 2 ],
              mac[ 3 ],
              mac[ 4 ],
              mac[ 5 ],
              outcome_name( a_outcome ),
              stage_name( a_failed_stage ),
              attempt_result.ntp_ok ? 1 : 0,
              attempt_result.http_status,
              esp_err_to_name( a_error ) );
    log_result();

    if( ESP_OK == release_result )
    {
        phase = HOTSPOTS_PHASE_SETTLE;
        phase_deadline_us = a_now_us + HOTSPOTS_SETTLE_US;
    }
    else
    {
        ESP_LOGW( TAG,
                  "connectivity teardown pending: %s",
                  esp_err_to_name( release_result ) );
        phase = HOTSPOTS_PHASE_CHECK_TEARDOWN;
        phase_deadline_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
    }
    render_due = true;
}

static void start_attempt( hotspot_entry_t *a_entry, int64_t a_now_us )
{
    attempt_bss = a_entry->bss;
    memset( &attempt_result, 0, sizeof( attempt_result ) );
    attempt_result.manual = a_entry->manual_requested;
    attempt_result.start_rssi = a_entry->bss.rssi;
    attempt_profile_id = 0U;
    a_entry->manual_requested = false;

    phase = HOTSPOTS_PHASE_NETWORK;
    phase_deadline_us = 0;
    render_due = true;

    esp_err_t result;
    if( RFSCOUT_WIFI_STATION_SECURITY_SECURED == attempt_bss.security )
    {
        /* A manual Join uses the chosen profile; automatic checks use the resolved one. */
        attempt_profile_id = 0U != a_entry->manual_profile_id && attempt_result.manual
                                 ? a_entry->manual_profile_id
                                 : a_entry->profile_id;
        char password[ SAVED_NETWORK_PASSWORD_MAX_LENGTH + 1U ] = { 0 };
        result = saved_networks_password( attempt_profile_id, password, sizeof( password ) );
        if( ESP_OK == result )
        {
            result = network_session_begin_secured_bss( &attempt_bss, password );
        }
        memset( password, 0, sizeof( password ) );
        a_entry->manual_profile_id = 0U;
    }
    else
    {
        result = network_session_begin_bss( &attempt_bss );
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not start association: %s", esp_err_to_name( result ) );
        finish_attempt( HOTSPOT_STATUS_RED, HOTSPOT_STAGE_JOIN, result, a_now_us );
    }
}

static void refresh_profiles( void )
{
    if( NULL == profiles )
    {
        profiles = app_session_calloc( SAVED_NETWORK_MAX_COUNT, sizeof( *profiles ) );
    }
    profile_count = 0U;
    if( NULL != profiles &&
        ESP_OK != saved_networks_list( profiles, SAVED_NETWORK_MAX_COUNT, &profile_count ) )
    {
        profile_count = 0U;
    }
}

static bool settings_valid( const void *a_record )
{
    const hotspots_settings_t *candidate = a_record;
    return HOTSPOTS_SETTINGS_MAGIC == candidate->magic &&
           candidate->match_distance_index < MATCH_DISTANCE_OPTION_COUNT &&
           candidate->retention_seconds_index < RETENTION_SECONDS_OPTION_COUNT &&
           candidate->eviction_distance_steps <= HOTSPOTS_EVICTION_DISTANCE_MAX_STEPS;
}

static void load_settings( void )
{
    settings = ( hotspots_settings_t ){ .magic = HOTSPOTS_SETTINGS_MAGIC,
                                        .match_distance_index = MATCH_DISTANCE_DEFAULT_INDEX,
                                        .retention_seconds_index = RETENTION_SECONDS_DEFAULT_INDEX,
                                        .eviction_distance_steps =
                                            HOTSPOTS_EVICTION_DISTANCE_DEFAULT_STEPS };
    /* Shared storage policy rejects and removes unsupported older record layouts.
       A settings version change therefore restores compiled defaults, without migration. */
    hotspots_settings_t saved = { 0 };
    if( ESP_OK == app_store_load( HOTSPOTS_SETTINGS_KEY, &saved, sizeof( saved ), settings_valid ) )
    {
        settings = saved;
    }
}

uint16_t hotspots_match_distance_m( void )
{
    return MATCH_DISTANCE_OPTIONS[ settings.match_distance_index ];
}

uint16_t hotspots_retention_seconds( void )
{
    return RETENTION_SECONDS_OPTIONS[ settings.retention_seconds_index ];
}

uint32_t hotspots_monitor_clock_ms( void )
{
    return monitor_clock_ms;
}

uint16_t hotspots_eviction_distance_m( void )
{
    return (uint16_t)( (uint16_t)settings.eviction_distance_steps * 100U );
}

static esp_err_t save_settings( const hotspots_settings_t *a_previous )
{
    const esp_err_t result =
        app_store_write( HOTSPOTS_SETTINGS_KEY, &settings, sizeof( settings ) );
    if( ESP_OK != result )
    {
        settings = *a_previous;
        ESP_LOGW( TAG, "could not save settings: %s", esp_err_to_name( result ) );
    }
    return result;
}

esp_err_t hotspots_step_match_distance( int a_direction )
{
    const hotspots_settings_t previous = settings;
    const uint8_t old = settings.match_distance_index;
    settings.match_distance_index =
        (uint8_t)( a_direction > 0
                       ? ( old + 1U ) % MATCH_DISTANCE_OPTION_COUNT
                       : ( old + MATCH_DISTANCE_OPTION_COUNT - 1U ) % MATCH_DISTANCE_OPTION_COUNT );
    return save_settings( &previous );
}

esp_err_t hotspots_step_retention_seconds( int a_direction )
{
    const hotspots_settings_t previous = settings;
    const uint8_t current = settings.retention_seconds_index;
    settings.retention_seconds_index =
        (uint8_t)( a_direction > 0 ? ( current + 1U ) % RETENTION_SECONDS_OPTION_COUNT
                                   : ( current + RETENTION_SECONDS_OPTION_COUNT - 1U ) %
                                         RETENTION_SECONDS_OPTION_COUNT );
    return save_settings( &previous );
}

esp_err_t hotspots_step_eviction_distance( int a_direction )
{
    const hotspots_settings_t previous = settings;
    settings.eviction_distance_steps =
        (uint8_t)( a_direction > 0 ? ( settings.eviction_distance_steps + 1U ) %
                                         ( HOTSPOTS_EVICTION_DISTANCE_MAX_STEPS + 1U )
                                   : ( settings.eviction_distance_steps +
                                       HOTSPOTS_EVICTION_DISTANCE_MAX_STEPS ) %
                                         ( HOTSPOTS_EVICTION_DISTANCE_MAX_STEPS + 1U ) );
    return save_settings( &previous );
}

const saved_network_profile_t *hotspots_profiles( size_t *a_count )
{
    *a_count = profile_count;
    return profiles;
}

/** History bounds for a BSSID; history keeps one entry per BSSID. */
static const geo_bounds_t *history_bounds( const uint8_t a_bssid[ 6 ] )
{
    for( size_t index = 0U; index < hotspot_history_count(); ++index )
    {
        const hotspot_history_entry_t *history = hotspot_history_at( index );
        if( 0 == memcmp( history->bssid, a_bssid, 6U ) )
        {
            return &history->bounds;
        }
    }
    return NULL;
}

/** @brief Refresh the profile snapshot and re-resolve every entry's credentials. */
static void resolve_credentials( const rfscout_gps_snapshot_t *a_gps )
{
    refresh_profiles();
    const hotspot_profiles_input_t input = { .profiles = profiles,
                                             .profile_count = profile_count,
                                             .table = &table,
                                             .history_bounds = history_bounds,
                                             .gps = a_gps,
                                             .match_distance_m = hotspots_match_distance_m() };
    for( size_t index = 0U; index < table.count; ++index )
    {
        hotspot_entry_t *entry = &table.entries[ index ];
        const bool secured = RFSCOUT_WIFI_STATION_SECURITY_SECURED == entry->bss.security;
        entry->profile_id = 0U;
        if( !secured )
        {
            entry->credential = HOTSPOT_CREDENTIAL_OPEN;
        }
        else if( !rfscout_wifi_station_secured_supported( entry->bss.auth_mode ) )
        {
            entry->credential = HOTSPOT_CREDENTIAL_UNSUPPORTED;
            entry->manual_profile_id = 0U;
        }
        else
        {
            entry->credential = hotspot_profiles_resolve( &input, entry, &entry->profile_id );
        }
        entry->credential_available = HOTSPOT_CREDENTIAL_OPEN == entry->credential ||
                                      HOTSPOT_CREDENTIAL_KNOWN == entry->credential ||
                                      HOTSPOT_CREDENTIAL_INFERRED == entry->credential;
        if( !hotspot_entry_checkable( entry ) )
        {
            entry->manual_requested = false;
        }
    }
}

void hotspots_credentials_changed( const network_credentials_outcome_t *a_outcome )
{
    if( NULL != a_outcome && NETWORK_CREDENTIALS_RESULT_FORGOTTEN == a_outcome->result )
    {
        /* Nothing more is learned for a deleted profile, and no queued join may use it. */
        if( a_outcome->profile_id == attempt_profile_id )
        {
            attempt_profile_id = 0U;
        }
        for( size_t index = 0U; index < table.count; ++index )
        {
            if( table.entries[ index ].manual_profile_id == a_outcome->profile_id )
            {
                table.entries[ index ].manual_profile_id = 0U;
            }
        }
    }
    rfscout_gps_snapshot_t gps = { 0 };
    (void)gps_snapshot_get( &gps );
    resolve_credentials( &gps );
    render_due = true;
}

static void complete_scan( int64_t a_now_us )
{
    /* Monitor is already stopped and all callbacks have quiesced. */
    drain_ap_queue();
    rfscout_gps_snapshot_t gps = { 0 };
    (void)gps_snapshot_get( &gps );
    const hotspot_retention_t retention = {
        .timeout_ms = (uint32_t)hotspots_retention_seconds() * 1000U,
        .distance_threshold_m = hotspots_eviction_distance_m(),
    };
    hotspot_table_finish_sweep( &table, monitor_clock_ms, &retention, &gps );
    for( size_t index = 0U; index < table.count; ++index )
    {
        if( table.entries[ index ].visible )
        {
            hotspot_history_observe( &table.entries[ index ].bss, &gps );
        }
    }
    const uint32_t queue_drops = ap_drops_snapshot();
    if( queue_drops > 0U || observations_rejected > 0U )
    {
        ESP_LOGW( TAG,
                  "AP monitoring pressure: queued drops=%lu admission rejects=%lu",
                  (unsigned long)queue_drops,
                  (unsigned long)observations_rejected );
    }
    resolve_credentials( &gps );
    phase = HOTSPOTS_PHASE_CHECK;
    window_end_us = a_now_us + HOTSPOTS_CHECK_WINDOW_US;
    render_due = true;
}

/** @brief Advance the scan/check scheduler; never blocks the main loop. */
static void scheduler_poll( int64_t a_now_us )
{
    network_session_poll( a_now_us );

    switch( phase )
    {
        case HOTSPOTS_PHASE_IDLE:
            break;

        case HOTSPOTS_PHASE_SCAN_START:
        {
            if( a_now_us < next_scan_us )
            {
                break;
            }
            /* Never activate promiscuous reception while a prior connection,
               event-loop drain or TLS probe still owns the Wi-Fi driver. */
            if( connectivity_probe_is_active() || network_session_is_active() )
            {
                const esp_err_t cleanup_result = release_check_services();
                if( ESP_OK != cleanup_result )
                {
                    ESP_LOGW( TAG,
                              "cannot resume monitor until network teardown: %s",
                              esp_err_to_name( cleanup_result ) );
                    next_scan_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
                    break;
                }
            }
            __atomic_store_n( &collecting_ap_observations, true, __ATOMIC_RELEASE );
            const esp_err_t result = radio_monitor_start();
            if( ESP_OK == result )
            {
                hotspot_table_begin_sweep( &table );
                monitor_last_tick_us = a_now_us;
                phase = HOTSPOTS_PHASE_SCANNING;
                phase_deadline_us = a_now_us + HOTSPOTS_MONITOR_SWEEP_US;
                render_due = true;
            }
            else
            {
                __atomic_store_n( &collecting_ap_observations, false, __ATOMIC_RELEASE );
                ESP_LOGW( TAG, "Wi-Fi monitor could not start: %s", esp_err_to_name( result ) );
                next_scan_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
            }
            break;
        }

        case HOTSPOTS_PHASE_SCANNING:
        {
            if( !radio_monitor_is_active() )
            {
                __atomic_store_n( &collecting_ap_observations, false, __ATOMIC_RELEASE );
                ESP_LOGW( TAG, "Wi-Fi monitor stopped unexpectedly; quiescing callbacks" );
                phase = HOTSPOTS_PHASE_MONITOR_STOP;
                break;
            }
            const int64_t sample_us = a_now_us < phase_deadline_us ? a_now_us : phase_deadline_us;
            tick_monitor_clock( sample_us );
            radio_monitor_step();
            drain_ap_queue();
            if( a_now_us >= phase_deadline_us )
            {
                __atomic_store_n( &collecting_ap_observations, false, __ATOMIC_RELEASE );
                const esp_err_t result = radio_monitor_stop();
                if( ESP_OK != result )
                {
                    ESP_LOGW( TAG, "Wi-Fi monitor did not quiesce: %s", esp_err_to_name( result ) );
                    /* A failed stop may leave callbacks in flight or receive mode active.
                       Retry the full quiesce path; do not enter station mode or restart. */
                    phase = HOTSPOTS_PHASE_MONITOR_STOP;
                }
                else
                {
                    complete_scan( a_now_us );
                }
            }
            break;
        }

        case HOTSPOTS_PHASE_MONITOR_STOP:
            /* Idempotent stop also waits for callbacks after a prior stop timeout. */
            if( ESP_OK == radio_monitor_stop() )
            {
                complete_scan( a_now_us );
            }
            break;

        case HOTSPOTS_PHASE_CHECK:
        {
            const bool window_open = a_now_us < window_end_us;
            hotspot_entry_t *candidate =
                hotspot_history_ready()
                    ? hotspot_table_next_candidate( &table, auto_enabled && window_open )
                    : NULL;
            if( NULL != candidate )
            {
                start_attempt( candidate, a_now_us );
            }
            else
            {
                phase = HOTSPOTS_PHASE_SCAN_START;
                next_scan_us = a_now_us;
            }
            break;
        }

        case HOTSPOTS_PHASE_NETWORK:
        {
            rfscout_network_session_status_t network;
            network_session_status( &network );
            if( RFSCOUT_NETWORK_SESSION_READY == network.state ||
                RFSCOUT_NETWORK_SESSION_LIMITED == network.state )
            {
                attempt_result.address = network.ip.address;
                attempt_result.netmask = network.ip.netmask;
                attempt_result.gateway = network.ip.gateway;
                attempt_result.dns_primary = network.ip.dns_primary;
                attempt_result.dns_backup = network.ip.dns_backup;

                if( RFSCOUT_NETWORK_SESSION_LIMITED == network.state )
                {
                    finish_attempt(
                        HOTSPOT_STATUS_YELLOW, HOTSPOT_STAGE_NTP, network.error, a_now_us );
                    break;
                }

                attempt_result.ntp_ok = true;
                const esp_err_t result = connectivity_probe_begin( HOTSPOTS_HTTPS_TIMEOUT_MS );
                if( ESP_OK != result )
                {
                    finish_attempt( HOTSPOT_STATUS_YELLOW, HOTSPOT_STAGE_HTTPS, result, a_now_us );
                    break;
                }
                phase = HOTSPOTS_PHASE_HTTPS;
                phase_deadline_us = a_now_us + HOTSPOTS_HTTPS_GUARD_US;
                render_due = true;
            }
            else if( RFSCOUT_NETWORK_SESSION_FAILED == network.state )
            {
                attempt_result.disconnect_reason = network.disconnect_reason;
                finish_attempt( HOTSPOT_STATUS_RED, HOTSPOT_STAGE_JOIN, network.error, a_now_us );
            }
            break;
        }

        case HOTSPOTS_PHASE_HTTPS:
        {
            rfscout_connectivity_probe_status_t probe;
            connectivity_probe_status( &probe );
            attempt_result.http_status = probe.http_status;
            if( RFSCOUT_CONNECTIVITY_PROBE_SUCCEEDED == probe.state )
            {
                strlcpy(
                    attempt_result.public_ip, probe.public_ip, sizeof( attempt_result.public_ip ) );
                strlcpy( attempt_result.colo, probe.colo, sizeof( attempt_result.colo ) );
                finish_attempt( HOTSPOT_STATUS_GREEN, HOTSPOT_STAGE_NONE, ESP_OK, a_now_us );
            }
            else if( RFSCOUT_CONNECTIVITY_PROBE_FAILED == probe.state )
            {
                finish_attempt( HOTSPOT_STATUS_YELLOW, HOTSPOT_STAGE_HTTPS, probe.error, a_now_us );
            }
            else if( a_now_us >= phase_deadline_us )
            {
                (void)connectivity_probe_stop();
                finish_attempt(
                    HOTSPOT_STATUS_YELLOW, HOTSPOT_STAGE_HTTPS, ESP_ERR_TIMEOUT, a_now_us );
            }
            break;
        }

        case HOTSPOTS_PHASE_CHECK_TEARDOWN:
            if( a_now_us >= phase_deadline_us )
            {
                const esp_err_t cleanup_result = release_check_services();
                if( ESP_OK == cleanup_result )
                {
                    phase = HOTSPOTS_PHASE_SETTLE;
                    phase_deadline_us = a_now_us + HOTSPOTS_SETTLE_US;
                    render_due = true;
                }
                else
                {
                    ESP_LOGW( TAG,
                              "retrying connectivity teardown: %s",
                              esp_err_to_name( cleanup_result ) );
                    phase_deadline_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
                }
            }
            break;

        case HOTSPOTS_PHASE_SETTLE:
            if( a_now_us >= phase_deadline_us )
            {
                phase = HOTSPOTS_PHASE_CHECK;
            }
            break;

        default:
            phase = HOTSPOTS_PHASE_SCAN_START;
            break;
    }
}

static rfscout_app_runtime_result_t hotspots_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    switch( a_event->type )
    {
        case RFSCOUT_APP_RUNTIME_ENTER:
            reset_session_state();
            hotspots_ui_reset();
            load_settings();
            if( ESP_OK != hotspot_history_open() )
            {
                ESP_LOGW( TAG, "history unavailable for this visit" );
            }
            render( a_event->now_us );
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_ACTIVATE:
            /* WiFi/BLE may have persisted a manual channel subset. Hotspots always
               makes its own full 14-channel passive sweep without inheriting that. */
            (void)radio_monitor_set_manual_channels( false, 0U, false, 120U );
            radio_monitor_set_ap_dwell_enabled( true );
            phase = HOTSPOTS_PHASE_SCAN_START;
            next_scan_us = a_event->now_us;
            render_due = true;
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_LEAVE:
            /* Station and probe services were already stopped by the session; app-session memory
               is about to be reclaimed, so flush history and drop every pointer into it. */
            hotspots_ui_close();
            hotspot_history_close();
            reset_session_state();
            hotspots_ui_reset();
            phase = HOTSPOTS_PHASE_IDLE;
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_INPUT:
            if( !hotspots_ui_input( &a_event->input ) )
            {
                return RFSCOUT_APP_RUNTIME_IGNORED;
            }
            render( a_event->now_us );
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_POLL:
            if( a_event->activated )
            {
                hotspot_history_poll( a_event->now_us );
                scheduler_poll( a_event->now_us );
            }
            if( render_due || ( a_event->now_us - last_render_us >= HOTSPOTS_RENDER_INTERVAL_US ) )
            {
                render( a_event->now_us );
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;

        default:
            return RFSCOUT_APP_RUNTIME_IGNORED;
    }
}

void hotspots_app_counts( hotspots_app_counts_t *a_counts )
{
    if( NULL == a_counts )
    {
        return;
    }
    memset( a_counts, 0, sizeof( *a_counts ) );
    for( size_t index = 0U; index < table.count; ++index )
    {
        const hotspot_entry_t *entry = &table.entries[ index ];
        if( entry->visible )
        {
            ++a_counts->visible;
        }
        if( HOTSPOT_STATUS_GREEN == entry->status )
        {
            ++a_counts->green;
        }
        else if( HOTSPOT_STATUS_YELLOW == entry->status )
        {
            ++a_counts->yellow;
        }
        else if( HOTSPOT_STATUS_RED == entry->status )
        {
            ++a_counts->red;
        }
    }
    a_counts->checks = checks_completed;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    hotspots_app_counts_t counts;
    hotspots_app_counts( &counts );
    bool success = log_uint( a_record, "visible", counts.visible ) &&
                   log_uint( a_record, "green", counts.green ) &&
                   log_uint( a_record, "yellow", counts.yellow ) &&
                   log_uint( a_record, "red", counts.red ) &&
                   log_uint( a_record, "checks", counts.checks ) &&
                   log_uint( a_record, "monitor_ms", monitor_clock_ms ) &&
                   log_uint( a_record, "ap_queue_drops", ap_drops_snapshot() ) &&
                   log_uint( a_record, "ap_rejected", observations_rejected );
    return success;
}

const rfscout_app_descriptor_t rfscout_hotspots_app = {
    .id = RFSCOUT_APP_HOTSPOTS,
    .name = "Hotspots",
    .persistence_key = "hotspots",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_NETWORK_SESSION |
                RFSCOUT_APP_SERVICE_CONNECTIVITY_PROBE,
    .consumers = { .wifi_ap_observation = observe_ap },
    .runtime = hotspots_runtime,
    .telemetry = app_telemetry,
};
