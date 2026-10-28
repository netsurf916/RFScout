"""Host regression tests for bounded captured HTTP HEAD response headers."""

import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIRELESS = ROOT / "main/services/wireless"

HARNESS = r'''
#include "probe_http_capture.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    rfscout_probe_http_capture_t headers;
    probe_http_capture_reset(&headers);
    assert(headers.length == 0 && !headers.truncated);

    probe_http_capture_header(&headers, "Server", "ONVIF/1.0");
    probe_http_capture_header(&headers, "WWW-Authenticate", "Basic realm=\"Camera\"");
    assert(strstr(headers.data, "Server: ONVIF/1.0\r\n") != NULL);
    assert(strstr(headers.data, "WWW-Authenticate: Basic realm=\"Camera\"\r\n") != NULL);
    assert(!headers.truncated);

    /* Remote header controls cannot forge new diagnostic lines. */
    probe_http_capture_reset(&headers);
    probe_http_capture_header(&headers, "X-Server", "value\r\nX-Forged: 1");
    assert(strstr(headers.data, "X-Server: value  X-Forged: 1\r\n") != NULL);
    assert(strstr(headers.data, "\r\nX-Forged:") == NULL);

    /* Single-field and total-buffer overflow report truncation. */
    probe_http_capture_reset(&headers);
    char enormous[RFSCOUT_PROBE_HTTP_FIELD_MAX + 40];
    memset(enormous, 'Q', sizeof(enormous) - 1);
    enormous[sizeof(enormous) - 1] = '\0';
    probe_http_capture_header(&headers, "X-Long", enormous);
    assert(headers.truncated);
    assert(headers.length <= RFSCOUT_PROBE_HTTP_CAPTURE_MAX);
    for (int i = 0; i < 8; ++i)
        probe_http_capture_header(&headers, "X-Next", enormous);
    assert(headers.truncated);
    assert(headers.length == RFSCOUT_PROBE_HTTP_CAPTURE_MAX);
    assert(headers.data[headers.length] == '\0');

    probe_http_capture_reset(&headers);
    assert(headers.length == 0 && headers.data[0] == '\0' && !headers.truncated);
    probe_http_capture_header(&headers, NULL, "ignored");
    assert(headers.length == 0);
    return 0;
}
'''


class ProbeHttpCaptureTests(unittest.TestCase):
    def test_bounded_http_headers(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / "capture.c"
            exe = pathlib.Path(folder) / "capture"
            source.write_text(HARNESS)
            built = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(WIRELESS), str(source),
                 str(WIRELESS / "probe_http_capture.c"),
                 "-o", str(exe)],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            subprocess.run([str(exe)], check=True)

    def test_sd_capture_is_outside_worker_and_bound_to_actual_probe(self):
        worker = (WIRELESS / "connectivity_probe.c").read_text()
        app = (ROOT / "main/apps/hotspots/app.c").read_text()
        self.assertIn("probe_http_capture_header(", worker)
        self.assertNotIn("media_write(", worker)
        self.assertIn('media_app_collection( "gateway_probes"', app)
        self.assertIn("RFSCOUT_MEDIA_OPEN_CREATE_NEW", app)
        self.assertIn("gateway_capture_pending = true;", app)
        self.assertIn("save_gateway_probe_result(", app)
        self.assertIn("finish_failed_network_attempt(", app)
        # LEAVE must archive cancelled HEAD metadata before app-session reset.
        leave = app.split("case RFSCOUT_APP_RUNTIME_LEAVE:", 1)[1].split(
            "case RFSCOUT_APP_RUNTIME_INPUT:", 1
        )[0]
        self.assertIn('save_gateway_probe_result( 0, ESP_ERR_INVALID_STATE, "app_exit" );', leave)
        self.assertLess(
            leave.index("save_gateway_probe_result("),
            leave.index("reset_session_state();"),
        )


if __name__ == "__main__":
    unittest.main()
