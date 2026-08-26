/**
    device_time.c : RFScout authoritative wall-clock service implementation
    Description: Maintains runtime wall time independently from monotonic timing and formats UTC/local timestamps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_time.h"

#include "device_config.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define DEVICE_TIME_MIN_VALID_EPOCH 1577836800LL
#define DEVICE_TIME_RTC_MAGIC 0x52544653U

static const char *TAG = "device_time";
static rfscout_time_source_t source = RFSCOUT_TIME_SOURCE_UNSET;
static char source_detail[ 24 ];
static bool valid;
static bool restored_from_config;
static RTC_DATA_ATTR uint32_t retained_magic;
static RTC_DATA_ATTR rfscout_time_source_t retained_source;
static RTC_DATA_ATTR char retained_source_detail[ 24 ];

/** @brief Return the stable configuration token for the current clock authority. */
static const char *source_token( rfscout_time_source_t a_source )
{
    switch( a_source )
    {
        case RFSCOUT_TIME_SOURCE_MANUAL:
            return "manual";
        case RFSCOUT_TIME_SOURCE_GPS:
            return "gps";
        case RFSCOUT_TIME_SOURCE_RTC:
            return source_detail[ 0 ] ? source_detail : "rtc";
        case RFSCOUT_TIME_SOURCE_NTP:
            return "ntp";
        case RFSCOUT_TIME_SOURCE_CONFIG:
            return source_detail[ 0 ] ? source_detail : "config";
        default:
            return "unset";
    }
}

/** @brief Convert a saved source token back into a runtime authority type. */
static rfscout_time_source_t source_from_token( const char *a_token )
{
    if( NULL == a_token )
    {
        return RFSCOUT_TIME_SOURCE_CONFIG;
    }
    if( 0 == strcmp( a_token, "manual" ) )
    {
        return RFSCOUT_TIME_SOURCE_MANUAL;
    }
    if( 0 == strcmp( a_token, "gps" ) )
    {
        return RFSCOUT_TIME_SOURCE_GPS;
    }
    if( 0 == strcmp( a_token, "ntp" ) )
    {
        return RFSCOUT_TIME_SOURCE_NTP;
    }
    if( ( 0 == strcmp( a_token, "rtc" ) ) || ( 0 == strncmp( a_token, "rtc:", 4U ) ) )
    {
        return RFSCOUT_TIME_SOURCE_RTC;
    }
    return RFSCOUT_TIME_SOURCE_CONFIG;
}

/** @brief Retain clock-authority metadata in the RTC domain for deep-sleep wake. */
static void retain_authority( void )
{
    retained_magic = DEVICE_TIME_RTC_MAGIC;
    retained_source = source;
    snprintf( retained_source_detail, sizeof( retained_source_detail ), "%s", source_detail );
}

/** @brief Restore clock-authority metadata without changing the RTC-retained system time. */
static bool restore_after_deep_sleep( void )
{
    if( ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause() )
    {
        return false;
    }
    if( DEVICE_TIME_RTC_MAGIC != retained_magic )
    {
        return false;
    }

    const time_t now = time( NULL );
    if( now < (time_t)DEVICE_TIME_MIN_VALID_EPOCH )
    {
        return false;
    }

    source = retained_source;
    if( ( source < RFSCOUT_TIME_SOURCE_CONFIG ) || ( source > RFSCOUT_TIME_SOURCE_NTP ) )
    {
        source = RFSCOUT_TIME_SOURCE_CONFIG;
    }
    snprintf( source_detail, sizeof( source_detail ), "%s", retained_source_detail );
    valid = true;
    restored_from_config = false;
    ESP_LOGI( TAG, "wall clock resumed after deep sleep source=%s", source_token( source ) );
    return true;
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
    source = RFSCOUT_TIME_SOURCE_UNSET;
    source_detail[ 0 ] = '\0';
    valid = false;
    restored_from_config = false;
    (void)device_time_apply_timezone();

    if( restore_after_deep_sleep() )
    {
        return ESP_OK;
    }

    int64_t epoch = 0;
    const char *saved_source = NULL;
    if( !device_config_saved_time( &epoch, &saved_source ) )
    {
        return ESP_OK;
    }

    const rfscout_time_source_t restored_source = source_from_token( saved_source );
    const esp_err_t result = device_time_set_epoch(
        (time_t)epoch, restored_source, ( NULL != saved_source ) ? saved_source : "config" );
    restored_from_config = ( ESP_OK == result );
    return result;
}

esp_err_t
device_time_set_epoch( time_t a_epoch, rfscout_time_source_t a_source, const char *a_detail )
{
    if( a_epoch < (time_t)DEVICE_TIME_MIN_VALID_EPOCH )
    {
        return ESP_ERR_INVALID_ARG;
    }

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
    valid = true;
    restored_from_config = false;
    retain_authority();
    ESP_LOGI( TAG, "wall clock set source=%s", source_token( source ) );
    return ESP_OK;
}

bool device_time_now( time_t *a_epoch )
{
    if( !valid )
    {
        return false;
    }
    const time_t now = time( NULL );
    if( NULL != a_epoch )
    {
        *a_epoch = now;
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
    static char restored_label[ 32 ];
    const char *base = NULL;

    switch( source )
    {
        case RFSCOUT_TIME_SOURCE_CONFIG:
            base = "Saved";
            break;
        case RFSCOUT_TIME_SOURCE_MANUAL:
            base = "Manual";
            break;
        case RFSCOUT_TIME_SOURCE_GPS:
            base = "GPS";
            break;
        case RFSCOUT_TIME_SOURCE_RTC:
            base = source_detail[ 0 ] ? source_detail : "RTC";
            break;
        case RFSCOUT_TIME_SOURCE_NTP:
            base = "NTP";
            break;
        default:
            return "Unset";
    }

    if( restored_from_config )
    {
        snprintf( restored_label, sizeof( restored_label ), "%s (saved)", base );
        return restored_label;
    }
    return base;
}

bool device_time_format_clock( char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_buffer ) || ( 0U == a_capacity ) )
    {
        return false;
    }

    time_t now;
    if( !device_time_now( &now ) )
    {
        snprintf( a_buffer, a_capacity, "--:--" );
        return false;
    }

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
    time_t now;
    if( !device_time_now( &now ) )
    {
        return device_config_save( true, 0, NULL );
    }
    return device_config_save( true, (int64_t)now, source_token( source ) );
}
