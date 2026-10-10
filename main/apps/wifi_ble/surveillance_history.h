#pragma once

#include <stddef.h>

void surveillance_history_open( void );
void surveillance_history_sample( void );
void surveillance_history_flush( void );
void surveillance_history_compact( void );
