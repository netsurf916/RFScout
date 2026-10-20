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

esp_err_t
saved_networks_create_profile( const char *a_ssid, const char *a_password, uint32_t *a_profile_id )
{
    if( NULL == a_ssid || NULL == a_password || NULL == a_profile_id )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        result = ota_networks_create_profile( &networks, a_ssid, a_password, a_profile_id );
    }
    if( ESP_OK == result )
    {
        result = ota_networks_save( &networks );
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_associate_profile_bssid( uint32_t a_profile_id,
                                                  const uint8_t a_bssid[ 6 ],
                                                  int32_t a_auth_mode )
{
    if( NULL == a_bssid )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        result = ota_networks_associate_bssid( &networks, a_profile_id, a_bssid, a_auth_mode );
    }
    if( ESP_OK == result )
    {
        result = ota_networks_save( &networks );
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_candidates( const char *a_ssid,
                                      const uint8_t a_bssid[ 6 ],
                                      int32_t a_auth_mode,
                                      saved_network_candidate_t *a_results,
                                      size_t a_capacity,
                                      size_t *a_count )
{
    if( NULL == a_ssid || NULL == a_count || ( a_capacity != 0U && NULL == a_results ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_count = 0U;
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        for( size_t i = 0U; i < networks.count; ++i )
        {
            const ota_network_t *profile = &networks.entries[ i ];
            if( strcmp( profile->ssid, a_ssid ) != 0 )
            {
                continue;
            }
            if( profile->auth_mode != OTA_NETWORK_AUTH_UNKNOWN &&
                profile->auth_mode != a_auth_mode )
            {
                continue;
            }
            bool known = false;
            if( NULL != a_bssid )
            {
                for( size_t b = 0U; b < profile->bssid_count; ++b )
                {
                    const ota_network_bssid_t *item = &profile->bssids[ b ];
                    if( item->associated && item->auth_mode == a_auth_mode &&
                        0 == memcmp( item->address, a_bssid, 6U ) )
                    {
                        known = true;
                        break;
                    }
                }
            }
            if( *a_count < a_capacity )
            {
                a_results[ *a_count ] = ( saved_network_candidate_t ){
                    .profile_id = profile->profile_id,
                    .auth_mode = profile->auth_mode,
                    .known_bssid = known,
                };
            }
            ++*a_count;
        }
        if( *a_count > a_capacity )
        {
            result = ESP_ERR_INVALID_SIZE;
        }
    }
    ota_networks_clear_runtime( &networks );
    return result;
}

esp_err_t saved_networks_password_for_profile( uint32_t a_profile_id,
                                                char *a_password,
                                                size_t a_capacity )
{
    if( 0U == a_profile_id || NULL == a_password || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_password, 0, a_capacity );
    esp_err_t result = ota_networks_load( &networks );
    if( ESP_OK == result )
    {
        result = ESP_ERR_NOT_FOUND;
        for( size_t i = 0U; i < networks.count; ++i )
        {
            if( networks.entries[ i ].profile_id != a_profile_id )
            {
                continue;
            }
            const size_t length = strlen( networks.entries[ i ].password );
            if( length >= a_capacity )
            {
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                memcpy( a_password, networks.entries[ i ].password, length + 1U );
                result = ESP_OK;
            }
            break;
        }
    }
    ota_networks_clear_runtime( &networks );
    return result;
}
