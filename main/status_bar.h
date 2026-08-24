/**
    status_bar.h : RFScout top status bar interface
    Description: Draws compact right-aligned system-state icons between the title and battery.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STATUS_BAR_H_
#define _RFSCOUT_STATUS_BAR_H_

/**
 * @brief Draw the current system-status icons into the LCD framebuffer.
 * @details Icons populate from right to left. Subsystems remain responsible for
 *          their own state; the status bar only queries and renders it.
 */
void status_bar_draw( void );

#endif // _RFSCOUT_STATUS_BAR_H_
