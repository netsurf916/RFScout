/**
    ota_boot.h : RFScout OTA first-boot confirmation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_BOOT_H_
#define _RFSCOUT_OTA_BOOT_H_

#include "esp_err.h"

/**
 * @brief Confirm the running image if ESP-IDF marks it pending verification.
 * @details Call only after bounded mandatory RFScout startup checks have succeeded.
 */
esp_err_t ota_boot_confirm_if_pending( void );

/**
 * @brief Roll back the running image if ESP-IDF marks it pending verification.
 * @details On a pending image, a successful call reboots into the previous valid slot.
 */
esp_err_t ota_boot_rollback_if_pending( void );

#endif // _RFSCOUT_OTA_BOOT_H_
