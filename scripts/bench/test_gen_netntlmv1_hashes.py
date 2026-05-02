"""Tests for gen_netntlmv1_hashes.py.

Test vectors were computed from feature/faster-table-loading's
cpu_rt_functions.c (setup_des_key + libgcrypt DES-ECB of the magic value
0x1122334455667788). Both branches use the same algorithm, so these vectors
are valid for both.
"""
import os
import sys
import tempfile
import unittest

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, THIS_DIR)

from gen_netntlmv1_hashes import setup_des_key, netntlmv1_hash, generate


VECTORS = [
    (b"abcdefg", bytes.fromhex("60b0986c462a98ce"), bytes.fromhex("397be08fb534a552")),
    (b"1234567", bytes.fromhex("30988c6642a8d86e"), bytes.fromhex("b4f7c7f6d3996837")),
    (b"ABCDEFG", bytes.fromhex("40a09068442a188e"), bytes.fromhex("c50758e0ac39c6db")),
    (b"passwd1", bytes.fromhex("70305c6e36ba9062"), bytes.fromhex("6fd42c5eb4ff856a")),
]


class TestSetupDesKey(unittest.TestCase):
    def test_known_vectors(self):
        for plaintext, expected_key, _hash in VECTORS:
            with self.subTest(plaintext=plaintext):
                self.assertEqual(setup_des_key(plaintext), expected_key)

    def test_input_must_be_seven_bytes(self):
        with self.assertRaises(ValueError):
            setup_des_key(b"abc")
        with self.assertRaises(ValueError):
            setup_des_key(b"abcdefgh")


class TestNetntlmv1Hash(unittest.TestCase):
    def test_known_vectors(self):
        for plaintext, _key, expected_hash in VECTORS:
            with self.subTest(plaintext=plaintext):
                self.assertEqual(netntlmv1_hash(plaintext), expected_hash)


class TestGenerate(unittest.TestCase):
    def test_seed_is_deterministic(self):
        with tempfile.TemporaryDirectory() as d:
            out_a = os.path.join(d, "a.txt")
            out_b = os.path.join(d, "b.txt")
            generate(seed=42, count=10, out_path=out_a)
            generate(seed=42, count=10, out_path=out_b)
            with open(out_a) as fa, open(out_b) as fb:
                self.assertEqual(fa.read(), fb.read())

    def test_output_format(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "h.txt")
            generate(seed=7, count=5, out_path=out)
            with open(out) as f:
                lines = f.read().strip().split("\n")
            self.assertEqual(len(lines), 5)
            for line in lines:
                self.assertEqual(len(line), 16)
                int(line, 16)  # raises if not hex

    def test_plaintext_sidecar(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "h.txt")
            generate(seed=7, count=3, out_path=out)
            with open(out + ".plaintexts") as f:
                lines = f.read().strip().split("\n")
            self.assertEqual(len(lines), 3)
            for line in lines:
                # "<hash>:<14 hex chars of plaintext>"
                hash_hex, plain_hex = line.split(":")
                self.assertEqual(len(hash_hex), 16)
                self.assertEqual(len(plain_hex), 14)


if __name__ == "__main__":
    unittest.main()
