/**
    observation_journal.c : shared versioned observation JSONL envelope
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_journal.h"

#include <stdarg.h>
#include <stdio.h>

static const char *source_name( rf_source_t a_source )
{
    switch( a_source )
    {
        case RF_SOURCE_WIFI:
            return "wifi";
        case RF_SOURCE_BLE:
            return "ble";
        case RF_SOURCE_SUBGHZ:
            return "subghz";
        case RF_SOURCE_NRF24:
            return "nrf24";
        case RF_SOURCE_NFC:
            return "nfc";
        case RF_SOURCE_IR:
            return "ir";
        default:
            return NULL;
    }
}

static int append( char *a_line, size_t a_capacity, int a_length, const char *a_format, ... )
{
    if( NULL == a_line || NULL == a_format || a_length < 0 ||
        (size_t)a_length >= a_capacity )
    {
        return -1;
    }
    va_list arguments;
    va_start( arguments, a_format );
    const int written =
        vsnprintf( &a_line[ a_length ], a_capacity - (size_t)a_length, a_format, arguments );
    va_end( arguments );
    if( written < 0 || (size_t)written >= a_capacity - (size_t)a_length )
    {
        return -1;
    }
    return a_length + written;
}

int observation_journal_format_prefix( const observation_journal_record_t *a_record,
                                       char *a_line,
                                       size_t a_capacity )
{
    if( NULL == a_record || NULL == a_line )
    {
        return -1;
    }
    const char *source = source_name( a_record->source );
    if( NULL == source )
    {
        return -1;
    }
    int length = append( a_line,
                         a_capacity,
                         0,
                         "{\"schema\":%u,\"type\":\"observation\",\"source\":\"%s\","
                         "\"address\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
                         "\"address_type\":%u,\"utc\":%lld,\"seen_ms\":%lu,"
                         "\"rssi\":%d,\"channel\":%u",
                         RFSCOUT_OBSERVATION_JOURNAL_SCHEMA,
                         source,
                         a_record->address[ 0 ],
                         a_record->address[ 1 ],
                         a_record->address[ 2 ],
                         a_record->address[ 3 ],
                         a_record->address[ 4 ],
                         a_record->address[ 5 ],
                         (unsigned)a_record->address_type,
                         (long long)a_record->utc_epoch,
                         (unsigned long)a_record->seen_ms,
                         (int)a_record->rssi,
                         (unsigned)a_record->channel );
    if( a_record->location_valid )
    {
        length = append( a_line,
                         a_capacity,
                         length,
                         ",\"lat\":%.7f,\"lon\":%.7f",
                         a_record->latitude,
                         a_record->longitude );
    }
    return length;
}

int observation_journal_format_record( void *a_context,
                                       size_t a_index,
                                       char *a_line,
                                       size_t a_capacity )
{
    if( NULL == a_context )
    {
        return -1;
    }
    const observation_journal_record_t *records = a_context;
    const int length = observation_journal_format_prefix( &records[ a_index ], a_line, a_capacity );
    return append( a_line, a_capacity, length, "}" );
}
