/**
    radio_app_view.c : RFScout placeholder radio application views
    Description: Provides UI scaffolding before the radio drivers are enabled.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "radio_app_view.h"

#include "display.h"

#define COLOR_BACKGROUND 0x0000U
#define COLOR_SUBDUED 0x03F0U
#define COLOR_TEXT 0xBDF7U
#define COLOR_DIM 0x7BEFU
#define COLOR_YELLOW 0xFFE0U

void radio_app_view_cc1101( bool a_settings_open )
{
    display_clear( COLOR_BACKGROUND );
    display_draw_text( 8, 7, "CC1101", COLOR_SUBDUED, 2U );

    if( a_settings_open )
    {
        display_draw_text( 8, 34, "BAND / RANGE", COLOR_TEXT, 1U );
        display_draw_text( 8, 54, "> 433 ISM", COLOR_YELLOW, 1U );
        display_draw_text( 8, 70, "  315 MHz", COLOR_DIM, 1U );
        display_draw_text( 8, 86, "  868 MHz", COLOR_DIM, 1U );
        display_draw_text( 8, 102, "  915 MHz", COLOR_DIM, 1U );
        display_draw_text( 8, 118, "  MANUAL", COLOR_DIM, 1U );
        display_draw_text( 170, 54, "433.050 - 434.790", COLOR_DIM, 1U );
        display_draw_text( 170, 70, "CENTER: CLOSE", COLOR_DIM, 1U );
    }
    else
    {
        display_draw_text( 8, 42, "SUB-GHZ SITUATIONAL AWARENESS", COLOR_TEXT, 1U );
        display_draw_text( 8, 66, "433 ISM PROFILE", COLOR_DIM, 1U );
        display_draw_text( 8, 86, "SPECTRUM / EVENTS COMING NEXT", COLOR_DIM, 1U );
        display_draw_text( 8, 126, "CENTER: BAND SETTINGS", COLOR_SUBDUED, 1U );
    }

    (void)display_present();
}

void radio_app_view_nrf24( void )
{
    display_clear( COLOR_BACKGROUND );
    display_draw_text( 8, 7, "nRF24", COLOR_SUBDUED, 2U );
    display_draw_text( 8, 42, "2.4 GHz RADIO AWARENESS", COLOR_TEXT, 1U );
    display_draw_text( 8, 70, "ONBOARD RADIO RESERVED", COLOR_DIM, 1U );
    display_draw_text( 8, 90, "DRIVER / VIEW COMING NEXT", COLOR_DIM, 1U );
    display_draw_text( 8, 126, "GPIO43 CE / GPIO44 CSN", COLOR_SUBDUED, 1U );
    (void)display_present();
}
