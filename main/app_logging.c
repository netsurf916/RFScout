/**
    app_logging.c : RFScout per-application SD logging coordinator
    Description: Gives each application lifecycle and rate-limited telemetry records.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_logging.h"

#include "app_manager.h"
#include "recorder.h"
#include "cc1101.h"
#include "cc1101_activity.h"
#include "log_writer.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "nrf24_activity.h"
#include "observation_store.h"
#include "system_events.h"

#include "esp_timer.h"

#include <stdio.h>

#define APP_LOG_INTERVAL_US 1000000LL

static rfscout_app_id_t last_app;
static int64_t last_record_us;
static bool initialized;

/** @brief Return the stable log token for an application identifier. */
static const char *app_name( rfscout_app_id_t a_app )
{
    static const char *names[ RFSCOUT_APP_COUNT ] = {
        [RFSCOUT_APP_SYSTEM_EVENTS] = "SYSTEM_EVENTS",
        [RFSCOUT_APP_DEVICE] = "DEVICE",
        [RFSCOUT_APP_WIFI_BT] = "WIFI_BT",
        [RFSCOUT_APP_CC1101] = "CC1101",
        [RFSCOUT_APP_NRF24] = "NRF24",
        [RFSCOUT_APP_AUDIO] = "RECORDER",
        [RFSCOUT_APP_THERMAL] = "THERMAL",
    };

    return ( a_app < RFSCOUT_APP_COUNT ) ? names[ a_app ] : "UNKNOWN";
}

/** @brief Emit one compact, application-specific telemetry record. */
static void write_periodic_record( rfscout_app_id_t a_app )
{
    char detail[ 160 ];

    switch( a_app )
    {
        case RFSCOUT_APP_SYSTEM_EVENTS:
            snprintf(
                detail, sizeof( detail ), "retained=%u", (unsigned)system_events_history_count() );
            break;

        case RFSCOUT_APP_WIFI_BT:
            snprintf( detail,
                      sizeof( detail ),
                      "active=%u surveillance=%u",
                      (unsigned)observation_store_count(),
                      (unsigned)observation_store_surveillance_count() );
            break;

        case RFSCOUT_APP_CC1101:
        {
            const cc1101_scan_config_t *config = cc1101_config();
            cc1101_fingerprint_t fingerprints[ CC1101_FINGERPRINT_MAX ];
            const size_t count =
                cc1101_activity_fingerprints( fingerprints, CC1101_FINGERPRINT_MAX );
            snprintf( detail,
                      sizeof( detail ),
                      "range_khz=%lu-%lu step_khz=%lu likely=%u detected=%u",
                      (unsigned long)config->low_khz,
                      (unsigned long)config->high_khz,
                      (unsigned long)config->step_khz,
                      (unsigned)count,
                      cc1101_is_detected() ? 1U : 0U );
            break;
        }

        case RFSCOUT_APP_NRF24:
        {
            const nrf24_scan_config_t *config = nrf24_config();
            nrf24_fingerprint_t fingerprints[ NRF24_FINGERPRINT_MAX ];
            const size_t count = nrf24_activity_fingerprints( fingerprints, NRF24_FINGERPRINT_MAX );
            snprintf( detail,
                      sizeof( detail ),
                      "channels=%u-%u dwell_us=%lu likely=%u detected=%u",
                      (unsigned)config->low_channel,
                      (unsigned)config->high_channel,
                      (unsigned long)config->dwell_us,
                      (unsigned)count,
                      nrf24_is_detected() ? 1U : 0U );
            break;
        }

        case RFSCOUT_APP_AUDIO:
            snprintf( detail,
                      sizeof( detail ),
                      "state=%u files=%u bytes=%lu",
                      (unsigned)recorder_state(),
                      (unsigned)recorder_file_count(),
                      (unsigned long)recorder_recorded_bytes() );
            break;

        case RFSCOUT_APP_THERMAL:
            snprintf( detail, sizeof( detail ), "detected=%u", mlx90640_is_detected() ? 1U : 0U );
            break;

        case RFSCOUT_APP_DEVICE:
        default:
            return;
    }

    (void)log_writer_app_record( app_name( a_app ), detail );
}

void app_logging_init( void )
{
    last_app = app_manager_current();
    last_record_us = 0;
    initialized = true;
    (void)log_writer_app_record( app_name( last_app ), "ENTER" );
}

void app_logging_poll( void )
{
    if( !initialized )
    {
        return;
    }

    const rfscout_app_id_t current = app_manager_current();
    const int64_t now = esp_timer_get_time();

    if( current != last_app )
    {
        (void)log_writer_app_record( app_name( last_app ), "EXIT" );
        (void)log_writer_app_record( app_name( current ), "ENTER" );
        last_app = current;
        last_record_us = 0;
    }

    if( ( 0 == last_record_us ) || ( ( now - last_record_us ) >= APP_LOG_INTERVAL_US ) )
    {
        last_record_us = now;
        write_periodic_record( current );
    }
}
