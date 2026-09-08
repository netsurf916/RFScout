/**
    audio_capture.h : RFScout board-neutral audio capture service
    Description: Owns capture-provider selection, probing, transport lifecycle, and PCM reads.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_CAPTURE_H_
#define _RFSCOUT_AUDIO_CAPTURE_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE 48000U
#define RFSCOUT_AUDIO_CAPTURE_CHANNELS 1U
#define RFSCOUT_AUDIO_CAPTURE_BITS_PER_SAMPLE 16U

typedef enum
{
    RFSCOUT_AUDIO_CAPTURE_PROVIDER_NONE = 0,
    RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM,
    RFSCOUT_AUDIO_CAPTURE_PROVIDER_ES7210,
} rfscout_audio_capture_provider_t;

typedef struct
{
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t bits_per_sample;
} rfscout_audio_capture_format_t;

/** @brief Return whether the active board profile legally advertises an audio-capture provider. */
bool audio_capture_supported( void );

/**
 * @brief Probe the profile-selected capture device without bypassing profile legality.
 * @return ESP_OK when the expected device is present, or the provider-specific probe error.
 * @note Fixed I2C codecs use a non-invasive bus probe. The PDM baseline retains its existing
 *       clock/data activity probe because that device has no independent control interface.
 */
esp_err_t audio_capture_probe( void );

/**
 * @brief Return whether opening the selected provider temporarily consumes physical Select.
 * @note Consumers that require Select while capture is active must provide another semantic input
 *       path before starting capture.
 */
bool audio_capture_primary_select_conflict( void );

/**
 * @brief Open the profile-selected provider and make normalized mono PCM available for reads.
 * @param a_format Optional output receiving the nominal software PCM format.
 * @return ESP_OK only after the provider transport is fully configured and running.
 */
esp_err_t audio_capture_open( rfscout_audio_capture_format_t *a_format );

/**
 * @brief Read normalized PCM from the active provider.
 * @param a_buffer Destination owned by the caller.
 * @param a_capacity Destination capacity in bytes.
 * @param a_bytes_read Receives bytes delivered.
 * @param a_timeout_ms Maximum blocking time in milliseconds.
 */
esp_err_t audio_capture_read( void *a_buffer,
                              size_t a_capacity,
                              size_t *a_bytes_read,
                              uint32_t a_timeout_ms );

/** @brief Synchronously stop and release the active provider and all transport resources. */
void audio_capture_close( void );

/** @brief Return whether a capture provider is currently open. */
bool audio_capture_is_open( void );

/** @brief Return the active provider identity for provider-specific diagnostics only. */
rfscout_audio_capture_provider_t audio_capture_provider( void );

/** @brief Return a short normalized input-channel label for diagnostics. */
const char *audio_capture_input_name( void );

/** @brief Return the measured PDM clock for diagnostics, or zero for non-PDM/unknown. */
uint32_t audio_capture_pdm_clock_hz( void );

#endif // _RFSCOUT_AUDIO_CAPTURE_H_
