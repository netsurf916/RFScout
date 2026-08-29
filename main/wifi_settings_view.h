/**
    wifi_settings_view.h : RFScout WiFi/BT application settings view
    Description: Renders app-scoped notification and live-detection actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_SETTINGS_VIEW_H_
#define _RFSCOUT_WIFI_SETTINGS_VIEW_H_

#include <stdbool.h>

/** @brief Render the WiFi/BT action menu. */
void wifi_settings_view_render( unsigned a_selected, bool a_notifications_enabled );

#endif // _RFSCOUT_WIFI_SETTINGS_VIEW_H_
