/**
    feedback.c : RFScout semantic feedback policy
    Description: Maps accepted UI/state events to disposable procedural sounds without coupling producers to audio.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "feedback.h"

#include "app_audio_policy.h"
#include "audio_output.h"
#include "event.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdint.h>

#define FEEDBACK_NAV_HZ 1150U
#define FEEDBACK_ACTIVATE_HZ 1450U
#define FEEDBACK_BACK_HZ 850U
#define FEEDBACK_STATE_HZ 1300U
#define FEEDBACK_LOCK_HZ 700U
#define FEEDBACK_NAV_MS 18U
#define FEEDBACK_ACTION_MS 35U
#define FEEDBACK_STATE_MS 55U

static bool app_allows_interaction_feedback( uint8_t a_source )
{
    if( RFSCOUT_EVENT_SOURCE_SYSTEM == a_source )
    {
        return true;
    }
    if( a_source >= (uint8_t)RFSCOUT_APP_COUNT )
    {
        return false;
    }
    return app_audio_policy_get( (rfscout_app_id_t)a_source )->interaction_feedback;
}

static void on_event( const rfscout_event_t *a_event, void *a_context )
{
    (void)a_context;
    if( NULL == a_event )
    {
        return;
    }

    if( RFSCOUT_EVENT_THEME_CHANGED == a_event->type )
    {
        const ui_theme_sound_policy_t *policy = ui_theme_sound_get();
        if( ( NULL == policy ) || !policy->interaction_audio )
        {
            audio_output_cancel_feedback_pending();
        }
        return;
    }

    const ui_theme_sound_policy_t *policy = ui_theme_sound_get();
    if( ( NULL == policy ) || !policy->interaction_audio ||
        ( 0U == policy->interaction_volume_percent ) ||
        !app_allows_interaction_feedback( a_event->source ) )
    {
        return;
    }

    /* Intentional application audio owns the speaker. Routine UI feedback is
       disposable and must never queue behind playback for later surprise output. */
    if( audio_output_playback_active() )
    {
        return;
    }

    uint16_t frequency = FEEDBACK_ACTIVATE_HZ;
    uint32_t duration = FEEDBACK_ACTION_MS;
    switch( a_event->type )
    {
        case RFSCOUT_EVENT_NAVIGATE:
            frequency = FEEDBACK_NAV_HZ;
            duration = FEEDBACK_NAV_MS;
            break;
        case RFSCOUT_EVENT_BACK:
            frequency = FEEDBACK_BACK_HZ;
            break;
        case RFSCOUT_EVENT_ENABLE:
            frequency = FEEDBACK_STATE_HZ + 250U;
            duration = FEEDBACK_STATE_MS;
            break;
        case RFSCOUT_EVENT_DISABLE:
            frequency = FEEDBACK_STATE_HZ - 250U;
            duration = FEEDBACK_STATE_MS;
            break;
        case RFSCOUT_EVENT_INPUT_LOCKED:
            frequency = FEEDBACK_LOCK_HZ;
            duration = FEEDBACK_STATE_MS;
            break;
        case RFSCOUT_EVENT_INPUT_UNLOCKED:
            frequency = FEEDBACK_LOCK_HZ + 500U;
            duration = FEEDBACK_STATE_MS;
            break;
        case RFSCOUT_EVENT_ACTIVATE:
        case RFSCOUT_EVENT_ENTER:
            break;
        case RFSCOUT_EVENT_THEME_CHANGED:
        case RFSCOUT_EVENT_NOTIFICATION:
        case RFSCOUT_EVENT_WARNING:
        case RFSCOUT_EVENT_ERROR:
        case RFSCOUT_EVENT_TYPE_COUNT:
        default:
            return;
    }

    (void)audio_output_play_feedback_tone(
        frequency, duration, policy->interaction_volume_percent );
}

esp_err_t feedback_init( void )
{
    return rfscout_event_subscribe( on_event, NULL ) ? ESP_OK : ESP_ERR_NO_MEM;
}
