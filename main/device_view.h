/**
    device_view.h : RFScout Device application interface
    Description: Renders and edits global configuration and authoritative wall-clock settings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_VIEW_H_
#define _RFSCOUT_DEVICE_VIEW_H_

#include <stdbool.h>

/** @brief Enter the Device application and render its initial state. */
void device_view_enter( void );

/** @brief Render current Device application state. */
void device_view_render( void );

/**
 * @brief Handle Device app button/encoder input.
 * @return True when the user selected Back and app navigation should resume.
 */
bool device_view_input( bool a_pressed, int a_delta );

#endif // _RFSCOUT_DEVICE_VIEW_H_
