/**
    display_provider.c : RFScout display provider selection metadata
    Description: Describes the display implementation selected by the current board image.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

static const rfscout_display_info_t active_display = {
    .provider = RFSCOUT_DISPLAY_PROVIDER_ST7789,
    .name = "ST7789",
    .width = RFSCOUT_BOARD_DISPLAY_WIDTH,
    .height = RFSCOUT_BOARD_DISPLAY_HEIGHT,
    .capabilities = RFSCOUT_DISPLAY_CAP_BRIGHTNESS | RFSCOUT_DISPLAY_CAP_LOW_POWER,
};

const rfscout_display_info_t *display_info( void )
{
    return &active_display;
}
