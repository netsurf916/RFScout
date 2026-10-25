"""Host regression tests for the Hotspots streaming AP table and scan-time eviction."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL = ROOT / "main/apps/hotspots/hotspot_table.c"
GEO = ROOT / "main/services/location/geo.c"
RETENTION = ROOT / "main/services/wireless/observation_retention.c"
LOCATION = ROOT / "main/services/location"
APP = ROOT / "main/apps/hotspots"
WIRELESS = ROOT / "main/services/wireless"

ESP_ERR = """#pragma once
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
"""
WIFI = """#pragma once
#include <stdint.h>
#define RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH 32U
#define RFSCOUT_WIFI_STATION_SECURITY_OPEN 0
#define RFSCOUT_WIFI_STATION_SECURITY_OWE 1
#define RFSCOUT_WIFI_STATION_SECURITY_SECURED 2
typedef struct {
    char ssid[RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[6];
    uint8_t channel;
    int security;
    int auth_mode;
    int8_t rssi;
} rfscout_wifi_station_bss_t;
"""
SESSION = """#pragma once
#include <stddef.h>
void *app_session_calloc(size_t count, size_t size);
void *app_session_resize(void *pointer, size_t count, size_t size);
int app_session_free(void *pointer);
"""

HARNESS = r'''
#include "hotspot_table.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void *app_session_calloc(size_t count, size_t size) { return calloc(count, size); }
void *app_session_resize(void *pointer, size_t count, size_t size)
{
    return realloc(pointer, count * size);
}
int app_session_free(void *pointer) { free(pointer); return 1; }

static rfscout_wifi_station_bss_t bss(unsigned id, int security)
{
    rfscout_wifi_station_bss_t result = { 0 };
    strcpy(result.ssid, "test");
    result.bssid[0] = (uint8_t)(id >> 8);
    result.bssid[1] = (uint8_t)id;
    result.security = security;
    result.channel = 6;
    result.rssi = -50;
    return result;
}

static rfscout_gps_snapshot_t gps(double longitude)
{
    rfscout_gps_snapshot_t result = { 0 };
    result.location_valid = true;
    result.longitude_deg = longitude;
    return result;
}

static void observe(hotspot_table_t *table, rfscout_wifi_station_bss_t bss,
                    uint32_t clock_ms, const rfscout_gps_snapshot_t *gps)
{
    assert(hotspot_table_observe(table, &bss, clock_ms, gps) == ESP_OK);
}

static void finish(hotspot_table_t *table, uint32_t clock_ms,
                   hotspot_retention_t policy, const rfscout_gps_snapshot_t *location)
{
    hotspot_table_finish_sweep(table, clock_ms, &policy, location);
}

int main(void)
{
    hotspot_table_t table = { 0 };
    hotspot_retention_t retention = { .timeout_ms = 60000, .distance_threshold_m = 500 };
    rfscout_wifi_station_bss_t one = bss(1, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    rfscout_gps_snapshot_t origin = gps(0.0);
    rfscout_gps_snapshot_t moved = gps(0.001);
    rfscout_gps_snapshot_t near = gps(0.004);
    rfscout_gps_snapshot_t far = gps(0.007);
    rfscout_gps_snapshot_t farther = gps(0.018);

    /* A whole neighborhood's APs can be observed in one sweep, not 32 entries. */
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 1000, &origin);
    finish(&table, 1000, retention, &origin);
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 2000, &moved);
    finish(&table, 2000, retention, &moved);
    assert(table.count == 1 && table.entries[0].live_bounds.samples == 2);
    hotspot_table_begin_sweep(&table);
    finish(&table, 3000, retention, &near);
    assert(table.count == 1 && !table.entries[0].visible);
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 4000, &far);
    finish(&table, 4000, retention, &far);
    assert(table.count == 1 && table.entries[0].visible);
    hotspot_table_begin_sweep(&table);
    finish(&table, 5000, retention, &farther);
    assert(table.count == 0); /* 500m past observed coverage circle. */

    retention.distance_threshold_m = 0;
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 10000, NULL);
    finish(&table, 10000, retention, NULL);
    hotspot_table_begin_sweep(&table);
    finish(&table, 69999, retention, NULL);
    assert(table.count == 1 && !table.entries[0].visible);
    hotspot_table_begin_sweep(&table);
    finish(&table, 70000, retention, NULL);
    assert(table.count == 0);

    /* Connection/TLS time never advances the app's logical receive clock. */
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 120000, NULL);
    finish(&table, 120000, retention, NULL);
    hotspot_table_begin_sweep(&table);
    finish(&table, 120000, retention, NULL);
    assert(table.count == 1); /* Wall clock may have advanced minutes. */
    hotspot_table_reset(&table);

    /* More than 32 APs per sweep, with strict 128-entry memory ceiling. */
    hotspot_table_begin_sweep(&table);
    for (unsigned i = 0; i < 128; ++i)
        observe(&table, bss(i, RFSCOUT_WIFI_STATION_SECURITY_OPEN), 100, NULL);
    finish(&table, 100, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(table.open_count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(table.capacity <= HOTSPOT_TABLE_MAX_ENTRIES);
    hotspot_table_begin_sweep(&table);
    rfscout_wifi_station_bss_t newcomer = bss(1000, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    observe(&table, newcomer, 150, NULL);
    finish(&table, 150, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, newcomer.bssid) != NULL);
    assert(hotspot_table_find(&table, bss(0, RFSCOUT_WIFI_STATION_SECURITY_OPEN).bssid) == NULL);
    hotspot_table_reset(&table);

    /* Capacity: prefer reclaiming absent secured networks over guest networks. */
    hotspot_table_begin_sweep(&table);
    for (unsigned i = 0; i < 128; ++i)
    {
        int security = i < 32 ? RFSCOUT_WIFI_STATION_SECURITY_OPEN :
                       i < 64 ? RFSCOUT_WIFI_STATION_SECURITY_OWE :
                                RFSCOUT_WIFI_STATION_SECURITY_SECURED;
        observe(&table, bss(i, security), 100, NULL);
    }
    finish(&table, 100, retention, NULL);
    hotspot_table_begin_sweep(&table);
    rfscout_wifi_station_bss_t guest_new = bss(1002, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    observe(&table, guest_new, 300, NULL);
    finish(&table, 300, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, bss(64, RFSCOUT_WIFI_STATION_SECURITY_SECURED).bssid) == NULL);
    assert(hotspot_table_find(&table, bss(0, RFSCOUT_WIFI_STATION_SECURITY_OPEN).bssid) != NULL);
    hotspot_table_reset(&table);

    /* All guest records protected from a secured new arrival; refuse admission. */
    hotspot_table_begin_sweep(&table);
    for (unsigned i = 0; i < 128; ++i)
        observe(&table, bss(i, RFSCOUT_WIFI_STATION_SECURITY_OWE), 100, NULL);
    finish(&table, 100, retention, NULL);
    hotspot_table_begin_sweep(&table);
    rfscout_wifi_station_bss_t secured = bss(2000, RFSCOUT_WIFI_STATION_SECURITY_SECURED);
    assert(hotspot_table_observe(&table, &secured, 200, NULL) == ESP_ERR_NO_MEM);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);

    /* Manual requests survive normal expiry and capacity eviction. */
    hotspot_entry_t *protected = hotspot_table_find(&table, bss(0, RFSCOUT_WIFI_STATION_SECURITY_OWE).bssid);
    assert(protected != NULL);
    protected->manual_requested = true;
    hotspot_table_begin_sweep(&table);
    finish(&table, 120001, retention, NULL);
    assert(hotspot_table_find(&table, bss(0, RFSCOUT_WIFI_STATION_SECURITY_OWE).bssid) != NULL);
    assert(table.count == 1);
    hotspot_table_reset(&table);
    return 0;
}
'''


class HotspotsRetentionTests(unittest.TestCase):
    def test_bounded_retention(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            directory = pathlib.Path(folder)
            (directory / "esp_err.h").write_text(ESP_ERR)
            (directory / "wifi_station.h").write_text(WIFI)
            (directory / "app_session.h").write_text(SESSION)
            source = directory / "harness.c"
            source.write_text(HARNESS)
            binary = directory / "harness"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(directory), "-I", str(APP), "-I", str(LOCATION),
                 "-I", str(WIRELESS), str(source), str(MODEL),
                 str(RETENTION), str(GEO), "-lm", "-o", str(binary)],
                check=True
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
