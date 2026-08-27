/**
    audio_spectrum.c : Recorder compatibility adapter for the former Audio app
    Description: Removes FFT processing and forwards lifecycle/selection state to Recorder.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_spectrum.h"

#include "recorder.h"
#include "storage.h"

static size_t selected_recording;

esp_err_t audio_spectrum_start( void )
{
    selected_recording = 0U;
    return recorder_init();
}

void audio_spectrum_stop( void )
{
    recorder_stop();
    selected_recording = 0U;
}

bool audio_spectrum_is_running( void )
{
    return storage_is_available();
}

bool audio_spectrum_take_frame( uint8_t *a_current, uint8_t *a_peak, size_t a_count )
{
    if( NULL != a_current && a_count > 0U )
    {
        a_current[ 0 ] = 0U;
    }
    if( NULL != a_peak && a_count > 0U )
    {
        a_peak[ 0 ] = 0U;
    }
    return false;
}

void audio_spectrum_set_range( audio_range_t a_range )
{
    (void)a_range;
}

audio_range_t audio_spectrum_range( void )
{
    return AUDIO_RANGE_RECORDER;
}

const char *audio_spectrum_range_name( audio_range_t a_range )
{
    (void)a_range;
    return "RECORDER";
}

void audio_spectrum_set_channel( audio_channel_t a_channel )
{
    const size_t count = recorder_file_count();
    if( 0U == count )
    {
        selected_recording = 0U;
        return;
    }

    selected_recording = a_channel % count;
}

audio_channel_t audio_spectrum_channel( void )
{
    const size_t count = recorder_file_count();
    if( 0U == count )
    {
        selected_recording = 0U;
    }
    else if( selected_recording >= count )
    {
        selected_recording = count - 1U;
    }
    return selected_recording;
}

const char *audio_spectrum_channel_name( audio_channel_t a_channel )
{
    const rfscout_recording_t *recording = recorder_file( a_channel );
    return ( NULL != recording ) ? recording->name : "NO FILES";
}

uint32_t audio_spectrum_low_hz( void )
{
    return 0U;
}

uint32_t audio_spectrum_high_hz( void )
{
    return 0U;
}

uint32_t audio_spectrum_effective_sample_rate( void )
{
    return RFSCOUT_RECORDER_SAMPLE_RATE;
}

size_t audio_spectrum_display_bin_count( void )
{
    return 0U;
}

int audio_spectrum_scale_floor_db( void )
{
    return 0;
}

int audio_spectrum_scale_ceiling_db( void )
{
    return 0;
}

uint32_t audio_spectrum_overruns( void )
{
    return 0U;
}
