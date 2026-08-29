/**
    device_view.h : RFScout Home and Device settings interface
    Description: Renders the branded Home landing screen and edits global configuration and authoritative wall-clock settings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_VIEW_H_
#define _RFSCOUT_DEVICE_VIEW_H_

#include <stdbool.h>

/** @brief Render the Home pass-through landing screen without taking encoder ownership. */
void device_view_landing( void );

/** @brief Enter Device settings with Back selected first. */
void device_view_enter( void );

/** @brief Render current Device settings state. */
void device_view_render( void );

/**
 * @brief Handle Device settings button/encoder input.
 * @return True when the user selected Back and settings should close.
 */
bool device_view_input( bool a_pressed, int a_delta );

#endif // _RFSCOUT_DEVICE_VIEW_H_
