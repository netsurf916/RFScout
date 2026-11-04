/**
    saved_networks.h : Shared saved infrastructure-network profiles
    Description: The one credential store for Home/OTA and Hotspots. Profiles have stable IDs;
                 several profiles may share an SSID. Each profile learns the BSSIDs it
                 associated with, and the BSSIDs that rejected its passphrase.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_SAVED_NETWORKS_H_
#define _RFSCOUT_SAVED_NETWORKS_H_

#include "esp_err.h"
#include "esp_wifi_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SAVED_NETWORK_MAX_COUNT 8U
#define SAVED_NETWORK_SSID_MAX_LENGTH 32U
#define SAVED_NETWORK_PASSWORD_MAX_LENGTH 64U
#define SAVED_NETWORK_BSSID_MAX_COUNT 128U
#define SAVED_NETWORK_AUTH_UNKNOWN ( -1 )

/** One BSSID that authenticated with a profile's passphrase. */
typedef struct
{
    uint8_t address[ 6 ];
    int16_t auth_mode;
} saved_network_bssid_t;

/** Non-secret view of one profile; passwords are only returned by saved_networks_password(). */
typedef struct
{
    uint32_t profile_id;
    /** Minimum security, learned on first association; SAVED_NETWORK_AUTH_UNKNOWN before. */
    int32_t auth_mode;
    bool has_password;
    uint8_t ordinal; /**< 1-based position among profiles sharing this SSID */
    uint8_t bssid_count;
    saved_network_bssid_t bssids[ SAVED_NETWORK_BSSID_MAX_COUNT ];
    char ssid[ SAVED_NETWORK_SSID_MAX_LENGTH + 1U ];
} saved_network_profile_t;

/*
 * Every operation loads the record, applies one change, saves it and erases its working copy
 * before returning, so changes made from one app are immediately visible to the other.
 * Call from the application task only.
 */

/** @brief Copy every profile in creation order, without passwords. */
esp_err_t
saved_networks_list( saved_network_profile_t *a_profiles, size_t a_capacity, size_t *a_count );

/** @brief Copy one profile, without its password. */
esp_err_t saved_networks_get( uint32_t a_profile_id, saved_network_profile_t *a_profile );

/** @brief Copy a profile's password. Erase it as soon as association ends; never log it. */
esp_err_t saved_networks_password( uint32_t a_profile_id, char *a_password, size_t a_capacity );

/** @brief Create a profile; ESP_ERR_NO_MEM when full. The ID is valid only on ESP_OK. */
esp_err_t
saved_networks_create( const char *a_ssid, const char *a_password, uint32_t *a_profile_id );

/**
 * @brief Replace a profile's password.
 * @details An unchanged password keeps learned BSSIDs; a changed one clears them and the
 *          learned auth mode, because that evidence belonged to the old secret.
 */
esp_err_t saved_networks_set_password( uint32_t a_profile_id, const char *a_password );

/** @brief Delete a profile and everything learned for it. */
esp_err_t saved_networks_forget( uint32_t a_profile_id );

/**
 * @brief Learn that one BSSID authenticated with a profile's passphrase.
 * @details Fails with ESP_ERR_INVALID_STATE when another profile already associated with that
 *          BSSID or its security is below the profile's minimum. The first association sets that
 *          minimum. When the BSSID list is full, the oldest learned BSSID is forgotten; it is
 *          learned again if it is joined later.
 */
esp_err_t saved_networks_record_bssid( uint32_t a_profile_id,
                                       const uint8_t a_bssid[ 6 ],
                                       int32_t a_auth_mode );

/* Pure profile-snapshot helpers; inline so host models and tests need no storage code. */

/** @brief Find a profile's entry for a BSSID, or NULL. */
static inline const saved_network_bssid_t *
saved_network_profile_bssid( const saved_network_profile_t *a_profile, const uint8_t a_bssid[ 6 ] )
{
    for( size_t index = 0U; NULL != a_profile && NULL != a_bssid && index < a_profile->bssid_count;
         ++index )
    {
        if( 0 == memcmp( a_profile->bssids[ index ].address, a_bssid, 6U ) )
        {
            return &a_profile->bssids[ index ];
        }
    }
    return NULL;
}

/** Security ranks for profile growth: WEP < WPA < WPA2 < WPA3. */
typedef enum
{
    SAVED_NETWORK_SECURITY_UNRANKED = 0,
    SAVED_NETWORK_SECURITY_WEP,
    SAVED_NETWORK_SECURITY_WPA,
    SAVED_NETWORK_SECURITY_WPA2,
    SAVED_NETWORK_SECURITY_WPA3,
} saved_network_security_t;

/**
 * @brief Rank of an auth mode.
 * @details A mixed mode ranks as the weakest security it accepts, so WPA2/WPA3 transition is WPA2.
 *          Modes a passphrase profile cannot use (open, OWE, enterprise) are unranked.
 */
static inline saved_network_security_t saved_network_security( int32_t a_auth_mode )
{
    switch( (wifi_auth_mode_t)a_auth_mode )
    {
        case WIFI_AUTH_WEP:
            return SAVED_NETWORK_SECURITY_WEP;
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
            return SAVED_NETWORK_SECURITY_WPA;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return SAVED_NETWORK_SECURITY_WPA2;
        case WIFI_AUTH_WPA3_PSK:
            return SAVED_NETWORK_SECURITY_WPA3;
        default:
            return SAVED_NETWORK_SECURITY_UNRANKED;
    }
}

/** @brief Whether a mode meets a profile's minimum: equal or greater security, never lower. */
static inline bool saved_network_auth_allowed( int32_t a_minimum, int32_t a_auth_mode )
{
    if( SAVED_NETWORK_AUTH_UNKNOWN == a_minimum || a_minimum == a_auth_mode )
    {
        return true;
    }
    const saved_network_security_t minimum = saved_network_security( a_minimum );
    return SAVED_NETWORK_SECURITY_UNRANKED != minimum &&
           saved_network_security( a_auth_mode ) >= minimum;
}

/** @brief Whether a profile may be used with an observed auth mode. */
static inline bool saved_network_profile_auth_compatible( const saved_network_profile_t *a_profile,
                                                          int32_t a_auth_mode )
{
    return NULL != a_profile && saved_network_auth_allowed( a_profile->auth_mode, a_auth_mode );
}

#endif // _RFSCOUT_SAVED_NETWORKS_H_
