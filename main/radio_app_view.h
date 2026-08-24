/**
    radio_app_view.h : RFScout placeholder radio application views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RADIO_APP_VIEW_H_
#define _RFSCOUT_RADIO_APP_VIEW_H_

#include <stdbool.h>

/** @brief Render the CC1101 placeholder view and optional settings panel. */
void radio_app_view_cc1101( bool a_settings_open );

/** @brief Render the nRF24 placeholder view. */
void radio_app_view_nrf24( void );

#endif // _RFSCOUT_RADIO_APP_VIEW_H_
