/**
    probe_http_capture.h : bounded response header capture
    Description: Preserves observed HTTP HEAD response headers for local diagnostics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_PROBE_HTTP_CAPTURE_H_
#define _RFSCOUT_PROBE_HTTP_CAPTURE_H_
#include <stdbool.h>
#include <stddef.h>
#define RFSCOUT_PROBE_HTTP_CAPTURE_MAX 3072U
#define RFSCOUT_PROBE_HTTP_FIELD_MAX 1024U
typedef struct
{
    char data[ RFSCOUT_PROBE_HTTP_CAPTURE_MAX + 1U ];
    size_t length;
    bool truncated;
} rfscout_probe_http_capture_t;
void probe_http_capture_reset( rfscout_probe_http_capture_t *a_capture );
/** @brief Preserve parsed HEAD header names/values, not exact wire bytes.
 * @details Sanitize control bytes; bound each field and total bytes. */
void probe_http_capture_header( rfscout_probe_http_capture_t *a_capture,
                                const char *a_key,
                                const char *a_value );
#endif // _RFSCOUT_PROBE_HTTP_CAPTURE_H_
