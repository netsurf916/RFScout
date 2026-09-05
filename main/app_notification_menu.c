/**
    app_notification_menu.c : Shared application notification-menu controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_notification_menu.h"

#include "device_config.h"
#include "event.h"
#include "morse.h"
#include "notification.h"
#include "notification_preferences.h"
#include "notification_settings_view.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define NOTIFICATION_MENU_BACK 0U
#define NOTIFICATION_MENU_ENABLED 1U
#define NOTIFICATION_MENU_FIRST_MAPPING 2U
#define NOTIFICATION_MENU_WIFI_EVENT_COUNT 2U

static const char *TAG = "app_notification_menu";

static void emit_event( const rfscout_app_notification_menu_t *a_menu,
                        rfscout_event_type_t a_type,
                        int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)a_menu->app, a_value );
}

static unsigned mapping_base( const rfscout_app_notification_menu_t *a_menu )
{
    return NOTIFICATION_MENU_FIRST_MAPPING +
           ( ( RFSCOUT_APP_WIFI_BT == a_menu->app ) ? NOTIFICATION_MENU_WIFI_EVENT_COUNT : 0U );
}

static char next_morse_character( char a_current, int a_direction )
{
    const char *characters = morse_supported_characters();
    const size_t count = strlen( characters );
    const char *current = strchr( characters, a_current );
    size_t index = ( NULL != current ) ? (size_t)( current - characters ) : 0U;
    index = ( a_direction > 0 ) ? ( index + 1U ) % count : ( index + count - 1U ) % count;
    return characters[ index ];
}

static void render_menu( const rfscout_app_notification_menu_t *a_menu )
{
    notification_settings_view_render( a_menu->app, a_menu->item, a_menu->editing );
}

static bool close_menu( rfscout_app_notification_menu_t *a_menu )
{
    const esp_err_t save_result = device_config_save( false, 0, NULL );
    if( ( ESP_OK != save_result ) && ( ESP_ERR_INVALID_STATE != save_result ) )
    {
        ESP_LOGW( TAG, "notification settings save failed: %s", esp_err_to_name( save_result ) );
        render_menu( a_menu );
        emit_event( a_menu, RFSCOUT_EVENT_ERROR, (int32_t)save_result );
        return false;
    }

    a_menu->active = false;
    a_menu->editing = false;
    a_menu->item = NOTIFICATION_MENU_BACK;
    if( NULL != a_menu->render_parent )
    {
        a_menu->render_parent( a_menu->render_context );
    }
    return true;
}

void app_notification_menu_init( rfscout_app_notification_menu_t *a_menu,
                                 rfscout_app_id_t a_app,
                                 rfscout_app_parent_render_fn a_render_parent,
                                 void *a_render_context )
{
    if( NULL == a_menu )
    {
        return;
    }
    a_menu->app = a_app;
    a_menu->render_parent = a_render_parent;
    a_menu->render_context = a_render_context;
    app_notification_menu_reset( a_menu );
}

void app_notification_menu_reset( rfscout_app_notification_menu_t *a_menu )
{
    if( NULL == a_menu )
    {
        return;
    }
    a_menu->item = NOTIFICATION_MENU_BACK;
    a_menu->active = false;
    a_menu->editing = false;
}

void app_notification_menu_open( rfscout_app_notification_menu_t *a_menu )
{
    if( NULL == a_menu )
    {
        return;
    }
    a_menu->active = true;
    a_menu->editing = false;
    a_menu->item = NOTIFICATION_MENU_BACK;
    render_menu( a_menu );
    emit_event( a_menu, RFSCOUT_EVENT_ENTER, 0 );
}

bool app_notification_menu_active( const rfscout_app_notification_menu_t *a_menu )
{
    return ( NULL != a_menu ) && a_menu->active;
}

bool app_notification_menu_input( rfscout_app_notification_menu_t *a_menu,
                                  const rfscout_input_event_t *a_input )
{
    if( ( NULL == a_menu ) || ( NULL == a_input ) || !a_menu->active )
    {
        return false;
    }

    if( RFSCOUT_INPUT_BACK == a_input->type )
    {
        if( a_menu->editing )
        {
            a_menu->editing = false;
            render_menu( a_menu );
            emit_event( a_menu, RFSCOUT_EVENT_BACK, 0 );
        }
        else if( close_menu( a_menu ) )
        {
            emit_event( a_menu, RFSCOUT_EVENT_BACK, 0 );
        }
        return true;
    }

    const size_t notification_count = notification_count_for_app( a_menu->app );
    const unsigned first_mapping = mapping_base( a_menu );
    const unsigned item_count = first_mapping + (unsigned)notification_count;
    if( item_count <= first_mapping )
    {
        (void)close_menu( a_menu );
        return true;
    }

    if( RFSCOUT_INPUT_SELECT == a_input->type )
    {
        if( a_menu->editing )
        {
            a_menu->editing = false;
            render_menu( a_menu );
            emit_event( a_menu, RFSCOUT_EVENT_ACTIVATE, 0 );
            return true;
        }
        if( NOTIFICATION_MENU_BACK == a_menu->item )
        {
            if( close_menu( a_menu ) )
            {
                emit_event( a_menu, RFSCOUT_EVENT_BACK, 0 );
            }
            return true;
        }
        if( NOTIFICATION_MENU_ENABLED == a_menu->item )
        {
            const bool old_value = notification_preferences_app_enabled( a_menu->app );
            if( ESP_OK == notification_preferences_set_app_enabled( a_menu->app, !old_value ) )
            {
                render_menu( a_menu );
                emit_event( a_menu,
                            !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                            !old_value ? 1 : 0 );
            }
            return true;
        }
        if( ( RFSCOUT_APP_WIFI_BT == a_menu->app ) &&
            ( a_menu->item >= NOTIFICATION_MENU_FIRST_MAPPING ) &&
            ( a_menu->item < first_mapping ) )
        {
            const rfscout_wifi_detection_event_t event =
                (rfscout_wifi_detection_event_t)( a_menu->item - NOTIFICATION_MENU_FIRST_MAPPING );
            const bool old_value = notification_preferences_wifi_detection_enabled( event );
            if( ESP_OK == notification_preferences_set_wifi_detection_enabled( event, !old_value ) )
            {
                render_menu( a_menu );
                emit_event( a_menu,
                            !old_value ? RFSCOUT_EVENT_ENABLE : RFSCOUT_EVENT_DISABLE,
                            !old_value ? 1 : 0 );
            }
            return true;
        }
        a_menu->editing = true;
        render_menu( a_menu );
        emit_event( a_menu, RFSCOUT_EVENT_ENTER, 0 );
        return true;
    }

    if( RFSCOUT_INPUT_NAVIGATION != a_input->type )
    {
        return true;
    }

    const int direction = ( a_input->value > 0 ) ? 1 : -1;
    if( a_menu->editing )
    {
        if( a_menu->item < first_mapping )
        {
            return true;
        }
        const rfscout_notification_descriptor_t *descriptor =
            notification_descriptor_for_app( a_menu->app, a_menu->item - first_mapping );
        if( NULL == descriptor )
        {
            return true;
        }
        const char old_character = device_config_notification_character( descriptor->type );
        const char new_character = next_morse_character( old_character, direction );
        if( ( new_character != old_character ) &&
            ( ESP_OK ==
              device_config_set_notification_character( descriptor->type, new_character ) ) )
        {
            render_menu( a_menu );
            emit_event( a_menu, RFSCOUT_EVENT_NAVIGATE, direction );
        }
        return true;
    }

    int item = (int)a_menu->item + direction;
    if( item < 0 )
    {
        item = (int)item_count - 1;
    }
    if( item >= (int)item_count )
    {
        item = 0;
    }
    if( (unsigned)item != a_menu->item )
    {
        a_menu->item = (unsigned)item;
        render_menu( a_menu );
        emit_event( a_menu, RFSCOUT_EVENT_NAVIGATE, direction );
    }
    return true;
}
