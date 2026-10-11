/**
    gps.c : RFScout GNSS location/time service
    Description: Parses checksum-validated NMEA RMC/GGA data from the runtime board GPS UART.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "gps.h"

#include "board.h"
#include "device_config.h"
#include "device_time.h"
#include "log_writer.h"
#include "persistent_store.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_UART UART_NUM_1
#define GPS_BAUD_PRIMARY 38400
#define GPS_BAUD_FALLBACK 9600
#define GPS_BAUD_MAX 115200

static const int GPS_BAUD_RATES[] = {
    GPS_BAUD_PRIMARY, GPS_BAUD_FALLBACK, 19200, 57600, GPS_BAUD_MAX };
#define GPS_BAUD_PROBE_MS 1500U
#define GPS_INITIAL_PROBE_CYCLES 2U
#define GPS_RX_BUFFER 1024
#define GPS_LINE_MAX 128
#define GPS_FIX_STALE_MS 5000U
#define GPS_TIME_STALE_MS 5000U
#define GPS_TRIP_STORE_KEY "gps/trip_distance"
#define GPS_TRIP_STORE_VERSION 1U
#define GPS_TRIP_MIN_STEP_M 5.0
#define GPS_TRIP_MIN_SPEED_MPS 0.7
#define GPS_TRIP_MAX_SPEED_MPS 100.0
#define GPS_TRIP_MAX_GAP_US 10000000LL
#define GPS_TRIP_CHECKPOINT_INTERVAL_US 300000000LL
#define GPS_TRIP_CHECKPOINT_DISTANCE_M 250.0
#define GPS_EARTH_RADIUS_M 6371000.0
#define GPS_RADIANS_PER_DEGREE 0.017453292519943295

static const char *TAG = "gps";
static bool initialized;
static bool receiver_detected;
static int64_t last_nmea_us;
static bool initial_probe_complete;
static int64_t initial_probe_started_us;
static bool baud_locked;
static int active_baud;
static size_t active_baud_index;
static int64_t baud_probe_started_us;
static char line[ GPS_LINE_MAX ];
static size_t line_length;
static rfscout_gps_snapshot_t snapshot;
static int64_t location_updated_us;
static int64_t time_updated_us;
static int64_t altitude_updated_us;
static int64_t speed_updated_us;
static int64_t satellites_updated_us;
static portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static rfscout_gps_snapshot_t published_snapshot;
static int64_t published_location_us;
static int64_t published_time_us;
static bool published_initialized;

/* System-owned trip odometer survives app changes and temporary nRF24 GPS suspension.
   Flash checkpoints are deliberately sparse to avoid writes on each NMEA fix. */
typedef struct
{
    uint32_t version;
    double distance_m;
} gps_trip_record_t;

/* Receiver CASIC responses can exceed two seconds under live NMEA traffic. */
#define GPS_POLICY_TIMEOUT_US 4000000LL
#define GPS_POLICY_PROBE_RETRY_LIMIT 2U
#define GPS_POLICY_PROBE_RETRY_DELAY_US 300000LL
#define GPS_POLICY_MIN_CHANGE_US 30000000LL
#define GPS_CASIC_MAX_PAYLOAD 128U
#define GPS_BAUD_SWITCH_TIMEOUT_US 5000000LL

/* HT1818Z3G5L: CASIC binary protocol. Keep generic NMEA on any failure. */
typedef enum
{
    GPS_POLICY_IDLE = 0,
    GPS_POLICY_WAIT_VERSION,
    GPS_POLICY_WAIT_RATE,
    GPS_POLICY_ACTIVE,
    GPS_POLICY_WAIT_ACK,
    GPS_POLICY_VERIFY_RATE,
    GPS_POLICY_WAIT_BAUD,
    GPS_POLICY_UNSUPPORTED
} gps_policy_state_t;

static gps_policy_state_t policy_state;
static int64_t policy_deadline_us;
static int64_t policy_last_change_us;
static int64_t policy_speed_since_us;
static double policy_filtered_speed_mps;
static int64_t policy_filtered_speed_updated_us;
static bool policy_filtered_speed_valid;
static int policy_desired_interval_ms = 1000;
static int policy_current_interval_ms = 1000;
static int policy_pending_interval_ms;
static uint8_t policy_uart_config[ 8 ];
static bool policy_uart_config_valid;
static int64_t policy_baud_switch_us;
static int policy_baud_previous;
static bool policy_baud_attempted;
static uint8_t casic_frame[ GPS_CASIC_MAX_PAYLOAD + 10U ];
static size_t casic_length;
static size_t casic_expected;
static unsigned policy_probe_index;
static unsigned policy_probe_retries;
static unsigned policy_rate_query_retries;
static int64_t policy_rate_query_retry_at_us;
static int64_t policy_probe_retry_at_us;
static const uint8_t policy_probe_ids[] = { 0x04U, 0x09U, 0x00U };
static const uint8_t policy_probe_classes[] = { 0x0AU, 0x0AU, 0x06U };
static const char *const policy_probe_names[] = { "MON-VER", "MON-HW", "CFG-PRT" };

/* One-off policy transitions are recorded independently of the active application.
   SD logging is best-effort and never changes GNSS policy decisions. */
static void policy_log_event( const char *a_event, int a_requested_ms, int a_actual_ms )
{
    rfscout_log_record_t record;
    if( !log_begin( &record, "gps_policy" ) )
    {
        return;
    }
    const bool ok = log_str( &record, "event", a_event ) &&
                    log_int( &record, "baud", active_baud ) &&
                    log_int( &record, "requested_ms", a_requested_ms ) &&
                    log_int( &record, "actual_ms", a_actual_ms ) &&
                    log_bool( &record, "speed_valid", policy_filtered_speed_valid ) &&
                    ( !policy_filtered_speed_valid ||
                      log_double( &record, "filtered_speed_mph",
                                  policy_filtered_speed_mps * 2.23693629 ) );
    if( ok )
    {
        (void)log_end( &record );
    }
}

static uint32_t
casic_checksum( uint8_t a_class, uint8_t a_id, const uint8_t *a_data, size_t a_size )
{
    uint32_t sum = ( (uint32_t)a_id << 24U ) | ( (uint32_t)a_class << 16U ) | (uint32_t)a_size;
    for( size_t index = 0U; index < a_size; index += 4U )
    {
        sum += (uint32_t)a_data[ index ] | ( (uint32_t)a_data[ index + 1U ] << 8U ) |
               ( (uint32_t)a_data[ index + 2U ] << 16U ) |
               ( (uint32_t)a_data[ index + 3U ] << 24U );
    }
    return sum;
}

static bool policy_send( uint8_t a_class, uint8_t a_id, const uint8_t *a_payload, size_t a_size )
{
    if( a_size > GPS_CASIC_MAX_PAYLOAD || 0U != ( a_size & 3U ) )
    {
        return false;
    }
    uint8_t frame[ GPS_CASIC_MAX_PAYLOAD + 10U ] = {
        0xBA, 0xCE, (uint8_t)a_size, (uint8_t)( a_size >> 8U ), a_class, a_id };
    if( a_size > 0U )
    {
        memcpy( frame + 6U, a_payload, a_size );
    }
    const uint32_t checksum = casic_checksum( a_class, a_id, frame + 6U, a_size );
    for( size_t index = 0U; index < 4U; ++index )
    {
        frame[ a_size + 6U + index ] = (uint8_t)( checksum >> ( 8U * index ) );
    }
    const int written = uart_write_bytes( GPS_UART, frame, a_size + 10U );
    if( written == (int)( a_size + 10U ) )
    {
        const esp_err_t completed = uart_wait_tx_done( GPS_UART, pdMS_TO_TICKS( 200 ) );
        if( ESP_OK != completed )
        {
            ESP_LOGW( TAG, "GPS CASIC UART TX drain failed: %s", esp_err_to_name( completed ) );
            return false;
        }
    }
    if( written != (int)( a_size + 10U ) )
    {
        ESP_LOGW( TAG, "GPS CASIC command %02X/%02X send failed (%d bytes)", a_class, a_id,
                  written );
    }
    return written == (int)( a_size + 10U );
}

/* Switch only the UART profile that matches our detected baud. A read-only
   discovery response may contain other receiver ports with unrelated settings. */
static void policy_offer_baud_switch( const uint8_t *a_data, size_t a_size )
{
    if( 8U != a_size || GPS_BAUD_MAX == active_baud || policy_baud_attempted )
    {
        return;
    }
    const uint32_t reported_baud = (uint32_t)a_data[ 4 ] |
                                   ( (uint32_t)a_data[ 5 ] << 8U ) |
                                   ( (uint32_t)a_data[ 6 ] << 16U ) |
                                   ( (uint32_t)a_data[ 7 ] << 24U );
    if( reported_baud != (uint32_t)active_baud || a_data[ 0 ] > 1U )
    {
        return;
    }
    memcpy( policy_uart_config, a_data, sizeof( policy_uart_config ) );
    policy_uart_config_valid = true;
    ESP_LOGI( TAG, "GPS CASIC selected UART port %u at %d baud", a_data[ 0 ], active_baud );
}

static bool policy_begin_baud_switch( void )
{
    if( !policy_uart_config_valid || policy_baud_attempted || GPS_BAUD_MAX == active_baud )
    {
        return false;
    }
    policy_baud_attempted = true;
    policy_baud_previous = active_baud;
    policy_uart_config[ 4 ] = (uint8_t)GPS_BAUD_MAX;
    policy_uart_config[ 5 ] = (uint8_t)( GPS_BAUD_MAX >> 8U );
    policy_uart_config[ 6 ] = (uint8_t)( GPS_BAUD_MAX >> 16U );
    policy_uart_config[ 7 ] = (uint8_t)( GPS_BAUD_MAX >> 24U );
    ESP_LOGI( TAG, "GPS switching UART from %d to %d baud", active_baud, GPS_BAUD_MAX );
    policy_log_event( "baud_change_requested", GPS_BAUD_MAX, active_baud );
    if( !policy_send( 0x06U, 0x00U, policy_uart_config, sizeof( policy_uart_config ) ) )
    {
        ESP_LOGW( TAG, "GPS UART baud request failed; staying at %d", active_baud );
        policy_log_event( "baud_change_failed", GPS_BAUD_MAX, active_baud );
        return false;
    }
    if( ESP_OK != uart_set_baudrate( GPS_UART, GPS_BAUD_MAX ) )
    {
        ESP_LOGW( TAG, "GPS UART could not switch to %d; baud probing resumes", GPS_BAUD_MAX );
        policy_log_event( "baud_change_failed", GPS_BAUD_MAX, active_baud );
        return false;
    }
    active_baud = GPS_BAUD_MAX;
    (void)uart_flush_input( GPS_UART );
    casic_length = 0U;
    casic_expected = 0U;
    line_length = 0U;
    policy_baud_switch_us = esp_timer_get_time();
    policy_deadline_us = policy_baud_switch_us + GPS_BAUD_SWITCH_TIMEOUT_US;
    policy_state = GPS_POLICY_WAIT_BAUD;
    return true;
}

static void policy_query_rate( void )
{
    policy_rate_query_retries = 0U;
    policy_rate_query_retry_at_us = 0;
    policy_state = GPS_POLICY_WAIT_RATE;
    policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
    ESP_LOGI( TAG, "GPS CASIC reading initial update rate" );
    policy_log_event( "rate_query", 0, policy_current_interval_ms );
    if( !policy_send( 0x06U, 0x04U, NULL, 0U ) )
    {
        policy_state = GPS_POLICY_UNSUPPORTED;
    }
}

static void policy_start_probe( void )
{
    policy_state = GPS_POLICY_WAIT_VERSION;
    policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
    ESP_LOGI( TAG, "GPS CASIC checking %s", policy_probe_names[ policy_probe_index ] );
    if( !policy_send( policy_probe_classes[ policy_probe_index ], policy_probe_ids[ policy_probe_index ],
                      NULL, 0U ) )
    {
        policy_state = GPS_POLICY_UNSUPPORTED;
    }
}

static void policy_next_probe( void )
{
    policy_probe_retries = 0U;
    policy_probe_retry_at_us = 0;
    if( policy_probe_index + 1U < sizeof( policy_probe_ids ) )
    {
        ++policy_probe_index;
        policy_start_probe();
    }
    else
    {
        ESP_LOGW( TAG, "GPS CASIC discovery exhausted; generic NMEA retained" );
        policy_log_event( "discovery_failed", 0, 0 );
        policy_state = GPS_POLICY_UNSUPPORTED;
    }
}

static void policy_begin( void )
{
    policy_probe_index = 0U;
    policy_probe_retries = 0U;
    policy_probe_retry_at_us = 0;
    policy_rate_query_retries = 0U;
    policy_rate_query_retry_at_us = 0;
    policy_uart_config_valid = false;
    ESP_LOGI( TAG, "GPS receiver detected at %d baud; checking CASIC support", active_baud );
    policy_log_event( "receiver_detected", 0, 0 );
    policy_start_probe();
}

static void policy_response( uint8_t a_class, uint8_t a_id, const uint8_t *a_data, size_t a_size )
{
    /* CFG-PRT may emit multiple port profiles. Retain the one matching
       the live UART even if its response arrives after discovery advanced. */
    if( 0x06U == a_class && 0x00U == a_id && 8U == a_size )
    {
        policy_offer_baud_switch( a_data, a_size );
    }
    if( 0x05U == a_class && 0x00U == a_id && 4U == a_size )
    {
        const uint8_t rejected_class = a_data[ 0 ];
        const uint8_t rejected_id = a_data[ 1 ];
        ESP_LOGI( TAG, "GPS CASIC command %02X/%02X unsupported", rejected_class,
                  rejected_id );
        if( GPS_POLICY_WAIT_VERSION == policy_state &&
            rejected_class == policy_probe_classes[ policy_probe_index ] &&
            rejected_id == policy_probe_ids[ policy_probe_index ] )
        {
            policy_next_probe();
        }
        else if( ( GPS_POLICY_WAIT_RATE == policy_state ||
                   GPS_POLICY_VERIFY_RATE == policy_state ) &&
                 0x06U == rejected_class && 0x04U == rejected_id )
        {
            ESP_LOGW( TAG, "GPS CASIC CFG-RATE poll rejected; rate control disabled" );
            policy_log_event( "rate_query_rejected", 0, 0 );
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
        else if( GPS_POLICY_WAIT_ACK == policy_state && 0x06U == rejected_class &&
                 0x04U == rejected_id )
        {
            ESP_LOGW( TAG, "GPS CASIC CFG-RATE write rejected; rate control disabled" );
            policy_log_event( "rate_write_rejected", policy_pending_interval_ms,
                              policy_current_interval_ms );
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
        return;
    }

    if( GPS_POLICY_WAIT_VERSION == policy_state &&
        a_class == policy_probe_classes[ policy_probe_index ] &&
        a_id == policy_probe_ids[ policy_probe_index ] )
    {
        if( 0U == policy_probe_index && 64U == a_size )
        {
            char software[ 33 ];
            char hardware[ 33 ];
            memcpy( software, a_data, 32U );
            memcpy( hardware, a_data + 32U, 32U );
            software[ 32 ] = '\0';
            hardware[ 32 ] = '\0';
            ESP_LOGI( TAG, "GPS CASIC identified: software=%s hardware=%s", software, hardware );
            policy_log_event( "transport_detected", 0, 0 );
            policy_query_rate();
        }
        else if( 2U == policy_probe_index && 8U == a_size )
        {
            ESP_LOGI( TAG, "GPS CASIC CFG-PRT answered; CASIC transport confirmed" );
            policy_log_event( "transport_detected", 0, 0 );
            policy_query_rate();
        }
        else if( 1U == policy_probe_index )
        {
            ESP_LOGI( TAG, "GPS CASIC transport confirmed through MON-HW" );
            policy_log_event( "transport_detected", 0, 0 );
            policy_query_rate();
        }
        else
        {
            ESP_LOGW( TAG, "GPS CASIC unexpected %s payload=%u; trying next probe",
                      policy_probe_names[ policy_probe_index ], (unsigned)a_size );
            policy_next_probe();
        }
    }
    else if( 0x06U == a_class && 0x04U == a_id && GPS_POLICY_WAIT_RATE == policy_state &&
             4U == a_size )
    {
        const unsigned interval = (unsigned)a_data[ 0 ] | ( (unsigned)a_data[ 1 ] << 8U );
        ESP_LOGI( TAG, "GPS CASIC CFG-RATE interval=%u ms", interval );
        if( interval < 100U || interval > 10000U )
        {
            ESP_LOGW( TAG, "GPS CASIC implausible update interval; rate control disabled" );
            policy_log_event( "rate_query_invalid", 0, (int)interval );
            policy_state = GPS_POLICY_UNSUPPORTED;
            return;
        }
        policy_current_interval_ms = (int)interval;
        policy_desired_interval_ms = (int)interval;
        policy_speed_since_us = 0;
        policy_state = GPS_POLICY_ACTIVE;
        ESP_LOGI( TAG, "GPS adaptive rate enabled: current=%u ms", interval );
        policy_log_event( "rate_detected", 0, (int)interval );
        (void)policy_begin_baud_switch();
    }
    else if( 0x06U == a_class && 0x04U == a_id &&
             GPS_POLICY_VERIFY_RATE == policy_state && 4U == a_size )
    {
        const unsigned interval = (unsigned)a_data[ 0 ] | ( (unsigned)a_data[ 1 ] << 8U );
        ESP_LOGI( TAG, "GPS CASIC CFG-RATE readback=%u ms requested=%d ms", interval,
                  policy_pending_interval_ms );
        if( interval != (unsigned)policy_pending_interval_ms )
        {
            ESP_LOGW( TAG, "GPS CASIC rate change did not take effect; adaptive control disabled" );
            policy_log_event( "rate_verify_mismatch", policy_pending_interval_ms, (int)interval );
            policy_state = GPS_POLICY_UNSUPPORTED;
            return;
        }
        policy_current_interval_ms = (int)interval;
        policy_state = GPS_POLICY_ACTIVE;
        policy_log_event( "rate_verified", policy_pending_interval_ms, (int)interval );
        ESP_LOGI( TAG, "GPS CASIC update rate verified at %u Hz (%u ms)",
                  interval > 0U ? 1000U / interval : 0U, interval );
    }
    else if( 0x05U == a_class && 0x01U == a_id && GPS_POLICY_WAIT_ACK == policy_state &&
             4U == a_size && 0x06U == a_data[ 0 ] && 0x04U == a_data[ 1 ] )
    {
        policy_log_event( "rate_write_ack", policy_pending_interval_ms, policy_current_interval_ms );
        ESP_LOGI( TAG, "GPS CASIC CFG-RATE write acknowledged: requested=%d ms; verifying",
                  policy_pending_interval_ms );
        policy_state = GPS_POLICY_VERIFY_RATE;
        policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
        if( !policy_send( 0x06U, 0x04U, NULL, 0U ) )
        {
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
}

static void casic_feed( uint8_t a_byte )
{
    if( 0U == casic_length && 0xBA != a_byte )
    {
        return;
    }
    if( 1U == casic_length && 0xCE != a_byte )
    {
        casic_length = 0U;
        casic_expected = 0U;
        return;
    }
    casic_frame[ casic_length++ ] = a_byte;
    if( 4U == casic_length )
    {
        const size_t payload = (size_t)casic_frame[ 2 ] | ( (size_t)casic_frame[ 3 ] << 8U );
        if( payload > GPS_CASIC_MAX_PAYLOAD || 0U != ( payload & 3U ) )
        {
            ESP_LOGW( TAG, "GPS CASIC invalid payload size=%u", (unsigned)payload );
            casic_length = 0U;
            casic_expected = 0U;
            return;
        }
        casic_expected = payload + 10U;
    }
    if( casic_expected >= 10U && casic_length == casic_expected )
    {
        const size_t payload = casic_length - 10U;
        const uint32_t expected =
            casic_checksum( casic_frame[ 4 ], casic_frame[ 5 ], casic_frame + 6U, payload );
        uint32_t received = 0U;
        for( size_t index = 0U; index < 4U; ++index )
        {
            received |= (uint32_t)casic_frame[ 6U + payload + index ] << ( 8U * index );
        }
        if( expected == received )
        {
            policy_response( casic_frame[ 4 ], casic_frame[ 5 ], casic_frame + 6U, payload );
        }
        else
        {
            ESP_LOGW( TAG,
                      "GPS CASIC checksum mismatch class=%02X id=%02X",
                      casic_frame[ 4 ],
                      casic_frame[ 5 ] );
        }
        casic_length = 0U;
        casic_expected = 0U;
    }
}

static void policy_poll( int64_t a_now )
{
    if( GPS_POLICY_WAIT_BAUD == policy_state && a_now >= policy_deadline_us )
    {
        ESP_LOGW( TAG, "GPS no NMEA at %d baud; returning to discovery", active_baud );
        policy_log_event( "baud_fallback", policy_baud_previous, active_baud );
        policy_state = GPS_POLICY_IDLE;
        policy_baud_attempted = true;
        baud_locked = false;
        receiver_detected = false;
        baud_probe_started_us = a_now - (int64_t)GPS_BAUD_PROBE_MS * 1000LL;
        snapshot.available = false;
        snapshot.location_valid = false;
        policy_uart_config_valid = false;
    }
    if( GPS_POLICY_WAIT_RATE == policy_state && 0 != policy_rate_query_retry_at_us &&
        a_now >= policy_rate_query_retry_at_us )
    {
        policy_rate_query_retry_at_us = 0;
        policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
        ESP_LOGI( TAG, "GPS CASIC retrying initial update-rate query" );
        if( !policy_send( 0x06U, 0x04U, NULL, 0U ) )
        {
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
    if( GPS_POLICY_WAIT_VERSION == policy_state && 0 != policy_probe_retry_at_us &&
        a_now >= policy_probe_retry_at_us )
    {
        policy_probe_retry_at_us = 0;
        policy_start_probe();
    }
    if( ( GPS_POLICY_WAIT_VERSION == policy_state || GPS_POLICY_WAIT_RATE == policy_state ||
          GPS_POLICY_WAIT_ACK == policy_state || GPS_POLICY_VERIFY_RATE == policy_state ) &&
        a_now >= policy_deadline_us )
    {
        if( GPS_POLICY_WAIT_VERSION == policy_state )
        {
            if( policy_probe_retries < GPS_POLICY_PROBE_RETRY_LIMIT )
            {
                ++policy_probe_retries;
                ESP_LOGW( TAG, "GPS CASIC %s timeout; retry %u/%u",
                          policy_probe_names[ policy_probe_index ], policy_probe_retries,
                          GPS_POLICY_PROBE_RETRY_LIMIT );
                policy_log_event( "discovery_retry", (int)policy_probe_index,
                                  (int)policy_probe_retries );
                policy_probe_retry_at_us = a_now + GPS_POLICY_PROBE_RETRY_DELAY_US;
                policy_deadline_us = policy_probe_retry_at_us + GPS_POLICY_TIMEOUT_US;
            }
            else
            {
                ESP_LOGW( TAG, "GPS CASIC %s unavailable after retries",
                          policy_probe_names[ policy_probe_index ] );
                policy_next_probe();
            }
        }
        else if( GPS_POLICY_WAIT_RATE == policy_state &&
                 policy_rate_query_retries < GPS_POLICY_PROBE_RETRY_LIMIT )
        {
            ++policy_rate_query_retries;
            ESP_LOGW( TAG, "GPS CASIC rate query timeout; retry %u/%u",
                      policy_rate_query_retries, GPS_POLICY_PROBE_RETRY_LIMIT );
            policy_log_event( "rate_query_retry", (int)policy_rate_query_retries,
                              policy_current_interval_ms );
            policy_rate_query_retry_at_us = a_now + GPS_POLICY_PROBE_RETRY_DELAY_US;
            policy_deadline_us = policy_rate_query_retry_at_us + GPS_POLICY_TIMEOUT_US;
        }
        else
        {
            policy_log_event( GPS_POLICY_WAIT_RATE == policy_state ? "rate_query_timeout" :
                              GPS_POLICY_WAIT_ACK == policy_state ? "rate_ack_timeout" :
                              "rate_verify_timeout", policy_pending_interval_ms,
                              policy_current_interval_ms );
            ESP_LOGW( TAG, "GPS CASIC policy timeout state=%d; generic NMEA retained",
                      (int)policy_state );
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
    if( GPS_POLICY_ACTIVE != policy_state || !policy_filtered_speed_valid ||
        a_now - policy_filtered_speed_updated_us > (int64_t)GPS_FIX_STALE_MS * 1000LL )
    {
        policy_speed_since_us = 0;
        return;
    }
    const float mph = (float)( policy_filtered_speed_mps * 2.23693629 );
    /* Keep the upper bound at 4 Hz while UART remains at the detected 9600 baud.
       All thresholds use the same stability and cooldown checks below. */
    const int target = mph >= 12.0f ? 250 : mph >= 7.0f ? 333 : mph >= 3.0f ? 500 : 1000;
    if( target != policy_desired_interval_ms )
    {
        policy_desired_interval_ms = target;
        policy_speed_since_us = a_now;
    }
    if( target == policy_current_interval_ms )
    {
        return;
    }
    const int64_t stable_us = target < policy_current_interval_ms ? 5000000LL : 20000000LL;
    if( 0 == policy_speed_since_us )
    {
        policy_speed_since_us = a_now;
    }
    if( a_now - policy_speed_since_us < stable_us ||
        ( 0 != policy_last_change_us && a_now - policy_last_change_us < GPS_POLICY_MIN_CHANGE_US ) )
    {
        return;
    }
    const uint8_t rate[ 4 ] = { (uint8_t)target, (uint8_t)( target >> 8U ), 0U, 0U };
    if( policy_send( 0x06, 0x04, rate, sizeof( rate ) ) )
    {
        policy_pending_interval_ms = target;
        policy_last_change_us = a_now;
        policy_state = GPS_POLICY_WAIT_ACK;
        policy_deadline_us = a_now + GPS_POLICY_TIMEOUT_US;
        policy_log_event( "rate_requested", target, policy_current_interval_ms );
        ESP_LOGI( TAG, "GPS HT1818Z3G5L rate change requested %d -> %d ms at %.1f mph",
                  policy_current_interval_ms, target, (double)mph );
    }
}

static bool trip_loaded;
static double trip_total_m;
static double trip_saved_m;
static bool trip_anchor_valid;
static double trip_anchor_latitude;
static double trip_anchor_longitude;
static int64_t trip_anchor_us;
static int64_t trip_last_checkpoint_us;

static void trip_load( void )
{
    if( trip_loaded )
    {
        return;
    }
    trip_loaded = true;
    gps_trip_record_t stored = { 0 };
    size_t length = 0U;
    if( ESP_OK == system_store_read( GPS_TRIP_STORE_KEY, &stored, sizeof( stored ), &length ) &&
        sizeof( stored ) == length && GPS_TRIP_STORE_VERSION == stored.version &&
        isfinite( stored.distance_m ) && stored.distance_m >= 0.0 )
    {
        trip_total_m = stored.distance_m;
    }
    trip_saved_m = trip_total_m;
    trip_last_checkpoint_us = esp_timer_get_time();
}

static void trip_checkpoint( int64_t a_now_us )
{
    if( trip_total_m - trip_saved_m < GPS_TRIP_CHECKPOINT_DISTANCE_M ||
        a_now_us - trip_last_checkpoint_us < GPS_TRIP_CHECKPOINT_INTERVAL_US )
    {
        return;
    }
    trip_last_checkpoint_us = a_now_us;
    const gps_trip_record_t record = { .version = GPS_TRIP_STORE_VERSION,
                                       .distance_m = trip_total_m };
    if( ESP_OK == system_store_write( GPS_TRIP_STORE_KEY, &record, sizeof( record ) ) )
    {
        trip_saved_m = record.distance_m;
    }
}

/* Count accepted RMC positions only: GGA and RMC can describe the same fix.
   Require movement reported by the receiver and reject jitter, jumps and fix gaps. */
static void trip_observe( double a_latitude, double a_longitude, double a_speed_mps )
{
    const int64_t now = esp_timer_get_time();
    if( a_speed_mps < GPS_TRIP_MIN_SPEED_MPS || a_speed_mps > GPS_TRIP_MAX_SPEED_MPS )
    {
        trip_anchor_valid = false;
        return;
    }
    if( !trip_anchor_valid || now <= trip_anchor_us || now - trip_anchor_us > GPS_TRIP_MAX_GAP_US )
    {
        trip_anchor_valid = true;
        trip_anchor_latitude = a_latitude;
        trip_anchor_longitude = a_longitude;
        trip_anchor_us = now;
        return;
    }

    const double lat1 = trip_anchor_latitude * GPS_RADIANS_PER_DEGREE;
    const double lat2 = a_latitude * GPS_RADIANS_PER_DEGREE;
    const double half_lat = ( lat2 - lat1 ) / 2.0;
    const double half_lon = ( a_longitude - trip_anchor_longitude ) * GPS_RADIANS_PER_DEGREE / 2.0;
    const double haversine = sin( half_lat ) * sin( half_lat ) +
                             cos( lat1 ) * cos( lat2 ) * sin( half_lon ) * sin( half_lon );
    const double bounded = fmin( 1.0, fmax( 0.0, haversine ) );
    const double step_m = 2.0 * GPS_EARTH_RADIUS_M * asin( sqrt( bounded ) );
    if( step_m < GPS_TRIP_MIN_STEP_M )
    {
        return;
    }

    const double elapsed_seconds = (double)( now - trip_anchor_us ) / 1000000.0;
    trip_anchor_latitude = a_latitude;
    trip_anchor_longitude = a_longitude;
    trip_anchor_us = now;
    if( step_m > GPS_TRIP_MAX_SPEED_MPS * elapsed_seconds + 15.0 )
    {
        return;
    }
    trip_total_m += step_m;
    snapshot.trip_distance_m = trip_total_m;
    trip_checkpoint( now );
}

static void publish_snapshot( void )
{
    portENTER_CRITICAL( &snapshot_mux );
    published_snapshot = snapshot;
    published_location_us = location_updated_us;
    published_time_us = time_updated_us;
    published_initialized = initialized;
    portEXIT_CRITICAL( &snapshot_mux );
}

static bool checksum_valid( const char *a_line )
{
    if( NULL == a_line || '$' != a_line[ 0 ] )
    {
        return false;
    }
    const char *star = strchr( a_line, '*' );
    if( NULL == star || strlen( star ) < 3U )
    {
        return false;
    }
    unsigned checksum = 0U;
    for( const char *cursor = a_line + 1; cursor < star; ++cursor )
    {
        checksum ^= (unsigned char)*cursor;
    }
    char *end = NULL;
    const unsigned expected = (unsigned)strtoul( star + 1, &end, 16 );
    return end == star + 3 && checksum == expected;
}

static bool parse_double_field( const char *a_text, double *a_value )
{
    if( NULL == a_text || NULL == a_value || '\0' == a_text[ 0 ] )
    {
        return false;
    }
    char *end = NULL;
    const double value = strtod( a_text, &end );
    if( end == a_text || '\0' != *end || !isfinite( value ) )
    {
        return false;
    }
    *a_value = value;
    return true;
}

static bool parse_int_field( const char *a_text, long *a_value )
{
    if( NULL == a_text || NULL == a_value || '\0' == a_text[ 0 ] )
    {
        return false;
    }
    char *end = NULL;
    const long value = strtol( a_text, &end, 10 );
    if( end == a_text || '\0' != *end )
    {
        return false;
    }
    *a_value = value;
    return true;
}

static bool
nmea_coordinate( const char *a_value, char a_hemisphere, bool a_latitude, double *a_result )
{
    if( NULL == a_result || ( a_latitude && 'N' != a_hemisphere && 'S' != a_hemisphere ) ||
        ( !a_latitude && 'E' != a_hemisphere && 'W' != a_hemisphere ) )
    {
        return false;
    }

    double raw;
    if( !parse_double_field( a_value, &raw ) || raw < 0.0 )
    {
        return false;
    }

    const double degrees = floor( raw / 100.0 );
    const double minutes = raw - degrees * 100.0;
    const double maximum_degrees = a_latitude ? 90.0 : 180.0;
    if( minutes < 0.0 || minutes >= 60.0 || degrees > maximum_degrees ||
        ( degrees == maximum_degrees && minutes > 0.0 ) )
    {
        return false;
    }

    double result = degrees + minutes / 60.0;
    if( 'S' == a_hemisphere || 'W' == a_hemisphere )
    {
        result = -result;
    }
    *a_result = result;
    return true;
}

static int64_t days_from_civil( int a_year, unsigned a_month, unsigned a_day )
{
    a_year -= a_month <= 2U;
    const int era = ( a_year >= 0 ? a_year : a_year - 399 ) / 400;
    const unsigned year_of_era = (unsigned)( a_year - era * 400 );
    const int adjusted_month = (int)a_month + ( a_month > 2U ? -3 : 9 );
    const unsigned day_of_year = ( 153U * (unsigned)adjusted_month + 2U ) / 5U + a_day - 1U;
    const unsigned day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static bool all_digits( const char *a_text, size_t a_count )
{
    if( NULL == a_text )
    {
        return false;
    }
    for( size_t index = 0U; index < a_count; ++index )
    {
        if( a_text[ index ] < '0' || a_text[ index ] > '9' )
        {
            return false;
        }
    }
    return true;
}

static bool leap_year( int a_year )
{
    return ( 0 == ( a_year % 4 ) ) && ( ( 0 != ( a_year % 100 ) ) || ( 0 == ( a_year % 400 ) ) );
}

static unsigned days_in_month( int a_year, unsigned a_month )
{
    static const uint8_t DAYS[] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };
    if( a_month < 1U || a_month > 12U )
    {
        return 0U;
    }
    if( 2U == a_month && leap_year( a_year ) )
    {
        return 29U;
    }
    return DAYS[ a_month - 1U ];
}

static bool parse_utc_epoch( const char *a_time, const char *a_date, time_t *a_epoch )
{
    if( NULL == a_time || NULL == a_date || NULL == a_epoch || strlen( a_time ) < 6U ||
        strlen( a_date ) != 6U || !all_digits( a_time, 6U ) || !all_digits( a_date, 6U ) )
    {
        return false;
    }
    const int hour = ( a_time[ 0 ] - '0' ) * 10 + ( a_time[ 1 ] - '0' );
    const int minute = ( a_time[ 2 ] - '0' ) * 10 + ( a_time[ 3 ] - '0' );
    const int second = ( a_time[ 4 ] - '0' ) * 10 + ( a_time[ 5 ] - '0' );
    const unsigned day = (unsigned)( ( a_date[ 0 ] - '0' ) * 10 + ( a_date[ 1 ] - '0' ) );
    const unsigned month = (unsigned)( ( a_date[ 2 ] - '0' ) * 10 + ( a_date[ 3 ] - '0' ) );
    const int yy = ( a_date[ 4 ] - '0' ) * 10 + ( a_date[ 5 ] - '0' );
    const int year = yy >= 80 ? 1900 + yy : 2000 + yy;
    if( hour > 23 || minute > 59 || second > 60 || month < 1U || month > 12U || year < 2020 ||
        year > 2099 || day < 1U || day > days_in_month( year, month ) )
    {
        return false;
    }
    *a_epoch = (time_t)( days_from_civil( year, month, day ) * 86400LL + hour * 3600LL +
                         minute * 60LL + second );
    return true;
}

static size_t split_fields( char *a_payload, char *a_fields[], size_t a_capacity )
{
    size_t count = 0U;
    char *cursor = a_payload;
    while( NULL != cursor && count < a_capacity )
    {
        a_fields[ count++ ] = cursor;
        char *comma = strchr( cursor, ',' );
        if( NULL == comma )
        {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

static void parse_rmc( char *a_payload )
{
    char *fields[ 16 ];
    const size_t count = split_fields( a_payload, fields, 16U );
    if( count < 9U || 'A' != fields[ 1 ][ 0 ] )
    {
        snapshot.speed_valid = false;
        trip_anchor_valid = false;
        return;
    }

    double latitude;
    double longitude;
    const bool valid_position = nmea_coordinate( fields[ 2 ], fields[ 3 ][ 0 ], true, &latitude ) &&
                                nmea_coordinate( fields[ 4 ], fields[ 5 ][ 0 ], false, &longitude );
    if( valid_position )
    {
        snapshot.latitude_deg = latitude;
        snapshot.longitude_deg = longitude;
        snapshot.location_valid = true;
        location_updated_us = esp_timer_get_time();
    }

    double speed_knots;
    const bool valid_speed = parse_double_field( fields[ 6 ], &speed_knots ) && speed_knots >= 0.0;
    snapshot.speed_valid = valid_speed;
    if( valid_speed )
    {
        snapshot.speed_mps = (float)( speed_knots * 0.514444 );
        speed_updated_us = esp_timer_get_time();
        /* 0.9 old + 0.1 new at 1 Hz, normalized for faster NMEA intervals. */
        if( !policy_filtered_speed_valid ||
            speed_updated_us - policy_filtered_speed_updated_us >
                (int64_t)GPS_FIX_STALE_MS * 1000LL )
        {
            policy_filtered_speed_mps = speed_knots * 0.514444;
        }
        else
        {
            const double elapsed_seconds =
                (double)( speed_updated_us - policy_filtered_speed_updated_us ) / 1000000.0;
            const double retention = pow( 0.9, elapsed_seconds );
            policy_filtered_speed_mps =
                retention * policy_filtered_speed_mps +
                ( 1.0 - retention ) * speed_knots * 0.514444;
        }
        policy_filtered_speed_updated_us = speed_updated_us;
        policy_filtered_speed_valid = true;
    }
    if( valid_position && valid_speed )
    {
        trip_observe( latitude, longitude, (double)snapshot.speed_mps );
    }
    else
    {
        trip_anchor_valid = false;
    }

    time_t epoch;
    if( parse_utc_epoch( fields[ 0 ], fields[ 8 ], &epoch ) )
    {
        snapshot.utc_epoch = epoch;
        snapshot.time_valid = true;
        time_updated_us = esp_timer_get_time();
        (void)device_time_offer_gps( epoch );
    }
}

static void parse_gga( char *a_payload )
{
    char *fields[ 16 ];
    const size_t count = split_fields( a_payload, fields, 16U );
    long fix_quality;
    if( count < 9U || !parse_int_field( fields[ 5 ], &fix_quality ) || fix_quality <= 0 )
    {
        snapshot.altitude_valid = false;
        snapshot.satellites_valid = false;
        return;
    }

    double latitude;
    double longitude;
    if( nmea_coordinate( fields[ 1 ], fields[ 2 ][ 0 ], true, &latitude ) &&
        nmea_coordinate( fields[ 3 ], fields[ 4 ][ 0 ], false, &longitude ) )
    {
        snapshot.latitude_deg = latitude;
        snapshot.longitude_deg = longitude;
        snapshot.location_valid = true;
        location_updated_us = esp_timer_get_time();
    }

    long satellites;
    snapshot.satellites_valid = parse_int_field( fields[ 6 ], &satellites );
    if( snapshot.satellites_valid )
    {
        snapshot.satellites =
            (uint8_t)( satellites < 0 ? 0 : ( satellites > 255 ? 255 : satellites ) );
        satellites_updated_us = esp_timer_get_time();
    }

    double altitude;
    snapshot.altitude_valid = parse_double_field( fields[ 8 ], &altitude );
    if( snapshot.altitude_valid )
    {
        snapshot.altitude_m = (float)altitude;
        altitude_updated_us = esp_timer_get_time();
    }
}

static void lock_active_baud( void )
{
    if( baud_locked )
    {
        return;
    }
    baud_locked = true;
    receiver_detected = true;
    last_nmea_us = esp_timer_get_time();
    initial_probe_complete = true;
    ESP_LOGI( TAG, "GPS NMEA detected at %d baud", active_baud );
    if( GPS_POLICY_IDLE == policy_state )
    {
        policy_begin();
    }
}

static void parse_line( char *a_line )
{
    if( !checksum_valid( a_line ) )
    {
        return;
    }
    char *star = strchr( a_line, '*' );
    *star = '\0';
    char *payload = a_line + 1;
    char *comma = strchr( payload, ',' );
    if( NULL == comma )
    {
        return;
    }
    *comma = '\0';
    const size_t type_length = strlen( payload );
    if( type_length >= 3U && 0 == strcmp( payload + type_length - 3U, "RMC" ) )
    {
        lock_active_baud();
        last_nmea_us = esp_timer_get_time();
        snapshot.available = true;
        if( GPS_POLICY_WAIT_BAUD == policy_state &&
            esp_timer_get_time() - policy_baud_switch_us >= 100000LL )
        {
            ESP_LOGI( TAG, "GPS UART confirmed at %d baud", active_baud );
            policy_log_event( "baud_verified", GPS_BAUD_MAX, active_baud );
            policy_state = GPS_POLICY_ACTIVE;
        }
        parse_rmc( comma + 1 );
    }
    else if( type_length >= 3U && 0 == strcmp( payload + type_length - 3U, "GGA" ) )
    {
        lock_active_baud();
        last_nmea_us = esp_timer_get_time();
        snapshot.available = true;
        if( GPS_POLICY_WAIT_BAUD == policy_state &&
            esp_timer_get_time() - policy_baud_switch_us >= 100000LL )
        {
            ESP_LOGI( TAG, "GPS UART confirmed at %d baud", active_baud );
            policy_log_event( "baud_verified", GPS_BAUD_MAX, active_baud );
            policy_state = GPS_POLICY_ACTIVE;
        }
        parse_gga( comma + 1 );
    }
}

static void probe_next_baud( int64_t a_now )
{
    if( baud_locked || ( a_now - baud_probe_started_us ) / 1000LL < (int64_t)GPS_BAUD_PROBE_MS )
    {
        return;
    }

    const size_t next_index =
        ( active_baud_index + 1U ) % ( sizeof( GPS_BAUD_RATES ) / sizeof( GPS_BAUD_RATES[ 0 ] ) );
    const int next_baud = GPS_BAUD_RATES[ next_index ];
    if( ESP_OK != uart_set_baudrate( GPS_UART, next_baud ) )
    {
        baud_probe_started_us = a_now;
        return;
    }
    (void)uart_flush_input( GPS_UART );
    line_length = 0U;
    active_baud_index = next_index;
    active_baud = next_baud;
    baud_probe_started_us = a_now;
    ESP_LOGI( TAG, "probing GPS UART at %d baud", active_baud );
}

esp_err_t gps_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }
    trip_load();
    memset( &snapshot, 0, sizeof( snapshot ) );
    snapshot.trip_distance_m = trip_total_m;
    trip_anchor_valid = false;
    line_length = 0U;
    location_updated_us = 0;
    time_updated_us = 0;
    altitude_updated_us = 0;
    speed_updated_us = 0;
    satellites_updated_us = 0;
    baud_locked = false;
    policy_state = GPS_POLICY_IDLE;
    casic_length = 0U;
    casic_expected = 0U;
    policy_last_change_us = 0;
    policy_speed_since_us = 0;
    policy_probe_retries = 0U;
    policy_probe_retry_at_us = 0;
    policy_rate_query_retries = 0U;
    policy_rate_query_retry_at_us = 0;
    policy_uart_config_valid = false;
    policy_baud_attempted = false;
    policy_baud_switch_us = 0;
    policy_baud_previous = 0;
    policy_filtered_speed_mps = 0.0;
    policy_filtered_speed_updated_us = 0;
    policy_filtered_speed_valid = false;
    policy_desired_interval_ms = 1000;
    policy_current_interval_ms = 1000;
    receiver_detected = false;
    last_nmea_us = 0;
    /* Preserve the completed startup window across temporary radio ownership. */
    active_baud = GPS_BAUD_PRIMARY;
    active_baud_index = 0U;
    baud_probe_started_us = 0;
    initialized = false;
    publish_snapshot();
    if( !RFSCOUT_BOARD_HAS_GPS )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const uart_config_t config = {
        .baud_rate = GPS_BAUD_PRIMARY,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_driver_install( GPS_UART, GPS_RX_BUFFER, 0, 0, NULL, 0 );
    if( ESP_OK != result )
    {
        return result;
    }
    if( ESP_OK != ( result = uart_param_config( GPS_UART, &config ) ) ||
        ESP_OK != ( result = uart_set_pin( GPS_UART,
                                           RFSCOUT_BOARD_GPS_TX_PIN,
                                           RFSCOUT_BOARD_GPS_RX_PIN,
                                           UART_PIN_NO_CHANGE,
                                           UART_PIN_NO_CHANGE ) ) )
    {
        (void)uart_driver_delete( GPS_UART );
        return result;
    }
    baud_probe_started_us = esp_timer_get_time();
    initial_probe_started_us = baud_probe_started_us;
    initialized = true;
    publish_snapshot();
    ESP_LOGI( TAG,
              "GPS provider started: UART1 RX=%d TX=%d probing baud up to %d",
              RFSCOUT_BOARD_GPS_RX_PIN,
              RFSCOUT_BOARD_GPS_TX_PIN,
              GPS_BAUD_MAX );
    return ESP_OK;
}

esp_err_t gps_stop( void )
{
    if( !initialized )
    {
        return ESP_OK;
    }
    if( gps_detected() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = uart_driver_delete( GPS_UART );
    if( ESP_OK == result )
    {
        /* Deleting the UART driver does not grant the nRF24 ownership of the
           GPIO matrix. Return both pins to GPIO defaults before radio SPI setup. */
        const esp_err_t tx_result = gpio_reset_pin( RFSCOUT_BOARD_GPS_TX_PIN );
        const esp_err_t rx_result = gpio_reset_pin( RFSCOUT_BOARD_GPS_RX_PIN );
        if( ESP_OK != tx_result )
        {
            return tx_result;
        }
        if( ESP_OK != rx_result )
        {
            return rx_result;
        }
        initialized = false;
        trip_anchor_valid = false;
        publish_snapshot();
    }
    return result;
}

bool gps_detected( void )
{
    return receiver_detected;
}

bool gps_probe_complete( void )
{
    return initial_probe_complete;
}

void gps_poll( void )
{
    /* The odometer is platform state, not UART state. When nRF24 owns the shared
       pins, continue time-based flash checkpoints without probing the receiver. */
    trip_checkpoint( esp_timer_get_time() );
    if( !initialized )
    {
        return;
    }
    uint8_t buffer[ 128 ];
    const int received = uart_read_bytes( GPS_UART, buffer, sizeof( buffer ), 0 );
    for( int index = 0; index < received; ++index )
    {
        const char value = (char)buffer[ index ];
        if( 0U != casic_length || 0xBA == (uint8_t)value )
        {
            casic_feed( (uint8_t)value );
            continue;
        }
        if( '\n' == value )
        {
            line[ line_length ] = '\0';
            if( line_length > 0U )
            {
                parse_line( line );
            }
            line_length = 0U;
        }
        else if( '\r' != value )
        {
            if( line_length + 1U < sizeof( line ) )
            {
                line[ line_length++ ] = value;
            }
            else
            {
                line_length = 0U;
            }
        }
    }

    const int64_t now = esp_timer_get_time();
    if( !initial_probe_complete &&
        ( now - initial_probe_started_us ) / 1000LL >=
            (int64_t)( GPS_INITIAL_PROBE_CYCLES * GPS_BAUD_PROBE_MS *
                       ( sizeof( GPS_BAUD_RATES ) / sizeof( GPS_BAUD_RATES[ 0 ] ) ) ) )
    {
        initial_probe_complete = true;
    }
    if( receiver_detected &&
        ( now - last_nmea_us ) / 1000000LL >= (int64_t)device_config_get()->gps_timeout_seconds )
    {
        receiver_detected = false;
        baud_locked = false;
        policy_state = GPS_POLICY_IDLE;
        snapshot.available = false;
        snapshot.location_valid = false;
        snapshot.time_valid = false;
        snapshot.altitude_valid = false;
        snapshot.speed_valid = false;
        snapshot.satellites_valid = false;
        trip_anchor_valid = false;
        baud_probe_started_us = now;
        ESP_LOGI( TAG, "GPS NMEA timeout; shared pins eligible for radio acquisition" );
    }
    probe_next_baud( now );
    policy_poll( now );
    if( snapshot.location_valid &&
        ( now - location_updated_us ) / 1000LL > (int64_t)GPS_FIX_STALE_MS )
    {
        snapshot.location_valid = false;
    }
    if( snapshot.time_valid && ( now - time_updated_us ) / 1000LL > (int64_t)GPS_TIME_STALE_MS )
    {
        snapshot.time_valid = false;
    }
    if( snapshot.altitude_valid &&
        ( now - altitude_updated_us ) / 1000LL > (int64_t)GPS_FIX_STALE_MS )
    {
        snapshot.altitude_valid = false;
    }
    if( snapshot.speed_valid && ( now - speed_updated_us ) / 1000LL > (int64_t)GPS_FIX_STALE_MS )
    {
        snapshot.speed_valid = false;
    }
    if( snapshot.satellites_valid &&
        ( now - satellites_updated_us ) / 1000LL > (int64_t)GPS_FIX_STALE_MS )
    {
        snapshot.satellites_valid = false;
    }
    publish_snapshot();
}

bool gps_snapshot_get( rfscout_gps_snapshot_t *a_snapshot )
{
    if( NULL == a_snapshot )
        return false;
    int64_t location_us;
    int64_t time_us;
    bool ready;
    portENTER_CRITICAL( &snapshot_mux );
    ready = published_initialized && published_snapshot.available;
    if( ready )
    {
        *a_snapshot = published_snapshot;
        location_us = published_location_us;
        time_us = published_time_us;
    }
    portEXIT_CRITICAL( &snapshot_mux );
    if( !ready )
        return false;
    const int64_t now = esp_timer_get_time();
    a_snapshot->location_age_ms =
        location_us > 0 ? (uint32_t)( ( now - location_us ) / 1000LL ) : UINT32_MAX;
    a_snapshot->time_age_ms = time_us > 0 ? (uint32_t)( ( now - time_us ) / 1000LL ) : UINT32_MAX;
    return true;
}

bool gps_available( void )
{
    return initialized;
}

double gps_trip_distance_m( void )
{
    portENTER_CRITICAL( &snapshot_mux );
    const double distance = published_snapshot.trip_distance_m;
    portEXIT_CRITICAL( &snapshot_mux );
    return distance;
}

esp_err_t gps_trip_reset( void )
{
    /* Commit the reset before changing RAM so failure cannot resurrect a stale
       trip total at the next boot after showing zero on the current screen. */
    const gps_trip_record_t record = { .version = GPS_TRIP_STORE_VERSION, .distance_m = 0.0 };
    const esp_err_t result = system_store_write( GPS_TRIP_STORE_KEY, &record, sizeof( record ) );
    if( ESP_OK != result )
    {
        return result;
    }

    portENTER_CRITICAL( &snapshot_mux );
    trip_total_m = 0.0;
    trip_saved_m = 0.0;
    trip_anchor_valid = false;
    snapshot.trip_distance_m = 0.0;
    published_snapshot.trip_distance_m = 0.0;
    trip_last_checkpoint_us = esp_timer_get_time();
    portEXIT_CRITICAL( &snapshot_mux );
    return ESP_OK;
}

bool gps_rate_diagnostics_get( float *a_filtered_speed_mps, int *a_current_interval_ms,
                               int *a_target_interval_ms )
{
    if( !policy_filtered_speed_valid ||
        esp_timer_get_time() - policy_filtered_speed_updated_us >
            (int64_t)GPS_FIX_STALE_MS * 1000LL )
    {
        return false;
    }
    if( NULL != a_filtered_speed_mps )
    {
        *a_filtered_speed_mps = (float)policy_filtered_speed_mps;
    }
    if( NULL != a_current_interval_ms )
    {
        *a_current_interval_ms = policy_current_interval_ms;
    }
    if( NULL != a_target_interval_ms )
    {
        *a_target_interval_ms = policy_desired_interval_ms;
    }
    return true;
}
