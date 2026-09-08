/**
    es7210_input.h : RFScout T-Deck ES7210 microphone provider
    Description: Owns codec control plus the T-Deck I2S0 capture transport behind audio_capture.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ES7210_INPUT_H_
#define _RFSCOUT_ES7210_INPUT_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Non-invasively probe the fixed onboard ES7210 on the shared board I2C bus. */
esp_err_t es7210_input_probe( void );

/** @brief Configure and start the T-Deck ES7210 codec and RFScout-owned I2S0 RX transport. */
esp_err_t es7210_input_open( void );

/** @brief Read normalized mono PCM from the active ES7210 provider. */
esp_err_t es7210_input_read( void *a_buffer,
                             size_t a_capacity,
                             size_t *a_bytes_read,
                             uint32_t a_timeout_ms );

/** @brief Stop and release codec/I2S resources and restore the primary Select input. */
void es7210_input_close( void );

/** @brief Return whether the ES7210 provider currently owns its capture transport. */
bool es7210_input_is_open( void );

#endif // _RFSCOUT_ES7210_INPUT_H_
