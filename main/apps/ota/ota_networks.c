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
#define OTA_NETWORK_RECORD_VERSION 2U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    ota_network_t entries[ OTA_NETWORK_MAX_COUNT ];
} ota_network_record_t;

/* V1 is retained only as an on-disk migration reader. */
typedef struct
{
    char ssid[ OTA_NETWORK_SSID_MAX_LENGTH + 1U ];
    char password[ OTA_NETWORK_PASSWORD_MAX_LENGTH + 1U ];
} ota_network_v1_t;
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    ota_network_v1_t entries[ OTA_NETWORK_MAX_COUNT ];
} ota_network_record_v1_t;

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
    if( record_buffer.magic != OTA_NETWORK_RECORD_MAGIC ||
        record_buffer.count > OTA_NETWORK_MAX_COUNT )
    {
        memset( &record_buffer, 0, sizeof( record_buffer ) );
        return ESP_ERR_INVALID_RESPONSE;
    }
    if( record_buffer.version == 1U && length == sizeof( ota_network_record_v1_t ) )
    {
        const ota_network_record_v1_t *legacy = (const void *)&record_buffer;
        a_networks->count = legacy->count;
        for( size_t index = 0U; index < a_networks->count; ++index )
        {
            ota_network_t *entry = &a_networks->entries[ index ];
            memcpy( entry->ssid, legacy->entries[ index ].ssid, sizeof( entry->ssid ) );
            memcpy( entry->password, legacy->entries[ index ].password, sizeof( entry->password ) );
            entry->profile_id = (uint32_t)index + 1U;
            entry->auth_mode = OTA_NETWORK_AUTH_UNKNOWN;
            if( !network_valid( entry ) )
            {
                ota_networks_clear_runtime( a_networks );
                memset( &record_buffer, 0, sizeof( record_buffer ) );
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    else if( record_buffer.version == OTA_NETWORK_RECORD_VERSION &&
             length == sizeof( record_buffer ) )
    {
        a_networks->count = record_buffer.count;
        memcpy( a_networks->entries, record_buffer.entries,
                a_networks->count * sizeof( a_networks->entries[ 0 ] ) );
        for( size_t index = 0U; index < a_networks->count; ++index )
        {
            if( !network_valid( &a_networks->entries[ index ] ) ||
                a_networks->entries[ index ].profile_id == 0U ||
                a_networks->entries[ index ].bssid_count > OTA_NETWORK_BSSID_MAX_COUNT )
            {
                ota_networks_clear_runtime( a_networks );
                memset( &record_buffer, 0, sizeof( record_buffer ) );
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    else
    {
        memset( &record_buffer, 0, sizeof( record_buffer ) );
        return ESP_ERR_INVALID_RESPONSE;
    }
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
    const uint32_t retained_id = network->profile_id;
    uint32_t next_id = 1U;
    for( size_t scan = 0U; scan < a_networks->count; ++scan )
    {
        if( a_networks->entries[ scan ].profile_id >= next_id )
        {
            next_id = a_networks->entries[ scan ].profile_id + 1U;
        }
    }
    memset( network, 0, sizeof( *network ) );
    network->profile_id = retained_id != 0U ? retained_id : next_id;
    network->auth_mode = OTA_NETWORK_AUTH_UNKNOWN;
    memcpy( network->ssid, a_ssid, ssid_length );
    if( password_length > 0U )
    {
        memcpy( network->password, a_password, password_length );
    }
    return ESP_OK;
}

int ota_networks_find_bssid( const ota_network_list_t *a_networks,
                             const char *a_ssid,
                             const uint8_t a_bssid[ 6 ],
                             int32_t a_auth_mode )
{
    if( NULL == a_networks || NULL == a_ssid || NULL == a_bssid )
    {
        return -1;
    }
    for( size_t index = 0U; index < a_networks->count; ++index )
    {
        const ota_network_t *entry = &a_networks->entries[ index ];
        if( entry->auth_mode != a_auth_mode || strcmp( entry->ssid, a_ssid ) != 0 )
        {
            continue;
        }
        for( size_t b = 0U; b < entry->bssid_count; ++b )
        {
            if( entry->bssids[ b ].associated &&
                entry->bssids[ b ].auth_mode == a_auth_mode &&
                0 == memcmp( entry->bssids[ b ].address, a_bssid, 6U ) )
            {
                return (int)index;
            }
        }
    }
    return -1;
}

esp_err_t ota_networks_associate_bssid( ota_network_list_t *a_networks,
                                       uint32_t a_profile_id,
                                       const uint8_t a_bssid[ 6 ],
                                       int32_t a_auth_mode )
{
    if( NULL == a_networks || NULL == a_bssid || a_profile_id == 0U ||
        a_auth_mode == OTA_NETWORK_AUTH_UNKNOWN )
    {
        return ESP_ERR_INVALID_ARG;
    }
    for( size_t index = 0U; index < a_networks->count; ++index )
    {
        ota_network_t *entry = &a_networks->entries[ index ];
        if( entry->profile_id != a_profile_id )
        {
            continue;
        }
        if( entry->auth_mode != OTA_NETWORK_AUTH_UNKNOWN && entry->auth_mode != a_auth_mode )
        {
            return ESP_ERR_INVALID_STATE;
        }
        for( size_t b = 0U; b < entry->bssid_count; ++b )
        {
            if( 0 == memcmp( entry->bssids[ b ].address, a_bssid, 6U ) )
            {
                if( entry->bssids[ b ].auth_mode != a_auth_mode )
                {
                    return ESP_ERR_INVALID_STATE;
                }
                entry->bssids[ b ].associated = true;
                entry->auth_mode = a_auth_mode;
                return ESP_OK;
            }
        }
        if( entry->bssid_count == OTA_NETWORK_BSSID_MAX_COUNT )
        {
            return ESP_ERR_NO_MEM;
        }
        ota_network_bssid_t *bss = &entry->bssids[ entry->bssid_count++ ];
        memcpy( bss->address, a_bssid, 6U );
        bss->auth_mode = a_auth_mode;
        bss->associated = true;
        entry->auth_mode = a_auth_mode;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
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
