/**
    wifi_settings_view.h : RFScout WiFi/BT application settings view
    Description: Renders app-scoped notification, retention, and live-detection actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_SETTINGS_VIEW_H_
#define _RFSCOUT_WIFI_SETTINGS_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Render the WiFi/BT action and retention menu. */
void wifi_settings_view_render( unsigned a_selected,
                                bool a_editing,
                                bool a_notifications_enabled,
                                uint16_t a_wifi_timeout_seconds,
                                uint16_t a_ble_timeout_seconds,
                                uint16_t a_surveillance_timeout_seconds );

#endif // _RFSCOUT_WIFI_SETTINGS_VIEW_H_
