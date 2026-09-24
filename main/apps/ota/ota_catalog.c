/**
    apps/ota/ota_catalog.c : RFScout OTA build-catalog validation and selection
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

typedef struct
{
    ota_catalog_build_kind_t kind;
    const char *build_id;
    const char *label;
    const char *version;
    const char *commit;
    const char *artifact_path;
    const char *sha256;
    uint32_t artifact_size;
    int32_t pull_request;
    bool stable;
    bool draft;
    bool prerelease;
    bool compatible;
} parsed_build_view_t;

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

static bool valid_build_metadata( const char *a_value )
{
    if( NULL == a_value || '\0' == a_value[ 0 ] )
    {
        return false;
    }

    bool segment_has_character = false;
    for( const char *cursor = a_value;; ++cursor )
    {
        const char character = *cursor;
        if( '\0' == character )
        {
            return segment_has_character;
        }
        if( '.' == character )
        {
            if( !segment_has_character )
            {
                return false;
            }
            segment_has_character = false;
            continue;
        }
        if( !isalnum( (unsigned char)character ) && '-' != character )
        {
            return false;
        }
        segment_has_character = true;
    }
}

static bool parse_semver( const char *a_value, ota_semver_t *a_version )
{
    if( NULL == a_value || NULL == a_version || '\0' == a_value[ 0 ] )
    {
        return false;
    }

    const char *cursor = a_value;
    if( !parse_component( &cursor, &a_version->major, '.' ) ||
        !parse_component( &cursor, &a_version->minor, '.' ) )
    {
        return false;
    }

    if( !isdigit( (unsigned char)*cursor ) ||
        ( '0' == *cursor && isdigit( (unsigned char)cursor[ 1 ] ) ) )
    {
        return false;
    }

    uint64_t patch = 0U;
    while( isdigit( (unsigned char)*cursor ) )
    {
        patch = patch * 10U + (uint64_t)( *cursor - '0' );
        if( patch > UINT32_MAX )
        {
            return false;
        }
        ++cursor;
    }
    a_version->patch = (uint32_t)patch;

    if( '\0' == *cursor )
    {
        return true;
    }
    return '+' == *cursor && valid_build_metadata( cursor + 1 );
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

static bool json_optional_bool( const cJSON *a_object, const char *a_name, bool *a_value )
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive( a_object, a_name );
    if( NULL == value )
    {
        *a_value = false;
        return true;
    }
    if( !cJSON_IsBool( value ) )
    {
        return false;
    }
    *a_value = cJSON_IsTrue( value );
    return true;
}

static bool string_fits( const char *a_value, size_t a_capacity )
{
    return ( NULL != a_value ) && ( a_capacity > 0U ) && ( strlen( a_value ) < a_capacity );
}

static bool copy_bounded( char *a_destination, size_t a_capacity, const char *a_source )
{
    if( NULL == a_destination || !string_fits( a_source, a_capacity ) )
    {
        return false;
    }
    const size_t length = strlen( a_source );
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

static bool parse_artifact( const cJSON *a_build, parsed_build_view_t *a_parsed )
{
    const cJSON *artifact = cJSON_GetObjectItemCaseSensitive( a_build, "artifact" );
    if( !cJSON_IsObject( artifact ) )
    {
        return false;
    }

    const char *path = json_string( artifact, "path" );
    const char *sha256 = json_string( artifact, "sha256" );
    const cJSON *size = cJSON_GetObjectItemCaseSensitive( artifact, "size" );
    if( !valid_relative_path( path ) ||
        !string_fits( path, OTA_CATALOG_ARTIFACT_PATH_MAX_LENGTH + 1U ) ||
        !valid_hex( sha256, OTA_CATALOG_SHA256_LENGTH ) || !cJSON_IsNumber( size ) ||
        size->valuedouble <= 0.0 || size->valuedouble > (double)UINT32_MAX )
    {
        return false;
    }

    const uint32_t parsed_size = (uint32_t)size->valuedouble;
    if( (double)parsed_size != size->valuedouble )
    {
        return false;
    }

    a_parsed->artifact_path = path;
    a_parsed->sha256 = sha256;
    a_parsed->artifact_size = parsed_size;
    return true;
}

static bool parse_build( const cJSON *a_json,
                         const rfscout_firmware_identity_t *a_running,
                         parsed_build_view_t *a_parsed )
{
    *a_parsed = ( parsed_build_view_t ){ 0 };

    a_parsed->build_id = json_string( a_json, "id" );
    const char *kind = json_string( a_json, "kind" );
    a_parsed->label = json_string( a_json, "label" );
    a_parsed->version = json_string( a_json, "version" );
    a_parsed->commit = json_string( a_json, "commit" );
    const char *project = json_string( a_json, "project" );
    const char *hardware = json_string( a_json, "hardware_target" );
    const char *target = json_string( a_json, "idf_target" );
    const char *layout = json_string( a_json, "partition_layout" );

    if( !string_fits( a_parsed->build_id, OTA_CATALOG_BUILD_ID_MAX_LENGTH + 1U ) || NULL == kind ||
        !string_fits( a_parsed->label, OTA_CATALOG_BUILD_LABEL_MAX_LENGTH + 1U ) ||
        !string_fits( a_parsed->version, OTA_CATALOG_VERSION_MAX_LENGTH + 1U ) ||
        !valid_hex( a_parsed->commit, OTA_CATALOG_COMMIT_LENGTH ) || NULL == project ||
        NULL == hardware || NULL == target || NULL == layout ||
        !json_bool( a_json, "stable", &a_parsed->stable ) ||
        !json_optional_bool( a_json, "draft", &a_parsed->draft ) ||
        !json_optional_bool( a_json, "prerelease", &a_parsed->prerelease ) ||
        !parse_artifact( a_json, a_parsed ) )
    {
        return false;
    }

    if( 0 == strcmp( kind, "release" ) )
    {
        a_parsed->kind = OTA_CATALOG_BUILD_RELEASE;
        if( a_parsed->stable && ( a_parsed->draft || a_parsed->prerelease ) )
        {
            return false;
        }
    }
    else if( 0 == strcmp( kind, "candidate" ) )
    {
        if( a_parsed->stable )
        {
            return false;
        }

        const char *source = json_string( a_json, "source" );
        if( NULL == source )
        {
            return false;
        }
        if( 0 == strcmp( source, "main" ) )
        {
            a_parsed->kind = OTA_CATALOG_BUILD_MAIN;
        }
        else if( 0 == strcmp( source, "pull_request" ) )
        {
            const cJSON *pr = cJSON_GetObjectItemCaseSensitive( a_json, "pr" );
            if( !cJSON_IsNumber( pr ) || pr->valuedouble <= 0.0 ||
                pr->valuedouble > (double)INT32_MAX )
            {
                return false;
            }
            a_parsed->pull_request = (int32_t)pr->valuedouble;
            if( (double)a_parsed->pull_request != pr->valuedouble )
            {
                return false;
            }
            a_parsed->kind = OTA_CATALOG_BUILD_PULL_REQUEST;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    a_parsed->compatible = 0 == strcmp( project, a_running->project ) &&
                           0 == strcmp( hardware, a_running->hardware_target ) &&
                           0 == strcmp( target, a_running->idf_target ) &&
                           0 == strcmp( layout, a_running->partition_layout );
    return true;
}

static bool materialize_candidate( const parsed_build_view_t *a_parsed,
                                   ota_catalog_candidate_t *a_candidate )
{
    memset( a_candidate, 0, sizeof( *a_candidate ) );
    if( !copy_bounded(
            a_candidate->build_id, sizeof( a_candidate->build_id ), a_parsed->build_id ) ||
        !copy_bounded( a_candidate->version, sizeof( a_candidate->version ), a_parsed->version ) ||
        !copy_bounded( a_candidate->commit, sizeof( a_candidate->commit ), a_parsed->commit ) ||
        !copy_bounded( a_candidate->artifact_path,
                       sizeof( a_candidate->artifact_path ),
                       a_parsed->artifact_path ) ||
        !copy_bounded( a_candidate->sha256, sizeof( a_candidate->sha256 ), a_parsed->sha256 ) )
    {
        return false;
    }

    a_candidate->artifact_size = a_parsed->artifact_size;
    return true;
}

static bool materialize_build( const parsed_build_view_t *a_parsed, ota_catalog_build_t *a_build )
{
    memset( a_build, 0, sizeof( *a_build ) );
    a_build->kind = a_parsed->kind;
    a_build->pull_request = a_parsed->pull_request;
    a_build->stable = a_parsed->stable;
    a_build->draft = a_parsed->draft;
    a_build->prerelease = a_parsed->prerelease;

    return copy_bounded( a_build->label, sizeof( a_build->label ), a_parsed->label ) &&
           materialize_candidate( a_parsed, &a_build->candidate );
}

static cJSON *parse_catalog_root( const char *a_json, size_t a_length, const cJSON **a_builds )
{
    cJSON *root = cJSON_ParseWithLength( a_json, a_length );
    if( NULL == root )
    {
        return NULL;
    }

    const cJSON *schema = cJSON_GetObjectItemCaseSensitive( root, "schema_version" );
    const cJSON *builds = cJSON_GetObjectItemCaseSensitive( root, "builds" );
    if( !cJSON_IsNumber( schema ) || (double)OTA_CATALOG_SCHEMA_VERSION != schema->valuedouble ||
        !cJSON_IsArray( builds ) )
    {
        cJSON_Delete( root );
        return NULL;
    }

    *a_builds = builds;
    return root;
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

    const cJSON *builds = NULL;
    cJSON *root = parse_catalog_root( a_json, a_length, &builds );
    if( NULL == root )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    bool selected = false;
    bool have_latest = false;
    ota_semver_t selected_version = { 0 };
    ota_semver_t latest_version = { 0 };

    const cJSON *entry = NULL;
    cJSON_ArrayForEach( entry, builds )
    {
        if( !cJSON_IsObject( entry ) )
        {
            goto cleanup;
        }

        parsed_build_view_t parsed;
        if( !parse_build( entry, a_running, &parsed ) )
        {
            goto cleanup;
        }
        if( !parsed.compatible || !parsed.stable )
        {
            continue;
        }
        if( OTA_CATALOG_BUILD_RELEASE != parsed.kind )
        {
            goto cleanup;
        }

        ota_semver_t parsed_version;
        if( !parse_semver( parsed.version, &parsed_version ) )
        {
            goto cleanup;
        }
        if( !have_latest || compare_semver( &parsed_version, &latest_version ) > 0 )
        {
            latest_version = parsed_version;
            have_latest = true;
            if( !copy_bounded( a_selection->latest_version,
                               sizeof( a_selection->latest_version ),
                               parsed.version ) )
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
            if( !materialize_candidate( &parsed, &a_selection->candidate ) )
            {
                goto cleanup;
            }
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

esp_err_t ota_catalog_list( const char *a_json,
                            size_t a_length,
                            const rfscout_firmware_identity_t *a_running,
                            ota_catalog_build_t *a_builds,
                            size_t a_capacity,
                            size_t *a_count )
{
    if( NULL == a_json || 0U == a_length || NULL == a_running || NULL == a_builds ||
        NULL == a_count )
    {
        return ESP_ERR_INVALID_ARG;
    }

    *a_count = 0U;
    const cJSON *builds = NULL;
    cJSON *root = parse_catalog_root( a_json, a_length, &builds );
    if( NULL == root )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach( entry, builds )
    {
        if( !cJSON_IsObject( entry ) )
        {
            goto cleanup;
        }

        parsed_build_view_t parsed;
        if( !parse_build( entry, a_running, &parsed ) )
        {
            goto cleanup;
        }
        if( !parsed.compatible )
        {
            continue;
        }
        if( *a_count < a_capacity )
        {
            if( !materialize_build( &parsed, &a_builds[ *a_count ] ) )
            {
                goto cleanup;
            }
            ++( *a_count );
        }
    }

    result = ESP_OK;

cleanup:
    cJSON_Delete( root );
    return result;
}
