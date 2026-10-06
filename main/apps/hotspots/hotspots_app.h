/**
    apps/hotspots/hotspots_app.h : Hotspots application telemetry interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_HOTSPOTS_APP_H_
#define _RFSCOUT_HOTSPOTS_APP_H_

#include <stddef.h>

/** @brief Aggregate counts of the current Hotspots session for periodic telemetry. */
typedef struct
{
    size_t visible;
    size_t green;
    size_t yellow;
    size_t red;
    size_t checks;
} hotspots_app_counts_t;

/** @brief Copy the current session counts; all zero when the app is not active. */
void hotspots_app_counts( hotspots_app_counts_t *a_counts );

#endif // _RFSCOUT_HOTSPOTS_APP_H_
