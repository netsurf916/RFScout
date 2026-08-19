#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*morse_tone_fn)(bool enabled, uint16_t frequency_hz, void *context);

void morse_play_letter(char letter, uint32_t unit_ms, uint16_t tone_hz,
                       morse_tone_fn tone, void *context);
const char *morse_pattern(char letter);
