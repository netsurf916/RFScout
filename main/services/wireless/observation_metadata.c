/**
    observation_metadata.c : Shared observation time/location enrichment
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_metadata.h"

#include "device_time.h"

#include "esp_timer.h"

#include <stddef.h>

#define OBSERVATION_GPS_ENRICH_MAX_DELAY_MS 1000U

void observation_metadata_enrich( uint32_t a_seen_ms,
                                  const rfscout_gps_snapshot_t *a_gps,
                                  rfscout_observation_metadata_t *a_metadata )
{
    if( NULL == a_metadata )
    {
        return;
    }
    *a_metadata = ( rfscout_observation_metadata_t ){ .seen_ms = a_seen_ms };
    const uint32_t now_ms = (uint32_t)( esp_timer_get_time() / 1000ULL );
    const uint32_t delay_ms = now_ms - a_seen_ms;
    time_t now = 0;
    if( device_time_now( &now ) )
    {
        a_metadata->utc_epoch = (int64_t)now - (int64_t)( delay_ms / 1000U );
    }
    if( delay_ms <= OBSERVATION_GPS_ENRICH_MAX_DELAY_MS && gps_location_fresh( a_gps ) )
    {
        a_metadata->location_valid = true;
        a_metadata->latitude = a_gps->latitude_deg;
        a_metadata->longitude = a_gps->longitude_deg;
    }
}
