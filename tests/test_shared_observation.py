"""Host tests for shared, allocation-free AP decoding, retention and journal envelopes."""
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIRELESS = ROOT / "main/services/wireless"
STORAGE = ROOT / "main/services/storage"
LOCATION = ROOT / "main/services/location"

AP_HARNESS = r'''
#include "wifi_ap_observation.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static unsigned char frame[256];
static size_t used;

static void begin(int privacy, int probe)
{
    memset(frame, 0, sizeof(frame));
    frame[0] = probe ? 0x50 : 0x80;
    frame[16] = 0x02; /* Valid unicast BSSID. */
    frame[17] = 0x11;
    frame[18] = 0x22;
    frame[19] = 0x33;
    frame[20] = 0x44;
    frame[21] = 0x55;
    if (privacy)
        frame[34] = 0x10;
    used = 36;
}

static void ie(unsigned id, const unsigned char *value, size_t count)
{
    assert(used + 2 + count < sizeof(frame) && count <= 255);
    frame[used++] = (unsigned char)id;
    frame[used++] = (unsigned char)count;
    memcpy(&frame[used], value, count);
    used += count;
}

static void ssid(void)
{
    static const unsigned char name[] = { 'C', 'a', 'f', 'e' };
    ie(0, name, sizeof(name));
}

static void rsn(const unsigned char *akm, size_t count)
{
    unsigned char payload[64] = {
        1, 0,                   /* Version. */
        0x00, 0x0f, 0xac, 4,  /* Group cipher. */
        1, 0,                   /* One pairwise cipher. */
        0x00, 0x0f, 0xac, 4,  /* Pairwise CCMP. */
        (unsigned char)count, 0
    };
    for (size_t index = 0; index < count; ++index)
    {
        payload[14 + index * 4] = 0;
        payload[15 + index * 4] = 0x0f;
        payload[16 + index * 4] = 0xac;
        payload[17 + index * 4] = akm[index];
    }
    ie(48, payload, 14 + count * 4);
}

static rfscout_wifi_ap_observation_t parse(void)
{
    rfscout_wifi_ap_observation_t ap = {0};
    assert(wifi_ap_observation_parse(frame, used, -65, 11, &ap));
    assert(ap.rssi == -65 && ap.channel == 11);
    return ap;
}

int main(void)
{
    begin(0, 0); ssid();
    rfscout_wifi_ap_observation_t ap = parse();
    assert(ap.ssid_length == 4 && strcmp(ap.ssid, "Cafe") == 0);
    assert(ap.security == RFSCOUT_WIFI_AP_SECURITY_OPEN);
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_OPEN);
    assert(ap.bssid[0] == 2 && ap.bssid[5] == 0x55);

    begin(1, 1); ssid();
    ap = parse();
    assert(ap.security == RFSCOUT_WIFI_AP_SECURITY_SECURED);
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_WEP);

    const unsigned char psk = 2, sae = 8, owe = 18, enterprise = 1, unknown = 99;
    begin(1, 0); ssid(); rsn(&psk, 1);
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_WPA2_PSK);

    begin(1, 0); ssid(); rsn(&sae, 1);
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_WPA3_SAE);

    const unsigned char mixed[2] = {2, 8};
    begin(1, 0); ssid(); rsn(mixed, 2);
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_WPA2_WPA3_PERSONAL);

    begin(1, 0); ssid(); rsn(&owe, 1);
    ap = parse();
    assert(ap.security == RFSCOUT_WIFI_AP_SECURITY_OWE);
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_OWE);

    begin(1, 0); ssid(); rsn(&enterprise, 1);
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_ENTERPRISE);

    begin(1, 0); ssid(); rsn(&unknown, 1);
    ap = parse();
    assert(ap.security == RFSCOUT_WIFI_AP_SECURITY_SECURED);
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_UNKNOWN);

    static const unsigned char legacy_wpa[] = { 0, 0x50, 0xf2, 1 };
    begin(1, 0); ssid(); ie(221, legacy_wpa, sizeof(legacy_wpa));
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_WPA);

    begin(0, 0);
    ap = parse();
    assert(ap.ssid_length == 0);

    begin(0, 0); ssid(); ssid();
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, &ap));

    begin(0, 0); ssid();
    frame[used++] = 48; frame[used++] = 12; frame[used++] = 1;
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, &ap));

    begin(1, 0); ssid();
    static const unsigned char malformed_rsn[] = { 1, 0, 0, 0x0f, 0xac, 4, 0xff, 0xff };
    ie(48, malformed_rsn, sizeof(malformed_rsn));
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, &ap));

    begin(0, 0); ssid();
    frame[16] = 0x01;
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, &ap));

    begin(0, 0); ssid();
    frame[1] = 0x08; /* Retry bit does not invalidate a management frame. */
    ap = parse();
    assert(ap.auth == RFSCOUT_WIFI_AP_AUTH_OPEN);

    begin(0, 0); ssid();
    frame[0] = 0x08; /* Data frame, not beacon. */
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, &ap));

    assert(!wifi_ap_observation_parse(frame, 12, -60, 1, &ap));
    assert(!wifi_ap_observation_parse(frame, used, -60, 1, NULL));
    return 0;
}
'''

RETENTION_HARNESS = r'''
#include "observation_retention.h"
#include "observation_journal.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(!observation_retention_expired(2000, 1000, 1500, 0));
    assert(observation_retention_expired(2500, 1000, 1500, 0));
    assert(!observation_retention_expired(2500, 1000, 1500, 300));
    assert(!observation_retention_expired(2500, 1000, 0, 0));
    assert(observation_retention_expired(11, UINT32_MAX - 9, 20, 0));
    assert(observation_retention_rssi_ema(-80, -40) == -70);

    assert(!observation_retention_over_budget(100, 1000, 0));
    assert(!observation_retention_over_budget(60, 40, 100));
    assert(observation_retention_over_budget(60, 41, 100));
    assert(observation_retention_over_budget(SIZE_MAX, 1, 100));
    geo_bounds_t bounds = {0};
    geo_bounds_extend(&bounds, 0.0, 0.0);
    assert(!observation_retention_outside_coverage(&bounds, 0, 0.004, 500, true, false));
    assert(observation_retention_outside_coverage(&bounds, 0, 0.008, 500, true, false));
    assert(!observation_retention_outside_coverage(&bounds, 0, 0.008, 500, false, false));
    assert(!observation_retention_outside_coverage(&bounds, 0, 0.008, 500, true, true));
    assert(!observation_retention_outside_coverage(&bounds, 0, 0.008, 0, true, false));

    const observation_journal_record_t events[2] = {
        { .source = RF_SOURCE_WIFI, .address = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
          .address_type = 0, .utc_epoch = 1730000000, .seen_ms = 12345,
          .rssi = -69, .channel = 11, .location_valid = true,
          .latitude = 38.1234567, .longitude = -94.1234567 },
        { .source = RF_SOURCE_BLE, .address = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff},
          .address_type = 1, .rssi = -41 }
    };
    char line[512];
    assert(observation_journal_format_record((void *)events, 0, line, sizeof(line)) > 0);
    puts(line);
    assert(observation_journal_format_record((void *)events, 1, line, sizeof(line)) > 0);
    puts(line);
    int length = observation_journal_format_prefix(events, line, sizeof(line));
    assert(length > 0);
    snprintf(&line[length], sizeof(line) - (size_t)length, ",\"app\":\"test\"}");
    puts(line);

    assert(observation_journal_format_record((void *)events, 0, line, 8) < 0);
    assert(observation_journal_format_prefix(NULL, line, sizeof(line)) < 0);
    observation_journal_record_t invalid = { .source = (rf_source_t)100 };
    assert(observation_journal_format_prefix(&invalid, line, sizeof(line)) < 0);
    return 0;
}
'''


def run_host(source, paths, includes):
    compiler = shutil.which("cc")
    if not compiler:
        raise unittest.SkipTest("C compiler unavailable")
    with tempfile.TemporaryDirectory() as tmp:
        harness = pathlib.Path(tmp) / "harness.c"
        output = pathlib.Path(tmp) / "harness"
        harness.write_text(source)
        cmd = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror"]
        cmd += [arg for include in includes for arg in ("-I", str(include))]
        cmd += [str(harness), *[str(path) for path in paths], "-lm", "-o", str(output)]
        subprocess.run(cmd, check=True)
        return subprocess.run([str(output)], check=True, capture_output=True, text=True).stdout


class SharedObservationTests(unittest.TestCase):
    def test_ap_metadata(self):
        run_host(AP_HARNESS, [WIRELESS / "wifi_ap_observation.c"], [WIRELESS])

    def test_retention_and_journal_schema(self):
        output = run_host(
            RETENTION_HARNESS,
            [WIRELESS / "observation_retention.c",
             STORAGE / "observation_journal.c",
             LOCATION / "geo.c"],
            [WIRELESS, STORAGE, LOCATION],
        )
        first, second, extended = [json.loads(line) for line in output.splitlines()]
        self.assertEqual(first["schema"], 1)
        self.assertEqual(first["type"], "observation")
        self.assertEqual(first["source"], "wifi")
        self.assertEqual(first["address"], "02:11:22:33:44:55")
        self.assertEqual(first["utc"], 1730000000)
        self.assertEqual(first["seen_ms"], 12345)
        self.assertEqual(first["rssi"], -69)
        self.assertEqual(first["channel"], 11)
        self.assertAlmostEqual(first["lon"], -94.1234567)
        self.assertEqual(second["source"], "ble")
        self.assertNotIn("lat", second)
        self.assertEqual(extended["app"], "test")


if __name__ == "__main__":
    unittest.main()
