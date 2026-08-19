#include "oui_database.h"
#include <stddef.h>

typedef struct { uint32_t prefix; const char *brand; } oui_record_t;
#include "oui_database.inc"

const char *oui_database_lookup(const uint8_t address[6])
{
    if (address == NULL || (address[0] & 0x03U) != 0) return NULL;
    const uint32_t prefix = ((uint32_t)address[0] << 16) | ((uint32_t)address[1] << 8) | address[2];
    size_t low = 0, high = OUI_RECORD_COUNT;
    while (low < high) {
        const size_t mid = low + (high - low) / 2U;
        if (oui_records[mid].prefix < prefix) low = mid + 1U; else high = mid;
    }
    return low < OUI_RECORD_COUNT && oui_records[low].prefix == prefix ? oui_records[low].brand : NULL;
}
