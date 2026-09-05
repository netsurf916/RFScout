/**
    apps/recorder/app.c : Recorder application descriptor
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "recorder.h"
#include "storage.h"

static bool microphone_probed;
static esp_err_t microphone_result = ESP_ERR_NOT_FOUND;

static esp_err_t probe_recorder( void )
{
    if( !storage_is_available() || ( ESP_OK != storage_probe_card() ) )
    {
        return ESP_ERR_NOT_FOUND;
    }

    if( !microphone_probed )
    {
        microphone_result = recorder_probe_microphone();
        microphone_probed = true;
    }
    return microphone_result;
}

const rfscout_app_descriptor_t rfscout_recorder_app = {
    .id = RFSCOUT_APP_AUDIO,
    .name = "Recorder",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = probe_recorder,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_RECORDER,
};
