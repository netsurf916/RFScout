/**
    status_bar.h : RFScout top status bar interface
    Description: Snapshots and renders compact right-aligned global system status.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STATUS_BAR_H_
#define _RFSCOUT_STATUS_BAR_H_

#include <stdbool.h>

#define RFSCOUT_STATUS_CLOCK_MAX 12

/** @brief Snapshot of global status consumed by the shared top chrome. */
typedef struct
{
    bool input_locked;
    bool sd_available;
    bool storage_error;
    bool thermal_available;
    bool gps_available;
    bool gps_fix_valid;
    char clock[ RFSCOUT_STATUS_CLOCK_MAX ];
} rfscout_status_bar_state_t;

/**
 * @brief Populate a coherent snapshot of global status data.
 * @param a_state Destination snapshot.
 * @return true when a_state was valid and populated.
 * @details Hardware/services remain responsible for their own state; this function only reads it.
 */
bool status_bar_snapshot( rfscout_status_bar_state_t *a_state );

/**
 * @brief Draw one previously populated status snapshot into the LCD framebuffer.
 * @param a_state Snapshot to render.
 */
void status_bar_draw_state( const rfscout_status_bar_state_t *a_state );

/**
 * @brief Snapshot and draw the current global status into the LCD framebuffer.
 * @details Icons populate from right to left in one canonical order. Applications should normally
 *          use ui_chrome_draw() rather than calling this directly.
 */
void status_bar_draw( void );

/**
 * @brief Refresh the status-bar framebuffer region only when global status has changed.
 *
 * Intended for the main periodic UI loop so asynchronous state such as the input lock and clock can
 * update even while the active application itself is static.
 */
void status_bar_poll( void );

#endif // _RFSCOUT_STATUS_BAR_H_
