/**
    device_time.c : RFScout authoritative wall-clock service implementation
    Description: Owns UTC wall-clock validity/source, synchronization policy, and local timezone presentation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_time.h"

#include "device_config.h"
#include "esp_timer.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#define DEVICE_TIME_PLAUSIBLE_EPOCH 1577836800LL

#define DEVICE_GPS_FRESH_US 5000000LL

static bool time_valid;
static rfscout_time_source_t time_source;
static int64_t last_gps_time_us;

bool device_time_gps_available( void )
{
    return last_gps_time_us > 0 &&
           esp_timer_get_time() - last_gps_time_us <= DEVICE_GPS_FRESH_US;
}

static esp_err_t set_epoch( time_t a_epoch, rfscout_time_source_t a_source )
{
    if( a_epoch < (time_t)DEVICE_TIME_PLAUSIBLE_EPOCH )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const struct timeval value = {
        .tv_sec = a_epoch,
        .tv_usec = 0,
    };
    if( 0 != settimeofday( &value, NULL ) )
    {
        return ESP_FAIL;
    }
    time_valid = true;
    time_source = a_source;
    return ESP_OK;
}

esp_err_t device_time_apply_timezone( void )
{
    const device_timezone_t *zone = device_config_timezone();
    if( NULL == zone )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( 0 != setenv( "TZ", zone->posix, 1 ) )
    {
        return ESP_FAIL;
    }
    tzset();
    return ESP_OK;
}

esp_err_t device_time_init( void )
{
    const time_t now = time( NULL );
    time_valid = now >= (time_t)DEVICE_TIME_PLAUSIBLE_EPOCH;
    time_source = time_valid ? RFSCOUT_TIME_SOURCE_RETAINED : RFSCOUT_TIME_SOURCE_INVALID;
    last_gps_time_us = 0;
    return device_time_apply_timezone();
}

esp_err_t device_time_set_epoch( time_t a_epoch )
{
    if( device_time_gps_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    return set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_MANUAL );
}

esp_err_t device_time_offer_gps( time_t a_epoch )
{
    if( a_epoch < (time_t)DEVICE_TIME_PLAUSIBLE_EPOCH )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const time_t current = time( NULL );
    if( !time_valid || llabs( (long long)a_epoch - (long long)current ) >= 2LL )
    {
        const esp_err_t result = set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_GPS );
        if( ESP_OK != result )
        {
            return result;
        }
    }
    time_valid = true;
    time_source = RFSCOUT_TIME_SOURCE_GPS;
    last_gps_time_us = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t device_time_offer_ntp( time_t a_epoch )
{
    return set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_NTP );
}

bool device_time_now( time_t *a_epoch )
{
    if( NULL != a_epoch )
    {
        *a_epoch = time( NULL );
    }
    return time_valid;
}

void device_time_status( rfscout_time_status_t *a_status )
{
    if( NULL == a_status )
    {
        return;
    }
    a_status->valid = time_valid;
    a_status->source = time_source;
    a_status->epoch = time( NULL );
}

bool device_time_format_clock( char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_buffer ) || ( 0U == a_capacity ) || !time_valid )
    {
        return false;
    }

    const time_t now = time( NULL );
    struct tm local;
    if( NULL == localtime_r( &now, &local ) )
    {
        return false;
    }
    const char *format = device_config_get()->clock_24h ? "%H:%M" : "%I:%M%p";
    return 0U != strftime( a_buffer, a_capacity, format, &local );
}

bool device_time_format_iso8601_utc( time_t a_epoch, char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_buffer ) || ( 0U == a_capacity ) )
    {
        return false;
    }

    struct tm utc;
    if( NULL == gmtime_r( &a_epoch, &utc ) )
    {
        return false;
    }
    return 0U != strftime( a_buffer, a_capacity, "%Y-%m-%dT%H:%M:%SZ", &utc );
}
