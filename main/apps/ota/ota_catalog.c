/**
    apps/ota/ota_catalog.c : RFScout OTA build-catalog validation and stable selection
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_catalog.h"

#include "cJSON.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OTA_CATALOG_SCHEMA_VERSION 1

typedef struct
{
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} ota_semver_t;

static bool parse_component( const char **a_cursor, uint32_t *a_value, char a_delimiter )
{
    const char *cursor = *a_cursor;
    if( !isdigit( (unsigned char)*cursor ) )
    {
        return false;
    }
    if( '0' == *cursor && isdigit( (unsigned char)cursor[ 1 ] ) )
    {
        return false;
    }

    uint64_t value = 0U;
    while( isdigit( (unsigned char)*cursor ) )
    {
        value = value * 10U + (uint64_t)( *cursor - '0' );
        if( value > UINT32_MAX )
        {
            return false;
        }
        ++cursor;
    }

    if( '\0' == a_delimiter )
    {
        if( '\0' != *cursor )
        {
            return false;
        }
    }
    else
    {
        if( a_delimiter != *cursor )
        {
            return false;
        }
        ++cursor;
    }

    *a_cursor = cursor;
    *a_value = (uint32_t)value;
    return true;
}

static bool parse_semver( const char *a_value, ota_semver_t *a_version )
{
    if( NULL == a_value || NULL == a_version || '\0' == a_value[ 0 ] )
    {
        return false;
    }

    const char *cursor = a_value;
    return parse_component( &cursor, &a_version->major, '.' ) &&
           parse_component( &cursor, &a_version->minor, '.' ) &&
           parse_component( &cursor, &a_version->patch, '\0' );
}

static int compare_semver( const ota_semver_t *a_left, const ota_semver_t *a_right )
{
    if( a_left->major != a_right->major )
    {
        return a_left->major < a_right->major ? -1 : 1;
    }
    if( a_left->minor != a_right->minor )
    {
        return a_left->minor < a_right->minor ? -1 : 1;
    }
    if( a_left->patch != a_right->patch )
    {
        return a_left->patch < a_right->patch ? -1 : 1;
    }
    return 0;
}

static const char *json_string( const cJSON *a_object, const char *a_name )
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive( a_object, a_name );
    return cJSON_IsString( value ) && NULL != value->valuestring ? value->valuestring : NULL;
}

static bool json_bool( const cJSON *a_object, const char *a_name, bool *a_value )
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive( a_object, a_name );
    if( !cJSON_IsBool( value ) )
    {
        return false;
    }
    *a_value = cJSON_IsTrue( value );
    return true;
}

static bool copy_bounded( char *a_destination, size_t a_capacity, const char *a_source )
{
    if( NULL == a_destination || 0U == a_capacity || NULL == a_source )
    {
        return false;
    }
    const size_t length = strlen( a_source );
    if( length >= a_capacity )
    {
        return false;
    }
    memcpy( a_destination, a_source, length + 1U );
    return true;
}

static bool valid_hex( const char *a_value, size_t a_length )
{
    if( NULL == a_value || a_length != strlen( a_value ) )
    {
        return false;
    }
    for( size_t index = 0U; index < a_length; ++index )
    {
        if( !isxdigit( (unsigned char)a_value[ index ] ) )
        {
            return false;
        }
    }
    return true;
}

static bool valid_relative_path( const char *a_path )
{
    if( NULL == a_path || '\0' == a_path[ 0 ] || '/' == a_path[ 0 ] ||
        NULL != strstr( a_path, "://" ) || NULL != strchr( a_path, '\\' ) ||
        NULL != strchr( a_path, '?' ) || NULL != strchr( a_path, '#' ) )
    {
        return false;
    }

    const char *segment = a_path;
    for( const char *cursor = a_path;; ++cursor )
    {
        if( '/' != *cursor && '\0' != *cursor )
        {
            continue;
        }
        const size_t length = (size_t)( cursor - segment );
        if( 0U == length || ( 1U == length && '.' == segment[ 0 ] ) ||
            ( 2U == length && '.' == segment[ 0 ] && '.' == segment[ 1 ] ) )
        {
            return false;
        }
        if( '\0' == *cursor )
        {
            break;
        }
        segment = cursor + 1;
    }
    return true;
}

static bool build_identity( const cJSON *a_build,
                            const char **a_project,
                            const char **a_hardware,
                            const char **a_target,
                            const char **a_layout )
{
    *a_project = json_string( a_build, "project" );
    *a_hardware = json_string( a_build, "hardware_target" );
    *a_target = json_string( a_build, "idf_target" );
    *a_layout = json_string( a_build, "partition_layout" );
    return NULL != *a_project && NULL != *a_hardware && NULL != *a_target && NULL != *a_layout;
}

static bool parse_artifact( const cJSON *a_build, ota_catalog_candidate_t *a_candidate )
{
    const cJSON *artifact = cJSON_GetObjectItemCaseSensitive( a_build, "artifact" );
    if( !cJSON_IsObject( artifact ) )
    {
        return false;
    }

    const char *path = json_string( artifact, "path" );
    const char *sha256 = json_string( artifact, "sha256" );
    const cJSON *size = cJSON_GetObjectItemCaseSensitive( artifact, "size" );
    if( !valid_relative_path( path ) || !valid_hex( sha256, OTA_CATALOG_SHA256_LENGTH ) ||
        !cJSON_IsNumber( size ) || size->valuedouble <= 0.0 ||
        size->valuedouble > (double)UINT32_MAX )
    {
        return false;
    }

    const uint32_t parsed_size = (uint32_t)size->valuedouble;
    if( (double)parsed_size != size->valuedouble )
    {
        return false;
    }

    return copy_bounded( a_candidate->artifact_path, sizeof( a_candidate->artifact_path ), path ) &&
           copy_bounded( a_candidate->sha256, sizeof( a_candidate->sha256 ), sha256 ) &&
           ( a_candidate->artifact_size = parsed_size, true );
}

esp_err_t ota_catalog_select( const char *a_json,
                              size_t a_length,
                              const rfscout_firmware_identity_t *a_running,
                              ota_catalog_selection_t *a_selection )
{
    if( NULL == a_json || 0U == a_length || NULL == a_running || NULL == a_selection )
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset( a_selection, 0, sizeof( *a_selection ) );
    ota_semver_t running_version;
    const bool running_is_semver = parse_semver( a_running->version, &running_version );

    cJSON *root = cJSON_ParseWithLength( a_json, a_length );
    if( NULL == root )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive( root, "schema_version" );
    const cJSON *builds = cJSON_GetObjectItemCaseSensitive( root, "builds" );
    if( !cJSON_IsNumber( schema ) || OTA_CATALOG_SCHEMA_VERSION != schema->valueint ||
        !cJSON_IsArray( builds ) || cJSON_GetArraySize( builds ) > OTA_CATALOG_MAX_BUILDS )
    {
        goto cleanup;
    }

    bool selected = false;
    bool have_latest = false;
    ota_semver_t selected_version = { 0 };
    ota_semver_t latest_version = { 0 };
    const cJSON *build = NULL;
    cJSON_ArrayForEach( build, builds )
    {
        if( !cJSON_IsObject( build ) )
        {
            goto cleanup;
        }

        const char *build_id = json_string( build, "id" );
        const char *kind = json_string( build, "kind" );
        const char *version = json_string( build, "version" );
        const char *commit = json_string( build, "commit" );
        const char *project = NULL;
        const char *hardware = NULL;
        const char *target = NULL;
        const char *layout = NULL;
        bool stable = false;
        ota_catalog_candidate_t candidate = { 0 };
        if( NULL == build_id || NULL == kind || NULL == version || NULL == commit ||
            !valid_hex( commit, OTA_CATALOG_COMMIT_LENGTH ) ||
            !json_bool( build, "stable", &stable ) ||
            !build_identity( build, &project, &hardware, &target, &layout ) ||
            !parse_artifact( build, &candidate ) )
        {
            goto cleanup;
        }
        if( !copy_bounded( candidate.build_id, sizeof( candidate.build_id ), build_id ) ||
            !copy_bounded( candidate.version, sizeof( candidate.version ), version ) ||
            !copy_bounded( candidate.commit, sizeof( candidate.commit ), commit ) )
        {
            goto cleanup;
        }

        const bool compatible = 0 == strcmp( project, a_running->project ) &&
                                0 == strcmp( hardware, a_running->hardware_target ) &&
                                0 == strcmp( target, a_running->idf_target ) &&
                                0 == strcmp( layout, a_running->partition_layout );
        if( !compatible || !stable )
        {
            continue;
        }
        if( 0 != strcmp( kind, "release" ) )
        {
            goto cleanup;
        }

        ota_semver_t parsed_version;
        if( !parse_semver( version, &parsed_version ) )
        {
            goto cleanup;
        }
        if( !have_latest || compare_semver( &parsed_version, &latest_version ) > 0 )
        {
            latest_version = parsed_version;
            have_latest = true;
            if( !copy_bounded(
                    a_selection->latest_version, sizeof( a_selection->latest_version ), version ) )
            {
                goto cleanup;
            }
        }

        if( running_is_semver && compare_semver( &parsed_version, &running_version ) <= 0 )
        {
            continue;
        }
        if( !selected || compare_semver( &parsed_version, &selected_version ) > 0 )
        {
            a_selection->candidate = candidate;
            selected_version = parsed_version;
            selected = true;
        }
    }

    a_selection->result = selected ? OTA_CATALOG_UPDATE_AVAILABLE : OTA_CATALOG_CURRENT;
    result = ESP_OK;

cleanup:
    cJSON_Delete( root );
    return result;
}
