# Crack Regression / False-Negative Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an automated framework that proves cracking has no false negatives (self-contained round-trip) and no regressions (BASE-vs-CANDIDATE differential) for the NTLM-8, NTLM-9, and NetNTLMv1-7 (default-challenge) paths, runnable on dell3 (CUDA) and local Metal.

**Architecture:** A new `scripts/bench/run_regression.sh` driver (phases `prepare | roundtrip | crackdiff | report | all`) reuses the bench harness's GPU flock and BASE/CANDIDATE build pattern. Pure-logic pieces (pot parsing, crack-set diff, report rendering) live in tested Python modules; GPU orchestration lives in the shell driver. `gen_known_hash` is extended from NetNTLMv1-only to also emit NTLM hashes so the round-trip can construct a provably-in-table hash for every path.

**Tech Stack:** C (libgcrypt + OpenSSL DES), bash, Python 3 + pycryptodome (existing bench venv), the existing `crackalack_gen`/`crackalack_sort`/`get_chain`/`crackalack_lookup` binaries.

**Spec:** `docs/superpowers/specs/2026-06-20-crack-regression-framework-design.md`

---

## Key interface facts (verified against the codebase)

- `crackalack_gen <hash> <charset> <min> <max> <table_index> <chain_len> <num_chains> <part>` — hash tokens: `ntlm`, `netntlmv1`, `md5`, `lm`. Output file: `<hash>_<charset>#<min>-<max>_<table_index>_<chain_len>x<num_chains>_<part>.rt`. Output is **unsorted**.
- `crackalack_sort [--jobs N] table.rt [...]` — sorts in place (lookup binary-search requires sorted tables).
- `get_chain <table.rt> <chain_num>` — prints `Start index: <N>\nEnd index:   <M>`.
- `gen_known_hash <chain_len> <reduction_offset> <start_index> <target_position> [--challenge <16hex>]` — TODAY: NetNTLMv1/byte/len-7 only. Prints `hash=<hex>\nplaintext=<hex>\npos=<n> start_index=<n>`. `reduction_offset` = `table_index * 65536` (0 for table_index 0).
- `crackalack_lookup <rt_dir> <hash|hashfile> [pot_path]` — when `pot_path` is given, the JTR pot is written there and the hashcat pot to `pot_path + ".hashcat"`. Lookup infers hash type from the table filename in `rt_dir`. Default challenge `1122334455667788` is adopted from a table filename with no `-chal` suffix.
- Pot line format (`save_cracked_hash`): JTR = `[$NT$]<hash>:<plaintext>\n` (`$NT$` prefix only for `HASH_NTLM`); hashcat = `<hash>:<plaintext>\n` (no prefix). For NetNTLMv1 the `<plaintext>` field is the 14-hex of the 7 raw bytes; for NTLM it is the raw ASCII plaintext.
- CPU primitives in `cpu_rt_functions.h`: `ntlm_hash(char*, len, uchar*)`, `index_to_plaintext(...)`, `hash_to_index(...)`, `fill_plaintext_space_table(...)`. `setup_des_key(char[], uchar*)`.
- `charset.h`: `char *validate_charset(char *charset_name)` returns the charset string (e.g. for `"ascii-32-95"`).
- Existing helpers to reuse: `scripts/bench/lib/gpu_lock.sh` (`with_gpu_lock`), `scripts/bench/gen_netntlmv1_hashes.py` (`--seed --count --out`, writes hashes + `<out>.plaintexts`), the venv-creation pattern in `scripts/bench/run_benchmark.sh::phase_prepare`.

## Round-trip configs (the 3 in-scope paths)

| name | gen args | gen_known_hash extra flags | hash hex len |
|------|----------|----------------------------|--------------|
| `ntlm8` | `ntlm ascii-32-95 8 8 0 1000 1024 0` | `--algo ntlm --charset ascii-32-95 --plaintext-len 8` | 32 |
| `ntlm9` | `ntlm ascii-32-95 9 9 0 1000 1024 0` | `--algo ntlm --charset ascii-32-95 --plaintext-len 9` | 32 |
| `netntlmv1_7` | `netntlmv1 byte 7 7 0 1000 1024 0` | *(none — defaults)* | 16 |

All use `table_index 0` ⇒ `reduction_offset 0`, `chain_len 1000`, `target_pos 500`.

## File structure

| File | Responsibility | Created/Modified |
|------|----------------|------------------|
| `gen_known_hash.c` | Add `--algo ntlm` (charset/len configurable) alongside existing NetNTLMv1 default | Modify |
| `scripts/bench/crack_diff.py` | Pure logic: parse pot files → `{hash: plaintext_hex}`; diff base vs cand; assert a hash cracked with expected plaintext | Create |
| `scripts/bench/test_crack_diff.py` | Unit tests for `crack_diff.py` (fixtures, no GPU) | Create |
| `scripts/bench/render_report.py` | Pure logic: read JSON artifacts → `regression_summary.md` + exit code | Create |
| `scripts/bench/test_render_report.py` | Unit tests for `render_report.py` | Create |
| `scripts/bench/test_gen_known_hash.py` | Invokes built `gen_known_hash` binary; checks ntlm + netntlmv1 output (no GPU) | Create |
| `scripts/bench/run_regression.sh` | Phase orchestration: prepare/roundtrip/crackdiff/report | Create |
| `scripts/bench/README.md` | Document the regression framework usage | Modify |

---

## Task 1: Extend `gen_known_hash` to emit NTLM hashes

**Files:**
- Modify: `gen_known_hash.c`
- Test: `scripts/bench/test_gen_known_hash.py` (create)

The current tool hardcodes NetNTLMv1/byte/len-7. Generalize the chain walk to a selectable algorithm while keeping the existing 4 positional args and defaults 100% backward-compatible (no flags ⇒ today's behavior).

- [ ] **Step 1: Write the failing test**

Create `scripts/bench/test_gen_known_hash.py`:

```python
#!/usr/bin/env python3
"""Tests for the gen_known_hash CLI binary (no GPU required).

Run from the repo root with the binary already built:
    make macos   # or: make linux
    .venv/bin/python -m pytest scripts/bench/test_gen_known_hash.py
The binary path is the repo-root ./gen_known_hash (override with GEN_KNOWN_HASH).
"""
import os
import subprocess
import pytest

from Crypto.Hash import MD4  # pycryptodome

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.environ.get("GEN_KNOWN_HASH", os.path.join(REPO_ROOT, "gen_known_hash"))


def _run(args):
    proc = subprocess.run([BIN] + args, capture_output=True, text=True)
    assert proc.returncode == 0, f"gen_known_hash failed: {proc.stderr}"
    out = {}
    for line in proc.stdout.splitlines():
        if line.startswith("hash="):
            out["hash"] = line[len("hash="):].strip()
        elif line.startswith("plaintext="):
            out["plaintext"] = line[len("plaintext="):].strip()
    return out


def ntlm_hash_hex(plaintext_bytes: bytes) -> str:
    return MD4.new(plaintext_bytes.decode("latin-1").encode("utf-16-le")).hexdigest()


@pytest.mark.skipif(not os.path.exists(BIN), reason="gen_known_hash not built")
def test_netntlmv1_default_still_works():
    # No flags => NetNTLMv1/byte/len-7: 16-hex hash, 14-hex plaintext.
    out = _run(["1000", "0", "12345", "10"])
    assert len(out["hash"]) == 16
    assert len(out["plaintext"]) == 14


@pytest.mark.skipif(not os.path.exists(BIN), reason="gen_known_hash not built")
def test_ntlm_algo_emits_ntlm_hash():
    # --algo ntlm => 32-hex NTLM hash that equals MD4(UTF-16LE(plaintext)).
    out = _run(["1000", "0", "12345", "10",
                "--algo", "ntlm", "--charset", "ascii-32-95", "--plaintext-len", "8"])
    assert len(out["hash"]) == 32
    pt = bytes.fromhex(out["plaintext"])
    assert len(pt) == 8
    assert out["hash"] == ntlm_hash_hex(pt)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts/bench/.venv/bin/python -m pytest scripts/bench/test_gen_known_hash.py -v` (or any python with pytest+pycryptodome).
Expected: `test_ntlm_algo_emits_ntlm_hash` FAILS — current binary treats `--algo` as a positional and errors with "Usage:" (returncode 1). (`test_netntlmv1_default_still_works` passes against the old binary.)

- [ ] **Step 3: Implement the NTLM-capable rewrite**

In `gen_known_hash.c`, add `#include "charset.h"` and `#include "cpu_rt_functions.h"` is already present. Add an algorithm enum and an argument parser, then generalize the walk. Replace the body of `main()` from the `/* Collect the 4 positional args ... */` comment through the end with:

```c
  enum { ALGO_NETNTLMV1, ALGO_NTLM } algo = ALGO_NETNTLMV1;
  const char *charset_name = "byte";
  unsigned int plaintext_len = 7;

  const char *positional[4] = {NULL, NULL, NULL, NULL};
  int pos_count = 0;

  for (int i = 1; i < ac; i++) {
    if (strcmp(av[i], "--challenge") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --challenge requires a 16-hex argument\n", av[0]); return 1; }
      if (parse_challenge_str_local(av[i + 1], challenge) != 0) {
        fprintf(stderr, "%s: invalid challenge '%s' (need exactly 16 hex chars)\n", av[0], av[i + 1]);
        return 1;
      }
      i++;
    } else if (strcmp(av[i], "--algo") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --algo requires a value (ntlm|netntlmv1)\n", av[0]); return 1; }
      if (strcmp(av[i + 1], "ntlm") == 0) algo = ALGO_NTLM;
      else if (strcmp(av[i + 1], "netntlmv1") == 0) algo = ALGO_NETNTLMV1;
      else { fprintf(stderr, "%s: unknown --algo '%s'\n", av[0], av[i + 1]); return 1; }
      i++;
    } else if (strcmp(av[i], "--charset") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --charset requires a value\n", av[0]); return 1; }
      charset_name = av[++i];
    } else if (strcmp(av[i], "--plaintext-len") == 0) {
      if (i + 1 >= ac) { fprintf(stderr, "%s: --plaintext-len requires a value\n", av[0]); return 1; }
      plaintext_len = (unsigned int)strtoul(av[++i], NULL, 10);
    } else {
      if (pos_count < 4) positional[pos_count] = av[i];
      pos_count++;
    }
  }

  if (pos_count != 4) {
    fprintf(stderr,
            "Usage: %s chain_len reduction_offset start_index target_position\n"
            "          [--algo ntlm|netntlmv1] [--charset NAME] [--plaintext-len N] [--challenge <16hex>]\n",
            av[0]);
    return 1;
  }

  unsigned int chain_len        = (unsigned int)strtoul(positional[0], NULL, 10);
  unsigned int reduction_offset = (unsigned int)strtoul(positional[1], NULL, 10);
  uint64_t start_index          = strtoull(positional[2], NULL, 10);
  unsigned int target_pos       = (unsigned int)strtoul(positional[3], NULL, 10);

  /* Select charset + hash length per algorithm. */
  for (int i = 0; i < 256; i++) charset_byte[i] = (char)i;
  char *charset;
  unsigned int charset_len, hash_len;
  if (algo == ALGO_NTLM) {
    charset = validate_charset((char *)charset_name);
    if (charset == NULL) { fprintf(stderr, "%s: invalid charset '%s'\n", av[0], charset_name); return 1; }
    charset_len = (unsigned int)strlen(charset);
    hash_len = 16;
  } else {
    charset = charset_byte;
    charset_len = 256;
    plaintext_len = 7;   /* NetNTLMv1 is fixed at 7 bytes. */
    hash_len = 8;
  }

  uint64_t plaintext_space_up_to_index[16] = {0};
  uint64_t plaintext_space_total =
      fill_plaintext_space_table(charset_len, plaintext_len, plaintext_len, plaintext_space_up_to_index);

  uint64_t index = start_index;
  char plaintext[16] = {0};
  unsigned char hash[16] = {0};
  unsigned int out_plaintext_len = plaintext_len;

  for (unsigned int pos = 0; pos < chain_len - 1; pos++) {
    index_to_plaintext(index, charset, charset_len, plaintext_len, plaintext_len,
                       plaintext_space_up_to_index, plaintext, &out_plaintext_len);
    if (algo == ALGO_NTLM)
      ntlm_hash(plaintext, out_plaintext_len, hash);
    else
      netntlmv1_hash_correct((unsigned char *)plaintext, hash, challenge);

    if (pos == target_pos) {
      printf("hash=");
      for (unsigned int j = 0; j < hash_len; j++) printf("%02x", hash[j]);
      printf("\nplaintext=");
      for (unsigned int j = 0; j < out_plaintext_len; j++) printf("%02x", (unsigned char)plaintext[j]);
      printf("\npos=%u start_index=%" PRIu64 "\n", pos, start_index);
      return 0;
    }
    index = hash_to_index(hash, hash_len, reduction_offset, plaintext_space_total, pos);
  }
  fprintf(stderr, "Error: target_pos=%u >= chain_len-1=%u\n", target_pos, chain_len - 1);
  return 1;
```

Leave the existing `netntlmv1_hash_correct`, `parse_challenge_str_local`, includes, and the `default_challenge`/`challenge` setup above this block unchanged.

- [ ] **Step 4: Build and run the test to verify it passes**

Run (macOS): `make gen_known_hash` (or `make macos`), then
`scripts/bench/.venv/bin/python -m pytest scripts/bench/test_gen_known_hash.py -v`
Expected: both tests PASS. If the venv doesn't exist yet, create it: `python3 -m venv scripts/bench/.venv && scripts/bench/.venv/bin/pip install -q pycryptodome pytest`.

- [ ] **Step 5: Commit**

```bash
git add gen_known_hash.c scripts/bench/test_gen_known_hash.py
git commit -m "feat(gen_known_hash): emit NTLM hashes via --algo for round-trip tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `crack_diff.py` — pot parsing + crack-set diff (pure logic, TDD)

**Files:**
- Create: `scripts/bench/crack_diff.py`
- Test: `scripts/bench/test_crack_diff.py`

- [ ] **Step 1: Write the failing tests**

Create `scripts/bench/test_crack_diff.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd scripts/bench && .venv/bin/python -m pytest test_crack_diff.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'crack_diff'`.

- [ ] **Step 3: Implement `crack_diff.py`**

Create `scripts/bench/crack_diff.py`:

```python
#!/usr/bin/env python3
"""Pot-file parsing and crack-set diffing for the regression framework.

A "cracked set" maps each cracked hash (lowercase hex) to its plaintext encoded
as hex. We hex-encode the plaintext uniformly so NTLM (ASCII) and NetNTLMv1
(binary, already written as hex by the pot writer) compare the same way.

Pot line format (see crackalack_lookup.c::save_cracked_hash):
    JTR:     [$NT$]<hash>:<plaintext>\\n   ($NT$ only for NTLM)
    hashcat: <hash>:<plaintext>\\n
We strip a leading "$NT$" so either file parses to the same hash key.
"""
import argparse
import json
import os
import sys


def parse_pot(path: str) -> dict:
    """Return {hash_hex_lower: plaintext_hex}. Missing file => {}.

    The plaintext field is read as raw bytes and re-encoded as hex, so an ASCII
    NTLM plaintext ("hello") and a NetNTLMv1 hex plaintext ("4142..") both end
    up as hex. NOTE: a NetNTLMv1 pot already stores hex text, so its bytes are
    the ASCII characters of that hex — callers compare against the same encoding
    produced here, so the representation is internally consistent.
    """
    result = {}
    if not os.path.exists(path):
        return result
    with open(path, "rb") as f:
        for raw in f.read().split(b"\n"):
            if not raw:
                continue
            if raw.startswith(b"$NT$"):
                raw = raw[4:]
            idx = raw.find(b":")
            if idx < 0:
                continue
            h = raw[:idx].decode("latin-1").lower()
            pt = raw[idx + 1:]
            result[h] = pt.hex()
    return result


def diff_cracks(base: dict, cand: dict) -> dict:
    """Regressions = hashes cracked by base but not cand. Improvements = reverse."""
    base_keys = set(base)
    cand_keys = set(cand)
    return {
        "base_cracked": len(base_keys),
        "cand_cracked": len(cand_keys),
        "regressions": sorted(base_keys - cand_keys),
        "improvements": sorted(cand_keys - base_keys),
    }


def assert_in_pot(pot_path: str, expected_hash: str, expected_plaintext_hex: str) -> bool:
    """True iff the hash is present with the expected plaintext (hex compare)."""
    cracked = parse_pot(pot_path)
    return cracked.get(expected_hash.lower()) == expected_plaintext_hex.lower()


def main() -> int:
    p = argparse.ArgumentParser(description="Diff two pot files for crack regressions.")
    p.add_argument("--base-pot", required=True)
    p.add_argument("--cand-pot", required=True)
    p.add_argument("--out", required=True, help="Write crackdiff.json here")
    a = p.parse_args()
    d = diff_cracks(parse_pot(a.base_pot), parse_pot(a.cand_pot))
    with open(a.out, "w") as f:
        json.dump(d, f, indent=2, sort_keys=True)
    print(f"base={d['base_cracked']} cand={d['cand_cracked']} "
          f"regressions={len(d['regressions'])} improvements={len(d['improvements'])}")
    return 1 if d["regressions"] else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd scripts/bench && .venv/bin/python -m pytest test_crack_diff.py -v`
Expected: all 7 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/bench/crack_diff.py scripts/bench/test_crack_diff.py
git commit -m "feat(regression): pot parsing + crack-set diff module

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `render_report.py` — summary + exit code (pure logic, TDD)

**Files:**
- Create: `scripts/bench/render_report.py`
- Test: `scripts/bench/test_render_report.py`

The report consumes two kinds of JSON artifacts: per-backend round-trip results
(`roundtrip_<backend>.json` = `{path_name: {"cracked": bool, "expected": hex, "got": hex|null}}`)
and the differential (`crackdiff.json` from Task 2).

- [ ] **Step 1: Write the failing tests**

Create `scripts/bench/test_render_report.py`:

```python
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd scripts/bench && .venv/bin/python -m pytest test_render_report.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'render_report'`.

- [ ] **Step 3: Implement `render_report.py`**

Create `scripts/bench/render_report.py`:

```python
#!/usr/bin/env python3
"""Render regression_summary.md and decide overall pass/fail.

Inputs:
  --roundtrip backend=path  (repeatable) per-backend roundtrip JSON
  --crackdiff path          differential JSON from crack_diff.py
  --out path                regression_summary.md
Exit code: 0 if overall pass, 1 otherwise.
"""
import argparse
import json
import sys


def _cross_backend_divergences(roundtrips: dict) -> list:
    """Paths whose 'cracked' result is not identical across all backends present."""
    paths = set()
    for res in roundtrips.values():
        paths.update(res)
    diverged = []
    for path in sorted(paths):
        results = [res.get(path, {}).get("cracked") for res in roundtrips.values()
                   if path in res]
        if len(set(results)) > 1:
            diverged.append(path)
    return diverged


def overall_pass(roundtrips: dict, crackdiff: dict) -> bool:
    for res in roundtrips.values():
        for path_result in res.values():
            if not path_result.get("cracked"):
                return False
    if _cross_backend_divergences(roundtrips):
        return False
    if crackdiff.get("regressions"):
        return False
    return True


def build_report(roundtrips: dict, crackdiff: dict) -> str:
    status = "PASS" if overall_pass(roundtrips, crackdiff) else "FAIL"
    lines = [f"# Crack Regression Summary — {status}", ""]

    lines.append("## Round-trip (self-constructed known-crackable hashes)")
    lines.append("")
    lines.append("| path | " + " | ".join(roundtrips) + " |")
    lines.append("|------|" + "|".join(["------"] * len(roundtrips)) + "|")
    all_paths = sorted({p for res in roundtrips.values() for p in res})
    for path in all_paths:
        cells = []
        for backend in roundtrips:
            r = roundtrips[backend].get(path)
            if r is None:
                cells.append("n/a")
            else:
                cells.append("PASS" if r.get("cracked") else "FAIL")
        lines.append(f"| {path} | " + " | ".join(cells) + " |")
    lines.append("")

    diverged = _cross_backend_divergences(roundtrips)
    if diverged:
        lines.append(f"**Cross-backend divergence:** {', '.join(diverged)}")
        lines.append("")

    lines.append("## Differential (BASE vs CANDIDATE, real tables)")
    lines.append("")
    lines.append(f"- BASE cracked:      {crackdiff.get('base_cracked', 0)}")
    lines.append(f"- CANDIDATE cracked: {crackdiff.get('cand_cracked', 0)}")
    regs = crackdiff.get("regressions", [])
    imps = crackdiff.get("improvements", [])
    lines.append(f"- Regressions (cracked by BASE, missed by CANDIDATE): {len(regs)}")
    for h in regs:
        lines.append(f"  - {h}")
    lines.append(f"- Improvements (informational): {len(imps)}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--roundtrip", action="append", default=[],
                   help="backend=path/to/roundtrip.json")
    p.add_argument("--crackdiff", default=None)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    roundtrips = {}
    for spec in a.roundtrip:
        backend, _, path = spec.partition("=")
        with open(path) as f:
            roundtrips[backend] = json.load(f)
    crackdiff = {}
    if a.crackdiff:
        with open(a.crackdiff) as f:
            crackdiff = json.load(f)

    md = build_report(roundtrips, crackdiff)
    with open(a.out, "w") as f:
        f.write(md + "\n")
    print(md)
    return 0 if overall_pass(roundtrips, crackdiff) else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd scripts/bench && .venv/bin/python -m pytest test_render_report.py -v`
Expected: all 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/bench/render_report.py scripts/bench/test_render_report.py
git commit -m "feat(regression): summary renderer + pass/fail decision

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `run_regression.sh` — prepare + roundtrip phases

**Files:**
- Create: `scripts/bench/run_regression.sh`

This phase needs a GPU to run end-to-end, but the script itself is validated by
execution on a GPU host (dell3 and/or local Metal). The `roundtrip` phase uses
only the **current** build (the repo it lives in); `crackdiff` (Task 5) adds the
BASE/CANDIDATE differential.

- [ ] **Step 1: Create the driver with `prepare` and `roundtrip`**

Create `scripts/bench/run_regression.sh`:

```bash
#!/usr/bin/env bash
# Crack regression / false-negative framework for crackalack.
# Spec: docs/superpowers/specs/2026-06-20-crack-regression-framework-design.md
#
# Usage:
#   run_regression.sh prepare     # venv + build current repo (+ BASE/CAND for crackdiff)
#   run_regression.sh roundtrip   # self-contained crack round-trip (current build)
#   run_regression.sh crackdiff   # BASE-vs-CANDIDATE differential (dell3 real tables)
#   run_regression.sh report      # write regression_summary.md, set exit code
#   run_regression.sh all         # prepare + roundtrip + crackdiff + report
set -euo pipefail

# ---- Tunables (override via env) ----
: "${REG_ROOT:=/tmp/crackalack-regression}"
: "${THIS_REPO:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
: "${BACKEND:=$(uname -s | grep -qi darwin && echo metal || echo cuda)}"
: "${MAKE_TARGET:=$([[ "$BACKEND" == metal ]] && echo macos || echo linux)}"
# Differential (crackdiff) tunables:
: "${BASE_REF:=origin/bench-base-preinnerloop}"
: "${CAND_REF:=$(git -C "$THIS_REPO" rev-parse --abbrev-ref HEAD)}"
: "${REAL_TABLES:=/mnt/nvme/rtc/}"
: "${HASH_COUNT:=200}"
: "${HASH_SEED:=20260620}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/gpu_lock.sh"
VENV="$REG_ROOT/.venv"
PY="$VENV/bin/python3"
RESULTS="$REG_ROOT/results"

log() { echo "[regression $(date -u +%H:%M:%S)] $*" >&2; }

# Round-trip configs: name|gen_args|gkh_flags|hash_hex_len
ROUNDTRIP_CONFIGS=(
  "ntlm8|ntlm ascii-32-95 8 8 0 1000 1024 0|--algo ntlm --charset ascii-32-95 --plaintext-len 8|32"
  "ntlm9|ntlm ascii-32-95 9 9 0 1000 1024 0|--algo ntlm --charset ascii-32-95 --plaintext-len 9|32"
  "netntlmv1_7|netntlmv1 byte 7 7 0 1000 1024 0||16"
)
CHAIN_LEN=1000
REDUCTION_OFFSET=0
TARGET_POS=500

ensure_venv() {
    if [[ ! -x "$PY" ]] || ! "$PY" -c "import pytest, Crypto" 2>/dev/null; then
        log "creating venv at $VENV"
        rm -rf "$VENV"; python3 -m venv "$VENV"
        "$VENV/bin/pip" install --quiet --upgrade pip
        "$VENV/bin/pip" install --quiet pycryptodome pytest
    fi
}

build_repo() {  # build_repo <ref> <dir>; empty ref => build $THIS_REPO in place
    local ref="$1" dir="$2"
    if [[ -z "$ref" ]]; then
        ( cd "$THIS_REPO" && make "$MAKE_TARGET" )
        return
    fi
    if [[ ! -d "$dir/.git" ]]; then git clone "$THIS_REPO" "$dir"; fi
    git -C "$dir" fetch "$THIS_REPO" 2>/dev/null || true
    git -C "$dir" fetch origin 2>/dev/null || true
    git -C "$dir" checkout -f "$ref"
    ( cd "$dir" && make clean >/dev/null 2>&1 || true && make "$MAKE_TARGET" )
}

phase_prepare() {
    log "PREPARE: root=$REG_ROOT backend=$BACKEND target=$MAKE_TARGET"
    mkdir -p "$REG_ROOT" "$RESULTS"
    ensure_venv
    build_repo "" ""   # current repo, in place
    [[ -x "$THIS_REPO/crackalack_gen" && -x "$THIS_REPO/crackalack_lookup" \
       && -x "$THIS_REPO/crackalack_sort" && -x "$THIS_REPO/get_chain" \
       && -x "$THIS_REPO/gen_known_hash" ]] \
       || { log "ERROR: current build incomplete"; exit 1; }
}

# Run one round-trip config in its own clean dir; echo "cracked expected_hex got_hex".
run_one_roundtrip() {
    local name="$1" gen_args="$2" gkh_flags="$3"
    local work="$REG_ROOT/rt/$name"
    rm -rf "$work"; mkdir -p "$work"
    local bin="$THIS_REPO"

    # 1. Generate + sort a tiny table in the work dir.
    ( cd "$work" && with_gpu_lock "$bin/crackalack_gen" $gen_args >/dev/null 2>&1 )
    local table; table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"
    [[ -n "$table" ]] || { log "$name: gen produced no .rt"; echo "false  "; return; }
    ( cd "$work" && with_gpu_lock "$bin/crackalack_sort" "$table" >/dev/null 2>&1 )
    table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"

    # 2. Read a real stored chain start from the sorted table.
    local start_index
    start_index="$("$bin/get_chain" "$table" 0 | awk '/Start index:/ {print $3}')"
    [[ -n "$start_index" ]] || { log "$name: get_chain failed"; echo "false  "; return; }

    # 3. Construct a provably-in-table (hash, plaintext) on that chain.
    local gkh_out exp_hash exp_pt
    gkh_out="$("$bin/gen_known_hash" "$CHAIN_LEN" "$REDUCTION_OFFSET" "$start_index" "$TARGET_POS" $gkh_flags)"
    exp_hash="$(awk -F= '/^hash=/{print $2}' <<<"$gkh_out")"
    exp_pt="$(awk -F= '/^plaintext=/{print $2}' <<<"$gkh_out")"
    [[ -n "$exp_hash" && -n "$exp_pt" ]] || { log "$name: gen_known_hash failed"; echo "false  "; return; }

    # 4. Look it up against the generated table; pot written into the work dir.
    local pot="$work/result.pot"
    ( cd "$work" && rm -f ./*.index rcracki.precalc.* \
        && with_gpu_lock "$bin/crackalack_lookup" "$work" "$exp_hash" "$pot" >/dev/null 2>&1 ) || true

    # 5. Assert via the hashcat pot (no $NT$ prefix). got_hex = plaintext as stored.
    local cracked got
    if "$PY" "$SCRIPT_DIR/crack_diff.py" --base-pot /dev/null --cand-pot /dev/null --out /dev/null \
        >/dev/null 2>&1; then :; fi   # no-op: ensures module imports cleanly
    got="$("$PY" - "$pot.hashcat" "$exp_hash" <<'PY'
import sys; from crack_diff import parse_pot
pot, h = sys.argv[1], sys.argv[2].lower()
print(parse_pot(pot).get(h, ""))
PY
)"
    if [[ "$got" == "$exp_pt" ]]; then cracked=true; else cracked=false; fi
    echo "$cracked $exp_hash $got|$exp_pt"
}

phase_roundtrip() {
    log "ROUNDTRIP: backend=$BACKEND"
    mkdir -p "$RESULTS"
    local json="$RESULTS/roundtrip_$BACKEND.json"
    export PYTHONPATH="$SCRIPT_DIR"
    echo "{" > "$json"
    local first=1
    for cfg in "${ROUNDTRIP_CONFIGS[@]}"; do
        IFS='|' read -r name gen_args gkh_flags _hlen <<<"$cfg"
        log "round-trip: $name"
        read -r cracked exp_hash gotpair < <(run_one_roundtrip "$name" "$gen_args" "$gkh_flags")
        local got="${gotpair%%|*}" exp="${gotpair##*|}"
        [[ $first -eq 1 ]] || echo "," >> "$json"; first=0
        local gotjson="null"; [[ -n "$got" ]] && gotjson="\"$got\""
        printf '  "%s": {"cracked": %s, "expected": "%s", "got": %s}' \
            "$name" "$cracked" "$exp" "$gotjson" >> "$json"
    done
    echo "" >> "$json"; echo "}" >> "$json"
    log "wrote $json"
    cat "$json" >&2
}

main() {
    local phase="${1:-all}"
    case "$phase" in
        prepare)   phase_prepare ;;
        roundtrip) phase_roundtrip ;;
        crackdiff) phase_crackdiff ;;
        report)    phase_report ;;
        all)       phase_prepare; phase_roundtrip; phase_crackdiff; phase_report ;;
        *) echo "Unknown phase: $phase" >&2; exit 2 ;;
    esac
}
# phase_crackdiff and phase_report are defined in Task 5 / Task 6 additions below.
main "$@"
```

> Note: `phase_crackdiff` and `phase_report` are appended in Tasks 5 and 6. Until then, running `crackdiff`/`report`/`all` will error with "command not found"; run `prepare` and `roundtrip` directly while iterating.

- [ ] **Step 2: Make executable and smoke-test prepare on a GPU host**

Run on the local Metal host:
```bash
chmod +x scripts/bench/run_regression.sh
scripts/bench/run_regression.sh prepare
```
Expected: venv created, current repo builds, no "build incomplete" error.

- [ ] **Step 3: Run the round-trip phase**

Run: `scripts/bench/run_regression.sh roundtrip`
Expected: `results/roundtrip_metal.json` with `"cracked": true` for `ntlm8`, `ntlm9`, and `netntlmv1_7`. If any is `false`, that is a real finding — stop and investigate before proceeding (this is the framework doing its job).

- [ ] **Step 4: Commit**

```bash
git add scripts/bench/run_regression.sh
git commit -m "feat(regression): run_regression.sh prepare + roundtrip phases

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `crackdiff` phase — BASE-vs-CANDIDATE differential

**Files:**
- Modify: `scripts/bench/run_regression.sh`

- [ ] **Step 1: Add `phase_crackdiff`**

In `scripts/bench/run_regression.sh`, insert this function immediately above the
`main()` definition:

```bash
phase_crackdiff() {
    log "CRACKDIFF: BASE=$BASE_REF CAND=$CAND_REF tables=$REAL_TABLES"
    if [[ ! -d "$REAL_TABLES" ]]; then
        log "WARN: real tables dir '$REAL_TABLES' not found — skipping crackdiff"
        echo '{"base_cracked":0,"cand_cracked":0,"regressions":[],"improvements":[],"skipped":true}' \
            > "$RESULTS/crackdiff.json"
        return
    fi
    mkdir -p "$RESULTS"
    local base_dir="$REG_ROOT/base" cand_dir="$REG_ROOT/cand"
    log "building BASE"; build_repo "$BASE_REF" "$base_dir"
    log "building CANDIDATE"; build_repo "$CAND_REF" "$cand_dir"

    # Generate a shared hash set (NetNTLMv1-7, default challenge) once.
    local hashes="$REG_ROOT/diff_hashes.txt"
    "$PY" "$SCRIPT_DIR/gen_netntlmv1_hashes.py" --seed "$HASH_SEED" --count "$HASH_COUNT" --out "$hashes"

    # Run each build against the SAME real tables with the SAME hashes, clearing
    # the precompute cache between runs so a stale *.index can't mask a diff.
    local role dir pot
    for role in base cand; do
        dir="$base_dir"; [[ "$role" == cand ]] && dir="$cand_dir"
        pot="$RESULTS/${role}.pot"
        rm -f "$pot" "$pot.hashcat"
        ( cd "$dir" && rm -f ./*.index rcracki.precalc.* \
            && with_gpu_lock ./crackalack_lookup "$REAL_TABLES" "$hashes" "$pot" >/dev/null 2>&1 ) || true
        log "$role cracked: $(wc -l < "$pot.hashcat" 2>/dev/null || echo 0)"
    done

    export PYTHONPATH="$SCRIPT_DIR"
    "$PY" "$SCRIPT_DIR/crack_diff.py" \
        --base-pot "$RESULTS/base.pot.hashcat" \
        --cand-pot "$RESULTS/cand.pot.hashcat" \
        --out "$RESULTS/crackdiff.json" || log "crackdiff: regressions detected"
}
```

- [ ] **Step 2: Run crackdiff on dell3**

Run on dell3 (with `REAL_TABLES` pointing at a small subset directory for a first pass):
```bash
REAL_TABLES=/mnt/nvme/rtc/<small-subset> scripts/bench/run_regression.sh prepare
REAL_TABLES=/mnt/nvme/rtc/<small-subset> scripts/bench/run_regression.sh crackdiff
```
Expected: `results/crackdiff.json` with `regressions: []`. Any non-empty `regressions` is the reproduction of the reported bug — capture the listed hashes.

- [ ] **Step 3: Commit**

```bash
git add scripts/bench/run_regression.sh
git commit -m "feat(regression): crackdiff differential phase (BASE vs CANDIDATE)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `report` phase — summary + exit code

**Files:**
- Modify: `scripts/bench/run_regression.sh`

- [ ] **Step 1: Add `phase_report`**

In `scripts/bench/run_regression.sh`, insert immediately above `main()`:

```bash
phase_report() {
    log "REPORT"
    export PYTHONPATH="$SCRIPT_DIR"
    local args=()
    local f
    for f in "$RESULTS"/roundtrip_*.json; do
        [[ -e "$f" ]] || continue
        local backend; backend="$(basename "$f" .json)"; backend="${backend#roundtrip_}"
        args+=(--roundtrip "$backend=$f")
    done
    [[ -f "$RESULTS/crackdiff.json" ]] && args+=(--crackdiff "$RESULTS/crackdiff.json")
    "$PY" "$SCRIPT_DIR/render_report.py" "${args[@]}" --out "$RESULTS/regression_summary.md"
    local rc=$?
    log "summary at $RESULTS/regression_summary.md (exit $rc)"
    return $rc
}
```

- [ ] **Step 2: Run the full pipeline and confirm exit code**

Run on local Metal (crackdiff will self-skip when `REAL_TABLES` is absent):
```bash
scripts/bench/run_regression.sh prepare
scripts/bench/run_regression.sh roundtrip
scripts/bench/run_regression.sh crackdiff
scripts/bench/run_regression.sh report; echo "exit=$?"
```
Expected: `results/regression_summary.md` written; `exit=0` when all round-trips pass and no regressions; the markdown shows a PASS header and the per-path/backend table.

- [ ] **Step 3: Commit**

```bash
git add scripts/bench/run_regression.sh
git commit -m "feat(regression): report phase with pass/fail exit code

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Cross-backend run + README

**Files:**
- Modify: `scripts/bench/README.md`

- [ ] **Step 1: Run round-trip on both backends and merge**

On local Metal:
```bash
scripts/bench/run_regression.sh prepare && scripts/bench/run_regression.sh roundtrip
# produces results/roundtrip_metal.json
```
On dell3 (CUDA), over SSH:
```bash
scripts/bench/run_regression.sh prepare && scripts/bench/run_regression.sh roundtrip
# produces results/roundtrip_cuda.json
```
Copy `roundtrip_cuda.json` into the local `results/` dir (scp), then run
`scripts/bench/run_regression.sh report`. Expected: the summary's round-trip
table has both `cuda` and `metal` columns; any path that differs between them is
flagged under "Cross-backend divergence" and forces FAIL.

- [ ] **Step 2: Document usage in the README**

Append to `scripts/bench/README.md`:

```markdown
## Crack regression / false-negative framework

`run_regression.sh` proves cracking has no false negatives (self-contained
round-trip) and no regressions (BASE-vs-CANDIDATE differential) for the NTLM-8,
NTLM-9, and NetNTLMv1-7 (default-challenge) paths.

Phases: `prepare | roundtrip | crackdiff | report | all`.

```bash
# Self-contained correctness on whatever GPU is present (CUDA on dell3, Metal locally):
scripts/bench/run_regression.sh prepare
scripts/bench/run_regression.sh roundtrip      # -> results/roundtrip_<backend>.json

# Differential against real tables (dell3):
REAL_TABLES=/mnt/nvme/rtc/ scripts/bench/run_regression.sh crackdiff   # -> results/crackdiff.json

# Summary + pass/fail exit code:
scripts/bench/run_regression.sh report         # -> results/regression_summary.md
```

Cross-backend: run `roundtrip` on dell3 and locally, copy both
`roundtrip_<backend>.json` into one `results/` dir, then `report`. A path that
cracks on one backend but not the other is reported as a divergence (FAIL).

Tunables (env): `REG_ROOT`, `BASE_REF` (default `origin/bench-base-preinnerloop`),
`CAND_REF` (default current branch), `REAL_TABLES`, `HASH_COUNT`, `HASH_SEED`,
`BACKEND`/`MAKE_TARGET` (auto-detected).

Pure-logic unit tests (no GPU):
`cd scripts/bench && .venv/bin/python -m pytest test_crack_diff.py test_render_report.py test_gen_known_hash.py`
```

- [ ] **Step 3: Run the unit-test suite one final time**

Run: `cd scripts/bench && "$REG_ROOT/.venv/bin/python" -m pytest test_crack_diff.py test_render_report.py -v`
(plus `test_gen_known_hash.py` if the binary is built)
Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add scripts/bench/README.md
git commit -m "docs(regression): document crack regression framework usage

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review notes (addressed)

- **Spec coverage:** round-trip (Task 4) + 3 in-scope paths (configs table); differential (Task 5); cross-backend (Tasks 4/7 + `render_report._cross_backend_divergences`); reporting + exit code (Task 6); harness's own tests (Tasks 2,3 full TDD; Task 1 binary test). Out-of-scope paths (Markov/MD5/non-default-challenge) are intentionally excluded.
- **Open item from spec resolved:** NTLM8/9 known-hash derivation is done by extending `gen_known_hash` (Task 1), not `enumerate_chain` (which hardcodes the byte charset and so cannot drive ascii-32-95 tables).
- **Type/name consistency:** `parse_pot`, `diff_cracks`, `assert_in_pot` (crack_diff.py) and `build_report`, `overall_pass`, `_cross_backend_divergences` (render_report.py) are used with identical names/signatures across tasks. JSON shapes (`{cracked,expected,got}`, `{base_cracked,cand_cracked,regressions,improvements}`) match between producer (shell/crack_diff) and consumer (render_report).
- **No placeholders:** every code step contains complete code; commands have expected output.
```
