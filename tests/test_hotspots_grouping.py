"""Host regression fixtures for Hotspotter's evidence-sensitive BSSID partitioning."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GROUPING = ROOT / "main/apps/hotspots/characterization_grouping.c"
HEADER_DIR = GROUPING.parent
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


class GroupingTests(unittest.TestCase):
    def test_evidence_parity(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "grouping_test.c"
            binary = pathlib.Path(folder) / "grouping_test"
            source.write_text(HARNESS)
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(HEADER_DIR), "-I", str(GEO.parent), str(source), str(GROUPING), str(GEO),
                 "-lm", "-o", str(binary)], check=True
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
