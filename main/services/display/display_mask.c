/**
    display_mask.c : RFScout transparent one-bit UI glyph renderer
    Description: Draws compact masked overlays while preserving framebuffer pixels under clear bits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t display_draw_mask( int a_x,
                             int a_y,
                             int a_width,
                             int a_height,
                             const uint8_t *a_mask,
                             size_t a_mask_size,
                             uint16_t a_color )
{
    if( ( NULL == a_mask ) || ( a_width <= 0 ) || ( a_height <= 0 ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t row_bytes = ( (size_t)a_width + 7U ) / 8U;
    const size_t required = row_bytes * (size_t)a_height;
    if( a_mask_size < required )
    {
        return ESP_ERR_INVALID_ARG;
    }

    for( int y = 0; y < a_height; ++y )
    {
        const uint8_t *row = &a_mask[ (size_t)y * row_bytes ];
        int run_start = -1;

        for( int x = 0; x <= a_width; ++x )
        {
            bool set = false;
            if( x < a_width )
            {
                const uint8_t byte = row[ (size_t)x / 8U ];
                set = 0U != ( byte & (uint8_t)( 0x80U >> ( (unsigned)x % 8U ) ) );
            }

            if( set && ( run_start < 0 ) )
            {
                run_start = x;
            }
            else if( !set && ( run_start >= 0 ) )
            {
                display_fill_rect( a_x + run_start, a_y + y, x - run_start, 1, a_color );
                run_start = -1;
            }
        }
    }

    return ESP_OK;
}
