/**
    navigation_bar.h : RFScout system-owned global app navigation chrome
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_NAVIGATION_BAR_H_
#define _RFSCOUT_NAVIGATION_BAR_H_

/** @brief Process authoritative touch against system-owned navigation chrome. */
void navigation_bar_poll( void );

/** @brief Draw the system-owned navigation chrome. */
void navigation_bar_draw( void );

#endif // _RFSCOUT_NAVIGATION_BAR_H_
