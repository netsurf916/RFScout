"""Host regression for independent, bounded Hotspots sightings before Live admission.

Links the production sightings writer and shared observation JSONL serializer against the
existing in-memory media-journal fixture. No ESP-IDF build or hardware is required.
"""
import importlib.util
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
STORAGE = ROOT / "main/services/storage"
HOTSPOTS = ROOT / "main/apps/hotspots"
WIRELESS = ROOT / "main/services/wireless"
LOCATION = ROOT / "main/services/location"
MAIN = ROOT / "main"

spec = importlib.util.spec_from_file_location(
    "media_journal_test_fixture", TESTS / "test_media_journal_replay.py")
fixture = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(fixture)

STUBS = dict(fixture.STUBS)
STUBS["esp_err.h"] += "const char *esp_err_to_name( esp_err_t a_error );\n"
STUBS["app_session.h"] = """#pragma once
#include <stddef.h>
void *app_session_calloc( size_t a_count, size_t a_size );
"""
STUBS["device_config.h"] = """#pragma once
#include <stdint.h>
typedef struct
{
    uint8_t gps_movement_m;
} device_config_t;
const device_config_t *device_config_get( void );
"""

HARNESS = fixture.MEMORY_STORE + r'''
#include "app_session.h"
#include "device_config.h"
#include "hotspot_sightings.h"
#include "observation_metadata.h"

#include <stdlib.h>

const char *esp_err_to_name( esp_err_t error )
{
    (void)error;
    return "error";
}

void *app_session_calloc( size_t count, size_t size )
{
    return calloc( count, size );
}

const device_config_t *device_config_get( void )
{
    static const device_config_t config = { .gps_movement_m = 10U };
    return &config;
}

esp_err_t media_app_collection( const char *name, rfscout_media_collection_t *collection )
{
    memset( collection, 0, sizeof( *collection ) );
    collection->app = RFSCOUT_APP_HOTSPOTS;
    collection->generation = 1U;
    snprintf( collection->collection, sizeof( collection->collection ), "%s", name );
    return ESP_OK;
}

static rfscout_wifi_station_bss_t bss( unsigned id )
{
    rfscout_wifi_station_bss_t result = { 0 };
    result.bssid[ 5 ] = (uint8_t)id;
    snprintf( result.ssid, sizeof( result.ssid ), "Cafe%u", id );
    result.auth_mode = 3;
    result.security = RFSCOUT_WIFI_STATION_SECURITY_OPEN;
    result.rssi = -40;
    result.channel = 6;
    return result;
}

static rfscout_observation_metadata_t observation( bool located, double latitude )
{
    return ( rfscout_observation_metadata_t ){
        .seen_ms = 125U,
        .utc_epoch = 1700000300LL,
        .location_valid = located,
        .latitude = latitude,
        .longitude = -100.0
    };
}

static unsigned occurrences( const store_file_t *file, const char *needle )
{
    unsigned found = 0U;
    const size_t length = strlen( needle );
    for( size_t index = 0U; index + length <= file->size; ++index )
    {
        found += 0 == memcmp( &file->data[ index ], needle, length );
    }
    return found;
}

static void run( void )
{
    store_reset();
    /* The collection must ignore unrelated files without disturbing them. */
    put( "unrelated.jsonl", "{}\\n" );
    assert( exists( "unrelated.jsonl" ) && holds( "unrelated.jsonl", "{}\\n" ) );
    assert( ESP_OK == hotspot_sightings_open() );
    rfscout_wifi_station_bss_t cafe = bss( 1U );
    rfscout_observation_metadata_t metadata = observation( false, 0.0 );

    hotspot_sightings_observe( &cafe, &metadata, false ); /* No authorization to journal. */
    hotspot_sightings_observe( &cafe, &metadata, true );
    hotspot_sightings_observe( &cafe, &metadata, true ); /* Same visit, no movement. */
    metadata = observation( true, 40.0 );
    hotspot_sightings_observe( &cafe, &metadata, true ); /* First GPS location. */
    hotspot_sightings_observe( &cafe, &metadata, true ); /* Stationary duplicate. */
    metadata = observation( true, 40.0002 );          /* About 22 m north. */
    hotspot_sightings_observe( &cafe, &metadata, true );
    cafe = bss( 2U );
    hotspot_sightings_observe( &cafe, &metadata, true );

    hotspot_sightings_poll( 1000000 );
    store_file_t *file = find_file( "hotspots-00000001.jsonl" );
    assert( file != NULL );
    assert( 4U == occurrences( file, "\"type\":\"observation\"" ) );
    assert( 3U == occurrences( file, "\"ssid\":\"Cafe1\"" ) );
    assert( 1U == occurrences( file, "\"ssid\":\"Cafe2\"" ) );
    assert( 3U == occurrences( file, "\"lat\":" ) ); /* Two Cafe1 points and Cafe2. */
    assert( occurrences( file, "\"utc\":1700000300" ) == 4U );
    assert( 0U == occurrences( file, "\"status\":" ) ); /* No invented check result. */
    assert( 0U == open_handles() );
    /* Changed SSID on the same BSSID starts a new sighting identity. */
    cafe = bss( 1U );
    snprintf( cafe.ssid, sizeof( cafe.ssid ), "Renamed" );
    hotspot_sightings_observe( &cafe, &metadata, true );
    hotspot_sightings_poll( 2000000 );
    assert( 1U == occurrences( file, "\"ssid\":\"Renamed\"" ) );
    hotspot_sightings_close();
    store_reboot();

    /* Re-entry clears the de-duplication cache, but keeps existing journal data. */
    assert( ESP_OK == hotspot_sightings_open() );
    cafe = bss( 1U );
    hotspot_sightings_observe( &cafe, &metadata, true );
    hotspot_sightings_close();
    file = find_file( "hotspots-00000001.jsonl" );
    assert( file != NULL );
    assert( 6U == occurrences( file, "\"type\":\"observation\"" ) );
    assert( 0U == open_handles() );
}

int main( void )
{
    run();
    return 0;
}
'''


class HotspotsSightingsTests(unittest.TestCase):
    def test_bounded_eligible_sightings(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            temp = pathlib.Path(folder)
            for name, contents in STUBS.items():
                (temp / name).write_text(contents)
            source = temp / "sightings_harness.c"
            source.write_text(HARNESS)
            output = temp / "sightings_harness"
            include_paths = [temp, MAIN, STORAGE, HOTSPOTS, WIRELESS, LOCATION]
            subprocess.run(
                [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                 "-Wall", "-Wextra", "-Werror",
                 *[arg for inc in include_paths for arg in ("-I", str(inc))],
                 str(source), str(HOTSPOTS / "hotspot_sightings.c"),
                 str(STORAGE / "observation_journal.c"),
                 str(STORAGE / "media_journal.c"),
                 str(LOCATION / "geo.c"), "-lm", "-o", str(output)],
                check=True)
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
