#pragma once
#include "esp_err.h"
#include "observation_store.h"
typedef void (*radio_observation_fn)(rf_device_t *observation);
esp_err_t radio_monitor_init(radio_observation_fn callback);
void radio_monitor_step(void);
