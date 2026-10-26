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

    /* The owner's interoperability policy accepts vendor QName casing errors. */
    service_fingerprint_reset(&result);
    const char lower_device[] = "networkvideotransmitter";
    service_fingerprint_ws_device_type(&result, lower_device, sizeof(lower_device) - 1);
    assert(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE);
    service_fingerprint_reset(&result);
    const char upper_device[] = "NetworkVideoTRANSMITTER";
    service_fingerprint_ws_device_type(&result, upper_device, sizeof(upper_device) - 1);
    assert(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE);

    /* Case tolerance does not permit near matches or longer local names. */
    service_fingerprint_reset(&result);
    const char suffix_device[] = "NetworkVideoTransmitterExtra";
    service_fingerprint_ws_device_type(&result, suffix_device, sizeof(suffix_device) - 1);
    assert(!(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE));
    const char wrong_device[] = "NetworkVideoReceiver";
    service_fingerprint_ws_device_type(&result, wrong_device, sizeof(wrong_device) - 1);
    assert(!(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE));
    const char prefix_device[] = "NetworkVideoTransmitte";
    service_fingerprint_ws_device_type(&result, prefix_device, sizeof(prefix_device) - 1);
    assert(!(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE));

    /* Same length, one altered non-case character: never classify near matches. */
    const char same_length_typo[] = "NetworkVideoTransmittar";
    assert(sizeof(same_length_typo) == sizeof(device));
    service_fingerprint_ws_device_type(&result, same_length_typo, sizeof(same_length_typo) - 1);
    assert(!(result.evidence & RFSCOUT_SERVICE_ONVIF_DEVICE_TYPE));

    service_fingerprint_reset(&result);
    HEADER(result, "Content-Type", "image/jpeg");
    assert(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE);
    assert(!(result.evidence & RFSCOUT_SERVICE_CAMERA_HINT));

    /* A MIME parameter may mention an image even if the response is plain text. */
    service_fingerprint_reset(&result);
    HEADER(result, "Content-Type", "text/plain; filename=image/jpeg");
    assert(!(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE));
    HEADER(result, "Content-Type", "application/json; note=multipart/x-mixed-replace");
    assert(!(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE));
    HEADER(result, "Content-Type", "application/image/jpeg");
    assert(!(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE));
    HEADER(result, "Content-Type", "image/jpegx");
    assert(!(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE));

    /* Media-type matching is ASCII case-insensitive with optional whitespace. */
    service_fingerprint_reset(&result);
    HEADER(result, "cOnTeNt-TyPe", "  IMAGE/JPEG ; charset=utf-8");
    assert(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE);
    service_fingerprint_reset(&result);
    HEADER(result, "Content-Type", "\tMULTIPART/X-MIXED-REPLACE; boundary=frame");
    assert(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE);

    /* The supplied value need not be NUL-terminated. */
    service_fingerprint_reset(&result);
    const char bounded_type[] = {'i','m','a','g','e','/','j','p','e','g',';','x'};
    service_fingerprint_http_header(&result, "Content-Type", 12U,
                                    bounded_type, sizeof(bounded_type));
    assert(result.evidence & RFSCOUT_SERVICE_MEDIA_RESPONSE);

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
