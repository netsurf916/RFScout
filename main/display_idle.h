/**
    display_idle.h : RFScout global display-idle policy
    Description: Dims or disables LCD output after inactivity while keeping applications running.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_IDLE_H_
#define _RFSCOUT_DISPLAY_IDLE_H_

#include "esp_err.h"

/** @brief Initialize the global display-idle state from current configuration. */
esp_err_t display_idle_init( void );

/** @brief Poll user activity and apply dim/off/wake transitions. */
void display_idle_poll( void );

#endif // _RFSCOUT_DISPLAY_IDLE_H_
