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
    AUDIO_RANGE_VOICE = 0,
    AUDIO_RANGE_MID,
    AUDIO_RANGE_HIGH,
    AUDIO_RANGE_AUDIBLE,
    AUDIO_RANGE_ULTRASONIC,
    AUDIO_RANGE_COUNT
} audio_range_t;

typedef enum
{
    AUDIO_CHANNEL_LEFT = 0,
    AUDIO_CHANNEL_RIGHT,
    AUDIO_CHANNEL_COUNT
} audio_channel_t;

esp_err_t audio_spectrum_start( void );
void audio_spectrum_stop( void );
bool audio_spectrum_is_running( void );
bool audio_spectrum_take_frame( uint8_t *a_current, uint8_t *a_peak, size_t a_count );
void audio_spectrum_set_range( audio_range_t a_range );
audio_range_t audio_spectrum_range( void );
const char *audio_spectrum_range_name( audio_range_t a_range );
void audio_spectrum_set_channel( audio_channel_t a_channel );
audio_channel_t audio_spectrum_channel( void );
const char *audio_spectrum_channel_name( audio_channel_t a_channel );
uint32_t audio_spectrum_low_hz( void );
uint32_t audio_spectrum_high_hz( void );
uint32_t audio_spectrum_effective_sample_rate( void );
size_t audio_spectrum_display_bin_count( void );
int audio_spectrum_scale_floor_db( void );
int audio_spectrum_scale_ceiling_db( void );
uint32_t audio_spectrum_overruns( void );
#endif
