/**
    apps/hotspots/hotspots_view.h : Hotspots presentation
    Description: Renders the landing list, browser, and BSSID detail from prepared view models.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_HOTSPOTS_VIEW_H_
#define _RFSCOUT_HOTSPOTS_VIEW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Detail-view action rows. */
#define HOTSPOTS_DETAIL_ACTION_BACK 0U
#define HOTSPOTS_DETAIL_ACTION_RECHECK 1U
#define HOTSPOTS_DETAIL_ACTION_COUNT 2U

/** Fixed browser rows preceding the access-point rows. */
#define HOTSPOTS_BROWSER_ITEM_BACK 0U
#define HOTSPOTS_BROWSER_ITEM_LOCK 1U
#define HOTSPOTS_BROWSER_ITEM_AUTO 2U
#define HOTSPOTS_BROWSER_FIXED_ITEMS 3U

/** @brief Semantic color role for a status tag. */
typedef enum
{
    HOTSPOTS_TONE_NEUTRAL = 0,
    HOTSPOTS_TONE_GOOD,
    HOTSPOTS_TONE_LIMITED,
    HOTSPOTS_TONE_BAD,
    HOTSPOTS_TONE_ACTIVE
} hotspots_view_tone_t;

/** @brief One access-point row; strings are borrowed for the duration of the render call. */
typedef struct
{
    const char *ssid;
    const char *tag;
    hotspots_view_tone_t tone;
    int8_t rssi;
    bool visible;
} hotspots_view_row_t;

/** @brief Prepared detail-view text; strings are borrowed for the duration of the render call. */
typedef struct
{
    const char *ssid;
    const char *bssid;
    const char *radio;
    const char *security;
    const char *status;
    hotspots_view_tone_t status_tone;
    const char *last_attempt;
    const char *addresses;
    const char *network;
    const char *dns;
    const char *egress;
    bool recheck_available;
} hotspots_view_detail_t;

/** @brief Render the non-interactive landing summary. */
void hotspots_view_landing( const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            const char *a_footer );

/** @brief Render the browser: Back, App lock, Auto check, then one row per access point. */
void hotspots_view_browser( const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            unsigned a_selected,
                            bool a_app_locked,
                            bool a_auto_enabled,
                            const char *a_footer );

/** @brief Render one access point's detail with its action rows. */
void hotspots_view_detail( const hotspots_view_detail_t *a_detail, unsigned a_selected_action );

#endif // _RFSCOUT_HOTSPOTS_VIEW_H_
