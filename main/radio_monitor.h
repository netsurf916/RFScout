#pragma once

#include "esp_err.h"
#include "observation_store.h"

typedef void (*radio_observation_fn)(rf_device_t *observation);

typedef enum {
    RF_WIFI_REGION_US = 0,        /* channels 1-11 */
    RF_WIFI_REGION_ETSI,          /* channels 1-13 */
    RF_WIFI_REGION_JAPAN,         /* channels 1-14 */
    RF_WIFI_REGION_EXTENDED_RX,   /* passive sweep attempt across 1-14 */
} rf_wifi_region_t;

esp_err_t radio_monitor_init(radio_observation_fn callback);
void radio_monitor_step(void);
esp_err_t radio_monitor_set_region(rf_wifi_region_t region);
rf_wifi_region_t radio_monitor_get_region(void);
const char *radio_monitor_region_name(rf_wifi_region_t region);
