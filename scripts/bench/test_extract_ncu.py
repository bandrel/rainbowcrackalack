"""Tests for extract_ncu.py."""
import os, sys, unittest
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(THIS_DIR, "fixtures")
sys.path.insert(0, THIS_DIR)
from extract_ncu import parse_ncu_csv, METRIC_MAP

class TestParseNcuCsv(unittest.TestCase):
    def test_maps_known_metrics(self):
        with open(os.path.join(FIXTURES, "sample_ncu.csv")) as f:
            text = f.read()
        m = parse_ncu_csv(text)
        self.assertAlmostEqual(m["compute_sm_pct"], 97.94)
        self.assertAlmostEqual(m["dram_pct"], 0.02)
        self.assertAlmostEqual(m["achieved_occupancy_pct"], 31.21)
        self.assertEqual(m["registers_per_thread"], 119)
        self.assertEqual(m["shared_ld_bank_conflicts"], 0)

    def test_metric_map_covers_friendly_names(self):
        self.assertIn("sm__throughput.avg.pct_of_peak_sustained_elapsed", METRIC_MAP)

if __name__ == "__main__":
    unittest.main()
