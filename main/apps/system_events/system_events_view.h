/**
    system_events_view.h : RFScout System Events application view
    Description: Renders and browses the volatile dmesg-style system-event history.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SYSTEM_EVENTS_VIEW_H_
#define _RFSCOUT_SYSTEM_EVENTS_VIEW_H_

#include <stdbool.h>

/** @brief Enter the live System Events application view at the newest event. */
void system_events_view_enter( void );

/** @brief Leave the view and discard any browse snapshot. */
void system_events_view_leave( void );

/** @brief Return whether the rotary encoder currently belongs to list browsing. */
bool system_events_view_browsing( void );

/** @brief Toggle between live app-navigation mode and a stable browse snapshot. */
void system_events_view_toggle_browse( void );

/** @brief Scroll the browse snapshot toward older/newer retained entries. */
void system_events_view_scroll( int a_direction );

/** @brief Render the current live or browse state. */
void system_events_view_render( void );

#endif // _RFSCOUT_SYSTEM_EVENTS_VIEW_H_
