/**
    audio_policy.c : RFScout device-wide audio presentation policy
    Description: Supplies Normal/Quiet enablement independently from visual themes and gain state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_policy.h"

#include <stddef.h>
#include <strings.h>

static const rfscout_audio_policy_t policies[ RFSCOUT_AUDIO_POLICY_COUNT ] = {
    [RFSCOUT_AUDIO_POLICY_NORMAL] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = true,
        },
    [RFSCOUT_AUDIO_POLICY_QUIET] =
        {
            .ui_audio = false,
            .event_audio = false,
            .alert_audio = false,
            .content_audio = true,
        },
};

const rfscout_audio_policy_t *audio_policy_get( rfscout_audio_policy_id_t a_policy )
{
    if( a_policy >= RFSCOUT_AUDIO_POLICY_COUNT )
    {
        a_policy = RFSCOUT_AUDIO_POLICY_NORMAL;
    }
    return &policies[ a_policy ];
}

bool audio_policy_category_enabled( rfscout_audio_policy_id_t a_policy,
                                    rfscout_audio_category_t a_category )
{
    const rfscout_audio_policy_t *policy = audio_policy_get( a_policy );
    switch( a_category )
    {
        case RFSCOUT_AUDIO_CATEGORY_UI:
            return policy->ui_audio;
        case RFSCOUT_AUDIO_CATEGORY_EVENT:
            return policy->event_audio;
        case RFSCOUT_AUDIO_CATEGORY_ALERT:
            return policy->alert_audio;
        case RFSCOUT_AUDIO_CATEGORY_CONTENT:
            return policy->content_audio;
        case RFSCOUT_AUDIO_CATEGORY_COUNT:
        default:
            return false;
    }
}

const char *audio_category_name( rfscout_audio_category_t a_category )
{
    static const char *names[ RFSCOUT_AUDIO_CATEGORY_COUNT ] = {
        "ui", "event", "alert", "content" };
    return ( a_category < RFSCOUT_AUDIO_CATEGORY_COUNT ) ? names[ a_category ] : "unknown";
}

const char *audio_policy_config_name( rfscout_audio_policy_id_t a_policy )
{
    static const char *names[ RFSCOUT_AUDIO_POLICY_COUNT ] = { "normal", "quiet" };
    return ( a_policy < RFSCOUT_AUDIO_POLICY_COUNT ) ? names[ a_policy ]
                                                     : names[ RFSCOUT_AUDIO_POLICY_NORMAL ];
}

const char *audio_policy_display_name( rfscout_audio_policy_id_t a_policy )
{
    static const char *names[ RFSCOUT_AUDIO_POLICY_COUNT ] = { "Normal", "Quiet" };
    return ( a_policy < RFSCOUT_AUDIO_POLICY_COUNT ) ? names[ a_policy ]
                                                     : names[ RFSCOUT_AUDIO_POLICY_NORMAL ];
}

bool audio_policy_from_config_name( const char *a_name, rfscout_audio_policy_id_t *a_policy )
{
    if( ( NULL == a_name ) || ( NULL == a_policy ) )
    {
        return false;
    }

    for( unsigned index = 0U; index < RFSCOUT_AUDIO_POLICY_COUNT; ++index )
    {
        if( 0 ==
            strcasecmp( a_name, audio_policy_config_name( (rfscout_audio_policy_id_t)index ) ) )
        {
            *a_policy = (rfscout_audio_policy_id_t)index;
            return true;
        }
    }
    return false;
}
