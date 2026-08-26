/**
    alert.c : RFScout audible alert policy implementation
    Description: Converts runtime alert preferences into asynchronous speaker requests.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "alert.h"

#include "audio_output.h"
#include "morse.h"

#include <ctype.h>

#define ALERT_TONE_FREQUENCY_HZ 900U
#define ALERT_TONE_DURATION_MS 250U
#define ALERT_MORSE_UNIT_MS 100U
#define ALERT_MORSE_DEFAULT_CHARACTER 'S'

static rfscout_alert_mode_t mode = RFSCOUT_ALERT_OFF;
static char morse_character = ALERT_MORSE_DEFAULT_CHARACTER;

esp_err_t alert_init( void )
{
    mode = RFSCOUT_ALERT_OFF;
    morse_character = ALERT_MORSE_DEFAULT_CHARACTER;
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
    return mode;
}

void alert_set_mode( rfscout_alert_mode_t a_mode )
{
    if( a_mode < RFSCOUT_ALERT_MODE_COUNT )
    {
        mode = a_mode;
    }
}

char alert_morse_character( void )
{
    return morse_character;
}

bool alert_morse_character_supported( char a_character )
{
    return NULL != morse_pattern( a_character );
}

bool alert_set_morse_character( char a_character )
{
    if( !alert_morse_character_supported( a_character ) )
    {
        return false;
    }
    morse_character = (char)toupper( (unsigned char)a_character );
    return true;
}

bool alert_request( void )
{
    switch( mode )
    {
        case RFSCOUT_ALERT_OFF:
            return true;

        case RFSCOUT_ALERT_TONE:
            return audio_output_play_tone( ALERT_TONE_FREQUENCY_HZ, ALERT_TONE_DURATION_MS );

        case RFSCOUT_ALERT_MORSE:
            return audio_output_play_morse(
                morse_character, ALERT_TONE_FREQUENCY_HZ, ALERT_MORSE_UNIT_MS );

        default:
            return false;
    }
}
