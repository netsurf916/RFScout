/**
    touch.h : RFScout board-neutral touch service
    Description: Probes and samples the selected board touch controller without making raw touch authoritative.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_TOUCH_H_
#define _RFSCOUT_TOUCH_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool pressed;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t reference_x;
    uint16_t reference_y;
    uint8_t point_count;
    uint32_t sequence;
} rfscout_touch_sample_t;

esp_err_t touch_init( void );
void touch_poll( void );
bool touch_available( void );
bool touch_sample_get( rfscout_touch_sample_t *a_sample );

#endif // _RFSCOUT_TOUCH_H_
