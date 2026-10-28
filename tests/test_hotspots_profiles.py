"""Host regression fixtures for Hotspots' saved-profile selection: which profile automatic checks
may use for a secured BSSID, from exact BSSIDs, rejections, auth modes, same-scan overlap and
GPS history evidence."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = ROOT / "main/apps/hotspots/hotspot_profiles.c"
HEADER_DIR = PROFILES.parent
GEO = ROOT / "main/services/location/geo.c"
WIRELESS = ROOT / "main/services/wireless"

HARNESS = r'''
#include "hotspot_profiles.h"
#include <assert.h>
#include <string.h>

#define WPA2 3
#define WPA3 6

/* BSSIDs in the fixtures: A is the BSSID being resolved; B is visible in the same scan; C is
   out of range with history; D has no history. */
enum { A = 1, B = 2, C = 3, D = 4 };

static void mac( uint8_t a_out[ 6 ], unsigned a_id )
{
    memset( a_out, 0, 6U );
    a_out[ 5 ] = (uint8_t)a_id;
}

static saved_network_profile_t profile( uint32_t a_id, const char *a_ssid, int32_t a_auth )
{
    saved_network_profile_t value = { 0 };
    value.profile_id = a_id;
    value.auth_mode = a_auth;
    value.has_password = true;
    strcpy( value.ssid, a_ssid );
    return value;
}

static void learn( saved_network_profile_t *a_profile, unsigned a_bssid, uint8_t a_state,
                   int32_t a_auth )
{
    saved_network_bssid_t *learned = &a_profile->bssids[ a_profile->bssid_count++ ];
    mac( learned->address, a_bssid );
    learned->state = a_state;
    learned->auth_mode = a_auth;
}

static hotspot_entry_t entry( unsigned a_bssid, const char *a_ssid, int32_t a_auth, bool a_visible )
{
    hotspot_entry_t value = { 0 };
    mac( value.bss.bssid, a_bssid );
    strcpy( value.bss.ssid, a_ssid );
    value.bss.auth_mode = a_auth;
    value.bss.security = RFSCOUT_WIFI_STATION_SECURITY_SECURED;
    value.visible = a_visible;
    return value;
}

/* History: C was characterized around (10, 10). */
static geo_bounds_t c_bounds;

static const geo_bounds_t *history( const uint8_t a_bssid[ 6 ] )
{
    uint8_t c[ 6 ];
    mac( c, C );
    return 0 == memcmp( a_bssid, c, 6U ) ? &c_bounds : NULL;
}

static hotspot_entry_t scan[ 4 ];
static hotspot_table_t table = { scan, 0, 4, 0, 0, 0, 0 };
static rfscout_gps_snapshot_t fix;

static hotspot_profiles_input_t input( const saved_network_profile_t *a_profiles, size_t a_count )
{
    hotspot_profiles_input_t value = { a_profiles, a_count, &table, history, &fix, 100U };
    return value;
}

static hotspot_credential_t resolve( const hotspot_profiles_input_t *a_input, uint32_t *a_id )
{
    *a_id = 0xFFFFFFFFU;
    return hotspot_profiles_resolve( a_input, &scan[ 0 ], a_id );
}

static void at( double a_latitude, double a_longitude, uint32_t a_age_ms )
{
    memset( &fix, 0, sizeof( fix ) );
    fix.available = true;
    fix.location_valid = true;
    fix.latitude_deg = a_latitude;
    fix.longitude_deg = a_longitude;
    fix.location_age_ms = a_age_ms;
}

int main( void )
{
    uint32_t id;
    geo_bounds_extend( &c_bounds, 10.0, 10.0 );
    /* Default: no GPS fix, so only exact BSSIDs and same-scan overlap count. */
    memset( &fix, 0, sizeof( fix ) );
    scan[ 0 ] = entry( A, "CAFE", WPA2, true );
    scan[ 1 ] = entry( B, "CAFE", WPA2, true );
    table.count = 2;

    /* No profiles, or none for this SSID: NONE. */
    hotspot_profiles_input_t none = input( NULL, 0 );
    assert( resolve( &none, &id ) == HOTSPOT_CREDENTIAL_NONE && id == 0 );
    saved_network_profile_t other[ 1 ] = { profile( 1, "LIBRARY", WPA2 ) };
    learn( &other[ 0 ], A, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    none = input( other, 1 );
    assert( resolve( &none, &id ) == HOTSPOT_CREDENTIAL_NONE && id == 0 );

    /* An empty credential or unassociated evidence must never promote a secured BSSID. */
    saved_network_profile_t no_password[ 1 ] = { profile( 21, "CAFE", WPA2 ) };
    learn( &no_password[ 0 ], A, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    no_password[ 0 ].has_password = false;
    none = input( no_password, 1 );
    assert( resolve( &none, &id ) == HOTSPOT_CREDENTIAL_NONE && id == 0 );
    no_password[ 0 ].has_password = true;
    no_password[ 0 ].bssids[ 0 ].state = 0U;
    assert( resolve( &none, &id ) != HOTSPOT_CREDENTIAL_KNOWN );

    /* Known: the exact associated BSSID with the same auth mode wins over earlier evidence. */
    saved_network_profile_t known[ 2 ] = { profile( 7, "CAFE", WPA2 ), profile( 9, "CAFE", WPA2 ) };
    learn( &known[ 0 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    learn( &known[ 1 ], A, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    hotspot_profiles_input_t pass = input( known, 2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_KNOWN && id == 9 );

    /* Legacy rejected BSSIDs never veto a compatible profile with positive evidence. */
    saved_network_profile_t refused[ 2 ] = { profile( 4, "CAFE", WPA2 ), profile( 5, "CAFE", WPA2 ) };
    learn( &refused[ 0 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    learn( &refused[ 0 ], A, SAVED_NETWORK_BSSID_REJECTED, WPA2 );
    learn( &refused[ 1 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    pass = input( refused, 2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_AMBIGUOUS && id == 0 );

    /* The profile with legacy rejected A is still inferred through associated B. */
    pass = input( refused, 1 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_INFERRED && id == 4 );

    /* A legacy rejected-only BSSID has no positive evidence: manual PICK, not DENY. */
    saved_network_profile_t rejected_only[ 1 ] = { profile( 34, "CAFE", WPA2 ) };
    learn( &rejected_only[ 0 ], A, SAVED_NETWORK_BSSID_REJECTED, WPA2 );
    pass = input( rejected_only, 1 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );

    /* Auth mismatch: a profile that learned another auth mode is ignored entirely. */
    saved_network_profile_t mismatch[ 2 ] = { profile( 6, "CAFE", WPA3 ), profile( 8, "CAFE", WPA2 ) };
    learn( &mismatch[ 0 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA3 );
    learn( &mismatch[ 0 ], A, SAVED_NETWORK_BSSID_REJECTED, WPA2 );
    pass = input( mismatch, 1 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_NONE && id == 0 );
    pass = input( mismatch, 2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );

    /* Unknown-auth profile is compatible; scan overlap through B makes it inferred. */
    saved_network_profile_t fresh[ 1 ] = { profile( 2, "CAFE", SAVED_NETWORK_AUTH_UNKNOWN ) };
    learn( &fresh[ 0 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    pass = input( fresh, 1 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_INFERRED && id == 2 );

    /* Overlap needs B visible, with the same SSID, and associated (not rejected) for the profile. */
    scan[ 1 ].visible = false;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    scan[ 1 ] = entry( B, "CAFE-5G", WPA2, true );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    scan[ 1 ] = entry( B, "CAFE", WPA2, true );
    fresh[ 0 ].bssids[ 0 ].state = SAVED_NETWORK_BSSID_REJECTED;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );

    /* Two profiles with evidence: AMBIGUOUS. */
    saved_network_profile_t both[ 2 ] = { profile( 10, "CAFE", WPA2 ), profile( 11, "CAFE", WPA2 ) };
    learn( &both[ 0 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    learn( &both[ 1 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    pass = input( both, 2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_AMBIGUOUS && id == 0 );

    /* No evidence (known BSSIDs out of range, no fix): UNMATCHED. */
    saved_network_profile_t away[ 2 ] = { profile( 12, "CAFE", WPA2 ), profile( 13, "CAFE", WPA2 ) };
    learn( &away[ 0 ], C, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    learn( &away[ 1 ], D, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    pass = input( away, 2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );

    /* GPS near C's history (inside, then ~77 m east): INFERRED via profile 12. */
    at( 10.0, 10.0, 0 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_INFERRED && id == 12 );
    at( 10.0, 10.0007, RFSCOUT_GPS_FIX_FRESH_MS );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_INFERRED && id == 12 );
    /* Beyond a smaller match distance, or far away: UNMATCHED. */
    pass.match_distance_m = 50U;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    pass.match_distance_m = 100U;
    at( 11.0, 10.0, 0 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    /* Stale fix, invalid fix, or no snapshot: UNMATCHED. */
    at( 10.0, 10.0, RFSCOUT_GPS_FIX_FRESH_MS + 1U );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    at( 10.0, 10.0, 0 );
    fix.location_valid = false;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    at( 10.0, 10.0, 0 );
    pass.gps = NULL;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    pass.gps = &fix;
    /* Match distance Off, or no history lookup: UNMATCHED. */
    pass.match_distance_m = 0U;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    pass.match_distance_m = 100U;
    pass.history_bounds = NULL;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    pass.history_bounds = history;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_INFERRED && id == 12 );
    /* A rejected BSSID's history is not evidence. */
    away[ 0 ].bssids[ 0 ].state = SAVED_NETWORK_BSSID_REJECTED;
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_UNMATCHED && id == 0 );
    away[ 0 ].bssids[ 0 ].state = SAVED_NETWORK_BSSID_ASSOCIATED;

    /* GPS evidence for one profile plus scan overlap for another: AMBIGUOUS. */
    learn( &away[ 1 ], B, SAVED_NETWORK_BSSID_ASSOCIATED, WPA2 );
    assert( resolve( &pass, &id ) == HOTSPOT_CREDENTIAL_AMBIGUOUS && id == 0 );
    return 0;
}
'''


def compile_and_run(harness, sources):
    compiler = shutil.which("cc")
    if compiler is None:
        raise unittest.SkipTest("C compiler unavailable")
    with tempfile.TemporaryDirectory() as folder:
        temp = pathlib.Path(folder)
        source = temp / "harness.c"
        binary = temp / "harness"
        source.write_text(harness)
        (temp / "esp_err.h").write_text("typedef int esp_err_t;\n")
        subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(temp), "-I", str(HEADER_DIR), "-I", str(GEO.parent),
             "-I", str(WIRELESS), str(source),
             *[str(path) for path in sources], "-lm", "-o", str(binary)], check=True
        )
        subprocess.run([str(binary)], check=True)


class ProfileSelectionTests(unittest.TestCase):
    def test_profile_selection(self):
        compile_and_run(HARNESS, [PROFILES, GEO])


if __name__ == "__main__":
    unittest.main()
