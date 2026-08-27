/**
    recorder.h : RFScout SD-backed audio recorder interface
    Description: Records microphone PCM to WAV files and enumerates playable recordings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RECORDER_H_
#define _RFSCOUT_RECORDER_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_RECORDER_DIRECTORY "/sdcard/audio"
#define RFSCOUT_RECORDER_SAMPLE_RATE 16000U
#define RFSCOUT_RECORDER_MAX_FILES 48U
#define RFSCOUT_RECORDER_NAME_MAX 64U

typedef enum
{
    RFSCOUT_RECORDER_IDLE = 0,
    RFSCOUT_RECORDER_RECORDING,
    RFSCOUT_RECORDER_PLAYING,
    RFSCOUT_RECORDER_NO_STORAGE,
    RFSCOUT_RECORDER_ERROR
} rfscout_recorder_state_t;

typedef struct
{
    char name[ RFSCOUT_RECORDER_NAME_MAX ];
    uint32_t duration_seconds;
    uint32_t data_bytes;
} rfscout_recording_t;

/** @brief Initialize recorder state and enumerate valid WAV files when storage is mounted. */
esp_err_t recorder_init( void );

/** @brief Refresh storage state and the playable recording list. */
esp_err_t recorder_refresh( void );

/** @brief Start a new 16 kHz, 16-bit mono PCM WAV recording. */
esp_err_t recorder_start_recording( void );

/** @brief Stop capture, finalize the WAV header, and refresh the recording list. */
void recorder_stop_recording( void );

/** @brief Start asynchronous playback of one enumerated recording. */
esp_err_t recorder_start_playback( size_t a_index, uint8_t a_volume_percent );

/** @brief Stop active recording or playback and leave the service idle. */
void recorder_stop( void );

/** @brief Return current recorder state. */
rfscout_recorder_state_t recorder_state( void );

/** @brief Return the current recording filename, or an empty string when not recording. */
const char *recorder_active_name( void );

/** @brief Return bytes of PCM data written by the active recording. */
uint32_t recorder_recorded_bytes( void );

/** @brief Return the number of validated playable recordings. */
size_t recorder_file_count( void );

/** @brief Return one validated recording entry or NULL for an invalid index. */
const rfscout_recording_t *recorder_file( size_t a_index );

#endif // _RFSCOUT_RECORDER_H_
