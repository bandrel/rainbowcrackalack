"""Tests for parse_gen_bench.py."""
import os, sys, unittest
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(THIS_DIR, "fixtures")
sys.path.insert(0, THIS_DIR)
from parse_gen_bench import parse_rate

class TestParseRate(unittest.TestCase):
    def test_extracts_rate(self):
        with open(os.path.join(FIXTURES, "sample_gen_bench.log")) as f:
            text = f.read()
        self.assertEqual(parse_rate(text), 7389.0)

    def test_raises_when_absent(self):
        with self.assertRaises(ValueError):
            parse_rate("no rate here")

    def test_strips_ansi_color_codes(self):
        # Real crackalack_gen wraps the rate value in ANSI color codes.
        line = "Run time: 6.0 secs; Chains generated: 40960; Rate: \x1b[1;97m6863/s\x1b[0m"
        self.assertEqual(parse_rate(line), 6863.0)

if __name__ == "__main__":
    unittest.main()
