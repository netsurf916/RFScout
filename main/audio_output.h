/**
    audio_output.h : RFScout asynchronous speaker output interface
    Description: Queues tones, Morse patterns, and PCM WAV playback for the onboard I2S speaker.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_OUTPUT_H_
#define _RFSCOUT_AUDIO_OUTPUT_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize the asynchronous onboard-speaker output service. */
esp_err_t audio_output_init( void );

/**
 * @brief Queue one tone without blocking the caller.
 * @param a_frequency_hz Tone frequency in hertz.
 * @param a_duration_ms Tone duration in milliseconds.
 * @param a_volume_percent Linear PCM amplitude percentage from 0 through 100.
 * @return True when the request was queued.
 */
bool audio_output_play_tone( uint16_t a_frequency_hz,
                             uint32_t a_duration_ms,
                             uint8_t a_volume_percent );

/**
 * @brief Queue one Morse-code character without blocking the caller.
 * @param a_character Single supported Morse character.
 * @param a_frequency_hz Tone frequency in hertz.
 * @param a_unit_ms Dot duration in milliseconds.
 * @param a_volume_percent Linear PCM amplitude percentage from 0 through 100.
 * @return True when the request was queued.
 */
bool audio_output_play_morse( char a_character,
                              uint16_t a_frequency_hz,
                              uint32_t a_unit_ms,
                              uint8_t a_volume_percent );

/**
 * @brief Queue playback of one RFScout-format PCM WAV file.
 * @param a_path Absolute filesystem path to a supported runtime-calibrated 16-bit mono PCM WAV file.
 * @param a_volume_percent Linear PCM amplitude percentage from 0 through 100.
 * @return True when the playback request was queued.
 * @note The worker opens and streams the file asynchronously; callers may return immediately.
 */
bool audio_output_play_wav( const char *a_path, uint8_t a_volume_percent );

/** @brief Change gain for current/queued WAV playback without affecting alerts. */
void audio_output_set_playback_volume( uint8_t a_volume_percent );

/** @brief Return true while a queued or active WAV playback command exists. */
bool audio_output_playback_active( void );

/** @brief Stop active WAV playback and remove queued WAV playback commands. */
void audio_output_stop_playback( void );

/**
 * @brief Drop speaker commands that are queued but have not started playing yet.
 * @note A tone or Morse command already executing on the output worker is allowed to finish.
 */
void audio_output_cancel_pending( void );

#endif // _RFSCOUT_AUDIO_OUTPUT_H_
