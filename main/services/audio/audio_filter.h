/**
    services/audio/audio_filter.h : RFScout shared lightweight audio filtering
    Description: Provides the common microphone DC/low-frequency blocking primitive.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_FILTER_H_
#define _RFSCOUT_AUDIO_FILTER_H_

#include <stddef.h>

#define RFSCOUT_AUDIO_HIGHPASS_ALPHA 0.98f

typedef struct
{
    float previous_input;
    float previous_output;
} rfscout_audio_highpass_t;

static inline void audio_highpass_reset( rfscout_audio_highpass_t *a_state )
{
    if( NULL == a_state )
    {
        return;
    }
    a_state->previous_input = 0.0f;
    a_state->previous_output = 0.0f;
}

static inline float audio_highpass_process( rfscout_audio_highpass_t *a_state, float a_input )
{
    if( NULL == a_state )
    {
        return a_input;
    }
    const float output =
        a_input - a_state->previous_input + RFSCOUT_AUDIO_HIGHPASS_ALPHA * a_state->previous_output;
    a_state->previous_input = a_input;
    a_state->previous_output = output;
    return output;
}

#endif // _RFSCOUT_AUDIO_FILTER_H_
