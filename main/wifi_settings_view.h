/**
    wifi_settings_view.h : RFScout WiFi/BT application settings view
    Description: Renders audible surveillance-alert settings for the WiFi/BT application.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_SETTINGS_VIEW_H_
#define _RFSCOUT_WIFI_SETTINGS_VIEW_H_

#include <stdbool.h>

/** @brief Render the WiFi/BT settings menu. */
void wifi_settings_view_render( unsigned a_selected, bool a_editing );

#endif // _RFSCOUT_WIFI_SETTINGS_VIEW_H_
