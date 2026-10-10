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

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>
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
        return;
    }

    double latitude;
    double longitude;
    if( nmea_coordinate( fields[ 2 ], fields[ 3 ][ 0 ], true, &latitude ) &&
        nmea_coordinate( fields[ 4 ], fields[ 5 ][ 0 ], false, &longitude ) )
    {
        snapshot.latitude_deg = latitude;
        snapshot.longitude_deg = longitude;
        snapshot.location_valid = true;
        location_updated_us = esp_timer_get_time();
    }

    double speed_knots;
    if( parse_double_field( fields[ 6 ], &speed_knots ) && speed_knots >= 0.0 )
    {
        snapshot.speed_mps = (float)( speed_knots * 0.514444 );
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
    if( parse_int_field( fields[ 6 ], &satellites ) )
    {
        snapshot.satellites =
            (uint8_t)( satellites < 0 ? 0 : ( satellites > 255 ? 255 : satellites ) );
    }

    double altitude;
    if( parse_double_field( fields[ 8 ], &altitude ) )
    {
        snapshot.altitude_m = (float)altitude;
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
        parse_rmc( comma + 1 );
    }
    else if( type_length >= 3U && 0 == strcmp( payload + type_length - 3U, "GGA" ) )
    {
        lock_active_baud();
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
    memset( &snapshot, 0, sizeof( snapshot ) );
    line_length = 0U;
    location_updated_us = 0;
    time_updated_us = 0;
    baud_locked = false;
    receiver_detected = false;
    last_nmea_us = 0;
    /* Preserve the completed startup window across temporary radio ownership. */
    active_baud = GPS_BAUD_PRIMARY;
    active_baud_index = 0U;
    baud_probe_started_us = 0;
    initialized = false;
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
        ( now - last_nmea_us ) / 1000000LL >=
            (int64_t)device_config_get()->gps_timeout_seconds )
    {
        receiver_detected = false;
        baud_locked = false;
        snapshot.available = false;
        snapshot.location_valid = false;
        snapshot.time_valid = false;
        baud_probe_started_us = now;
        ESP_LOGI( TAG, "GPS NMEA timeout; shared pins eligible for radio acquisition" );
    }
    probe_next_baud( now );
    if( snapshot.location_valid &&
        ( now - location_updated_us ) / 1000LL > (int64_t)GPS_FIX_STALE_MS )
    {
        snapshot.location_valid = false;
    }
    if( snapshot.time_valid && ( now - time_updated_us ) / 1000LL > (int64_t)GPS_TIME_STALE_MS )
    {
        snapshot.time_valid = false;
    }
}

bool gps_snapshot_get( rfscout_gps_snapshot_t *a_snapshot )
{
    if( NULL == a_snapshot || !initialized || !snapshot.available )
    {
        return false;
    }
    *a_snapshot = snapshot;
    const int64_t now = esp_timer_get_time();
    a_snapshot->location_age_ms =
        location_updated_us > 0 ? (uint32_t)( ( now - location_updated_us ) / 1000LL ) : UINT32_MAX;
    a_snapshot->time_age_ms =
        time_updated_us > 0 ? (uint32_t)( ( now - time_updated_us ) / 1000LL ) : UINT32_MAX;
    return true;
}

bool gps_available( void )
{
    return initialized;
}
