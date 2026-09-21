/**
    ota_installer.c : RFScout transport-neutral OTA image installer
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_installer.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "psa/crypto.h"

#include <string.h>

#define OTA_INSTALLER_HASH_CHUNK 4096U
#define OTA_INSTALLER_PROJECT "rfscout"

static const char *TAG = "ota_installer";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_installer_status_t status;
static ota_installer_image_t expected_image;
static const esp_partition_t *target_partition;
static esp_ota_handle_t ota_handle;
static bool ota_handle_open;
static psa_hash_operation_t stream_hash = PSA_HASH_OPERATION_INIT;
static bool stream_hash_open;
static uint8_t readback_buffer[ OTA_INSTALLER_HASH_CHUNK ];

static void set_failure( esp_err_t a_error )
{
    portENTER_CRITICAL( &state_lock );
    status.state = OTA_INSTALLER_FAILED;
    status.error = a_error;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );
}

static bool decode_sha256( const char *a_hex, uint8_t a_digest[ 32 ] )
{
    if( NULL == a_hex || 64U != strlen( a_hex ) )
    {
        return false;
    }

    for( size_t index = 0U; index < 32U; ++index )
    {
        unsigned value = 0U;
        for( size_t nibble = 0U; nibble < 2U; ++nibble )
        {
            const char character = a_hex[ index * 2U + nibble ];
            unsigned digit;
            if( character >= '0' && character <= '9' )
            {
                digit = (unsigned)( character - '0' );
            }
            else if( character >= 'a' && character <= 'f' )
            {
                digit = (unsigned)( character - 'a' ) + 10U;
            }
            else if( character >= 'A' && character <= 'F' )
            {
                digit = (unsigned)( character - 'A' ) + 10U;
            }
            else
            {
                return false;
            }
            value = ( value << 4U ) | digit;
        }
        a_digest[ index ] = (uint8_t)value;
    }
    return true;
}

static esp_err_t finish_hash( psa_hash_operation_t *a_hash, uint8_t a_digest[ 32 ] )
{
    size_t digest_length = 0U;
    const psa_status_t result = psa_hash_finish( a_hash, a_digest, 32U, &digest_length );
    return PSA_SUCCESS == result && 32U == digest_length ? ESP_OK : ESP_FAIL;
}

static esp_err_t verify_flash_readback( const uint8_t a_expected[ 32 ] )
{
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if( PSA_SUCCESS != psa_hash_setup( &hash, PSA_ALG_SHA_256 ) )
    {
        return ESP_FAIL;
    }

    uint32_t offset = 0U;
    while( offset < expected_image.size )
    {
        const uint32_t remaining = expected_image.size - offset;
        const size_t chunk =
            remaining < sizeof( readback_buffer ) ? remaining : sizeof( readback_buffer );
        esp_err_t result = esp_partition_read( target_partition, offset, readback_buffer, chunk );
        if( ESP_OK != result )
        {
            (void)psa_hash_abort( &hash );
            memset( readback_buffer, 0, sizeof( readback_buffer ) );
            return result;
        }
        if( PSA_SUCCESS != psa_hash_update( &hash, readback_buffer, chunk ) )
        {
            (void)psa_hash_abort( &hash );
            memset( readback_buffer, 0, sizeof( readback_buffer ) );
            return ESP_FAIL;
        }
        offset += (uint32_t)chunk;
    }
    memset( readback_buffer, 0, sizeof( readback_buffer ) );

    uint8_t digest[ 32 ];
    const esp_err_t result = finish_hash( &hash, digest );
    const bool matches = ESP_OK == result && 0 == memcmp( digest, a_expected, sizeof( digest ) );
    memset( digest, 0, sizeof( digest ) );
    return matches ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static void clear_runtime( void )
{
    if( stream_hash_open )
    {
        (void)psa_hash_abort( &stream_hash );
        stream_hash = PSA_HASH_OPERATION_INIT;
        stream_hash_open = false;
    }
    memset( &expected_image, 0, sizeof( expected_image ) );
    target_partition = NULL;
    ota_handle = 0U;
    ota_handle_open = false;
}

esp_err_t ota_installer_begin( const ota_installer_image_t *a_image )
{
    if( NULL == a_image || 0U == a_image->size || '\0' == a_image->version[ 0 ] )
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t expected_digest[ 32 ];
    if( !decode_sha256( a_image->sha256, expected_digest ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( expected_digest, 0, sizeof( expected_digest ) );

    portENTER_CRITICAL( &state_lock );
    const bool busy = OTA_INSTALLER_IDLE != status.state && OTA_INSTALLER_FAILED != status.state;
    portEXIT_CRITICAL( &state_lock );
    if( busy )
    {
        return ESP_ERR_INVALID_STATE;
    }

    (void)ota_installer_abort();

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition( NULL );
    if( NULL == running || NULL == target || target == running )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( a_image->size > target->size )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0U;
    esp_err_t result = esp_ota_begin( target, a_image->size, &handle );
    if( ESP_OK != result )
    {
        return result;
    }

    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if( PSA_SUCCESS != psa_hash_setup( &hash, PSA_ALG_SHA_256 ) )
    {
        (void)esp_ota_abort( handle );
        return ESP_FAIL;
    }

    expected_image = *a_image;
    target_partition = target;
    ota_handle = handle;
    ota_handle_open = true;
    stream_hash = hash;
    stream_hash_open = true;

    portENTER_CRITICAL( &state_lock );
    status.state = OTA_INSTALLER_WRITING;
    status.error = ESP_OK;
    status.bytes_written = 0U;
    status.expected_size = a_image->size;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );

    ESP_LOGI( TAG,
              "writing OTA candidate version=%s bytes=%u partition=%s",
              expected_image.version,
              (unsigned)expected_image.size,
              target_partition->label );
    return ESP_OK;
}

esp_err_t ota_installer_write( const void *a_data, size_t a_length )
{
    if( NULL == a_data || 0U == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL( &state_lock );
    const bool writing = OTA_INSTALLER_WRITING == status.state;
    const uint32_t already_written = status.bytes_written;
    portEXIT_CRITICAL( &state_lock );
    if( !writing || !ota_handle_open || !stream_hash_open )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( a_length > expected_image.size - already_written )
    {
        set_failure( ESP_ERR_INVALID_SIZE );
        return ESP_ERR_INVALID_SIZE;
    }

    if( PSA_SUCCESS != psa_hash_update( &stream_hash, a_data, a_length ) )
    {
        set_failure( ESP_FAIL );
        return ESP_FAIL;
    }

    const esp_err_t result = esp_ota_write( ota_handle, a_data, a_length );
    if( ESP_OK != result )
    {
        set_failure( result );
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    status.bytes_written += (uint32_t)a_length;
    portEXIT_CRITICAL( &state_lock );
    return ESP_OK;
}

esp_err_t ota_installer_finish( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool writing = OTA_INSTALLER_WRITING == status.state;
    const uint32_t bytes_written = status.bytes_written;
    status.state = writing ? OTA_INSTALLER_VERIFYING : status.state;
    portEXIT_CRITICAL( &state_lock );
    if( !writing || !ota_handle_open || !stream_hash_open )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( bytes_written != expected_image.size )
    {
        set_failure( ESP_ERR_INVALID_SIZE );
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t expected_digest[ 32 ];
    uint8_t stream_digest[ 32 ];
    if( !decode_sha256( expected_image.sha256, expected_digest ) )
    {
        set_failure( ESP_ERR_INVALID_ARG );
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = finish_hash( &stream_hash, stream_digest );
    stream_hash_open = false;
    stream_hash = PSA_HASH_OPERATION_INIT;
    if( ESP_OK != result || 0 != memcmp( expected_digest, stream_digest, sizeof( stream_digest ) ) )
    {
        memset( expected_digest, 0, sizeof( expected_digest ) );
        memset( stream_digest, 0, sizeof( stream_digest ) );
        set_failure( ESP_ERR_INVALID_CRC );
        return ESP_ERR_INVALID_CRC;
    }
    memset( stream_digest, 0, sizeof( stream_digest ) );

    result = esp_ota_end( ota_handle );
    ota_handle_open = false;
    ota_handle = 0U;
    if( ESP_OK != result )
    {
        memset( expected_digest, 0, sizeof( expected_digest ) );
        set_failure( result );
        return result;
    }

    esp_app_desc_t description;
    memset( &description, 0, sizeof( description ) );
    result = esp_ota_get_partition_description( target_partition, &description );
    if( ESP_OK != result || 0 != strcmp( description.project_name, OTA_INSTALLER_PROJECT ) ||
        0 != strcmp( description.version, expected_image.version ) )
    {
        memset( expected_digest, 0, sizeof( expected_digest ) );
        set_failure( ESP_ERR_INVALID_VERSION );
        return ESP_ERR_INVALID_VERSION;
    }

    result = verify_flash_readback( expected_digest );
    memset( expected_digest, 0, sizeof( expected_digest ) );
    if( ESP_OK != result )
    {
        set_failure( result );
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    status.state = OTA_INSTALLER_VERIFIED;
    status.error = ESP_OK;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );
    ESP_LOGI( TAG, "OTA candidate verified from flash: %s", expected_image.version );
    return ESP_OK;
}

esp_err_t ota_installer_commit( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool verified = OTA_INSTALLER_VERIFIED == status.state;
    portEXIT_CRITICAL( &state_lock );
    if( !verified || NULL == target_partition )
    {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = esp_ota_set_boot_partition( target_partition );
    if( ESP_OK != result )
    {
        set_failure( result );
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    status.state = OTA_INSTALLER_COMMITTED;
    status.error = ESP_OK;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );
    ESP_LOGI( TAG, "OTA candidate committed for next boot: %s", expected_image.version );
    return ESP_OK;
}

esp_err_t ota_installer_abort( void )
{
    esp_err_t result = ESP_OK;
    if( ota_handle_open )
    {
        result = esp_ota_abort( ota_handle );
    }
    clear_runtime();

    portENTER_CRITICAL( &state_lock );
    status.state = OTA_INSTALLER_IDLE;
    status.error = ESP_OK;
    status.bytes_written = 0U;
    status.expected_size = 0U;
    ++status.generation;
    portEXIT_CRITICAL( &state_lock );
    return ESP_ERR_NOT_FOUND == result ? ESP_OK : result;
}

ota_installer_status_t ota_installer_status( void )
{
    ota_installer_status_t result;
    portENTER_CRITICAL( &state_lock );
    result = status;
    portEXIT_CRITICAL( &state_lock );
    return result;
}

bool ota_installer_is_active( void )
{
    portENTER_CRITICAL( &state_lock );
    const bool active =
        OTA_INSTALLER_IDLE != status.state && OTA_INSTALLER_COMMITTED != status.state;
    portEXIT_CRITICAL( &state_lock );
    return active;
}
