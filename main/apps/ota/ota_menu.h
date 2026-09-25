/**
    apps/ota/ota_menu.h : Home Update menu controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_MENU_H_
#define _RFSCOUT_OTA_MENU_H_

#include "input.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Enter the Home-owned Network menu and start passive discovery when disconnected. */
void network_menu_enter( void );
/** @brief Leave the Network menu while preserving the Home-owned station connection. */
void network_menu_leave( void );
/** @brief Handle Network-menu select/rotation input; true requests return to Device settings. */
bool network_menu_input( const rfscout_input_event_t *a_input );
/** @brief Poll asynchronous Network state while the Home Network menu is open. */
void network_menu_poll( int64_t a_now_us );

/** @brief Enter the Home-owned Update menu using the existing Home network session. */
void ota_menu_enter( void );

/**
 * @brief Tear down Update acquisition/install state when leaving the menu.
 * @return ESP_OK only after acquisition is quiesced and installer teardown is complete.
 */
esp_err_t ota_menu_leave( void );

/** @brief Handle Home-menu select/rotation input; true requests return to Device settings. */
bool ota_menu_input( const rfscout_input_event_t *a_input );

/** @brief Poll asynchronous Update operations while the Home Update menu is open. */
void ota_menu_poll( int64_t a_now_us );

#endif // _RFSCOUT_OTA_MENU_H_
