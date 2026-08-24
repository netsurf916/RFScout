/**
    log_writer.h : RFScout asynchronous SD log writer
    Description: Queues live-list lifecycle events for bounded rolling text logs.
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

/**
 * @brief Start asynchronous SD-card logging.
 * @return ESP_OK when logging is active or an error when storage is unavailable.
 */
esp_err_t log_writer_init( void );

/**
 * @brief Queue a live-list lifecycle event without performing filesystem I/O.
 * @param a_event Device-list membership event to serialize.
 * @param a_device Snapshot of the device being added or dropped.
 * @return True when queued, false when logging is disabled or the queue is full.
 * @details Added entries include surveillance signature, confidence, and evidence.
 *          Dropped entries intentionally record only the device identity and reason.
 */
bool log_writer_device_event(
    observation_store_event_t a_event,
    const rf_device_t *a_device );

/**
 * @brief Observation-store callback that forwards lifecycle events to the log queue.
 * @param a_event Device-list membership event.
 * @param a_device Snapshot of the device being added or dropped.
 * @param a_context Unused callback context.
 */
void log_writer_store_event(
    observation_store_event_t a_event,
    const rf_device_t *a_device,
    void *a_context );

/**
 * @brief Flush queued records, close the active file, and stop the writer task.
 * @param a_timeout_ms Maximum time to wait for queued records to drain.
 * @return True only when the worker has stopped and storage may be unmounted.
 * @details If the timeout expires, shutdown is cancelled and logging resumes so
 *          callers can safely leave the filesystem mounted and continue running.
 */
bool log_writer_shutdown( uint32_t a_timeout_ms );

/** @brief Return the number of records dropped because the queue was full. */
uint32_t log_writer_dropped_count( void );

/**
 * @brief Determine whether the SD logging path has encountered an I/O error.
 * @return True after an open, write, flush, sync, close, or rotation failure.
 * @details The state is sticky for the current runtime so transient media
 *          failures remain visible in the status bar instead of disappearing.
 */
bool log_writer_has_error( void );

#endif // _RFSCOUT_LOG_WRITER_H_
