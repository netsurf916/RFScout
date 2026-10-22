/**
    apps/hotspots/hotspots_ui.h : Hotspots screens and navigation
    Description: Live, History and Saved views with their menus, grouped network and BSSID
                 lists, detail screens and the shared credential screens.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOTS_UI_H_
#define _RFSCOUT_HOTSPOTS_UI_H_

#include "input.h"

#include <stdbool.h>

/** @brief Start on the Live landing view and forget every app-session pointer. */
void hotspots_ui_reset( void );

/** @brief Close the credential screens, erasing any password they hold. */
void hotspots_ui_close( void );

/** @brief Rebuild the current view model and draw it. */
void hotspots_ui_render( void );

/**
 * @brief Handle one input event.
 * @return false when the framework should handle it (landing rotary while unlocked).
 */
bool hotspots_ui_input( const rfscout_input_event_t *a_input );

#endif // _RFSCOUT_HOTSPOTS_UI_H_
