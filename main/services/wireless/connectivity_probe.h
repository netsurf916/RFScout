/**
    connectivity_probe.h : RFScout outbound connectivity probe
    Description: Session-managed, cancellable, certificate-validated HTTPS viability check.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CONNECTIVITY_PROBE_H_
#define _RFSCOUT_CONNECTIVITY_PROBE_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_CONNECTIVITY_PROBE_PUBLIC_IP_MAX_LENGTH 45U
#define RFSCOUT_CONNECTIVITY_PROBE_COLO_MAX_LENGTH 15U

/** @brief Lifecycle state of the single connectivity-probe request. */
typedef enum
{
    RFSCOUT_CONNECTIVITY_PROBE_IDLE = 0,
    RFSCOUT_CONNECTIVITY_PROBE_RUNNING,
    RFSCOUT_CONNECTIVITY_PROBE_SUCCEEDED,
    RFSCOUT_CONNECTIVITY_PROBE_FAILED
} rfscout_connectivity_probe_state_t;

/** @brief Result snapshot of the most recent probe request. */
typedef struct
{
    rfscout_connectivity_probe_state_t state;
    esp_err_t error;
    int http_status;
    char public_ip[ RFSCOUT_CONNECTIVITY_PROBE_PUBLIC_IP_MAX_LENGTH + 1U ];
    char colo[ RFSCOUT_CONNECTIVITY_PROBE_COLO_MAX_LENGTH + 1U ];
} rfscout_connectivity_probe_status_t;

/** @brief Initialize reusable probe synchronization state without starting a request. */
esp_err_t connectivity_probe_init( void );

/**
 * @brief Start one verified HTTPS request to Cloudflare /cdn-cgi/trace on the current connection.
 * @param a_timeout_ms Request timeout; the whole request is also bounded by it.
 * @return ESP_OK when the worker started, ESP_ERR_INVALID_STATE when a request is still running.
 * @details SUCCEEDED means HTTP 200; public egress IP and Cloudflare colo are parsed from the body.
 */
esp_err_t connectivity_probe_begin( uint32_t a_timeout_ms );

/** @brief Copy the latest probe status into caller-owned storage. */
void connectivity_probe_status( rfscout_connectivity_probe_status_t *a_status );

/** @brief Return whether a probe worker is currently running. */
bool connectivity_probe_is_active( void );

/**
 * @brief Cancel any running request, wait for the worker to exit, and reset status to IDLE.
 * @details Session-service stop: success proves no probe worker can still run.
 * @return ESP_OK, or ESP_ERR_TIMEOUT if the worker did not exit within the bounded wait.
 */
esp_err_t connectivity_probe_stop( void );

#endif // _RFSCOUT_CONNECTIVITY_PROBE_H_
