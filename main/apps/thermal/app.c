/**
    apps/thermal/app.c : Thermal application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "mlx90640.h"

const rfscout_app_descriptor_t rfscout_thermal_app = {
    .id = RFSCOUT_APP_THERMAL,
    .name = "Thermal",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = mlx90640_probe,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_NONE,
};
