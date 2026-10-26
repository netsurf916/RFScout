"""Host regression tests for the pure service metadata classifier."""

import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIRELESS = ROOT / "main/services/wireless"

HARNESS = r'''
#include "service_fingerprint.h"
#include <assert.h>
#include <string.h>

#define HEADER(result, name, value) service_fingerprint_http_header(&(result), (name), strlen(name), (value), strlen(value))
#define DNS(result, value) service_fingerprint_dns_sd(&(result), (value), strlen(value))

int main(void)
{
    rfscout_service_fingerprint_t result = {0};

    /* A restricted network, a generic web server and auth are not camera proof. */
    service_fingerprint_http_status(&result, 401);
    HEADER(result, "Server", "nginx");
    HEADER(result, "WWW-Authenticate", "Basic realm=\"admin\"");
    assert((result.evidence & RFSCOUT_SERVICE_AUTH_CHALLENGE) != 0);
    assert((result.evidence & RFSCOUT_SERVICE_CAMERA_HINT) == 0);
    assert((result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE) == 0);

    service_fingerprint_reset(&result);
    HEADER(result, "sErVeR", "Embedded Network Camera Server");
    assert(result.evidence & RFSCOUT_SERVICE_CAMERA_HINT);
    assert(!(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE));

    service_fingerprint_reset(&result);
    DNS(result, "_rtsp._tcp");
    assert(result.evidence & RFSCOUT_SERVICE_RTSP_ADVERTISED);
    assert(!(result.evidence & RFSCOUT_SERVICE_CAMERA_HINT));
    DNS(result, "_http._tcp");
    assert(result.evidence & RFSCOUT_SERVICE_HTTP);

    service_fingerprint_reset(&result);
    const char device[] = "NetworkVideoTransmitter";
    service_fingerprint_ws_device_type(&result, device, sizeof(device) - 1);
    assert(result.evidence == RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE);

    service_fingerprint_reset(&result);
    HEADER(result, "Content-Type", "image/jpeg");
    assert(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE);
    assert(!(result.evidence & RFSCOUT_SERVICE_CAMERA_HINT));

    /* A body/URL mention does not count as a recognized header. */
    service_fingerprint_reset(&result);
    HEADER(result, "Location", "http://example.test/onvif");
    assert(result.evidence == RFSCOUT_SERVICE_HTTP);

    /* Explicit-length non-NUL-terminated input must remain bounded. */
    service_fingerprint_reset(&result);
    const char header_name[] = {'S','e','r','v','e','r'};
    const char header_value[] = {'I','P',' ','C','a','m','e','r','a'};
    service_fingerprint_http_header(&result, header_name, sizeof(header_name),
                                    header_value, sizeof(header_value));
    assert(result.evidence & RFSCOUT_SERVICE_CAMERA_HINT);

    service_fingerprint_reset(&result);
    service_fingerprint_http_header(&result, header_name, 257, header_value, sizeof(header_value));
    assert(result.evidence == 0);
    service_fingerprint_http_header(NULL, header_name, sizeof(header_name), header_value, sizeof(header_value));
    service_fingerprint_dns_sd(&result, NULL, 4);
    service_fingerprint_ws_device_type(&result, NULL, 3);
    assert(result.evidence == 0);
    return 0;
}
'''


class ServiceFingerprintTests(unittest.TestCase):
    def test_metadata_classifier(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "fingerprint_test.c"
            exe = pathlib.Path(tmp) / "fingerprint_test"
            src.write_text(HARNESS)
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(WIRELESS), str(src),
                 str(WIRELESS / "service_fingerprint.c"), "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
