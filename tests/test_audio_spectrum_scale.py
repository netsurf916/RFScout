"""Exercise the production FFT scaling with quiet, silent, and wide-span inputs."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AudioSpectrumScaleTests(unittest.TestCase):
    def test_automatic_fit_has_no_fixed_span_clamp(self):
        source = (ROOT / "main/services/audio/recorder.c").read_text()
        header = (ROOT / "main/services/audio/recorder.h").read_text()
        defines = "\n".join(re.findall(r"^#define RECORDER_MONITOR_.*$", source, re.M))
        bins = re.search(r"^#define RFSCOUT_RECORDER_MONITOR_BINS.*$", header, re.M).group()
        start = source.index("static void compute_monitor_fft(")
        end = source.index("static void monitor_process_samples(", start)
        declarations = "\n".join(re.findall(
            r"^static (?:volatile )?(?:uint8_t|bool|float) monitor_(?:spectrum|range_initialized|floor_db|ceiling_db|range_db)[^;]*;",
            source, re.M,
        ))
        harness = r'''
int main(void) {
    float samples[RECORDER_MONITOR_FFT_SIZE] = {0};
    compute_monitor_fft(samples);
    assert(isfinite(monitor_range_db) && monitor_range_db > 0.0f);
    for (size_t i = 0; i < RFSCOUT_RECORDER_MONITOR_BINS; ++i)
        assert(monitor_spectrum[i] == 0);
    monitor_range_initialized = false;
    for (size_t i = 0; i < RECORDER_MONITOR_FFT_SIZE; ++i)
        samples[i] = 0.1f * sinf(2.0f * (float)M_PI * 12.0f * i / RECORDER_MONITOR_FFT_SIZE);
    compute_monitor_fft(samples);
    assert(isfinite(monitor_range_db) && monitor_range_db > 0.0f && monitor_range_db < 18.0f);
    assert(monitor_spectrum[11] > 0);
    for (size_t i = 0; i < RECORDER_MONITOR_FFT_SIZE; ++i) samples[i] = 0.0f;
    monitor_floor_db = 0.0f;
    monitor_ceiling_db = 400.0f;
    compute_monitor_fft(samples);
    assert(isfinite(monitor_range_db) && monitor_range_db > 255.0f);
    for (size_t i = 0; i < RFSCOUT_RECORDER_MONITOR_BINS; ++i)
        assert(monitor_spectrum[i] == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test_source = path / "scale.c"
            test_source.write_text(
                "#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <math.h>\n"
                + defines + "\n" + bins + "\n" + declarations + "\n" + source[start:end] + harness
            )
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            str(test_source), "-lm", "-o", str(path / "scale")], check=True)
            subprocess.run([str(path / "scale")], check=True)
