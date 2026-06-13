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

    def test_skips_gen_banner_before_csv(self):
        # ncu output is preceded by crackalack_gen's (ANSI-colored) banner and
        # ==PROF== lines; the CSV header appears partway down.
        text = (
            "==PROF== Connected to process 123\n"
            "\x1b[1;97mRainbow Crackalack v1.3\x1b[0m\n"
            "Run time: 6.0 secs; Chains generated: 40960; Rate: \x1b[1;97m6863/s\x1b[0m\n"
            '"ID","Process ID","Kernel Name","Metric Name","Metric Unit","Metric Value"\n'
            '"0","123","crackalack_netntlmv1_7","launch__registers_per_thread","register/thread","119"\n'
            '"0","123","crackalack_netntlmv1_7","sm__throughput.avg.pct_of_peak_sustained_elapsed","%","95.91"\n'
            "==PROF== Disconnected\n"
        )
        m = parse_ncu_csv(text)
        self.assertEqual(m["registers_per_thread"], 119)
        self.assertAlmostEqual(m["compute_sm_pct"], 95.91)

class TestEmptyCsv(unittest.TestCase):
    def test_junk_returns_empty(self):
        self.assertEqual(parse_ncu_csv("not a csv"), {})

    def test_header_only_returns_empty(self):
        header = '"ID","Process ID","Kernel Name","Metric Name","Metric Unit","Metric Value"\n'
        self.assertEqual(parse_ncu_csv(header), {})

if __name__ == "__main__":
    unittest.main()
