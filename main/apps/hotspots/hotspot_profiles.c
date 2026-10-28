/**
    apps/hotspots/hotspot_profiles.c : Hotspots saved-profile selection
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_profiles.h"

#include <string.h>

/** A profile's associated BSSID is seen in the latest completed scan with the same SSID. */
static bool profile_overlaps_scan( const hotspot_profiles_input_t *a_input,
                                   const saved_network_profile_t *a_profile,
                                   const char *a_ssid )
{
    const hotspot_table_t *table = a_input->table;
    for( size_t index = 0U; NULL != table && index < table->count; ++index )
    {
        const hotspot_entry_t *other = &table->entries[ index ];
        const saved_network_bssid_t *known =
            saved_network_profile_bssid( a_profile, other->bss.bssid );
        if( other->visible && 0 == strcmp( other->bss.ssid, a_ssid ) && NULL != known &&
            SAVED_NETWORK_BSSID_ASSOCIATED == known->state )
        {
            return true;
        }
    }
    return false;
}

/** A profile's associated BSSID has history within the match distance of the current fix. */
static bool profile_near( const hotspot_profiles_input_t *a_input,
                          const saved_network_profile_t *a_profile )
{
    const uint16_t limit = a_input->match_distance_m;
    if( 0U == limit || NULL == a_input->history_bounds || !gps_location_fresh( a_input->gps ) )
    {
        return false;
    }
    for( size_t bssid = 0U; bssid < a_profile->bssid_count; ++bssid )
    {
        if( SAVED_NETWORK_BSSID_ASSOCIATED != a_profile->bssids[ bssid ].state )
        {
            continue;
        }
        const geo_bounds_t *bounds = a_input->history_bounds( a_profile->bssids[ bssid ].address );
        if( NULL == bounds )
        {
            continue;
        }
        const double distance = geo_bounds_distance_m(
            bounds, a_input->gps->latitude_deg, a_input->gps->longitude_deg );
        if( distance >= 0.0 && distance <= (double)limit )
        {
            return true;
        }
    }
    return false;
}

hotspot_credential_t hotspot_profiles_resolve( const hotspot_profiles_input_t *a_input,
                                               const hotspot_entry_t *a_entry,
                                               uint32_t *a_profile_id )
{
    *a_profile_id = 0U;
    size_t candidates = 0U;
    size_t with_evidence = 0U;
    for( size_t index = 0U; NULL != a_input->profiles && index < a_input->profile_count; ++index )
    {
        const saved_network_profile_t *profile = &a_input->profiles[ index ];
        if( !profile->has_password || 0 != strcmp( profile->ssid, a_entry->bss.ssid ) ||
            !saved_network_profile_auth_compatible( profile, a_entry->bss.auth_mode ) )
        {
            continue;
        }
        const saved_network_bssid_t *known =
            saved_network_profile_bssid( profile, a_entry->bss.bssid );
        /* Existing saved REJECTED entries are no longer a credential veto. */
        if( NULL != known && SAVED_NETWORK_BSSID_ASSOCIATED == known->state &&
            known->auth_mode == a_entry->bss.auth_mode )
        {
            *a_profile_id = profile->profile_id;
            return HOTSPOT_CREDENTIAL_KNOWN;
        }
        ++candidates;
        if( profile_overlaps_scan( a_input, profile, a_entry->bss.ssid ) ||
            profile_near( a_input, profile ) )
        {
            ++with_evidence;
            *a_profile_id = profile->profile_id;
        }
    }
    if( 1U == with_evidence )
    {
        return HOTSPOT_CREDENTIAL_INFERRED;
    }
    *a_profile_id = 0U;
    if( with_evidence > 1U )
    {
        return HOTSPOT_CREDENTIAL_AMBIGUOUS;
    }
    if( 0U != candidates )
    {
        return HOTSPOT_CREDENTIAL_UNMATCHED;
    }
    return HOTSPOT_CREDENTIAL_NONE;
}
