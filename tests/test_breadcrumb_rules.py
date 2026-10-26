"""Host regression: UTC daily breadcrumb filenames and velocity cadence."""
import calendar
import datetime
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORAGE = ROOT / "main/services/storage"

HARNESS = r'''
#include "breadcrumb_rules.h"

#include <assert.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    const time_t last = (time_t)atoll(argv[1]);
    const time_t next = (time_t)atoll(argv[2]);
    char name[64];
    assert(breadcrumb_daily_filename(last, name, sizeof(name)));
    assert(strcmp(name, "breadcrumbs-2026-10-25.jsonl") == 0);
    assert(breadcrumb_daily_filename(next, name, sizeof(name)));
    assert(strcmp(name, "breadcrumbs-2026-10-26.jsonl") == 0);
    assert(!breadcrumb_daily_filename(last, name, 8U));
    assert(!breadcrumb_daily_filename(last, NULL, sizeof(name)));
    assert(breadcrumb_capture_interval_us(0.0f) == 5000000LL);
    assert(breadcrumb_capture_interval_us(2.99f) == 5000000LL);
    assert(breadcrumb_capture_interval_us(3.0f) == 2000000LL);
    assert(breadcrumb_capture_interval_us(14.99f) == 2000000LL);
    assert(breadcrumb_capture_interval_us(15.0f) == 1000000LL);
    assert(breadcrumb_capture_interval_us(60.0f) == 1000000LL);
    return 0;
}
'''


class BreadcrumbRulesTests(unittest.TestCase):
    def test_daily_utc_names_and_speed_tiers(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            source = folder / "breadcrumb_rules_test.c"
            binary = folder / "breadcrumb_rules_test"
            source.write_text(
                "#include <stdlib.h>\n" + HARNESS
            )
            subprocess.run(
                [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                 "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-I", str(STORAGE), str(source), "-o", str(binary)],
                check=True,
            )
            utc_last = calendar.timegm(
                datetime.datetime(2026, 10, 25, 23, 59, 59).timetuple()
            )
            utc_next = utc_last + 1
            subprocess.run([str(binary), str(utc_last), str(utc_next)], check=True)


if __name__ == "__main__":
    unittest.main()
