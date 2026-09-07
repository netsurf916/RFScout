/**
    display_provider.c : RFScout display provider selection metadata
    Description: Describes physical display geometry and the managed application/system view split.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "ui_theme.h"

#define DISPLAY_APP_VIEW_WIDTH 320U
#define DISPLAY_APP_VIEW_HEIGHT 170U

static esp_err_t app_view_init( const rfscout_view_t *a_view )
{
    if( NULL == a_view )
    {
        return ESP_ERR_INVALID_ARG;
    }
    display_fill_rect(
        a_view->x, a_view->y, a_view->width, a_view->height, ui_theme_get()->background );
    return ESP_OK;
}

static void app_view_deinit( const rfscout_view_t *a_view )
{
    if( NULL == a_view )
    {
        return;
    }
    display_fill_rect(
        a_view->x, a_view->y, a_view->width, a_view->height, ui_theme_get()->background );
}

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

const rfscout_view_t *display_app_view( void )
{
    static const rfscout_view_t view = {
        .x = 0,
        .y = 0,
        .width = DISPLAY_APP_VIEW_WIDTH,
        .height = DISPLAY_APP_VIEW_HEIGHT,
        .init = app_view_init,
        .deinit = app_view_deinit,
    };
    const rfscout_display_info_t *info = display_info();
    if( ( info->width < view.width ) || ( info->height < view.height ) )
    {
        return NULL;
    }
    return &view;
}

rfscout_view_t display_navigation_view( void )
{
    const rfscout_display_info_t *info = display_info();
    const rfscout_view_t *app_view = display_app_view();
    if( !RFSCOUT_BOARD_HAS_GLOBAL_TABS || ( NULL == app_view ) ||
        ( info->height <= app_view->height ) )
    {
        return ( rfscout_view_t ){ 0 };
    }
    return ( rfscout_view_t ){ .x = 0,
                               .y = (int16_t)app_view->height,
                               .width = info->width,
                               .height = (uint16_t)( info->height - app_view->height ) };
}
