/**
    log_writer.h : RFScout asynchronous SD log writer
    Description: Queues normalized observations for bounded rolling text logs.
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
 * @brief Queue a normalized RF observation without performing filesystem I/O.
 * @param a_observation Observation to serialize on the writer task.
 * @return True when queued, false when logging is disabled or the queue is full.
 */
bool log_writer_observation( const rf_device_t *a_observation );

/**
 * @brief Flush queued records, close the active file, and stop the writer task.
 * @param a_timeout_ms Maximum time to wait for queued records to drain.
 */
void log_writer_shutdown( uint32_t a_timeout_ms );

/**
 * @brief Return the number of records dropped because the queue was full.
 */
uint32_t log_writer_dropped_count( void );

/**
 * @brief Determine whether the SD logging path has encountered an I/O error.
 * @return True after an open, write, flush, sync, or close failure.
 * @details The state is sticky for the current runtime so transient media
 *          failures remain visible in the status bar instead of disappearing.
 */
bool log_writer_has_error( void );

#endif // _RFSCOUT_LOG_WRITER_H_
