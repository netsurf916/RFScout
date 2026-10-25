/**
    rotary_decoder.h : Bounded rotary-encoder quadrature decoder
    Description: Rejects invalid transitions and retains individual detents in arrival order.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ROTARY_DECODER_H_
#define _RFSCOUT_ROTARY_DECODER_H_

#include <stdbool.h>
#include <stdint.h>

#define ROTARY_DECODER_QUEUE_CAPACITY 64U

/** @brief Half-step quadrature decoder and bounded ordered queue of signed detents. */
typedef struct
{
    uint8_t last_state;
    uint8_t anchor_state;
    uint8_t intermediate_state;
    bool synchronized;
    int8_t steps[ ROTARY_DECODER_QUEUE_CAPACITY ];
    uint8_t head;
    uint8_t count;
    uint32_t invalid_transitions;
    uint32_t dropped_steps;
} rotary_decoder_t;

/** @brief Initialize from the actual sampled two-bit A/B state. */
void rotary_decoder_reset( rotary_decoder_t *a_decoder, uint8_t a_initial_state );

/** @brief Sample a two-bit A/B state from the GPIO edge ISR; caller provides synchronization. */
void rotary_decoder_sample( rotary_decoder_t *a_decoder, uint8_t a_state );

/** @brief Consume one queued signed detent in arrival order, or zero when empty. */
int rotary_decoder_take_step( rotary_decoder_t *a_decoder );

/** @brief Return whether at least one decoded detent is pending. */
bool rotary_decoder_pending( const rotary_decoder_t *a_decoder );

/** @brief Discard queued movement and any incomplete half-step without resetting diagnostics. */
void rotary_decoder_clear_pending( rotary_decoder_t *a_decoder );

#endif // _RFSCOUT_ROTARY_DECODER_H_
