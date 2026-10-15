/**
    gps_view.h : GPS diagnostic display contract
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_GPS_VIEW_H_
#define _RFSCOUT_GPS_VIEW_H_

#include <stdbool.h>

/** @brief Render available GPS receiver telemetry without owning the UART provider. */
void gps_view_render( bool a_menu_open, unsigned a_menu_item, bool a_menu_error );

#endif // _RFSCOUT_GPS_VIEW_H_
