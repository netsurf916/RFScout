/**
    wifi_core.c : shared ESP-IDF Wi-Fi substrate
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_core.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <stdbool.h>

static bool initialized;

esp_err_t wifi_core_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    esp_err_t result = esp_netif_init();
    if( ESP_OK != result && ESP_ERR_INVALID_STATE != result )
    {
        return result;
    }

    result = esp_event_loop_create_default();
    if( ESP_OK != result && ESP_ERR_INVALID_STATE != result )
    {
        return result;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init( &config );
    if( ESP_OK != result )
    {
        return result;
    }

    result = esp_wifi_set_mode( WIFI_MODE_NULL );
    if( ESP_OK != result )
    {
        return result;
    }

    initialized = true;
    return ESP_OK;
}
