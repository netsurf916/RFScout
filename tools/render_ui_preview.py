"""Compile real firmware views against deterministic host services; emit 320x170 PPMs.

Requires a C compiler, no ESP-IDF or image libraries. This exercises presentation,
not hardware acquisition. Run with --output-dir to keep the rendered fixtures.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = ROOT / "main/services/display"
SOURCES = [
    "services/display/ui_theme.c", "services/display/ui_game.c",
    "services/display/ui_chrome.c", "services/display/ui_present.c",
    "services/display/battery_indicator.c", "services/display/display_mask.c",
    "apps/audio_spectrum/view.c", "apps/gps/gps_view.c",
    "apps/recorder/recorder_view.c", "apps/hotspots/hotspots_view.c",
    "apps/ota/ota_view.c", "apps/cc1101/cc1101_view.c",
    "apps/radio_common/radio_app_view.c", "apps/wifi_ble/wifi_settings_view.c",
]


def extract_function(source: str, name: str) -> str:
    """Borrow the driver's actual drawing implementation, excluding its hardware I/O."""
    match = re.search(r"^(?:static )?(?:bool|void|esp_err_t)\s+" + name + r"\(\s", source, re.M)
    if match is None:
        raise ValueError(f"Driver function not found: {name}")
    start = match.start()
    opening = source.index("{", match.end())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_and_render(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rfscout-ui-") as temporary:
        temp = Path(temporary)
        shims = {
            "host_string.h": """#pragma once
#include <stddef.h>
size_t strlcpy(char *, const char *, size_t);
""",
            "esp_err.h": """#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FINISHED 4
const char *esp_err_to_name(esp_err_t);
""",
            "esp_log.h": """#pragma once
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
""",
            "esp_timer.h": """#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
            "esp_heap_caps.h": """#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
""",
            "device_config.h": """#pragma once
#include "ui_theme.h"
esp_err_t device_config_set_theme(rfscout_theme_id_t);
rfscout_theme_id_t device_config_theme(void);
esp_err_t device_config_save(void);
typedef enum { RFSCOUT_EVIDENCE_STRENGTH_EVERYTHING, RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE,
RFSCOUT_EVIDENCE_STRENGTH_PROBABLE, RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE,
RFSCOUT_EVIDENCE_STRENGTH_COUNT } rfscout_evidence_strength_t;
""",
        }
        for name, content in shims.items():
            (temp / name).write_text(content)
        driver = (DISPLAY / "display.c").read_text()
        font_start = driver.index("static const uint8_t font[")
        font_end = driver.index("// clang-format on", font_start)
        drawing = [driver[font_start:font_end]]
        for name in ["valid_rect", "display_fill_rect_raw", "draw_character", "display_clear",
                     "display_fill_rect", "display_draw_text", "display_read_rect", "display_write_rect"]:
            drawing.append(extract_function(driver, name))
        host = (ROOT / "tests/ui_host/preview.c").read_text()
        host = host.replace("/* DRIVER_DRAWING */", "\n\n".join(drawing))
        (temp / "preview.c").write_text(host)
        includes = sorted({path.parent for path in (ROOT / "main").rglob("*.h")})
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-ffunction-sections", "-fdata-sections", "-I", str(temp),
                   "-include", str(temp / "host_string.h")]
        for path in includes:
            command.extend(["-I", str(path)])
        command.extend([str(temp / "preview.c")])
        command.extend(str(ROOT / "main" / path) for path in SOURCES)
        command.extend(["-Wl,--gc-sections", "-o", str(temp / "preview")])
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(temp / "preview"), str(output_dir.resolve())], check=True, cwd=ROOT)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/ui-preview")
    build_and_render(parser.parse_args().output_dir)
