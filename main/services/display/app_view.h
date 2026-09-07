/**
    app_view.h : RFScout managed application view
    Description: Defines app presentation geometry and standardized session lifecycle traits.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_VIEW_H_
#define _RFSCOUT_APP_VIEW_H_

#include "esp_err.h"

#include <stdint.h>

typedef struct rfscout_view rfscout_view_t;
typedef esp_err_t ( *rfscout_view_init_fn_t )( const rfscout_view_t *a_view );
typedef void ( *rfscout_view_deinit_fn_t )( const rfscout_view_t *a_view );

struct rfscout_view
{
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    rfscout_view_init_fn_t init;
    rfscout_view_deinit_fn_t deinit;
};

#endif // _RFSCOUT_APP_VIEW_H_
