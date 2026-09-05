/**
    recorder_view.h : RFScout Recorder application UI
    Description: Renders recording, playback, and the SD-backed recording browser.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RECORDER_VIEW_H_
#define _RFSCOUT_RECORDER_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

void recorder_view_landing( const char *a_playback_name, uint8_t a_playback_volume );
void recorder_view_browser( unsigned a_selected,
                            bool a_delete_mode,
                            bool a_delete_confirm,
                            const char *a_playback_name,
                            uint8_t a_playback_volume );

#endif // _RFSCOUT_RECORDER_VIEW_H_
