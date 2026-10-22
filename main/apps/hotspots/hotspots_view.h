/**
    apps/hotspots/hotspots_view.h : Hotspots presentation
    Description: Renders landing summaries, menus with network rows, and detail screens from
                 prepared view models. No model or radio access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_HOTSPOTS_VIEW_H_
#define _RFSCOUT_HOTSPOTS_VIEW_H_

#include "ui_menu.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Detail screens show at most this many text lines and actions. */
#define HOTSPOTS_VIEW_DETAIL_LINES 10U
#define HOTSPOTS_VIEW_DETAIL_ACTIONS 3U

/** @brief Semantic color role for a status tag; the tag text always carries the meaning too. */
typedef enum
{
    HOTSPOTS_TONE_NEUTRAL = 0,
    HOTSPOTS_TONE_GOOD,
    HOTSPOTS_TONE_LIMITED,
    HOTSPOTS_TONE_BAD,
    HOTSPOTS_TONE_ACTIVE
} hotspots_view_tone_t;

/** @brief One network or BSSID row; strings are borrowed for the duration of the render call. */
typedef struct
{
    const char *label;
    const char *tag;
    hotspots_view_tone_t tone;
    const char *value; /**< right column: RSSI, distance or "--" */
    uint16_t members;  /**< BSSIDs represented; 0 hides the count column */
    bool visible;      /**< false dims the row (not currently visible) */
} hotspots_view_row_t;

/** @brief Prepared detail text; strings are borrowed for the duration of the render call. */
typedef struct
{
    const char *lines[ HOTSPOTS_VIEW_DETAIL_LINES ];
    size_t line_count;
    size_t status_line; /**< line drawn in the status tone */
    hotspots_view_tone_t status_tone;
    ui_menu_row_t actions[ HOTSPOTS_VIEW_DETAIL_ACTIONS ];
    size_t action_count;
} hotspots_view_detail_t;

/** @brief Render a non-interactive summary of the strongest/nearest rows. */
void hotspots_view_landing( const char *a_title,
                            const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            const char *a_empty,
                            const char *a_footer );

/**
 * @brief Render a menu: fixed label/value rows, then one row per network or BSSID.
 * @param a_selected Index across fixed rows and network rows.
 */
void hotspots_view_menu( const char *a_title,
                         const ui_menu_row_t *a_fixed,
                         size_t a_fixed_count,
                         const hotspots_view_row_t *a_rows,
                         size_t a_count,
                         unsigned a_selected,
                         const char *a_footer );

/** @brief Render detail lines with the action rows below them. */
void hotspots_view_detail( const char *a_title,
                           const hotspots_view_detail_t *a_detail,
                           unsigned a_selected_action );

#endif // _RFSCOUT_HOTSPOTS_VIEW_H_
