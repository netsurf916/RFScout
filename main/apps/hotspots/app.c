/**
    apps/hotspots/app.c : Hotspots application descriptor and runtime
    Description: Alternates passive scans with bounded credential-aware connectivity checks
                 (join, NTP, verified HTTPS) and presents the realtime result table.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_manager.h"

#include "app_session.h"
#include "connectivity_probe.h"
#include "event.h"
#include "hotspot_table.h"
#include "hotspots_app.h"
#include "hotspots_view.h"
#include "log_writer.h"
#include "network_session.h"
#include "ota_networks.h"
#include "saved_networks.h"
#include "wifi_station.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

#define HOTSPOTS_CHECK_WINDOW_US 2800000LL
#define HOTSPOTS_HTTPS_TIMEOUT_MS 10000U
#define HOTSPOTS_HTTPS_GUARD_US 12000000LL
#define HOTSPOTS_SETTLE_US 300000LL
#define HOTSPOTS_SCAN_TIMEOUT_US 8000000LL
#define HOTSPOTS_SCAN_RETRY_US 1000000LL
#define HOTSPOTS_RENDER_INTERVAL_US 500000LL
#define HOTSPOTS_FOOTER_MAX 64U
#define HOTSPOTS_DETAIL_LINE_MAX 64U

/** @brief Scheduler phase: alternating passive scan and check window, plus check stages. */
typedef enum
{
    HOTSPOTS_PHASE_IDLE = 0,
    HOTSPOTS_PHASE_SCAN_START,
    HOTSPOTS_PHASE_SCANNING,
    HOTSPOTS_PHASE_CHECK,
    HOTSPOTS_PHASE_NETWORK,
    HOTSPOTS_PHASE_HTTPS,
    HOTSPOTS_PHASE_SETTLE
} hotspots_phase_t;

/** @brief Presentation surface. */
typedef enum
{
    HOTSPOTS_MODE_LANDING = 0,
    HOTSPOTS_MODE_BROWSER,
    HOTSPOTS_MODE_DETAIL
} hotspots_mode_t;

static const char *TAG = "hotspots";

/* Session-scoped state. Pointers reference app-session memory and are cleared on ENTER/LEAVE. */
static hotspot_table_t table;
static rfscout_wifi_station_bss_t *scan_buffer;
static size_t *order_buffer;
static hotspots_view_row_t *row_buffer;
static size_t scratch_capacity;

static hotspots_phase_t phase;
static int64_t phase_deadline_us;
static int64_t window_end_us;
static int64_t next_scan_us;
static uint32_t scan_generation_at_start;
static rfscout_wifi_station_bss_t attempt_bss;
static hotspot_result_t attempt_result;
static size_t checks_completed;

static bool auto_enabled;
static hotspots_mode_t mode;
static unsigned browser_selected;
static uint8_t selected_bssid[ 6 ];
static bool selected_is_entry;
static unsigned detail_action;
static bool render_due;
static int64_t last_render_us;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_HOTSPOTS, a_value );
}

static void reset_session_state( void )
{
    memset( &table, 0, sizeof( table ) );
    scan_buffer = NULL;
    order_buffer = NULL;
    row_buffer = NULL;
    scratch_capacity = 0U;
    phase = HOTSPOTS_PHASE_IDLE;
    phase_deadline_us = 0;
    window_end_us = 0;
    next_scan_us = 0;
    scan_generation_at_start = 0U;
    memset( &attempt_bss, 0, sizeof( attempt_bss ) );
    memset( &attempt_result, 0, sizeof( attempt_result ) );
    checks_completed = 0U;
    auto_enabled = true;
    mode = HOTSPOTS_MODE_LANDING;
    browser_selected = HOTSPOTS_BROWSER_ITEM_BACK;
    memset( selected_bssid, 0, sizeof( selected_bssid ) );
    selected_is_entry = false;
    detail_action = HOTSPOTS_DETAIL_ACTION_BACK;
    render_due = true;
    last_render_us = 0;
}

/** @brief Grow presentation scratch to cover every table entry. */
static bool ensure_scratch( size_t a_count )
{
    if( a_count <= scratch_capacity )
    {
        return true;
    }
    size_t capacity = ( scratch_capacity > 0U ) ? scratch_capacity : 16U;
    while( capacity < a_count )
    {
        capacity *= 2U;
    }
    size_t *order = ( NULL == order_buffer )
                        ? app_session_calloc( capacity, sizeof( *order ) )
                        : app_session_resize( order_buffer, capacity, sizeof( *order ) );
    if( NULL == order )
    {
        return false;
    }
    order_buffer = order;
    hotspots_view_row_t *rows = ( NULL == row_buffer )
                                    ? app_session_calloc( capacity, sizeof( *rows ) )
                                    : app_session_resize( row_buffer, capacity, sizeof( *rows ) );
    if( NULL == rows )
    {
        return false;
    }
    row_buffer = rows;
    scratch_capacity = capacity;
    return true;
}

static bool attempt_running( void )
{
    return ( HOTSPOTS_PHASE_NETWORK == phase ) || ( HOTSPOTS_PHASE_HTTPS == phase );
}

static bool entry_is_attempt_target( const hotspot_entry_t *a_entry )
{
    return attempt_running() && ( 0 == memcmp( a_entry->bss.bssid, attempt_bss.bssid, 6U ) );
}

static const char *security_name( rfscout_wifi_station_security_t a_security )
{
    return ( RFSCOUT_WIFI_STATION_SECURITY_OWE == a_security ) ? "OWE" : "Open";
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

/** @brief Return the status-column tag and tone for one entry. */
static const char *entry_tag( const hotspot_entry_t *a_entry, hotspots_view_tone_t *a_tone )
{
    if( entry_is_attempt_target( a_entry ) )
    {
        *a_tone = HOTSPOTS_TONE_ACTIVE;
        if( HOTSPOTS_PHASE_NETWORK == phase )
        {
            return "JOIN";
        }
        return "HTTPS";
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
    if( auto_enabled && a_entry->visible && !hotspot_auto_gate_open( a_entry ) )
    {
        return "<-80";
    }
    return "-";
}

/** @brief Build ordered rows for every entry; returns the row count. */
static size_t build_rows( void )
{
    if( ( 0U == table.count ) || !ensure_scratch( table.count ) )
    {
        return 0U;
    }
    const size_t count = hotspot_table_order( &table, order_buffer, scratch_capacity );
    for( size_t row = 0U; row < count; ++row )
    {
        const hotspot_entry_t *entry = &table.entries[ order_buffer[ row ] ];
        hotspots_view_row_t *output = &row_buffer[ row ];
        output->ssid = entry->bss.ssid;
        output->tag = entry_tag( entry, &output->tone );
        output->rssi = entry->bss.rssi;
        output->visible = entry->visible;
    }
    return count;
}

static void format_footer( char *a_footer, size_t a_capacity )
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
    snprintf( a_footer,
              a_capacity,
              "%s OPEN %u OWE %u SECURED %u",
              ( HOTSPOTS_PHASE_SCANNING == phase ) ? "SCAN" : ( auto_enabled ? "AUTO" : "MANUAL" ),
              (unsigned)table.open_count,
              (unsigned)table.owe_count,
              (unsigned)table.secured_count );
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
            snprintf( a_line, a_capacity, "Last: NTP ok, HTTPS 200" );
            break;
        case HOTSPOT_STATUS_YELLOW:
            if( HOTSPOT_STAGE_NTP == last->failed_stage )
            {
                snprintf( a_line, a_capacity, "Last: NTP failed, HTTPS skipped" );
            }
            else if( last->http_status > 0 )
            {
                snprintf( a_line, a_capacity, "Last: NTP ok, HTTPS %d", last->http_status );
            }
            else
            {
                snprintf(
                    a_line, a_capacity, "Last: NTP ok, HTTPS %s", esp_err_to_name( last->error ) );
            }
            break;
        default:
            if( 0 != last->disconnect_reason )
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

static void render_detail( const hotspot_entry_t *a_entry )
{
    char bssid[ HOTSPOTS_DETAIL_LINE_MAX ];
    char radio[ HOTSPOTS_DETAIL_LINE_MAX ];
    char security[ HOTSPOTS_DETAIL_LINE_MAX ];
    char status[ HOTSPOTS_DETAIL_LINE_MAX ];
    char last[ HOTSPOTS_DETAIL_LINE_MAX ];
    char addresses[ HOTSPOTS_DETAIL_LINE_MAX ];
    char network[ HOTSPOTS_DETAIL_LINE_MAX ];
    char dns[ HOTSPOTS_DETAIL_LINE_MAX ];
    /* "Public " + IP + " (" + colo + ")" must fit without truncation. */
    char egress[ sizeof( "Public  ()" ) + HOTSPOT_PUBLIC_IP_MAX_LENGTH + HOTSPOT_COLO_MAX_LENGTH ];

    const uint8_t *mac = a_entry->bss.bssid;
    snprintf( bssid,
              sizeof( bssid ),
              "BSSID %02X:%02X:%02X:%02X:%02X:%02X",
              mac[ 0 ],
              mac[ 1 ],
              mac[ 2 ],
              mac[ 3 ],
              mac[ 4 ],
              mac[ 5 ] );
    if( a_entry->visible )
    {
        snprintf( radio,
                  sizeof( radio ),
                  "CH %u  RSSI %d dBm",
                  (unsigned)a_entry->bss.channel,
                  (int)a_entry->bss.rssi );
    }
    else
    {
        snprintf( radio,
                  sizeof( radio ),
                  "CH %u  not visible (missed %u)",
                  (unsigned)a_entry->bss.channel,
                  (unsigned)a_entry->missed_scans );
    }
    snprintf( security,
              sizeof( security ),
              "%s  checks %u",
              security_name( a_entry->bss.security ),
              (unsigned)a_entry->attempts );

    hotspots_view_tone_t tone = HOTSPOTS_TONE_NEUTRAL;
    const char *tag = entry_tag( a_entry, &tone );
    if( entry_is_attempt_target( a_entry ) )
    {
        snprintf( status, sizeof( status ), "Checking: %s", tag );
    }
    else
    {
        switch( a_entry->status )
        {
            case HOTSPOT_STATUS_GREEN:
                snprintf( status, sizeof( status ), "OK - Internet reachable" );
                break;
            case HOTSPOT_STATUS_YELLOW:
                snprintf( status, sizeof( status ), "LIMITED - joined, Internet not verified" );
                break;
            case HOTSPOT_STATUS_RED:
                snprintf( status, sizeof( status ), "FAILED - could not join" );
                break;
            case HOTSPOT_STATUS_UNCHECKED:
            default:
                snprintf( status,
                          sizeof( status ),
                          "%s",
                          a_entry->manual_requested ? "Queued for recheck" : "Not checked" );
                break;
        }
    }
    describe_last_attempt( a_entry, last, sizeof( last ) );

    /* Show validated network details even when the latest recheck failed. */
    const hotspot_result_t *evidence = a_entry->has_success ? &a_entry->success : &a_entry->last;
    char client[ 20 ];
    char gateway[ 20 ];
    char netmask[ 20 ];
    char dns_primary[ 20 ];
    char dns_backup[ 20 ];
    format_ipv4( evidence->address, client, sizeof( client ) );
    format_ipv4( evidence->gateway, gateway, sizeof( gateway ) );
    format_ipv4( evidence->netmask, netmask, sizeof( netmask ) );
    format_ipv4( evidence->dns_primary, dns_primary, sizeof( dns_primary ) );
    format_ipv4( evidence->dns_backup, dns_backup, sizeof( dns_backup ) );
    snprintf( addresses, sizeof( addresses ), "IP %s  GW %s", client, gateway );
    snprintf( network, sizeof( network ), "Mask %s", netmask );
    snprintf( dns, sizeof( dns ), "DNS %s / %s", dns_primary, dns_backup );
    if( '\0' != evidence->public_ip[ 0 ] )
    {
        snprintf( egress,
                  sizeof( egress ),
                  "Public %s (%s)",
                  evidence->public_ip,
                  ( '\0' != evidence->colo[ 0 ] ) ? evidence->colo : "?" );
    }
    else
    {
        snprintf( egress, sizeof( egress ), "Public --" );
    }

    const hotspots_view_detail_t detail = {
        .ssid = a_entry->bss.ssid,
        .bssid = bssid,
        .radio = radio,
        .security = security,
        .status = status,
        .status_tone = tone,
        .last_attempt = last,
        .addresses = addresses,
        .network = network,
        .dns = dns,
        .egress = egress,
        .recheck_available =
            a_entry->visible && ( RFSCOUT_WIFI_STATION_SECURITY_SECURED != a_entry->bss.security ||
                                  a_entry->credential_available ),
    };
    hotspots_view_detail( &detail, detail_action );
}

/** @brief Keep browser selection attached to its BSSID while rows re-sort. */
static void resolve_browser_selection( size_t a_row_count )
{
    if( selected_is_entry )
    {
        for( size_t row = 0U; row < a_row_count; ++row )
        {
            if( 0 == memcmp( table.entries[ order_buffer[ row ] ].bss.bssid, selected_bssid, 6U ) )
            {
                browser_selected = HOTSPOTS_BROWSER_FIXED_ITEMS + (unsigned)row;
                return;
            }
        }
        selected_is_entry = false;
        browser_selected = HOTSPOTS_BROWSER_ITEM_AUTO;
    }
    const unsigned item_count = HOTSPOTS_BROWSER_FIXED_ITEMS + (unsigned)a_row_count;
    if( browser_selected >= item_count )
    {
        browser_selected = item_count - 1U;
    }
}

static void render( int64_t a_now_us )
{
    render_due = false;
    last_render_us = a_now_us;

    char footer[ HOTSPOTS_FOOTER_MAX ];
    format_footer( footer, sizeof( footer ) );

    if( HOTSPOTS_MODE_DETAIL == mode )
    {
        const hotspot_entry_t *entry = hotspot_table_find( &table, selected_bssid );
        if( NULL != entry )
        {
            render_detail( entry );
            return;
        }
        /* The access point's observation session ended; fall back to the browser. */
        mode = HOTSPOTS_MODE_BROWSER;
        selected_is_entry = false;
    }

    const size_t row_count = build_rows();
    if( HOTSPOTS_MODE_BROWSER == mode )
    {
        resolve_browser_selection( row_count );
        hotspots_view_browser(
            row_buffer, row_count, browser_selected, app_manager_locked(), auto_enabled, footer );
        return;
    }
    hotspots_view_landing( row_buffer, row_count, footer );
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
        log_str( &record, "security", security_name( attempt_bss.security ) ) &&
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

/** @brief Close one check: leave the network, record the result, and settle before continuing. */
static void finish_attempt( hotspot_status_t a_outcome,
                            hotspot_stage_t a_failed_stage,
                            esp_err_t a_error,
                            int64_t a_now_us )
{
    (void)network_session_release();

    attempt_result.outcome = a_outcome;
    attempt_result.failed_stage = a_failed_stage;
    attempt_result.error = a_error;
    hotspot_entry_t *entry = hotspot_table_find( &table, attempt_bss.bssid );
    if( NULL != entry )
    {
        hotspot_entry_record_result( entry, &attempt_result );
    }
    ++checks_completed;

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

    phase = HOTSPOTS_PHASE_SETTLE;
    phase_deadline_us = a_now_us + HOTSPOTS_SETTLE_US;
    render_due = true;
}

static void start_attempt( hotspot_entry_t *a_entry, int64_t a_now_us )
{
    attempt_bss = a_entry->bss;
    memset( &attempt_result, 0, sizeof( attempt_result ) );
    attempt_result.manual = a_entry->manual_requested;
    attempt_result.start_rssi = a_entry->bss.rssi;
    a_entry->manual_requested = false;

    phase = HOTSPOTS_PHASE_NETWORK;
    phase_deadline_us = 0;
    render_due = true;

    esp_err_t result;
    if( RFSCOUT_WIFI_STATION_SECURITY_SECURED == attempt_bss.security )
    {
        char password[ OTA_NETWORK_PASSWORD_MAX_LENGTH + 1U ] = { 0 };
        result = saved_networks_password_for_bss( attempt_bss.bssid,
                                                  attempt_bss.ssid,
                                                  attempt_bss.auth_mode,
                                                  password,
                                                  sizeof( password ) );
        if( ESP_OK == result )
        {
            result = network_session_begin_secured_bss( &attempt_bss, password );
        }
        memset( password, 0, sizeof( password ) );
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

static void complete_scan( int64_t a_now_us )
{
    if( NULL == scan_buffer )
    {
        scan_buffer =
            app_session_calloc( RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS, sizeof( *scan_buffer ) );
    }
    if( NULL != scan_buffer )
    {
        const size_t count =
            rfscout_wifi_station_bss_results( scan_buffer, RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS );
        if( ESP_OK != hotspot_table_apply_scan( &table, scan_buffer, count ) )
        {
            ESP_LOGW( TAG, "access-point table could not grow; some networks are not shown" );
        }
    }
    /* A secured candidate is eligible only for an exact, previously associated
       BSSID with matching SSID and concrete authentication mode. */
    for( size_t index = 0U; index < table.count; ++index )
    {
        hotspot_entry_t *entry = &table.entries[ index ];
        const bool secured = RFSCOUT_WIFI_STATION_SECURITY_SECURED == entry->bss.security;
        entry->credential_available = !secured;
        if( secured )
        {
            char password[ OTA_NETWORK_PASSWORD_MAX_LENGTH + 1U ] = { 0 };
            entry->credential_available =
                ESP_OK == saved_networks_password_for_bss( entry->bss.bssid,
                                                           entry->bss.ssid,
                                                           entry->bss.auth_mode,
                                                           password,
                                                           sizeof( password ) );
            memset( password, 0, sizeof( password ) );
        }
        if( !entry->credential_available )
        {
            entry->manual_requested = false;
        }
    }
    phase = HOTSPOTS_PHASE_CHECK;
    window_end_us = a_now_us + HOTSPOTS_CHECK_WINDOW_US;
    render_due = true;
}

/** @brief Advance the scan/check scheduler; never blocks the main loop. */
static void scheduler_poll( int64_t a_now_us )
{
    network_session_poll( a_now_us );

    rfscout_wifi_station_status_t station;
    rfscout_wifi_station_status( &station );

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
            scan_generation_at_start = station.scan_generation;
            const esp_err_t result = rfscout_wifi_station_passive_scan_start();
            if( ESP_OK == result )
            {
                phase = HOTSPOTS_PHASE_SCANNING;
                phase_deadline_us = a_now_us + HOTSPOTS_SCAN_TIMEOUT_US;
                render_due = true;
            }
            else
            {
                ESP_LOGW( TAG, "passive scan could not start: %s", esp_err_to_name( result ) );
                next_scan_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
            }
            break;
        }

        case HOTSPOTS_PHASE_SCANNING:
            if( station.scan_generation != scan_generation_at_start )
            {
                complete_scan( a_now_us );
            }
            else if( ( RFSCOUT_WIFI_STATION_SCANNING != station.state ) ||
                     ( a_now_us >= phase_deadline_us ) )
            {
                ESP_LOGW( TAG, "passive scan did not complete; retrying" );
                (void)rfscout_wifi_station_cancel_scan();
                phase = HOTSPOTS_PHASE_SCAN_START;
                next_scan_us = a_now_us + HOTSPOTS_SCAN_RETRY_US;
            }
            break;

        case HOTSPOTS_PHASE_CHECK:
        {
            const bool window_open = a_now_us < window_end_us;
            hotspot_entry_t *candidate =
                hotspot_table_next_candidate( &table, auto_enabled && window_open );
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

static unsigned browser_item_count( void )
{
    return HOTSPOTS_BROWSER_FIXED_ITEMS + (unsigned)table.count;
}

/** @brief Record which BSSID the browser selection points at after it moves. */
static void capture_browser_selection( void )
{
    selected_is_entry = false;
    if( ( browser_selected < HOTSPOTS_BROWSER_FIXED_ITEMS ) || ( NULL == order_buffer ) )
    {
        return;
    }
    const size_t row = browser_selected - HOTSPOTS_BROWSER_FIXED_ITEMS;
    const size_t count = hotspot_table_order( &table, order_buffer, scratch_capacity );
    if( row < count )
    {
        memcpy( selected_bssid, table.entries[ order_buffer[ row ] ].bss.bssid, 6U );
        selected_is_entry = true;
    }
}

static void handle_browser_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        const unsigned count = browser_item_count();
        int target = (int)browser_selected + ( ( a_input->value > 0 ) ? 1 : -1 );
        if( target < 0 )
        {
            target = 0;
        }
        if( target >= (int)count )
        {
            target = (int)count - 1;
        }
        browser_selected = (unsigned)target;
        capture_browser_selection();
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return;
    }

    if( HOTSPOTS_BROWSER_ITEM_BACK == browser_selected )
    {
        mode = HOTSPOTS_MODE_LANDING;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }
    if( HOTSPOTS_BROWSER_ITEM_LOCK == browser_selected )
    {
        const bool locked = app_manager_set_locked( !app_manager_locked() );
        ESP_LOGI( TAG, "application navigation %s", locked ? "locked" : "unlocked" );
        return;
    }
    if( HOTSPOTS_BROWSER_ITEM_AUTO == browser_selected )
    {
        auto_enabled = !auto_enabled;
        ESP_LOGI( TAG, "automatic checks %s", auto_enabled ? "enabled" : "disabled" );
        emit_event( auto_enabled ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                    auto_enabled ? 1 : 0 );
        return;
    }
    capture_browser_selection();
    if( selected_is_entry )
    {
        mode = HOTSPOTS_MODE_DETAIL;
        detail_action = HOTSPOTS_DETAIL_ACTION_BACK;
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
    }
}

static void handle_detail_input( const rfscout_input_event_t *a_input )
{
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        detail_action =
            ( a_input->value > 0 ) ? HOTSPOTS_DETAIL_ACTION_RECHECK : HOTSPOTS_DETAIL_ACTION_BACK;
        emit_event( RFSCOUT_EVENT_NAVIGATE, a_input->value > 0 ? 1 : -1 );
        return;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return;
    }
    if( HOTSPOTS_DETAIL_ACTION_BACK == detail_action )
    {
        mode = HOTSPOTS_MODE_BROWSER;
        emit_event( RFSCOUT_EVENT_BACK, 0 );
        return;
    }

    hotspot_entry_t *entry = hotspot_table_find( &table, selected_bssid );
    if( ( NULL != entry ) && entry->visible &&
        ( RFSCOUT_WIFI_STATION_SECURITY_SECURED != entry->bss.security ||
          entry->credential_available ) &&
        !entry_is_attempt_target( entry ) )
    {
        entry->manual_requested = true;
        emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
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
            render( a_event->now_us );
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_ACTIVATE:
            phase = HOTSPOTS_PHASE_SCAN_START;
            next_scan_us = a_event->now_us;
            render_due = true;
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_LEAVE:
            /* Station and probe services were already stopped by the session; app-session memory
               is about to be reclaimed, so drop every pointer into it. */
            reset_session_state();
            phase = HOTSPOTS_PHASE_IDLE;
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_INPUT:
            if( HOTSPOTS_MODE_LANDING == mode )
            {
                if( RFSCOUT_INPUT_SELECT != a_event->input.type )
                {
                    return RFSCOUT_APP_RUNTIME_IGNORED;
                }
                mode = HOTSPOTS_MODE_BROWSER;
                browser_selected = HOTSPOTS_BROWSER_ITEM_BACK;
                selected_is_entry = false;
                emit_event( RFSCOUT_EVENT_ENTER, 0 );
            }
            else if( HOTSPOTS_MODE_BROWSER == mode )
            {
                handle_browser_input( &a_event->input );
            }
            else
            {
                handle_detail_input( &a_event->input );
            }
            render( a_event->now_us );
            return RFSCOUT_APP_RUNTIME_HANDLED;

        case RFSCOUT_APP_RUNTIME_POLL:
            if( a_event->activated )
            {
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
                   log_uint( a_record, "checks", counts.checks );
    return success;
}

const rfscout_app_descriptor_t rfscout_hotspots_app = {
    .id = RFSCOUT_APP_HOTSPOTS,
    .name = "Hotspots",
    .persistence_key = "hotspots",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_NETWORK_SESSION | RFSCOUT_APP_SERVICE_CONNECTIVITY_PROBE,
    .runtime = hotspots_runtime,
    .telemetry = app_telemetry,
};
