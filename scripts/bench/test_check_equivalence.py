"""Tests for check_equivalence.py."""
import json, os, sys, tempfile, unittest
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, THIS_DIR)
from check_equivalence import sha256_file, record_equivalence

class TestEquivalence(unittest.TestCase):
    def test_sha256_file(self):
        with tempfile.NamedTemporaryFile("wb", delete=False) as f:
            f.write(b"hello"); path = f.name
        try:
            self.assertEqual(sha256_file(path)[:12], "2cf24dba5fb0")
        finally:
            os.unlink(path)

    def test_record_match_and_mismatch(self):
        with tempfile.TemporaryDirectory() as d:
            record_equivalence(d, "chk1", "aaa", "aaa")
            record_equivalence(d, "chk2", "aaa", "bbb")
            with open(os.path.join(d, "equivalence.json")) as f:
                data = json.load(f)
            self.assertTrue(data["chk1"]["match"])
            self.assertFalse(data["chk2"]["match"])

if __name__ == "__main__":
    unittest.main()
