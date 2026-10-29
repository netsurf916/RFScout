/**
    connectivity_probe.c : RFScout outbound connectivity probe
    Description: Runs one bounded, cancellable, certificate-validated HTTPS request in a worker task.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "connectivity_probe.h"
#include "log_writer.h"
#include "service_fingerprint.h"
#include "probe_http_capture.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
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

typedef enum
{
    PROBE_MODE_INTERNET = 0,
    PROBE_MODE_CAMERA
} probe_mode_t;

static probe_mode_t mode;
static rfscout_service_fingerprint_t camera_fingerprint;
static rfscout_probe_http_capture_t camera_headers;
static char camera_url[ 32 ];

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
    if( NULL != a_event && PROBE_MODE_CAMERA == mode && HTTP_EVENT_ON_HEADER == a_event->event_id &&
        NULL != a_event->header_key && NULL != a_event->header_value )
    {
        service_fingerprint_http_header( &camera_fingerprint,
                                         a_event->header_key,
                                         strlen( a_event->header_key ),
                                         a_event->header_value,
                                         strlen( a_event->header_value ) );
        probe_http_capture_header( &camera_headers, a_event->header_key, a_event->header_value );
    }
    if( PROBE_MODE_CAMERA == mode )
    {
        return ESP_OK; /* HEAD cannot download response bodies. */
    }
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
            if( PROBE_MODE_CAMERA == mode )
            {
                status.camera_evidence = camera_fingerprint.evidence;
            }
            else
            {
                copy_trace_value( response, "ip", status.public_ip, sizeof( status.public_ip ) );
                copy_trace_value( response, "colo", status.colo, sizeof( status.colo ) );
            }
        }
    }
    worker_task = NULL;
    portEXIT_CRITICAL( &state_lock );
}

/* Temporary diagnostics only: measure before emitting asynchronous SD records. */
static void log_probe_timing( const char *a_phase,
                              probe_mode_t a_mode,
                              int64_t a_elapsed_us,
                              int64_t a_request_us,
                              esp_err_t a_error,
                              int a_http_status )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "connectivity_probe_timing" ) && log_str( &record, "phase", a_phase ) &&
        log_str( &record, "mode", PROBE_MODE_CAMERA == a_mode ? "gateway" : "https" ) &&
        log_int( &record, "elapsed_us", a_elapsed_us ) &&
        log_int( &record, "request_us", a_request_us ) &&
        log_str( &record, "error", esp_err_to_name( a_error ) ) &&
        log_int( &record, "http_status", a_http_status ) )
    {
        (void)log_end( &record );
    }
}

static void log_probe_stop_timing( probe_mode_t a_mode,
                                   int64_t a_elapsed_us,
                                   int64_t a_cancel_us,
                                   int64_t a_join_us,
                                   bool a_timeout )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "connectivity_probe_timing" ) && log_str( &record, "phase", "stop" ) &&
        log_str( &record, "mode", PROBE_MODE_CAMERA == a_mode ? "gateway" : "https" ) &&
        log_int( &record, "elapsed_us", a_elapsed_us ) &&
        log_int( &record, "cancel_us", a_cancel_us ) && log_int( &record, "join_us", a_join_us ) &&
        log_bool( &record, "timeout", a_timeout ) )
    {
        (void)log_end( &record );
    }
}

static void probe_worker( void *a_context )
{
    (void)a_context;

    const int64_t worker_started_us = esp_timer_get_time();
    const probe_mode_t worker_mode = mode;
    log_probe_timing( "begin", worker_mode, 0, 0, ESP_OK, 0 );

    response_length = 0U;
    response[ 0 ] = '\0';
    const esp_http_client_config_t config = {
        .url = PROBE_MODE_CAMERA == mode ? camera_url : CONNECTIVITY_PROBE_URL,
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

    if( PROBE_MODE_CAMERA == mode )
    {
        (void)esp_http_client_set_method( client, HTTP_METHOD_HEAD );
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
    int64_t request_us = 0;
    if( !cancelled )
    {
        const int64_t request_started_us = esp_timer_get_time();
        result = esp_http_client_perform( client );
        request_us = esp_timer_get_time() - request_started_us;
        http_status = esp_http_client_get_status_code( client );
        if( ESP_OK != result )
        {
            int tls_code = 0;
            int tls_flags = 0;
            const int socket_errno = esp_http_client_get_errno( client );
            const esp_err_t tls_error =
                esp_http_client_get_and_clear_last_tls_error( client, &tls_code, &tls_flags );
            ESP_LOGW( TAG,
                      "HTTPS transport error: error=%s errno=%d tls=%s tls_code=%d "
                      "tls_flags=0x%x",
                      esp_err_to_name( result ),
                      socket_errno,
                      esp_err_to_name( tls_error ),
                      tls_code,
                      (unsigned)tls_flags );
        }
    }

    /* Clear the shared handle under the cancellation mutex before cleanup so a concurrent cancel
       either sees a valid client or none at all. */
    (void)xSemaphoreTake( client_mutex, portMAX_DELAY );
    active_client = NULL;
    (void)xSemaphoreGive( client_mutex );
    esp_http_client_cleanup( client );

    if( ESP_OK == result && ( ( PROBE_MODE_CAMERA == mode && http_status >= 100 ) ||
                              ( PROBE_MODE_INTERNET == mode && 200 == http_status ) ) )
    {
        if( PROBE_MODE_CAMERA == mode )
        {
            service_fingerprint_http_status( &camera_fingerprint, (unsigned)http_status );
        }
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

    /* Do not hold the worker-completion signal while the SD logger queues this record. */
    log_probe_timing( "complete",
                      worker_mode,
                      esp_timer_get_time() - worker_started_us,
                      request_us,
                      result,
                      http_status );
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

static esp_err_t begin_probe( uint32_t a_timeout_ms, probe_mode_t a_mode, uint32_t a_gateway )
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
    mode = a_mode;
    service_fingerprint_reset( &camera_fingerprint );
    probe_http_capture_reset( &camera_headers );
    if( PROBE_MODE_CAMERA == mode )
    {
        /* lwIP gateway fields use network byte order, first octet in low byte on ESP32. */
        (void)snprintf( camera_url,
                        sizeof( camera_url ),
                        "http://%u.%u.%u.%u/",
                        (unsigned)( a_gateway & 0xffU ),
                        (unsigned)( ( a_gateway >> 8U ) & 0xffU ),
                        (unsigned)( ( a_gateway >> 16U ) & 0xffU ),
                        (unsigned)( ( a_gateway >> 24U ) & 0xffU ) );
    }
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

esp_err_t connectivity_probe_begin( uint32_t a_timeout_ms )
{
    return begin_probe( a_timeout_ms, PROBE_MODE_INTERNET, 0U );
}

esp_err_t connectivity_probe_begin_camera( uint32_t a_gateway, uint32_t a_timeout_ms )
{
    if( 0U == a_gateway || UINT32_MAX == a_gateway || a_timeout_ms > 3000U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    return begin_probe( a_timeout_ms, PROBE_MODE_CAMERA, a_gateway );
}

/** @brief Borrow completed HEAD response headers until the next probe begins. */
bool connectivity_probe_camera_headers( const char **a_data, size_t *a_length, bool *a_truncated )
{
    if( NULL == a_data || NULL == a_length || NULL == a_truncated )
    {
        return false;
    }
    portENTER_CRITICAL( &state_lock );
    /* The worker finishes all header callbacks before publishing terminal status.
       Only the app task starts a subsequent request in this session. */
    const bool ready = PROBE_MODE_CAMERA == mode &&
                       RFSCOUT_CONNECTIVITY_PROBE_RUNNING != status.state && NULL == worker_task;
    portEXIT_CRITICAL( &state_lock );
    if( !ready )
    {
        return false;
    }
    *a_data = camera_headers.data;
    *a_length = camera_headers.length;
    *a_truncated = camera_headers.truncated;
    return true;
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
    const int64_t stop_started_us = esp_timer_get_time();
    portENTER_CRITICAL( &state_lock );
    const bool running = ( RFSCOUT_CONNECTIVITY_PROBE_RUNNING == status.state );
    const probe_mode_t stop_mode = mode;
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

    const int64_t join_started_us = esp_timer_get_time();
    const int64_t cancel_us = join_started_us - stop_started_us;
    const int64_t deadline_us =
        join_started_us + (int64_t)CONNECTIVITY_PROBE_STOP_TIMEOUT_MS * 1000LL;
    while( connectivity_probe_is_active() )
    {
        if( esp_timer_get_time() >= deadline_us )
        {
            ESP_LOGW(
                TAG, "probe worker did not stop within %u ms", CONNECTIVITY_PROBE_STOP_TIMEOUT_MS );
            if( running )
            {
                const int64_t now_us = esp_timer_get_time();
                log_probe_stop_timing( stop_mode,
                                       now_us - stop_started_us,
                                       cancel_us,
                                       now_us - join_started_us,
                                       true );
            }
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay( pdMS_TO_TICKS( CONNECTIVITY_PROBE_STOP_POLL_MS ) );
    }

    if( running )
    {
        const int64_t now_us = esp_timer_get_time();
        log_probe_stop_timing(
            stop_mode, now_us - stop_started_us, cancel_us, now_us - join_started_us, false );
    }

    portENTER_CRITICAL( &state_lock );
    memset( &status, 0, sizeof( status ) );
    status.state = RFSCOUT_CONNECTIVITY_PROBE_IDLE;
    cancel_requested = false;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}
