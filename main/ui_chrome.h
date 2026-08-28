/**
    ui_chrome.h : RFScout shared application chrome interface
    Description: Draws the standard title, global status bar, and battery indicator.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_UI_CHROME_H_
#define _RFSCOUT_UI_CHROME_H_

#include <stdint.h>

/**
 * @brief Draw the standard RFScout top chrome into the current framebuffer.
 * @param a_title Application/view title shown at the shared title origin.
 * @param a_title_color RGB565 title color chosen by the caller's semantic palette.
 * @details The chrome owns title placement, clock/system-status placement, and battery placement.
 *          Application content below the header remains entirely caller-owned; no footer area is
 *          reserved by this interface.
 */
void ui_chrome_draw( const char *a_title, uint16_t a_title_color );

#endif // _RFSCOUT_UI_CHROME_H_
