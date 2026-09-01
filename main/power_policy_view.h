/**
    power_policy_view.h : RFScout protective power policy settings view
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_POWER_POLICY_VIEW_H_
#define _RFSCOUT_POWER_POLICY_VIEW_H_

#include <stdbool.h>

/** @brief Reset selection and render the protective power policy submenu. */
void power_policy_view_enter( void );

/** @brief Render the protective power policy submenu. */
void power_policy_view_render( void );

/**
 * @brief Handle semantic menu input.
 * @return True when the Back row was activated and Device Settings should resume.
 */
bool power_policy_view_input( bool a_pressed, int a_delta );

#endif // _RFSCOUT_POWER_POLICY_VIEW_H_
