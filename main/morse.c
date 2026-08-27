/**
    morse.c : RFScout Morse-code implementation
    Description: Provides ITU-R M.1677-1 printable-character patterns and timing playback helpers.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "morse.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stddef.h>

typedef struct
{
    char character;
    const char *pattern;
} morse_entry_t;

static const morse_entry_t entries[] = {
    { 'A', ".-" },      { 'B', "-..." },   { 'C', "-.-." },   { 'D', "-.." },    { 'E', "." },
    { 'F', "..-." },    { 'G', "--." },    { 'H', "...." },   { 'I', ".." },     { 'J', ".---" },
    { 'K', "-.-" },     { 'L', ".-.." },   { 'M', "--" },     { 'N', "-." },     { 'O', "---" },
    { 'P', ".--." },    { 'Q', "--.-" },   { 'R', ".-." },    { 'S', "..." },    { 'T', "-" },
    { 'U', "..-" },     { 'V', "...-" },   { 'W', ".--" },    { 'X', "-..-" },   { 'Y', "-.--" },
    { 'Z', "--.." },    { '0', "-----" },  { '1', ".----" },  { '2', "..---" },  { '3', "...--" },
    { '4', "....-" },   { '5', "....." },  { '6', "-...." },  { '7', "--..." },  { '8', "---.." },
    { '9', "----." },   { '.', ".-.-.-" }, { ',', "--..--" }, { ':', "---..." }, { '?', "..--.." },
    { '\'', ".----." }, { '-', "-....-" }, { '/', "-..-." },  { '(', "-.--." },  { ')', "-.--.-" },
    { '"', ".-..-." },  { '=', "-...-" },  { '+', ".-.-." },  { '@', ".--.-." },
};

static const char supported_characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:?'-/()\"=+@";

const char *morse_pattern( char a_character )
{
    const int upper = toupper( (unsigned char)a_character );
    for( size_t index = 0U; index < sizeof( entries ) / sizeof( entries[ 0 ] ); ++index )
    {
        if( upper == (unsigned char)entries[ index ].character )
        {
            return entries[ index ].pattern;
        }
    }
    return NULL;
}

const char *morse_supported_characters( void )
{
    return supported_characters;
}

void morse_play_letter(
    char a_letter, uint32_t a_unit_ms, uint16_t a_tone_hz, morse_tone_fn a_tone, void *a_context )
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
