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
    hotspot_credential_t credential = bss.security == RFSCOUT_WIFI_STATION_SECURITY_OPEN ||
                                      bss.security == RFSCOUT_WIFI_STATION_SECURITY_OWE
                                          ? HOTSPOT_CREDENTIAL_OPEN : HOTSPOT_CREDENTIAL_NONE;
    assert(hotspot_table_observe(table, &bss, clock_ms, gps, credential, 0) == ESP_OK);
}

static esp_err_t observe_credential(hotspot_table_t *table, rfscout_wifi_station_bss_t bss,
                                    uint32_t clock_ms, hotspot_credential_t credential,
                                    uint32_t profile_id)
{
    return hotspot_table_observe(table, &bss, clock_ms, NULL, credential, profile_id);
}

static rfscout_wifi_station_bss_t tier_bss(unsigned id, unsigned tier)
{
    return bss(id, tier == 1U ? RFSCOUT_WIFI_STATION_SECURITY_OPEN
                               : RFSCOUT_WIFI_STATION_SECURITY_SECURED);
}

static hotspot_credential_t tier_credential(unsigned tier)
{
    return tier == 1U ? HOTSPOT_CREDENTIAL_OPEN :
           tier == 2U ? HOTSPOT_CREDENTIAL_KNOWN :
           tier == 3U ? HOTSPOT_CREDENTIAL_UNMATCHED : HOTSPOT_CREDENTIAL_UNSUPPORTED;
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
    assert(table.count == 1 && table.entries[0].visible); /* Still recent after a missed sweep. */
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

    /* Recent visibility persists across scan boundaries, but ages on monitor time. */
    hotspot_table_begin_sweep(&table);
    observe(&table, one, 1000, NULL);
    finish(&table, 1000, retention, NULL);
    hotspot_table_begin_sweep(&table);
    assert(table.entries[0].visible);
    hotspot_table_refresh_visibility(&table, 8999);
    assert(table.entries[0].visible && table.open_count == 1);
    hotspot_table_refresh_visibility(&table, 9000);
    assert(!table.entries[0].visible && table.open_count == 0);
    observe(&table, one, 9001, NULL);
    assert(table.entries[0].visible);
    finish(&table, 9001, retention, NULL);
    assert(table.entries[0].visible);
    hotspot_table_reset(&table);

    rfscout_wifi_station_bss_t guest_old = bss(0, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    rfscout_wifi_station_bss_t secured_old = bss(64, RFSCOUT_WIFI_STATION_SECURITY_SECURED);
    rfscout_wifi_station_bss_t owe_old = bss(0, RFSCOUT_WIFI_STATION_SECURITY_OWE);

    /* More than 32 APs per sweep, with strict 128-entry memory ceiling. */
    hotspot_table_begin_sweep(&table);
    for (unsigned i = 0; i < 128; ++i)
        observe(&table, bss(i, RFSCOUT_WIFI_STATION_SECURITY_OPEN), 100, NULL);
    finish(&table, 100, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(table.open_count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(table.capacity <= HOTSPOT_TABLE_MAX_ENTRIES);
    hotspot_table_begin_sweep(&table);
    assert(table.entries[0].visible); /* Recent entries stay visible during a sweep. */
    rfscout_wifi_station_bss_t protected_guest = bss(1, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    observe(&table, protected_guest, 150, NULL);
    rfscout_wifi_station_bss_t newcomer = bss(1000, RFSCOUT_WIFI_STATION_SECURITY_OPEN);
    observe(&table, newcomer, 150, NULL);
    finish(&table, 150, retention, NULL);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
    assert(hotspot_table_find(&table, newcomer.bssid) != NULL);
    assert(hotspot_table_find(&table, protected_guest.bssid) != NULL);
    assert(hotspot_table_find(&table, guest_old.bssid) == NULL);
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
    assert(hotspot_table_find(&table, secured_old.bssid) == NULL);
    assert(hotspot_table_find(&table, guest_old.bssid) != NULL);
    hotspot_table_reset(&table);

    /* All guest records protected from a secured new arrival; refuse admission. */
    hotspot_table_begin_sweep(&table);
    for (unsigned i = 0; i < 128; ++i)
        observe(&table, bss(i, RFSCOUT_WIFI_STATION_SECURITY_OWE), 100, NULL);
    finish(&table, 100, retention, NULL);
    hotspot_table_begin_sweep(&table);
    rfscout_wifi_station_bss_t secured = bss(2000, RFSCOUT_WIFI_STATION_SECURITY_SECURED);
    assert(hotspot_table_observe(&table, &secured, 200, NULL,
                                 HOTSPOT_CREDENTIAL_NONE, 0) == ESP_ERR_NO_MEM);
    assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);

    /* Manual requests survive normal expiry and capacity eviction. */
    hotspot_entry_t *protected = hotspot_table_find(&table, owe_old.bssid);
    assert(protected != NULL);
    protected->manual_requested = true;
    hotspot_table_begin_sweep(&table);
    finish(&table, 120001, retention, NULL);
    assert(hotspot_table_find(&table, owe_old.bssid) != NULL);
    assert(table.count == 1);
    hotspot_table_reset(&table);

    /* Every incoming tier against every incumbent tier, including equal-tier
       replacement with all 128 incumbents heard in the same sweep. */
    for (unsigned incumbent = 1U; incumbent <= 4U; ++incumbent)
    {
        for (unsigned incoming = 1U; incoming <= 4U; ++incoming)
        {
            hotspot_table_begin_sweep(&table);
            for (unsigned id = 0U; id < HOTSPOT_TABLE_MAX_ENTRIES; ++id)
            {
                assert(observe_credential(&table, tier_bss(id, incumbent), 100U,
                                          tier_credential(incumbent), 42U) == ESP_OK);
            }
            rfscout_wifi_station_bss_t candidate = tier_bss(1000U, incoming);
            const esp_err_t result = observe_credential(&table, candidate, 101U,
                                                         tier_credential(incoming), 17U);
            assert(result == (incoming <= incumbent ? ESP_OK : ESP_ERR_NO_MEM));
            assert(table.count == HOTSPOT_TABLE_MAX_ENTRIES);
            assert(table.capacity <= HOTSPOT_TABLE_MAX_ENTRIES);
            assert((hotspot_table_find(&table, candidate.bssid) != NULL) ==
                   (incoming <= incumbent));
            rfscout_wifi_station_bss_t oldest = tier_bss(0U, incumbent);
            assert((hotspot_table_find(&table, oldest.bssid) == NULL) ==
                   (incoming <= incumbent));
            hotspot_table_reset(&table);
        }
    }

    /* The lowest tier yields before older/higher-priority records. */
    hotspot_table_begin_sweep(&table);
    for (unsigned id = 0U; id < HOTSPOT_TABLE_MAX_ENTRIES; ++id)
    {
        const unsigned tier = id / 32U + 1U;
        assert(observe_credential(&table, tier_bss(id, tier), 100U,
                                  tier_credential(tier), tier) == ESP_OK);
    }
    rfscout_wifi_station_bss_t guest_first = tier_bss(0U, 1U);
    rfscout_wifi_station_bss_t unsupported_oldest = tier_bss(96U, 4U);
    assert(observe_credential(&table, tier_bss(1000U, 2U), 120U,
                              HOTSPOT_CREDENTIAL_INFERRED, 9U) == ESP_OK);
    assert(hotspot_table_find(&table, unsupported_oldest.bssid) == NULL);
    assert(hotspot_table_find(&table, guest_first.bssid) != NULL);
    rfscout_wifi_station_bss_t newcomer = tier_bss(1000U, 2U);
    assert(hotspot_table_find(&table, newcomer.bssid)->profile_id == 9U);
    hotspot_table_reset(&table);

    /* At equal ages, weaker RSSI yields; exact ties use deterministic BSSID order. */
    hotspot_table_begin_sweep(&table);
    for (unsigned id = 0U; id < HOTSPOT_TABLE_MAX_ENTRIES; ++id)
    {
        rfscout_wifi_station_bss_t value = tier_bss(id, 1U);
        value.rssi = (id == 60U || id == 61U) ? -85 : -50;
        assert(observe_credential(&table, value, 100U, HOTSPOT_CREDENTIAL_OPEN, 0U) == ESP_OK);
    }
    assert(observe_credential(&table, tier_bss(1000U, 1U), 110U,
                              HOTSPOT_CREDENTIAL_OPEN, 0U) == ESP_OK);
    rfscout_wifi_station_bss_t weak = tier_bss(60U, 1U);
    assert(hotspot_table_find(&table, weak.bssid) == NULL);
    weak = tier_bss(61U, 1U);
    assert(hotspot_table_find(&table, weak.bssid) != NULL);
    hotspot_table_reset(&table);

    /* Manual work is an absolute capacity protection, even for lower tiers. */
    hotspot_table_begin_sweep(&table);
    for (unsigned id = 0U; id < HOTSPOT_TABLE_MAX_ENTRIES; ++id)
    {
        assert(observe_credential(&table, tier_bss(id, 4U), 100U,
                                  HOTSPOT_CREDENTIAL_UNSUPPORTED, 0U) == ESP_OK);
        table.entries[id].manual_requested = true;
    }
    assert(observe_credential(&table, tier_bss(1000U, 1U), 110U,
                              HOTSPOT_CREDENTIAL_OPEN, 0U) == ESP_ERR_NO_MEM);
    table.entries[5].manual_requested = false;
    assert(observe_credential(&table, tier_bss(1000U, 1U), 110U,
                              HOTSPOT_CREDENTIAL_OPEN, 0U) == ESP_OK);
    hotspot_table_reset(&table);

    /* Re-observation refreshes an incumbent's credential tier; there is no
       stale permanent ranking after the saved-profile evidence changes. */
    hotspot_table_begin_sweep(&table);
    for (unsigned id = 0U; id < HOTSPOT_TABLE_MAX_ENTRIES; ++id)
    {
        assert(observe_credential(&table, tier_bss(id, 2U), 100U,
                                  HOTSPOT_CREDENTIAL_KNOWN, 8U) == ESP_OK);
    }
    assert(observe_credential(&table, tier_bss(1000U, 3U), 110U,
                              HOTSPOT_CREDENTIAL_NONE, 0U) == ESP_ERR_NO_MEM);
    assert(observe_credential(&table, tier_bss(0U, 2U), 100U,
                              HOTSPOT_CREDENTIAL_UNMATCHED, 0U) == ESP_OK);
    assert(observe_credential(&table, tier_bss(1000U, 3U), 110U,
                              HOTSPOT_CREDENTIAL_NONE, 0U) == ESP_OK);
    rfscout_wifi_station_bss_t changed = tier_bss(0U, 2U);
    assert(hotspot_table_find(&table, changed.bssid) == NULL);
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
