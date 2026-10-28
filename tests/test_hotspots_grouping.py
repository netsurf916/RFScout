"""Host regression fixtures for Hotspotter's evidence-sensitive BSSID partitioning and the
Hotspots network grouping model built on it."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GROUPING = ROOT / "main/apps/hotspots/characterization_grouping.c"
HEADER_DIR = GROUPING.parent
GROUPS = HEADER_DIR / "hotspot_groups.c"
GEO = ROOT / "main/services/location/geo.c"

HARNESS = r'''
#include "characterization_grouping.h"
#include <assert.h>
#include <string.h>

static characterization_group_member_t member(
    unsigned gateway, unsigned netmask, unsigned mac, double lat, int located)
{
    characterization_group_member_t m = { 0 };
    m.evidence.subnet_valid = gateway != 0;
    m.evidence.gateway = gateway;
    m.evidence.netmask = netmask;
    m.evidence.gateway_mac_valid = mac != 0;
    m.evidence.gateway_mac[ 5 ] = (unsigned char)mac;
    m.location_valid = located != 0;
    m.latitude_deg = lat;
    return m;
}

static void group( characterization_group_member_t *m, size_t n, size_t expected )
{
    characterization_group_cluster_t scratch[ 12 ] = { 0 };
    assert( characterization_grouping_partition( m, n, scratch ) == expected );
}

int main( void )
{
    characterization_group_member_t same[ 3 ] = {
        member( 1, 255, 1, 0.0, 0 ),
        member( 1, 255, 1, 0.0, 0 ),
        member( 0, 0, 0, 0.0, 0 )
    };
    group( same, 3, 1 );
    assert( same[ 0 ].cluster == same[ 2 ].cluster );

    characterization_group_member_t conflict[ 3 ] = {
        member( 1, 255, 1, 0.0, 0 ),
        member( 1, 255, 2, 0.0, 0 ),
        member( 2, 255, 1, 0.0, 0 )
    };
    group( conflict, 3, 3 );
    assert( conflict[ 0 ].cluster != conflict[ 1 ].cluster );
    assert( conflict[ 0 ].cluster != conflict[ 2 ].cluster );

    characterization_group_member_t partial[ 3 ] = {
        member( 1, 255, 1, 0.0, 1 ),
        member( 1, 255, 2, 10.0, 1 ),
        member( 1, 255, 0, 9.0, 1 )
    };
    group( partial, 3, 2 );
    assert( partial[ 1 ].cluster == partial[ 2 ].cluster );

    characterization_group_member_t unknown[ 4 ] = {
        member( 1, 255, 1, 0.0, 1 ),
        member( 2, 255, 2, 10.0, 1 ),
        member( 0, 0, 0, 10.1, 1 ),
        member( 0, 0, 0, 0.1, 1 )
    };
    group( unknown, 4, 2 );
    assert( unknown[ 1 ].cluster == unknown[ 2 ].cluster );
    assert( unknown[ 0 ].cluster == unknown[ 3 ].cluster );

    characterization_group_member_t absent_gps[ 4 ] = {
        member( 1, 255, 1, 0.0, 0 ),
        member( 2, 255, 2, 0.0, 0 ),
        member( 2, 255, 2, 0.0, 0 ),
        member( 0, 0, 0, 0.0, 0 )
    };
    group( absent_gps, 4, 2 );
    assert( absent_gps[ 1 ].cluster == absent_gps[ 3 ].cluster );
    assert( absent_gps[ 1 ].cluster == 0 );

    characterization_group_member_t mac_only[ 3 ] = {
        member( 0, 0, 1, 0.0, 0 ),
        member( 0, 0, 2, 0.0, 0 ),
        member( 0, 0, 1, 0.0, 0 )
    };
    group( mac_only, 3, 2 );
    assert( mac_only[ 0 ].cluster == mac_only[ 2 ].cluster );
    assert( mac_only[ 0 ].cluster != mac_only[ 1 ].cluster );

    characterization_group_member_t mac_mixed[ 3 ] = {
        member( 1, 255, 1, 0.0, 0 ),
        member( 0, 0, 1, 0.0, 0 ),
        member( 0, 0, 2, 0.0, 0 )
    };
    group( mac_mixed, 3, 2 );
    assert( mac_mixed[ 0 ].cluster == mac_mixed[ 1 ].cluster );
    assert( mac_mixed[ 0 ].cluster != mac_mixed[ 2 ].cluster );

    characterization_group_member_t no_evidence[ 2 ] = {
        member( 0, 0, 0, 0.0, 1 ),
        member( 0, 0, 0, 50.0, 1 )
    };
    group( no_evidence, 2, 1 );

    return 0;
}
'''

MODEL_HARNESS = r'''
#include "geo.h"
#include "hotspot_groups.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static hotspot_group_input_t input( const char *ssid, int auth, unsigned gateway, unsigned mac )
{
    hotspot_group_input_t value = { 0 };
    value.ssid = ssid;
    value.auth_mode = auth;
    value.member.evidence.subnet_valid = gateway != 0;
    value.member.evidence.gateway = gateway;
    value.member.evidence.netmask = gateway != 0 ? 255U : 0U;
    value.member.evidence.gateway_mac_valid = mac != 0;
    value.member.evidence.gateway_mac[ 5 ] = (unsigned char)mac;
    return value;
}

int main( void )
{
    /* 0,1: same-SSID conflicting gateways; 2,5: no evidence; 3: open; 4: LIB. */
    hotspot_group_input_t inputs[ 6 ] = {
        input( "CAFE", 3, 1, 1 ), input( "CAFE", 3, 2, 2 ), input( "CAFE", 3, 0, 0 ),
        input( "CAFE", 0, 0, 0 ), input( "LIB", 3, 0, 0 ),  input( "CAFE", 3, 0, 0 ),
    };
    /* Preference: LIB first, then 1, 5, 3, 2, 0. */
    const size_t preference[ 6 ] = { 4, 1, 5, 3, 2, 0 };
    size_t order[ 6 ];
    hotspot_group_t groups[ 6 ];
    size_t sorted[ 6 ];
    size_t rank[ 6 ];
    characterization_group_member_t members[ 6 ];
    characterization_group_cluster_t clusters[ 6 ];
    const hotspot_group_scratch_t scratch = { sorted, rank, members, clusters };

    const size_t count = hotspot_groups_build( inputs, 6, preference, order, groups, &scratch );
    /* No credential-only split: LIB, two characterized CAFE/WPA2 networks and CAFE/open. */
    assert( count == 4 );
    assert( groups[ 0 ].count == 1 && order[ groups[ 0 ].first ] == 4 );
    assert( groups[ 0 ].networks == 1 );
    assert( groups[ 1 ].networks == 2 && groups[ 1 ].count == 3 );
    assert( order[ groups[ 1 ].first ] == 1 );
    /* Unknown members 5 and 2 stay with an existing characterized network. */
    assert( order[ groups[ 1 ].first + 1 ] == 5 );
    assert( order[ groups[ 1 ].first + 2 ] == 2 );
    assert( groups[ 2 ].count == 1 && order[ groups[ 2 ].first ] == 3 );
    assert( groups[ 2 ].networks == 1 );
    assert( groups[ 3 ].count == 1 && order[ groups[ 3 ].first ] == 0 );
    assert( groups[ 3 ].networks == 2 && groups[ 1 ].network != groups[ 3 ].network );
    assert( hotspot_groups_build( inputs, 0, preference, order, groups, &scratch ) == 0 );

    /* Shared geo helpers used by History distance and direction. */
    assert( fabs( geo_bearing_deg( 0.0, 0.0, 1.0, 0.0 ) ) < 0.001 );
    assert( fabs( geo_bearing_deg( 0.0, 0.0, 0.0, 1.0 ) - 90.0 ) < 0.001 );
    assert( strcmp( geo_compass_label( 359.0 ), "N" ) == 0 );
    assert( strcmp( geo_compass_label( 225.0 ), "SW" ) == 0 );
    assert( strcmp( geo_compass_label( -45.0 ), "NW" ) == 0 );
    char text[ 16 ];
    geo_format_distance( 850.4, text, sizeof( text ) );
    assert( strcmp( text, "850m" ) == 0 );
    geo_format_distance( 1234.0, text, sizeof( text ) );
    assert( strcmp( text, "1.2km" ) == 0 );
    geo_format_distance( 12345.0, text, sizeof( text ) );
    assert( strcmp( text, "12km" ) == 0 );
    return 0;
}
'''


def compile_and_run(harness, sources):
    compiler = shutil.which("cc")
    if compiler is None:
        raise unittest.SkipTest("C compiler unavailable")
    with tempfile.TemporaryDirectory() as folder:
        source = pathlib.Path(folder) / "harness.c"
        binary = pathlib.Path(folder) / "harness"
        source.write_text(harness)
        subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(HEADER_DIR), "-I", str(GEO.parent), str(source),
             *[str(path) for path in sources], "-lm", "-o", str(binary)], check=True
        )
        subprocess.run([str(binary)], check=True)


class GroupingTests(unittest.TestCase):
    def test_evidence_parity(self):
        compile_and_run(HARNESS, [GROUPING, GEO])

    def test_network_model_and_geo(self):
        compile_and_run(MODEL_HARNESS, [GROUPS, GROUPING, GEO])


if __name__ == "__main__":
    unittest.main()
