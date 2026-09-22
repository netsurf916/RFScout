/**
    apps/ota/ota_networks.h : Home-owned remembered infrastructure networks
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_NETWORKS_H_
#define _RFSCOUT_OTA_NETWORKS_H_

#include "esp_err.h"

#include <stddef.h>

#define OTA_NETWORK_MAX_COUNT 8U
#define OTA_NETWORK_SSID_MAX_LENGTH 32U
#define OTA_NETWORK_PASSWORD_MAX_LENGTH 64U

typedef struct
{
    char ssid[ OTA_NETWORK_SSID_MAX_LENGTH + 1U ];
    char password[ OTA_NETWORK_PASSWORD_MAX_LENGTH + 1U ];
} ota_network_t;

typedef struct
{
    size_t count;
    ota_network_t entries[ OTA_NETWORK_MAX_COUNT ];
} ota_network_list_t;

esp_err_t ota_networks_load( ota_network_list_t *a_networks );
esp_err_t ota_networks_save( const ota_network_list_t *a_networks );
int ota_networks_find( const ota_network_list_t *a_networks, const char *a_ssid );
esp_err_t
ota_networks_upsert( ota_network_list_t *a_networks, const char *a_ssid, const char *a_password );
esp_err_t ota_networks_forget( ota_network_list_t *a_networks, size_t a_index );
void ota_networks_clear_runtime( ota_network_list_t *a_networks );

#endif // _RFSCOUT_OTA_NETWORKS_H_
