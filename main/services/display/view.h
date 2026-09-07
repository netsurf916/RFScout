/**
    view.h : RFScout application view geometry
    Description: Defines the board-agnostic rectangle exposed to an application runtime.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_VIEW_H_
#define _RFSCOUT_VIEW_H_

#include <stdint.h>

typedef struct
{
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} rfscout_view_t;

#endif // _RFSCOUT_VIEW_H_
