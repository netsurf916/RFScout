/**
    subghz_capture.h : RFScout bounded Sub-GHz capture contract
    Description: Declares hardware-independent metadata for immutable native timing captures.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SUBGHZ_CAPTURE_H_
#define _RFSCOUT_SUBGHZ_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Raw capture word shape: two native RMT-compatible level/duration halves. */
typedef uint32_t rfscout_subghz_symbol_t;

/** @brief Demodulation family used to produce a Sub-GHz capture. */
typedef enum
{
    RFSCOUT_SUBGHZ_MODULATION_UNKNOWN = 0,
    RFSCOUT_SUBGHZ_MODULATION_OOK,
    RFSCOUT_SUBGHZ_MODULATION_2FSK,
} rfscout_subghz_modulation_t;

/** @brief Why one bounded raw capture ended. */
typedef enum
{
    RFSCOUT_SUBGHZ_CAPTURE_END_IDLE = 0,
    RFSCOUT_SUBGHZ_CAPTURE_END_CAPACITY,
    RFSCOUT_SUBGHZ_CAPTURE_END_ABORT,
} rfscout_subghz_capture_end_t;

/**
 * @brief Immutable completed Sub-GHz timing capture.
 * @details symbols is borrowed from the acquisition pool. Consumers must not mutate it or retain
 *          the pointer after the coordinator releases the pool lease.
 */
typedef struct
{
    const rfscout_subghz_symbol_t *symbols;
    size_t symbol_count;
    uint32_t frequency_hz;
    uint32_t resolution_hz;
    rfscout_subghz_modulation_t modulation;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t ended_ms;
    int8_t peak_rssi_dbm;
    rfscout_subghz_capture_end_t end_reason;
    bool truncated;
} rfscout_subghz_capture_t;

/** @brief Duration ticks for the first half of one native symbol word. */
static inline uint16_t rfscout_subghz_symbol_duration0( rfscout_subghz_symbol_t a_symbol )
{
    return (uint16_t)( a_symbol & 0x7FFFU );
}

/** @brief Logic level for the first half of one native symbol word. */
static inline bool rfscout_subghz_symbol_level0( rfscout_subghz_symbol_t a_symbol )
{
    return 0U != ( ( a_symbol >> 15U ) & 0x01U );
}

/** @brief Duration ticks for the second half of one native symbol word. */
static inline uint16_t rfscout_subghz_symbol_duration1( rfscout_subghz_symbol_t a_symbol )
{
    return (uint16_t)( ( a_symbol >> 16U ) & 0x7FFFU );
}

/** @brief Logic level for the second half of one native symbol word. */
static inline bool rfscout_subghz_symbol_level1( rfscout_subghz_symbol_t a_symbol )
{
    return 0U != ( ( a_symbol >> 31U ) & 0x01U );
}

/** @brief Convert capture ticks to rounded microseconds without modifying the raw evidence. */
static inline uint32_t rfscout_subghz_ticks_to_us( uint32_t a_ticks, uint32_t a_resolution_hz )
{
    if( 0U == a_resolution_hz )
    {
        return 0U;
    }
    return (uint32_t)( ( (uint64_t)a_ticks * 1000000ULL + ( a_resolution_hz / 2U ) ) /
                       a_resolution_hz );
}

#endif // _RFSCOUT_SUBGHZ_CAPTURE_H_
