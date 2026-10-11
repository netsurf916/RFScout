/**
    app_logging.c : RFScout per-application SD logging coordinator
    Description: Gives each application lifecycle and rate-limited telemetry records.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_logging.h"

#include "app_lifecycle.h"
#include "app_manager.h"
#include "cc1101.h"
#include "hotspots_app.h"
#include "gps.h"
#include "log_writer.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "recorder.h"
#include "system_events.h"

#include "esp_timer.h"

#define APP_LOG_INTERVAL_US 1000000LL

static rfscout_app_id_t last_app;
static uint32_t last_transition_sequence;
static int64_t last_record_us;
static bool initialized;

static const char *app_name( rfscout_app_id_t a_app )
{
    static const char *names[ RFSCOUT_APP_COUNT ] = {
        [RFSCOUT_APP_SYSTEM_EVENTS] = "SYSTEM_EVENTS",
        [RFSCOUT_APP_DEVICE] = "DEVICE",
        [RFSCOUT_APP_WIFI_BT] = "WIFI_BT",
        [RFSCOUT_APP_HOTSPOTS] = "HOTSPOTS",
        [RFSCOUT_APP_CC1101] = "CC1101",
        [RFSCOUT_APP_NRF24] = "NRF24",
        [RFSCOUT_APP_AUDIO] = "RECORDER",
        [RFSCOUT_APP_THERMAL] = "THERMAL",
        [RFSCOUT_APP_AUDIO_SPECTRUM] = "AUDIO_SPECTRUM",
        [RFSCOUT_APP_GPS] = "GPS",
    };

    return ( ( a_app >= 0 ) && ( a_app < RFSCOUT_APP_COUNT ) ) ? names[ a_app ] : "UNKNOWN";
}

static void write_lifecycle_record( rfscout_app_id_t a_app, const char *a_event )
{
    rfscout_log_record_t record;
    if( !log_begin( &record, "app" ) || !log_str( &record, "app", app_name( a_app ) ) ||
        !log_str( &record, "event", a_event ) )
    {
        return;
    }
    (void)log_end( &record );
}

static void write_transition_record( const rfscout_app_transition_diagnostics_t *a_diagnostics )
{
    if( ( NULL == a_diagnostics ) || ( 0U == a_diagnostics->sequence ) )
    {
        return;
    }

    rfscout_log_record_t record;
    if( !log_begin( &record, "transition" ) )
    {
        return;
    }

    const bool success =
        log_uint( &record, "seq", a_diagnostics->sequence ) &&
        log_str( &record, "from", app_name( a_diagnostics->from ) ) &&
        log_str( &record, "to", app_name( a_diagnostics->to ) ) &&
        log_uint( &record, "outcome", a_diagnostics->outcome ) &&
        log_str( &record, "error", esp_err_to_name( a_diagnostics->error ) ) &&
        log_uint( &record, "reclaimed", a_diagnostics->reclaimed_bytes ) &&
        log_uint( &record, "remaining", a_diagnostics->remaining_allocations ) &&
        log_uint( &record, "media_closed", a_diagnostics->closed_media_handles ) &&
        log_int( &record, "elapsed_us", a_diagnostics->elapsed_us ) &&
        log_uint( &record, "psram_free", a_diagnostics->psram_free_bytes ) &&
        log_uint( &record, "psram_largest", a_diagnostics->psram_largest_free_block );

    if( success )
    {
        (void)log_end( &record );
    }
}

static void write_periodic_record( rfscout_app_id_t a_app )
{
    if( ( RFSCOUT_APP_WIFI_BT == a_app ) || ( RFSCOUT_APP_DEVICE == a_app ) ||
        ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return;
    }

    rfscout_log_record_t record;
    if( !log_begin( &record, "telemetry" ) || !log_str( &record, "app", app_name( a_app ) ) )
    {
        return;
    }

    bool success = true;
    switch( a_app )
    {
        case RFSCOUT_APP_SYSTEM_EVENTS:
            success = log_uint( &record, "retained", system_events_history_count() );
            break;

        case RFSCOUT_APP_CC1101:
        {
            cc1101_scan_config_t scan;
            cc1101_receive_config_t receive;
            cc1101_capture_stats_t stats;
            cc1101_scan_config( &scan );
            cc1101_receive_config( &receive );
            cc1101_capture_stats( &stats );

            const cc1101_acquisition_mode_t mode = cc1101_acquisition_mode();
            success = log_uint( &record, "mode", mode ) &&
                      log_bool( &record, "detected", cc1101_is_detected() );

            if( success && ( CC1101_ACQUISITION_RECEIVE == mode ) )
            {
                success = log_uint( &record, "frequency_khz", receive.frequency_khz ) &&
                          log_uint( &record, "preset", receive.preset ) &&
                          log_uint( &record, "captures", stats.captures_completed ) &&
                          log_uint( &record, "truncated", stats.captures_truncated ) &&
                          log_uint( &record, "overruns", stats.capture_overruns ) &&
                          log_uint( &record, "errors", stats.capture_errors );
            }
            else if( success )
            {
                success = log_uint( &record, "low_khz", scan.low_khz ) &&
                          log_uint( &record, "high_khz", scan.high_khz ) &&
                          log_uint( &record, "step_khz", scan.step_khz );
            }
            break;
        }

        case RFSCOUT_APP_NRF24:
        {
            const nrf24_scan_config_t *config = nrf24_config();
            nrf24_fingerprint_t fingerprints[ NRF24_FINGERPRINT_MAX ];
            const size_t count = nrf24_activity_fingerprints( fingerprints, NRF24_FINGERPRINT_MAX );
            success = log_uint( &record, "low_channel", config->low_channel ) &&
                      log_uint( &record, "high_channel", config->high_channel ) &&
                      log_uint( &record, "dwell_us", config->dwell_us ) &&
                      log_uint( &record, "likely", count ) &&
                      log_bool( &record, "detected", nrf24_is_detected() );
            break;
        }

        case RFSCOUT_APP_AUDIO:
            success = log_uint( &record, "state", recorder_state() ) &&
                      log_uint( &record, "files", recorder_file_count() ) &&
                      log_uint( &record, "bytes", recorder_recorded_bytes() );
            break;

        case RFSCOUT_APP_AUDIO_SPECTRUM:
            success = log_uint( &record, "state", recorder_state() ) &&
                      log_uint( &record, "range_db", recorder_monitor_range_db() );
            break;

        case RFSCOUT_APP_THERMAL:
            success = log_bool( &record, "detected", mlx90640_is_detected() );
            break;

        case RFSCOUT_APP_GPS:
        {
            rfscout_gps_snapshot_t gps = { 0 };
            const bool receiver_present = gps_snapshot_get( &gps );
            success = log_bool( &record, "uart_active", gps_available() ) &&
                      log_bool( &record, "receiver", receiver_present ) &&
                      log_bool( &record, "fix", receiver_present && gps.location_valid ) &&
                      log_double( &record, "trip_m", gps_trip_distance_m() );
            if( success && receiver_present && gps.speed_valid )
            {
                success = log_double( &record, "speed_mps", gps.speed_mps );
            }
            if( success && receiver_present && gps.altitude_valid )
            {
                success = log_double( &record, "altitude_m", gps.altitude_m );
            }
            if( success && receiver_present && gps.satellites_valid )
            {
                success = log_uint( &record, "satellites_used", gps.satellites );
            }
            break;
        }

        case RFSCOUT_APP_HOTSPOTS:
        {
            hotspots_app_counts_t counts;
            hotspots_app_counts( &counts );
            success = log_uint( &record, "visible", counts.visible ) &&
                      log_uint( &record, "green", counts.green ) &&
                      log_uint( &record, "yellow", counts.yellow ) &&
                      log_uint( &record, "red", counts.red ) &&
                      log_uint( &record, "checks", counts.checks );
            break;
        }

        default:
            /* Never abandon a begun log record with the writer mutex held. */
            success = log_str( &record, "status", "unsupported" );
            break;
    }

    if( success )
    {
        (void)log_end( &record );
    }
}

void app_logging_init( void )
{
    last_app = app_manager_current();
    const rfscout_app_transition_diagnostics_t *transition = app_lifecycle_last_transition();
    last_transition_sequence = ( NULL != transition ) ? transition->sequence : 0U;
    last_record_us = 0;
    initialized = true;
    write_lifecycle_record( last_app, "enter" );
}

void app_logging_poll( void )
{
    if( !initialized )
    {
        return;
    }

    const rfscout_app_transition_diagnostics_t *transition = app_lifecycle_last_transition();
    if( ( NULL != transition ) && ( 0U != transition->sequence ) &&
        ( transition->sequence != last_transition_sequence ) )
    {
        last_transition_sequence = transition->sequence;
        write_transition_record( transition );
    }

    const rfscout_app_id_t current = app_manager_current();
    const int64_t now = esp_timer_get_time();
    if( current != last_app )
    {
        write_lifecycle_record( last_app, "exit" );
        write_lifecycle_record( current, "enter" );
        last_app = current;
        last_record_us = 0;
    }

    if( ( 0 == last_record_us ) || ( ( now - last_record_us ) >= APP_LOG_INTERVAL_US ) )
    {
        last_record_us = now;
        write_periodic_record( current );
    }
}
