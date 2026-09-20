/**
    app_text_entry.c : Shared rotary text-entry controller
    Description: Provides printable-ASCII entry without binding applications to physical controls.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_text_entry.h"

#include <string.h>

#define TEXT_ENTRY_FIRST_CHARACTER 32
#define TEXT_ENTRY_LAST_CHARACTER 126
#define TEXT_ENTRY_CHARACTER_COUNT ( TEXT_ENTRY_LAST_CHARACTER - TEXT_ENTRY_FIRST_CHARACTER + 1 )
#define TEXT_ENTRY_DEFAULT_CHARACTER 'a'

void app_text_entry_begin( rfscout_text_entry_t *a_entry,
                           const char *a_initial,
                           size_t a_max_length )
{
    if( NULL == a_entry )
    {
        return;
    }

    memset( a_entry, 0, sizeof( *a_entry ) );
    a_entry->max_length =
        a_max_length < RFSCOUT_TEXT_ENTRY_MAX_LENGTH ? a_max_length : RFSCOUT_TEXT_ENTRY_MAX_LENGTH;
    if( NULL != a_initial )
    {
        a_entry->length = strnlen( a_initial, a_entry->max_length );
        memcpy( a_entry->value, a_initial, a_entry->length );
        a_entry->value[ a_entry->length ] = '\0';
    }
    a_entry->character_index =
        (unsigned)( TEXT_ENTRY_DEFAULT_CHARACTER - TEXT_ENTRY_FIRST_CHARACTER );
    a_entry->active = true;
}

void app_text_entry_clear( rfscout_text_entry_t *a_entry )
{
    if( NULL != a_entry )
    {
        memset( a_entry, 0, sizeof( *a_entry ) );
    }
}

char app_text_entry_character( const rfscout_text_entry_t *a_entry )
{
    if( NULL == a_entry || !a_entry->active )
    {
        return '\0';
    }
    return (char)( TEXT_ENTRY_FIRST_CHARACTER + (int)a_entry->character_index );
}

static void append_character( rfscout_text_entry_t *a_entry, char a_character )
{
    if( a_entry->length >= a_entry->max_length )
    {
        return;
    }
    a_entry->value[ a_entry->length++ ] = a_character;
    a_entry->value[ a_entry->length ] = '\0';
}

rfscout_text_entry_result_t app_text_entry_input( rfscout_text_entry_t *a_entry,
                                                  const rfscout_input_event_t *a_input )
{
    if( NULL == a_entry || NULL == a_input || !a_entry->active )
    {
        return RFSCOUT_TEXT_ENTRY_CONTINUE;
    }

    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        const int direction = a_input->value > 0 ? 1 : -1;
        int index = (int)a_entry->character_index + direction;
        if( index < 0 )
        {
            index = TEXT_ENTRY_CHARACTER_COUNT - 1;
        }
        else if( index >= TEXT_ENTRY_CHARACTER_COUNT )
        {
            index = 0;
        }
        a_entry->character_index = (unsigned)index;
        return RFSCOUT_TEXT_ENTRY_CONTINUE;
    }

    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        append_character( a_entry, app_text_entry_character( a_entry ) );
        return RFSCOUT_TEXT_ENTRY_CONTINUE;
    }

    if( RFSCOUT_INPUT_CHARACTER == a_input->type && a_input->value >= TEXT_ENTRY_FIRST_CHARACTER &&
        a_input->value <= TEXT_ENTRY_LAST_CHARACTER )
    {
        append_character( a_entry, (char)a_input->value );
        return RFSCOUT_TEXT_ENTRY_CONTINUE;
    }

    if( RFSCOUT_INPUT_BACK == a_input->type )
    {
        if( a_entry->length > 0U )
        {
            a_entry->value[ --a_entry->length ] = '\0';
            return RFSCOUT_TEXT_ENTRY_CONTINUE;
        }
        a_entry->active = false;
        return RFSCOUT_TEXT_ENTRY_CANCELLED;
    }

    if( RFSCOUT_INPUT_LONG_SELECT == a_input->type )
    {
        a_entry->active = false;
        return RFSCOUT_TEXT_ENTRY_ACCEPTED;
    }

    return RFSCOUT_TEXT_ENTRY_CONTINUE;
}
