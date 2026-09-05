/**
    audio_view.h : RFScout Recorder compatibility UI
    Description: Preserves Audio view entry points while rendering the replacement Recorder app.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_VIEW_H_
#define _RFSCOUT_AUDIO_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Render Recorder status; legacy spectrum buffers are ignored. */
void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak );

/** @brief Render and drive the Back/Record/File/Play Recorder menu. */
void audio_view_menu( unsigned a_selected, bool a_editing, bool a_unused );

#endif // _RFSCOUT_AUDIO_VIEW_H_
