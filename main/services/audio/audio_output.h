/**
    audio_output.h : RFScout asynchronous speaker output interface
    Description: Arbitrates disposable feedback, notifications, and intentional application audio.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_OUTPUT_H_
#define _RFSCOUT_AUDIO_OUTPUT_H_

#include "media_store.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize the asynchronous speaker output service when the board provides one. */
esp_err_t audio_output_init( void );

/** @brief Return whether the selected board has an initialized speaker output path. */
bool audio_output_available( void );

/** @brief Queue one ordinary generated tone without blocking the caller. */
bool audio_output_play_tone( uint16_t a_frequency_hz,
                             uint32_t a_duration_ms,
                             uint8_t a_volume_percent );

/** @brief Queue one ordinary Morse character without blocking the caller. */
bool audio_output_play_morse( char a_character,
                              uint16_t a_frequency_hz,
                              uint32_t a_unit_ms,
                              uint8_t a_volume_percent );

/**
 * @brief Queue disposable interaction feedback.
 * @details At most one feedback command is pending/active so fast navigation can never build a
 *          stale audible backlog.
 */
bool audio_output_play_feedback_tone( uint16_t a_frequency_hz,
                                      uint32_t a_duration_ms,
                                      uint8_t a_volume_percent );

/** @brief Queue a notification tone ahead of ordinary/feedback commands. */
bool audio_output_play_notification_tone( uint16_t a_frequency_hz,
                                          uint32_t a_duration_ms,
                                          uint8_t a_volume_percent );

/** @brief Queue a notification Morse character ahead of ordinary/feedback commands. */
bool audio_output_play_notification_morse( char a_character,
                                           uint16_t a_frequency_hz,
                                           uint32_t a_unit_ms,
                                           uint8_t a_volume_percent );

/**
 * @brief Queue intentional application PCM WAV playback of one media-store file.
 * @param a_collection Resolved collection; copied, so a captured app collection keeps its session
 *        generation and playback fails cleanly if that session has already closed.
 * @param a_name File name within the collection; copied.
 * @note Content audio is independent from theme/notification policy and remains explicitly usable
 *       in Discipline mode.
 */
bool audio_output_play_wav( const rfscout_media_collection_t *a_collection,
                            const char *a_name,
                            uint8_t a_volume_percent );

/** @brief Change gain for current/queued WAV playback without affecting feedback or notifications. */
void audio_output_set_playback_volume( uint8_t a_volume_percent );

/** @brief Return true while a queued or active WAV playback command exists. */
bool audio_output_playback_active( void );

/** @brief Stop active WAV playback and invalidate queued WAV playback commands. */
void audio_output_stop_playback( void );

/** @brief Remove queued disposable interaction feedback without touching application audio. */
void audio_output_cancel_feedback_pending( void );

/** @brief Remove queued notification audio without touching feedback or application audio. */
void audio_output_cancel_notifications_pending( void );

/**
 * @brief Legacy broad cancellation helper.
 * @deprecated Policy code should cancel its own class rather than clearing unrelated audio.
 */
void audio_output_cancel_pending( void );

#endif // _RFSCOUT_AUDIO_OUTPUT_H_
