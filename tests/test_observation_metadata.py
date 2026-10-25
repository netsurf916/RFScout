"""Host regression for common observation timestamp and GPS enrichment."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
LOCATION = MAIN / "services/location"
WIRELESS = MAIN / "services/wireless"

HARNESS = r'''
#include "observation_metadata.h"
#include "device_time.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static int64_t now_us;
static bool valid_time;

int64_t esp_timer_get_time( void )
{
    return now_us;
}

bool device_time_now( time_t *epoch )
{
    if( !valid_time )
    {
        return false;
    }
    if( epoch != NULL )
    {
        *epoch = 1700000500;
    }
    return true;
}

int main( void )
{
    rfscout_observation_metadata_t metadata = { 0 };
    rfscout_gps_snapshot_t gps = { .location_valid = true,
                                  .location_age_ms = 100,
                                  .latitude_deg = 39.1,
                                  .longitude_deg = -94.5 };

    now_us = 10000000LL; /* 10 seconds uptime */
    valid_time = true;
    observation_metadata_enrich( 9900U, &gps, &metadata );
    assert( metadata.seen_ms == 9900U );
    assert( metadata.utc_epoch == 1700000500 );
    assert( metadata.location_valid && metadata.latitude == 39.1 );

    observation_metadata_enrich( 8000U, &gps, &metadata );
    assert( metadata.utc_epoch == 1700000498 );
    assert( !metadata.location_valid ); /* Old queued observation must not be geotagged. */

    valid_time = false;
    gps.location_valid = false;
    observation_metadata_enrich( 9900U, &gps, &metadata );
    assert( metadata.utc_epoch == 0 && !metadata.location_valid );

    valid_time = true;
    observation_metadata_enrich( 9900U, NULL, &metadata );
    assert( metadata.utc_epoch == 1700000500 && !metadata.location_valid );
    return 0;
}
'''


class ObservationMetadataTests(unittest.TestCase):
    def test_shared_enrichment(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            temp = pathlib.Path(folder)
            (temp / "esp_err.h").write_text("typedef int esp_err_t;\n")
            (temp / "esp_timer.h").write_text(
                "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time( void );\n")
            source = temp / "enrichment_harness.c"
            source.write_text(HARNESS)
            output = temp / "enrichment_harness"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(temp), "-I", str(MAIN),
                 "-I", str(LOCATION), "-I", str(WIRELESS),
                 str(source), str(WIRELESS / "observation_metadata.c"),
                 "-o", str(output)],
                check=True)
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
