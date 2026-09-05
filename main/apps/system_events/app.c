/**
    apps/system_events/app.c : System Events application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

const rfscout_app_descriptor_t rfscout_system_events_app = {
    .id = RFSCOUT_APP_SYSTEM_EVENTS,
    .name = "System Events",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
};
