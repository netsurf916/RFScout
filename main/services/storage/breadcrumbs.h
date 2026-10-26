/**
    breadcrumbs.h : RFScout GPS breadcrumb recorder
    Description: Appends GPS-UTC location breadcrumbs to dated SD files.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BREADCRUMBS_H_
#define _RFSCOUT_BREADCRUMBS_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Dated-file lifetime. Forever means never automatically prune. */
typedef enum
{
    BREADCRUMB_RETENTION_DAY = 0,
    BREADCRUMB_RETENTION_WEEK,
    BREADCRUMB_RETENTION_MONTH,
    BREADCRUMB_RETENTION_FOREVER,
    BREADCRUMB_RETENTION_COUNT
} breadcrumb_retention_t;

/** @brief Start or resume the breadcrumb worker when SD storage is available. */
esp_err_t breadcrumbs_init( void );

/**
 * @brief Queue a fresh GPS fix once the receiver has moved by the shared movement trigger.
 * @details Velocity-adaptive 60-to-1-second cadence and a 60-second stationary heartbeat,
 *          never more than one record per GPS UTC second.
 */
void breadcrumbs_poll( void );

/** @brief Return whether breadcrumb recording is enabled. */
bool breadcrumbs_enabled( void );

/** @brief Persist breadcrumb recording enablement before publishing the new setting. */
esp_err_t breadcrumbs_set_enabled( bool a_enabled );

/** @brief Current persisted retention duration (default Forever, including upgrades). */
breadcrumb_retention_t breadcrumbs_retention( void );
/** @brief Human-readable label for the specified retention selection. */
const char *breadcrumbs_retention_name( breadcrumb_retention_t a_retention );
/** @brief Wrap to the next or previous supported retention selection. */
breadcrumb_retention_t breadcrumbs_retention_step( breadcrumb_retention_t a_current,
                                                   int a_direction );
/** @brief Persist a new duration and request bounded asynchronous cleanup. */
esp_err_t breadcrumbs_set_retention( breadcrumb_retention_t a_retention );

/** @brief Drain queued writes and quiesce the breadcrumb worker before terminal storage shutdown. */
bool breadcrumbs_shutdown( uint32_t a_timeout_ms );

#endif // _RFSCOUT_BREADCRUMBS_H_
