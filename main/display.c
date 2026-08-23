/**
    display.c : RFScout ST7789 LCD implementation
    Description: Native ESP-IDF framebuffer driver for the T-Embed display.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "board.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define DISPLAY_SPI_HOST SPI2_HOST
#define DISPLAY_PIXEL_COUNT ( RFSCOUT_DISPLAY_WIDTH * RFSCOUT_DISPLAY_HEIGHT )

static const char *TAG = "display";
static esp_lcd_panel_handle_t panel;
static volatile bool transfer_done;
static uint16_t *framebuffer;

/** @brief Compact 5x7 printable ASCII font shared with Brandalize/OUI-SPY. */
static const uint8_t font[ 95 ][ 5 ] =
{
    {0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},
    {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},{50,73,121,65,62},
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},{0,1,2,4,0},
    {32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{8,4,8,16,8}
};

/** @brief Signal completion of a DMA-backed color transfer. */
static bool transfer_complete( esp_lcd_panel_io_handle_t a_io,
                               esp_lcd_panel_io_event_data_t *a_event,
                               void *a_context )
{
    (void)a_io;
    (void)a_event;
    *(volatile bool *)a_context = true;
    return false;
}

/** @brief Write one scaled glyph to the local framebuffer. */
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
            display_fill_rect( a_x + ( x * (int)a_scale ),
                               a_y + ( y * (int)a_scale ),
                               (int)a_scale, (int)a_scale, a_color );
        }
    }
}

esp_err_t display_init( void )
{
    gpio_config_t backlight =
    {
        .pin_bit_mask = 1ULL << RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN,
        .mode = GPIO_MODE_OUTPUT
    };
    ESP_ERROR_CHECK( gpio_config( &backlight ) );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 0 );

    framebuffer = heap_caps_calloc( DISPLAY_PIXEL_COUNT, sizeof( *framebuffer ), MALLOC_CAP_DMA );
    if( NULL == framebuffer )
    {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus =
    {
        .mosi_io_num = RFSCOUT_BOARD_SPI_MOSI_PIN,
        .miso_io_num = RFSCOUT_BOARD_SPI_MISO_PIN,
        .sclk_io_num = RFSCOUT_BOARD_SPI_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_PIXEL_COUNT * sizeof( *framebuffer )
    };
    ESP_ERROR_CHECK( spi_bus_initialize( DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO ) );

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config =
    {
        .dc_gpio_num = RFSCOUT_BOARD_DISPLAY_DC_PIN,
        .cs_gpio_num = RFSCOUT_BOARD_DISPLAY_CS_PIN,
        .pclk_hz = 20000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10
    };
    ESP_ERROR_CHECK( esp_lcd_new_panel_io_spi( (esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST, &io_config, &io ) );

    esp_lcd_panel_io_callbacks_t callbacks =
    {
        .on_color_trans_done = transfer_complete
    };
    ESP_ERROR_CHECK( esp_lcd_panel_io_register_event_callbacks( io, &callbacks, (void *)&transfer_done ) );

    esp_lcd_panel_dev_config_t panel_config =
    {
        .reset_gpio_num = RFSCOUT_BOARD_DISPLAY_RESET_PIN,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16
    };
    ESP_ERROR_CHECK( esp_lcd_new_panel_st7789( io, &panel_config, &panel ) );
    ESP_ERROR_CHECK( esp_lcd_panel_reset( panel ) );
    ESP_ERROR_CHECK( esp_lcd_panel_init( panel ) );
    ESP_ERROR_CHECK( esp_lcd_panel_swap_xy( panel, true ) );
    ESP_ERROR_CHECK( esp_lcd_panel_mirror( panel, true, false ) );
    ESP_ERROR_CHECK( esp_lcd_panel_set_gap( panel, RFSCOUT_BOARD_DISPLAY_X_GAP,
                                           RFSCOUT_BOARD_DISPLAY_Y_GAP ) );
    ESP_ERROR_CHECK( esp_lcd_panel_invert_color( panel, true ) );
    ESP_ERROR_CHECK( esp_lcd_panel_disp_on_off( panel, true ) );

    display_clear( 0x0000 );
    ESP_ERROR_CHECK( display_present() );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_BACKLIGHT_PIN, 1 );
    ESP_LOGI( TAG, "ST7789 LCD initialized at %dx%d", RFSCOUT_DISPLAY_WIDTH, RFSCOUT_DISPLAY_HEIGHT );
    return ESP_OK;
}

void display_clear( uint16_t a_color )
{
    if( NULL == framebuffer )
    {
        return;
    }
    for( size_t i = 0; i < DISPLAY_PIXEL_COUNT; ++i )
    {
        framebuffer[ i ] = a_color;
    }
}

void display_fill_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
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

    for( int y = y0; y < y1; ++y )
    {
        for( int x = x0; x < x1; ++x )
        {
            framebuffer[ ( y * RFSCOUT_DISPLAY_WIDTH ) + x ] = a_color;
        }
    }
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

    const int advance = 6 * (int)a_scale;
    while( '\0' != *a_text )
    {
        draw_character( a_x, a_y, *a_text++, a_color, a_scale );
        a_x += advance;
        if( a_x >= RFSCOUT_DISPLAY_WIDTH )
        {
            break;
        }
    }
}

esp_err_t display_present( void )
{
    if( ( NULL == panel ) || ( NULL == framebuffer ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    transfer_done = false;
    const esp_err_t result = esp_lcd_panel_draw_bitmap( panel, 0, 0,
                                                        RFSCOUT_DISPLAY_WIDTH,
                                                        RFSCOUT_DISPLAY_HEIGHT,
                                                        framebuffer );
    if( ESP_OK != result )
    {
        return result;
    }
    while( !transfer_done )
    {
        taskYIELD();
    }
    return ESP_OK;
}
