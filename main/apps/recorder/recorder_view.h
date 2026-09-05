/**
    recorder_view.h : RFScout recorder application UI
    Description: Renders SD availability, recording state, and validated WAV recordings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RECORDER_VIEW_H_
#define _RFSCOUT_RECORDER_VIEW_H_

#include <stddef.h>

/** @brief Render the recorder landing screen and active operation state. */
void recorder_view_render( size_t a_selected );

/** @brief Render the recording browser with one selected file. */
void recorder_view_files( size_t a_selected );

#endif // _RFSCOUT_RECORDER_VIEW_H_
