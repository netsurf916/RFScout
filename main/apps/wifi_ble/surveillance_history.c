/**
    surveillance_history.c : Append-only GPS-qualified packet observations
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "surveillance_history.h"
#include "app_session.h"
#include "media_store.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

#define HISTORY_JOURNAL "surveillance.jsonl"

static const char *TAG = "surveillance_history";
static uint32_t dropped_points;

typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    surveillance_class_t classification;
    double latitude;
    double longitude;
    int64_t utc_epoch;
    uint32_t seen_ms;
} history_point_t;

static SemaphoreHandle_t history_lock;
static history_point_t *pending;
static size_t pending_count;
static size_t pending_capacity;
static history_point_t *writing;
static size_t writing_count;

void surveillance_history_open( void )
{
    if( NULL == history_lock )
        history_lock = xSemaphoreCreateMutex();
    if( NULL == history_lock )
        return;
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( NULL != pending )
        (void)app_session_free( pending );
    if( NULL != writing )
        (void)app_session_free( writing );
    pending = NULL;
    pending_count = 0U;
    pending_capacity = 0U;
    writing = NULL;
    writing_count = 0U;
    dropped_points = 0U;
    xSemaphoreGive( history_lock );
}

void surveillance_history_record( const rf_device_t *a_device,
                                  double a_latitude,
                                  double a_longitude,
                                  int64_t a_utc_epoch )
{
    if( NULL == history_lock || NULL == a_device )
        return;
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( pending_count == pending_capacity )
    {
        const size_t capacity = pending_capacity ? pending_capacity * 2U : 64U;
        if( capacity <= SIZE_MAX / sizeof( *pending ) )
        {
            history_point_t *next = app_session_resize( pending, capacity, sizeof( *pending ) );
            if( NULL != next )
            {
                pending = next;
                pending_capacity = capacity;
            }
        }
    }
    if( pending_count < pending_capacity )
    {
        history_point_t *point = &pending[ pending_count++ ];
        memcpy( point->address, a_device->address, sizeof( point->address ) );
        point->address_type = a_device->address_type;
        point->classification = a_device->surveillance;
        point->latitude = a_latitude;
        point->longitude = a_longitude;
        point->utc_epoch = a_utc_epoch;
        point->seen_ms = a_device->last_seen_ms;
    }
    else
    {
        ++dropped_points;
        if( 1U == dropped_points || 0U == ( dropped_points % 100U ) )
            ESP_LOGW( TAG,
                      "Journal PSRAM exhausted; dropped %lu packet observations",
                      (unsigned long)dropped_points );
    }
    xSemaphoreGive( history_lock );
}

static esp_err_t append_point( const rfscout_media_file_t *a_file, const history_point_t *a_point )
{
    char line[ 256 ];
    const int length = snprintf( line,
                                 sizeof( line ),
                                 "{\"type\":%u,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"class\":%u,"
                                 "\"lat\":%.7f,\"lon\":%.7f,\"utc\":%lld,\"seen\":%lu}\n",
                                 (unsigned)a_point->address_type,
                                 a_point->address[ 0 ],
                                 a_point->address[ 1 ],
                                 a_point->address[ 2 ],
                                 a_point->address[ 3 ],
                                 a_point->address[ 4 ],
                                 a_point->address[ 5 ],
                                 (unsigned)a_point->classification,
                                 a_point->latitude,
                                 a_point->longitude,
                                 (long long)a_point->utc_epoch,
                                 (unsigned long)a_point->seen_ms );
    if( length < 0 || (size_t)length >= sizeof( line ) )
        return ESP_ERR_INVALID_SIZE;
    return media_write( a_file, line, (size_t)length );
}

void surveillance_history_flush( void )
{
    if( NULL == history_lock )
        return;
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( NULL == writing && pending_count != 0U )
    {
        writing = pending;
        writing_count = pending_count;
        pending = NULL;
        pending_count = 0U;
        pending_capacity = 0U;
    }
    xSemaphoreGive( history_lock );
    if( NULL == writing )
        return;

    rfscout_media_collection_t collection;
    rfscout_media_file_t file = { 0 };
    esp_err_t result = media_store_probe();
    if( ESP_OK == result )
        result = media_app_collection( "locations", &collection );
    if( ESP_OK == result )
        result = media_open( &collection, HISTORY_JOURNAL, RFSCOUT_MEDIA_OPEN_APPEND, &file );
    if( ESP_OK == result )
    {
        uint64_t original_size = 0U;
        result = media_size( &file, &original_size );
        const bool size_known = ESP_OK == result;
        for( size_t i = 0U; ESP_OK == result && i < writing_count; ++i )
            result = append_point( &file, &writing[ i ] );
        if( ESP_OK == result )
            result = media_sync( &file );
        if( ESP_OK != result && size_known )
            (void)media_truncate( &file, original_size );
        const esp_err_t close_result = media_close( &file );
        if( ESP_OK != close_result )
            ESP_LOGW( TAG, "Journal close returned %ld", (long)close_result );
    }
    if( ESP_OK == result )
    {
        (void)app_session_free( writing );
        writing = NULL;
        writing_count = 0U;
    }
    else
        ESP_LOGW( TAG, "Surveillance journal flush failed: %ld", (long)result );
}
