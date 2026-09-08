/**
    es7210_input.h : RFScout T-Deck ES7210 microphone provider
    Description: Configures the onboard codec behind the existing recorder capture service.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ES7210_INPUT_H_
#define _RFSCOUT_ES7210_INPUT_H_

#include "driver/i2s_std.h"
#include "esp_err.h"

/** @brief Configure and start the T-Deck ES7210 capture path on I2S0. */
esp_err_t es7210_input_open( i2s_chan_handle_t *a_channel );

/** @brief Stop and release the ES7210 codec/I2S resources and restore the primary Select input. */
void es7210_input_close( void );

#endif // _RFSCOUT_ES7210_INPUT_H_
