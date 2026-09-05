/**
    apps/home/app.c : Home application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

const rfscout_app_descriptor_t rfscout_home_app = {
    .id = RFSCOUT_APP_DEVICE,
    .name = "Home",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
};
