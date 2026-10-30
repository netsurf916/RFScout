/**
    app_notification_menu.h : Shared application notification-menu controller
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_NOTIFICATION_MENU_H_
#define _RFSCOUT_APP_NOTIFICATION_MENU_H_

#include "app_descriptor.h"

#include <stdbool.h>

typedef void ( *rfscout_app_parent_render_fn )( void *a_context );

typedef struct
{
    rfscout_app_id_t app;
    unsigned item;
    bool active;
    bool editing;
    rfscout_app_parent_render_fn render_parent;
    void *render_context;
} rfscout_app_notification_menu_t;

/** @brief Initialize reusable notification-menu state for one app. */
void app_notification_menu_init( rfscout_app_notification_menu_t *a_menu,
                                 rfscout_app_id_t a_app,
                                 rfscout_app_parent_render_fn a_render_parent,
                                 void *a_render_context );

/** @brief Reset the menu to its inactive state without persisting configuration. */
void app_notification_menu_reset( rfscout_app_notification_menu_t *a_menu );

/** @brief Open and render the notification menu. */
void app_notification_menu_open( rfscout_app_notification_menu_t *a_menu );

/** @brief Render current notification-menu state in the framework render callback. */
void app_notification_menu_render( const rfscout_app_notification_menu_t *a_menu );

/** @brief Return whether the notification menu currently owns app input. */
bool app_notification_menu_active( const rfscout_app_notification_menu_t *a_menu );

/**
 * @brief Handle one semantic input event while the menu is active.
 * @return true when the event was consumed by the notification menu.
 */
bool app_notification_menu_input( rfscout_app_notification_menu_t *a_menu,
                                  const rfscout_input_event_t *a_input );

#endif // _RFSCOUT_APP_NOTIFICATION_MENU_H_
