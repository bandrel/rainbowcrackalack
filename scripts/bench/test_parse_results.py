"""Tests for parse_results.py."""
import json
import os
import sys
import tempfile
import unittest

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(THIS_DIR, "fixtures")
sys.path.insert(0, THIS_DIR)

from parse_results import (
    parse_time_file,
    count_cracks,
    summarize,
    write_summary_md,
)


class TestParseTimeFile(unittest.TestCase):
    def test_extracts_wall_and_rss(self):
        result = parse_time_file(os.path.join(FIXTURES, "sample.time"))
        # 8:21.45 == 8*60 + 21.45 == 501.45 seconds
        self.assertAlmostEqual(result["wall_seconds"], 501.45, places=2)
        # 6815744 KB
        self.assertEqual(result["peak_rss_kb"], 6815744)
        self.assertEqual(result["exit_status"], 0)

    def test_handles_h_mm_ss_format(self):
        with tempfile.NamedTemporaryFile("w", suffix=".time", delete=False) as f:
            f.write(
                "\tElapsed (wall clock) time (h:mm:ss or m:ss): 1:02:03\n"
                "\tMaximum resident set size (kbytes): 100\n"
                "\tExit status: 0\n"
            )
            path = f.name
        try:
            result = parse_time_file(path)
            self.assertAlmostEqual(result["wall_seconds"], 3723.0, places=2)
        finally:
            os.unlink(path)


class TestCountCracks(unittest.TestCase):
    def test_counts_cracked_lines(self):
        n = count_cracks(os.path.join(FIXTURES, "sample_lookup.log"))
        self.assertEqual(n, 2)

    def test_returns_zero_for_missing_file(self):
        self.assertEqual(count_cracks("/nonexistent/path"), 0)


class TestSummarize(unittest.TestCase):
    def test_median_and_speedup(self):
        trials = [
            {"branch": "blurbdust", "wall_seconds": 100.0, "peak_rss_kb": 1000, "cracked": 5, "exit_status": 0},
            {"branch": "blurbdust", "wall_seconds": 110.0, "peak_rss_kb": 1100, "cracked": 5, "exit_status": 0},
            {"branch": "blurbdust", "wall_seconds": 120.0, "peak_rss_kb": 1200, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 20.0,  "peak_rss_kb": 2000, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 22.0,  "peak_rss_kb": 2100, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 24.0,  "peak_rss_kb": 2200, "cracked": 5, "exit_status": 0},
        ]
        summary = summarize(trials)
        self.assertAlmostEqual(summary["blurbdust"]["wall_median"], 110.0)
        self.assertAlmostEqual(summary["feature"]["wall_median"], 22.0)
        self.assertAlmostEqual(summary["speedup"], 110.0 / 22.0, places=4)
        self.assertFalse(summary["divergence"])

    def test_divergence_when_crack_counts_differ(self):
        trials = [
            {"branch": "blurbdust", "wall_seconds": 100.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "blurbdust", "wall_seconds": 110.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 20.0,  "peak_rss_kb": 1, "cracked": 6, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 22.0,  "peak_rss_kb": 1, "cracked": 6, "exit_status": 0},
        ]
        summary = summarize(trials)
        self.assertTrue(summary["divergence"])

    def test_insufficient_data(self):
        trials = [
            {"branch": "blurbdust", "wall_seconds": 100.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 1},
            {"branch": "blurbdust", "wall_seconds": 110.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 20.0,  "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "feature",   "wall_seconds": 22.0,  "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
        ]
        summary = summarize(trials)
        self.assertEqual(summary["blurbdust"]["status"], "INSUFFICIENT_DATA")

    def test_summarize_works_with_arbitrary_branch_names(self):
        trials = [
            {"branch": "main",  "wall_seconds": 100.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "main",  "wall_seconds": 110.0, "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "feat1", "wall_seconds": 50.0,  "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
            {"branch": "feat1", "wall_seconds": 55.0,  "peak_rss_kb": 1, "cracked": 5, "exit_status": 0},
        ]
        summary = summarize(trials)
        self.assertIn("main", summary)
        self.assertIn("feat1", summary)
        self.assertIn("speedup", summary)
        # speedup numerator/denominator order is deterministic (alphabetical):
        # feat1 (median 52.5) / main (median 105.0) — but which is which depends on
        # implementation. Just assert the ratio is one of the two expected values.
        expected_a = 105.0 / 52.5
        expected_b = 52.5 / 105.0
        self.assertTrue(
            abs(summary["speedup"] - expected_a) < 1e-6 or
            abs(summary["speedup"] - expected_b) < 1e-6,
            f"speedup {summary['speedup']} matches neither {expected_a} nor {expected_b}",
        )


class TestWriteSummaryMd(unittest.TestCase):
    def test_writes_file_with_headline(self):
        trials = [
            {"branch": "blurbdust", "wall_seconds": 100.0, "peak_rss_kb": 1000, "cracked": 5, "exit_status": 0, "trial": 1},
            {"branch": "blurbdust", "wall_seconds": 110.0, "peak_rss_kb": 1100, "cracked": 5, "exit_status": 0, "trial": 2},
            {"branch": "blurbdust", "wall_seconds": 120.0, "peak_rss_kb": 1200, "cracked": 5, "exit_status": 0, "trial": 3},
            {"branch": "feature",   "wall_seconds": 20.0,  "peak_rss_kb": 2000, "cracked": 5, "exit_status": 0, "trial": 1},
            {"branch": "feature",   "wall_seconds": 22.0,  "peak_rss_kb": 2100, "cracked": 5, "exit_status": 0, "trial": 2},
            {"branch": "feature",   "wall_seconds": 24.0,  "peak_rss_kb": 2200, "cracked": 5, "exit_status": 0, "trial": 3},
        ]
        with tempfile.TemporaryDirectory() as d:
            out_md = os.path.join(d, "summary.md")
            meta = {"host": "dell3", "gpu": "TestGPU", "parts": 8, "hash_count": 100,
                    "blurbdust_sha": "ace7f9d", "feature_sha": "deadbee"}
            write_summary_md(trials, summarize(trials), meta, out_md)
            with open(out_md) as f:
                content = f.read()
            self.assertIn("blurbdust", content)
            self.assertIn("feature", content)
            self.assertIn("speedup", content.lower())
            self.assertIn("ace7f9d", content)


if __name__ == "__main__":
    unittest.main()
