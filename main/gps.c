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

#include <stdlib.h>
#include <string.h>

#define GPS_UART UART_NUM_1
#define GPS_BAUD_RATE 9600
#define GPS_RX_BUFFER_SIZE 1024
#define GPS_READ_BUFFER_SIZE 128
#define GPS_SENTENCE_SIZE 128
#define GPS_DETECTION_TIMEOUT_US 5000000LL
#define GPS_FIX_TIMEOUT_US 3000000LL

static const char *TAG = "gps";
static char sentence[ GPS_SENTENCE_SIZE ];
static size_t sentence_length;
static bool detected;
static bool has_fix;
static int64_t last_sentence_us;
static int64_t last_fix_us;

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

/** @brief Parse one complete NMEA sentence and update receiver/fix state. */
static void parse_sentence( const char *a_sentence )
{
    if( ( NULL == a_sentence ) || ( '$' != a_sentence[ 0 ] ) )
    {
        return;
    }

    if( ( NULL == strstr( a_sentence, "GGA," ) ) &&
        ( NULL == strstr( a_sentence, "RMC," ) ) )
    {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if( !detected )
    {
        ESP_LOGI( TAG, "NMEA GPS receiver detected" );
    }
    detected = true;
    last_sentence_us = now_us;

    char copy[ GPS_SENTENCE_SIZE ];
    strlcpy( copy, a_sentence, sizeof( copy ) );

    bool valid_fix = false;
    if( NULL != strstr( copy, "GGA," ) )
    {
        const char *quality = field_at( copy, 6U );
        valid_fix = ( NULL != quality ) && ( '0' != quality[ 0 ] ) &&
                    ( '\0' != quality[ 0 ] ) && ( ',' != quality[ 0 ] );
    }
    else
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

esp_err_t gps_init( void )
{
    const uart_config_t config =
    {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install( GPS_UART,
                                            GPS_RX_BUFFER_SIZE,
                                            0,
                                            0,
                                            NULL,
                                            0 );
    if( ESP_OK != result )
    {
        return result;
    }

    result = uart_param_config( GPS_UART, &config );
    if( ESP_OK == result )
    {
        result = uart_set_pin( GPS_UART,
                               RFSCOUT_BOARD_GPS_TX_PIN,
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
    detected = false;
    has_fix = false;
    last_sentence_us = 0;
    last_fix_us = 0;
    ESP_LOGI( TAG,
              "GPS UART initialized at %u baud TX=%d RX=%d",
              GPS_BAUD_RATE,
              RFSCOUT_BOARD_GPS_TX_PIN,
              RFSCOUT_BOARD_GPS_RX_PIN );
    return ESP_OK;
}

void gps_poll( void )
{
    uint8_t data[ GPS_READ_BUFFER_SIZE ];
    const int length = uart_read_bytes( GPS_UART, data, sizeof( data ), 0 );
    for( int i = 0; i < length; ++i )
    {
        consume_byte( data[ i ] );
    }

    const int64_t now_us = esp_timer_get_time();
    if( detected && ( ( now_us - last_sentence_us ) > GPS_DETECTION_TIMEOUT_US ) )
    {
        detected = false;
        has_fix = false;
        ESP_LOGW( TAG, "GPS receiver no longer responding" );
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
