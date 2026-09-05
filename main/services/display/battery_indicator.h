/**
    battery_indicator.h : RFScout battery status widget
    Description: Draws the compact top-right battery indicator.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BATTERY_INDICATOR_H_
#define _RFSCOUT_BATTERY_INDICATOR_H_

/**
 * @brief Draw the current cached battery status into the LCD framebuffer.
 * @details The widget renders percentage, proportional fill when not charging, urgency color,
 *          and a highlighted charging bolt without performing I2C operations.
 */
void battery_indicator_draw( void );

/** @brief Redraw the battery widget only when its rendered state has changed. */
void battery_indicator_poll( void );

#endif // _RFSCOUT_BATTERY_INDICATOR_H_
