/**
    thermal_view.h : RFScout thermal application view
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_THERMAL_VIEW_H_
#define _RFSCOUT_THERMAL_VIEW_H_

#include "esp_err.h"
#include <stdbool.h>

/** @brief Enter the thermal view and probe the optional MLX90640. */
void thermal_view_enter( void );

/** @brief Clear the cached frame on session entry. */
void thermal_view_reset( void );
/** @brief Acquire and cache the next complete sensor frame without drawing pixels. */
esp_err_t thermal_view_sample( void );
/** @brief Render the latest sampled frame (or initial waiting state). */
esp_err_t thermal_view_render( void );

void thermal_view_menu( unsigned a_selected, bool a_locked, bool a_keep_awake );

#endif // _RFSCOUT_THERMAL_VIEW_H_
