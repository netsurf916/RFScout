/**
    feedback.c : RFScout semantic feedback policy
    Description: Maps accepted UI/device, event, and warning/error semantics to disposable sounds.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "feedback.h"

#include "app_audio_policy.h"
#include "app_manager.h"
#include "audio_output.h"
#include "audio_policy.h"
#include "device_config.h"
#include "event.h"
#include "system_events.h"

#include <stddef.h>
#include <stdint.h>

#define FEEDBACK_NAV_HZ 1150U
#define FEEDBACK_ACTIVATE_HZ 1450U
#define FEEDBACK_BACK_HZ 850U
#define FEEDBACK_STATE_HZ 1300U
#define FEEDBACK_LOCK_HZ 700U
#define FEEDBACK_DETECTED_HZ 1000U
#define FEEDBACK_CONNECTED_HZ 1550U
#define FEEDBACK_DISCONNECTED_HZ 850U
#define FEEDBACK_WARNING_HZ 650U
#define FEEDBACK_ERROR_HZ 425U
#define FEEDBACK_NAV_MS 18U
#define FEEDBACK_ACTION_MS 35U
#define FEEDBACK_STATE_MS 55U
#define FEEDBACK_DETECTED_MS 45U
#define FEEDBACK_SYSTEM_STATE_MS 70U
#define FEEDBACK_WARNING_MS 65U
#define FEEDBACK_ERROR_MS 90U

static bool is_system_state_feedback( rfscout_event_type_t a_type )
{
    switch( a_type )
    {
        case RFSCOUT_EVENT_POWER_CONNECTED:
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
        case RFSCOUT_EVENT_APP_AVAILABLE:
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            return true;
        default:
            return false;
    }
}

static uint8_t policy_source_for_event( const rfscout_event_t *a_event )
{
    if( is_system_state_feedback( a_event->type ) ||
        ( ( RFSCOUT_EVENT_SOURCE_SYSTEM == a_event->source ) &&
          ( ( RFSCOUT_EVENT_INPUT_LOCKED == a_event->type ) ||
            ( RFSCOUT_EVENT_INPUT_UNLOCKED == a_event->type ) ) ) )
    {
        return (uint8_t)app_manager_current();
    }
    return a_event->source;
}

static bool app_allows_category( uint8_t a_source, rfscout_audio_category_t a_category )
{
    if( RFSCOUT_EVENT_SOURCE_SYSTEM == a_source )
    {
        return true;
    }
    if( a_source >= (uint8_t)RFSCOUT_APP_COUNT )
    {
        return false;
    }
    return app_audio_policy_allows( (rfscout_app_id_t)a_source, a_category );
}

static uint8_t category_volume( rfscout_audio_category_t a_category )
{
    const device_config_t *config = device_config_get();
    switch( a_category )
    {
        case RFSCOUT_AUDIO_CATEGORY_UI:
            return config->audio_interaction_gain_percent;
        case RFSCOUT_AUDIO_CATEGORY_EVENT:
            return config->audio_notification_gain_percent;
        case RFSCOUT_AUDIO_CATEGORY_ALERT:
            return config->alert_volume_percent;
        case RFSCOUT_AUDIO_CATEGORY_CONTENT:
        case RFSCOUT_AUDIO_CATEGORY_COUNT:
        default:
            return 0U;
    }
}

static void on_event( const rfscout_event_t *a_event, void *a_context )
{
    (void)a_context;
    if( NULL == a_event )
    {
        return;
    }

    if( RFSCOUT_EVENT_AUDIO_POLICY_CHANGED == a_event->type )
    {
        if( !audio_policy_category_enabled( device_config_audio_policy(),
                                            RFSCOUT_AUDIO_CATEGORY_UI ) ||
            !audio_policy_category_enabled( device_config_audio_policy(),
                                            RFSCOUT_AUDIO_CATEGORY_EVENT ) ||
            !audio_policy_category_enabled( device_config_audio_policy(),
                                            RFSCOUT_AUDIO_CATEGORY_ALERT ) )
        {
            audio_output_cancel_feedback_pending();
        }
        return;
    }

    if( is_system_state_feedback( a_event->type ) && !system_events_notifications_enabled() )
    {
        return;
    }

    const rfscout_audio_category_t category = rfscout_event_audio_category( a_event->type );
    if( ( RFSCOUT_AUDIO_CATEGORY_COUNT == category ) ||
        ( RFSCOUT_EVENT_NOTIFICATION == a_event->type ) )
    {
        return;
    }

    const uint8_t volume = category_volume( category );
    if( !audio_policy_category_enabled( device_config_audio_policy(), category ) ||
        ( 0U == volume ) || !app_allows_category( policy_source_for_event( a_event ), category ) )
    {
        return;
    }

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
        case RFSCOUT_EVENT_DETECTED:
            frequency = FEEDBACK_DETECTED_HZ;
            duration = FEEDBACK_DETECTED_MS;
            break;
        case RFSCOUT_EVENT_POWER_CONNECTED:
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_APP_AVAILABLE:
            frequency = FEEDBACK_CONNECTED_HZ;
            duration = FEEDBACK_SYSTEM_STATE_MS;
            break;
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            frequency = FEEDBACK_DISCONNECTED_HZ;
            duration = FEEDBACK_SYSTEM_STATE_MS;
            break;
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
        case RFSCOUT_EVENT_WARNING:
            frequency = FEEDBACK_WARNING_HZ;
            duration = FEEDBACK_WARNING_MS;
            break;
        case RFSCOUT_EVENT_ERROR:
            frequency = FEEDBACK_ERROR_HZ;
            duration = FEEDBACK_ERROR_MS;
            break;
        case RFSCOUT_EVENT_ACTIVATE:
        case RFSCOUT_EVENT_ENTER:
            break;
        case RFSCOUT_EVENT_THEME_CHANGED:
        case RFSCOUT_EVENT_AUDIO_POLICY_CHANGED:
        case RFSCOUT_EVENT_NOTIFICATION:
        case RFSCOUT_EVENT_TYPE_COUNT:
        default:
            return;
    }

    if( RFSCOUT_AUDIO_CATEGORY_ALERT == category )
    {
        (void)audio_output_play_notification_tone( frequency, duration, volume );
    }
    else
    {
        (void)audio_output_play_feedback_tone( frequency, duration, volume );
    }
}

esp_err_t feedback_init( void )
{
    return rfscout_event_subscribe( on_event, NULL ) ? ESP_OK : ESP_ERR_NO_MEM;
}
