/**
    rotary_decoder.h : Rotary quadrature decoder with coalesced navigation
    Description: Rejects invalid transitions and collapses pending detents to one action.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ROTARY_DECODER_H_
#define _RFSCOUT_ROTARY_DECODER_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Half-step quadrature decoder with a signed count awaiting application input. */
typedef struct
{
    uint8_t last_state;
    uint8_t anchor_state;
    uint8_t intermediate_state;
    bool synchronized;
    int32_t pending_delta;
    uint32_t invalid_transitions;
} rotary_decoder_t;

/** @brief Initialize from the actual sampled two-bit A/B state. */
void rotary_decoder_reset( rotary_decoder_t *a_decoder, uint8_t a_initial_state );

/** @brief Sample a two-bit A/B state from the GPIO edge ISR; caller provides synchronization. */
void rotary_decoder_sample( rotary_decoder_t *a_decoder, uint8_t a_state );

/** @brief Consume all pending detents and return their net direction (-1, 0 or +1). */
int rotary_decoder_take_direction( rotary_decoder_t *a_decoder );

/** @brief Return whether at least one decoded detent is pending. */
bool rotary_decoder_pending( const rotary_decoder_t *a_decoder );

/** @brief Discard pending movement and any incomplete half-step without resetting diagnostics. */
void rotary_decoder_clear_pending( rotary_decoder_t *a_decoder );

#endif // _RFSCOUT_ROTARY_DECODER_H_
