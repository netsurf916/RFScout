/**
    ota_boot.c : RFScout OTA first-boot confirmation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_boot.h"

#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "ota_boot";

esp_err_t ota_boot_confirm_if_pending( void )
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if( NULL == running )
    {
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_img_states_t state;
    const esp_err_t state_result = esp_ota_get_state_partition( running, &state );
    if( ESP_ERR_NOT_FOUND == state_result || ESP_ERR_NOT_SUPPORTED == state_result )
    {
        return ESP_OK;
    }
    if( ESP_OK != state_result )
    {
        return state_result;
    }
    if( ESP_OTA_IMG_PENDING_VERIFY != state )
    {
        return ESP_OK;
    }

    ESP_LOGI( TAG, "mandatory startup checks passed; confirming pending OTA image" );
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "could not confirm pending OTA image: %s", esp_err_to_name( result ) );
        return result;
    }

    ESP_LOGI( TAG, "pending OTA image marked valid" );
    return ESP_OK;
}

esp_err_t ota_boot_rollback_if_pending( void )
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if( NULL == running )
    {
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_img_states_t state;
    const esp_err_t state_result = esp_ota_get_state_partition( running, &state );
    if( ESP_ERR_NOT_FOUND == state_result || ESP_ERR_NOT_SUPPORTED == state_result )
    {
        return ESP_OK;
    }
    if( ESP_OK != state_result )
    {
        return state_result;
    }
    if( ESP_OTA_IMG_PENDING_VERIFY != state )
    {
        return ESP_OK;
    }

    ESP_LOGE( TAG, "mandatory startup failed; rolling back pending OTA image" );
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}
