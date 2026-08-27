import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("update_oui", ROOT / "tools" / "update_oui.py")
UPDATE_OUI = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(UPDATE_OUI)


class UpdateOuiTests(unittest.TestCase):
    def test_parse_registry_accepts_expected_prefix_length(self):
        csv_text = (
            "Registry,Assignment,Organization Name,Organization Address\n"
            "MA-M,ABCDEF1,Example Corporation,Somewhere\n"
            "MA-L,112233,Wrong Length,Elsewhere\n"
        )
        records = UPDATE_OUI.parse_registry(csv_text, 7)
        self.assertEqual({0xABCDEF1: "Example"}, records)

    def test_extended_output_keeps_28_and_36_bit_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            original = UPDATE_OUI.EXTENDED_OUT
            try:
                output = pathlib.Path(directory) / "extended.inc"
                UPDATE_OUI.EXTENDED_OUT = output
                UPDATE_OUI.write_extended(
                    {0x1234567: "Medium"},
                    {0x123456789: "Legacy"},
                    {0x123456789: "Small", 0xABCDEF012: "Other"},
                )
                text = output.read_text(encoding="utf-8")
            finally:
                UPDATE_OUI.EXTENDED_OUT = original

        self.assertIn('0x1234567U, "Medium"', text)
        self.assertIn('0x123456789ULL, "Small"', text)
        self.assertNotIn('0x123456789ULL, "Legacy"', text)
        self.assertIn('0xABCDEF012ULL, "Other"', text)


if __name__ == "__main__":
    unittest.main()
