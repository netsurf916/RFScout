/**
    apps/ota/ota_menu.h : Home Update menu controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_MENU_H_
#define _RFSCOUT_OTA_MENU_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Enter the Home-owned Update menu and start passive network discovery. */
void ota_menu_enter( void );

/** @brief Tear down Update acquisition/install/network state when leaving the menu. */
void ota_menu_leave( void );

/** @brief Handle Home-menu select/rotation input; true requests return to Device settings. */
bool ota_menu_input( bool a_pressed, int a_delta );

/** @brief Handle Back within the Update menu; true requests return to Device settings. */
bool ota_menu_back( void );

/** @brief Poll asynchronous Update operations while the Home Update menu is open. */
void ota_menu_poll( int64_t a_now_us );

#endif // _RFSCOUT_OTA_MENU_H_
