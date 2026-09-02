/**
    memory_manager.c : RFScout owner-aware dynamic memory manager
    Description: Tracks PSRAM-backed application allocations for safe growth, diagnostics, and bulk reclamation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "memory_manager.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "memory_manager";

typedef struct memory_allocation
{
    void *pointer;
    size_t size;
    rfscout_memory_owner_t owner;
    struct memory_allocation *next;
} memory_allocation_t;

static SemaphoreHandle_t manager_lock;
static memory_allocation_t *allocations;
static rfscout_memory_stats_t app_stats[ RFSCOUT_APP_COUNT ];
static uint32_t platform_allocation_failures;

static bool owner_is_valid( rfscout_memory_owner_t a_owner )
{
    if( RFSCOUT_MEMORY_OWNER_PLATFORM == a_owner.owner_class )
    {
        return true;
    }

    return ( a_owner.app >= 0 ) && ( a_owner.app < RFSCOUT_APP_COUNT );
}

static bool owner_matches( rfscout_memory_owner_t a_left, rfscout_memory_owner_t a_right )
{
    if( a_left.owner_class != a_right.owner_class )
    {
        return false;
    }

    if( RFSCOUT_MEMORY_OWNER_PLATFORM == a_left.owner_class )
    {
        return true;
    }

    if( a_left.app != a_right.app )
    {
        return false;
    }

    return ( RFSCOUT_MEMORY_OWNER_OPERATION != a_left.owner_class ) ||
           ( a_left.operation == a_right.operation );
}

static void record_failure( rfscout_memory_owner_t a_owner )
{
    if( ( RFSCOUT_MEMORY_OWNER_PLATFORM != a_owner.owner_class ) && owner_is_valid( a_owner ) )
    {
        ++app_stats[ a_owner.app ].allocation_failures;
        return;
    }

    ++platform_allocation_failures;
}

static void account_add( rfscout_memory_owner_t a_owner, size_t a_size )
{
    if( RFSCOUT_MEMORY_OWNER_PLATFORM == a_owner.owner_class )
    {
        return;
    }

    rfscout_memory_stats_t *stats = &app_stats[ a_owner.app ];
    stats->current_bytes += a_size;
    ++stats->allocation_count;
    if( stats->current_bytes > stats->peak_bytes )
    {
        stats->peak_bytes = stats->current_bytes;
    }
}

static void account_remove( rfscout_memory_owner_t a_owner, size_t a_size )
{
    if( RFSCOUT_MEMORY_OWNER_PLATFORM == a_owner.owner_class )
    {
        return;
    }

    rfscout_memory_stats_t *stats = &app_stats[ a_owner.app ];
    stats->current_bytes -= a_size;
    --stats->allocation_count;
}

static memory_allocation_t *find_allocation( void *a_pointer )
{
    for( memory_allocation_t *allocation = allocations; NULL != allocation;
         allocation = allocation->next )
    {
        if( allocation->pointer == a_pointer )
        {
            return allocation;
        }
    }

    return NULL;
}

static bool calculate_size( size_t a_count, size_t a_size, size_t *a_total )
{
    if( ( 0U == a_count ) || ( 0U == a_size ) )
    {
        *a_total = 0U;
        return true;
    }

    if( a_count > ( SIZE_MAX / a_size ) )
    {
        return false;
    }

    *a_total = a_count * a_size;
    return true;
}

esp_err_t memory_manager_init( void )
{
    if( NULL != manager_lock )
    {
        return ESP_OK;
    }

    if( 0U == heap_caps_get_total_size( MALLOC_CAP_SPIRAM ) )
    {
        ESP_LOGE( TAG, "usable PSRAM is required for managed application memory" );
        return ESP_ERR_NOT_SUPPORTED;
    }

    manager_lock = xSemaphoreCreateMutex();
    if( NULL == manager_lock )
    {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

rfscout_memory_owner_t memory_manager_app_owner( rfscout_app_id_t a_app )
{
    return (rfscout_memory_owner_t){
        .owner_class = RFSCOUT_MEMORY_OWNER_APPLICATION,
        .app = a_app,
        .operation = 0U,
    };
}

rfscout_memory_owner_t memory_manager_operation_owner( rfscout_app_id_t a_app,
                                                       uint16_t a_operation )
{
    return (rfscout_memory_owner_t){
        .owner_class = RFSCOUT_MEMORY_OWNER_OPERATION,
        .app = a_app,
        .operation = a_operation,
    };
}

void *memory_manager_calloc( rfscout_memory_owner_t a_owner, size_t a_count, size_t a_size )
{
    size_t total = 0U;
    if( ( NULL == manager_lock ) || !owner_is_valid( a_owner ) ||
        !calculate_size( a_count, a_size, &total ) || ( 0U == total ) )
    {
        return NULL;
    }

    void *pointer = heap_caps_calloc( a_count, a_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == pointer )
    {
        xSemaphoreTake( manager_lock, portMAX_DELAY );
        record_failure( a_owner );
        xSemaphoreGive( manager_lock );
        return NULL;
    }

    memory_allocation_t *allocation = heap_caps_malloc(
        sizeof( *allocation ), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT );
    if( NULL == allocation )
    {
        heap_caps_free( pointer );
        xSemaphoreTake( manager_lock, portMAX_DELAY );
        record_failure( a_owner );
        xSemaphoreGive( manager_lock );
        return NULL;
    }

    allocation->pointer = pointer;
    allocation->size = total;
    allocation->owner = a_owner;

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    allocation->next = allocations;
    allocations = allocation;
    account_add( a_owner, total );
    xSemaphoreGive( manager_lock );

    return pointer;
}

void *memory_manager_resize( rfscout_memory_owner_t a_owner,
                             void *a_pointer,
                             size_t a_count,
                             size_t a_size )
{
    size_t requested_size = 0U;
    if( ( NULL == manager_lock ) || !owner_is_valid( a_owner ) ||
        !calculate_size( a_count, a_size, &requested_size ) )
    {
        return NULL;
    }

    if( NULL == a_pointer )
    {
        return memory_manager_calloc( a_owner, a_count, a_size );
    }

    if( 0U == requested_size )
    {
        return memory_manager_free( a_owner, a_pointer ) ? NULL : a_pointer;
    }

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    memory_allocation_t *allocation = find_allocation( a_pointer );
    if( ( NULL == allocation ) || !owner_matches( allocation->owner, a_owner ) )
    {
        xSemaphoreGive( manager_lock );
        ESP_LOGE( TAG, "resize rejected for untracked or mismatched owner pointer %p", a_pointer );
        return NULL;
    }
    const size_t old_size = allocation->size;
    xSemaphoreGive( manager_lock );

    if( requested_size == old_size )
    {
        return a_pointer;
    }

    void *replacement = heap_caps_malloc( requested_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == replacement )
    {
        xSemaphoreTake( manager_lock, portMAX_DELAY );
        record_failure( a_owner );
        xSemaphoreGive( manager_lock );
        return NULL;
    }

    memcpy( replacement, a_pointer, requested_size < old_size ? requested_size : old_size );
    if( requested_size > old_size )
    {
        memset( (uint8_t *)replacement + old_size, 0, requested_size - old_size );
    }

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    allocation = find_allocation( a_pointer );
    if( ( NULL == allocation ) || !owner_matches( allocation->owner, a_owner ) ||
        ( allocation->size != old_size ) )
    {
        xSemaphoreGive( manager_lock );
        heap_caps_free( replacement );
        ESP_LOGE( TAG, "resize ownership changed while replacing pointer %p", a_pointer );
        return NULL;
    }

    allocation->pointer = replacement;
    allocation->size = requested_size;
    rfscout_memory_stats_t *stats = &app_stats[ a_owner.app ];
    if( RFSCOUT_MEMORY_OWNER_PLATFORM != a_owner.owner_class )
    {
        stats->current_bytes = stats->current_bytes - old_size + requested_size;
        if( stats->current_bytes > stats->peak_bytes )
        {
            stats->peak_bytes = stats->current_bytes;
        }
    }
    xSemaphoreGive( manager_lock );

    heap_caps_free( a_pointer );
    return replacement;
}

bool memory_manager_free( rfscout_memory_owner_t a_owner, void *a_pointer )
{
    if( NULL == a_pointer )
    {
        return true;
    }
    if( ( NULL == manager_lock ) || !owner_is_valid( a_owner ) )
    {
        return false;
    }

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    memory_allocation_t **link = &allocations;
    while( NULL != *link )
    {
        memory_allocation_t *allocation = *link;
        if( allocation->pointer == a_pointer )
        {
            if( !owner_matches( allocation->owner, a_owner ) )
            {
                xSemaphoreGive( manager_lock );
                ESP_LOGE( TAG, "free rejected for mismatched owner pointer %p", a_pointer );
                return false;
            }

            *link = allocation->next;
            account_remove( allocation->owner, allocation->size );
            xSemaphoreGive( manager_lock );
            heap_caps_free( allocation->pointer );
            heap_caps_free( allocation );
            return true;
        }
        link = &( *link )->next;
    }
    xSemaphoreGive( manager_lock );

    ESP_LOGE( TAG, "free rejected for untracked pointer %p", a_pointer );
    return false;
}

static size_t release_matching( rfscout_app_id_t a_app,
                                bool a_match_operation,
                                uint16_t a_operation )
{
    if( ( NULL == manager_lock ) || ( a_app < 0 ) || ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return 0U;
    }

    size_t reclaimed = 0U;
    memory_allocation_t *released = NULL;

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    memory_allocation_t **link = &allocations;
    while( NULL != *link )
    {
        memory_allocation_t *allocation = *link;
        const bool app_owned = ( RFSCOUT_MEMORY_OWNER_PLATFORM != allocation->owner.owner_class ) &&
                               ( allocation->owner.app == a_app );
        const bool operation_matches = !a_match_operation ||
                                       ( ( RFSCOUT_MEMORY_OWNER_OPERATION == allocation->owner.owner_class ) &&
                                         ( allocation->owner.operation == a_operation ) );
        if( app_owned && operation_matches )
        {
            *link = allocation->next;
            reclaimed += allocation->size;
            account_remove( allocation->owner, allocation->size );
            allocation->next = released;
            released = allocation;
            continue;
        }
        link = &( *link )->next;
    }
    xSemaphoreGive( manager_lock );

    while( NULL != released )
    {
        memory_allocation_t *next = released->next;
        heap_caps_free( released->pointer );
        heap_caps_free( released );
        released = next;
    }

    return reclaimed;
}

size_t memory_manager_release_app( rfscout_app_id_t a_app )
{
    return release_matching( a_app, false, 0U );
}

size_t memory_manager_release_operation( rfscout_app_id_t a_app, uint16_t a_operation )
{
    return release_matching( a_app, true, a_operation );
}

rfscout_memory_stats_t memory_manager_app_stats( rfscout_app_id_t a_app )
{
    rfscout_memory_stats_t result = { 0 };
    if( ( NULL == manager_lock ) || ( a_app < 0 ) || ( a_app >= RFSCOUT_APP_COUNT ) )
    {
        return result;
    }

    xSemaphoreTake( manager_lock, portMAX_DELAY );
    result = app_stats[ a_app ];
    xSemaphoreGive( manager_lock );
    return result;
}

size_t memory_manager_app_allocation_count( rfscout_app_id_t a_app )
{
    return memory_manager_app_stats( a_app ).allocation_count;
}
