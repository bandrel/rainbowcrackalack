#!/usr/bin/env python3
import json
from crack_diff import parse_pot, diff_cracks, assert_in_pot


def write(p, text):
    p.write_text(text)
    return str(p)


def test_parse_pot_hashcat_format(tmp_path):
    # hashcat pot: <hash>:<plaintext>, no $NT$ prefix.
    pot = write(tmp_path / "x.pot.hashcat", "abcd:hello\n0011223344556677:41424344\n")
    got = parse_pot(pot)
    assert got == {"abcd": b"hello".hex(), "0011223344556677": "41424344"}


def test_parse_pot_strips_nt_prefix(tmp_path):
    # JTR pot prepends $NT$ for NTLM; parser must strip it to recover the hash.
    pot = write(tmp_path / "x.pot", "$NT$deadbeef:secret\n")
    got = parse_pot(pot)
    assert got == {"deadbeef": b"secret".hex()}


def test_parse_pot_missing_file_is_empty(tmp_path):
    assert parse_pot(str(tmp_path / "nope.pot")) == {}


def test_diff_cracks_reports_regressions_and_improvements():
    base = {"aa": "01", "bb": "02", "cc": "03"}
    cand = {"aa": "01", "cc": "03", "dd": "04"}
    d = diff_cracks(base, cand)
    assert d["regressions"] == ["bb"]      # cracked by base, missed by cand
    assert d["improvements"] == ["dd"]     # cracked by cand, not base
    assert d["base_cracked"] == 3
    assert d["cand_cracked"] == 3


def test_assert_in_pot_true_on_match(tmp_path):
    pot = write(tmp_path / "x.pot.hashcat", "0011223344556677:41424344\n")
    assert assert_in_pot(pot, "0011223344556677", "41424344") is True


def test_assert_in_pot_false_when_missing(tmp_path):
    pot = write(tmp_path / "x.pot.hashcat", "")
    assert assert_in_pot(pot, "0011223344556677", "41424344") is False


def test_assert_in_pot_false_on_wrong_plaintext(tmp_path):
    pot = write(tmp_path / "x.pot.hashcat", "0011223344556677:ffffffff\n")
    assert assert_in_pot(pot, "0011223344556677", "41424344") is False
