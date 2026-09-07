/**
    alert.c : RFScout alert-class audio presentation policy
    Description: Resolves semantic alerts through app, global, and device audio policy into requests.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"

#include "audio_output.h"
#include "audio_policy.h"
#include "board.h"
#include "device_config.h"
#include "event.h"
#include "morse.h"
#include "notification_preferences.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <ctype.h>
#include <stddef.h>

#define ALERT_TONE_FREQUENCY_HZ 900U
#define ALERT_TONE_DURATION_MS 250U
#define ALERT_MORSE_WPM 20U
#define ALERT_MORSE_UNIT_MS ( 1200U / ALERT_MORSE_WPM )
#define ALERT_MORSE_DEFAULT_CHARACTER 'S'
#define ALERT_AUDIO_OFF ' '

static const char *TAG = "alert";
static rfscout_alert_mode_t mode = RFSCOUT_ALERT_OFF;
static uint8_t volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
static char morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
static SemaphoreHandle_t alert_mutex;

static uint8_t effective_alert_volume_locked( void )
{
    if( !audio_policy_category_enabled( device_config_audio_policy(),
                                        RFSCOUT_AUDIO_CATEGORY_ALERT ) )
    {
        return 0U;
    }
    return volume_percent;
}

static bool request_generic_locked( char a_character )
{
    const uint8_t effective_volume = effective_alert_volume_locked();
    if( ( RFSCOUT_ALERT_OFF == mode ) || ( 0U == effective_volume ) )
    {
        return true;
    }

    if( RFSCOUT_ALERT_TONE == mode )
    {
        return audio_output_play_notification_tone(
            ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS, effective_volume );
    }
    if( RFSCOUT_ALERT_MORSE == mode && alert_morse_character_supported( a_character ) )
    {
        return audio_output_play_notification_morse( (char)toupper( (unsigned char)a_character ),
                                                     ALERT_TONE_FREQUENCY_HZ,
                                                     ALERT_MORSE_UNIT_MS,
                                                     effective_volume );
    }
    return false;
}

static bool request_notification_locked( char a_character )
{
    const uint8_t effective_volume = effective_alert_volume_locked();
    if( ( ALERT_AUDIO_OFF == a_character ) || ( RFSCOUT_ALERT_OFF == mode ) ||
        ( 0U == effective_volume ) )
    {
        return true;
    }

    if( RFSCOUT_ALERT_TONE == mode )
    {
        return audio_output_play_notification_tone(
            ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS, effective_volume );
    }
    if( RFSCOUT_ALERT_MORSE == mode && alert_morse_character_supported( a_character ) )
    {
        return audio_output_play_notification_morse( (char)toupper( (unsigned char)a_character ),
                                                     ALERT_TONE_FREQUENCY_HZ,
                                                     ALERT_MORSE_UNIT_MS,
                                                     effective_volume );
    }
    return false;
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
                                            RFSCOUT_AUDIO_CATEGORY_ALERT ) )
        {
            audio_output_cancel_notifications_pending();
        }
        return;
    }

    if( RFSCOUT_EVENT_NOTIFICATION != a_event->type )
    {
        return;
    }
    if( ( a_event->value < 0 ) || ( a_event->value >= RFSCOUT_NOTIFICATION_TYPE_COUNT ) )
    {
        return;
    }

    const rfscout_notification_type_t type = (rfscout_notification_type_t)a_event->value;
    const rfscout_notification_descriptor_t *descriptor = notification_descriptor( type );
    if( ( NULL == descriptor ) || ( (uint8_t)descriptor->app != a_event->source ) )
    {
        return;
    }
    if( !notification_preferences_app_enabled( descriptor->app ) )
    {
        return;
    }

    /* Intentional application playback owns the speaker. Alert audio is disposable
       and must not contaminate or queue behind content audio. */
    if( audio_output_playback_active() )
    {
        return;
    }

    if( ( NULL == alert_mutex ) || ( pdTRUE != xSemaphoreTake( alert_mutex, 0U ) ) )
    {
        ESP_LOGW( TAG, "alert dropped while presentation policy busy: %s", descriptor->label );
        return;
    }

    const char character = device_config_notification_character( type );
    const bool queued = request_notification_locked( character );
    (void)xSemaphoreGive( alert_mutex );
    if( !queued )
    {
        ESP_LOGW( TAG, "alert audio dropped: %s", descriptor->label );
    }
}

esp_err_t alert_init( void )
{
    if( NULL == alert_mutex )
    {
        alert_mutex = xSemaphoreCreateMutex();
        if( NULL == alert_mutex )
        {
            return ESP_ERR_NO_MEM;
        }
    }

    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return ESP_FAIL;
    }
    mode = RFSCOUT_ALERT_OFF;
    volume_percent = RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT;
    morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
    (void)xSemaphoreGive( alert_mutex );

    if( board_profile_has( RFSCOUT_BOARD_CAP_SPEAKER ) )
    {
        const esp_err_t audio_result = audio_output_init();
        if( ( ESP_OK != audio_result ) && ( ESP_ERR_NOT_SUPPORTED != audio_result ) )
        {
            return audio_result;
        }
    }

    return rfscout_event_subscribe( on_event, NULL ) ? ESP_OK : ESP_ERR_NO_MEM;
}

const char *alert_mode_name( rfscout_alert_mode_t a_mode )
{
    switch( a_mode )
    {
        case RFSCOUT_ALERT_TONE:
            return "TONE";
        case RFSCOUT_ALERT_MORSE:
            return "MORSE";
        case RFSCOUT_ALERT_OFF:
        default:
            return "OFF";
    }
}

const rfscout_notification_descriptor_t *
alert_notification_descriptor( rfscout_notification_type_t a_type )
{
    return notification_descriptor( a_type );
}

const char *alert_notification_name( rfscout_notification_type_t a_type )
{
    return notification_name( a_type );
}

const char *alert_notification_config_key( rfscout_notification_type_t a_type )
{
    return notification_config_key( a_type );
}

rfscout_app_id_t alert_notification_app( rfscout_notification_type_t a_type )
{
    return notification_app( a_type );
}

char alert_notification_default_character( rfscout_notification_type_t a_type )
{
    return notification_default_character( a_type );
}

rfscout_alert_mode_t alert_mode( void )
{
    rfscout_alert_mode_t current = mode;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = mode;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

void alert_set_mode( rfscout_alert_mode_t a_mode )
{
    if( ( a_mode >= RFSCOUT_ALERT_MODE_COUNT ) || ( NULL == alert_mutex ) )
    {
        return;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return;
    }

    mode = a_mode;
    (void)xSemaphoreGive( alert_mutex );
    if( RFSCOUT_ALERT_OFF == a_mode )
    {
        audio_output_cancel_notifications_pending();
    }
}

uint8_t alert_volume_percent( void )
{
    uint8_t current = volume_percent;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = volume_percent;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

bool alert_set_volume_percent( uint8_t a_percent )
{
    if( ( a_percent > RFSCOUT_ALERT_VOLUME_MAX_PERCENT ) || ( NULL == alert_mutex ) )
    {
        return false;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return false;
    }
    volume_percent = a_percent;
    (void)xSemaphoreGive( alert_mutex );
    return true;
}

char alert_morse_character( void )
{
    char current = morse_character;
    if( ( NULL != alert_mutex ) && ( pdTRUE == xSemaphoreTake( alert_mutex, portMAX_DELAY ) ) )
    {
        current = morse_character;
        (void)xSemaphoreGive( alert_mutex );
    }
    return current;
}

bool alert_morse_character_supported( char a_character )
{
    return NULL != morse_pattern( a_character );
}

bool alert_set_morse_character( char a_character )
{
    if( !alert_morse_character_supported( a_character ) || ( NULL == alert_mutex ) )
    {
        return false;
    }
    if( pdTRUE != xSemaphoreTake( alert_mutex, portMAX_DELAY ) )
    {
        return false;
    }
    morse_character = (char)toupper( (unsigned char)a_character );
    (void)xSemaphoreGive( alert_mutex );
    return true;
}

bool alert_request( void )
{
    return alert_request_character( alert_morse_character() );
}

bool alert_request_character( char a_character )
{
    if( ( NULL == alert_mutex ) || ( pdTRUE != xSemaphoreTake( alert_mutex, 0U ) ) )
    {
        return false;
    }
    const bool queued = request_generic_locked( a_character );
    (void)xSemaphoreGive( alert_mutex );
    return queued;
}

bool alert_request_notification( rfscout_notification_type_t a_type )
{
    return notification_emit( a_type );
}
