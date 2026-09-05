/**
    platform.h : RFScout board-level services
    Description: Exposes shared non-input board services used by the application.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_PLATFORM_H_
#define _RFSCOUT_PLATFORM_H_

#include "input.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize shared board GPIO and peripherals not owned by a dedicated service. */
esp_err_t platform_init( void );

/** @brief Set the visual surveillance-alert state. */
void platform_set_alert_led( bool a_active );

/** @brief Enable or disable the requested audio tone. */
void platform_audio_tone( bool a_enabled, uint16_t a_frequency_hz, void *a_context );

/* Power-state persistence still includes platform.h; keep this source-compatible alias until that
   service is next touched, without retaining a duplicate implementation or state owner. */
#define platform_input_locked input_locked

#endif // _RFSCOUT_PLATFORM_H_
