"""Host regression for per-location surveillance RSSI persistence.

Compile the production location-record formatter with a minimal host fixture. This
keeps the check independent of ESP-IDF while exercising the JSONL field values.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HISTORY = ROOT / "main/apps/wifi_ble/surveillance_history.c"
STORE = ROOT / "main/apps/wifi_ble/observation_store.c"
HEADER = ROOT / "main/apps/wifi_ble/observation_store.h"

# The formatter is private, so compile the original function in isolation with
# the same journal printf contract. Avoid introducing another runtime API.
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef struct
{
    uint8_t address[6];
    uint8_t address_type;
    int classification;
    double latitude;
    double longitude;
    int64_t utc_epoch;
    uint32_t seen_ms;
    int8_t rssi;
} observation_location_event_t;
typedef observation_location_event_t history_point_t;

static const char *address_type_name(uint8_t type)
{
    return type == 0 ? "public_or_wifi" : "random";
}
static const char *surveillance_name(int classification)
{
    (void)classification;
    return "Camera";
}
static int media_journal_printf(char *line, size_t capacity, int length, const char *format, ...)
{
    if (length < 0 || (size_t)length >= capacity)
        return -1;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(line + length, capacity - (size_t)length, format, args);
    va_end(args);
    return written < 0 || (size_t)written >= capacity - (size_t)length ? -1 : length + written;
}
'''
# Extract the actual production serializer to avoid copying its format string.
def formatter_source():
    source = HISTORY.read_text()
    begin = source.index("static int format_point(")
    end = source.index("\nstatic bool surveillance_history_flush_one(", begin)
    return source[begin:end]


class SurveillanceRssiTests(unittest.TestCase):
    def test_location_rssi_format(self):
        if not shutil.which("cc"):
            self.skipTest("C compiler unavailable")
        # Guard the producer: the journal must use the latest merged device
        # RSSI at the time the movement-triggered location event is generated.
        source = STORE.read_text()
        start = source.index("static bool record_receiver_location(")
        end = source.index("\nbool observation_store_observe(", start)
        self.assertIn("a_event->rssi = a_device->rssi;", source[start:end])
        self.assertIn("int8_t rssi;", HEADER.read_text())

        harness = HARNESS + formatter_source() + r'''
int main(void)
{
    const history_point_t records[] = {
        { .address = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
          .latitude = 40.0, .longitude = -94.0, .seen_ms = 100,
          .utc_epoch = 1700000000, .rssi = -43 },
        { .address = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
          .latitude = 40.0002, .longitude = -94.0, .seen_ms = 200,
          .rssi = -81 },
    };
    char line[512];
    assert(format_point((void *)records, 0, line, sizeof(line)) > 0);
    assert(strstr(line, "\"seen_ms\":100,\"rssi\":-43}") != NULL);
    assert(strstr(line, "\"utc\":1700000000") != NULL);
    assert(format_point((void *)records, 1, line, sizeof(line)) > 0);
    assert(strstr(line, "\"seen_ms\":200,\"rssi\":-81}") != NULL);
    assert(strstr(line, "\"utc\"") == NULL);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_file = pathlib.Path(directory) / "test.c"
            binary = pathlib.Path(directory) / "test"
            source_file.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source_file), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
