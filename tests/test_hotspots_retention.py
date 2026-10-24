"""Host regression tests for Hotspots bounded Live eviction and GPS coverage."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL = ROOT / "main/apps/hotspots/hotspot_table.c"
GEO = ROOT / "main/services/location/geo.c"
LOCATION = ROOT / "main/services/location"
APP = ROOT / "main/apps/hotspots"

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

static rfscout_wifi_station_bss_t bss(unsigned id)
{
    rfscout_wifi_station_bss_t result = { 0 };
    strcpy(result.ssid, "test");
    result.bssid[0] = (uint8_t)(id >> 8);
    result.bssid[1] = (uint8_t)id;
    result.security = RFSCOUT_WIFI_STATION_SECURITY_OPEN;
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
static void scan(hotspot_table_t *table, const rfscout_wifi_station_bss_t *records,
                 size_t count, hotspot_retention_t policy, const rfscout_gps_snapshot_t *location)
{
    assert(hotspot_table_apply_scan(table, records, count, &policy, location) == ESP_OK);
}

int main(void)
{
    hotspot_table_t table = { 0 };
    hotspot_retention_t retention = { 3, 500 };
    rfscout_wifi_station_bss_t one = bss(1);
    rfscout_gps_snapshot_t origin = gps(0.0);
    rfscout_gps_snapshot_t moved = gps(0.001);
    rfscout_gps_snapshot_t near = gps(0.004);
    rfscout_gps_snapshot_t far = gps(0.007);
    rfscout_gps_snapshot_t farther = gps(0.018);

    scan(&table, &one, 1, retention, &origin);
    scan(&table, &one, 1, retention, &moved);
    assert(table.count == 1 && table.entries[0].live_bounds.samples == 2);
    scan(&table, NULL, 0, retention, &near);
    assert(table.count == 1 && !table.entries[0].visible);
    scan(&table, &one, 1, retention, &far);
    assert(table.count == 1 && table.entries[0].visible);
    scan(&table, NULL, 0, retention, &farther);
    assert(table.count == 0); /* 500m beyond half-diagonal of live bounds. */

    scan(&table, &one, 1, retention, NULL);
    scan(&table, NULL, 0, retention, NULL);
    assert(table.count == 1 && table.entries[0].missed_scans == 1);
    scan(&table, NULL, 0, retention, NULL);
    assert(table.count == 1 && table.entries[0].missed_scans == 2);
    scan(&table, NULL, 0, retention, NULL);
    assert(table.count == 0);
    hotspot_table_reset(&table);
    assert(table.entries == NULL && table.capacity == 0);

    /* A wider missed-scan window allows a busy Live table to reach capacity. */
    retention.missed_scan_limit = 10;
    retention.distance_threshold_m = 0;
    rfscout_wifi_station_bss_t batch[32];
    for (unsigned round = 0; round < 4; ++round)
    {
        for (unsigned i = 0; i < 32; ++i)
            batch[i] = bss(round * 32 + i);
        scan(&table, batch, 32, retention, NULL);
    }
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(table.capacity <= HOTSPOT_TABLE_MAX_ENTRIES);
    rfscout_wifi_station_bss_t newcomer = bss(1000);
    scan(&table, &newcomer, 1, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, newcomer.bssid) != NULL);
    rfscout_wifi_station_bss_t oldest = bss(0);
    assert(hotspot_table_find(&table, oldest.bssid) == NULL);

    /* A BSSID observed in the current scan is protected even if listed last. */
    batch[0] = bss(1001);
    batch[1] = bss(1);
    scan(&table, batch, 2, retention, NULL);
    assert(hotspot_table_find(&table, batch[1].bssid) != NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    hotspot_table_reset(&table);

    /* Preserve guest-facing OPEN and OWE entries preferentially under pressure. */
    for (unsigned round = 0; round < 4; ++round)
    {
        for (unsigned i = 0; i < 32; ++i)
        {
            const unsigned id = round * 32 + i;
            batch[i] = bss(id);
            batch[i].security = id < 32 ? RFSCOUT_WIFI_STATION_SECURITY_OPEN :
                                id < 64 ? RFSCOUT_WIFI_STATION_SECURITY_OWE :
                                          RFSCOUT_WIFI_STATION_SECURITY_SECURED;
        }
        scan(&table, batch, 32, retention, NULL);
    }
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    rfscout_wifi_station_bss_t guest_new = bss(1002);
    scan(&table, &guest_new, 1, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, guest_new.bssid) != NULL);
    rfscout_wifi_station_bss_t secured_oldest = bss(64);
    assert(hotspot_table_find(&table, secured_oldest.bssid) == NULL);
    rfscout_wifi_station_bss_t guest_oldest = bss(0);
    assert(hotspot_table_find(&table, guest_oldest.bssid) != NULL);
    rfscout_wifi_station_bss_t owe_oldest = bss(32);
    assert(hotspot_table_find(&table, owe_oldest.bssid) != NULL);

    rfscout_wifi_station_bss_t secured_new = bss(1003);
    secured_new.security = RFSCOUT_WIFI_STATION_SECURITY_SECURED;
    scan(&table, &secured_new, 1, retention, NULL);
    assert(hotspot_table_find(&table, secured_new.bssid) != NULL);
    rfscout_wifi_station_bss_t secured_second = bss(65);
    assert(hotspot_table_find(&table, secured_second.bssid) == NULL);

    /* A secured newcomer cannot displace a guest candidate even when all are absent. */
    hotspot_table_reset(&table);
    for (unsigned round = 0; round < 4; ++round)
    {
        for (unsigned i = 0; i < 32; ++i)
        {
            batch[i] = bss(round * 32 + i);
            batch[i].security = round < 2 ? RFSCOUT_WIFI_STATION_SECURITY_OPEN :
                                           RFSCOUT_WIFI_STATION_SECURITY_OWE;
        }
        scan(&table, batch, 32, retention, NULL);
    }
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_apply_scan(&table, &secured_new, 1, &retention, NULL) == ESP_ERR_NO_MEM);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, bss(0).bssid) != NULL);
    assert(hotspot_table_find(&table, secured_new.bssid) == NULL);
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
                 str(source), str(MODEL), str(GEO), "-lm", "-o", str(binary)],
                check=True
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
