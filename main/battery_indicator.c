/**
    battery_indicator.c : RFScout battery status widget
    Description: Draws the compact top-right battery indicator.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "battery_indicator.h"

#include "battery.h"
#include "display.h"

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

#define BATTERY_INDICATOR_COLOR_NORMAL 0x03F0U
#define BATTERY_INDICATOR_COLOR_COMPLETE 0x03E0U
#define BATTERY_INDICATOR_COLOR_LOW 0xFC00U
#define BATTERY_INDICATOR_COLOR_CRITICAL 0xF800U
#define BATTERY_INDICATOR_COLOR_UNAVAILABLE 0x8410U
#define BATTERY_INDICATOR_COLOR_CHARGING 0xFFE1U
#define BATTERY_INDICATOR_COLOR_BACKGROUND 0x0000U

static bool have_last_status;
static battery_status_t last_status;

/** @brief Return the subdued battery color associated with its urgency. */
static uint16_t indicator_color( const battery_status_t *a_status )
{
    if( ( NULL == a_status ) || !a_status->available )
    {
        return BATTERY_INDICATOR_COLOR_UNAVAILABLE;
    }
    if( ( BATTERY_CHARGE_COMPLETE == a_status->charge_state ) && a_status->external_power )
    {
        return BATTERY_INDICATOR_COLOR_COMPLETE;
    }
    if( a_status->percentage <= 10U )
    {
        return BATTERY_INDICATOR_COLOR_CRITICAL;
    }
    if( a_status->percentage <= 20U )
    {
        return BATTERY_INDICATOR_COLOR_LOW;
    }
    return BATTERY_INDICATOR_COLOR_NORMAL;
}

/** @brief Draw a compact transparent lightning bolt inside the empty battery body. */
static void draw_charging_bolt( int a_x, int a_y )
{
    /*
     * Eight-pixel-wide bolt derived from the simple diagonal reference silhouette. The mask is
     * transparent: zero bits preserve the black battery interior already in the framebuffer.
     */
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

    (void)display_draw_mask(
        a_x, a_y, 8, 10, mask, sizeof( mask ), BATTERY_INDICATOR_COLOR_CHARGING );
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
    const uint16_t color = indicator_color( a_status );
    char percentage[ 6 ];

    display_fill_rect( BATTERY_INDICATOR_REGION_X,
                       0,
                       BATTERY_INDICATOR_REGION_WIDTH,
                       BATTERY_INDICATOR_REGION_HEIGHT,
                       BATTERY_INDICATOR_COLOR_BACKGROUND );

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
    battery_status_t status = { 0 };
    (void)battery_get_status( &status );
    draw_status( &status );
}

void battery_indicator_poll( void )
{
    battery_status_t status = { 0 };
    (void)battery_get_status( &status );

    if( have_last_status && battery_status_equal( &status, &last_status ) )
    {
        return;
    }

    draw_status( &status );
}
