/**
    audio_spectrum.h : PDM microphone spectrum analyzer
    Description: Declares DMA-fed audio acquisition, FFT range control, and spectrum frame access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_SPECTRUM_H_
#define _RFSCOUT_AUDIO_SPECTRUM_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_SPECTRUM_BINS 160U

/** @brief Selectable audio spectrum frequency ranges. */
typedef enum
{
    AUDIO_RANGE_VOICE = 0,
    AUDIO_RANGE_MID,
    AUDIO_RANGE_HIGH,
    AUDIO_RANGE_AUDIBLE,
    AUDIO_RANGE_ULTRASONIC,
    AUDIO_RANGE_COUNT
} audio_range_t;

/** @brief PDM receive slot exposed as a user-selectable microphone channel. */
typedef enum
{
    AUDIO_CHANNEL_LEFT = 0,
    AUDIO_CHANNEL_RIGHT,
    AUDIO_CHANNEL_COUNT
} audio_channel_t;

/** @brief Initialize and start DMA-fed PDM microphone acquisition. */
esp_err_t audio_spectrum_start( void );

/** @brief Stop microphone acquisition and release the active I2S channel. */
void audio_spectrum_stop( void );

/** @brief Return true while the audio acquisition worker is active. */
bool audio_spectrum_is_running( void );

/**
 * @brief Consume the most recently published spectrum frame.
 * @param a_current Destination for current normalized spectrum bins.
 * @param a_peak Destination for slow-decay peak-hold bins.
 * @param a_count Capacity of each destination array.
 * @return True when a new frame was consumed; false when no new frame is ready.
 */
bool audio_spectrum_take_frame( uint8_t *a_current,
                                uint8_t *a_peak,
                                size_t a_count );

/** @brief Select the active audio frequency range and reset accumulated display state. */
void audio_spectrum_set_range( audio_range_t a_range );

/** @brief Return the currently selected audio frequency range. */
audio_range_t audio_spectrum_range( void );

/** @brief Return a stable display name for an audio frequency range. */
const char *audio_spectrum_range_name( audio_range_t a_range );

/** @brief Select the PDM receive slot used for FFT input. */
void audio_spectrum_set_channel( audio_channel_t a_channel );

/** @brief Return the currently selected PDM receive slot. */
audio_channel_t audio_spectrum_channel( void );

/** @brief Return a stable display name for a PDM receive slot. */
const char *audio_spectrum_channel_name( audio_channel_t a_channel );

/** @brief Return the lower bound of the active display range in hertz. */
uint32_t audio_spectrum_low_hz( void );

/** @brief Return the upper bound of the active display range in hertz. */
uint32_t audio_spectrum_high_hz( void );

/** @brief Return the effective sample rate after range-specific decimation. */
uint32_t audio_spectrum_effective_sample_rate( void );

/** @brief Return the number of real display bins populated for the active range. */
size_t audio_spectrum_display_bin_count( void );

/** @brief Return the current automatic display floor in dBFS. */
int audio_spectrum_scale_floor_db( void );

/** @brief Return the current automatic display ceiling in dBFS. */
int audio_spectrum_scale_ceiling_db( void );

/** @brief Return the number of unpublished FFT frames overwritten by newer data. */
uint32_t audio_spectrum_overruns( void );

#endif // _RFSCOUT_AUDIO_SPECTRUM_H_
