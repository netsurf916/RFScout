/**
    device_time.c : RFScout authoritative wall-clock service implementation
    Description: Maintains runtime wall time independently from monotonic timing and formats UTC/local timestamps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_time.h"

#include "device_config.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "device_time";
static rfscout_time_source_t source = RFSCOUT_TIME_SOURCE_RTC;
static char source_detail[ 24 ];

static const char *source_token( rfscout_time_source_t a_source )
{
    switch( a_source )
    {
        case RFSCOUT_TIME_SOURCE_MANUAL:
            return "manual";
        case RFSCOUT_TIME_SOURCE_GPS:
            return "gps";
        case RFSCOUT_TIME_SOURCE_NTP:
            return "ntp";
        case RFSCOUT_TIME_SOURCE_RTC:
        default:
            return source_detail[ 0 ] ? source_detail : "rtc";
    }
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
    source = RFSCOUT_TIME_SOURCE_RTC;
    source_detail[ 0 ] = '\0';
    return device_time_apply_timezone();
}

esp_err_t
device_time_set_epoch( time_t a_epoch, rfscout_time_source_t a_source, const char *a_detail )
{
    struct timeval value = {
        .tv_sec = a_epoch,
        .tv_usec = 0,
    };
    if( 0 != settimeofday( &value, NULL ) )
    {
        return ESP_FAIL;
    }

    source = a_source;
    snprintf( source_detail, sizeof( source_detail ), "%s", ( NULL != a_detail ) ? a_detail : "" );
    ESP_LOGI( TAG, "wall clock set source=%s", source_token( source ) );
    return ESP_OK;
}

bool device_time_now( time_t *a_epoch )
{
    if( NULL != a_epoch )
    {
        *a_epoch = time( NULL );
    }
    return true;
}

rfscout_time_source_t device_time_source( void )
{
    return source;
}

const char *device_time_source_detail( void )
{
    return source_detail;
}

const char *device_time_source_label( void )
{
    switch( source )
    {
        case RFSCOUT_TIME_SOURCE_MANUAL:
            return "Manual";
        case RFSCOUT_TIME_SOURCE_GPS:
            return "GPS";
        case RFSCOUT_TIME_SOURCE_NTP:
            return "NTP";
        case RFSCOUT_TIME_SOURCE_RTC:
        default:
            return source_detail[ 0 ] ? source_detail : "RTC";
    }
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
