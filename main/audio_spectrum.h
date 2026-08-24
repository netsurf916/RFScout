/**
 * audio_spectrum.h : PDM microphone spectrum analyzer
 * Copyright 2026 Daniel Wilson
 * SPDX-License-Identifier: MIT
 */
#pragma once
#ifndef _RFSCOUT_AUDIO_SPECTRUM_H_
#define _RFSCOUT_AUDIO_SPECTRUM_H_
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_SPECTRUM_BINS 160U

typedef enum
{
    AUDIO_RANGE_INFRASONIC = 0,
    AUDIO_RANGE_AUDIBLE,
    AUDIO_RANGE_ULTRASONIC,
    AUDIO_RANGE_COUNT
} audio_range_t;

esp_err_t audio_spectrum_start( void );
void audio_spectrum_stop( void );
bool audio_spectrum_is_running( void );
bool audio_spectrum_take_frame( uint8_t *a_current, uint8_t *a_peak, size_t a_count );
void audio_spectrum_set_range( audio_range_t a_range );
audio_range_t audio_spectrum_range( void );
const char *audio_spectrum_range_name( audio_range_t a_range );
uint32_t audio_spectrum_low_hz( void );
uint32_t audio_spectrum_high_hz( void );
uint32_t audio_spectrum_overruns( void );
#endif
