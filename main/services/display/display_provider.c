/**
    display_provider.c : RFScout display provider selection metadata
    Description: Describes physical display geometry and the application/system view split.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#define DISPLAY_GLOBAL_TAB_HEIGHT 32U

const rfscout_display_info_t *display_info( void )
{
    static rfscout_display_info_t info;
    const rfscout_board_profile_t *profile = board_profile();
    info.provider =
        board_profile_selected() ? RFSCOUT_DISPLAY_PROVIDER_ST7789 : RFSCOUT_DISPLAY_PROVIDER_NONE;
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
    uint16_t height = info->height;
    if( RFSCOUT_BOARD_HAS_GLOBAL_TABS && height > DISPLAY_GLOBAL_TAB_HEIGHT )
    {
        height = (uint16_t)( height - DISPLAY_GLOBAL_TAB_HEIGHT );
    }
    return ( rfscout_view_t ){ .x = 0, .y = 0, .width = info->width, .height = height };
}

rfscout_view_t display_navigation_view( void )
{
    const rfscout_display_info_t *info = display_info();
    if( !RFSCOUT_BOARD_HAS_GLOBAL_TABS || info->height <= DISPLAY_GLOBAL_TAB_HEIGHT )
    {
        return ( rfscout_view_t ){ 0 };
    }
    return ( rfscout_view_t ){ .x = 0,
                               .y = (int16_t)( info->height - DISPLAY_GLOBAL_TAB_HEIGHT ),
                               .width = info->width,
                               .height = DISPLAY_GLOBAL_TAB_HEIGHT };
}
