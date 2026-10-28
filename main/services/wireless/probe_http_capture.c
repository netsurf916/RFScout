/**
    probe_http_capture.c : bounded HTTP response header recorder
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "probe_http_capture.h"
#include <string.h>
void probe_http_capture_reset( rfscout_probe_http_capture_t *a_capture )
{
    if( NULL != a_capture )
    {
        memset( a_capture, 0, sizeof( *a_capture ) );
    }
}
static void add_byte( rfscout_probe_http_capture_t *a_capture, char a_byte, bool a_delimiter )
{
    if( a_capture->length >= RFSCOUT_PROBE_HTTP_CAPTURE_MAX )
    {
        a_capture->truncated = true;
        return;
    }
    const unsigned char value = (unsigned char)a_byte;
    a_capture->data[ a_capture->length++ ] =
        ( !a_delimiter && ( value < 0x20U || 0x7fU == value ) ) ? ' ' : a_byte;
    a_capture->data[ a_capture->length ] = '\0';
}
static void add_field( rfscout_probe_http_capture_t *a_capture, const char *a_field )
{
    size_t index = 0U;
    while( index < RFSCOUT_PROBE_HTTP_FIELD_MAX && '\0' != a_field[ index ] )
    {
        add_byte( a_capture, a_field[ index++ ], false );
    }
    if( '\0' != a_field[ index ] )
    {
        a_capture->truncated = true;
    }
}
void probe_http_capture_header( rfscout_probe_http_capture_t *a_capture,
                                const char *a_key,
                                const char *a_value )
{
    if( NULL == a_capture || NULL == a_key || NULL == a_value )
    {
        return;
    }
    add_field( a_capture, a_key );
    add_byte( a_capture, ':', true );
    add_byte( a_capture, ' ', true );
    add_field( a_capture, a_value );
    add_byte( a_capture, '\r', true );
    add_byte( a_capture, '\n', true );
}
