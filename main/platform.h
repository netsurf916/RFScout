/**
    platform.h : RFScout board-level services
    Description: Exposes non-radio hardware services used by the application.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_PLATFORM_H_
#define _RFSCOUT_PLATFORM_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize shared board GPIO and peripherals. */
esp_err_t platform_init( void );

/** @brief Poll low-rate board inputs and housekeeping. */
void platform_poll( void );

/**
 * @brief Consume accumulated encoder movement.
 * @return Negative for counter-clockwise, positive for clockwise, or zero.
 */
int platform_encoder_delta( void );

/** @brief Set the visual surveillance-alert state. */
void platform_set_alert_led( bool a_active );

/** @brief Enable or disable the requested audio tone. */
void platform_audio_tone( bool a_enabled, uint16_t a_frequency_hz, void *a_context );

#endif // _RFSCOUT_PLATFORM_H_
