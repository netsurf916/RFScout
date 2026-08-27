/**
    alert.c : RFScout audible alert policy implementation
    Description: Converts device-wide alert preferences into asynchronous speaker requests.
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

/** @brief Queue an alert while the alert-policy mutex is already held. */
static bool request_locked( char a_character )
{
    switch( mode )
    {
        case RFSCOUT_ALERT_OFF:
            return true;

        case RFSCOUT_ALERT_TONE:
            return audio_output_play_tone( ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS );

        case RFSCOUT_ALERT_MORSE:
            if( !alert_morse_character_supported( a_character ) )
            {
                return false;
            }
            return audio_output_play_morse(
                (char)toupper( (unsigned char)a_character ),
                ALERT_TONE_FREQUENCY_HZ,
                ALERT_MORSE_UNIT_MS );

        default:
            return false;
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

const char *alert_notification_name( rfscout_notification_type_t a_type )
{
    switch( a_type )
    {
        case RFSCOUT_NOTIFICATION_FLOCK:
            return "Flock / ALPR";
        case RFSCOUT_NOTIFICATION_BODY_CAMERA:
            return "Body camera";
        case RFSCOUT_NOTIFICATION_DRONE:
            return "Drone";
        case RFSCOUT_NOTIFICATION_CAMERA:
            return "Camera";
        case RFSCOUT_NOTIFICATION_SMART_GLASSES:
            return "Smart glasses";
        case RFSCOUT_NOTIFICATION_ACOUSTIC_SENSOR:
            return "Acoustic sensor";
        case RFSCOUT_NOTIFICATION_SURVEILLANCE_OTHER:
            return "Other surveillance";
        case RFSCOUT_NOTIFICATION_CC1101_NARROW_ISM:
            return "CC1101 narrowband";
        case RFSCOUT_NOTIFICATION_CC1101_REMOTE_SENSOR:
            return "CC1101 remote/sensor";
        case RFSCOUT_NOTIFICATION_CC1101_CARRIER:
            return "CC1101 carrier";
        case RFSCOUT_NOTIFICATION_CC1101_BURSTY:
            return "CC1101 bursty RF";
        case RFSCOUT_NOTIFICATION_CC1101_WIDEBAND:
            return "CC1101 wide RF";
        case RFSCOUT_NOTIFICATION_NRF24_ESB:
            return "nRF24 ESB device";
        case RFSCOUT_NOTIFICATION_NRF24_INPUT:
            return "nRF24 input";
        case RFSCOUT_NOTIFICATION_NRF24_SENSOR:
            return "nRF24 sensor";
        case RFSCOUT_NOTIFICATION_NRF24_WIFI_LIKE:
            return "nRF24 WiFi-like";
        case RFSCOUT_NOTIFICATION_NRF24_BLE_LIKE:
            return "nRF24 hopping RF";
        case RFSCOUT_NOTIFICATION_NRF24_LOGITECH_LIKE:
            return "nRF24 Logitech";
        default:
            return "Unknown";
    }
}

char alert_notification_default_character( rfscout_notification_type_t a_type )
{
    switch( a_type )
    {
        case RFSCOUT_NOTIFICATION_FLOCK:
            return 'F';
        case RFSCOUT_NOTIFICATION_BODY_CAMERA:
            return 'B';
        case RFSCOUT_NOTIFICATION_DRONE:
            return 'D';
        case RFSCOUT_NOTIFICATION_CAMERA:
            return 'C';
        case RFSCOUT_NOTIFICATION_SMART_GLASSES:
            return 'G';
        case RFSCOUT_NOTIFICATION_ACOUSTIC_SENSOR:
            return 'A';
        case RFSCOUT_NOTIFICATION_SURVEILLANCE_OTHER:
            return 'S';
        case RFSCOUT_NOTIFICATION_CC1101_NARROW_ISM:
            return 'N';
        case RFSCOUT_NOTIFICATION_CC1101_REMOTE_SENSOR:
            return 'R';
        case RFSCOUT_NOTIFICATION_CC1101_CARRIER:
            return 'K';
        case RFSCOUT_NOTIFICATION_CC1101_BURSTY:
            return 'U';
        case RFSCOUT_NOTIFICATION_CC1101_WIDEBAND:
            return 'W';
        case RFSCOUT_NOTIFICATION_NRF24_ESB:
            return 'E';
        case RFSCOUT_NOTIFICATION_NRF24_INPUT:
            return 'I';
        case RFSCOUT_NOTIFICATION_NRF24_SENSOR:
            return 'T';
        case RFSCOUT_NOTIFICATION_NRF24_WIFI_LIKE:
            return 'V';
        case RFSCOUT_NOTIFICATION_NRF24_BLE_LIKE:
            return 'L';
        case RFSCOUT_NOTIFICATION_NRF24_LOGITECH_LIKE:
            return 'X';
        default:
            return ALERT_MORSE_DEFAULT_CHARACTER;
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
    return alert_request_character( alert_morse_character() );
}

bool alert_request_character( char a_character )
{
    if( ( NULL == alert_mutex ) || ( pdTRUE != xSemaphoreTake( alert_mutex, 0U ) ) )
    {
        return false;
    }

    const bool queued = request_locked( a_character );
    (void)xSemaphoreGive( alert_mutex );
    return queued;
}
