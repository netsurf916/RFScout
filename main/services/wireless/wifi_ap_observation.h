/**
    wifi_ap_observation.h : normalized passive access-point metadata
    Description: Allocation-free beacon/probe-response decoding for shared Wi-Fi monitoring.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_WIFI_AP_OBSERVATION_H_
#define _RFSCOUT_WIFI_AP_OBSERVATION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_WIFI_AP_SSID_MAX_LENGTH 32U

/** Classification is conservative: unknown security is never treated as Open. */
typedef enum
{
    RFSCOUT_WIFI_AP_SECURITY_UNKNOWN = 0,
    RFSCOUT_WIFI_AP_SECURITY_OPEN,
    RFSCOUT_WIFI_AP_SECURITY_OWE,
    RFSCOUT_WIFI_AP_SECURITY_SECURED
} rfscout_wifi_ap_security_t;

/**
 * @brief Observed authentication evidence, not a direct ESP-IDF wifi_auth_mode_t value.
 * @details Station association must independently translate/confirm supported modes.
 */
typedef enum
{
    RFSCOUT_WIFI_AP_AUTH_UNKNOWN = 0,
    RFSCOUT_WIFI_AP_AUTH_OPEN,
    RFSCOUT_WIFI_AP_AUTH_WEP,
    RFSCOUT_WIFI_AP_AUTH_WPA,
    RFSCOUT_WIFI_AP_AUTH_WPA2_PSK,
    RFSCOUT_WIFI_AP_AUTH_WPA3_SAE,
    RFSCOUT_WIFI_AP_AUTH_WPA2_WPA3_PERSONAL,
    RFSCOUT_WIFI_AP_AUTH_OWE,
    RFSCOUT_WIFI_AP_AUTH_ENTERPRISE
} rfscout_wifi_ap_auth_t;

typedef struct
{
    uint8_t bssid[ 6 ];
    char ssid[ RFSCOUT_WIFI_AP_SSID_MAX_LENGTH + 1U ];
    uint8_t ssid_length; /**< Zero for hidden or absent SSID. */
    uint8_t channel;     /**< Receiver-reported channel, not an unverified IE claim. */
    int8_t rssi;
    rfscout_wifi_ap_security_t security;
    rfscout_wifi_ap_auth_t auth;
} rfscout_wifi_ap_observation_t;

/**
 * @brief Decode a single complete 802.11 Beacon or Probe Response.
 * @details No heap allocation or retained pointers. Rejects malformed IEs, invalid RSN
 *          suites, group BSSIDs, and truncated frames. The caller owns and copies the result.
 *          A valid hidden-SSID frame has ssid_length=0; consumers decide whether to admit it.
 *          Only normalized evidence is returned; it must not directly authorize association.
 */
bool wifi_ap_observation_parse( const uint8_t *a_frame,
                                size_t a_length,
                                int8_t a_rssi,
                                uint8_t a_channel,
                                rfscout_wifi_ap_observation_t *a_output );

#endif // _RFSCOUT_WIFI_AP_OBSERVATION_H_
