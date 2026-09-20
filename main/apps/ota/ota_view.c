/**
    apps/ota/ota_view.c : OTA Update application baseline presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_app_desc.h"

#include <stddef.h>
#include <stdio.h>

void ota_view_render( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const esp_app_desc_t *app = esp_app_get_description();
    char version[ 40 ];
    snprintf( version, sizeof( version ), "%s", NULL != app ? app->version : "unknown" );

    display_clear( theme->background );
    ui_chrome_draw( "OTA UPDATE", theme->secondary );

    display_draw_text( UI_LAYOUT_LABEL_X, 48, "Current", theme->text, 1U );
    display_draw_text( 180, 48, version, theme->accent, 1U );

    display_draw_text( UI_LAYOUT_LABEL_X, 75, "Network", theme->text, 1U );
    display_draw_text( 180, 75, "Disconnected", theme->secondary, 1U );

    display_draw_text( UI_LAYOUT_LABEL_X, 102, "Update", theme->text, 1U );
    display_draw_text( 180, 102, "Not checked", theme->secondary, 1U );

    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: APPS", theme->secondary, 1U );
    (void)display_present();
}
