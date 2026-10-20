/**
    apps/ota/ota_networks.c : Home-owned remembered infrastructure networks
    Description: Persists bounded credentials for the Home Network menu.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_networks.h"

#include "persistent_store.h"
#include "app_descriptor.h"

#include <stdint.h>
#include <string.h>

#define OTA_NETWORK_RECORD_KEY "networks"
#define OTA_NETWORK_RECORD_MAGIC 0x4f54414eU
#define OTA_NETWORK_RECORD_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    ota_network_t entries[ OTA_NETWORK_MAX_COUNT ];
} ota_network_record_t;

static ota_network_record_t record_buffer;

static bool network_valid( const ota_network_t *a_network )
{
    if( NULL == a_network )
    {
        return false;
    }
    const size_t ssid_length = strnlen( a_network->ssid, sizeof( a_network->ssid ) );
    const size_t password_length = strnlen( a_network->password, sizeof( a_network->password ) );
    return ssid_length > 0U && ssid_length <= OTA_NETWORK_SSID_MAX_LENGTH &&
           password_length <= OTA_NETWORK_PASSWORD_MAX_LENGTH;
}

esp_err_t ota_networks_load( ota_network_list_t *a_networks )
{
    if( NULL == a_networks )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_networks, 0, sizeof( *a_networks ) );

    memset( &record_buffer, 0, sizeof( record_buffer ) );
    size_t length = 0U;
    const esp_err_t result = persistent_store_app_read( RFSCOUT_APP_DEVICE,
                                                        OTA_NETWORK_RECORD_KEY,
                                                        &record_buffer,
                                                        sizeof( record_buffer ),
                                                        &length );
    if( ESP_ERR_NOT_FOUND == result )
    {
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        memset( &record_buffer, 0, sizeof( record_buffer ) );
        return result;
    }
    if( sizeof( record_buffer ) != length || OTA_NETWORK_RECORD_MAGIC != record_buffer.magic ||
        OTA_NETWORK_RECORD_VERSION != record_buffer.version ||
        record_buffer.count > OTA_NETWORK_MAX_COUNT )
    {
        memset( &record_buffer, 0, sizeof( record_buffer ) );
        return ESP_ERR_INVALID_RESPONSE;
    }

    for( size_t index = 0U; index < record_buffer.count; ++index )
    {
        if( !network_valid( &record_buffer.entries[ index ] ) )
        {
            memset( &record_buffer, 0, sizeof( record_buffer ) );
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    a_networks->count = record_buffer.count;
    memcpy( a_networks->entries,
            record_buffer.entries,
            record_buffer.count * sizeof( a_networks->entries[ 0 ] ) );
    memset( &record_buffer, 0, sizeof( record_buffer ) );
    return ESP_OK;
}

esp_err_t ota_networks_save( const ota_network_list_t *a_networks )
{
    if( NULL == a_networks || a_networks->count > OTA_NETWORK_MAX_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    for( size_t index = 0U; index < a_networks->count; ++index )
    {
        if( !network_valid( &a_networks->entries[ index ] ) )
        {
            return ESP_ERR_INVALID_ARG;
        }
    }

    memset( &record_buffer, 0, sizeof( record_buffer ) );
    record_buffer.magic = OTA_NETWORK_RECORD_MAGIC;
    record_buffer.version = OTA_NETWORK_RECORD_VERSION;
    record_buffer.count = (uint16_t)a_networks->count;
    memcpy( record_buffer.entries,
            a_networks->entries,
            a_networks->count * sizeof( record_buffer.entries[ 0 ] ) );
    const esp_err_t result = persistent_store_app_write(
        RFSCOUT_APP_DEVICE, OTA_NETWORK_RECORD_KEY, &record_buffer, sizeof( record_buffer ) );
    memset( &record_buffer, 0, sizeof( record_buffer ) );
    return result;
}

int ota_networks_find( const ota_network_list_t *a_networks, const char *a_ssid )
{
    if( NULL == a_networks || NULL == a_ssid )
    {
        return -1;
    }
    for( size_t index = 0U; index < a_networks->count; ++index )
    {
        if( 0 == strcmp( a_networks->entries[ index ].ssid, a_ssid ) )
        {
            return (int)index;
        }
    }
    return -1;
}

esp_err_t
ota_networks_upsert( ota_network_list_t *a_networks, const char *a_ssid, const char *a_password )
{
    if( NULL == a_networks || NULL == a_ssid )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_length = strnlen( a_ssid, OTA_NETWORK_SSID_MAX_LENGTH + 1U );
    const size_t password_length =
        NULL == a_password ? 0U : strnlen( a_password, OTA_NETWORK_PASSWORD_MAX_LENGTH + 1U );
    if( 0U == ssid_length || ssid_length > OTA_NETWORK_SSID_MAX_LENGTH ||
        password_length > OTA_NETWORK_PASSWORD_MAX_LENGTH )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    int index = ota_networks_find( a_networks, a_ssid );
    if( index < 0 )
    {
        if( a_networks->count >= OTA_NETWORK_MAX_COUNT )
        {
            return ESP_ERR_NO_MEM;
        }
        index = (int)a_networks->count++;
    }

    ota_network_t *network = &a_networks->entries[ index ];
    memset( network, 0, sizeof( *network ) );
    memcpy( network->ssid, a_ssid, ssid_length );
    if( password_length > 0U )
    {
        memcpy( network->password, a_password, password_length );
    }
    return ESP_OK;
}

esp_err_t ota_networks_forget( ota_network_list_t *a_networks, size_t a_index )
{
    if( NULL == a_networks || a_index >= a_networks->count )
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset( &a_networks->entries[ a_index ], 0, sizeof( a_networks->entries[ a_index ] ) );
    for( size_t index = a_index; index + 1U < a_networks->count; ++index )
    {
        a_networks->entries[ index ] = a_networks->entries[ index + 1U ];
    }
    --a_networks->count;
    memset( &a_networks->entries[ a_networks->count ],
            0,
            sizeof( a_networks->entries[ a_networks->count ] ) );
    return ESP_OK;
}

void ota_networks_clear_runtime( ota_network_list_t *a_networks )
{
    if( NULL != a_networks )
    {
        memset( a_networks, 0, sizeof( *a_networks ) );
    }
}
