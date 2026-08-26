/**
    alert.c : RFScout audible alert policy implementation
    Description: Converts runtime alert preferences into asynchronous speaker requests.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"

#include "audio_output.h"
#include "morse.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <ctype.h>

#define ALERT_TONE_FREQUENCY_HZ 900U
#define ALERT_TONE_DURATION_MS 250U
#define ALERT_MORSE_WPM 20U
#define ALERT_MORSE_UNIT_MS ( 1200U / ALERT_MORSE_WPM )
#define ALERT_MORSE_DEFAULT_CHARACTER 'S'

static rfscout_alert_mode_t mode = RFSCOUT_ALERT_OFF;
static char morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
static SemaphoreHandle_t alert_mutex;

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
    morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
    (void)xSemaphoreGive( alert_mutex );
    return audio_output_init();
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
    if( RFSCOUT_ALERT_OFF == mode )
    {
        audio_output_cancel_pending();
    }
    (void)xSemaphoreGive( alert_mutex );
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
    if( ( NULL == alert_mutex ) || ( pdTRUE != xSemaphoreTake( alert_mutex, 0U ) ) )
    {
        return false;
    }

    bool queued = false;
    switch( mode )
    {
        case RFSCOUT_ALERT_OFF:
            queued = true;
            break;

        case RFSCOUT_ALERT_TONE:
            queued = audio_output_play_tone( ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS );
            break;

        case RFSCOUT_ALERT_MORSE:
            queued = audio_output_play_morse(
                morse_character, ALERT_TONE_FREQUENCY_HZ, ALERT_MORSE_UNIT_MS );
            break;

        default:
            break;
    }

    (void)xSemaphoreGive( alert_mutex );
    return queued;
}
