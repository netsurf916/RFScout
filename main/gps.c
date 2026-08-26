/**
    gps.c : RFScout UART GPS implementation
    Description: Detects an NMEA receiver and tracks whether it reports a valid fix.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "gps.h"

#include "board.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define GPS_UART UART_NUM_1
#define GPS_RX_BUFFER_SIZE 1024
#define GPS_READ_BUFFER_SIZE 128
#define GPS_SENTENCE_SIZE 128
#define GPS_PROBE_INTERVAL_US 2000000LL
#define GPS_DETECTION_TIMEOUT_US 5000000LL
#define GPS_FIX_TIMEOUT_US 3000000LL

static const uint32_t probe_baud_rates[] = {
    9600U,
    38400U,
    115200U,
    4800U,
};

static const char *TAG = "gps";
static char sentence[ GPS_SENTENCE_SIZE ];
static size_t sentence_length;
static size_t probe_baud_index;
static bool detected;
static bool has_fix;
static bool received_bytes;
static int64_t last_sentence_us;
static int64_t last_fix_us;
static int64_t last_probe_us;

/** @brief Return the field at the requested comma-separated NMEA position. */
static const char *field_at( char *a_sentence, unsigned a_index )
{
    char *field = a_sentence;
    for( unsigned index = 0U; index < a_index; ++index )
    {
        field = strchr( field, ',' );
        if( NULL == field )
        {
            return NULL;
        }
        ++field;
    }
    return field;
}

/** @brief Convert one hexadecimal character to its numeric value. */
static int hex_value( char a_character )
{
    if( ( a_character >= '0' ) && ( a_character <= '9' ) )
    {
        return a_character - '0';
    }

    a_character = (char)toupper( (unsigned char)a_character );
    if( ( a_character >= 'A' ) && ( a_character <= 'F' ) )
    {
        return 10 + ( a_character - 'A' );
    }

    return -1;
}

/** @brief Validate an NMEA checksum when a sentence supplies one. */
static bool valid_checksum( const char *a_sentence )
{
    if( ( NULL == a_sentence ) || ( '$' != a_sentence[ 0 ] ) )
    {
        return false;
    }

    const char *asterisk = strchr( a_sentence, '*' );
    if( NULL == asterisk )
    {
        return true;
    }
    if( ( '\0' == asterisk[ 1 ] ) || ( '\0' == asterisk[ 2 ] ) )
    {
        return false;
    }

    uint8_t checksum = 0U;
    for( const char *cursor = a_sentence + 1; cursor < asterisk; ++cursor )
    {
        checksum ^= (uint8_t)*cursor;
    }

    const int high = hex_value( asterisk[ 1 ] );
    const int low = hex_value( asterisk[ 2 ] );
    if( ( high < 0 ) || ( low < 0 ) )
    {
        return false;
    }

    return checksum == (uint8_t)( ( high << 4 ) | low );
}

/** @brief Return true when a sentence looks like a standard NMEA talker sentence. */
static bool is_nmea_sentence( const char *a_sentence )
{
    return ( NULL != a_sentence ) && ( '$' == a_sentence[ 0 ] ) &&
           isalpha( (unsigned char)a_sentence[ 1 ] ) && isalpha( (unsigned char)a_sentence[ 2 ] ) &&
           valid_checksum( a_sentence );
}

/** @brief Parse one complete NMEA sentence and update receiver/fix state. */
static void parse_sentence( const char *a_sentence )
{
    if( !is_nmea_sentence( a_sentence ) )
    {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if( !detected )
    {
        ESP_LOGI( TAG,
                  "NMEA GPS receiver detected at %lu baud",
                  (unsigned long)probe_baud_rates[ probe_baud_index ] );
    }
    detected = true;
    last_sentence_us = now_us;

    char copy[ GPS_SENTENCE_SIZE ];
    strlcpy( copy, a_sentence, sizeof( copy ) );

    bool valid_fix = false;
    if( NULL != strstr( copy, "GGA," ) )
    {
        const char *quality = field_at( copy, 6U );
        valid_fix = ( NULL != quality ) && ( '0' != quality[ 0 ] ) && ( '\0' != quality[ 0 ] ) &&
                    ( ',' != quality[ 0 ] );
    }
    else if( NULL != strstr( copy, "RMC," ) )
    {
        const char *status = field_at( copy, 2U );
        valid_fix = ( NULL != status ) && ( 'A' == status[ 0 ] );
    }

    if( valid_fix )
    {
        if( !has_fix )
        {
            ESP_LOGI( TAG, "GPS position lock established" );
        }
        has_fix = true;
        last_fix_us = now_us;
    }
}

/** @brief Feed one received byte into the bounded NMEA line accumulator. */
static void consume_byte( uint8_t a_byte )
{
    if( '$' == a_byte )
    {
        sentence_length = 0U;
        sentence[ sentence_length++ ] = (char)a_byte;
        return;
    }

    if( 0U == sentence_length )
    {
        return;
    }

    if( ( '\r' == a_byte ) || ( '\n' == a_byte ) )
    {
        sentence[ sentence_length ] = '\0';
        parse_sentence( sentence );
        sentence_length = 0U;
        return;
    }

    if( sentence_length < ( sizeof( sentence ) - 1U ) )
    {
        sentence[ sentence_length++ ] = (char)a_byte;
    }
    else
    {
        sentence_length = 0U;
    }
}

/** @brief Select one candidate baud rate while probing for NMEA traffic. */
static void set_probe_baud( size_t a_index )
{
    probe_baud_index = a_index % ( sizeof( probe_baud_rates ) / sizeof( probe_baud_rates[ 0 ] ) );
    const uint32_t baud = probe_baud_rates[ probe_baud_index ];
    const esp_err_t result = uart_set_baudrate( GPS_UART, baud );
    if( ESP_OK == result )
    {
        uart_flush_input( GPS_UART );
        sentence_length = 0U;
        received_bytes = false;
        ESP_LOGI( TAG, "probing GPS UART RX at %lu baud", (unsigned long)baud );
    }
    else
    {
        ESP_LOGW( TAG,
                  "unable to set GPS UART baud %lu: %s",
                  (unsigned long)baud,
                  esp_err_to_name( result ) );
    }
}

esp_err_t gps_init( void )
{
    const uart_config_t config = {
        .baud_rate = (int)probe_baud_rates[ 0 ],
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install( GPS_UART, GPS_RX_BUFFER_SIZE, 0, 0, NULL, 0 );
    if( ESP_OK != result )
    {
        return result;
    }

    result = uart_param_config( GPS_UART, &config );
    if( ESP_OK == result )
    {
        result = uart_set_pin( GPS_UART,
                               UART_PIN_NO_CHANGE,
                               RFSCOUT_BOARD_GPS_RX_PIN,
                               UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE );
    }

    if( ESP_OK != result )
    {
        uart_driver_delete( GPS_UART );
        return result;
    }

    sentence_length = 0U;
    probe_baud_index = 0U;
    detected = false;
    has_fix = false;
    received_bytes = false;
    last_sentence_us = 0;
    last_fix_us = 0;
    last_probe_us = esp_timer_get_time();

    ESP_LOGI( TAG,
              "GPS UART receive-only mode on RX=%d; probing common NMEA baud rates",
              RFSCOUT_BOARD_GPS_RX_PIN );
    ESP_LOGI( TAG,
              "probing GPS UART RX at %lu baud",
              (unsigned long)probe_baud_rates[ probe_baud_index ] );
    return ESP_OK;
}

void gps_poll( void )
{
    uint8_t data[ GPS_READ_BUFFER_SIZE ];
    const int length = uart_read_bytes( GPS_UART, data, sizeof( data ), 0 );
    if( length > 0 )
    {
        if( !received_bytes && !detected )
        {
            ESP_LOGI( TAG,
                      "GPS UART RX receiving bytes at %lu baud",
                      (unsigned long)probe_baud_rates[ probe_baud_index ] );
        }
        received_bytes = true;

        for( int i = 0; i < length; ++i )
        {
            consume_byte( data[ i ] );
        }
    }

    const int64_t now_us = esp_timer_get_time();
    if( !detected && ( ( now_us - last_probe_us ) >= GPS_PROBE_INTERVAL_US ) )
    {
        set_probe_baud( probe_baud_index + 1U );
        last_probe_us = now_us;
    }

    if( detected && ( ( now_us - last_sentence_us ) > GPS_DETECTION_TIMEOUT_US ) )
    {
        detected = false;
        has_fix = false;
        last_probe_us = now_us;
        ESP_LOGW( TAG, "GPS receiver no longer responding; baud probing resumed" );
    }
    else if( has_fix && ( ( now_us - last_fix_us ) > GPS_FIX_TIMEOUT_US ) )
    {
        has_fix = false;
        ESP_LOGI( TAG, "GPS position lock lost" );
    }
}

void gps_get_status( gps_status_t *a_status )
{
    if( NULL == a_status )
    {
        return;
    }

    a_status->detected = detected;
    a_status->has_fix = has_fix;
}
