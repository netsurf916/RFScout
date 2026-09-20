/**
    display.c : RFScout ST7789 LCD implementation
    Description: Native ESP-IDF framebuffer driver for the runtime-selected ST7789 profile.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "board.h"
#include "board_spi.h"
#include "ui_theme.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define DISPLAY_PIXEL_COUNT ( RFSCOUT_DISPLAY_WIDTH * RFSCOUT_DISPLAY_HEIGHT )
#define DISPLAY_DMA_ROWS 10
#define DISPLAY_DMA_PIXEL_COUNT ( RFSCOUT_DISPLAY_WIDTH * DISPLAY_DMA_ROWS )
#define DISPLAY_ST7789_RESET_SETTLE_MS 120U
#define DISPLAY_ST7789_SLEEP_IN_SETTLE_MS 20U
#define DISPLAY_ST7789_SLEEP_OUT_SETTLE_MS 20U
#define DISPLAY_ST7789_DISPLAY_ON_SETTLE_MS 120U
#define DISPLAY_PIXEL_SHIFT_POSITION_COUNT 9U

typedef struct
{
    uint8_t command;
    uint8_t data[ 14 ];
    uint8_t length;
    uint16_t delay_ms;
} st7789_init_command_t;

/*
 * LILYGO's board-specific TFT_eSPI initialization values. ESP-IDF's generic
 * ST7789 provider intentionally covers only the controller's basic setup.
 */
static const st7789_init_command_t tdeck_st7789_init[] = {
    { 0x13, { 0 }, 0, 0 },
    { 0x3A, { 0x55 }, 1, 0 },
    { 0xB2, { 0x0C, 0x0C, 0x00, 0x33, 0x33 }, 5, 0 },
    { 0xB7, { 0x75 }, 1, 0 },
    { 0xBB, { 0x1A }, 1, 0 },
    { 0xC0, { 0x2C }, 1, 0 },
    { 0xC2, { 0x01 }, 1, 0 },
    { 0xC3, { 0x13 }, 1, 0 },
    { 0xC4, { 0x20 }, 1, 0 },
    { 0xC6, { 0x0F }, 1, 0 },
    { 0xD0, { 0xA4, 0xA1 }, 2, 0 },
    { 0xE0,
      { 0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30 },
      14,
      0 },
    { 0xE1,
      { 0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E },
      14,
      0 },
};

/*
 * T-Embed electrical/drive tuning from LILYGO's ST7789 setup. Transport
 * semantics remain owned by ESP-IDF: do not write RAMCTRL (0xB0), MADCTL,
 * or COLMOD here because those encode byte order, RGB order, and pixel format.
 */
static const st7789_init_command_t tembed_st7789_init[] = {
    { 0x13, { 0 }, 0, 0 },
    { 0xB6, { 0x0A, 0x82 }, 2, 0 },
    { 0xB2, { 0x0C, 0x0C, 0x00, 0x33, 0x33 }, 5, 0 },
    { 0xB7, { 0x35 }, 1, 0 },
    { 0xBB, { 0x28 }, 1, 0 },
    { 0xC0, { 0x0C }, 1, 0 },
    { 0xC2, { 0x01, 0xFF }, 2, 0 },
    { 0xC3, { 0x10 }, 1, 0 },
    { 0xC4, { 0x20 }, 1, 0 },
    { 0xC6, { 0x0F }, 1, 0 },
    { 0xD0, { 0xA4, 0xA1 }, 2, 0 },
    { 0xE0,
      { 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17 },
      14,
      0 },
    { 0xE1,
      { 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E },
      14,
      0 },
};

static const int8_t pixel_shift_positions[ DISPLAY_PIXEL_SHIFT_POSITION_COUNT ][ 2 ] = {
    { 0, 0 },
    { 1, 0 },
    { 1, 1 },
    { 0, 1 },
    { -1, 1 },
    { -1, 0 },
    { -1, -1 },
    { 0, -1 },
    { 1, -1 },
};

static const char *TAG = "display";
static esp_lcd_panel_handle_t panel;
static volatile bool transfer_done;
static uint16_t *framebuffer;
static uint16_t *dma_buffer;
static bool display_dirty = true;
static bool output_enabled;
static bool low_power_active;
static size_t pixel_shift_position;
static int8_t pixel_shift_x;
static int8_t pixel_shift_y;

/** @brief Compact 5x7 printable ASCII font used by the RFScout LCD UI. */
// clang-format off
static const uint8_t font[ 95 ][ 5 ] =
{
    {0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
    {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},{50,73,121,65,62},
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},{0,1,2,4,0},
    {32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{8,4,8,16,8}
};
// clang-format on

static bool transfer_complete( esp_lcd_panel_io_handle_t a_io,
                               esp_lcd_panel_io_event_data_t *a_event,
                               void *a_context )
{
    (void)a_io;
    (void)a_event;
    *(volatile bool *)a_context = true;
    return false;
}

static void wait_for_transfer( void )
{
    while( !transfer_done )
    {
    }
}

static esp_err_t apply_st7789_init_sequence( esp_lcd_panel_io_handle_t a_io,
                                             const st7789_init_command_t *a_sequence,
                                             size_t a_count )
{
    for( size_t index = 0U; index < a_count; ++index )
    {
        const st7789_init_command_t *entry = &a_sequence[ index ];
        const esp_err_t result = esp_lcd_panel_io_tx_param(
            a_io, entry->command, entry->length ? entry->data : NULL, entry->length );
        if( ESP_OK != result )
        {
            return result;
        }
        if( 0U != entry->delay_ms )
        {
            vTaskDelay( pdMS_TO_TICKS( entry->delay_ms ) );
        }
    }
    return ESP_OK;
}

static esp_err_t apply_board_st7789_init( esp_lcd_panel_io_handle_t a_io )
{
    switch( board_profile()->id )
    {
        case RFSCOUT_BOARD_TDECK_PLUS:
            return apply_st7789_init_sequence( a_io,
                                               tdeck_st7789_init,
                                               sizeof( tdeck_st7789_init ) /
                                                   sizeof( tdeck_st7789_init[ 0 ] ) );
        case RFSCOUT_BOARD_TEMBED_CC1101_PLUS:
            return apply_st7789_init_sequence( a_io,
                                               tembed_st7789_init,
                                               sizeof( tembed_st7789_init ) /
                                                   sizeof( tembed_st7789_init[ 0 ] ) );
        case RFSCOUT_BOARD_UNKNOWN:
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static void fill_dma_row( int a_destination_y, uint16_t *a_destination )
{
    const uint16_t background = ui_theme_get()->background;
    const int source_y = a_destination_y - pixel_shift_y;
    if( source_y < 0 || source_y >= RFSCOUT_DISPLAY_HEIGHT )
    {
        for( int x = 0; x < RFSCOUT_DISPLAY_WIDTH; ++x )
        {
            a_destination[ x ] = background;
        }
        return;
    }

    const uint16_t *source = &framebuffer[ (size_t)source_y * RFSCOUT_DISPLAY_WIDTH ];
    if( 0 == pixel_shift_x )
    {
        memcpy( a_destination, source, RFSCOUT_DISPLAY_WIDTH * sizeof( *a_destination ) );
        return;
    }

    if( pixel_shift_x > 0 )
    {
        const size_t offset = (size_t)pixel_shift_x;
        for( size_t x = 0U; x < offset; ++x )
        {
            a_destination[ x ] = background;
        }
        memcpy( &a_destination[ offset ],
                source,
                ( RFSCOUT_DISPLAY_WIDTH - offset ) * sizeof( *a_destination ) );
        return;
    }

    const size_t offset = (size_t)( -pixel_shift_x );
    memcpy( a_destination,
            &source[ offset ],
            ( RFSCOUT_DISPLAY_WIDTH - offset ) * sizeof( *a_destination ) );
    for( size_t x = RFSCOUT_DISPLAY_WIDTH - offset; x < RFSCOUT_DISPLAY_WIDTH; ++x )
    {
        a_destination[ x ] = background;
    }
}

static bool valid_rect( int a_x, int a_y, int a_width, int a_height )
{
    return ( a_x >= 0 ) && ( a_y >= 0 ) && ( a_width > 0 ) && ( a_height > 0 ) &&
           ( a_x + a_width <= RFSCOUT_DISPLAY_WIDTH ) &&
           ( a_y + a_height <= RFSCOUT_DISPLAY_HEIGHT );
}

esp_err_t display_set_output_enabled( bool a_enabled )
{
    if( NULL == panel )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( output_enabled == a_enabled )
    {
        if( a_enabled )
        {
            display_invalidate();
        }
        return ESP_OK;
    }

    const esp_err_t result = esp_lcd_panel_disp_on_off( panel, a_enabled );
    if( ESP_OK == result )
    {
        output_enabled = a_enabled;
        if( a_enabled )
        {
            display_invalidate();
        }
    }
    return result;
}

bool display_output_enabled( void )
{
    return output_enabled;
}

esp_err_t display_enter_low_power( void )
{
    if( NULL == panel )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( low_power_active )
    {
        return ESP_OK;
    }

    const esp_err_t output_result = display_set_output_enabled( false );
    if( ESP_OK != output_result )
    {
        return output_result;
    }

    const esp_err_t sleep_result = esp_lcd_panel_disp_sleep( panel, true );
    if( ESP_OK != sleep_result )
    {
        (void)display_set_output_enabled( true );
        return sleep_result;
    }

    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_SLEEP_IN_SETTLE_MS ) );
    low_power_active = true;
    return ESP_OK;
}

esp_err_t display_exit_low_power( void )
{
    if( NULL == panel )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( !low_power_active )
    {
        return display_set_output_enabled( true );
    }

    const esp_err_t sleep_result = esp_lcd_panel_disp_sleep( panel, false );
    if( ESP_OK != sleep_result )
    {
        return sleep_result;
    }

    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_SLEEP_OUT_SETTLE_MS ) );
    const esp_err_t output_result = display_set_output_enabled( true );
    if( ESP_OK != output_result )
    {
        (void)esp_lcd_panel_disp_sleep( panel, true );
        return output_result;
    }

    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_DISPLAY_ON_SETTLE_MS ) );
    low_power_active = false;
    display_invalidate();
    return ESP_OK;
}

bool display_low_power_active( void )
{
    return low_power_active;
}

void display_invalidate( void )
{
    display_dirty = true;
}

bool display_is_dirty( void )
{
    return display_dirty;
}

void display_mark_clean( void )
{
    display_dirty = false;
}

void display_pixel_shift_reset( void )
{
    if( 0U == pixel_shift_position )
    {
        return;
    }

    pixel_shift_position = 0U;
    pixel_shift_x = pixel_shift_positions[ 0 ][ 0 ];
    pixel_shift_y = pixel_shift_positions[ 0 ][ 1 ];
    display_invalidate();
}

void display_pixel_shift_advance( void )
{
    pixel_shift_position = ( pixel_shift_position + 1U ) % DISPLAY_PIXEL_SHIFT_POSITION_COUNT;
    pixel_shift_x = pixel_shift_positions[ pixel_shift_position ][ 0 ];
    pixel_shift_y = pixel_shift_positions[ pixel_shift_position ][ 1 ];
    display_invalidate();
}

void display_fill_rect_raw( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
{
    if( ( NULL == framebuffer ) || ( a_width <= 0 ) || ( a_height <= 0 ) )
    {
        return;
    }

    int x0 = ( a_x < 0 ) ? 0 : a_x;
    int y0 = ( a_y < 0 ) ? 0 : a_y;
    int x1 = a_x + a_width;
    int y1 = a_y + a_height;
    if( x1 > RFSCOUT_DISPLAY_WIDTH )
    {
        x1 = RFSCOUT_DISPLAY_WIDTH;
    }
    if( y1 > RFSCOUT_DISPLAY_HEIGHT )
    {
        y1 = RFSCOUT_DISPLAY_HEIGHT;
    }
    if( ( x0 >= x1 ) || ( y0 >= y1 ) )
    {
        return;
    }

    for( int y = y0; y < y1; ++y )
    {
        for( int x = x0; x < x1; ++x )
        {
            framebuffer[ ( y * RFSCOUT_DISPLAY_WIDTH ) + x ] = a_color;
        }
    }
    display_invalidate();
}

static void draw_character( int a_x, int a_y, char a_character, uint16_t a_color, unsigned a_scale )
{
    if( ( a_character < 32 ) || ( a_character > 126 ) )
    {
        a_character = '?';
    }
    if( 0U == a_scale )
    {
        a_scale = 1U;
    }

    const uint8_t *glyph = font[ (uint8_t)a_character - 32U ];
    for( int y = 0; y < 7; ++y )
    {
        for( int x = 0; x < 5; ++x )
        {
            if( 0 == ( glyph[ x ] & ( 1U << y ) ) )
            {
                continue;
            }
            display_fill_rect_raw( a_x + ( x * (int)a_scale ),
                                   a_y + ( y * (int)a_scale ),
                                   (int)a_scale,
                                   (int)a_scale,
                                   a_color );
        }
    }
}

esp_err_t display_init( void )
{
    display_dirty = true;
    output_enabled = false;
    low_power_active = false;
    pixel_shift_position = 0U;
    pixel_shift_x = 0;
    pixel_shift_y = 0;

    const esp_err_t bus_result = board_spi_init();
    if( ESP_OK != bus_result )
    {
        return bus_result;
    }

    gpio_config_t backlight = { .pin_bit_mask = 1ULL << RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN,
                                .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK( gpio_config( &backlight ) );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 0 );

    framebuffer = heap_caps_calloc(
        DISPLAY_PIXEL_COUNT, sizeof( *framebuffer ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == framebuffer )
    {
        return ESP_ERR_NO_MEM;
    }

    dma_buffer = heap_caps_malloc( DISPLAY_DMA_PIXEL_COUNT * sizeof( *dma_buffer ),
                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL );
    if( NULL == dma_buffer )
    {
        heap_caps_free( framebuffer );
        framebuffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = { .dc_gpio_num = RFSCOUT_BOARD_DISPLAY_DC_PIN,
                                                .cs_gpio_num = RFSCOUT_BOARD_DISPLAY_CS_PIN,
                                                .pclk_hz = 20000000,
                                                .lcd_cmd_bits = 8,
                                                .lcd_param_bits = 8,
                                                .spi_mode = 0,
                                                .trans_queue_depth = 10 };
    ESP_ERROR_CHECK( esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)RFSCOUT_BOARD_SPI_HOST, &io_config, &io ) );

    esp_lcd_panel_io_callbacks_t callbacks = { .on_color_trans_done = transfer_complete };
    ESP_ERROR_CHECK(
        esp_lcd_panel_io_register_event_callbacks( io, &callbacks, (void *)&transfer_done ) );

    esp_lcd_panel_dev_config_t panel_config = { .reset_gpio_num = RFSCOUT_BOARD_DISPLAY_RESET_PIN,
                                                .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                                .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
                                                .bits_per_pixel = 16 };
    ESP_ERROR_CHECK( esp_lcd_new_panel_st7789( io, &panel_config, &panel ) );
    ESP_ERROR_CHECK( esp_lcd_panel_reset( panel ) );
    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_RESET_SETTLE_MS ) );
    ESP_ERROR_CHECK( esp_lcd_panel_init( panel ) );
    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_RESET_SETTLE_MS ) );
    ESP_ERROR_CHECK( apply_board_st7789_init( io ) );
    ESP_ERROR_CHECK( esp_lcd_panel_swap_xy( panel, RFSCOUT_BOARD_DISPLAY_SWAP_XY ) );
    ESP_ERROR_CHECK( esp_lcd_panel_mirror(
        panel, RFSCOUT_BOARD_DISPLAY_MIRROR_X, RFSCOUT_BOARD_DISPLAY_MIRROR_Y ) );
    ESP_ERROR_CHECK(
        esp_lcd_panel_set_gap( panel, RFSCOUT_BOARD_DISPLAY_X_GAP, RFSCOUT_BOARD_DISPLAY_Y_GAP ) );
    ESP_ERROR_CHECK( esp_lcd_panel_invert_color( panel, RFSCOUT_BOARD_DISPLAY_INVERT_COLOR ) );
    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_DISPLAY_ON_SETTLE_MS ) );
    ESP_ERROR_CHECK( esp_lcd_panel_disp_on_off( panel, true ) );
    vTaskDelay( pdMS_TO_TICKS( DISPLAY_ST7789_DISPLAY_ON_SETTLE_MS ) );
    output_enabled = true;

    display_clear( 0x0000U );
    ESP_ERROR_CHECK( display_flush() );
    display_set_brightness( 100U );
    ESP_LOGI( TAG,
              "ST7789 LCD initialized at %dx%d (native %dx%d)",
              RFSCOUT_DISPLAY_WIDTH,
              RFSCOUT_DISPLAY_HEIGHT,
              RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH,
              RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT );
    return ESP_OK;
}

void display_clear( uint16_t a_color )
{
    if( NULL == framebuffer )
    {
        return;
    }

    const uint16_t color = ui_theme_resolve_legacy_color( a_color );
    for( size_t i = 0; i < DISPLAY_PIXEL_COUNT; ++i )
    {
        framebuffer[ i ] = color;
    }
    display_invalidate();
}

void display_fill_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
{
    display_fill_rect_raw( a_x, a_y, a_width, a_height, ui_theme_resolve_legacy_color( a_color ) );
}

void display_draw_text( int a_x, int a_y, const char *a_text, uint16_t a_color, unsigned a_scale )
{
    if( NULL == a_text )
    {
        return;
    }
    if( 0U == a_scale )
    {
        a_scale = 1U;
    }

    const uint16_t color = ui_theme_resolve_legacy_color( a_color );
    const int advance = 6 * (int)a_scale;
    while( '\0' != *a_text )
    {
        draw_character( a_x, a_y, *a_text++, color, a_scale );
        a_x += advance;
        if( a_x >= RFSCOUT_DISPLAY_WIDTH )
        {
            break;
        }
    }
}

esp_err_t display_read_rect(
    int a_x, int a_y, int a_width, int a_height, uint16_t *a_pixels, size_t a_pixel_capacity )
{
    const size_t required = (size_t)a_width * (size_t)a_height;
    if( ( NULL == framebuffer ) || ( NULL == a_pixels ) ||
        !valid_rect( a_x, a_y, a_width, a_height ) || ( a_pixel_capacity < required ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    for( int y = 0; y < a_height; ++y )
    {
        memcpy( &a_pixels[ (size_t)y * (size_t)a_width ],
                &framebuffer[ (size_t)( a_y + y ) * RFSCOUT_DISPLAY_WIDTH + (size_t)a_x ],
                (size_t)a_width * sizeof( *a_pixels ) );
    }
    return ESP_OK;
}

esp_err_t display_write_rect(
    int a_x, int a_y, int a_width, int a_height, const uint16_t *a_pixels, size_t a_pixel_count )
{
    const size_t required = (size_t)a_width * (size_t)a_height;
    if( ( NULL == framebuffer ) || ( NULL == a_pixels ) ||
        !valid_rect( a_x, a_y, a_width, a_height ) || ( a_pixel_count < required ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    for( int y = 0; y < a_height; ++y )
    {
        memcpy( &framebuffer[ (size_t)( a_y + y ) * RFSCOUT_DISPLAY_WIDTH + (size_t)a_x ],
                &a_pixels[ (size_t)y * (size_t)a_width ],
                (size_t)a_width * sizeof( *a_pixels ) );
    }
    display_invalidate();
    return ESP_OK;
}

esp_err_t display_flush( void )
{
    if( !display_dirty || !output_enabled )
    {
        return ESP_OK;
    }
    if( ( NULL == panel ) || ( NULL == framebuffer ) || ( NULL == dma_buffer ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    for( int y = 0; y < RFSCOUT_DISPLAY_HEIGHT; y += DISPLAY_DMA_ROWS )
    {
        int rows = DISPLAY_DMA_ROWS;
        if( y + rows > RFSCOUT_DISPLAY_HEIGHT )
        {
            rows = RFSCOUT_DISPLAY_HEIGHT - y;
        }

        for( int row = 0; row < rows; ++row )
        {
            fill_dma_row( y + row, &dma_buffer[ (size_t)row * RFSCOUT_DISPLAY_WIDTH ] );
        }

        transfer_done = false;
        const esp_err_t result =
            esp_lcd_panel_draw_bitmap( panel, 0, y, RFSCOUT_DISPLAY_WIDTH, y + rows, dma_buffer );
        if( ESP_OK != result )
        {
            return result;
        }
        wait_for_transfer();
    }

    display_mark_clean();
    return ESP_OK;
}