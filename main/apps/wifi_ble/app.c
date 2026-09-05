/**
    apps/wifi_ble/app.c : WiFi/BLE application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

const rfscout_app_descriptor_t rfscout_wifi_ble_app = {
    .id = RFSCOUT_APP_WIFI_BT,
    .name = "WiFi / BLE",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_WIFI_MONITOR | RFSCOUT_APP_SERVICE_BLE_MONITOR,
};
