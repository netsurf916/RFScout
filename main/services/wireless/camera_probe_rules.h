/**
    camera_probe_rules.h : Hotspots bounded local gateway probe policy
    Description: Pure target eligibility and tentative camera-evidence rules.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_CAMERA_PROBE_RULES_H_
#define _RFSCOUT_CAMERA_PROBE_RULES_H_

#include "service_fingerprint.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Only probe the DHCP gateway when it belongs to the connected local subnet. */
static inline bool
camera_probe_gateway_eligible( uint32_t a_address, uint32_t a_netmask, uint32_t a_gateway )
{
    return 0U != a_address && 0U != a_netmask && 0U != a_gateway && a_gateway != UINT32_MAX &&
           a_gateway != a_address && ( a_gateway & a_netmask ) == ( a_address & a_netmask );
}

/** @brief Camera-specific banner or validated ONVIF device type; generic HTTP/media is insufficient. */
static inline bool camera_probe_camera_hint( uint32_t a_evidence )
{
    return 0U !=
           ( a_evidence & ( RFSCOUT_SERVICE_CAMERA_HINT | RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE ) );
}

#endif // _RFSCOUT_CAMERA_PROBE_RULES_H_
