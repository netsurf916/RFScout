/**
    rotary_decoder.h : LilyGO-compatible rotary quadrature decoder
    Description: Reports position changes only at TWO03 latches and coalesces navigation.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ROTARY_DECODER_H_
#define _RFSCOUT_ROTARY_DECODER_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Persistent Gray-code position and last externally consumed TWO03 latch. */
typedef struct
{
    uint8_t last_state;
    bool suppress_until_latch;
    int32_t position;
    int32_t latched_position;
    int32_t consumed_position;
    uint32_t invalid_transitions;
} rotary_decoder_t;

/** @brief Initialize using the sampled A | (B << 1) GPIO state. */
void rotary_decoder_reset( rotary_decoder_t *a_decoder, uint8_t a_initial_state );

/** @brief Apply a LilyGO-style tick from the 1ms periodic sampler; caller synchronizes. */
void rotary_decoder_sample( rotary_decoder_t *a_decoder, uint8_t a_state );

/** @brief Consume all published latch movement as one net direction (-1, 0, +1). */
int rotary_decoder_take_direction( rotary_decoder_t *a_decoder );

/** @brief Report completed, unconsumed latch motion, not intermediate GPIO changes. */
bool rotary_decoder_pending( const rotary_decoder_t *a_decoder );

/** @brief Discard movement; if mid-cycle, suppress input through the next TWO03 latch. */
void rotary_decoder_clear_pending( rotary_decoder_t *a_decoder );

#endif // _RFSCOUT_ROTARY_DECODER_H_
