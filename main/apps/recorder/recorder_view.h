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

void recorder_view_landing( void );
void recorder_view_browser( unsigned a_selected, bool a_delete_mode, bool a_delete_confirm );

#endif // _RFSCOUT_RECORDER_VIEW_H_
