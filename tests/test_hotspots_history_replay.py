"""Host regression fixtures for Hotspots history replay: malformed and torn lines, configuration
epochs, location backfill, deferred flush, and compaction round trips, using the production
hotspot_history.c and media_journal.c against the in-memory media store.

Needs a C compiler and the cJSON development package (Ubuntu: libcjson-dev); skips otherwise.
"""
import importlib.util
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOTSPOTS = ROOT / "main/apps/hotspots"
HISTORY = HOTSPOTS / "hotspot_history.c"
GEO = ROOT / "main/services/location/geo.c"
CJSON_DIRS = [pathlib.Path("/usr/include/cjson"), pathlib.Path("/usr/local/include/cjson")]

SPEC = importlib.util.spec_from_file_location(
    "media_journal_replay", pathlib.Path(__file__).with_name("test_media_journal_replay.py"))
JOURNAL_TEST = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(JOURNAL_TEST)

STUBS = dict(JOURNAL_TEST.STUBS)
STUBS["esp_err.h"] += "const char *esp_err_to_name( esp_err_t a_error );\n"
STUBS["app_session.h"] = """#pragma once
#include <stddef.h>
void *app_session_calloc( size_t a_count, size_t a_size );
void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size );
"""
STUBS["device_config.h"] = """#pragma once
#include <stdint.h>
typedef struct
{
    uint8_t gps_movement_m;
} device_config_t;
const device_config_t *device_config_get( void );
"""

HARNESS = JOURNAL_TEST.MEMORY_STORE + r'''
/* ------------------------------------------------------------------ platform stubs */

#include "app_session.h"
#include "device_config.h"
#include "hotspot_history.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

const char *esp_err_to_name( esp_err_t a_error )
{
    (void)a_error;
    return "error";
}

void *app_session_calloc( size_t a_count, size_t a_size )
{
    return calloc( a_count, a_size );
}

void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size )
{
    return realloc( a_pointer, a_count * a_size );
}

const device_config_t *device_config_get( void )
{
    static const device_config_t config = { .gps_movement_m = 10U };
    return &config;
}

esp_err_t media_app_collection( const char *a_collection, rfscout_media_collection_t *a_resolved )
{
    memset( a_resolved, 0, sizeof( *a_resolved ) );
    a_resolved->app = RFSCOUT_APP_HOTSPOTS;
    a_resolved->generation = 1U;
    snprintf( a_resolved->collection, sizeof( a_resolved->collection ), "%s", a_collection );
    return ESP_OK;
}

/* ------------------------------------------------------------------ helpers */

#define SEGMENT "hotspots-00000001.jsonl"

static int64_t now_us;
static char text[ STORE_FILE_BYTES ];
static size_t used;

static void line( const char *a_format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );

static void line( const char *a_format, ... )
{
    va_list arguments;
    va_start( arguments, a_format );
    const int written = vsnprintf( &text[ used ], sizeof( text ) - used, a_format, arguments );
    va_end( arguments );
    assert( written > 0 && (size_t)written < sizeof( text ) - used );
    used += (size_t)written;
}

/** One characterization line; a_extra is appended inside the object (",\"lat\":..."). */
static void characterization( unsigned a_id,
                              const char *a_ssid,
                              int a_auth,
                              const char *a_status,
                              unsigned a_gateway,
                              unsigned a_netmask,
                              unsigned a_dns1,
                              unsigned a_dns2,
                              const char *a_extra )
{
    line( "{\"type\":\"characterization\",\"bssid\":\"AABBCC0000%02X\",\"ssid\":\"%s\","
          "\"auth_mode\":%d,\"rssi\":-50,\"status\":\"%s\",\"gateway\":%u,\"netmask\":%u,"
          "\"dns\":[%u,%u]%s}\n",
          a_id,
          a_ssid,
          a_auth,
          a_status,
          a_gateway,
          a_netmask,
          a_dns1,
          a_dns2,
          a_extra );
}

static void begin_text( void )
{
    used = 0U;
    text[ 0 ] = '\0';
}

static void poll_until_ready( void )
{
    for( unsigned step = 0U; step < 1000U && !hotspot_history_ready(); ++step )
    {
        now_us += 100000;
        hotspot_history_poll( now_us );
    }
    assert( hotspot_history_ready() );
}

static void poll_for( int64_t a_duration_us )
{
    const int64_t end = now_us + a_duration_us;
    while( now_us < end )
    {
        now_us += 100000;
        hotspot_history_poll( now_us );
    }
}

/** Open as on app entry after a power cycle. */
static void reopen( void )
{
    hotspot_history_close();
    assert( 0U == open_handles() );
    store_reboot();
    assert( ESP_OK == hotspot_history_open() );
    poll_until_ready();
}

static const hotspot_history_entry_t *entry_for( unsigned a_id )
{
    for( size_t index = 0U; index < hotspot_history_count(); ++index )
    {
        const hotspot_history_entry_t *entry = hotspot_history_at( index );
        const uint8_t bssid[ 6 ] = { 0xAA, 0xBB, 0xCC, 0x00, 0x00, (uint8_t)a_id };
        if( 0 == memcmp( entry->bssid, bssid, 6U ) )
        {
            return entry;
        }
    }
    return NULL;
}

static rfscout_wifi_station_bss_t bss( unsigned a_id, const char *a_ssid, int32_t a_auth )
{
    rfscout_wifi_station_bss_t value;
    memset( &value, 0, sizeof( value ) );
    const uint8_t bssid[ 6 ] = { 0xAA, 0xBB, 0xCC, 0x00, 0x00, (uint8_t)a_id };
    memcpy( value.bssid, bssid, 6U );
    snprintf( value.ssid, sizeof( value.ssid ), "%s", a_ssid );
    value.auth_mode = a_auth;
    value.rssi = -45;
    return value;
}

static rfscout_gps_snapshot_t fix( double a_latitude, double a_longitude )
{
    rfscout_gps_snapshot_t gps;
    memset( &gps, 0, sizeof( gps ) );
    gps.available = true;
    gps.location_valid = true;
    gps.latitude_deg = a_latitude;
    gps.longitude_deg = a_longitude;
    return gps;
}

static bool near( double a_left, double a_right )
{
    return fabs( a_left - a_right ) < 1e-6;
}

/* ------------------------------------------------------------------ scenarios */

/** Intact records survive; malformed lines are counted; the torn tail is ignored. */
static void replay_and_skipped( void )
{
    store_reset();
    begin_text();
    characterization( 1U,
                      "CAFE",
                      3,
                      "green",
                      1U,
                      255U,
                      1U,
                      2U,
                      ",\"utc\":1700000100,\"gateway_mac\":\"020000000001\","
                      "\"public_ip\":\"203.0.113.9\",\"colo\":\"SEA\",\"lat\":47.0,\"lon\":-122.0" );
    line( "not json\n" );
    line( "{\"type\":\"characterization\",\"ssid\":\"CAFE\",\"auth_mode\":3}\n" );
    line( "{\"type\":\"rumor\",\"bssid\":\"AABBCC000001\",\"ssid\":\"CAFE\",\"auth_mode\":3}\n" );
    line( "{\"type\":\"location\",\"bssid\":\"AABBCC000001\",\"ssid\":\"CAFE\",\"auth_mode\":3,"
          "\"lat\":47.001,\"lon\":-122.0}\n" );
    /* A location for a BSSID without history is a valid record that changes nothing. */
    line( "{\"type\":\"location\",\"bssid\":\"AABBCC000009\",\"ssid\":\"NOPE\",\"auth_mode\":3,"
          "\"lat\":1.0,\"lon\":1.0}\n" );
    characterization( 2U, "LIB", 0, "yellow", 5U, 255U, 5U, 0U, "" );
    line( "{\"type\":\"characterization\",\"bssid\":\"AABBCC0000" );
    put( SEGMENT, text );

    assert( ESP_OK == hotspot_history_open() );
    assert( !hotspot_history_ready() );
    poll_until_ready();
    assert( 100U == hotspot_history_progress() );
    assert( !hotspot_history_degraded() );
    assert( 3U == hotspot_history_skipped() );
    assert( 2U == hotspot_history_count() );
    assert( holds( SEGMENT, text ) );

    const hotspot_history_entry_t *cafe = entry_for( 1U );
    assert( NULL != cafe && 1U == cafe->successes && HOTSPOT_STATUS_GREEN == cafe->status );
    assert( 1700000100LL == cafe->validated_utc && -50 == cafe->rssi );
    assert( 1U == cafe->network.gateway && 255U == cafe->network.netmask );
    assert( 1U == cafe->network.dns_primary && 2U == cafe->network.dns_backup );
    assert( cafe->network.gateway_mac_valid && 0x01 == cafe->network.gateway_mac[ 5 ] );
    assert( 0 == strcmp( cafe->public_ip, "203.0.113.9" ) && 0 == strcmp( cafe->colo, "SEA" ) );
    assert( cafe->bounds.valid && 2U == cafe->bounds.samples );
    assert( near( cafe->bounds.min_latitude, 47.0 ) && near( cafe->bounds.max_latitude, 47.001 ) );

    const hotspot_history_entry_t *library = entry_for( 2U );
    assert( NULL != library && HOTSPOT_STATUS_YELLOW == library->status );
    assert( !library->bounds.valid && 0 == library->validated_utc );
    assert( !library->network.gateway_mac_valid && '\0' == library->public_ip[ 0 ] );
    assert( NULL == entry_for( 9U ) );

    rfscout_wifi_station_bss_t lookup = bss( 1U, "CAFE", 3 );
    assert( cafe == hotspot_history_find( &lookup ) );
    lookup.auth_mode = 4;
    assert( NULL == hotspot_history_find( &lookup ) );
    lookup = bss( 1U, "OTHER", 3 );
    assert( NULL == hotspot_history_find( &lookup ) );

    hotspot_history_close();
    assert( 0U == open_handles() );
}

/** A change of SSID, auth, gateway, netmask or either DNS server restarts the BSSID's epoch. */
static void epochs( void )
{
    store_reset();
    begin_text();
    const char *located = ",\"gateway_mac\":\"020000000001\",\"public_ip\":\"198.51.100.1\","
                          "\"colo\":\"SEA\",\"lat\":10.0,\"lon\":10.0";
    /* 0x10: same network twice; enrichment is kept when the later record lacks it. */
    characterization( 0x10U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x10U, "CAFE", 3, "yellow", 1U, 255U, 1U, 2U, ",\"lat\":10.001,\"lon\":10.0" );
    /* 0x11-0x16: the second record changes exactly one epoch field. */
    characterization( 0x11U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x11U, "CAFE", 3, "green", 9U, 255U, 1U, 2U, "" );
    characterization( 0x12U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x12U, "CAFE", 3, "green", 1U, 254U, 1U, 2U, "" );
    characterization( 0x13U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x13U, "CAFE", 3, "green", 1U, 255U, 3U, 2U, "" );
    characterization( 0x14U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x14U, "CAFE", 3, "green", 1U, 255U, 1U, 4U, "" );
    characterization( 0x15U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x15U, "CAFE2", 3, "green", 1U, 255U, 1U, 2U, "" );
    characterization( 0x16U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, located );
    characterization( 0x16U, "CAFE", 4, "green", 1U, 255U, 1U, 2U, "" );
    /* A location from the previous epoch's SSID no longer applies. */
    line( "{\"type\":\"location\",\"bssid\":\"AABBCC000015\",\"ssid\":\"CAFE\",\"auth_mode\":3,"
          "\"lat\":5.0,\"lon\":5.0}\n" );
    put( SEGMENT, text );

    assert( ESP_OK == hotspot_history_open() );
    poll_until_ready();
    assert( 0U == hotspot_history_skipped() );
    assert( 7U == hotspot_history_count() );

    const hotspot_history_entry_t *same = entry_for( 0x10U );
    assert( 2U == same->successes && HOTSPOT_STATUS_YELLOW == same->status );
    assert( same->network.gateway_mac_valid && 0 == strcmp( same->public_ip, "198.51.100.1" ) );
    assert( 2U == same->bounds.samples );

    for( unsigned id = 0x11U; id <= 0x16U; ++id )
    {
        const hotspot_history_entry_t *entry = entry_for( id );
        assert( NULL != entry && 1U == entry->successes );
        assert( !entry->bounds.valid );
        assert( !entry->network.gateway_mac_valid && '\0' == entry->public_ip[ 0 ] );
    }
    assert( 9U == entry_for( 0x11U )->network.gateway );
    assert( 254U == entry_for( 0x12U )->network.netmask );
    assert( 3U == entry_for( 0x13U )->network.dns_primary );
    assert( 4U == entry_for( 0x14U )->network.dns_backup );
    assert( 0 == strcmp( entry_for( 0x15U )->ssid, "CAFE2" ) );
    assert( 4 == entry_for( 0x16U )->auth_mode );
    hotspot_history_close();
}

/** Results without a fix gain a location later; a failed flush is retried without duplicates. */
static void backfill_and_deferred_flush( void )
{
    store_reset();
    assert( ESP_OK == hotspot_history_open() );
    poll_until_ready();
    assert( 0U == hotspot_history_count() && !hotspot_history_degraded() );

    const rfscout_wifi_station_bss_t cafe = bss( 0x30U, "CAFE", 0 );
    hotspot_result_t result;
    memset( &result, 0, sizeof( result ) );
    result.outcome = HOTSPOT_STATUS_GREEN;
    result.start_rssi = -45;
    result.gateway = 7U;
    result.netmask = 255U;
    const rfscout_gps_snapshot_t no_fix = { 0 };
    hotspot_history_record_result( &cafe, &result, &no_fix );
    const hotspot_history_entry_t *entry = entry_for( 0x30U );
    assert( NULL != entry && 1U == entry->successes && !entry->bounds.valid );

    /* Red results never enter history. */
    hotspot_result_t failed = result;
    failed.outcome = HOTSPOT_STATUS_RED;
    const rfscout_wifi_station_bss_t other = bss( 0x31U, "CAFE", 0 );
    hotspot_history_record_result( &other, &failed, &no_fix );
    assert( 1U == hotspot_history_count() );

    rfscout_gps_snapshot_t gps = fix( 40.0, -100.0 );
    hotspot_history_observe( &cafe, &gps );
    assert( entry->bounds.valid && 1U == entry->bounds.samples );
    hotspot_history_observe( &cafe, &gps ); /* not moved: no new sample */
    assert( 1U == entry->bounds.samples );
    gps = fix( 40.0002, -100.0 ); /* about 22 m north, beyond the 10 m trigger */
    hotspot_history_observe( &cafe, &gps );
    assert( 2U == entry->bounds.samples );
    gps.location_age_ms = RFSCOUT_GPS_FIX_FRESH_MS + 1U;
    gps.latitude_deg = 41.0;
    hotspot_history_observe( &cafe, &gps ); /* stale fix */
    assert( 2U == entry->bounds.samples );

    /* The batch write fails and rolls back cleanly: records stay pending and SD! shows. */
    fail_write_at = write_calls + 1U;
    torn_bytes = 0U;
    poll_for( 500000 ); /* one flush attempt; the next is a second away */
    assert( hotspot_history_degraded() );
    clear_faults();
    poll_for( 1100000 );
    assert( !hotspot_history_degraded() );

    reopen();
    assert( 1U == hotspot_history_count() );
    entry = entry_for( 0x30U );
    assert( 1U == entry->successes && 7U == entry->network.gateway );
    assert( 2U == entry->bounds.samples );
    assert( near( entry->bounds.max_latitude, 40.0002 ) );
    hotspot_history_close();
}

/** Compaction keeps every BSSID's details and applies records that arrived meanwhile. */
static void compaction_round_trip( void )
{
    store_reset();
    begin_text();
    characterization( 0x40U,
                      "CAFE",
                      3,
                      "green",
                      1U,
                      255U,
                      1U,
                      2U,
                      ",\"utc\":1700000300,\"gateway_mac\":\"020000000002\","
                      "\"public_ip\":\"203.0.113.7\",\"colo\":\"PDX\",\"lat\":45.0,\"lon\":-122.5" );
    characterization( 0x40U, "CAFE", 3, "green", 1U, 255U, 1U, 2U, ",\"lat\":45.002,\"lon\":-122.5" );
    line( "{\"type\":\"location\",\"bssid\":\"AABBCC000040\",\"ssid\":\"CAFE\",\"auth_mode\":3,"
          "\"lat\":45.001,\"lon\":-122.501}\n" );
    characterization( 0x41U, "LIB", 0, "yellow", 5U, 255U, 5U, 0U, "" );
    put( SEGMENT, text );

    assert( ESP_OK == hotspot_history_open() );
    poll_until_ready();
    const hotspot_history_entry_t before = *entry_for( 0x40U );
    assert( 2U == before.successes && 3U == before.bounds.samples );

    hotspot_history_request_compaction();
    assert( hotspot_history_compacting() );
    poll_for( 100000 ); /* first ready poll starts compaction */
    assert( hotspot_history_compacting() );

    const rfscout_wifi_station_bss_t fresh = bss( 0x42U, "NEW", 0 );
    hotspot_result_t result;
    memset( &result, 0, sizeof( result ) );
    result.outcome = HOTSPOT_STATUS_GREEN;
    result.gateway = 3U;
    result.netmask = 255U;
    const rfscout_gps_snapshot_t no_fix = { 0 };
    hotspot_history_record_result( &fresh, &result, &no_fix );
    assert( 2U == hotspot_history_count() ); /* held until compaction finishes */

    for( unsigned step = 0U; step < 100U && hotspot_history_compacting(); ++step )
    {
        poll_for( 100000 );
    }
    assert( !hotspot_history_compacting() && !hotspot_history_degraded() );
    assert( 3U == hotspot_history_count() );
    poll_for( 1100000 ); /* flush the held record after the base */

    hotspot_history_close();
    assert( exists( "hotspots-00000002c.jsonl" ) );
    assert( !exists( SEGMENT ) );
    assert( exists( "hotspots-00000003.jsonl" ) );

    reopen();
    assert( 0U == hotspot_history_skipped() );
    assert( 3U == hotspot_history_count() );
    const hotspot_history_entry_t *after = entry_for( 0x40U );
    assert( before.successes == after->successes && before.status == after->status );
    assert( before.validated_utc == after->validated_utc && before.rssi == after->rssi );
    assert( before.network.gateway == after->network.gateway );
    assert( before.network.dns_backup == after->network.dns_backup );
    assert( after->network.gateway_mac_valid && 0x02 == after->network.gateway_mac[ 5 ] );
    assert( 0 == strcmp( after->public_ip, "203.0.113.7" ) && 0 == strcmp( after->colo, "PDX" ) );
    assert( after->bounds.valid && before.bounds.samples == after->bounds.samples );
    assert( near( before.bounds.min_latitude, after->bounds.min_latitude ) );
    assert( near( before.bounds.max_latitude, after->bounds.max_latitude ) );
    assert( near( before.bounds.min_longitude, after->bounds.min_longitude ) );
    assert( near( before.bounds.max_longitude, after->bounds.max_longitude ) );
    assert( HOTSPOT_STATUS_YELLOW == entry_for( 0x41U )->status );
    assert( 3U == entry_for( 0x42U )->network.gateway );
    hotspot_history_close();
}

int main( int argc, char **argv )
{
    static const struct
    {
        const char *name;
        void ( *run )( void );
    } scenarios[] = {
        { "replay_and_skipped", replay_and_skipped },
        { "epochs", epochs },
        { "backfill_and_deferred_flush", backfill_and_deferred_flush },
        { "compaction_round_trip", compaction_round_trip },
    };
    if( 2 != argc )
    {
        return 2;
    }
    for( size_t index = 0U; index < sizeof( scenarios ) / sizeof( scenarios[ 0 ] ); ++index )
    {
        if( 0 == strcmp( argv[ 1 ], scenarios[ index ].name ) )
        {
            scenarios[ index ].run();
            return 0;
        }
    }
    return 2;
}
'''


def cjson_include():
    return next((path for path in CJSON_DIRS if (path / "cJSON.h").is_file()), None)


class HotspotsHistoryReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("C compiler unavailable")
        cjson = cjson_include()
        if cjson is None:
            raise unittest.SkipTest("cJSON development headers unavailable (libcjson-dev)")
        cls.folder = tempfile.TemporaryDirectory()
        temp = pathlib.Path(cls.folder.name)
        for name, text in STUBS.items():
            (temp / name).write_text(text)
        source = temp / "history_harness.c"
        source.write_text(HARNESS)
        cls.binary = temp / "history_harness"
        includes = [temp, JOURNAL_TEST.STORAGE, HOTSPOTS, GEO.parent,
                    ROOT / "main/services/wireless"]
        try:
            # strnlen() is POSIX, not ISO C11.
            subprocess.run(
                [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 *[argument for path in includes for argument in ("-I", str(path))],
                 "-isystem", str(cjson), str(source), str(JOURNAL_TEST.JOURNAL), str(HISTORY),
                 str(GEO), "-lcjson", "-lm", "-o", str(cls.binary)], check=True
            )
        except BaseException:
            cls.folder.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_scenario(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_replay_counts_malformed_and_ignores_torn_lines(self):
        self.run_scenario("replay_and_skipped")

    def test_configuration_changes_start_new_epochs(self):
        self.run_scenario("epochs")

    def test_location_backfill_and_deferred_flush(self):
        self.run_scenario("backfill_and_deferred_flush")

    def test_compaction_round_trip(self):
        self.run_scenario("compaction_round_trip")


if __name__ == "__main__":
    unittest.main()
