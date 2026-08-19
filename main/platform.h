#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t platform_init(void);
void platform_poll(void);
void platform_set_alert_led(bool active);
void platform_audio_tone(bool enabled, uint16_t frequency_hz, void *context);
