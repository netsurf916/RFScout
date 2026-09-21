/**
    firmware_identity.c : RFScout running firmware identity
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "firmware_identity.h"

#include "board.h"

#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#include <string.h>

#define FIRMWARE_HASH_READ_CHUNK 4096U

static uint8_t hash_read_buffer[ FIRMWARE_HASH_READ_CHUNK ];

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

esp_err_t firmware_running_image_sha256( uint8_t a_digest[ RFSCOUT_FIRMWARE_SHA256_LENGTH ],
                                         size_t *a_image_length )
{
    if( NULL == a_digest || NULL == a_image_length )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if( NULL == running )
    {
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_pos_t position = {
        .offset = running->address,
        .size = running->size,
    };
    esp_image_metadata_t metadata;
    memset( &metadata, 0, sizeof( metadata ) );
    esp_err_t result = esp_image_get_metadata( &position, &metadata );
    if( ESP_OK != result )
    {
        return result;
    }
    if( 0U == metadata.image_len || metadata.image_len > running->size )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    psa_hash_operation_t sha256 = PSA_HASH_OPERATION_INIT;
    psa_status_t sha_result = psa_hash_setup( &sha256, PSA_ALG_SHA_256 );
    if( PSA_SUCCESS != sha_result )
    {
        return ESP_FAIL;
    }

    size_t offset = 0U;
    while( offset < metadata.image_len )
    {
        const size_t remaining = metadata.image_len - offset;
        const size_t chunk =
            remaining < sizeof( hash_read_buffer ) ? remaining : sizeof( hash_read_buffer );
        result = esp_partition_read( running, offset, hash_read_buffer, chunk );
        if( ESP_OK != result )
        {
            memset( hash_read_buffer, 0, sizeof( hash_read_buffer ) );
            (void)psa_hash_abort( &sha256 );
            return result;
        }

        sha_result = psa_hash_update( &sha256, hash_read_buffer, chunk );
        if( PSA_SUCCESS != sha_result )
        {
            memset( hash_read_buffer, 0, sizeof( hash_read_buffer ) );
            (void)psa_hash_abort( &sha256 );
            return ESP_FAIL;
        }
        offset += chunk;
    }
    memset( hash_read_buffer, 0, sizeof( hash_read_buffer ) );

    size_t digest_length = 0U;
    sha_result = psa_hash_finish(
        &sha256, a_digest, RFSCOUT_FIRMWARE_SHA256_LENGTH, &digest_length );
    if( PSA_SUCCESS != sha_result || RFSCOUT_FIRMWARE_SHA256_LENGTH != digest_length )
    {
        (void)psa_hash_abort( &sha256 );
        memset( a_digest, 0, RFSCOUT_FIRMWARE_SHA256_LENGTH );
        return ESP_FAIL;
    }

    *a_image_length = metadata.image_len;
    return ESP_OK;
}

void firmware_sha256_hex( const uint8_t a_digest[ RFSCOUT_FIRMWARE_SHA256_LENGTH ],
                          char a_hex[ RFSCOUT_FIRMWARE_SHA256_HEX_LENGTH + 1U ] )
{
    static const char digits[] = "0123456789abcdef";
    if( NULL == a_digest || NULL == a_hex )
    {
        return;
    }

    for( size_t index = 0U; index < RFSCOUT_FIRMWARE_SHA256_LENGTH; ++index )
    {
        a_hex[ index * 2U ] = digits[ a_digest[ index ] >> 4U ];
        a_hex[ index * 2U + 1U ] = digits[ a_digest[ index ] & 0x0fU ];
    }
    a_hex[ RFSCOUT_FIRMWARE_SHA256_HEX_LENGTH ] = '\0';
}
