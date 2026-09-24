/**
    cc1101_view.h : RFScout CC1101 discovery and receive views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CC1101_VIEW_H_
#define _RFSCOUT_CC1101_VIEW_H_

#include "cc1101.h"
#include "subghz_decode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    CC1101_VIEW_DECODED = 0,
    CC1101_VIEW_TIMING,
    CC1101_VIEW_WAVEFORM,
    CC1101_VIEW_COUNT
} cc1101_view_mode_t;

void cc1101_view_discovery( const int8_t *a_rssi,
                            size_t a_count,
                            const cc1101_scan_config_t *a_config,
                            uint32_t a_peak_khz,
                            int8_t a_peak_rssi,
                            bool a_detected );

void cc1101_view_receive( cc1101_view_mode_t a_view,
                          const cc1101_receive_config_t *a_config,
                          const rfscout_subghz_analysis_t *a_analysis,
                          const cc1101_capture_stats_t *a_stats,
                          const rfscout_subghz_symbol_t *a_raw_symbols,
                          size_t a_raw_count,
                          uint32_t a_raw_resolution_hz );

void cc1101_view_menu( unsigned a_selected,
                       bool a_editing,
                       bool a_limit_reached,
                       cc1101_acquisition_mode_t a_mode,
                       const cc1101_scan_config_t *a_scan,
                       const cc1101_receive_config_t *a_receive );

#endif // _RFSCOUT_CC1101_VIEW_H_
