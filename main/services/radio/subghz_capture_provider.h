/**
    subghz_capture_provider.h : RFScout bounded Sub-GHz RMT capture provider
    Description: Owns RMT/GDMA resources and the fixed DMA-capable capture pool.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SUBGHZ_CAPTURE_PROVIDER_H_
#define _RFSCOUT_SUBGHZ_CAPTURE_PROVIDER_H_

#include "subghz_capture.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_SUBGHZ_CAPTURE_POOL_SIZE 2U
#define RFSCOUT_SUBGHZ_CAPTURE_SYMBOL_CAPACITY 2048U

typedef struct
{
    int gpio_num;
    uint32_t resolution_hz;
    uint32_t signal_range_min_ns;
    uint32_t signal_range_max_ns;
    uint32_t frequency_hz;
    rfscout_subghz_modulation_t modulation;
} rfscout_subghz_capture_provider_config_t;

typedef struct
{
    uint32_t captures_completed;
    uint32_t captures_truncated;
    uint32_t capture_overruns;
    uint32_t capture_errors;
} rfscout_subghz_capture_stats_t;

/**
 * @brief Allocate the DMA-capable pool, configure RMT receive, and arm the first capture.
 * @details The provider is singleton because RFScout currently has one asynchronous Sub-GHz input.
 */
esp_err_t subghz_capture_provider_start( const rfscout_subghz_capture_provider_config_t *a_config );

/**
 * @brief Disable RMT before releasing DMA buffers.
 * @return true once no RMT/DMA operation can reference the pool.
 */
bool subghz_capture_provider_stop( void );

/** @brief Return whether the provider currently owns an enabled RMT receive channel. */
bool subghz_capture_provider_active( void );

/** @brief Retry acquisition after a prior no-free-buffer or arm failure condition. */
void subghz_capture_provider_poll( void );

/** @brief Lease the oldest READY capture as an immutable PROCESSING capture. */
bool subghz_capture_provider_acquire( rfscout_subghz_capture_t *a_capture, uint8_t *a_lease );

/** @brief Release a PROCESSING capture back to FREE and re-arm acquisition if needed. */
void subghz_capture_provider_release( uint8_t a_lease );

/** @brief Copy provider diagnostics. */
void subghz_capture_provider_stats( rfscout_subghz_capture_stats_t *a_output );

#endif // _RFSCOUT_SUBGHZ_CAPTURE_PROVIDER_H_
