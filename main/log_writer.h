/**
    log_writer.h : RFScout asynchronous SD log writer
    Description: Queues device lifecycle and application records for bounded rolling logs.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_LOG_WRITER_H_
#define _RFSCOUT_LOG_WRITER_H_

#include "observation_store.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_LOG_SEGMENT_COUNT 100U
#define RFSCOUT_LOG_SEGMENT_SIZE ( 4U * 1024U * 1024U )
#define RFSCOUT_LOG_FREE_RESERVE ( 64ULL * 1024ULL * 1024ULL )

/** Start asynchronous SD-card logging. */
esp_err_t log_writer_init( void );

/** Queue a Wi-Fi/BLE live-list lifecycle event without filesystem I/O. */
bool log_writer_device_event(
    observation_store_event_t a_event,
    const rf_device_t *a_device );

/** Observation-store callback forwarding lifecycle events to the log queue. */
void log_writer_store_event(
    observation_store_event_t a_event,
    const rf_device_t *a_device,
    void *a_context );

/**
 * @brief Queue an application-owned SD log record without filesystem I/O.
 * @param a_app Stable application name, for example CC1101 or AUDIO.
 * @param a_detail Key/value detail text owned by the application.
 * @return True when queued; false when logging is unavailable or congested.
 * @details The writer adds the timestamp and APP marker. Callers should keep
 *          detail text compact and should rate-limit periodic telemetry.
 */
bool log_writer_app_record( const char *a_app, const char *a_detail );

/** Flush queued records, close the active file, and stop the writer task. */
bool log_writer_shutdown( uint32_t a_timeout_ms );

/** Return the number of records dropped because the queue was full/write failed. */
uint32_t log_writer_dropped_count( void );

/** Return sticky SD logging I/O-error state. */
bool log_writer_has_error( void );

#endif // _RFSCOUT_LOG_WRITER_H_
