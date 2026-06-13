"""Tests for parse_gen_bench.py."""
import os, sys, unittest
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(THIS_DIR, "fixtures")
sys.path.insert(0, THIS_DIR)
from parse_gen_bench import parse_rate

class TestParseRate(unittest.TestCase):
    def test_extracts_rate(self):
        text = open(os.path.join(FIXTURES, "sample_gen_bench.log")).read()
        self.assertEqual(parse_rate(text), 7389.0)

    def test_raises_when_absent(self):
        with self.assertRaises(ValueError):
            parse_rate("no rate here")

if __name__ == "__main__":
    unittest.main()
