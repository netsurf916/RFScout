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

/** @brief Start or resume the breadcrumb worker when SD storage is available. */
esp_err_t breadcrumbs_init( void );

/**
 * @brief Queue a fresh GPS fix once the receiver has moved by the shared movement trigger.
 * @details Velocity-adaptive 5s / 2s / 1s minimum cadence, with one per GPS UTC second max.
 */
void breadcrumbs_poll( void );

/** @brief Return whether breadcrumb recording is enabled. */
bool breadcrumbs_enabled( void );

/** @brief Persist breadcrumb recording enablement before publishing the new setting. */
esp_err_t breadcrumbs_set_enabled( bool a_enabled );

/** @brief Drain queued writes and quiesce the breadcrumb worker before terminal storage shutdown. */
bool breadcrumbs_shutdown( uint32_t a_timeout_ms );

#endif // _RFSCOUT_BREADCRUMBS_H_
