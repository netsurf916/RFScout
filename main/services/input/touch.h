/**
    touch.h : RFScout board-neutral touch service
    Description: Probes, calibrates, and samples the selected board touch controller.
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
    bool calibrated;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t reference_x;
    uint16_t reference_y;
    uint16_t x;
    uint16_t y;
    uint8_t point_count;
    uint32_t sequence;
} rfscout_touch_sample_t;

esp_err_t touch_init( void );
/** @brief Poll touch state and return true when presentation-visible touch state changed. */
bool touch_poll( void );
bool touch_available( void );
bool touch_sample_get( rfscout_touch_sample_t *a_sample );

/** @brief Return whether a compatible calibration record is loaded and authoritative. */
bool touch_calibration_valid( void );
/** @brief Return whether startup/manual calibration is currently capturing points. */
bool touch_calibration_active( void );
/** @brief Start or restart calibration; the existing record remains on flash until replacement succeeds. */
esp_err_t touch_calibration_begin( void );
/** @brief Return the current calibration target label, or NULL when calibration is inactive. */
const char *touch_calibration_prompt( void );
/** @brief Return one-based calibration step and total step count for UI presentation. */
void touch_calibration_progress( unsigned *a_step, unsigned *a_total );

#endif // _RFSCOUT_TOUCH_H_
