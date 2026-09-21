/**
    app_text_entry.h : Shared rotary text-entry controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_TEXT_ENTRY_H_
#define _RFSCOUT_APP_TEXT_ENTRY_H_

#include "input.h"

#include <stdbool.h>
#include <stddef.h>

#define RFSCOUT_TEXT_ENTRY_MAX_LENGTH 64U

typedef enum
{
    RFSCOUT_TEXT_ENTRY_SELECTION_CHARACTER = 0,
    RFSCOUT_TEXT_ENTRY_SELECTION_DONE,
    RFSCOUT_TEXT_ENTRY_SELECTION_BACKSPACE
} rfscout_text_entry_selection_t;

typedef struct
{
    char value[ RFSCOUT_TEXT_ENTRY_MAX_LENGTH + 1U ];
    size_t length;
    size_t max_length;
    unsigned selection_index;
    bool active;
} rfscout_text_entry_t;

typedef enum
{
    RFSCOUT_TEXT_ENTRY_CONTINUE = 0,
    RFSCOUT_TEXT_ENTRY_ACCEPTED,
    RFSCOUT_TEXT_ENTRY_CANCELLED
} rfscout_text_entry_result_t;

void app_text_entry_begin( rfscout_text_entry_t *a_entry,
                           const char *a_initial,
                           size_t a_max_length );
void app_text_entry_clear( rfscout_text_entry_t *a_entry );
rfscout_text_entry_selection_t app_text_entry_selection( const rfscout_text_entry_t *a_entry );
char app_text_entry_character( const rfscout_text_entry_t *a_entry );
rfscout_text_entry_result_t app_text_entry_input( rfscout_text_entry_t *a_entry,
                                                  const rfscout_input_event_t *a_input );

#endif // _RFSCOUT_APP_TEXT_ENTRY_H_
