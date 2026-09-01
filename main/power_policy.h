/**
    power_policy.h : RFScout protective power-state policy
    Description: Monitors battery and internal temperature conditions and requests protective state changes.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_POWER_POLICY_H_
#define _RFSCOUT_POWER_POLICY_H_

#include "esp_err.h"

/** @brief Initialize battery and internal-temperature protection monitoring. */
esp_err_t power_policy_init( void );

/** @brief Poll configured protection conditions and manage pending protective transitions. */
void power_policy_poll( void );

#endif // _RFSCOUT_POWER_POLICY_H_
