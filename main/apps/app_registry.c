/**
    apps/app_registry.c : RFScout compiled-in application registry
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_registry.h"

extern const rfscout_app_descriptor_t rfscout_system_events_app;
extern const rfscout_app_descriptor_t rfscout_home_app;
extern const rfscout_app_descriptor_t rfscout_wifi_ble_app;
extern const rfscout_app_descriptor_t rfscout_cc1101_app;
extern const rfscout_app_descriptor_t rfscout_nrf24_app;
extern const rfscout_app_descriptor_t rfscout_recorder_app;
extern const rfscout_app_descriptor_t rfscout_thermal_app;

const rfscout_app_descriptor_t *const rfscout_app_registry[] = {
    &rfscout_system_events_app,
    &rfscout_home_app,
    &rfscout_wifi_ble_app,
    &rfscout_cc1101_app,
    &rfscout_nrf24_app,
    &rfscout_recorder_app,
    &rfscout_thermal_app,
};

const size_t rfscout_app_registry_count =
    sizeof( rfscout_app_registry ) / sizeof( rfscout_app_registry[ 0 ] );
