/**
    ota_acquisition.c : RFScout OTA catalog acquisition service
    Description: Fetches the static stable catalog over verified HTTPS with hard cancellation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_acquisition.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define OTA_CATALOG_URL "https://netsurf916.github.io/RFScout/ota/stable.json"
#define OTA_HTTP_TIMEOUT_MS 5000
#define OTA_ACQUISITION_TASK_STACK 8192U
#define OTA_ACQUISITION_STOP_TIMEOUT_MS 6000U

static const char *TAG = "ota_acquisition";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t worker_task;
static esp_http_client_handle_t active_client;
static ota_acquisition_status_t status;
static char catalog[ OTA_ACQUISITION_CATALOG_MAX_LENGTH + 1U ];
static size_t catalog_length;
static bool cancel_requested;
static bool overflowed;

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

    bool cancelled;
    portENTER_CRITICAL( &state_lock );
    cancelled = cancel_requested;
    portEXIT_CRITICAL( &state_lock );
    if( cancelled )
    {
        return ESP_FAIL;
    }

    const size_t incoming = (size_t)a_event->data_len;
    if( incoming > OTA_ACQUISITION_CATALOG_MAX_LENGTH - catalog_length )
    {
        overflowed = true;
        return ESP_ERR_NO_MEM;
    }

    memcpy( catalog + catalog_length, a_event->data, incoming );
    catalog_length += incoming;
    catalog[ catalog_length ] = '\0';
    return ESP_OK;
}

static void finish_worker( ota_acquisition_state_t a_state, esp_err_t a_error, int a_http_status )
{
    portENTER_CRITICAL( &state_lock );
    status.state = a_state;
    status.error = a_error;
    status.http_status = a_http_status;
    status.length = OTA_ACQUISITION_READY == a_state ? catalog_length : 0U;
    ++status.generation;
    active_client = NULL;
    portEXIT_CRITICAL( &state_lock );
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
        .url = OTA_CATALOG_URL,
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
    }

    portENTER_CRITICAL( &state_lock );
    const bool cancelled = cancel_requested;
    portEXIT_CRITICAL( &state_lock );

    if( cancelled )
    {
        finish_worker( OTA_ACQUISITION_IDLE, ESP_OK, 0 );
    }
    else if( ESP_OK == result && 200 == http_status && !overflowed && catalog_length > 0U )
    {
        finish_worker( OTA_ACQUISITION_READY, ESP_OK, http_status );
    }
    else
    {
        if( overflowed )
        {
            result = ESP_ERR_NO_MEM;
        }
        else if( ESP_OK == result )
        {
            result = ESP_FAIL;
        }
        ESP_LOGW( TAG,
                  "catalog fetch failed: error=%s http=%d bytes=%u",
                  esp_err_to_name( result ),
                  http_status,
                  (unsigned)catalog_length );
        finish_worker( OTA_ACQUISITION_FAILED, result, http_status );
    }

    esp_http_client_cleanup( client );
    mark_worker_stopped();
    vTaskDelete( NULL );
}

esp_err_t ota_acquisition_begin_catalog( void )
{
    portENTER_CRITICAL( &state_lock );
    if( NULL != worker_task )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }

    memset( catalog, 0, sizeof( catalog ) );
    catalog_length = 0U;
    overflowed = false;
    cancel_requested = false;
    status.state = OTA_ACQUISITION_FETCHING;
    status.error = ESP_OK;
    status.http_status = 0;
    status.length = 0U;
    portEXIT_CRITICAL( &state_lock );

    if( pdPASS != xTaskCreate( acquisition_worker,
                               "ota_catalog",
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

ota_acquisition_status_t ota_acquisition_status( void )
{
    ota_acquisition_status_t result;
    portENTER_CRITICAL( &state_lock );
    result = status;
    portEXIT_CRITICAL( &state_lock );
    return result;
}

esp_err_t ota_acquisition_copy_catalog( char *a_destination, size_t a_capacity, size_t *a_length )
{
    if( NULL == a_destination || NULL == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    if( OTA_ACQUISITION_READY != status.state )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }
    if( a_capacity <= catalog_length )
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
    portEXIT_CRITICAL( &state_lock );

    if( NULL != client )
    {
        (void)esp_http_client_cancel_request( client );
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
                ESP_LOGE( TAG, "catalog acquisition did not stop within timeout" );
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay( pdMS_TO_TICKS( 10U ) );
        }
    }

    portENTER_CRITICAL( &state_lock );
    memset( catalog, 0, sizeof( catalog ) );
    catalog_length = 0U;
    overflowed = false;
    cancel_requested = false;
    active_client = NULL;
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
