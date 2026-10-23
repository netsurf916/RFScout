/**
    https_diagnostics.h : Shared UART diagnostics for outbound HTTPS operations
    Description: Observe internal network heap and hostname resolution without recording secrets.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#ifndef RFSCOUT_HTTPS_DIAGNOSTICS_H
#define RFSCOUT_HTTPS_DIAGNOSTICS_H

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static inline void https_diag_heap( const char *a_tag, const char *a_stage )
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI( a_tag,
              "HTTPS heap %s internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
              a_stage,
              (unsigned)heap_caps_get_free_size( internal ),
              (unsigned)heap_caps_get_largest_free_block( internal ),
              (unsigned)heap_caps_get_free_size( dma ),
              (unsigned)heap_caps_get_largest_free_block( dma ) );
}

/* Run once before the HTTP request; this intentionally uses the same system resolver as TLS. */
static inline void https_diag_dns( const char *a_tag, const char *a_host )
{
    const struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *addresses = NULL;
    const int64_t started = esp_timer_get_time();
    const int result = getaddrinfo( a_host, "443", &hints, &addresses );
    char address[ 48 ] = { 0 };
    if( 0 == result && NULL != addresses )
    {
        const void *ip = NULL;
        if( AF_INET == addresses->ai_family )
        {
            ip = &((const struct sockaddr_in *)addresses->ai_addr)->sin_addr;
        }
        else if( AF_INET6 == addresses->ai_family )
        {
            ip = &((const struct sockaddr_in6 *)addresses->ai_addr)->sin6_addr;
        }
        if( NULL != ip )
        {
            (void)inet_ntop( addresses->ai_family, ip, address, sizeof( address ) );
        }
    }
    ESP_LOGI( a_tag,
              "HTTPS DNS host=%s result=%d family=%d address=%s elapsed_ms=%lld",
              a_host,
              result,
              NULL != addresses ? addresses->ai_family : 0,
              '\0' != address[ 0 ] ? address : "-",
              (long long)( ( esp_timer_get_time() - started ) / 1000LL ) );
    if( NULL != addresses )
    {
        freeaddrinfo( addresses );
    }
}

#endif
