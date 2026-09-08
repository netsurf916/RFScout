/**
    navigation_bar.c : RFScout system-owned global app navigation chrome
    Description: Renders available app tabs and consumes authoritative touch in system-owned display space.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "navigation_bar.h"

#include "app_framework.h"
#include "app_manager.h"
#include "device_config.h"
#include "display.h"
#include "input.h"
#include "touch.h"
#include "ui_theme.h"

#include "esp_timer.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define NAV_LABEL_MAX 5U
#define NAV_TEXT_CELL_WIDTH 6
#define NAV_TEXT_SCALE 1U
#define NAV_TAB_HEIGHT 38

static bool touch_pressed;
static bool app_touch_active;
static bool app_touch_lock_fired;
static int64_t app_touch_started_us;

static bool app_visible( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_DEVICE == a_app )
    {
        return true;
    }
    return device_config_app_enabled( a_app ) &&
           ( RFSCOUT_APP_AVAILABILITY_AVAILABLE == app_framework_availability( a_app ) );
}

static size_t collect_visible_apps( rfscout_app_id_t a_visible[ RFSCOUT_APP_COUNT ] )
{
    size_t count = 0U;
    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        const rfscout_app_id_t candidate = (rfscout_app_id_t)app;
        if( app_visible( candidate ) )
        {
            a_visible[ count++ ] = candidate;
        }
    }
    return count;
}

static void make_label( const char *a_name, char a_label[ NAV_LABEL_MAX + 1U ] )
{
    size_t out = 0U;
    if( NULL != a_name )
    {
        for( size_t index = 0U; a_name[ index ] != '\0' && out < NAV_LABEL_MAX; ++index )
        {
            const char value = a_name[ index ];
            if( ( value >= 'A' && value <= 'Z' ) || ( value >= 'a' && value <= 'z' ) ||
                ( value >= '0' && value <= '9' ) )
            {
                a_label[ out++ ] = value;
            }
        }
    }
    if( 0U == out )
    {
        a_label[ out++ ] = '?';
    }
    a_label[ out ] = '\0';
}

static bool touch_in_view( const rfscout_touch_sample_t *a_sample, const rfscout_view_t *a_view )
{
    return NULL != a_sample && NULL != a_view && a_view->width > 0U && a_view->height > 0U &&
           a_sample->x >= (uint16_t)a_view->x &&
           a_sample->x < (uint16_t)( a_view->x + (int)a_view->width ) &&
           a_sample->y >= (uint16_t)a_view->y &&
           a_sample->y < (uint16_t)( a_view->y + (int)a_view->height );
}

static void reset_app_touch( void )
{
    app_touch_active = false;
    app_touch_lock_fired = false;
    app_touch_started_us = 0;
}

static void draw_calibration( const rfscout_view_t *a_view )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( a_view->x, a_view->y, a_view->width, a_view->height, theme->background );
    display_fill_rect( a_view->x, a_view->y, a_view->width, 2, theme->divider );
    display_draw_text( a_view->x + 8, a_view->y + 10, "TOUCH CALIBRATION", theme->accent, 1U );

    unsigned step = 0U;
    unsigned total = 0U;
    touch_calibration_progress( &step, &total );
    const char *prompt = touch_calibration_prompt();
    char instruction[ 64 ];
    snprintf( instruction,
              sizeof( instruction ),
              "%u/%u TOUCH %s",
              step,
              total,
              NULL != prompt ? prompt : "SCREEN" );
    display_draw_text( a_view->x + 8, a_view->y + 34, instruction, theme->text, 1U );
    display_draw_text(
        a_view->x + 8, a_view->y + 52, "LIFT FINGER BETWEEN POINTS", theme->secondary, 1U );
}

void navigation_bar_poll( void )
{
    rfscout_touch_sample_t sample;
    if( !touch_sample_get( &sample ) || !sample.calibrated || touch_calibration_active() )
    {
        touch_pressed = false;
        reset_app_touch();
        return;
    }

    if( !sample.pressed )
    {
        if( app_touch_active && !app_touch_lock_fired && !input_locked() )
        {
            input_inject_select();
        }
        touch_pressed = false;
        reset_app_touch();
        return;
    }

    if( app_touch_active )
    {
        if( input_primary_select_available() )
        {
            reset_app_touch();
            return;
        }
        if( !app_touch_lock_fired && app_touch_started_us > 0 &&
            ( esp_timer_get_time() - app_touch_started_us ) >= RFSCOUT_INPUT_LOCK_HOLD_US )
        {
            input_toggle_lock();
            app_touch_lock_fired = true;
        }
        return;
    }

    if( touch_pressed )
    {
        return;
    }
    touch_pressed = true;

    /* Some capture hardware temporarily owns the physical Select GPIO. Keep touch system-owned,
       but translate a fresh touch in the managed app view through the existing semantic input
       path while that physical source is unavailable. Applications still receive no coordinates. */
    if( !input_primary_select_available() )
    {
        const rfscout_view_t app_view = display_app_view();
        if( touch_in_view( &sample, &app_view ) )
        {
            app_touch_active = true;
            app_touch_started_us = esp_timer_get_time();
            app_touch_lock_fired = false;
            return;
        }
    }

    /* Input lock is device-wide. Touch remains observable so a held app-view touch can use the
       normal unlock gesture when the primary Select source is suspended, but locked input never
       drives global tab selection. */
    if( input_locked() )
    {
        return;
    }

    const rfscout_view_t view = display_navigation_view();
    if( ( 0U == view.width ) || ( 0U == view.height ) )
    {
        return;
    }

    const int tab_height = view.height < NAV_TAB_HEIGHT ? view.height : NAV_TAB_HEIGHT;
    if( ( sample.x < (uint16_t)view.x ) || ( sample.x >= (uint16_t)( view.x + (int)view.width ) ) ||
        ( sample.y < (uint16_t)view.y ) || ( sample.y >= (uint16_t)( view.y + tab_height ) ) )
    {
        return;
    }

    rfscout_app_id_t visible[ RFSCOUT_APP_COUNT ];
    const size_t visible_count = collect_visible_apps( visible );
    if( 0U == visible_count )
    {
        return;
    }

    const size_t tab_width = view.width / visible_count;
    if( 0U == tab_width )
    {
        return;
    }
    size_t index = (size_t)( sample.x - (uint16_t)view.x ) / tab_width;
    if( index >= visible_count )
    {
        index = visible_count - 1U;
    }
    (void)app_manager_select( visible[ index ] );
}

void navigation_bar_draw( void )
{
    const rfscout_view_t view = display_navigation_view();
    if( ( 0U == view.width ) || ( 0U == view.height ) )
    {
        return;
    }

    if( touch_calibration_active() )
    {
        draw_calibration( &view );
        return;
    }

    rfscout_app_id_t visible[ RFSCOUT_APP_COUNT ];
    const size_t visible_count = collect_visible_apps( visible );
    if( 0U == visible_count )
    {
        return;
    }

    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( view.x, view.y, view.width, view.height, theme->background );
    display_fill_rect( view.x, view.y, view.width, 2, theme->divider );

    const int tab_height = view.height < NAV_TAB_HEIGHT ? view.height : NAV_TAB_HEIGHT;
    const int tab_width = (int)view.width / (int)visible_count;
    for( size_t index = 0U; index < visible_count; ++index )
    {
        const rfscout_app_id_t app = visible[ index ];
        const int x = view.x + (int)index * tab_width;
        const int width =
            ( index + 1U == visible_count ) ? ( view.x + (int)view.width - x ) : tab_width;
        const bool active = app == app_manager_current();
        if( active )
        {
            display_fill_rect( x, view.y + 2, width, tab_height - 2, theme->selected );
        }
        if( index > 0U )
        {
            display_fill_rect( x, view.y + 6, 1, tab_height - 12, theme->divider );
        }

        const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( app );
        char label[ NAV_LABEL_MAX + 1U ];
        make_label( NULL != descriptor ? descriptor->name : NULL, label );
        const int text_width = (int)strlen( label ) * NAV_TEXT_CELL_WIDTH;
        const int text_x = x + ( width - text_width ) / 2;
        const int text_y = view.y + ( tab_height - 7 ) / 2;
        display_draw_text(
            text_x, text_y, label, active ? theme->text : theme->secondary, NAV_TEXT_SCALE );
    }

    if( view.height > (uint16_t)tab_height )
    {
        const int diagnostic_y = view.y + tab_height + 8;
        rfscout_touch_sample_t sample;
        if( touch_sample_get( &sample ) && sample.pressed )
        {
            char coordinates[ 24 ];
            snprintf( coordinates,
                      sizeof( coordinates ),
                      "TOUCH %u,%u",
                      (unsigned)( sample.calibrated ? sample.x : sample.reference_x ),
                      (unsigned)( sample.calibrated ? sample.y : sample.reference_y ) );
            display_draw_text( view.x + 8, diagnostic_y, coordinates, theme->info, 1U );
        }
        else
        {
            display_draw_text( view.x + 8,
                               diagnostic_y,
                               touch_calibration_valid() ? "TOUCH READY" : "TOUCH UNCALIBRATED",
                               touch_calibration_valid() ? theme->secondary : theme->warning,
                               1U );
        }
    }
}
