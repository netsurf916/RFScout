/**
    device_time.c : RFScout authoritative wall-clock service implementation
    Description: Uses the RTC-backed system clock directly and formats UTC/local timestamps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_time.h"

#include "device_config.h"

#include <stdlib.h>
#include <sys/time.h>

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
    return device_time_apply_timezone();
}

esp_err_t device_time_set_epoch( time_t a_epoch, ... )
{
    const struct timeval value = {
        .tv_sec = a_epoch,
        .tv_usec = 0,
    };
    return 0 == settimeofday( &value, NULL ) ? ESP_OK : ESP_FAIL;
}

bool device_time_now( time_t *a_epoch )
{
    if( NULL != a_epoch )
    {
        *a_epoch = time( NULL );
    }
    return true;
}

bool device_time_format_clock( char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_buffer ) || ( 0U == a_capacity ) )
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

esp_err_t device_time_checkpoint( void )
{
    return device_config_save_if_dirty();
}
