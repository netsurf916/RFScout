/**
    battery_indicator.c : RFScout battery status widget
    Description: Draws the compact top-right battery indicator.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "battery_indicator.h"

#include "battery.h"
#include "display.h"
#include "ui_theme.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BATTERY_INDICATOR_BODY_X 282
#define BATTERY_INDICATOR_BODY_Y 4
#define BATTERY_INDICATOR_BODY_WIDTH 28
#define BATTERY_INDICATOR_BODY_HEIGHT 14
#define BATTERY_INDICATOR_TERMINAL_WIDTH 3
#define BATTERY_INDICATOR_TERMINAL_HEIGHT 6
#define BATTERY_INDICATOR_TEXT_GAP 5
#define BATTERY_INDICATOR_REGION_X 253
#define BATTERY_INDICATOR_REGION_WIDTH 60
#define BATTERY_INDICATOR_REGION_HEIGHT 20

static bool have_last_status;
static battery_status_t last_status;

/** @brief Return the semantic battery color associated with its urgency. */
static uint16_t indicator_color( const battery_status_t *a_status )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ( NULL == a_status ) || !a_status->available )
    {
        return theme->secondary;
    }
    if( ( BATTERY_CHARGE_COMPLETE == a_status->charge_state ) && a_status->external_power )
    {
        return theme->success;
    }
    if( a_status->percentage <= 10U )
    {
        return theme->error;
    }
    if( a_status->percentage <= 20U )
    {
        return theme->warning;
    }
    return theme->normal;
}

/** @brief Draw a compact transparent lightning bolt inside the empty battery body. */
static void draw_charging_bolt( int a_x, int a_y )
{
    static const uint8_t mask[] = {
        0x1CU,
        0x38U,
        0x38U,
        0x70U,
        0xFEU,
        0x1CU,
        0x38U,
        0x38U,
        0x70U,
        0xE0U,
    };

    const ui_theme_palette_t *theme = ui_theme_get();
    (void)display_draw_mask( a_x, a_y, 8, 10, mask, sizeof( mask ), theme->warning );
}

/** @brief Draw the outline and terminal of the battery body. */
static void draw_battery_outline( uint16_t a_color )
{
    display_fill_rect( BATTERY_INDICATOR_BODY_X,
                       BATTERY_INDICATOR_BODY_Y,
                       BATTERY_INDICATOR_BODY_WIDTH,
                       1,
                       a_color );
    display_fill_rect( BATTERY_INDICATOR_BODY_X,
                       BATTERY_INDICATOR_BODY_Y + BATTERY_INDICATOR_BODY_HEIGHT - 1,
                       BATTERY_INDICATOR_BODY_WIDTH,
                       1,
                       a_color );
    display_fill_rect( BATTERY_INDICATOR_BODY_X,
                       BATTERY_INDICATOR_BODY_Y,
                       1,
                       BATTERY_INDICATOR_BODY_HEIGHT,
                       a_color );
    display_fill_rect( BATTERY_INDICATOR_BODY_X + BATTERY_INDICATOR_BODY_WIDTH - 1,
                       BATTERY_INDICATOR_BODY_Y,
                       1,
                       BATTERY_INDICATOR_BODY_HEIGHT,
                       a_color );

    const int terminal_y =
        BATTERY_INDICATOR_BODY_Y +
        ( BATTERY_INDICATOR_BODY_HEIGHT - BATTERY_INDICATOR_TERMINAL_HEIGHT ) / 2;
    display_fill_rect( BATTERY_INDICATOR_BODY_X + BATTERY_INDICATOR_BODY_WIDTH,
                       terminal_y,
                       BATTERY_INDICATOR_TERMINAL_WIDTH,
                       BATTERY_INDICATOR_TERMINAL_HEIGHT,
                       a_color );
}

/** @brief Draw the proportional battery fill when the battery is not actively charging. */
static void draw_battery_fill( const battery_status_t *a_status, uint16_t a_color )
{
    if( ( NULL == a_status ) || !a_status->available ||
        ( BATTERY_CHARGE_CHARGING == a_status->charge_state ) )
    {
        return;
    }

    const int inner_x = BATTERY_INDICATOR_BODY_X + 2;
    const int inner_y = BATTERY_INDICATOR_BODY_Y + 2;
    const int inner_width = BATTERY_INDICATOR_BODY_WIDTH - 4;
    const int inner_height = BATTERY_INDICATOR_BODY_HEIGHT - 4;
    int fill_width = ( (int)a_status->percentage * inner_width + 99 ) / 100;

    if( fill_width > inner_width )
    {
        fill_width = inner_width;
    }
    if( fill_width > 0 )
    {
        display_fill_rect( inner_x, inner_y, fill_width, inner_height, a_color );
    }
}

/** @brief Return whether two cached battery states render identically. */
static bool battery_status_equal( const battery_status_t *a_left, const battery_status_t *a_right )
{
    return ( a_left->available == a_right->available ) &&
           ( a_left->percentage == a_right->percentage ) &&
           ( a_left->charge_state == a_right->charge_state ) &&
           ( a_left->external_power == a_right->external_power );
}

/** @brief Render one battery-status snapshot into the framebuffer. */
static void draw_status( const battery_status_t *a_status )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t color = indicator_color( a_status );
    char percentage[ 6 ];

    display_fill_rect( BATTERY_INDICATOR_REGION_X,
                       0,
                       BATTERY_INDICATOR_REGION_WIDTH,
                       BATTERY_INDICATOR_REGION_HEIGHT,
                       theme->background );

    if( a_status->available )
    {
        snprintf( percentage, sizeof( percentage ), "%u%%", (unsigned)a_status->percentage );
    }
    else
    {
        strlcpy( percentage, "--%", sizeof( percentage ) );
    }

    const int text_width = (int)strlen( percentage ) * 6;
    const int text_x = BATTERY_INDICATOR_BODY_X - BATTERY_INDICATOR_TEXT_GAP - text_width;
    display_draw_text( text_x, 7, percentage, color, 1 );

    draw_battery_outline( color );
    draw_battery_fill( a_status, color );

    if( BATTERY_CHARGE_CHARGING == a_status->charge_state )
    {
        draw_charging_bolt( BATTERY_INDICATOR_BODY_X + 10, BATTERY_INDICATOR_BODY_Y + 2 );
    }

    last_status = *a_status;
    have_last_status = true;
}

void battery_indicator_draw( void )
{
    /* Full-screen renders should not depend on some earlier caller having refreshed telemetry. */
    battery_poll();

    battery_status_t status = { 0 };
    (void)battery_get_status( &status );
    draw_status( &status );
}

void battery_indicator_poll( void )
{
    /* Keep the widget self-contained: battery_poll() is internally rate-limited to 1 Hz, so this
     * remains cheap when called from the 10 ms UI loop and cannot depend on caller ordering. */
    battery_poll();

    battery_status_t status = { 0 };
    (void)battery_get_status( &status );

    if( have_last_status && battery_status_equal( &status, &last_status ) )
    {
        return;
    }

    /* draw_status() clears and redraws the whole indicator region through display primitives;
     * those writes invalidate the framebuffer so static Home is flushed on the next present. */
    draw_status( &status );
}
