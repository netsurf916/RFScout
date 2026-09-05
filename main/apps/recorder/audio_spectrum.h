/**
    audio_spectrum.h : Recorder compatibility adapter for the former Audio app
    Description: Preserves the app-manager Audio lifecycle ABI while the UI is replaced by Recorder.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_SPECTRUM_H_
#define _RFSCOUT_AUDIO_SPECTRUM_H_

#include "esp_err.h"
#include "recorder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The app manager historically owns the Audio menu through this interface. Keep these types and
 * entry points until the broader application registry is renamed, but do not add analyzer/FFT
 * behavior here. Recorder is the implementation and source of truth.
 */
#define AUDIO_SPECTRUM_BINS 1U
#define AUDIO_CHANNEL_COUNT RFSCOUT_RECORDER_MAX_FILES

typedef enum
{
    AUDIO_RANGE_RECORDER = 0,
    AUDIO_RANGE_COUNT
} audio_range_t;

typedef size_t audio_channel_t;

/** @brief Initialize Recorder state for the former Audio application slot. */
esp_err_t audio_spectrum_start( void );

/** @brief Stop active recording/playback when leaving the application. */
void audio_spectrum_stop( void );

/** @brief Return true while Recorder has mounted storage and can operate. */
bool audio_spectrum_is_running( void );

/** @brief Compatibility no-op; Recorder does not publish FFT frames. */
bool audio_spectrum_take_frame( uint8_t *a_current, uint8_t *a_peak, size_t a_count );

/** @brief Compatibility no-op retained for the legacy menu controller. */
void audio_spectrum_set_range( audio_range_t a_range );

/** @brief Return the single Recorder pseudo-range. */
audio_range_t audio_spectrum_range( void );

/** @brief Return the Recorder display label. */
const char *audio_spectrum_range_name( audio_range_t a_range );

/** @brief Select one validated recording by index. */
void audio_spectrum_set_channel( audio_channel_t a_channel );

/** @brief Return the selected recording index. */
audio_channel_t audio_spectrum_channel( void );

/** @brief Return the selected recording filename or an empty-list label. */
const char *audio_spectrum_channel_name( audio_channel_t a_channel );

/** @brief Recorder format sample rate. */
uint32_t audio_spectrum_effective_sample_rate( void );

/** @brief Compatibility values retained until Audio naming is removed from app_manager. */
uint32_t audio_spectrum_low_hz( void );
uint32_t audio_spectrum_high_hz( void );
size_t audio_spectrum_display_bin_count( void );
int audio_spectrum_scale_floor_db( void );
int audio_spectrum_scale_ceiling_db( void );
uint32_t audio_spectrum_overruns( void );

#endif // _RFSCOUT_AUDIO_SPECTRUM_H_
