/**
    app_audio_policy.h : RFScout application audio behavior policy
    Description: Declares native per-application UI, event, alert, and content-audio traits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_AUDIO_POLICY_H_
#define _RFSCOUT_APP_AUDIO_POLICY_H_

#include "app_descriptor.h"
#include "audio_policy.h"

#include <stdbool.h>

typedef struct
{
    bool ui_audio;
    bool event_audio;
    bool alert_audio;
    bool content_audio;

    /* Alert configuration capability remains distinct from the semantic class:
       an app may emit warnings/errors without owning configurable detection alerts. */
    bool notifications_supported;
    bool notifications_enabled_default;
} rfscout_app_audio_policy_t;

/**
 * @brief Return the native audio behavior profile for one application.
 *
 * Native policy is separate from user preferences and the device-wide audio
 * policy. It describes which semantic classes make sense for the application;
 * for example, Recorder suppresses incidental UI/event sounds while retaining
 * intentional content audio.
 */
const rfscout_app_audio_policy_t *app_audio_policy_get( rfscout_app_id_t a_app );

/** @brief Return whether one application natively permits an audio category. */
bool app_audio_policy_allows( rfscout_app_id_t a_app, rfscout_audio_category_t a_category );

#endif // _RFSCOUT_APP_AUDIO_POLICY_H_
