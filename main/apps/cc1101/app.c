/**
    apps/cc1101/app.c : CC1101 application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "cc1101.h"

const rfscout_app_descriptor_t rfscout_cc1101_app = {
    .id = RFSCOUT_APP_CC1101,
    .name = "CC1101",
    .probe_policy = RFSCOUT_APP_PROBE_STARTUP,
    .probe = cc1101_init,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_CC1101,
};
