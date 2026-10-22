/**
    apps/network_common/network_credentials.c : Shared saved-network credential screens
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "network_credentials.h"

#include "app_session.h"
#include "app_text_entry.h"
#include "display.h"
#include "saved_networks.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_menu.h"
#include "ui_theme.h"

#include "esp_err.h"

#include <stdio.h>
#include <string.h>

#define CREDENTIALS_INFO_Y 26
#define CREDENTIALS_PASSWORD_Y 42
#define CREDENTIALS_PROFILE_ROWS_Y 62
#define CREDENTIALS_PICK_ROWS_Y 44
#define CREDENTIALS_VISIBLE_ROWS 5U
#define CREDENTIALS_LABEL_LENGTH ( SAVED_NETWORK_SSID_MAX_LENGTH + 6U )
#define CREDENTIALS_ERROR_LENGTH 48U

typedef enum
{
    CREDENTIALS_CLOSED = 0,
    CREDENTIALS_PROFILE,
    CREDENTIALS_PICK,
    CREDENTIALS_ENTRY,
} credentials_screen_t;

typedef enum
{
    PROFILE_ROW_BACK = 0,
    PROFILE_ROW_SHOW,
    PROFILE_ROW_EDIT,
    PROFILE_ROW_FORGET,
    PROFILE_ROW_COUNT,
} profile_row_t;

static credentials_screen_t screen;
static credentials_screen_t entry_return;
static bool pick_for_add;
static unsigned selected;
static char ssid[ SAVED_NETWORK_SSID_MAX_LENGTH + 1U ];
/** Profile being viewed or replaced; 0 while entering a new profile. */
static uint32_t profile_id;
static saved_network_profile_t profile;
/** Same-SSID candidates for the picker, in app-session memory. */
static saved_network_profile_t *candidates;
static size_t candidate_count;
static char password[ SAVED_NETWORK_PASSWORD_MAX_LENGTH + 1U ];
static bool password_shown;
static rfscout_text_entry_t entry;
static char error_text[ CREDENTIALS_ERROR_LENGTH ];

void network_credentials_label(
    const char *a_ssid, uint8_t a_ordinal, bool a_shared, char *a_output, size_t a_capacity )
{
    if( a_shared )
    {
        snprintf( a_output, a_capacity, "%s #%u", a_ssid, (unsigned)a_ordinal );
    }
    else
    {
        snprintf( a_output, a_capacity, "%s", a_ssid );
    }
}

static void set_error( esp_err_t a_error )
{
    if( ESP_OK == a_error )
    {
        error_text[ 0 ] = '\0';
    }
    else
    {
        snprintf( error_text, sizeof( error_text ), "Not saved: %s", esp_err_to_name( a_error ) );
    }
}

static void hide_password( void )
{
    memset( password, 0, sizeof( password ) );
    password_shown = false;
}

static void release_candidates( void )
{
    if( NULL != candidates )
    {
        memset( candidates, 0, SAVED_NETWORK_MAX_COUNT * sizeof( *candidates ) );
        (void)app_session_free( candidates );
    }
    candidates = NULL;
    candidate_count = 0U;
}

void network_credentials_close( void )
{
    hide_password();
    app_text_entry_clear( &entry );
    release_candidates();
    memset( &profile, 0, sizeof( profile ) );
    screen = CREDENTIALS_CLOSED;
    profile_id = 0U;
    error_text[ 0 ] = '\0';
}

static void begin_entry( uint32_t a_profile_id, credentials_screen_t a_return )
{
    profile_id = a_profile_id;
    entry_return = a_return;
    app_text_entry_begin( &entry, NULL, SAVED_NETWORK_PASSWORD_MAX_LENGTH );
    screen = CREDENTIALS_ENTRY;
}

/** Load same-SSID candidates; a_auth_mode < 0 accepts any auth mode. */
static esp_err_t load_candidates( int32_t a_auth_mode, bool a_filter_auth )
{
    release_candidates();
    candidates = app_session_calloc( SAVED_NETWORK_MAX_COUNT, sizeof( *candidates ) );
    if( NULL == candidates )
    {
        return ESP_ERR_NO_MEM;
    }
    size_t count = 0U;
    const esp_err_t result = saved_networks_list( candidates, SAVED_NETWORK_MAX_COUNT, &count );
    for( size_t index = 0U; ESP_OK == result && index < count; ++index )
    {
        if( 0 != strcmp( candidates[ index ].ssid, ssid ) ||
            ( a_filter_auth &&
              !saved_network_profile_auth_compatible( &candidates[ index ], a_auth_mode ) ) )
        {
            continue;
        }
        candidates[ candidate_count++ ] = candidates[ index ];
    }
    return result;
}

void network_credentials_open_profile( uint32_t a_profile_id )
{
    network_credentials_close();
    const esp_err_t result = saved_networks_get( a_profile_id, &profile );
    set_error( result );
    profile_id = ESP_OK == result ? a_profile_id : 0U;
    snprintf( ssid, sizeof( ssid ), "%s", profile.ssid );
    selected = PROFILE_ROW_BACK;
    screen = CREDENTIALS_PROFILE;
}

void network_credentials_open_add( const char *a_ssid )
{
    network_credentials_close();
    snprintf( ssid, sizeof( ssid ), "%s", NULL != a_ssid ? a_ssid : "" );
    set_error( load_candidates( 0, false ) );
    if( 0U == candidate_count )
    {
        release_candidates();
        begin_entry( 0U, CREDENTIALS_CLOSED );
        return;
    }
    pick_for_add = true;
    selected = 0U;
    screen = CREDENTIALS_PICK;
}

void network_credentials_open_pick( const char *a_ssid, int32_t a_auth_mode )
{
    network_credentials_close();
    snprintf( ssid, sizeof( ssid ), "%s", NULL != a_ssid ? a_ssid : "" );
    set_error( load_candidates( a_auth_mode, true ) );
    pick_for_add = false;
    selected = 0U;
    screen = CREDENTIALS_PICK;
}

bool network_credentials_active( void )
{
    return CREDENTIALS_CLOSED != screen;
}

static unsigned pick_row_count( void )
{
    return 1U + (unsigned)candidate_count + ( pick_for_add ? 1U : 0U );
}

static unsigned row_count( void )
{
    return CREDENTIALS_PROFILE == screen ? PROFILE_ROW_COUNT : pick_row_count();
}

static void draw_footer( const char *a_hint )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool failed = '\0' != error_text[ 0 ];
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       failed ? error_text : a_hint,
                       failed ? theme->error : theme->secondary,
                       1U );
}

static void render_profile( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    char label[ CREDENTIALS_LABEL_LENGTH ];
    network_credentials_label(
        profile.ssid, profile.ordinal, profile.ordinal > 1U, label, sizeof( label ) );
    display_draw_text( UI_LAYOUT_LEFT_X, CREDENTIALS_INFO_Y, label, theme->accent, 1U );

    char masked[ SAVED_NETWORK_PASSWORD_MAX_LENGTH + 1U ] = "<none>";
    if( password_shown )
    {
        snprintf( masked, sizeof( masked ), "%s", '\0' != password[ 0 ] ? password : "<none>" );
    }
    else if( profile.has_password )
    {
        snprintf( masked, sizeof( masked ), "********" );
    }
    display_draw_text( UI_LAYOUT_LEFT_X, CREDENTIALS_PASSWORD_Y, "Password", theme->secondary, 1U );
    display_draw_text( 78, CREDENTIALS_PASSWORD_Y, masked, theme->text, 1U );

    const ui_menu_row_t rows[ PROFILE_ROW_COUNT ] = {
        { .label = "Back", .state.available = true },
        { .label = password_shown ? "Hide password" : "Show password",
          .state.available = profile.has_password },
        { .label = "Edit password", .state.available = 0U != profile_id },
        { .label = "Forget", .state.available = 0U != profile_id },
    };
    (void)ui_menu_draw_rows(
        rows, PROFILE_ROW_COUNT, selected, CREDENTIALS_VISIBLE_ROWS, CREDENTIALS_PROFILE_ROWS_Y );
    draw_footer( "TURN SELECT / CENTER USE" );
}

static void render_pick( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_draw_text( UI_LAYOUT_LEFT_X,
                       CREDENTIALS_INFO_Y,
                       pick_for_add ? "Replace a profile or add one:" : "Join using profile:",
                       theme->secondary,
                       1U );

    static char labels[ SAVED_NETWORK_MAX_COUNT ][ CREDENTIALS_LABEL_LENGTH ];
    ui_menu_row_t rows[ SAVED_NETWORK_MAX_COUNT + 2U ] = {
        { .label = "Back", .state.available = true } };
    unsigned count = 1U;
    for( size_t index = 0U; index < candidate_count; ++index )
    {
        network_credentials_label( candidates[ index ].ssid,
                                   candidates[ index ].ordinal,
                                   true,
                                   labels[ index ],
                                   sizeof( labels[ 0 ] ) );
        rows[ count++ ] = ( ui_menu_row_t ){ .label = labels[ index ], .state.available = true };
    }
    if( pick_for_add )
    {
        rows[ count++ ] = ( ui_menu_row_t ){ .label = "New profile", .state.available = true };
    }
    (void)ui_menu_draw_rows(
        rows, count, selected, CREDENTIALS_VISIBLE_ROWS, CREDENTIALS_PICK_ROWS_Y );
    draw_footer( 0U == candidate_count ? "No compatible saved profile"
                                       : "TURN SELECT / CENTER USE" );
}

static void render_entry( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_draw_text( UI_LAYOUT_LEFT_X, 31, ssid, theme->accent, 1U );

    char masked[ 33 ];
    const size_t shown =
        entry.length < sizeof( masked ) - 1U ? entry.length : sizeof( masked ) - 1U;
    memset( masked, '*', shown );
    masked[ shown ] = '\0';
    display_draw_text( UI_LAYOUT_LEFT_X, 58, "Password", theme->text, 1U );
    display_draw_text( 90, 58, masked, theme->secondary, 1U );

    char candidate[ 32 ];
    const rfscout_text_entry_selection_t selection = app_text_entry_selection( &entry );
    if( RFSCOUT_TEXT_ENTRY_SELECTION_DONE == selection )
    {
        snprintf( candidate, sizeof( candidate ), "Action: Done" );
    }
    else if( RFSCOUT_TEXT_ENTRY_SELECTION_BACKSPACE == selection )
    {
        snprintf( candidate, sizeof( candidate ), "Action: Backspace" );
    }
    else
    {
        const char character = app_text_entry_character( &entry );
        snprintf(
            candidate, sizeof( candidate ), "Character: %c", ' ' == character ? '_' : character );
    }
    display_draw_text( UI_LAYOUT_LEFT_X, 84, candidate, theme->selected, 1U );
    draw_footer( "TURN SELECT / CENTER USE / LONG PRESS CANCEL" );
}

void network_credentials_render( const char *a_title )
{
    if( CREDENTIALS_CLOSED == screen )
    {
        return;
    }
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( CREDENTIALS_ENTRY == screen ? "WI-FI PASSWORD" : a_title, theme->secondary );
    if( CREDENTIALS_PROFILE == screen )
    {
        render_profile();
    }
    else if( CREDENTIALS_PICK == screen )
    {
        render_pick();
    }
    else
    {
        render_entry();
    }
    (void)display_present();
}

static bool finish( network_credentials_result_t a_result,
                    uint32_t a_profile_id,
                    network_credentials_outcome_t *a_outcome )
{
    if( NULL != a_outcome )
    {
        a_outcome->result = a_result;
        a_outcome->profile_id = a_profile_id;
    }
    network_credentials_close();
    return true;
}

static bool handle_entry( const rfscout_input_event_t *a_input,
                          network_credentials_outcome_t *a_outcome )
{
    const rfscout_text_entry_result_t result = app_text_entry_input( &entry, a_input );
    if( RFSCOUT_TEXT_ENTRY_CANCELLED == result )
    {
        app_text_entry_clear( &entry );
        error_text[ 0 ] = '\0';
        if( CREDENTIALS_CLOSED == entry_return )
        {
            return finish( NETWORK_CREDENTIALS_RESULT_NONE, 0U, a_outcome );
        }
        screen = entry_return;
        return false;
    }
    if( RFSCOUT_TEXT_ENTRY_ACCEPTED != result )
    {
        return false;
    }
    uint32_t saved_id = profile_id;
    const esp_err_t save_result = 0U == profile_id
                                      ? saved_networks_create( ssid, entry.value, &saved_id )
                                      : saved_networks_set_password( profile_id, entry.value );
    app_text_entry_clear( &entry );
    if( ESP_OK == save_result )
    {
        return finish( NETWORK_CREDENTIALS_RESULT_SAVED, saved_id, a_outcome );
    }
    /* Keep the screen open so a failed save is visible and can be retried or cancelled. */
    app_text_entry_begin( &entry, NULL, SAVED_NETWORK_PASSWORD_MAX_LENGTH );
    set_error( save_result );
    return false;
}

static bool handle_profile_select( network_credentials_outcome_t *a_outcome )
{
    if( PROFILE_ROW_BACK == selected )
    {
        return finish( NETWORK_CREDENTIALS_RESULT_NONE, 0U, a_outcome );
    }
    if( 0U == profile_id )
    {
        return false;
    }
    if( PROFILE_ROW_SHOW == selected && profile.has_password )
    {
        if( password_shown )
        {
            hide_password();
        }
        else
        {
            const esp_err_t result =
                saved_networks_password( profile_id, password, sizeof( password ) );
            password_shown = ESP_OK == result;
            set_error( result );
        }
    }
    else if( PROFILE_ROW_EDIT == selected )
    {
        hide_password();
        begin_entry( profile_id, CREDENTIALS_PROFILE );
    }
    else if( PROFILE_ROW_FORGET == selected )
    {
        const uint32_t forgotten = profile_id;
        const esp_err_t result = saved_networks_forget( forgotten );
        if( ESP_OK == result )
        {
            return finish( NETWORK_CREDENTIALS_RESULT_FORGOTTEN, forgotten, a_outcome );
        }
        set_error( result );
    }
    return false;
}

static bool handle_pick_select( network_credentials_outcome_t *a_outcome )
{
    if( 0U == selected )
    {
        return finish( NETWORK_CREDENTIALS_RESULT_NONE, 0U, a_outcome );
    }
    const size_t index = selected - 1U;
    if( index < candidate_count )
    {
        const uint32_t chosen = candidates[ index ].profile_id;
        if( !pick_for_add )
        {
            return finish( NETWORK_CREDENTIALS_RESULT_PICKED, chosen, a_outcome );
        }
        begin_entry( chosen, CREDENTIALS_PICK );
        return false;
    }
    if( pick_for_add )
    {
        begin_entry( 0U, CREDENTIALS_PICK );
    }
    return false;
}

bool network_credentials_input( const rfscout_input_event_t *a_input,
                                network_credentials_outcome_t *a_outcome )
{
    if( NULL == a_input || CREDENTIALS_CLOSED == screen )
    {
        return false;
    }
    if( CREDENTIALS_ENTRY == screen )
    {
        return handle_entry( a_input, a_outcome );
    }
    if( RFSCOUT_INPUT_NAVIGATION == a_input->type )
    {
        const unsigned count = row_count();
        selected =
            a_input->value > 0 ? ( selected + 1U ) % count : ( selected + count - 1U ) % count;
        return false;
    }
    if( RFSCOUT_INPUT_SELECT != a_input->type )
    {
        return false;
    }
    error_text[ 0 ] = '\0';
    return CREDENTIALS_PROFILE == screen ? handle_profile_select( a_outcome )
                                         : handle_pick_select( a_outcome );
}
