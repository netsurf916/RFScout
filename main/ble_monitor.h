#pragma once

#include "esp_err.h"
#include "observation_store.h"

typedef void (*ble_observation_fn)(rf_device_t *observation);

esp_err_t ble_monitor_init(ble_observation_fn callback);
