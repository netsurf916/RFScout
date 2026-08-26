/**
    morse.h : RFScout Morse-code alert interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_MORSE_H_
#define _RFSCOUT_MORSE_H_

#include <stdbool.h>
#include <stdint.h>

typedef void ( *morse_tone_fn )( bool a_enabled, uint16_t a_frequency_hz, void *a_context );

/** @brief Play one Morse-code letter using the supplied tone callback. */
void morse_play_letter(
    char a_letter, uint32_t a_unit_ms, uint16_t a_tone_hz, morse_tone_fn a_tone, void *a_context );

/** @brief Return the dot/dash pattern for an ASCII letter. */
const char *morse_pattern( char a_letter );

#endif // _RFSCOUT_MORSE_H_
