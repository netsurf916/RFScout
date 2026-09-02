/**
    audio_policy.h : RFScout device-wide audio presentation policy
    Description: Defines independent Normal/Quiet policy for incidental feedback and detection notifications.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_POLICY_H_
#define _RFSCOUT_AUDIO_POLICY_H_

#include <stdbool.h>

typedef enum
{
    RFSCOUT_AUDIO_POLICY_NORMAL = 0,
    RFSCOUT_AUDIO_POLICY_QUIET,
    RFSCOUT_AUDIO_POLICY_COUNT
} rfscout_audio_policy_id_t;

typedef struct
{
    bool interaction_audio;
    bool notification_audio;
} rfscout_audio_policy_t;

/** @brief Return presentation policy for one audio-policy identifier. */
const rfscout_audio_policy_t *audio_policy_get( rfscout_audio_policy_id_t a_policy );
/** @brief Return stable configuration token for one audio policy. */
const char *audio_policy_config_name( rfscout_audio_policy_id_t a_policy );
/** @brief Return user-facing name for one audio policy. */
const char *audio_policy_display_name( rfscout_audio_policy_id_t a_policy );
/** @brief Parse one configuration token. */
bool audio_policy_from_config_name( const char *a_name, rfscout_audio_policy_id_t *a_policy );

#endif // _RFSCOUT_AUDIO_POLICY_H_
