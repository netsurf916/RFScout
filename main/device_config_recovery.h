/**
    device_config_recovery.h : RFScout configuration downgrade recovery
    Description: Restores validated newer-schema configuration quarantined by older firmware.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_CONFIG_RECOVERY_H_
#define _RFSCOUT_DEVICE_CONFIG_RECOVERY_H_

#include "esp_err.h"

/** @brief Restore a validated schema-v3 configuration quarantined by older firmware when appropriate. */
esp_err_t device_config_recover_downgrade( void );

#endif // _RFSCOUT_DEVICE_CONFIG_RECOVERY_H_
