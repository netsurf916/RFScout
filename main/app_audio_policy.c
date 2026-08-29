/**
    app_audio_policy.c : RFScout application audio behavior policy
    Description: Supplies native per-application feedback, notification, and content-audio traits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_audio_policy.h"

static const rfscout_app_audio_policy_t policies[ RFSCOUT_APP_COUNT ] = {
    [RFSCOUT_APP_WIFI_BT] =
        {
            .interaction_feedback = true,
            .general_event_feedback = true,
            .notifications_supported = true,
            .notifications_enabled_default = true,
            .content_audio = false,
        },
    [RFSCOUT_APP_CC1101] =
        {
            .interaction_feedback = true,
            .general_event_feedback = true,
            .notifications_supported = true,
            .notifications_enabled_default = true,
            .content_audio = false,
        },
    [RFSCOUT_APP_NRF24] =
        {
            .interaction_feedback = true,
            .general_event_feedback = true,
            .notifications_supported = true,
            .notifications_enabled_default = true,
            .content_audio = false,
        },
    [RFSCOUT_APP_AUDIO] =
        {
            .interaction_feedback = false,
            .general_event_feedback = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
            .content_audio = true,
        },
    [RFSCOUT_APP_THERMAL] =
        {
            .interaction_feedback = true,
            .general_event_feedback = true,
            .notifications_supported = false,
            .notifications_enabled_default = false,
            .content_audio = false,
        },
    [RFSCOUT_APP_DEVICE] =
        {
            .interaction_feedback = true,
            .general_event_feedback = true,
            .notifications_supported = false,
            .notifications_enabled_default = false,
            .content_audio = false,
        },
};

const rfscout_app_audio_policy_t *app_audio_policy_get( rfscout_app_id_t a_app )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        a_app = RFSCOUT_APP_DEVICE;
    }
    return &policies[ a_app ];
}
