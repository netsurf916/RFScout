/**
    surveillance_history.c : Receiver-observation history
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "surveillance_history.h"
#include "gps.h"
#include "media_store.h"
#include "observation_store.h"

#include <string.h>

#define HISTORY_CAPACITY 128U
static surveillance_history_entry_t entries[ HISTORY_CAPACITY ];

size_t surveillance_history_list( surveillance_history_entry_t *a_rows, size_t a_capacity )
{
    size_t count = 0U;
    for( size_t i = 0U; i < HISTORY_CAPACITY; ++i )
    {
        if( !entries[ i ].valid ) continue;
        if( NULL != a_rows && count < a_capacity ) a_rows[ count ] = entries[ i ];
        ++count;
    }
    return count;
}

void surveillance_history_open( void )
{
    memset( entries, 0, sizeof( entries ) );
}

void surveillance_history_flush( void )
{
}

void surveillance_history_compact( void )
{
}

void surveillance_history_sample( void )
{
}
