/**
    storage_view.c : RFScout Home storage management view
    Description: Shows internal and SD storage use and clears owner-scoped records and media collections.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "storage_view.h"

#include "app_framework.h"
#include "device_config.h"
#include "display.h"
#include "event.h"
#include "log_writer.h"
#include "media_store.h"
#include "persistent_store.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_menu.h"
#include "ui_theme.h"

#include "esp_log.h"
#include "esp_system.h"

#include <stdio.h>
#include <string.h>

#define STORAGE_ROWS_MAX 32U
#define STORAGE_LABEL_MAX 30U
#define STORAGE_CLEAR_STEP_FILES 4U
#define STORAGE_LOG_SHUTDOWN_TIMEOUT_MS 3000U

typedef enum
{
    ROW_BACK = 0,
    ROW_HEADER,
    ROW_RECORDS,
    ROW_COLLECTION
} storage_row_kind_t;

typedef struct
{
    storage_row_kind_t kind;
    persistent_scope_t scope;
    rfscout_media_collection_t collection;
    char label[ STORAGE_LABEL_MAX + 1U ];
    char value[ 20 ];
    uint32_t count;
    uint64_t bytes;
} storage_row_t;

typedef enum
{
    STATE_LIST = 0,
    STATE_CONFIRM,
    STATE_CLEARING
} storage_state_t;

static const char *TAG = "storage_view";
static storage_row_t rows[ STORAGE_ROWS_MAX ];
static unsigned row_count;
static unsigned selected;
static storage_state_t state;
static bool confirm_delete;
static uint32_t cleared_files;
static char status[ 48 ];

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_DEVICE, a_value );
}

/** @brief Format bytes as B, K, M, or G with one decimal, without floating-point printf. */
static void format_bytes( uint64_t a_bytes, char *a_output, size_t a_capacity )
{
    static const char UNITS[] = "KMG";
    if( a_bytes < 1024U )
    {
        snprintf( a_output, a_capacity, "%uB", (unsigned)a_bytes );
        return;
    }
    uint64_t unit = 1024U;
    size_t index = 0U;
    while( index + 1U < sizeof( UNITS ) - 1U && a_bytes >= unit * 1024U )
    {
        unit *= 1024U;
        ++index;
    }
    const uint64_t tenths = a_bytes * 10U / unit;
    snprintf( a_output,
              a_capacity,
              "%u.%u%c",
              (unsigned)( tenths / 10U ),
              (unsigned)( tenths % 10U ),
              UNITS[ index ] );
}

static storage_row_t *add_row( storage_row_kind_t a_kind, const char *a_label )
{
    if( row_count >= STORAGE_ROWS_MAX )
    {
        return NULL;
    }
    storage_row_t *row = &rows[ row_count++ ];
    memset( row, 0, sizeof( *row ) );
    row->kind = a_kind;
    strlcpy( row->label, a_label, sizeof( row->label ) );
    return row;
}

static void set_usage_value( storage_row_t *a_row )
{
    char size[ 10 ];
    format_bytes( a_row->bytes, size, sizeof( size ) );
    snprintf( a_row->value, sizeof( a_row->value ), "%u/%s", (unsigned)a_row->count, size );
}

static void
add_records_row( persistent_scope_t a_scope, rfscout_app_id_t a_app, const char *a_label )
{
    size_t records = 0U;
    size_t bytes = 0U;
    if( ESP_OK != persistent_store_usage( a_scope, a_app, &records, &bytes ) ||
        ( 0U == records && PERSISTENT_SCOPE_SYSTEM != a_scope ) )
    {
        return;
    }
    storage_row_t *row = add_row( ROW_RECORDS, a_label );
    if( NULL != row )
    {
        row->scope = a_scope;
        row->collection.app = a_app;
        row->count = (uint32_t)records;
        row->bytes = bytes;
        set_usage_value( row );
    }
}

static bool count_file( const rfscout_media_entry_t *a_entry, void *a_context )
{
    storage_row_t *row = a_context;
    ++row->count;
    row->bytes += a_entry->size_bytes;
    return true;
}

static bool add_collection_row( const rfscout_media_collection_t *a_collection, void *a_context )
{
    (void)a_context;
    const rfscout_app_descriptor_t *descriptor =
        a_collection->system ? NULL : app_framework_descriptor( a_collection->app );
    char label[ STORAGE_LABEL_MAX + 1U ];
    /* Bound each part so the label provably fits: owner name, a space, then the collection. */
    const int name_width = 14;
    const int collection_width = (int)STORAGE_LABEL_MAX - name_width - 1;
    snprintf( label,
              sizeof( label ),
              "%.*s %.*s",
              name_width,
              ( NULL != descriptor ) ? descriptor->name : "System",
              collection_width,
              a_collection->collection );
    storage_row_t *row = add_row( ROW_COLLECTION, label );
    if( NULL == row )
    {
        return false;
    }
    row->collection = *a_collection;
    if( ESP_OK != media_list( a_collection, count_file, row ) || 0U == row->count )
    {
        --row_count;
        return true;
    }
    set_usage_value( row );
    return true;
}

/** @brief Rebuild every row; SD presence is probed once here, never on a timer. */
static void scan( void )
{
    row_count = 0U;
    (void)add_row( ROW_BACK, "Back" );

    storage_row_t *header = add_row( ROW_HEADER, "INTERNAL" );
    size_t total = 0U;
    size_t used = 0U;
    if( ESP_OK == persistent_store_space( &total, &used ) )
    {
        char used_text[ 10 ];
        char total_text[ 10 ];
        format_bytes( used, used_text, sizeof( used_text ) );
        format_bytes( total, total_text, sizeof( total_text ) );
        snprintf( header->value, sizeof( header->value ), "%s/%s", used_text, total_text );
        add_records_row( PERSISTENT_SCOPE_SYSTEM, RFSCOUT_APP_COUNT, "System settings" );
        for( int app = 0; app < (int)RFSCOUT_APP_COUNT; ++app )
        {
            const rfscout_app_descriptor_t *descriptor =
                app_framework_descriptor( (rfscout_app_id_t)app );
            if( NULL != descriptor )
            {
                add_records_row( PERSISTENT_SCOPE_APP, (rfscout_app_id_t)app, descriptor->name );
            }
        }
        add_records_row( PERSISTENT_SCOPE_UNUSED, RFSCOUT_APP_COUNT, "Unused app data" );
        add_records_row( PERSISTENT_SCOPE_ALL, RFSCOUT_APP_COUNT, "Erase all internal" );
    }
    else
    {
        strlcpy( header->value, "Unavailable", sizeof( header->value ) );
    }

    header = add_row( ROW_HEADER, "SD CARD" );
    uint64_t sd_total = 0U;
    uint64_t sd_free = 0U;
    if( NULL == header )
    {
        return;
    }
    if( ESP_OK != media_store_probe() || ESP_OK != media_store_space( &sd_total, &sd_free ) )
    {
        strlcpy( header->value, "No card", sizeof( header->value ) );
        return;
    }
    char free_text[ 10 ];
    format_bytes( sd_free, free_text, sizeof( free_text ) );
    snprintf( header->value, sizeof( header->value ), "%s free", free_text );
    (void)media_collections( true, RFSCOUT_APP_COUNT, add_collection_row, NULL );
    for( int app = 0; app < (int)RFSCOUT_APP_COUNT; ++app )
    {
        (void)media_collections( false, (rfscout_app_id_t)app, add_collection_row, NULL );
    }
}

static bool row_selectable( unsigned a_index )
{
    return a_index < row_count && ROW_HEADER != rows[ a_index ].kind;
}

static void log_clear( const storage_row_t *a_row, esp_err_t a_result, uint32_t a_count )
{
    rfscout_log_record_t record;
    if( log_begin( &record, "storage" ) && log_str( &record, "clear", a_row->label ) &&
        log_uint( &record, "count", a_count ) &&
        log_str( &record, "result", esp_err_to_name( a_result ) ) )
    {
        (void)log_end( &record );
    }
}

/** @brief Draw the list through the shared menu rows; section headers are unavailable rows. */
static void draw_rows( void )
{
    ui_menu_row_t menu_rows[ STORAGE_ROWS_MAX ];
    for( unsigned index = 0U; index < row_count; ++index )
    {
        menu_rows[ index ] = ( ui_menu_row_t ){
            .label = rows[ index ].label,
            .value = rows[ index ].value,
            .state = { .available = ROW_HEADER != rows[ index ].kind },
        };
    }
    (void)ui_menu_draw_rows(
        menu_rows, row_count, selected, UI_LAYOUT_MENU_VISIBLE_ROWS, UI_LAYOUT_MENU_START_Y );
}

static void render_confirm( const ui_theme_palette_t *a_theme, const storage_row_t *a_row )
{
    const bool internal = ROW_RECORDS == a_row->kind;
    char size[ 10 ];
    char detail[ 48 ];
    format_bytes( a_row->bytes, size, sizeof( size ) );
    snprintf( detail,
              sizeof( detail ),
              "%u %s, %s",
              (unsigned)a_row->count,
              internal ? "records" : "files",
              size );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 42, internal ? "ERASE RECORDS?" : "DELETE FILES?", a_theme->error, 2U );
    display_draw_text( UI_LAYOUT_LEFT_X, 72, a_row->label, a_theme->text, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X, 88, detail, a_theme->text, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       104,
                       internal ? "Defaults apply; device restarts" : "Files in use are kept",
                       a_theme->secondary,
                       1U );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       124,
                       confirm_delete ? "  CANCEL   > DELETE" : "> CANCEL     DELETE",
                       confirm_delete ? a_theme->error : a_theme->selected,
                       1U );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       "TURN: CHOOSE  CENTER: CONFIRM",
                       a_theme->secondary,
                       1U );
}

void storage_view_render( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "STORAGE", theme->secondary );
    const storage_row_t *row = &rows[ selected ];

    if( STATE_CONFIRM == state )
    {
        render_confirm( theme, row );
    }
    else if( STATE_CLEARING == state )
    {
        char progress[ 32 ];
        snprintf( progress, sizeof( progress ), "%u files deleted", (unsigned)cleared_files );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "DELETING", theme->error, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X, 72, row->label, theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 88, progress, theme->text, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: STOP", theme->secondary, 1U );
    }
    else
    {
        draw_rows();
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           ( '\0' != status[ 0 ] )     ? status
                           : ( ROW_BACK == row->kind ) ? "TURN SELECT / CENTER BACK"
                                                       : "TURN SELECT / CENTER DELETE",
                           theme->secondary,
                           1U );
    }
}

void storage_view_enter( void )
{
    selected = 0U;
    state = STATE_LIST;
    status[ 0 ] = '\0';
    row_count = 0U;
    (void)add_row( ROW_BACK, "Back" );
    display_modal_show( "STORAGE", "Measuring..." );
    storage_view_render();
    scan();
    display_modal_clear();
    storage_view_render();
}

static void finish_clearing( const char *a_outcome, esp_err_t a_result, size_t a_kept )
{
    log_clear( &rows[ selected ], a_result, cleared_files );
    if( a_kept > 0U )
    {
        snprintf( status,
                  sizeof( status ),
                  "%s %u, kept %u in use",
                  a_outcome,
                  (unsigned)cleared_files,
                  (unsigned)a_kept );
    }
    else
    {
        snprintf( status, sizeof( status ), "%s %u files", a_outcome, (unsigned)cleared_files );
    }
    state = STATE_LIST;
    selected = 0U;
    scan();
}

static void erase_records( const storage_row_t *a_row )
{
    /* Persist pending setting changes first so a scoped erase loses nothing else. */
    (void)device_config_save_if_dirty();
    const esp_err_t result = persistent_store_clear( a_row->scope, a_row->collection.app );
    log_clear( a_row, result, a_row->count );
    display_modal_show( "STORAGE", "Restarting..." );
    storage_view_render();
    /* Owners hold loaded records in RAM; restarting is the only way they all reload defaults. */
    ESP_LOGW( TAG, "restarting after internal erase: %s", esp_err_to_name( result ) );
    (void)log_writer_shutdown( STORAGE_LOG_SHUTDOWN_TIMEOUT_MS );
    esp_restart();
}

bool storage_view_input( bool a_pressed, int a_delta )
{
    if( STATE_CLEARING == state )
    {
        if( a_pressed )
        {
            finish_clearing( "Stopped after", ESP_OK, 0U );
            storage_view_render();
        }
        return false;
    }
    if( STATE_CONFIRM == state )
    {
        if( 0 != a_delta )
        {
            confirm_delete = !confirm_delete;
            emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
        }
        else if( a_pressed && !confirm_delete )
        {
            state = STATE_LIST;
            emit_event( RFSCOUT_EVENT_BACK, 0 );
        }
        else if( a_pressed && ROW_RECORDS == rows[ selected ].kind )
        {
            erase_records( &rows[ selected ] );
        }
        else if( a_pressed )
        {
            state = STATE_CLEARING;
            cleared_files = 0U;
            emit_event( RFSCOUT_EVENT_ACTIVATE, 0 );
        }
        storage_view_render();
        return false;
    }

    if( a_pressed )
    {
        if( ROW_BACK == rows[ selected ].kind )
        {
            emit_event( RFSCOUT_EVENT_BACK, 0 );
            return true;
        }
        state = STATE_CONFIRM;
        confirm_delete = false;
        status[ 0 ] = '\0';
        emit_event( RFSCOUT_EVENT_ENTER, 1 );
        storage_view_render();
        return false;
    }
    if( 0 == a_delta || row_count < 2U )
    {
        return false;
    }
    do
    {
        selected = ( selected + ( ( a_delta > 0 ) ? 1U : row_count - 1U ) ) % row_count;
    } while( !row_selectable( selected ) );
    status[ 0 ] = '\0';
    emit_event( RFSCOUT_EVENT_NAVIGATE, ( a_delta > 0 ) ? 1 : -1 );
    storage_view_render();
    return false;
}

void storage_view_poll( void )
{
    if( STATE_CLEARING != state )
    {
        return;
    }
    size_t removed = 0U;
    size_t kept = 0U;
    const esp_err_t result =
        media_clear( &rows[ selected ].collection, STORAGE_CLEAR_STEP_FILES, &removed, &kept );
    cleared_files += (uint32_t)removed;
    if( ESP_OK != result )
    {
        finish_clearing( "Failed after", result, 0U );
    }
    else if( 0U == removed )
    {
        finish_clearing( "Deleted", ESP_OK, kept );
    }
    storage_view_render();
}
