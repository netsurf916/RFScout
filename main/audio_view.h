/**
    audio_view.h : RFScout microphone analyzer UI
    Description: Declares rendering entry points for the audio spectrum and settings views.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_AUDIO_VIEW_H_
#define _RFSCOUT_AUDIO_VIEW_H_

#include "audio_spectrum.h"

#include <stdbool.h>

/**
 * @brief Render the live audio spectrum.
 * @param a_current Current normalized spectrum bins.
 * @param a_peak Slow-decay peak-hold bins, or zeroed bins when peak hold is disabled.
 */
void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak );

/**
 * @brief Render the audio settings menu.
 * @param a_selected Selected menu row.
 * @param a_editing True while the selected setting is being adjusted.
 * @param a_peak_hold Current peak-hold enable state.
 */
void audio_view_menu( unsigned a_selected, bool a_editing, bool a_peak_hold );

#endif // _RFSCOUT_AUDIO_VIEW_H_
