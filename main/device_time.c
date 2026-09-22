/**
    device_time.c : RFScout authoritative wall-clock service implementation
    Description: Owns UTC wall-clock validity/source, synchronization policy, and local timezone presentation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_time.h"

#include "device_config.h"
#include "persistent_store.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#define DEVICE_TIME_PLAUSIBLE_EPOCH 1577836800LL
#define DEVICE_TIME_POLICY_KEY "time.policy"
#define DEVICE_TIME_POLICY_MAGIC 0x54494D45U
#define DEVICE_TIME_POLICY_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t reserved;
} device_time_policy_record_t;

_Static_assert( sizeof( device_time_policy_record_t ) <= 16U,
                "time policy record must stay bounded" );

static bool time_valid;
static rfscout_time_source_t time_source;
static rfscout_time_mode_t time_mode = RFSCOUT_TIME_MODE_MANUAL;

static esp_err_t persist_mode( rfscout_time_mode_t a_mode )
{
    const device_time_policy_record_t record = {
        .magic = DEVICE_TIME_POLICY_MAGIC,
        .version = DEVICE_TIME_POLICY_VERSION,
        .mode = (uint8_t)a_mode,
        .reserved = 0U,
    };
    return system_store_write( DEVICE_TIME_POLICY_KEY, &record, sizeof( record ) );
}

static void load_mode( void )
{
    device_time_policy_record_t record;
    size_t length = 0U;
    const esp_err_t result =
        system_store_read( DEVICE_TIME_POLICY_KEY, &record, sizeof( record ), &length );
    if( ESP_OK == result && sizeof( record ) == length &&
        DEVICE_TIME_POLICY_MAGIC == record.magic && DEVICE_TIME_POLICY_VERSION == record.version &&
        record.mode <= RFSCOUT_TIME_MODE_MANUAL )
    {
        time_mode = (rfscout_time_mode_t)record.mode;
        return;
    }
    time_mode = RFSCOUT_TIME_MODE_MANUAL;
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
    load_mode();
    const time_t now = time( NULL );
    time_valid = now >= (time_t)DEVICE_TIME_PLAUSIBLE_EPOCH;
    time_source = time_valid ? RFSCOUT_TIME_SOURCE_RETAINED : RFSCOUT_TIME_SOURCE_INVALID;
    return device_time_apply_timezone();
}

esp_err_t device_time_set_epoch( time_t a_epoch )
{
    if( a_epoch < (time_t)DEVICE_TIME_PLAUSIBLE_EPOCH )
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = persist_mode( RFSCOUT_TIME_MODE_MANUAL );
    if( ESP_OK != result )
    {
        return result;
    }

    result = set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_MANUAL );
    time_mode = RFSCOUT_TIME_MODE_MANUAL;
    return result;
}

esp_err_t device_time_offer_gps( time_t a_epoch )
{
    if( RFSCOUT_TIME_MODE_AUTO != time_mode )
    {
        return ESP_ERR_INVALID_STATE;
    }
    return set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_GPS );
}

esp_err_t device_time_offer_ntp( time_t a_epoch )
{
    return set_epoch( a_epoch, RFSCOUT_TIME_SOURCE_NTP );
}

esp_err_t device_time_set_mode( rfscout_time_mode_t a_mode )
{
    if( a_mode > RFSCOUT_TIME_MODE_MANUAL )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( time_mode == a_mode )
    {
        return ESP_OK;
    }

    const esp_err_t result = persist_mode( a_mode );
    if( ESP_OK != result )
    {
        return result;
    }
    time_mode = a_mode;
    return ESP_OK;
}

rfscout_time_mode_t device_time_mode( void )
{
    return time_mode;
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
    a_status->mode = time_mode;
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
