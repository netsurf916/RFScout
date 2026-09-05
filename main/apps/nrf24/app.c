/**
    apps/nrf24/app.c : nRF24 application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "nrf24.h"

const rfscout_app_descriptor_t rfscout_nrf24_app = {
    .id = RFSCOUT_APP_NRF24,
    .name = "nRF24",
    .probe_policy = RFSCOUT_APP_PROBE_STARTUP,
    .probe = nrf24_init,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_NRF24,
};
