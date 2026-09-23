import csv
import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SIGNATURES = ROOT / "data" / "surveillance_signatures.csv"

VALID_SIGNALS = {
    "oui",
    "name_contains",
    "name_prefix",
    "company_id",
    "service_uuid16",
    "service_uuid128",
    "remote_id",
}


class SurveillanceSignatureDataTests(unittest.TestCase):
    def load_rows(self):
        with SIGNATURES.open(newline="", encoding="utf-8") as stream:
            return list(csv.DictReader(stream))

    def test_evidence_rows_have_unique_identity_and_positive_weight(self):
        seen = set()
        for row in self.load_rows():
            identity = (
                row["type"].strip().upper(),
                row["brand"].strip(),
                row["signal"].strip().lower(),
                row["value"].strip().upper(),
            )
            self.assertNotIn(identity, seen)
            seen.add(identity)
            self.assertIn(identity[2], VALID_SIGNALS)
            self.assertGreater(int(row["weight"]), 0)
            self.assertTrue(row["evidence"].strip())

    def test_raven_proprietary_services_are_independent_low_weight_evidence(self):
        raven = {
            row["value"].strip().upper(): int(row["weight"])
            for row in self.load_rows()
            if row["type"].strip().upper() == "AUDIO"
            and row["brand"].strip() == "Raven"
            and row["signal"].strip().lower() == "service_uuid16"
        }
        self.assertEqual(
            raven,
            {
                "3100": 1,
                "3200": 1,
                "3300": 1,
                "3400": 1,
                "3500": 1,
            },
        )

    def test_generated_tables_are_current(self):
        generated = ROOT / "main" / "services" / "wireless" / "surveillance_signatures.inc"
        report = ROOT / "data" / "surveillance_oui_validation.csv"
        before_generated = generated.read_bytes()
        before_report = report.read_bytes()

        subprocess.run(
            [sys.executable, str(ROOT / "tools" / "update_surveillance.py")],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertEqual(generated.read_bytes(), before_generated)
        self.assertEqual(report.read_bytes(), before_report)

    def test_product_specific_non_oui_signals_live_in_database(self):
        signals = {
            (row["brand"].strip(), row["signal"].strip().lower(), row["value"].strip().upper())
            for row in self.load_rows()
        }
        self.assertIn(("Flock", "company_id", "09C8"), signals)
        self.assertIn(("Brilliant Frame", "service_uuid128", "7A2300015475A6A4654C8431F6AD49C4"), signals)
        self.assertIn(("Rokid", "service_uuid128", "0000910000001000800000805F9B34FB"), signals)
        self.assertIn(("Remote ID", "remote_id", "1"), signals)


if __name__ == "__main__":
    unittest.main()
