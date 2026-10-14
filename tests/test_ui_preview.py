"""Run production C views and verify framebuffer/presentation invariants on the host."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("render_ui_preview", ROOT / "tools/render_ui_preview.py")
PREVIEW = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PREVIEW)


class UiPreviewTests(unittest.TestCase):
    def test_production_views_and_overlay_restoration(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            PREVIEW.build_and_render(output)
            images = list(output.glob("*.ppm"))
            self.assertEqual(11, len(images))
            for image in images:
                content = image.read_bytes()
                self.assertTrue(content.startswith(b"P6\n320 170\n255\n"))
                self.assertEqual(320 * 170 * 3 + len(b"P6\n320 170\n255\n"), len(content))


if __name__ == "__main__":
    unittest.main()
