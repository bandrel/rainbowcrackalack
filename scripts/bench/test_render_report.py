#!/usr/bin/env python3
from render_report import build_report, overall_pass


def test_overall_pass_all_green():
    roundtrips = {"cuda": {"ntlm8": {"cracked": True, "expected": "ab", "got": "ab"}}}
    crackdiff = {"base_cracked": 5, "cand_cracked": 5, "regressions": [], "improvements": []}
    assert overall_pass(roundtrips, crackdiff) is True


def test_overall_fail_on_roundtrip_miss():
    roundtrips = {"cuda": {"ntlm8": {"cracked": False, "expected": "ab", "got": None}}}
    crackdiff = {"base_cracked": 5, "cand_cracked": 5, "regressions": [], "improvements": []}
    assert overall_pass(roundtrips, crackdiff) is False


def test_overall_fail_on_regression():
    roundtrips = {"cuda": {"ntlm8": {"cracked": True, "expected": "ab", "got": "ab"}}}
    crackdiff = {"base_cracked": 5, "cand_cracked": 4, "regressions": ["bb"], "improvements": []}
    assert overall_pass(roundtrips, crackdiff) is False


def test_overall_fail_on_cross_backend_divergence():
    # ntlm8 cracks on cuda but not metal => divergence => fail.
    roundtrips = {
        "cuda": {"ntlm8": {"cracked": True, "expected": "ab", "got": "ab"}},
        "metal": {"ntlm8": {"cracked": False, "expected": "ab", "got": None}},
    }
    crackdiff = {"base_cracked": 1, "cand_cracked": 1, "regressions": [], "improvements": []}
    assert overall_pass(roundtrips, crackdiff) is False


def test_build_report_lists_regressed_hashes():
    roundtrips = {"cuda": {"ntlm8": {"cracked": True, "expected": "ab", "got": "ab"}}}
    crackdiff = {"base_cracked": 5, "cand_cracked": 4, "regressions": ["bb"], "improvements": []}
    md = build_report(roundtrips, crackdiff)
    assert "bb" in md
    assert "FAIL" in md
    assert "ntlm8" in md
