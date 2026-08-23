/**
    morse.c : RFScout Morse-code alert implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "morse.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>

static const char *patterns[ 26 ] =
{
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
    "..-", "...-", ".--", "-..-", "-.--", "--.."
};

const char *morse_pattern( char a_letter )
{
    const int character = toupper( (unsigned char)a_letter );
    if( ( character < 'A' ) || ( character > 'Z' ) )
    {
        return NULL;
    }
    return patterns[ character - 'A' ];
}

void morse_play_letter( char a_letter, uint32_t a_unit_ms, uint16_t a_tone_hz,
                        morse_tone_fn a_tone, void *a_context )
{
    const char *pattern = morse_pattern( a_letter );
    if( ( NULL == pattern ) || ( NULL == a_tone ) || ( 0U == a_unit_ms ) )
    {
        return;
    }

    for( const char *symbol = pattern; '\0' != *symbol; ++symbol )
    {
        const uint32_t on_units = ( '-' == *symbol ) ? 3U : 1U;
        a_tone( true, a_tone_hz, a_context );
        vTaskDelay( pdMS_TO_TICKS( on_units * a_unit_ms ) );
        a_tone( false, a_tone_hz, a_context );
        if( '\0' != symbol[ 1 ] )
        {
            vTaskDelay( pdMS_TO_TICKS( a_unit_ms ) );
        }
    }
    vTaskDelay( pdMS_TO_TICKS( 3U * a_unit_ms ) );
}
