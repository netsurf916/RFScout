/**
    wifi_settings_view.h : RFScout WiFi/BT application settings view
    Description: Renders app-scoped notification, retention, and live-detection actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_SETTINGS_VIEW_H_
#define _RFSCOUT_WIFI_SETTINGS_VIEW_H_

#include "device_config.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Render the WiFi/BT action and retention menu. */
void wifi_settings_view_render( unsigned a_selected,
                                bool a_editing,
                                bool a_app_locked,
                                bool a_notifications_enabled,
                                rfscout_evidence_strength_t a_threshold,
                                uint16_t a_wifi_timeout_seconds,
                                uint16_t a_ble_timeout_seconds,
                                bool a_wifi_enabled,
                                bool a_ble_enabled );

#endif // _RFSCOUT_WIFI_SETTINGS_VIEW_H_
