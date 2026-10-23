/**
    ota_acquisition.c : RFScout cancellable HTTPS OTA acquisition service
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_acquisition.h"

#include "ota_installer.h"
#include "memory_manager.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#define OTA_CATALOG_URL "https://netsurf916.github.io/RFScout/ota/catalog.json"
#define OTA_PAGES_BASE_URL "https://netsurf916.github.io/RFScout/"
#define OTA_HTTP_URL_MAX_LENGTH 320U
#define OTA_HTTP_TIMEOUT_MS 15000
#define OTA_ACQUISITION_TASK_STACK 8192U
#define OTA_ACQUISITION_STOP_TIMEOUT_MS 6000U

static const char *TAG = "ota_acquisition";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t worker_task;
static esp_http_client_handle_t active_client;
static ota_acquisition_status_t status;
static char request_url[ OTA_HTTP_URL_MAX_LENGTH ];
static char *catalog;
static size_t catalog_length;
static size_t catalog_capacity;
static bool cancel_requested;
static bool cancel_call_active;
static esp_err_t stream_error;
static bool initialized;
static esp_err_t ensure_catalog_capacity( size_t a_required )
{
    if( a_required <= catalog_capacity )
    {
        return ESP_OK;
    }

    size_t capacity = catalog_capacity > 0U ? catalog_capacity : 4096U;
    while( capacity < a_required )
    {
        if( capacity > SIZE_MAX / 2U )
        {
            capacity = a_required;
            break;
        }
        capacity *= 2U;
    }

    rfscout_memory_owner_t owner = memory_manager_app_owner( RFSCOUT_APP_DEVICE );
    char *resized = memory_manager_resize( owner, catalog, capacity, sizeof( *catalog ) );
    if( NULL == resized )
    {
        return ESP_ERR_NO_MEM;
    }

    catalog = resized;
    catalog_capacity = capacity;
    return ESP_OK;
}

static bool release_catalog( void )
{
    if( NULL == catalog )
    {
        catalog_length = 0U;
        catalog_capacity = 0U;
        return true;
    }

    rfscout_memory_owner_t owner = memory_manager_app_owner( RFSCOUT_APP_DEVICE );
    if( !memory_manager_free( owner, catalog ) )
    {
        return false;
    }

    catalog = NULL;
    catalog_length = 0U;
    catalog_capacity = 0U;
    return true;
}

static bool safe_relative_path( const char *a_path )
{
    if( NULL == a_path || '\0' == a_path[ 0 ] || '/' == a_path[ 0 ] ||
        NULL != strstr( a_path, "://" ) || NULL != strchr( a_path, '\\' ) ||
        NULL != strchr( a_path, '?' ) || NULL != strchr( a_path, '#' ) )
    {
        return false;
    }

    const char *segment = a_path;
    for( const char *cursor = a_path;; ++cursor )
    {
        if( '/' != *cursor && '\0' != *cursor )
        {
            continue;
        }
        const size_t length = (size_t)( cursor - segment );
        if( 0U == length || ( 1U == length && '.' == segment[ 0 ] ) ||
            ( 2U == length && '.' == segment[ 0 ] && '.' == segment[ 1 ] ) )
        {
            return false;
        }
        if( '\0' == *cursor )
        {
            break;
        }
        segment = cursor + 1;
    }
    return true;
}

static esp_err_t http_event( esp_http_client_event_t *a_event )
{
    if( NULL == a_event )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( HTTP_EVENT_ON_DATA != a_event->event_id || NULL == a_event->data || a_event->data_len <= 0 )
    {
        return ESP_OK;
    }

    ota_acquisition_kind_t kind;
    bool cancelled;
    portENTER_CRITICAL( &state_lock );
    kind = status.kind;
    cancelled = cancel_requested;
    portEXIT_CRITICAL( &state_lock );
    if( cancelled )
    {
        return ESP_FAIL;
    }

    const size_t incoming = (size_t)a_event->data_len;
    if( OTA_ACQUISITION_KIND_CATALOG == kind )
    {
        if( incoming > SIZE_MAX - catalog_length - 1U )
        {
            stream_error = ESP_ERR_NO_MEM;
            return stream_error;
        }

        const size_t required = catalog_length + incoming + 1U;
        const esp_err_t resize_result = ensure_catalog_capacity( required );
        if( ESP_OK != resize_result )
        {
            stream_error = resize_result;
            return resize_result;
        }

        memcpy( catalog + catalog_length, a_event->data, incoming );
        catalog_length += incoming;
        catalog[ catalog_length ] = '\0';
        portENTER_CRITICAL( &state_lock );
        status.length = catalog_length;
        portEXIT_CRITICAL( &state_lock );
        return ESP_OK;
    }

    if( OTA_ACQUISITION_KIND_FIRMWARE == kind )
    {
        const esp_err_t result = ota_installer_write( a_event->data, incoming );
        if( ESP_OK != result )
        {
            stream_error = result;
            return result;
        }
        portENTER_CRITICAL( &state_lock );
        status.length += incoming;
        portEXIT_CRITICAL( &state_lock );
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}

static void finish_worker( ota_acquisition_state_t a_state, esp_err_t a_error, int a_http_status )
{
    portENTER_CRITICAL( &state_lock );
    status.state = a_state;
    status.error = a_error;
    status.http_status = a_http_status;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );
}

static void release_client_for_cleanup( esp_http_client_handle_t a_client )
{
    while( true )
    {
        portENTER_CRITICAL( &state_lock );
        if( !cancel_call_active )
        {
            if( active_client == a_client )
            {
                active_client = NULL;
            }
            portEXIT_CRITICAL( &state_lock );
            return;
        }
        portEXIT_CRITICAL( &state_lock );
        vTaskDelay( 1U );
    }
}

static void mark_worker_stopped( void )
{
    portENTER_CRITICAL( &state_lock );
    worker_task = NULL;
    portEXIT_CRITICAL( &state_lock );
}

static void acquisition_worker( void *a_context )
{
    (void)a_context;

    const esp_http_client_config_t config = {
        .url = request_url,
        .event_handler = http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .user_agent = "RFScout OTA",
    };
    esp_http_client_handle_t client = esp_http_client_init( &config );
    if( NULL == client )
    {
        finish_worker( OTA_ACQUISITION_FAILED, ESP_ERR_NO_MEM, 0 );
        mark_worker_stopped();
        vTaskDelete( NULL );
        return;
    }

    portENTER_CRITICAL( &state_lock );
    active_client = client;
    const bool cancelled_before_start = cancel_requested;
    portEXIT_CRITICAL( &state_lock );

    esp_err_t result = ESP_ERR_INVALID_STATE;
    int http_status = 0;
    if( !cancelled_before_start )
    {
        result = esp_http_client_perform( client );
        http_status = esp_http_client_get_status_code( client );
        if( ESP_OK != result )
        {
            int tls_code = 0;
            int tls_flags = 0;
            const int socket_errno = esp_http_client_get_errno( client );
            const esp_err_t tls_error =
                esp_http_client_get_and_clear_last_tls_error( client, &tls_code, &tls_flags );
            ESP_LOGW( TAG,
                      "HTTP transport error: error=%s errno=%d tls=%s tls_code=%d "
                      "tls_flags=0x%x",
                      esp_err_to_name( result ),
                      socket_errno,
                      esp_err_to_name( tls_error ),
                      tls_code,
                      (unsigned)tls_flags );
        }
    }

    ota_acquisition_kind_t kind;
    size_t transferred;
    portENTER_CRITICAL( &state_lock );
    const bool cancelled = cancel_requested;
    kind = status.kind;
    transferred = status.length;
    portEXIT_CRITICAL( &state_lock );

    if( cancelled )
    {
        finish_worker( OTA_ACQUISITION_IDLE, ESP_OK, 0 );
    }
    else if( ESP_OK == result && 200 == http_status && ESP_OK == stream_error &&
             ( OTA_ACQUISITION_KIND_FIRMWARE == kind || catalog_length > 0U ) )
    {
        finish_worker( OTA_ACQUISITION_READY, ESP_OK, http_status );
    }
    else
    {
        if( ESP_OK != stream_error )
        {
            result = stream_error;
        }
        else if( ESP_OK == result )
        {
            result = ESP_FAIL;
        }
        ESP_LOGW( TAG,
                  "OTA acquisition failed: kind=%d error=%s http=%d bytes=%u",
                  (int)kind,
                  esp_err_to_name( result ),
                  http_status,
                  (unsigned)transferred );
        finish_worker( OTA_ACQUISITION_FAILED, result, http_status );
    }

    release_client_for_cleanup( client );
    esp_http_client_cleanup( client );
    mark_worker_stopped();
    vTaskDelete( NULL );
}

static esp_err_t begin_request( ota_acquisition_kind_t a_kind, const char *a_url )
{
    if( NULL == a_url || '\0' == a_url[ 0 ] )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    if( NULL != worker_task )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }

    portEXIT_CRITICAL( &state_lock );
    if( !release_catalog() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL( &state_lock );

    snprintf( request_url, sizeof( request_url ), "%s", a_url );
    stream_error = ESP_OK;
    cancel_requested = false;
    cancel_call_active = false;
    status.kind = a_kind;
    status.state = OTA_ACQUISITION_FETCHING;
    status.error = ESP_OK;
    status.http_status = 0;
    status.length = 0U;
    portEXIT_CRITICAL( &state_lock );

    if( pdPASS != xTaskCreate( acquisition_worker,
                               "ota_acquire",
                               OTA_ACQUISITION_TASK_STACK,
                               NULL,
                               5U,
                               &worker_task ) )
    {
        portENTER_CRITICAL( &state_lock );
        worker_task = NULL;
        status.state = OTA_ACQUISITION_FAILED;
        status.error = ESP_ERR_NO_MEM;
        ++status.generation;
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_acquisition_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    portENTER_CRITICAL( &state_lock );
    memset( &status, 0, sizeof( status ) );
    status.state = OTA_ACQUISITION_IDLE;
    portEXIT_CRITICAL( &state_lock );
    initialized = true;
    return ESP_OK;
}

esp_err_t ota_acquisition_begin_catalog( void )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }
    return begin_request( OTA_ACQUISITION_KIND_CATALOG, OTA_CATALOG_URL );
}

esp_err_t ota_acquisition_begin_firmware( const char *a_artifact_path )
{
    if( !initialized )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( !safe_relative_path( a_artifact_path ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char url[ OTA_HTTP_URL_MAX_LENGTH ];
    const int length = snprintf( url, sizeof( url ), "%s%s", OTA_PAGES_BASE_URL, a_artifact_path );
    if( length <= 0 || (size_t)length >= sizeof( url ) )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return begin_request( OTA_ACQUISITION_KIND_FIRMWARE, url );
}

void ota_acquisition_status( ota_acquisition_status_t *a_status )
{
    if( NULL == a_status )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    *a_status = status;
    portEXIT_CRITICAL( &state_lock );
}

esp_err_t ota_acquisition_copy_catalog( char *a_destination, size_t a_capacity, size_t *a_length )
{
    if( NULL == a_destination || NULL == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    if( OTA_ACQUISITION_KIND_CATALOG != status.kind || OTA_ACQUISITION_READY != status.state )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }
    if( NULL == catalog || a_capacity <= catalog_length )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy( a_destination, catalog, catalog_length + 1U );
    *a_length = catalog_length;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

esp_err_t ota_acquisition_stop( void )
{
    esp_http_client_handle_t client = NULL;
    TaskHandle_t task = NULL;

    portENTER_CRITICAL( &state_lock );
    cancel_requested = true;
    client = active_client;
    task = worker_task;
    cancel_call_active = NULL != client;
    portEXIT_CRITICAL( &state_lock );

    if( NULL != client )
    {
        (void)esp_http_client_cancel_request( client );
        portENTER_CRITICAL( &state_lock );
        cancel_call_active = false;
        portEXIT_CRITICAL( &state_lock );
    }

    if( NULL != task )
    {
        const TickType_t started = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS( OTA_ACQUISITION_STOP_TIMEOUT_MS );
        while( true )
        {
            portENTER_CRITICAL( &state_lock );
            const bool stopped = NULL == worker_task;
            portEXIT_CRITICAL( &state_lock );
            if( stopped )
            {
                break;
            }
            if( xTaskGetTickCount() - started >= timeout )
            {
                ESP_LOGE( TAG, "OTA acquisition did not stop within timeout" );
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay( pdMS_TO_TICKS( 10U ) );
        }
    }

    if( !release_catalog() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL( &state_lock );
    memset( request_url, 0, sizeof( request_url ) );
    stream_error = ESP_OK;
    cancel_requested = false;
    cancel_call_active = false;
    active_client = NULL;
    status.kind = OTA_ACQUISITION_KIND_NONE;
    status.state = OTA_ACQUISITION_IDLE;
    status.error = ESP_OK;
    status.http_status = 0;
    status.length = 0U;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

bool ota_acquisition_is_active( void )
{
    bool active;
    portENTER_CRITICAL( &state_lock );
    active = NULL != worker_task;
    portEXIT_CRITICAL( &state_lock );
    return active;
}
