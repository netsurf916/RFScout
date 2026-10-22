/**
    breadcrumbs.h : RFScout GPS breadcrumb recorder
    Description: Records durable GPS-UTC location breadcrumbs to a bounded SD JSONL journal.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BREADCRUMBS_H_
#define _RFSCOUT_BREADCRUMBS_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT 144U
#define RFSCOUT_BREADCRUMB_FILE_LIMIT_MAX 1008U

/** @brief Start or resume the breadcrumb worker when SD storage is available. */
esp_err_t breadcrumbs_init( void );

/**
 * @brief Queue a fresh GPS fix once the receiver has moved by the shared movement trigger.
 * @details At most one record per second and per GPS UTC second.
 */
void breadcrumbs_poll( void );

/** @brief Return whether breadcrumb recording is enabled. */
bool breadcrumbs_enabled( void );

/** @brief Persist breadcrumb recording enablement before publishing the new setting. */
esp_err_t breadcrumbs_set_enabled( bool a_enabled );

/** @brief Return the configured maximum retained breadcrumb journal segment count. */
uint16_t breadcrumbs_file_limit( void );

/** @brief Next supported retained-segment count in a_direction, wrapping. */
uint16_t breadcrumbs_file_limit_step( uint16_t a_file_limit, int a_direction );

/** @brief Persist a new retained breadcrumb file count and request asynchronous pruning. */
esp_err_t breadcrumbs_set_file_limit( uint16_t a_file_limit );

/** @brief Drain queued writes and quiesce the breadcrumb worker before terminal storage shutdown. */
bool breadcrumbs_shutdown( uint32_t a_timeout_ms );

#endif // _RFSCOUT_BREADCRUMBS_H_
