/**
    apps/app_registry.c : RFScout compiled-in application registry
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_registry.h"

extern const rfscout_app_descriptor_t rfscout_diagnostics_app;
extern const rfscout_app_descriptor_t rfscout_system_events_app;
extern const rfscout_app_descriptor_t rfscout_home_app;
extern const rfscout_app_descriptor_t rfscout_battery_app;
extern const rfscout_app_descriptor_t rfscout_wifi_ble_app;
extern const rfscout_app_descriptor_t rfscout_hotspots_app;
extern const rfscout_app_descriptor_t rfscout_cc1101_app;
extern const rfscout_app_descriptor_t rfscout_nrf24_app;
extern const rfscout_app_descriptor_t rfscout_recorder_app;
extern const rfscout_app_descriptor_t rfscout_thermal_app;
extern const rfscout_app_descriptor_t rfscout_audio_spectrum_app;
extern const rfscout_app_descriptor_t rfscout_gps_app;
extern const rfscout_app_descriptor_t rfscout_wardrive_app;

const rfscout_app_descriptor_t *const rfscout_app_registry[ RFSCOUT_APP_COUNT ] = {
    [RFSCOUT_APP_DIAGNOSTICS] = &rfscout_diagnostics_app,
    [RFSCOUT_APP_SYSTEM_EVENTS] = &rfscout_system_events_app,
    [RFSCOUT_APP_DEVICE] = &rfscout_home_app,
    [RFSCOUT_APP_BATTERY] = &rfscout_battery_app,
    [RFSCOUT_APP_WIFI_BT] = &rfscout_wifi_ble_app,
    [RFSCOUT_APP_HOTSPOTS] = &rfscout_hotspots_app,
    [RFSCOUT_APP_CC1101] = &rfscout_cc1101_app,
    [RFSCOUT_APP_NRF24] = &rfscout_nrf24_app,
    [RFSCOUT_APP_AUDIO] = &rfscout_recorder_app,
    [RFSCOUT_APP_THERMAL] = &rfscout_thermal_app,
    [RFSCOUT_APP_AUDIO_SPECTRUM] = &rfscout_audio_spectrum_app,
    [RFSCOUT_APP_GPS] = &rfscout_gps_app,
    [RFSCOUT_APP_WARDRIVE] = &rfscout_wardrive_app,
};
