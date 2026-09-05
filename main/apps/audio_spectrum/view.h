/**
    apps/audio_spectrum/view.h : RFScout live audio spectrum UI
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_SPECTRUM_VIEW_H_
#define _RFSCOUT_AUDIO_SPECTRUM_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

void audio_spectrum_view_render( uint8_t a_gain_db, bool a_settings );

#endif // _RFSCOUT_AUDIO_SPECTRUM_VIEW_H_
