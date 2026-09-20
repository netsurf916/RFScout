import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_home_logo", ROOT / "tools" / "generate_home_logo.py"
)
GENERATE_HOME_LOGO = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(GENERATE_HOME_LOGO)


class GenerateHomeLogoTests(unittest.TestCase):
    def test_partial_alpha_is_composited_against_black_before_quantization(self):
        rows = [bytearray((255, 128, 0, 20))]

        rgb = GENERATE_HOME_LOGO.rgb_at(rows, 4, 0, 0)

        self.assertEqual((20, 10, 0), rgb)
        self.assertEqual(0, GENERATE_HOME_LOGO.nearest_palette(rgb))


    def test_diagnostic_canonical_logo_palette_samples(self):
        from collections import Counter

        width, height, channels, rows = GENERATE_HOME_LOGO.read_png(ROOT / "assets" / "logo.png")
        x0, y0, x1, y1 = GENERATE_HOME_LOGO.content_bounds(width, height, channels, rows)
        crop_width, crop_height = x1 - x0, y1 - y0
        orange = Counter()
        white = Counter()
        black = Counter()
        for y in range(122):
            source_y = y0 + min(crop_height - 1, (y * crop_height) // 122)
            for x in range(216):
                source_x = x0 + min(crop_width - 1, (x * crop_width) // 216)
                rgb = GENERATE_HOME_LOGO.rgb_at(rows, channels, source_x, source_y)
                target = GENERATE_HOME_LOGO.nearest_palette(rgb)
                (black if target == 0 else orange if target == 1 else white)[rgb] += 1

        print("CANONICAL_LOGO", {
            "channels": channels,
            "bounds": (x0, y0, x1, y1),
            "counts": (sum(black.values()), sum(orange.values()), sum(white.values())),
            "orange_top": orange.most_common(30),
            "orange_darkest": sorted(orange.items(), key=lambda item: max(item[0]))[:30],
        })

    def test_opaque_rgba_preserves_source_color(self):
        rows = [bytearray((255, 128, 0, 255))]

        rgb = GENERATE_HOME_LOGO.rgb_at(rows, 4, 0, 0)

        self.assertEqual((255, 128, 0), rgb)
        self.assertEqual(1, GENERATE_HOME_LOGO.nearest_palette(rgb))


if __name__ == "__main__":
    unittest.main()
