/**
    wifi_ap_observation.c : bounded shared 802.11 access-point metadata decoder
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_ap_observation.h"

#include <string.h>

#define WIFI_AP_HEADER_BYTES 24U
#define WIFI_AP_FIXED_BYTES 12U
#define WIFI_AP_IE_OFFSET ( WIFI_AP_HEADER_BYTES + WIFI_AP_FIXED_BYTES )
#define WIFI_AP_IE_SSID 0U
#define WIFI_AP_IE_RSN 48U
#define WIFI_AP_IE_VENDOR 221U
#define WIFI_AP_RSN_OUI_0 0x00U
#define WIFI_AP_RSN_OUI_1 0x0fU
#define WIFI_AP_RSN_OUI_2 0xacU

#define WIFI_AP_AKM_ENTERPRISE ( 1U << 0 )
#define WIFI_AP_AKM_PSK ( 1U << 1 )
#define WIFI_AP_AKM_SAE ( 1U << 2 )
#define WIFI_AP_AKM_OWE ( 1U << 3 )

static uint16_t little_endian_16( const uint8_t *a_bytes )
{
    return (uint16_t)a_bytes[ 0 ] | (uint16_t)( (uint16_t)a_bytes[ 1 ] << 8U );
}

/** RSN version, cipher-list and AKM-list lengths must all be internally consistent. */
static bool parse_rsn( const uint8_t *a_ie, size_t a_length, uint32_t *a_akm )
{
    if( a_length < 2U + 4U + 2U || 1U != little_endian_16( a_ie ) )
    {
        return false;
    }
    size_t cursor = 2U + 4U;
    const uint16_t cipher_count = little_endian_16( &a_ie[ cursor ] );
    cursor += 2U;
    if( 0U == cipher_count || (size_t)cipher_count > ( a_length - cursor ) / 4U )
    {
        return false;
    }
    cursor += (size_t)cipher_count * 4U;
    if( a_length - cursor < 2U )
    {
        return false;
    }
    const uint16_t akm_count = little_endian_16( &a_ie[ cursor ] );
    cursor += 2U;
    if( 0U == akm_count || (size_t)akm_count > ( a_length - cursor ) / 4U )
    {
        return false;
    }
    uint32_t result = 0U;
    for( size_t index = 0U; index < akm_count; ++index )
    {
        const uint8_t *suite = &a_ie[ cursor + 4U * index ];
        if( suite[ 0 ] != WIFI_AP_RSN_OUI_0 || suite[ 1 ] != WIFI_AP_RSN_OUI_1 ||
            suite[ 2 ] != WIFI_AP_RSN_OUI_2 )
        {
            continue;
        }
        switch( suite[ 3 ] )
        {
            case 1U:  /* 802.1X */
            case 3U:  /* FT over 802.1X */
            case 5U:  /* SHA256 802.1X */
            case 11U: /* Suite B */
            case 12U: /* Suite B 192-bit */
            case 13U: /* FT 802.1X SHA384 */
                result |= WIFI_AP_AKM_ENTERPRISE;
                break;
            case 2U: /* PSK */
            case 4U: /* FT PSK */
            case 6U: /* SHA256 PSK */
                result |= WIFI_AP_AKM_PSK;
                break;
            case 8U: /* SAE */
            case 9U: /* FT SAE */
                result |= WIFI_AP_AKM_SAE;
                break;
            case 18U: /* OWE */
                result |= WIFI_AP_AKM_OWE;
                break;
            default:
                break;
        }
    }
    *a_akm = result;
    return true;
}

static void classify( rfscout_wifi_ap_observation_t *a_ap,
                      bool a_privacy,
                      bool a_rsn,
                      uint32_t a_akm,
                      bool a_legacy_wpa )
{
    a_ap->security = RFSCOUT_WIFI_AP_SECURITY_UNKNOWN;
    a_ap->auth = RFSCOUT_WIFI_AP_AUTH_UNKNOWN;
    if( a_rsn )
    {
        a_ap->security = RFSCOUT_WIFI_AP_SECURITY_SECURED;
        if( WIFI_AP_AKM_OWE == a_akm && !a_legacy_wpa )
        {
            a_ap->security = RFSCOUT_WIFI_AP_SECURITY_OWE;
            a_ap->auth = RFSCOUT_WIFI_AP_AUTH_OWE;
        }
        else if( 0U != ( a_akm & WIFI_AP_AKM_ENTERPRISE ) )
        {
            a_ap->auth = RFSCOUT_WIFI_AP_AUTH_ENTERPRISE;
        }
        else if( 0U == ( a_akm & WIFI_AP_AKM_OWE ) && !a_legacy_wpa )
        {
            if( ( a_akm & ( WIFI_AP_AKM_PSK | WIFI_AP_AKM_SAE ) ) ==
                ( WIFI_AP_AKM_PSK | WIFI_AP_AKM_SAE ) )
            {
                a_ap->auth = RFSCOUT_WIFI_AP_AUTH_WPA2_WPA3_PERSONAL;
            }
            else if( 0U != ( a_akm & WIFI_AP_AKM_SAE ) )
            {
                a_ap->auth = RFSCOUT_WIFI_AP_AUTH_WPA3_SAE;
            }
            else if( 0U != ( a_akm & WIFI_AP_AKM_PSK ) )
            {
                a_ap->auth = RFSCOUT_WIFI_AP_AUTH_WPA2_PSK;
            }
        }
    }
    else if( a_legacy_wpa )
    {
        a_ap->security = RFSCOUT_WIFI_AP_SECURITY_SECURED;
        a_ap->auth = RFSCOUT_WIFI_AP_AUTH_WPA;
    }
    else if( a_privacy )
    {
        a_ap->security = RFSCOUT_WIFI_AP_SECURITY_SECURED;
        a_ap->auth = RFSCOUT_WIFI_AP_AUTH_WEP;
    }
    else
    {
        a_ap->security = RFSCOUT_WIFI_AP_SECURITY_OPEN;
        a_ap->auth = RFSCOUT_WIFI_AP_AUTH_OPEN;
    }
}

bool wifi_ap_observation_parse( const uint8_t *a_frame,
                                size_t a_length,
                                int8_t a_rssi,
                                uint8_t a_channel,
                                rfscout_wifi_ap_observation_t *a_output )
{
    if( NULL == a_frame || NULL == a_output || a_length < WIFI_AP_IE_OFFSET )
    {
        return false;
    }
    const uint16_t control = little_endian_16( a_frame );
    if( 0x0080U != control && 0x0050U != control )
    {
        return false;
    }
    const uint8_t *bssid = &a_frame[ 16 ];
    if( 0U != ( bssid[ 0 ] & 0x01U ) )
    {
        return false;
    }

    rfscout_wifi_ap_observation_t result = { 0 };
    memcpy( result.bssid, bssid, sizeof( result.bssid ) );
    result.channel = a_channel;
    result.rssi = a_rssi;
    const bool privacy = 0U != ( little_endian_16( &a_frame[ 34 ] ) & 0x0010U );
    bool rsn = false;
    bool legacy_wpa = false;
    bool ssid_seen = false;
    uint32_t akm = 0U;

    size_t cursor = WIFI_AP_IE_OFFSET;
    while( cursor < a_length )
    {
        if( a_length - cursor < 2U )
        {
            return false;
        }
        const uint8_t id = a_frame[ cursor++ ];
        const size_t length = a_frame[ cursor++ ];
        if( length > a_length - cursor )
        {
            return false;
        }
        const uint8_t *payload = &a_frame[ cursor ];
        if( WIFI_AP_IE_SSID == id )
        {
            if( ssid_seen || length > RFSCOUT_WIFI_AP_SSID_MAX_LENGTH )
            {
                return false;
            }
            ssid_seen = true;
            if( length > 0U && NULL == memchr( payload, '\0', length ) )
            {
                memcpy( result.ssid, payload, length );
                result.ssid_length = (uint8_t)length;
            }
        }
        else if( WIFI_AP_IE_RSN == id )
        {
            if( rsn || !parse_rsn( payload, length, &akm ) )
            {
                return false;
            }
            rsn = true;
        }
        else if( WIFI_AP_IE_VENDOR == id && length >= 4U && 0x00U == payload[ 0 ] &&
                 0x50U == payload[ 1 ] && 0xf2U == payload[ 2 ] && 0x01U == payload[ 3 ] )
        {
            legacy_wpa = true;
        }
        cursor += length;
    }
    classify( &result, privacy, rsn, akm, legacy_wpa );
    *a_output = result;
    return true;
}
