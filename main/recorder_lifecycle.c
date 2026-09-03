/**
    recorder_lifecycle.c : RFScout recorder lifecycle reachability helpers
    Description: Provides a conservative synchronous quiescence check for application transitions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "recorder.h"

#include "esp_err.h"

bool recorder_is_quiescent( void )
{
    const rfscout_recorder_state_t state = recorder_state();
    if( ( RFSCOUT_RECORDER_IDLE != state ) && ( RFSCOUT_RECORDER_NO_STORAGE != state ) )
    {
        return false;
    }

    /*
     * The microphone probe refuses to start while recorder-owned RX or worker state is still
     * reachable. Once entry is allowed it runs synchronously and releases its temporary channel
     * before returning. Probe/detection failures other than ESP_ERR_INVALID_STATE therefore still
     * prove that the recorder no longer owns a worker or microphone channel.
     */
    return ESP_ERR_INVALID_STATE != recorder_probe_microphone();
}
