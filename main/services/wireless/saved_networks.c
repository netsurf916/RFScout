/**
    saved_networks.c : Shared saved infrastructure-network profiles
    Description: Owns the persistent "networks" record used by Home/OTA and Hotspots.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "saved_networks.h"

#include "app_descriptor.h"
#include "persistent_store.h"

#include <string.h>

#define SAVED_NETWORK_RECORD_KEY "networks"
#define SAVED_NETWORK_RECORD_MAGIC 0x4f54414eU
#define SAVED_NETWORK_RECORD_VERSION 2U

typedef struct
{
    uint32_t profile_id;
    int32_t auth_mode;
    uint8_t bssid_count;
    saved_network_bssid_t bssids[ SAVED_NETWORK_BSSID_MAX_COUNT ];
    char ssid[ SAVED_NETWORK_SSID_MAX_LENGTH + 1U ];
    char password[ SAVED_NETWORK_PASSWORD_MAX_LENGTH + 1U ];
} saved_network_entry_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    saved_network_entry_t entries[ SAVED_NETWORK_MAX_COUNT ];
} saved_network_record_t;

/* The record stays in the Home/device namespace where Home created it, so this platform
   service uses the privileged core path rather than the calling app's namespace. The working
   copy is static to keep credential material off the limited app task stack. */
static saved_network_record_t record;

static bool entry_valid( const saved_network_entry_t *a_entry )
{
    const size_t ssid_length = strnlen( a_entry->ssid, sizeof( a_entry->ssid ) );
    const size_t password_length = strnlen( a_entry->password, sizeof( a_entry->password ) );
    return 0U != a_entry->profile_id && ssid_length > 0U &&
           ssid_length <= SAVED_NETWORK_SSID_MAX_LENGTH &&
           password_length <= SAVED_NETWORK_PASSWORD_MAX_LENGTH &&
           a_entry->bssid_count <= SAVED_NETWORK_BSSID_MAX_COUNT;
}

static void release( void )
{
    memset( &record, 0, sizeof( record ) );
}

static bool record_valid( const void *a_record )
{
    const saved_network_record_t *candidate = a_record;
    bool valid = SAVED_NETWORK_RECORD_MAGIC == candidate->magic &&
                 SAVED_NETWORK_RECORD_VERSION == candidate->version &&
                 candidate->count <= SAVED_NETWORK_MAX_COUNT;
    for( size_t index = 0U; valid && index < candidate->count; ++index )
    {
        valid = entry_valid( &candidate->entries[ index ] );
    }
    return valid;
}

/** Load the record; a missing or rejected (and therefore deleted) record is an empty list. */
static esp_err_t load( void )
{
    release();
    const esp_err_t result = persistent_store_app_load(
        RFSCOUT_APP_DEVICE, SAVED_NETWORK_RECORD_KEY, &record, sizeof( record ), record_valid );
    if( ESP_OK != result )
    {
        release();
    }
    return ESP_ERR_NOT_FOUND == result || ESP_ERR_INVALID_RESPONSE == result ? ESP_OK : result;
}

static esp_err_t save( void )
{
    record.magic = SAVED_NETWORK_RECORD_MAGIC;
    record.version = SAVED_NETWORK_RECORD_VERSION;
    const esp_err_t result = persistent_store_app_write(
        RFSCOUT_APP_DEVICE, SAVED_NETWORK_RECORD_KEY, &record, sizeof( record ) );
    release();
    return result;
}

static saved_network_entry_t *find( uint32_t a_profile_id )
{
    for( size_t index = 0U; 0U != a_profile_id && index < record.count; ++index )
    {
        if( record.entries[ index ].profile_id == a_profile_id )
        {
            return &record.entries[ index ];
        }
    }
    return NULL;
}

static void describe( const saved_network_entry_t *a_entry, saved_network_profile_t *a_profile )
{
    memset( a_profile, 0, sizeof( *a_profile ) );
    a_profile->profile_id = a_entry->profile_id;
    a_profile->auth_mode = a_entry->auth_mode;
    a_profile->has_password = '\0' != a_entry->password[ 0 ];
    a_profile->bssid_count = a_entry->bssid_count;
    memcpy( a_profile->bssids, a_entry->bssids, sizeof( a_profile->bssids ) );
    memcpy( a_profile->ssid, a_entry->ssid, sizeof( a_profile->ssid ) );
    for( const saved_network_entry_t *other = record.entries; other <= a_entry; ++other )
    {
        if( 0 == strcmp( other->ssid, a_entry->ssid ) )
        {
            ++a_profile->ordinal;
        }
    }
}

static esp_err_t copy_text( char *a_output, size_t a_capacity, const char *a_text, size_t a_max )
{
    const size_t length = strnlen( a_text, a_max + 1U );
    if( length > a_max )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if( length >= a_capacity )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy( a_output, a_text, length );
    a_output[ length ] = '\0';
    return ESP_OK;
}

esp_err_t
saved_networks_list( saved_network_profile_t *a_profiles, size_t a_capacity, size_t *a_count )
{
    if( NULL == a_count || ( 0U != a_capacity && NULL == a_profiles ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_count = 0U;
    const esp_err_t result = load();
    for( size_t index = 0U; ESP_OK == result && index < record.count && index < a_capacity;
         ++index )
    {
        describe( &record.entries[ index ], &a_profiles[ index ] );
        ++*a_count;
    }
    release();
    return result;
}

esp_err_t saved_networks_get( uint32_t a_profile_id, saved_network_profile_t *a_profile )
{
    if( NULL == a_profile )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = load();
    if( ESP_OK == result )
    {
        const saved_network_entry_t *entry = find( a_profile_id );
        if( NULL == entry )
        {
            result = ESP_ERR_NOT_FOUND;
        }
        else
        {
            describe( entry, a_profile );
        }
    }
    release();
    return result;
}

esp_err_t saved_networks_password( uint32_t a_profile_id, char *a_password, size_t a_capacity )
{
    if( NULL == a_password || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_password, 0, a_capacity );
    esp_err_t result = load();
    if( ESP_OK == result )
    {
        const saved_network_entry_t *entry = find( a_profile_id );
        result =
            NULL == entry
                ? ESP_ERR_NOT_FOUND
                : copy_text(
                      a_password, a_capacity, entry->password, SAVED_NETWORK_PASSWORD_MAX_LENGTH );
    }
    release();
    return result;
}

esp_err_t
saved_networks_create( const char *a_ssid, const char *a_password, uint32_t *a_profile_id )
{
    if( NULL == a_ssid || NULL == a_password || NULL == a_profile_id )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_profile_id = 0U;
    esp_err_t result = load();
    if( ESP_OK == result && record.count >= SAVED_NETWORK_MAX_COUNT )
    {
        result = ESP_ERR_NO_MEM;
    }
    uint32_t profile_id = 1U;
    for( size_t index = 0U; ESP_OK == result && index < record.count; ++index )
    {
        if( record.entries[ index ].profile_id >= profile_id )
        {
            profile_id = record.entries[ index ].profile_id + 1U;
        }
    }
    saved_network_entry_t *entry = ESP_OK == result ? &record.entries[ record.count ] : NULL;
    if( ESP_OK == result )
    {
        memset( entry, 0, sizeof( *entry ) );
        result =
            copy_text( entry->ssid, sizeof( entry->ssid ), a_ssid, SAVED_NETWORK_SSID_MAX_LENGTH );
    }
    if( ESP_OK == result && '\0' == entry->ssid[ 0 ] )
    {
        result = ESP_ERR_INVALID_SIZE;
    }
    if( ESP_OK == result )
    {
        result = copy_text( entry->password,
                            sizeof( entry->password ),
                            a_password,
                            SAVED_NETWORK_PASSWORD_MAX_LENGTH );
    }
    if( ESP_OK != result )
    {
        release();
        return result;
    }
    entry->profile_id = profile_id;
    entry->auth_mode = SAVED_NETWORK_AUTH_UNKNOWN;
    ++record.count;
    result = save();
    if( ESP_OK == result )
    {
        *a_profile_id = profile_id;
    }
    return result;
}

esp_err_t saved_networks_set_password( uint32_t a_profile_id, const char *a_password )
{
    if( NULL == a_password )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = load();
    saved_network_entry_t *entry = ESP_OK == result ? find( a_profile_id ) : NULL;
    if( ESP_OK == result && NULL == entry )
    {
        result = ESP_ERR_NOT_FOUND;
    }
    if( ESP_OK == result && 0 == strcmp( entry->password, a_password ) )
    {
        release();
        return ESP_OK;
    }
    char password[ SAVED_NETWORK_PASSWORD_MAX_LENGTH + 1U ];
    if( ESP_OK == result )
    {
        result = copy_text(
            password, sizeof( password ), a_password, SAVED_NETWORK_PASSWORD_MAX_LENGTH );
    }
    if( ESP_OK != result )
    {
        release();
        return result;
    }
    memcpy( entry->password, password, sizeof( entry->password ) );
    memset( password, 0, sizeof( password ) );
    entry->auth_mode = SAVED_NETWORK_AUTH_UNKNOWN;
    entry->bssid_count = 0U;
    memset( entry->bssids, 0, sizeof( entry->bssids ) );
    return save();
}

esp_err_t saved_networks_forget( uint32_t a_profile_id )
{
    esp_err_t result = load();
    saved_network_entry_t *entry = ESP_OK == result ? find( a_profile_id ) : NULL;
    if( ESP_OK != result || NULL == entry )
    {
        release();
        return ESP_OK != result ? result : ESP_ERR_NOT_FOUND;
    }
    const size_t index = (size_t)( entry - record.entries );
    memmove( entry, entry + 1, ( record.count - index - 1U ) * sizeof( *entry ) );
    --record.count;
    memset( &record.entries[ record.count ], 0, sizeof( record.entries[ 0 ] ) );
    return save();
}

static saved_network_bssid_t *entry_bssid( saved_network_entry_t *a_entry,
                                           const uint8_t a_bssid[ 6 ] )
{
    for( size_t index = 0U; index < a_entry->bssid_count; ++index )
    {
        if( 0 == memcmp( a_entry->bssids[ index ].address, a_bssid, 6U ) )
        {
            return &a_entry->bssids[ index ];
        }
    }
    return NULL;
}

/** Make room by evicting the oldest rejected BSSID; associated BSSIDs are never evicted. */
static saved_network_bssid_t *append_bssid( saved_network_entry_t *a_entry )
{
    if( a_entry->bssid_count == SAVED_NETWORK_BSSID_MAX_COUNT )
    {
        size_t index = 0U;
        while( index < a_entry->bssid_count &&
               SAVED_NETWORK_BSSID_REJECTED != a_entry->bssids[ index ].state )
        {
            ++index;
        }
        if( index == a_entry->bssid_count )
        {
            return NULL;
        }
        memmove( &a_entry->bssids[ index ],
                 &a_entry->bssids[ index + 1U ],
                 ( a_entry->bssid_count - index - 1U ) * sizeof( a_entry->bssids[ 0 ] ) );
        --a_entry->bssid_count;
    }
    return &a_entry->bssids[ a_entry->bssid_count++ ];
}

esp_err_t saved_networks_record_bssid( uint32_t a_profile_id,
                                       const uint8_t a_bssid[ 6 ],
                                       int32_t a_auth_mode,
                                       saved_network_bssid_state_t a_state )
{
    if( NULL == a_bssid || SAVED_NETWORK_AUTH_UNKNOWN == a_auth_mode ||
        ( SAVED_NETWORK_BSSID_ASSOCIATED != a_state && SAVED_NETWORK_BSSID_REJECTED != a_state ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = load();
    saved_network_entry_t *entry = ESP_OK == result ? find( a_profile_id ) : NULL;
    if( ESP_OK == result && NULL == entry )
    {
        result = ESP_ERR_NOT_FOUND;
    }
    if( ESP_OK == result && SAVED_NETWORK_BSSID_ASSOCIATED == a_state )
    {
        /* One BSSID is never trusted by two credentials, and a profile keeps one auth mode. */
        for( size_t index = 0U; index < record.count; ++index )
        {
            saved_network_entry_t *other = &record.entries[ index ];
            const saved_network_bssid_t *known = entry_bssid( other, a_bssid );
            if( other != entry && NULL != known && SAVED_NETWORK_BSSID_ASSOCIATED == known->state )
            {
                result = ESP_ERR_INVALID_STATE;
            }
        }
        if( SAVED_NETWORK_AUTH_UNKNOWN != entry->auth_mode && entry->auth_mode != a_auth_mode )
        {
            result = ESP_ERR_INVALID_STATE;
        }
    }
    saved_network_bssid_t *bssid = NULL;
    if( ESP_OK == result )
    {
        bssid = entry_bssid( entry, a_bssid );
        if( NULL != bssid && bssid->state == a_state && bssid->auth_mode == a_auth_mode )
        {
            release();
            return ESP_OK;
        }
        if( NULL == bssid )
        {
            bssid = append_bssid( entry );
        }
        if( NULL == bssid )
        {
            result = ESP_ERR_NO_MEM;
        }
    }
    if( ESP_OK != result )
    {
        release();
        return result;
    }
    memcpy( bssid->address, a_bssid, sizeof( bssid->address ) );
    bssid->state = (uint8_t)a_state;
    bssid->auth_mode = a_auth_mode;
    if( SAVED_NETWORK_BSSID_ASSOCIATED == a_state )
    {
        entry->auth_mode = a_auth_mode;
    }
    return save();
}
