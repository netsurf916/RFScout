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

/** @brief Acquire and render the thermal view. */
esp_err_t thermal_view_render( void );

void thermal_view_menu( unsigned a_selected, bool a_locked, bool a_keep_awake );

#endif // _RFSCOUT_THERMAL_VIEW_H_
