/**
    ui_game.c : RFScout pixel-console presentation primitives
    Description: Allocation-free original art with crisp integer-pixel geometry.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_game.h"

#include "display.h"
#include "ui_theme.h"

#include <stddef.h>
#include <string.h>

/* One byte per row, MSB at the left. All silhouettes are original RFScout art. */
static const uint8_t icons[ UI_GAME_ICON_COUNT ][ 8 ] = {
    [UI_GAME_ICON_SCOUT] = { 0x18, 0x3C, 0x7E, 0x5A, 0x7E, 0x3C, 0x66, 0x42 },
    [UI_GAME_ICON_RADIO] = { 0x81, 0x5A, 0x24, 0x18, 0x18, 0x3C, 0x3C, 0x7E },
    [UI_GAME_ICON_NETWORK] = { 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x24, 0x00, 0x18 },
    [UI_GAME_ICON_TAPE] = { 0x7E, 0xFF, 0xA5, 0xA5, 0xFF, 0xBD, 0xC3, 0x7E },
    [UI_GAME_ICON_THERMAL] = { 0x18, 0x24, 0x24, 0x24, 0x3C, 0x7E, 0x7E, 0x3C },
    [UI_GAME_ICON_AUDIO] = { 0x00, 0x08, 0x28, 0xAA, 0xAA, 0xAA, 0xAA, 0xFF },
    [UI_GAME_ICON_COMPASS] = { 0x3C, 0x42, 0x99, 0x95, 0xA9, 0x99, 0x42, 0x3C },
    [UI_GAME_ICON_DRIVE] = { 0x00, 0x3C, 0x42, 0x7E, 0xFF, 0xBD, 0xFF, 0x66 },
    [UI_GAME_ICON_EVENTS] = { 0x7E, 0x42, 0x5A, 0x42, 0x5A, 0x42, 0x7E, 0x00 },
    [UI_GAME_ICON_SETTINGS] = { 0x24, 0x7E, 0xDB, 0xA5, 0xA5, 0xDB, 0x7E, 0x24 },
    [UI_GAME_ICON_STAR] = { 0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x7E, 0x66, 0x42 },
};

bool ui_game_active( void )
{
    return RFSCOUT_THEME_PIXEL_QUEST == ui_theme_current();
}

void ui_game_draw_icon(
    ui_game_icon_t a_icon, int a_x, int a_y, unsigned a_scale, uint16_t a_color )
{
    if( ( (unsigned)a_icon >= UI_GAME_ICON_COUNT ) || ( 0U == a_scale ) || ( a_scale > 8U ) )
    {
        return;
    }
    for( unsigned row = 0U; row < 8U; ++row )
    {
        for( unsigned column = 0U; column < 8U; ++column )
        {
            if( 0U != ( icons[ a_icon ][ row ] & ( 0x80U >> column ) ) )
            {
                display_fill_rect_raw( a_x + (int)( column * a_scale ),
                                       a_y + (int)( row * a_scale ),
                                       (int)a_scale,
                                       (int)a_scale,
                                       a_color );
            }
        }
    }
}

ui_game_icon_t ui_game_icon_for_title( const char *a_title )
{
    if( NULL == a_title )
    {
        return UI_GAME_ICON_SCOUT;
    }
    if( NULL != strstr( a_title, "SETTINGS" ) || NULL != strstr( a_title, "settings" ) ||
        NULL != strstr( a_title, "POWER" ) || NULL != strstr( a_title, "TIME" ) ||
        NULL != strstr( a_title, "SOURCES" ) || NULL != strstr( a_title, "ALERTS" ) ||
        NULL != strstr( a_title, "ENABLEMENT" ) )
    {
        return UI_GAME_ICON_SETTINGS;
    }
    if( NULL != strstr( a_title, "CC1101" ) || NULL != strstr( a_title, "nRF24" ) )
    {
        return UI_GAME_ICON_RADIO;
    }
    if( NULL != strstr( a_title, "RECORDER" ) )
    {
        return UI_GAME_ICON_TAPE;
    }
    if( NULL != strstr( a_title, "THERMAL" ) )
    {
        return UI_GAME_ICON_THERMAL;
    }
    if( NULL != strstr( a_title, "AUDIO" ) )
    {
        return UI_GAME_ICON_AUDIO;
    }
    if( NULL != strstr( a_title, "GPS" ) )
    {
        return UI_GAME_ICON_COMPASS;
    }
    if( NULL != strstr( a_title, "WARDRIVING" ) )
    {
        return UI_GAME_ICON_DRIVE;
    }
    if( NULL != strstr( a_title, "EVENTS" ) )
    {
        return UI_GAME_ICON_EVENTS;
    }
    if( NULL != strstr( a_title, "WiFi" ) || NULL != strstr( a_title, "WIFI" ) ||
        NULL != strstr( a_title, "WI-FI" ) || NULL != strstr( a_title, "NETWORK" ) ||
        NULL != strstr( a_title, "HOTSPOTS" ) || NULL != strstr( a_title, "SURVEILLANCE" ) )
    {
        return UI_GAME_ICON_NETWORK;
    }
    return UI_GAME_ICON_SCOUT;
}

/** @brief Blend two RGB565 colors without channel bleed. */
static uint16_t blend_color( uint16_t a_left, uint16_t a_right )
{
    const unsigned red = ( ( a_left >> 11 ) + ( a_right >> 11 ) ) / 2U;
    const unsigned green = ( ( ( a_left >> 5 ) & 63U ) + ( ( a_right >> 5 ) & 63U ) ) / 2U;
    const unsigned blue = ( ( a_left & 31U ) + ( a_right & 31U ) ) / 2U;
    return (uint16_t)( ( red << 11 ) | ( green << 5 ) | blue );
}

/** @brief Clip one tile feature to its caller's rectangle, including partial edge tiles. */
static void fill_tile_feature( int a_x,
                               int a_y,
                               int a_width,
                               int a_height,
                               int a_left,
                               int a_top,
                               int a_feature_width,
                               int a_feature_height,
                               uint16_t a_color )
{
    const int left = a_left < 0 ? 0 : a_left;
    const int top = a_top < 0 ? 0 : a_top;
    const int right = a_left + a_feature_width < a_width ? a_left + a_feature_width : a_width;
    const int bottom = a_top + a_feature_height < a_height ? a_top + a_feature_height : a_height;
    display_fill_rect_raw( a_x + left, a_y + top, right - left, bottom - top, a_color );
}

void ui_game_draw_bricks( int a_x, int a_y, int a_width, int a_height )
{
    if( !ui_game_active() || a_width <= 0 || a_height <= 0 )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t face = blend_color( theme->accent, theme->selected );
    const uint16_t light = blend_color( face, theme->text );
    const uint16_t shade = blend_color( face, theme->shadow );
    display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->shadow );
    for( int row = 0; row < a_height; row += 8 )
    {
        const int offset = ( ( row / 8 ) & 1 ) * 8;
        for( int column = -offset; column < a_width; column += 16 )
        {
            fill_tile_feature( a_x, a_y, a_width, a_height, column, row, 15, 7, face );
            fill_tile_feature( a_x, a_y, a_width, a_height, column + 1, row + 1, 13, 1, light );
            fill_tile_feature( a_x, a_y, a_width, a_height, column + 1, row + 2, 1, 4, light );
            fill_tile_feature( a_x, a_y, a_width, a_height, column + 1, row + 6, 13, 1, shade );
        }
    }
}

void ui_game_draw_pipe( int a_x, int a_y, int a_width, int a_height )
{
    if( !ui_game_active() || a_width < 8 || a_height < 8 )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t light = blend_color( theme->success, theme->text );
    const uint16_t shade = blend_color( theme->success, theme->shadow );
    /* The rim is wider than the stem; unpainted corners remain transparent. */
    display_fill_rect_raw( a_x + 2, a_y + 4, a_width - 4, a_height - 4, shade );
    display_fill_rect_raw( a_x + 3, a_y + 5, a_width - 6, a_height - 6, theme->success );
    display_fill_rect_raw( a_x + 4, a_y + 6, 2, a_height - 7, light );
    display_fill_rect_raw( a_x, a_y, a_width, 6, shade );
    display_fill_rect_raw( a_x + 1, a_y + 1, a_width - 2, 4, theme->success );
    display_fill_rect_raw( a_x + 1, a_y + 1, a_width - 2, 1, light );
}

void ui_game_draw_panel( int a_x, int a_y, int a_width, int a_height, uint16_t a_border )
{
    if( a_width < 12 || a_height < 12 )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->shadow );
    display_fill_rect_raw( a_x, a_y, a_width - 2, a_height - 2, a_border );
    display_fill_rect_raw( a_x + 2, a_y + 2, a_width - 6, a_height - 6, theme->surface );
    display_fill_rect_raw( a_x + 4, a_y + 4, a_width - 10, 1, theme->divider );
    display_fill_rect_raw( a_x + 4, a_y + 4, 1, a_height - 10, theme->divider );
    /* Cut corners stay inside the caller's saved overlay rectangle. */
    display_fill_rect_raw( a_x, a_y, 2, 2, theme->background );
    display_fill_rect_raw( a_x + a_width - 4, a_y, 2, 2, theme->background );
    ui_game_draw_bricks( a_x + 2, a_y + a_height - 2, a_width - 4, 2 );
}

void ui_game_draw_menu_marker( int a_x, int a_y, const char *a_marker, uint16_t a_color )
{
    if( !ui_game_active() )
    {
        display_draw_text( a_x, a_y, a_marker, a_color, 1U );
        return;
    }
    if( NULL == a_marker || '\0' == a_marker[ 0 ] || ' ' == a_marker[ 0 ] )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool editing = NULL != strchr( a_marker, '*' );
    const int top = a_y - 3 < 22 ? 22 : a_y - 3;
    const int height = a_y + 10 - top;
    display_fill_rect_raw( 4, top, RFSCOUT_DISPLAY_WIDTH - 8, height, theme->surface );
    display_fill_rect_raw( 4, top, 2, height, editing ? theme->accent : a_color );
    ui_game_draw_bricks( 6, a_y + 7, RFSCOUT_DISPLAY_WIDTH - 12, 3 );
    for( int row = 0; row < 7; ++row )
    {
        const int half = row <= 3 ? row : 6 - row;
        const int x = editing ? a_x + 5 - half : a_x + 3;
        const int width = editing ? half * 2 + 1 : half * 2 + 2;
        display_fill_rect_raw( x, a_y + row, width, 1, a_color );
    }
    if( editing )
    {
        display_fill_rect_raw( a_x + 4, a_y + 2, 3, 3, theme->surface );
    }
}

void ui_game_draw_meter( int a_x,
                         int a_y,
                         int a_width,
                         int a_height,
                         uint32_t a_value,
                         uint32_t a_maximum,
                         uint16_t a_color )
{
    if( a_width <= 0 || a_height <= 0 )
    {
        return;
    }
    if( a_value > a_maximum )
    {
        a_value = a_maximum;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ui_game_active() && a_width >= 8 && a_height >= 6 )
    {
        const uint16_t light = blend_color( a_color, theme->text );
        const uint16_t shade = blend_color( a_color, theme->shadow );
        const int inner_width = a_width - 5;
        const int filled =
            0U == a_maximum ? 0 : (int)( (uint64_t)a_value * (unsigned)inner_width / a_maximum );
        display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->background );
        display_fill_rect_raw( a_x + 2, a_y + 1, a_width - 2, a_height - 2, shade );
        display_fill_rect_raw( a_x + 4, a_y + 2, inner_width, a_height - 4, theme->background );
        display_fill_rect_raw( a_x + 4, a_y + 2, filled, a_height - 4, a_color );
        display_fill_rect_raw( a_x + 4, a_y + 1, inner_width, 1, light );
        /* The fixed lip is decoration; only the inner cavity represents the value. */
        display_fill_rect_raw( a_x, a_y, 4, a_height, shade );
        display_fill_rect_raw( a_x + 1, a_y + 1, 2, a_height - 2, a_color );
        display_fill_rect_raw( a_x + 1, a_y + 1, 1, a_height - 2, light );
        return;
    }
    const int filled =
        0U == a_maximum ? 0 : (int)( (uint64_t)a_value * (unsigned)a_width / a_maximum );
    display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->background );
    display_fill_rect_raw( a_x, a_y, filled, a_height, a_color );
}

void ui_game_draw_column( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
{
    display_fill_rect( a_x, a_y, a_width, a_height, a_color );
    if( ui_game_active() )
    {
        /* Gaps do not quantize the measured height or move the actual top. */
        for( int row = 3; row < a_height; row += 4 )
        {
            display_fill_rect_raw( a_x, a_y + row, a_width, 1, ui_theme_get()->background );
        }
    }
}

void ui_game_draw_home( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    ui_game_draw_panel( 12, 30, 296, 128, theme->accent );
    ui_game_draw_icon( UI_GAME_ICON_SCOUT, 30, 48, 5U, theme->info );
    ui_game_draw_icon( UI_GAME_ICON_STAR, 274, 39, 2U, theme->selected );
    display_draw_text( 95, 50, "RFSCOUT", theme->shadow, 3U );
    display_draw_text( 92, 47, "RFSCOUT", theme->text, 3U );
    display_draw_text( 94, 76, "PIXEL QUEST", theme->selected, 1U );
    display_draw_text( 94, 90, "RF SITUATIONAL AWARENESS", theme->secondary, 1U );
    ui_game_draw_panel( 28, 106, 264, 38, theme->divider );
    display_draw_text( 42, 114, "TURN: EXPLORE APPS", theme->info, 1U );
    display_draw_text( 42, 130, "CENTER: DEVICE SETTINGS", theme->selected, 1U );
    ui_game_draw_bricks( 240, 63, 48, 8 );
    ui_game_draw_pipe( 26, 88, 44, 18 );
    ui_game_draw_pipe( 268, 82, 24, 24 );
    ui_game_draw_bricks( 24, 147, 272, 8 );
}
