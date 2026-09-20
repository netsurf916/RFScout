/**
    app_audio_policy.c : RFScout application audio behavior policy
    Description: Supplies native per-application UI, event, alert, and content-audio traits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_audio_policy.h"

static const rfscout_app_audio_policy_t policies[ RFSCOUT_APP_COUNT ] = {
    [RFSCOUT_APP_SYSTEM_EVENTS] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
        },
    [RFSCOUT_APP_OTA] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
        },
    [RFSCOUT_APP_WIFI_BT] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = true,
            .notifications_enabled_default = true,
        },
    [RFSCOUT_APP_CC1101] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = true,
            .notifications_enabled_default = true,
        },
    [RFSCOUT_APP_NRF24] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = true,
            .notifications_enabled_default = true,
        },
    [RFSCOUT_APP_AUDIO] =
        {
            .ui_audio = false,
            .event_audio = false,
            .alert_audio = false,
            .content_audio = true,
            .notifications_supported = false,
            .notifications_enabled_default = false,
        },
    [RFSCOUT_APP_THERMAL] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
        },
    [RFSCOUT_APP_AUDIO_SPECTRUM] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
        },
    [RFSCOUT_APP_DEVICE] =
        {
            .ui_audio = true,
            .event_audio = true,
            .alert_audio = true,
            .content_audio = false,
            .notifications_supported = false,
            .notifications_enabled_default = false,
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

bool app_audio_policy_allows( rfscout_app_id_t a_app, rfscout_audio_category_t a_category )
{
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
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
