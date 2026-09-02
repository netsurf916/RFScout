/**
    audio_policy.h : RFScout device-wide audio presentation policy
    Description: Defines global policy for UI/device, event, alert, and intentional content audio.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_POLICY_H_
#define _RFSCOUT_AUDIO_POLICY_H_

#include <stdbool.h>

typedef enum
{
    RFSCOUT_AUDIO_CATEGORY_UI = 0,
    RFSCOUT_AUDIO_CATEGORY_EVENT,
    RFSCOUT_AUDIO_CATEGORY_ALERT,
    RFSCOUT_AUDIO_CATEGORY_CONTENT,
    RFSCOUT_AUDIO_CATEGORY_COUNT
} rfscout_audio_category_t;

typedef enum
{
    RFSCOUT_AUDIO_POLICY_NORMAL = 0,
    RFSCOUT_AUDIO_POLICY_QUIET,
    RFSCOUT_AUDIO_POLICY_COUNT
} rfscout_audio_policy_id_t;

typedef struct
{
    bool ui_audio;
    bool event_audio;
    bool alert_audio;
    bool content_audio;
} rfscout_audio_policy_t;

/** @brief Return presentation policy for one audio-policy identifier. */
const rfscout_audio_policy_t *audio_policy_get( rfscout_audio_policy_id_t a_policy );
/** @brief Return whether one semantic audio category is permitted by the policy. */
bool audio_policy_category_enabled( rfscout_audio_policy_id_t a_policy,
                                    rfscout_audio_category_t a_category );
/** @brief Return a stable lower-case name for an audio category. */
const char *audio_category_name( rfscout_audio_category_t a_category );
/** @brief Return stable configuration token for one audio policy. */
const char *audio_policy_config_name( rfscout_audio_policy_id_t a_policy );
/** @brief Return user-facing name for one audio policy. */
const char *audio_policy_display_name( rfscout_audio_policy_id_t a_policy );
/** @brief Parse one configuration token. */
bool audio_policy_from_config_name( const char *a_name, rfscout_audio_policy_id_t *a_policy );

#endif // _RFSCOUT_AUDIO_POLICY_H_
