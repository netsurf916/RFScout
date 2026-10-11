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

#define GPS_POLICY_TIMEOUT_US 2000000LL
#define GPS_POLICY_MIN_CHANGE_US 30000000LL
/* PMTK requests are allowed only after a valid NMEA stream locks UART ownership. */
typedef enum
{
    GPS_POLICY_IDLE = 0,
    GPS_POLICY_WAIT_VERSION,
    GPS_POLICY_WAIT_OUTPUT,
    GPS_POLICY_WAIT_RATE,
    GPS_POLICY_WAIT_OUTPUT_ACK,
    GPS_POLICY_ACTIVE,
    GPS_POLICY_UNSUPPORTED
} gps_policy_state_t;

static gps_policy_state_t policy_state;
static int64_t policy_deadline_us;
static int64_t policy_last_change_us;
static int64_t policy_speed_since_us;
static int policy_desired_interval_ms = 1000;
static int policy_current_interval_ms = 1000;
static int policy_pending_interval_ms;
static bool policy_rate_pending;
static bool policy_output_acknowledged;
static char policy_version[ 88 ];

static bool policy_send( const char *a_payload )
{
    char sentence[ 148 ];
    uint8_t checksum = 0U;
    for( const char *cursor = a_payload; '\0' != *cursor; ++cursor )
    {
        checksum ^= (uint8_t)*cursor;
    }
    const int length =
        snprintf( sentence, sizeof( sentence ), "$%s*%02X\r\n", a_payload, checksum );
    if( length <= 0 || (size_t)length >= sizeof( sentence ) )
    {
        return false;
    }
    const int written = uart_write_bytes( GPS_UART, sentence, (size_t)length );
    ESP_LOGI( TAG, "GPS policy TX %s (%d/%d bytes)", a_payload, written, length );
    return written == length;
}

static void policy_begin( void )
{
    policy_state = GPS_POLICY_WAIT_VERSION;
    policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
    ESP_LOGI( TAG, "GPS policy probing receiver at %d baud", active_baud );
    if( !policy_send( "PMTK605" ) )
    {
        policy_state = GPS_POLICY_UNSUPPORTED;
        ESP_LOGW( TAG, "GPS policy version query failed; generic NMEA only" );
    }
}

/* Called only for checksum-validated proprietary responses. */
static void policy_response( const char *a_type, const char *a_fields )
{
    if( 0 == strcmp( a_type, "PMTK705" ) && GPS_POLICY_WAIT_VERSION == policy_state )
    {
        snprintf( policy_version, sizeof( policy_version ), "%s", a_fields );
        ESP_LOGI( TAG, "GPS receiver PMTK firmware: %s", policy_version );
        policy_state = GPS_POLICY_WAIT_OUTPUT;
        policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
        if( !policy_send( "PMTK414" ) )
        {
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
    else if( 0 == strcmp( a_type, "PMTK514" ) && GPS_POLICY_WAIT_OUTPUT == policy_state )
    {
        ESP_LOGI( TAG, "GPS existing NMEA configuration: %s", a_fields );
        policy_state = GPS_POLICY_WAIT_RATE;
        policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
        if( !policy_send( "PMTK400" ) )
        {
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
    else if( 0 == strcmp( a_type, "PMTK500" ) && GPS_POLICY_WAIT_RATE == policy_state )
    {
        ESP_LOGI( TAG, "GPS existing fix interval: %s ms", a_fields );
        const int reported = atoi( a_fields );
        if( reported >= 100 && reported <= 10000 )
        {
            policy_current_interval_ms = reported;
        }
        /* Keep GGA/RMC each fix, GSA each fix and GSV once per five fixes.
           This is the MT3337 19-field PMTK314 layout. Never change UART baud. */
        policy_state = GPS_POLICY_WAIT_OUTPUT_ACK;
        policy_deadline_us = esp_timer_get_time() + GPS_POLICY_TIMEOUT_US;
        if( !policy_send( "PMTK314,0,1,0,1,1,5,0,0,0,0,0,0,0,0,0,0,0,0,0" ) )
        {
            policy_state = GPS_POLICY_UNSUPPORTED;
        }
    }
    else if( 0 == strcmp( a_type, "PMTK001" ) )
    {
        int command = 0;
        int result = 0;
        if( 2 == sscanf( a_fields, "%d,%d", &command, &result ) )
        {
            ESP_LOGI( TAG, "GPS PMTK acknowledgment command=%d result=%d", command, result );
            if( 314 == command && GPS_POLICY_WAIT_OUTPUT_ACK == policy_state )
            {
                policy_output_acknowledged = 3 == result;
                policy_state =
                    policy_output_acknowledged ? GPS_POLICY_ACTIVE : GPS_POLICY_UNSUPPORTED;
                ESP_LOGI( TAG,
                          "GPS policy NMEA configuration %s",
                          policy_output_acknowledged ? "accepted"
                                                     : "rejected; adaptive rate disabled" );
            }
            if( 220 == command && policy_rate_pending )
            {
                if( 3 == result )
                {
                    policy_current_interval_ms = policy_pending_interval_ms;
                }
                ESP_LOGI( TAG,
                          "GPS policy update interval result=%d configured=%d ms",
                          result,
                          policy_current_interval_ms );
                policy_rate_pending = false;
                if( 3 != result )
                {
                    policy_state = GPS_POLICY_UNSUPPORTED;
                    ESP_LOGW( TAG, "GPS policy rate rejected; adaptive changes disabled" );
                }
            }
        }
    }
}

static void policy_poll( int64_t a_now )
{
    if( GPS_POLICY_WAIT_VERSION <= policy_state && GPS_POLICY_WAIT_OUTPUT_ACK >= policy_state &&
        a_now >= policy_deadline_us )
    {
        ESP_LOGW( TAG, "GPS policy response timeout state=%d; retaining NMEA", (int)policy_state );
        policy_state = GPS_POLICY_UNSUPPORTED;
    }
    if( policy_rate_pending && a_now - policy_last_change_us >= GPS_POLICY_TIMEOUT_US )
    {
        ESP_LOGW( TAG,
                  "GPS policy rate acknowledgment timeout for %d ms; retaining last confirmed rate",
                  policy_pending_interval_ms );
        policy_rate_pending = false;
        policy_state = GPS_POLICY_UNSUPPORTED;
    }
    if( GPS_POLICY_ACTIVE != policy_state || !snapshot.speed_valid ||
        a_now - speed_updated_us > (int64_t)GPS_FIX_STALE_MS * 1000LL )
    {
        policy_speed_since_us = 0;
        return;
    }
    /* Speed hysteresis: 1 Hz below 3 mph, 2 Hz above 3, 5 Hz above 10,
       10 Hz only above 35 mph. Downshift requires a longer stable interval. */
    const float mph = snapshot.speed_mps * 2.23693629f;
    const int target = mph >= 35.0f && active_baud >= 38400   ? 100
                       : mph >= 10.0f && active_baud >= 19200 ? 200
                       : mph >= 3.0f                          ? 500
                                                              : 1000;
    if( target != policy_desired_interval_ms )
    {
        policy_desired_interval_ms = target;
        policy_speed_since_us = a_now;
    }
    if( target == policy_current_interval_ms || policy_rate_pending )
    {
        return;
    }
    const int64_t stable_us = target < policy_current_interval_ms ? 5000000LL : 20000000LL;
    if( 0 == policy_speed_since_us )
    {
        policy_speed_since_us = a_now;
    }
    if( a_now - policy_speed_since_us < stable_us ||
        ( 0 != policy_last_change_us &&
          a_now - policy_last_change_us < GPS_POLICY_MIN_CHANGE_US ) )
    {
        return;
    }
    char payload[ 32 ];
    snprintf( payload, sizeof( payload ), "PMTK220,%d", target );
    if( policy_send( payload ) )
    {
        policy_pending_interval_ms = target;
        policy_rate_pending = true;
        policy_last_change_us = a_now;
        ESP_LOGI( TAG, "GPS policy requested %d ms fix interval at %.1f mph", target, (double)mph );
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
    if( 0 == strncmp( payload, "PMTK", 4U ) )
    {
        policy_response( payload, comma + 1 );
        return;
    }
    if( type_length >= 3U && 0 == strcmp( payload + type_length - 3U, "RMC" ) )
    {
        lock_active_baud();
        last_nmea_us = esp_timer_get_time();
        snapshot.available = true;
        parse_rmc( comma + 1 );
    }
    else if( type_length >= 3U && 0 == strcmp( payload + type_length - 3U, "GGA" ) )
    {
        lock_active_baud();
        last_nmea_us = esp_timer_get_time();
        snapshot.available = true;
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
    policy_rate_pending = false;
    policy_output_acknowledged = false;
    policy_last_change_us = 0;
    policy_speed_since_us = 0;
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
        policy_rate_pending = false;
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
