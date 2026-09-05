/**
    notification_settings_view.h : RFScout application notification settings view
    Description: Renders per-app notification enablement and Morse mappings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NOTIFICATION_SETTINGS_VIEW_H_
#define _RFSCOUT_NOTIFICATION_SETTINGS_VIEW_H_

#include "app_descriptor.h"

#include <stdbool.h>

/** @brief Render one application's notification configuration page. */
void notification_settings_view_render( rfscout_app_id_t a_app,
                                        unsigned a_selected,
                                        bool a_editing );

#endif // _RFSCOUT_NOTIFICATION_SETTINGS_VIEW_H_
