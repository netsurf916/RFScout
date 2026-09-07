/**
    device_view_touch.c : T-Deck Home settings touch-calibration gateway
    Description: Adds touch calibration to Home settings without changing the validated T-Embed settings flow.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "board.h"
#include "display.h"
#include "touch.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define TOUCH_SETTINGS_BACK 0U
#define TOUCH_SETTINGS_DEVICE 1U
#define TOUCH_SETTINGS_CALIBRATE 2U
#define TOUCH_SETTINGS_COUNT 3U

void device_view_enter_original( void );
void device_view_render_original( void );
bool device_view_back_original( void );
bool device_view_input_original( bool a_pressed, int a_delta );

static bool gateway_active;
static unsigned selected_item;

static void draw_row( int a_y, unsigned a_item, const char *a_label, const char *a_value )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool selected = selected_item == a_item;
    const uint16_t color = selected ? theme->selected : theme->text;
    if( selected )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, a_y, ">", color, 1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, a_y, a_label, color, 1U );
    if( NULL != a_value )
    {
        display_draw_text( 226, a_y, a_value, selected ? theme->accent : theme->secondary, 1U );
    }
}

static void render_gateway( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "HOME SETTINGS", theme->secondary );
    draw_row( UI_LAYOUT_MENU_START_Y, TOUCH_SETTINGS_BACK, "Back", NULL );
    draw_row( UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
              TOUCH_SETTINGS_DEVICE,
              "Device settings",
              NULL );

    const char *status = touch_calibration_active()
                             ? "Running"
                             : ( touch_calibration_valid() ? "Ready" : "Required" );
    draw_row( UI_LAYOUT_MENU_START_Y + 2 * UI_LAYOUT_MENU_ROW_HEIGHT,
              TOUCH_SETTINGS_CALIBRATE,
              "Calibrate touch",
              status );

    char footer[ 48 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  MOVE / CENTER ENTER",
              selected_item + 1U,
              TOUCH_SETTINGS_COUNT );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, theme->secondary, 1U );
    (void)display_present();
}

void device_view_enter( void )
{
    if( !RFSCOUT_BOARD_HAS_TOUCH )
    {
        gateway_active = false;
        device_view_enter_original();
        return;
    }

    gateway_active = true;
    selected_item = TOUCH_SETTINGS_BACK;
    render_gateway();
}

void device_view_render( void )
{
    if( !gateway_active )
    {
        device_view_render_original();
        return;
    }
    render_gateway();
}

bool device_view_back( void )
{
    if( gateway_active )
    {
        return false;
    }
    if( !RFSCOUT_BOARD_HAS_TOUCH )
    {
        return device_view_back_original();
    }
    if( device_view_back_original() )
    {
        return true;
    }

    gateway_active = true;
    selected_item = TOUCH_SETTINGS_DEVICE;
    render_gateway();
    return true;
}

bool device_view_input( bool a_pressed, int a_delta )
{
    if( !gateway_active )
    {
        return device_view_input_original( a_pressed, a_delta );
    }

    if( a_pressed )
    {
        if( TOUCH_SETTINGS_BACK == selected_item )
        {
            return true;
        }
        if( TOUCH_SETTINGS_DEVICE == selected_item )
        {
            gateway_active = false;
            device_view_enter_original();
            return false;
        }
        if( TOUCH_SETTINGS_CALIBRATE == selected_item )
        {
            (void)touch_calibration_begin();
            render_gateway();
            return false;
        }
    }

    if( 0 != a_delta )
    {
        int item = (int)selected_item + ( a_delta > 0 ? 1 : -1 );
        if( item < 0 )
        {
            item = TOUCH_SETTINGS_COUNT - 1;
        }
        if( item >= (int)TOUCH_SETTINGS_COUNT )
        {
            item = 0;
        }
        selected_item = (unsigned)item;
        render_gateway();
    }
    return false;
}
