/**
    service_fingerprint.c : RFScout network service metadata classifier
    Description: No network access or surveillance-signature dependencies.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "service_fingerprint.h"

#include <stdbool.h>
#include <string.h>

static unsigned char ascii_lower( unsigned char a_value )
{
    if( ( a_value >= 'A' ) && ( a_value <= 'Z' ) )
    {
        return (unsigned char)( a_value + ( 'a' - 'A' ) );
    }
    return a_value;
}

/** @brief Match an entire bounded field against a trusted literal. */
static bool field_equals( const char *a_text, size_t a_length, const char *a_literal )
{
    const size_t expected = strlen( a_literal );
    if( a_length != expected )
    {
        return false;
    }
    for( size_t index = 0U; index < expected; ++index )
    {
        if( ascii_lower( (unsigned char)a_text[ index ] ) !=
            ascii_lower( (unsigned char)a_literal[ index ] ) )
        {
            return false;
        }
    }
    return true;
}

/** @brief Find a bounded, case-insensitive phrase without assuming NUL termination. */
static bool field_contains( const char *a_text, size_t a_length, const char *a_literal )
{
    const size_t expected = strlen( a_literal );
    if( ( 0U == expected ) || ( expected > a_length ) )
    {
        return false;
    }
    for( size_t index = 0U; index <= a_length - expected; ++index )
    {
        if( field_equals( &a_text[ index ], expected, a_literal ) )
        {
            return true;
        }
    }
    return false;
}

void service_fingerprint_reset( rfscout_service_fingerprint_t *a_result )
{
    if( NULL != a_result )
    {
        a_result->evidence = 0U;
    }
}

void service_fingerprint_http_status( rfscout_service_fingerprint_t *a_result, unsigned a_status )
{
    if( ( NULL == a_result ) || ( a_status < 100U ) || ( a_status > 599U ) )
    {
        return;
    }
    a_result->evidence |= RFSCOUT_SERVICE_HTTP;
    if( 401U == a_status )
    {
        a_result->evidence |= RFSCOUT_SERVICE_AUTH_CHALLENGE;
    }
}

void service_fingerprint_http_header( rfscout_service_fingerprint_t *a_result,
                                      const char *a_name,
                                      size_t a_name_length,
                                      const char *a_value,
                                      size_t a_value_length )
{
    if( ( NULL == a_result ) || ( NULL == a_name ) || ( NULL == a_value ) ||
        ( 0U == a_name_length ) || ( a_name_length > RFSCOUT_SERVICE_METADATA_MAX_LENGTH ) ||
        ( a_value_length > RFSCOUT_SERVICE_METADATA_MAX_LENGTH ) )
    {
        return;
    }

    a_result->evidence |= RFSCOUT_SERVICE_HTTP;
    const bool server = field_equals( a_name, a_name_length, "Server" );
    const bool auth = field_equals( a_name, a_name_length, "WWW-Authenticate" );
    if( auth )
    {
        a_result->evidence |= RFSCOUT_SERVICE_AUTH_CHALLENGE;
    }
    if( server || auth )
    {
        if( field_contains( a_value, a_value_length, "ip camera" ) ||
            field_contains( a_value, a_value_length, "network camera" ) ||
            field_contains( a_value, a_value_length, "onvif" ) )
        {
            a_result->evidence |= RFSCOUT_SERVICE_CAMERA_HINT;
        }
    }

    if( field_equals( a_name, a_name_length, "Content-Type" ) &&
        ( field_contains( a_value, a_value_length, "image/jpeg" ) ||
          field_contains( a_value, a_value_length, "multipart/x-mixed-replace" ) ) )
    {
        a_result->evidence |= RFSCOUT_SERVICE_MEDIA_RESPONSE;
    }
}

void service_fingerprint_dns_sd( rfscout_service_fingerprint_t *a_result,
                                 const char *a_service,
                                 size_t a_service_length )
{
    if( ( NULL == a_result ) || ( NULL == a_service ) ||
        ( a_service_length > RFSCOUT_SERVICE_METADATA_MAX_LENGTH ) )
    {
        return;
    }
    if( field_equals( a_service, a_service_length, "_rtsp._tcp" ) )
    {
        a_result->evidence |= RFSCOUT_SERVICE_RTSP_ADVERTISED;
    }
    else if( field_equals( a_service, a_service_length, "_http._tcp" ) ||
             field_equals( a_service, a_service_length, "_https._tcp" ) )
    {
        a_result->evidence |= RFSCOUT_SERVICE_HTTP;
    }
}

void service_fingerprint_ws_device_type( rfscout_service_fingerprint_t *a_result,
                                         const char *a_local_name,
                                         size_t a_local_name_length )
{
    if( ( NULL == a_result ) || ( NULL == a_local_name ) ||
        ( a_local_name_length > RFSCOUT_SERVICE_METADATA_MAX_LENGTH ) )
    {
        return;
    }
    if( field_equals( a_local_name, a_local_name_length, "NetworkVideoTransmitter" ) )
    {
        a_result->evidence |= RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE;
    }
}
