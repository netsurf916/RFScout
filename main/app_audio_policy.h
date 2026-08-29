/**
    app_audio_policy.h : RFScout application audio behavior policy
    Description: Declares native per-application feedback, notification, and content-audio traits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_AUDIO_POLICY_H_
#define _RFSCOUT_APP_AUDIO_POLICY_H_

#include "app_manager.h"

#include <stdbool.h>

typedef struct
{
    bool interaction_feedback;
    bool general_event_feedback;
    bool notifications_supported;
    bool notifications_enabled_default;
    bool content_audio;
} rfscout_app_audio_policy_t;

/**
 * @brief Return the native audio behavior profile for one application.
 *
 * Native policy is intentionally separate from user preferences and the active
 * global theme. It describes what experience makes sense for the application
 * itself; for example, Recorder suppresses incidental interaction and general
 * event sounds while still owning intentional content audio.
 */
const rfscout_app_audio_policy_t *app_audio_policy_get( rfscout_app_id_t a_app );

#endif // _RFSCOUT_APP_AUDIO_POLICY_H_
