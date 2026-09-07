/**
    input.h : RFScout semantic input service
    Description: Normalizes board controls into focused application input while preserving wake activity.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_INPUT_H_
#define _RFSCOUT_INPUT_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RFSCOUT_INPUT_NONE = 0,
    RFSCOUT_INPUT_NAVIGATION,
    RFSCOUT_INPUT_SELECT,
    RFSCOUT_INPUT_LONG_SELECT,
    RFSCOUT_INPUT_BACK,
    RFSCOUT_INPUT_CHARACTER
} rfscout_input_type_t;

typedef struct
{
    rfscout_input_type_t type;
    int32_t value;
} rfscout_input_event_t;

typedef enum
{
    RFSCOUT_INPUT_PROVIDER_NONE = 0,
    RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS,
    RFSCOUT_INPUT_PROVIDER_TRACKBALL_BUTTONS
} rfscout_input_provider_t;

esp_err_t input_init( void );
void input_poll( void );
bool input_activity_pending( void );
void input_consume_wake( void );
bool input_get( rfscout_input_event_t *a_event );
bool input_locked( void );
rfscout_input_provider_t input_provider( void );

#endif // _RFSCOUT_INPUT_H_
