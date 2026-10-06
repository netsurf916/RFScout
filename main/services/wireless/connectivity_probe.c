/**
    connectivity_probe.c : RFScout outbound connectivity probe
    Description: Runs one bounded, cancellable, certificate-validated HTTPS request in a worker task.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "connectivity_probe.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

#define CONNECTIVITY_PROBE_URL "https://cloudflare.com/cdn-cgi/trace"
#define CONNECTIVITY_PROBE_TASK_STACK 6144U
#define CONNECTIVITY_PROBE_TASK_PRIORITY 4U
#define CONNECTIVITY_PROBE_RESPONSE_MAX_LENGTH 1024U
#define CONNECTIVITY_PROBE_STOP_TIMEOUT_MS 5000U
#define CONNECTIVITY_PROBE_STOP_POLL_MS 10U

static const char *TAG = "connectivity_probe";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static rfscout_connectivity_probe_status_t status;
static TaskHandle_t worker_task;
static uint32_t request_timeout_ms;
static bool cancel_requested;
static StaticSemaphore_t client_mutex_control;
static SemaphoreHandle_t client_mutex;
static esp_http_client_handle_t active_client;

/* Only one worker exists at a time, so its bounded response buffer is static rather than stack. */
static char response[ CONNECTIVITY_PROBE_RESPONSE_MAX_LENGTH + 1U ];
static size_t response_length;

/** @brief Copy the value of one `key=value` line from a Cloudflare trace body. */
static void
copy_trace_value( const char *a_trace, const char *a_key, char *a_destination, size_t a_capacity )
{
    const size_t key_length = strlen( a_key );
    const char *line = a_trace;
    while( '\0' != *line )
    {
        const char *end = strchr( line, '\n' );
        const size_t line_length = ( NULL != end ) ? (size_t)( end - line ) : strlen( line );
        if( ( line_length > key_length ) && ( 0 == strncmp( line, a_key, key_length ) ) &&
            ( '=' == line[ key_length ] ) )
        {
            const char *value = line + key_length + 1U;
            size_t value_length = line_length - key_length - 1U;
            if( ( value_length > 0U ) && ( '\r' == value[ value_length - 1U ] ) )
            {
                --value_length;
            }
            if( value_length >= a_capacity )
            {
                value_length = a_capacity - 1U;
            }
            memcpy( a_destination, value, value_length );
            a_destination[ value_length ] = '\0';
            return;
        }
        if( NULL == end )
        {
            return;
        }
        line = end + 1;
    }
}

static esp_err_t http_event( esp_http_client_event_t *a_event )
{
    if( ( NULL == a_event ) || ( HTTP_EVENT_ON_DATA != a_event->event_id ) ||
        ( NULL == a_event->data ) || ( a_event->data_len <= 0 ) )
    {
        return ESP_OK;
    }

    const size_t available = CONNECTIVITY_PROBE_RESPONSE_MAX_LENGTH - response_length;
    const size_t incoming = (size_t)a_event->data_len;
    const size_t copy = ( incoming < available ) ? incoming : available;
    if( copy > 0U )
    {
        memcpy( &response[ response_length ], a_event->data, copy );
        response_length += copy;
        response[ response_length ] = '\0';
    }
    return ESP_OK;
}

/** @brief Publish the final result and mark the worker finished. */
static void
finish( rfscout_connectivity_probe_state_t a_state, esp_err_t a_error, int a_http_status )
{
    portENTER_CRITICAL( &state_lock );
    if( cancel_requested )
    {
        status.state = RFSCOUT_CONNECTIVITY_PROBE_IDLE;
        status.error = ESP_OK;
        status.http_status = 0;
    }
    else
    {
        status.state = a_state;
        status.error = a_error;
        status.http_status = a_http_status;
        if( RFSCOUT_CONNECTIVITY_PROBE_SUCCEEDED == a_state )
        {
            copy_trace_value( response, "ip", status.public_ip, sizeof( status.public_ip ) );
            copy_trace_value( response, "colo", status.colo, sizeof( status.colo ) );
        }
    }
    worker_task = NULL;
    portEXIT_CRITICAL( &state_lock );
}

static void probe_worker( void *a_context )
{
    (void)a_context;

    response_length = 0U;
    response[ 0 ] = '\0';
    const esp_http_client_config_t config = {
        .url = CONNECTIVITY_PROBE_URL,
        .event_handler = http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = (int)request_timeout_ms,
        .disable_auto_redirect = true,
        .user_agent = "RFScout Hotspots",
    };

    esp_http_client_handle_t client = esp_http_client_init( &config );
    if( NULL == client )
    {
        finish( RFSCOUT_CONNECTIVITY_PROBE_FAILED, ESP_ERR_NO_MEM, 0 );
        vTaskDelete( NULL );
        return;
    }

    (void)xSemaphoreTake( client_mutex, portMAX_DELAY );
    active_client = client;
    bool cancelled;
    portENTER_CRITICAL( &state_lock );
    cancelled = cancel_requested;
    portEXIT_CRITICAL( &state_lock );
    (void)xSemaphoreGive( client_mutex );

    esp_err_t result = ESP_ERR_INVALID_STATE;
    int http_status = 0;
    if( !cancelled )
    {
        result = esp_http_client_perform( client );
        http_status = esp_http_client_get_status_code( client );
    }

    /* Clear the shared handle under the cancellation mutex before cleanup so a concurrent cancel
       either sees a valid client or none at all. */
    (void)xSemaphoreTake( client_mutex, portMAX_DELAY );
    active_client = NULL;
    (void)xSemaphoreGive( client_mutex );
    esp_http_client_cleanup( client );

    if( ( ESP_OK == result ) && ( 200 == http_status ) )
    {
        finish( RFSCOUT_CONNECTIVITY_PROBE_SUCCEEDED, ESP_OK, http_status );
    }
    else
    {
        if( !cancelled )
        {
            ESP_LOGI( TAG,
                      "HTTPS probe did not pass: error=%s http=%d",
                      esp_err_to_name( result ),
                      http_status );
        }
        finish( RFSCOUT_CONNECTIVITY_PROBE_FAILED,
                ( ESP_OK == result ) ? ESP_FAIL : result,
                http_status );
    }
    vTaskDelete( NULL );
}

esp_err_t connectivity_probe_init( void )
{
    if( NULL != client_mutex )
    {
        return ESP_OK;
    }

    client_mutex = xSemaphoreCreateMutexStatic( &client_mutex_control );
    if( NULL == client_mutex )
    {
        return ESP_ERR_NO_MEM;
    }

    memset( &status, 0, sizeof( status ) );
    status.state = RFSCOUT_CONNECTIVITY_PROBE_IDLE;
    return ESP_OK;
}

esp_err_t connectivity_probe_start( uint32_t a_timeout_ms )
{
    if( NULL == client_mutex )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( 0U == a_timeout_ms )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    if( RFSCOUT_CONNECTIVITY_PROBE_RUNNING == status.state )
    {
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_INVALID_STATE;
    }
    memset( &status, 0, sizeof( status ) );
    status.state = RFSCOUT_CONNECTIVITY_PROBE_RUNNING;
    request_timeout_ms = a_timeout_ms;
    cancel_requested = false;
    portEXIT_CRITICAL( &state_lock );

    TaskHandle_t created = NULL;
    if( pdPASS != xTaskCreate( probe_worker,
                               "net_probe",
                               CONNECTIVITY_PROBE_TASK_STACK,
                               NULL,
                               CONNECTIVITY_PROBE_TASK_PRIORITY,
                               &created ) )
    {
        portENTER_CRITICAL( &state_lock );
        status.state = RFSCOUT_CONNECTIVITY_PROBE_FAILED;
        status.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL( &state_lock );
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL( &state_lock );
    /* The worker may already have finished and cleared worker_task; only record a live worker. */
    if( RFSCOUT_CONNECTIVITY_PROBE_RUNNING == status.state )
    {
        worker_task = created;
    }
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

void connectivity_probe_status( rfscout_connectivity_probe_status_t *a_status )
{
    if( NULL == a_status )
    {
        return;
    }
    portENTER_CRITICAL( &state_lock );
    *a_status = status;
    portEXIT_CRITICAL( &state_lock );
}

bool connectivity_probe_is_active( void )
{
    bool active;
    portENTER_CRITICAL( &state_lock );
    active = ( RFSCOUT_CONNECTIVITY_PROBE_RUNNING == status.state );
    portEXIT_CRITICAL( &state_lock );
    return active;
}

esp_err_t connectivity_probe_stop( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool running = ( RFSCOUT_CONNECTIVITY_PROBE_RUNNING == status.state );
    cancel_requested = running;
    portEXIT_CRITICAL( &state_lock );

    if( running && ( NULL != client_mutex ) )
    {
        (void)xSemaphoreTake( client_mutex, portMAX_DELAY );
        if( NULL != active_client )
        {
            (void)esp_http_client_cancel_request( active_client );
        }
        (void)xSemaphoreGive( client_mutex );
    }

    const int64_t deadline_us =
        esp_timer_get_time() + (int64_t)CONNECTIVITY_PROBE_STOP_TIMEOUT_MS * 1000LL;
    while( connectivity_probe_is_active() )
    {
        if( esp_timer_get_time() >= deadline_us )
        {
            ESP_LOGW(
                TAG, "probe worker did not stop within %u ms", CONNECTIVITY_PROBE_STOP_TIMEOUT_MS );
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( CONNECTIVITY_PROBE_STOP_POLL_MS ) );
    }

    portENTER_CRITICAL( &state_lock );
    memset( &status, 0, sizeof( status ) );
    status.state = RFSCOUT_CONNECTIVITY_PROBE_IDLE;
    cancel_requested = false;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}
