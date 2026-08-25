/** audio_view.h : RFScout microphone analyzer UI */
#pragma once
#ifndef _RFSCOUT_AUDIO_VIEW_H_
#define _RFSCOUT_AUDIO_VIEW_H_
#include "audio_spectrum.h"
#include <stdbool.h>
void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak );
void audio_view_menu( unsigned a_selected, bool a_editing, bool a_peak_hold );
#endif
