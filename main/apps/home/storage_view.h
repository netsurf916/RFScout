/**
    storage_view.h : RFScout Home storage management view
    Description: Shows internal and SD storage use and clears owner-scoped records and media collections.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STORAGE_VIEW_H_
#define _RFSCOUT_STORAGE_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Measure internal and SD storage and render the list with Back selected. */
void storage_view_enter( void );

/** @brief Render the current storage list, confirmation, or deletion progress. */
void storage_view_render( void );

/**
 * @brief Handle storage view button/encoder input.
 * @return True when the user selected Back and the view should close.
 */
bool storage_view_input( bool a_pressed, int a_delta );

/** @brief Advance an SD collection deletion in bounded steps. */
void storage_view_poll( void );

#endif // _RFSCOUT_STORAGE_VIEW_H_
