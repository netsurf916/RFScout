/**
    apps/network_common/network_credentials.h : Shared saved-network credential screens
    Description: Rotary profile view, masked password entry/edit, forget, and profile picking
                 over the saved_networks service. Used by Home and Hotspots so both apps manage
                 the same profiles with the same interaction.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_NETWORK_CREDENTIALS_H_
#define _RFSCOUT_NETWORK_CREDENTIALS_H_

#include "input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    NETWORK_CREDENTIALS_RESULT_NONE = 0,  /**< closed without a change */
    NETWORK_CREDENTIALS_RESULT_SAVED,     /**< profile created or password replaced */
    NETWORK_CREDENTIALS_RESULT_FORGOTTEN, /**< profile deleted */
    NETWORK_CREDENTIALS_RESULT_PICKED,    /**< a profile was chosen for a one-off join */
} network_credentials_result_t;

typedef struct
{
    network_credentials_result_t result;
    uint32_t profile_id;
} network_credentials_outcome_t;

/** @brief Show one profile: password (masked until Show), Edit password, Forget. */
void network_credentials_open_profile( uint32_t a_profile_id );

/**
 * @brief Save a password for an SSID.
 * @details With no profile for the SSID this goes straight to password entry. Otherwise the
 *          user chooses which profile to replace, or New profile.
 */
void network_credentials_open_add( const char *a_ssid );

/** @brief Choose one profile for an SSID whose auth mode is compatible with a_auth_mode. */
void network_credentials_open_pick( const char *a_ssid, int32_t a_auth_mode );

bool network_credentials_active( void );

/** @brief Render the current credential screen under the caller's title. */
void network_credentials_render( const char *a_title );

/**
 * @brief Handle one input while active.
 * @return true when the screens closed; a_outcome then says what changed.
 */
bool network_credentials_input( const rfscout_input_event_t *a_input,
                                network_credentials_outcome_t *a_outcome );

/** @brief Close immediately and erase any password held for display or entry. */
void network_credentials_close( void );

/** @brief Format "SSID" or "SSID #n" for profiles that share an SSID. */
void network_credentials_label(
    const char *a_ssid, uint8_t a_ordinal, bool a_shared, char *a_output, size_t a_capacity );

#endif // _RFSCOUT_NETWORK_CREDENTIALS_H_
