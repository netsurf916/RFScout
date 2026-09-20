/**
    firmware_identity.c : RFScout running firmware identity
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "firmware_identity.h"

#include "board.h"

#include "esp_app_desc.h"
#include "sdkconfig.h"

const rfscout_firmware_identity_t *firmware_identity( void )
{
    static rfscout_firmware_identity_t identity;
    const esp_app_desc_t *description = esp_app_get_description();
    const rfscout_board_profile_t *board = board_profile();

    identity.project = NULL != description ? description->project_name : "unknown";
    identity.version = NULL != description ? description->version : "unknown";
    identity.hardware_target =
        NULL != board && NULL != board->target_id ? board->target_id : "unknown";
    identity.idf_target = CONFIG_IDF_TARGET;
    identity.partition_layout = "ab-v1";
    return &identity;
}
