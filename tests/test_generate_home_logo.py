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


    def test_neutral_antialias_gray_does_not_quantize_to_orange(self):
        self.assertEqual(0, GENERATE_HOME_LOGO.nearest_palette((107, 107, 106)))
        self.assertEqual(0, GENERATE_HOME_LOGO.nearest_palette((180, 181, 179)))
        self.assertEqual(2, GENERATE_HOME_LOGO.nearest_palette((200, 199, 201)))

    def test_colored_orange_still_quantizes_to_orange(self):
        self.assertEqual(1, GENERATE_HOME_LOGO.nearest_palette((247, 118, 11)))

    def test_opaque_rgba_preserves_source_color(self):
        rows = [bytearray((255, 128, 0, 255))]

        rgb = GENERATE_HOME_LOGO.rgb_at(rows, 4, 0, 0)

        self.assertEqual((255, 128, 0), rgb)
        self.assertEqual(1, GENERATE_HOME_LOGO.nearest_palette(rgb))


if __name__ == "__main__":
    unittest.main()
