/**
    network_session.h : RFScout framework-owned infrastructure network session
    Description: Owns association, time synchronization, common policy, and teardown.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NETWORK_SESSION_H_
#define _RFSCOUT_NETWORK_SESSION_H_

#include "wifi_station.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_NETWORK_SESSION_SSID_MAX_LENGTH RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH

typedef enum
{
    RFSCOUT_NETWORK_SESSION_IDLE = 0,
    RFSCOUT_NETWORK_SESSION_ASSOCIATING,
    RFSCOUT_NETWORK_SESSION_TIME_SYNCING,
    RFSCOUT_NETWORK_SESSION_LIMITED,
    RFSCOUT_NETWORK_SESSION_READY,
    RFSCOUT_NETWORK_SESSION_FAILED
} rfscout_network_session_state_t;

typedef struct
{
    rfscout_network_session_state_t state;
    esp_err_t error;
    int32_t disconnect_reason;
    bool associated; /**< station association succeeded, independent of DHCP/NTP */
    uint32_t generation;
    char ssid[ RFSCOUT_NETWORK_SESSION_SSID_MAX_LENGTH + 1U ];
    rfscout_wifi_station_ip_info_t ip;
} rfscout_network_session_status_t;

/** @brief Initialize inactive infrastructure-network state for the owning app session. */
esp_err_t network_session_init( void );

esp_err_t network_session_begin_saved( const char *a_ssid, const char *a_password );
esp_err_t network_session_begin_bss( const rfscout_wifi_station_bss_t *a_bss );
/** @brief Begin secured association pinned to a discovered BSSID; caller owns password. */
esp_err_t network_session_begin_secured_bss( const rfscout_wifi_station_bss_t *a_bss,
                                             const char *a_password );
void network_session_poll( int64_t a_now_us );
void network_session_status( rfscout_network_session_status_t *a_status );
bool network_session_has_lan( void );
bool network_session_ready( void );
esp_err_t network_session_release( void );
bool network_session_is_active( void );

#endif // _RFSCOUT_NETWORK_SESSION_H_
