/**
    network_session.c : RFScout framework-owned infrastructure network session
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "network_session.h"

#include "esp_log.h"

#include <string.h>

#define NETWORK_SESSION_CONNECT_TIMEOUT_US 30000000LL
#define NETWORK_SESSION_NTP_TIMEOUT_US 15000000LL

static const char *TAG = "network_session";
static rfscout_network_session_status_t status;
static int64_t phase_started_us;

static void advance_generation( void )
{
    ++status.generation;
    if( 0U == status.generation )
    {
        ++status.generation;
    }
}

static void reset_logical_state( void )
{
    const uint32_t generation = status.generation;
    memset( &status, 0, sizeof( status ) );
    status.generation = generation;
    status.state = RFSCOUT_NETWORK_SESSION_IDLE;
    phase_started_us = 0;
}

static void fail_session( esp_err_t a_error,
                          rfscout_network_session_failure_t a_failure,
                          int32_t a_disconnect_reason )
{
    const esp_err_t stop_result = rfscout_wifi_station_stop();
    status.state = RFSCOUT_NETWORK_SESSION_FAILED;
    status.error = ( ESP_OK == stop_result ) ? a_error : stop_result;
    status.failure = ( ESP_OK == stop_result ) ? a_failure : RFSCOUT_NETWORK_SESSION_FAILURE_TEARDOWN;
    status.disconnect_reason = a_disconnect_reason;
    phase_started_us = 0;
    if( ESP_OK != stop_result )
    {
        ESP_LOGW( TAG,
                  "station cleanup after network failure failed: %s",
                  esp_err_to_name( stop_result ) );
    }
}

static esp_err_t prepare_begin( const char *a_ssid )
{
    if( NULL == a_ssid )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( RFSCOUT_NETWORK_SESSION_IDLE != status.state )
    {
        const esp_err_t release_result = network_session_release();
        if( ESP_OK != release_result )
        {
            return release_result;
        }
    }

    advance_generation();
    memset( status.ssid, 0, sizeof( status.ssid ) );
    strlcpy( status.ssid, a_ssid, sizeof( status.ssid ) );
    memset( &status.ip, 0, sizeof( status.ip ) );
    status.error = ESP_OK;
    status.disconnect_reason = 0;
    status.state = RFSCOUT_NETWORK_SESSION_ASSOCIATING;
    phase_started_us = 0;
    return ESP_OK;
}

esp_err_t network_session_begin_saved( const char *a_ssid, const char *a_password )
{
    esp_err_t result = prepare_begin( a_ssid );
    if( ESP_OK == result )
    {
        result = rfscout_wifi_station_connect( a_ssid, a_password, true );
    }
    if( ESP_OK != result )
    {
        fail_session( result, RFSCOUT_NETWORK_SESSION_FAILURE_ASSOCIATION, 0 );
    }
    return result;
}

esp_err_t network_session_begin_bss( const rfscout_wifi_station_bss_t *a_bss )
{
    if( NULL == a_bss )
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = prepare_begin( a_bss->ssid );
    if( ESP_OK == result )
    {
        result = rfscout_wifi_station_connect_open_bss( a_bss, true );
    }
    if( ESP_OK != result )
    {
        fail_session( result, RFSCOUT_NETWORK_SESSION_FAILURE_ASSOCIATION, 0 );
    }
    return result;
}

void network_session_poll( int64_t a_now_us )
{
    if( RFSCOUT_NETWORK_SESSION_ASSOCIATING != status.state &&
        RFSCOUT_NETWORK_SESSION_TIME_SYNCING != status.state )
    {
        return;
    }

    if( 0 == phase_started_us )
    {
        phase_started_us = a_now_us;
    }

    rfscout_wifi_station_status_t station;
    rfscout_wifi_station_status( &station );

    if( RFSCOUT_NETWORK_SESSION_ASSOCIATING == status.state )
    {
        if( RFSCOUT_WIFI_STATION_FAILED == station.state )
        {
            fail_session( ESP_FAIL, RFSCOUT_NETWORK_SESSION_FAILURE_ASSOCIATION, station.disconnect_reason );
            return;
        }
        if( RFSCOUT_WIFI_STATION_CONNECTED == station.state )
        {
            (void)rfscout_wifi_station_ip_info( &status.ip );
            const esp_err_t result = rfscout_wifi_station_start_time_sync();
            if( ESP_OK != result )
            {
                fail_session( result, RFSCOUT_NETWORK_SESSION_FAILURE_TIME_SYNC, station.disconnect_reason );
                return;
            }
            status.state = RFSCOUT_NETWORK_SESSION_TIME_SYNCING;
            phase_started_us = a_now_us;
            return;
        }
        if( a_now_us - phase_started_us >= NETWORK_SESSION_CONNECT_TIMEOUT_US )
        {
            fail_session( ESP_ERR_TIMEOUT, RFSCOUT_NETWORK_SESSION_FAILURE_ASSOCIATION, station.disconnect_reason );
        }
        return;
    }

    if( RFSCOUT_WIFI_STATION_CONNECTED != station.state )
    {
        fail_session( ESP_ERR_INVALID_STATE, RFSCOUT_NETWORK_SESSION_FAILURE_TIME_SYNC, station.disconnect_reason );
        return;
    }
    if( station.ntp_synced )
    {
        status.state = RFSCOUT_NETWORK_SESSION_READY;
        status.error = ESP_OK;
        status.disconnect_reason = 0;
        phase_started_us = 0;
        return;
    }
    if( !station.ntp_syncing ||
        a_now_us - phase_started_us >= NETWORK_SESSION_NTP_TIMEOUT_US )
    {
        fail_session( ESP_ERR_TIMEOUT, RFSCOUT_NETWORK_SESSION_FAILURE_TIME_SYNC, station.disconnect_reason );
    }
}

void network_session_status( rfscout_network_session_status_t *a_status )
{
    if( NULL != a_status )
    {
        *a_status = status;
    }
}

esp_err_t network_session_release( void )
{
    const esp_err_t result = rfscout_wifi_station_stop();
    if( ESP_OK == result )
    {
        reset_logical_state();
    }
    else
    {
        status.state = RFSCOUT_NETWORK_SESSION_FAILED;
        status.error = result;
        status.failure = RFSCOUT_NETWORK_SESSION_FAILURE_TEARDOWN;
        phase_started_us = 0;
    }
    return result;
}

esp_err_t network_session_stop( void )
{
    return network_session_release();
}

bool network_session_is_active( void )
{
    return RFSCOUT_NETWORK_SESSION_IDLE != status.state || rfscout_wifi_station_is_active();
}
