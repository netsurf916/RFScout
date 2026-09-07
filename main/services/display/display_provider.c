/**
    display_provider.c : RFScout display provider selection metadata
    Description: Describes the display implementation selected by the runtime board profile.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

const rfscout_display_info_t *display_info( void )
{
    static rfscout_display_info_t info;
    const rfscout_board_profile_t *profile = board_profile();
    info.provider = board_profile_selected() ? RFSCOUT_DISPLAY_PROVIDER_ST7789
                                             : RFSCOUT_DISPLAY_PROVIDER_NONE;
    info.name = board_profile_selected() ? "ST7789" : "None";
    info.width = profile->display_width;
    info.height = profile->display_height;
    info.capabilities = board_profile_selected()
                            ? RFSCOUT_DISPLAY_CAP_BRIGHTNESS | RFSCOUT_DISPLAY_CAP_LOW_POWER
                            : 0U;
    return &info;
}

rfscout_view_t display_view( void )
{
    const rfscout_display_info_t *info = display_info();
    return (rfscout_view_t){ .x = 0, .y = 0, .width = info->width, .height = info->height };
}
