/**
    service_fingerprint.h : RFScout network service metadata classifier
    Description: Pure, bounded interpretation of supplied service metadata; performs no I/O.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SERVICE_FINGERPRINT_H_
#define _RFSCOUT_SERVICE_FINGERPRINT_H_

#include <stddef.h>
#include <stdint.h>

/** Upper bound for a single metadata field supplied to the classifier. */
#define RFSCOUT_SERVICE_METADATA_MAX_LENGTH 256U

/** Independent observations; none by itself grants access to a device or its media. */
typedef enum
{
    RFSCOUT_SERVICE_HTTP = 1U << 0,
    RFSCOUT_SERVICE_AUTH_CHALLENGE = 1U << 1,
    RFSCOUT_SERVICE_MEDIA_RESPONSE = 1U << 2,
    RFSCOUT_SERVICE_RTSP_ADVERTISED = 1U << 3,
    RFSCOUT_SERVICE_CAMERA_HINT = 1U << 4,
    RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE = 1U << 5
} rfscout_service_evidence_t;

/** Fixed-size result; no raw headers, credentials, URLs or media are retained. */
typedef struct
{
    uint32_t evidence;
} rfscout_service_fingerprint_t;

/** @brief Reset one service fingerprint. */
void service_fingerprint_reset( rfscout_service_fingerprint_t *a_result );

/** @brief Ingest a response status from a separately authorized HTTP interaction. */
void service_fingerprint_http_status( rfscout_service_fingerprint_t *a_result, unsigned a_status );

/**
 * @brief Ingest one HTTP header, using explicit lengths from the HTTP client.
 * @details Overlong fields and null pointers are ignored. Case-insensitive matching
 *          is ASCII-only. An HTTP banner is only a weak device-category hint.
 */
void service_fingerprint_http_header( rfscout_service_fingerprint_t *a_result,
                                      const char *a_name,
                                      size_t a_name_length,
                                      const char *a_value,
                                      size_t a_value_length );

/** @brief Ingest an exact DNS-SD service type (e.g. _rtsp._tcp). */
void service_fingerprint_dns_sd( rfscout_service_fingerprint_t *a_result,
                                 const char *a_service,
                                 size_t a_service_length );

/**
 * @brief Ingest a validated WS-Discovery device-type local name.
 * @details The caller must validate XML/QName syntax and extract the local name;
 *          never pass an unparsed SOAP response as a device type.
 */
void service_fingerprint_ws_device_type( rfscout_service_fingerprint_t *a_result,
                                         const char *a_local_name,
                                         size_t a_local_name_length );

#endif // _RFSCOUT_SERVICE_FINGERPRINT_H_
