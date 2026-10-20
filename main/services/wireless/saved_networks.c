/**
    saved_networks.c : Shared saved-network operations over the existing Home/OTA store
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "saved_networks.h"

#include <string.h>

/* Avoid placing credential-bearing network lists on the limited app task stack. */
static ota_network_list_t networks;

esp_err_t
saved_networks_password_for_ssid( const char *a_ssid, char *a_password, size_t a_capacity )
{
    if( NULL == a_ssid || NULL == a_password || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_password, 0, a_capacity );
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        const int index = ota_networks_find( &networks, a_ssid );
        if( index < 0 )
        {
            result = ESP_ERR_NOT_FOUND;
        }
        else
        {
            const size_t length = strlen( networks.entries[ index ].password );
            if( length >= a_capacity )
            {
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                memcpy( a_password, networks.entries[ index ].password, length + 1U );
            }
        }
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_save( const char *a_ssid, const char *a_password )
{
    if( NULL == a_ssid || NULL == a_password )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        result = ota_networks_upsert( &networks, a_ssid, a_password );
    }
    if( ESP_OK == result )
    {
        result = ota_networks_save( &networks );
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_forget( const char *a_ssid )
{
    if( NULL == a_ssid )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        const int index = ota_networks_find( &networks, a_ssid );
        result = index < 0 ? ESP_ERR_NOT_FOUND : ota_networks_forget( &networks, (size_t)index );
    }
    if( ESP_OK == result )
    {
        result = ota_networks_save( &networks );
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_password_for_bss( const uint8_t a_bssid[ 6 ],
                                           const char *a_ssid,
                                           int32_t a_auth_mode,
                                           char *a_password,
                                           size_t a_capacity )
{
    if( NULL == a_bssid || NULL == a_ssid || NULL == a_password || a_capacity == 0U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_password, 0, a_capacity );
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        const int index = ota_networks_find_bssid( &networks, a_ssid, a_bssid, a_auth_mode );
        if( index < 0 )
        {
            result = ESP_ERR_NOT_FOUND;
        }
        else
        {
            const size_t length = strlen( networks.entries[ index ].password );
            if( length >= a_capacity )
            {
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                memcpy( a_password, networks.entries[ index ].password, length + 1U );
            }
        }
    }
    ota_networks_clear_runtime( &networks );
    return result;
}
