"""Test the production Hotspots connection teardown ordering with failure injection."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOTSPOTS = ROOT / "main/apps/hotspots/app.c"


def extract_release_function(source: str) -> str:
    match = re.search(
        r"static esp_err_t\s+release_check_services\s*\(\s*void\s*\)\s*\{",
        source,
    )
    if match is None:
        raise AssertionError("Missing shared Hotspots check-teardown helper")
    start = match.start()
    opening = source.index("{", match.start())
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError("Unterminated Hotspots teardown helper")


HARNESS_BEFORE = r'''
#include <assert.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_TIMEOUT (-2)

static esp_err_t probe_result;
static esp_err_t station_result;
static unsigned probe_calls;
static unsigned station_calls;

static esp_err_t connectivity_probe_stop(void)
{
    ++probe_calls;
    return probe_result;
}
static esp_err_t network_session_release(void)
{
    ++station_calls;
    return station_result;
}
'''

HARNESS_AFTER = r'''
int main(void)
{
    /* The TLS worker must be quiescent before any station teardown attempt. */
    probe_result = ESP_ERR_TIMEOUT;
    station_result = ESP_OK;
    assert(release_check_services() == ESP_ERR_TIMEOUT);
    assert(probe_calls == 1U && station_calls == 0U);

    /* Station release may fail; the helper propagates that error to the
       scheduler instead of claiming that teardown completed. */
    probe_result = ESP_OK;
    station_result = ESP_FAIL;
    assert(release_check_services() == ESP_FAIL);
    assert(probe_calls == 2U && station_calls == 1U);

    /* The next attempt retries both stops and succeeds only when confirmed. */
    station_result = ESP_OK;
    assert(release_check_services() == ESP_OK);
    assert(probe_calls == 3U && station_calls == 2U);

    return 0;
}
'''


class HotspotsTeardownTests(unittest.TestCase):
    def test_network_release_is_gated_by_probe_stop(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")

        code = (
            HARNESS_BEFORE
            + extract_release_function(HOTSPOTS.read_text())
            + HARNESS_AFTER
        )
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "teardown.c"
            output = pathlib.Path(folder) / "teardown"
            source.write_text(code)
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-o", str(output), str(source)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
