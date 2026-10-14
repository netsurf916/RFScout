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
    return RFSCOUT_STYLE_PIXEL_QUEST == ui_style_current();
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

/** @brief Draw short corner brackets within the supplied instrument rectangle. */
static void draw_corner_brackets( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
{
    const int arm = a_width < 24 || a_height < 24 ? 4 : 8;
    display_fill_rect_raw( a_x + 4, a_y, arm, 1, a_color );
    display_fill_rect_raw( a_x, a_y + 4, 1, arm, a_color );
    display_fill_rect_raw( a_x + a_width - 4 - arm, a_y, arm, 1, a_color );
    display_fill_rect_raw( a_x + a_width - 1, a_y + 4, 1, arm, a_color );
    display_fill_rect_raw( a_x + 4, a_y + a_height - 1, arm, 1, a_color );
    display_fill_rect_raw( a_x, a_y + a_height - 4 - arm, 1, arm, a_color );
    display_fill_rect_raw( a_x + a_width - 4 - arm, a_y + a_height - 1, arm, 1, a_color );
    display_fill_rect_raw( a_x + a_width - 1, a_y + a_height - 4 - arm, 1, arm, a_color );
}

void ui_game_draw_panel( int a_x, int a_y, int a_width, int a_height, uint16_t a_border )
{
    if( a_width < 12 || a_height < 12 )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t corner_color = a_border == theme->divider ? theme->accent : a_border;
    display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->surface );
    display_fill_rect_raw( a_x + 4, a_y, a_width - 8, 1, theme->divider );
    display_fill_rect_raw( a_x + 4, a_y + a_height - 1, a_width - 8, 1, theme->divider );
    display_fill_rect_raw( a_x, a_y + 4, 1, a_height - 8, theme->divider );
    display_fill_rect_raw( a_x + a_width - 1, a_y + 4, 1, a_height - 8, theme->divider );
    /* Four clipped corners and their steps remain inside the saved popup rectangle. */
    for( int row = 0; row < 4; ++row )
    {
        const int cut = 4 - row;
        display_fill_rect_raw( a_x, a_y + row, cut, 1, theme->background );
        display_fill_rect_raw( a_x + a_width - cut, a_y + row, cut, 1, theme->background );
        display_fill_rect_raw( a_x, a_y + a_height - 1 - row, cut, 1, theme->background );
        display_fill_rect_raw(
            a_x + a_width - cut, a_y + a_height - 1 - row, cut, 1, theme->background );
        display_fill_rect_raw( a_x + 3 - row, a_y + row, 1, 1, corner_color );
        display_fill_rect_raw( a_x + a_width - 4 + row, a_y + row, 1, 1, corner_color );
        display_fill_rect_raw( a_x + 3 - row, a_y + a_height - 1 - row, 1, 1, corner_color );
        display_fill_rect_raw(
            a_x + a_width - 4 + row, a_y + a_height - 1 - row, 1, 1, corner_color );
    }
    draw_corner_brackets( a_x, a_y, a_width, a_height, corner_color );
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
    display_fill_rect_raw( 4, top, 2, height, theme->info );
    const uint16_t tab_color = theme->info;
    display_fill_rect_raw( 4, top, 8, 1, tab_color );
    display_fill_rect_raw( 4, a_y + 9, 8, 1, tab_color );
    display_fill_rect_raw( RFSCOUT_DISPLAY_WIDTH - 12, top, 8, 1, tab_color );
    display_fill_rect_raw( RFSCOUT_DISPLAY_WIDTH - 5, top, 1, height, tab_color );
    display_fill_rect_raw( RFSCOUT_DISPLAY_WIDTH - 12, a_y + 9, 8, 1, tab_color );
    for( int row = 0; row < 7; ++row )
    {
        const int half = row <= 3 ? row : 6 - row;
        const int x = editing ? a_x + 5 - half : a_x + 3;
        const int width = editing ? half * 2 + 1 : half * 2 + 2;
        display_fill_rect_raw( x, a_y + row, width, 1, tab_color );
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
    const int filled =
        0U == a_maximum ? 0 : (int)( (uint64_t)a_value * (unsigned)a_width / a_maximum );
    display_fill_rect_raw( a_x, a_y, a_width, a_height, theme->background );
    display_fill_rect_raw( a_x, a_y, filled, a_height, a_color );
    if( ui_game_active() )
    {
        /* Each dim cell is empty capacity; the lit extent stays proportional to the value. */
        for( int column = 0; column < a_width; column += 6 )
        {
            const int cell_width = a_width - column < 5 ? a_width - column : 5;
            display_fill_rect_raw( a_x + column, a_y, cell_width, a_height, theme->divider );
            const int lit_width = filled - column < cell_width ? filled - column : cell_width;
            display_fill_rect_raw( a_x + column, a_y, lit_width, a_height, a_color );
            if( column + 5 < a_width )
            {
                display_fill_rect_raw( a_x + column + 5, a_y, 1, a_height, theme->background );
            }
        }
    }
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
    ui_game_draw_panel( 12, 30, 296, 75, theme->accent );
    draw_corner_brackets( 26, 40, 62, 53, theme->divider );
    display_fill_rect_raw( 28, 65, 7, 1, theme->accent );
    display_fill_rect_raw( 79, 65, 7, 1, theme->accent );
    display_fill_rect_raw( 56, 42, 1, 4, theme->accent );
    display_fill_rect_raw( 56, 87, 1, 4, theme->accent );
    ui_game_draw_icon( UI_GAME_ICON_SCOUT, 41, 49, 4U, theme->info );
    display_draw_text( 109, 45, "RFSCOUT", theme->shadow, 3U );
    display_draw_text( 108, 44, "RFSCOUT", theme->text, 3U );
    display_draw_text( 108, 73, "PIXEL QUEST", theme->selected, 1U );
    display_draw_text( 108, 88, "RF FIELD INSTRUMENT", theme->secondary, 1U );
    ui_game_draw_panel( 12, 116, 144, 40, theme->accent );
    ui_game_draw_panel( 164, 116, 144, 40, theme->selected );
    display_draw_text( 24, 125, "TURN", theme->accent, 1U );
    display_draw_text( 24, 141, "EXPLORE APPS", theme->text, 1U );
    display_draw_text( 176, 125, "CENTER", theme->selected, 1U );
    display_draw_text( 176, 141, "DEVICE SETTINGS", theme->text, 1U );
}
