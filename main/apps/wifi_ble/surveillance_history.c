/**
    surveillance_history.c : Append-only GPS-qualified packet observations
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "surveillance_history.h"
#include "surveillance.h"
#include "app_session.h"
#include "media_journal.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

#define HISTORY_COLLECTION "locations"
#define HISTORY_PREFIX "surveillance"
#define HISTORY_SEGMENT_BYTES ( 1024U * 1024U )
#define HISTORY_MAX_SEGMENTS 64U
#define HISTORY_INITIAL_CAPACITY 2048U

static const char *TAG = "surveillance_history";
static uint32_t dropped_points;
static media_journal_t journal;
static bool journal_ready;

typedef observation_location_event_t history_point_t;

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
    rfscout_media_collection_t collection;
    journal_ready = ESP_OK == media_app_collection( HISTORY_COLLECTION, &collection ) &&
                    ESP_OK == media_journal_init( &journal,
                                                  &collection,
                                                  HISTORY_PREFIX,
                                                  HISTORY_SEGMENT_BYTES,
                                                  HISTORY_MAX_SEGMENTS );
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( NULL != pending )
        (void)app_session_free( pending );
    if( NULL != writing )
        (void)app_session_free( writing );
    pending = app_session_resize( NULL, HISTORY_INITIAL_CAPACITY, sizeof( *pending ) );
    pending_count = 0U;
    pending_capacity = NULL != pending ? HISTORY_INITIAL_CAPACITY : 0U;
    writing = NULL;
    writing_count = 0U;
    dropped_points = 0U;
    xSemaphoreGive( history_lock );
}

void surveillance_history_close( void )
{
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
    journal_ready = false;
    xSemaphoreGive( history_lock );
}

void surveillance_history_record( const observation_location_event_t *a_event )
{
    if( NULL == history_lock || NULL == a_event )
        return;
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( pending_count < pending_capacity )
        pending[ pending_count++ ] = *a_event;
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

/* Wi-Fi records use type zero; BLE public addresses share that value. */
static const char *address_type_name( uint8_t a_type )
{
    switch( a_type )
    {
        case 0U:
            return "public_or_wifi";
        case 1U:
            return "random";
        case 2U:
            return "public_identity";
        case 3U:
            return "random_identity";
        default:
            return "unknown";
    }
}

static int format_point( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    const history_point_t *point = &( (const history_point_t *)a_context )[ a_index ];
    int length = media_journal_printf( a_line, a_capacity, 0, "{\"type\":\"surveillance\"" );
    if( 0 != point->utc_epoch )
        length = media_journal_printf(
            a_line, a_capacity, length, ",\"utc\":%lld", (long long)point->utc_epoch );
    return media_journal_printf( a_line,
                                 a_capacity,
                                 length,
                                 ",\"lat\":%.7f,\"lon\":%.7f,\"mac\":\"%02X%02X%02X%02X%02X%02X\","
                                 "\"address_type\":\"%s\",\"class\":\"%s\",\"seen_ms\":%lu}",
                                 point->latitude,
                                 point->longitude,
                                 point->address[ 0 ],
                                 point->address[ 1 ],
                                 point->address[ 2 ],
                                 point->address[ 3 ],
                                 point->address[ 4 ],
                                 point->address[ 5 ],
                                 address_type_name( point->address_type ),
                                 surveillance_name( point->classification ),
                                 (unsigned long)point->seen_ms );
}

static bool surveillance_history_flush_one( void )
{
    if( NULL == history_lock || !journal_ready )
        return false;
    xSemaphoreTake( history_lock, portMAX_DELAY );
    if( NULL == writing && pending_count != 0U )
    {
        const size_t next_capacity =
            pending_count > pending_capacity / 2U && pending_capacity <= SIZE_MAX / 2U
                ? pending_capacity * 2U
                : pending_capacity;
        history_point_t *next = app_session_resize( NULL, next_capacity, sizeof( *pending ) );
        if( NULL != next )
        {
            writing = pending;
            writing_count = pending_count;
            pending = next;
            pending_count = 0U;
            pending_capacity = next_capacity;
        }
    }
    xSemaphoreGive( history_lock );
    if( NULL == writing )
        return false;

    const esp_err_t result = media_journal_append( &journal, format_point, writing, writing_count );
    /* A rolled-back batch is retried; an uncertain one is dropped rather than duplicated. */
    if( ESP_OK == result || media_journal_outcome_uncertain( &journal ) )
    {
        (void)app_session_free( writing );
        writing = NULL;
        writing_count = 0U;
    }
    if( ESP_OK != result )
        ESP_LOGW( TAG, "Surveillance journal flush failed: %ld", (long)result );
    return ESP_OK == result;
}

/* Once the location sink is detached on app exit, at most two batches remain:
   an in-flight writing batch and the pending batch collected behind it.
   Stop after the first failure; no spin/retry loop against missing media. */
void surveillance_history_flush_final( void )
{
    if( surveillance_history_flush_one() )
        (void)surveillance_history_flush_one();
}

void surveillance_history_flush( void )
{
    (void)surveillance_history_flush_one();
}
