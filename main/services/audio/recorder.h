/**
    services/audio/recorder.h : RFScout shared recorder, spectrum, and playback service
    Description: Exposes live PCM processing plus SD-backed WAV recording and playback.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RECORDER_H_
#define _RFSCOUT_RECORDER_H_

#include "audio_capture.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_RECORDER_DIRECTORY "/sdcard/audio"
#define RFSCOUT_RECORDER_SAMPLE_RATE RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE
#define RFSCOUT_RECORDER_TARGET_PDM_CLOCK_HZ 2048000U
#define RFSCOUT_RECORDER_WAV_RATE_MIN 8000U
#define RFSCOUT_RECORDER_WAV_RATE_MAX 48000U
#define RFSCOUT_RECORDER_MAX_FILES 48U
#define RFSCOUT_RECORDER_NAME_MAX 64U
#define RFSCOUT_RECORDER_MONITOR_BINS 64U

typedef enum
{
    RFSCOUT_RECORDER_IDLE = 0,
    RFSCOUT_RECORDER_RECORDING,
    RFSCOUT_RECORDER_SAVING,
    RFSCOUT_RECORDER_MONITORING,
    RFSCOUT_RECORDER_PLAYING,
    RFSCOUT_RECORDER_NO_STORAGE,
    RFSCOUT_RECORDER_ERROR
} rfscout_recorder_state_t;

typedef struct
{
    char name[ RFSCOUT_RECORDER_NAME_MAX ];
    uint32_t duration_seconds;
    uint32_t data_bytes;
    int64_t modified_time;
} rfscout_recording_t;

/** @brief Probe the active board audio-capture provider through the shared capture service. */
esp_err_t recorder_probe_microphone( void );

/** @brief Initialize recorder state and enumerate valid WAV files when storage is mounted. */
esp_err_t recorder_init( void );

/** @brief Refresh storage state and the playable recording list. */
esp_err_t recorder_refresh( void );

/** @brief Start a 16-bit mono PCM WAV recording from the active board audio-capture provider. */
esp_err_t recorder_start_recording( void );

/** @brief Request capture stop and enter asynchronous save/post-processing state. */
void recorder_request_stop_recording( void );

/** @brief Stop capture, wait for save/post-processing, and refresh the recording list. */
void recorder_stop_recording( void );

/** @brief Stop SD-backed recorder activity before storage becomes unavailable. */
bool recorder_prepare_storage_shutdown( uint32_t a_timeout_ms );

/** @brief Delete one enumerated recording and refresh the list. */
esp_err_t recorder_delete_file( size_t a_index );

/** @brief Start raw live microphone spectrum monitoring with automatic display scaling. */
esp_err_t recorder_start_monitor( void );

/** @brief Stop live microphone spectrum monitoring. */
void recorder_stop_monitor( void );

/** @brief Copy the latest live spectrum bins and return the copied count. */
size_t recorder_monitor_bins( uint8_t *a_bins, size_t a_capacity );

/** @brief Return the center frequency of a displayed spectrum bin using measured PCM throughput. */
uint32_t recorder_monitor_bin_frequency_hz( size_t a_bin );

/** @brief Return the current automatic spectrum display span in dB. */
uint8_t recorder_monitor_range_db( void );

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

/** @brief Return wall-clock elapsed seconds for the active recording. */
uint32_t recorder_recording_elapsed_seconds( void );

/** @brief Return smoothed live microphone level from 0 through 100 while recording. */
uint8_t recorder_level_percent( void );

/** @brief Return recent microphone peak level from 0 through 100 while recording. */
uint8_t recorder_peak_percent( void );

/** @brief Return the active input channel/slot name for diagnostics. */
const char *recorder_input_slot_name( void );

/** @brief Change volume of active WAV playback without restarting it. */
void recorder_set_playback_volume( uint8_t a_volume_percent );

/** @brief Return elapsed seconds for the current WAV playback. */
uint32_t recorder_playback_elapsed_seconds( void );

/** @brief Return the number of validated playable recordings. */
size_t recorder_file_count( void );

/** @brief Return one validated recording entry or NULL for an invalid index. */
const rfscout_recording_t *recorder_file( size_t a_index );

#endif // _RFSCOUT_RECORDER_H_
