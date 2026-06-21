# Few-Hash Bloom Auto-Skip + Decompress Alloc Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a few-hash lookup skip the per-table bloom-filter build automatically (a measured ~5.6× on 5 TB NetNTLMv1-7) without penalizing many-hash runs, and stop `rtc_decompress` from zeroing the ~536 MB output buffer it fully overwrites.

**Architecture:** Add a pure break-even helper to `bloom.c` (`bloom_is_worthwhile`, using a shared `bloom_optimal_k` so the build-cost estimate matches `bloom_create` exactly). In `crackalack_lookup.c`, compute one per-run decision in `streaming_lookup` (before the preloader thread spawns, using `total_hashes × filter->chain_len` as the query estimate and `filter->num_chains` as the representative table size) and gate the build in `load_single_table`. The bloom-less search path already exists (`--bloom-fpr 0` → `bf == NULL`, which `rt_binary_search` handles). In `rtc_decompress.c`, swap `calloc`→`malloc` with an overflow guard.

**Tech Stack:** C (libgcrypt + CUDA/Metal backends), `gcc`/`clang`, the existing `crackalack_lookup`/`crackalack_rtc2rt`/`crackalack_unit_tests` binaries.

**Spec:** `docs/superpowers/specs/2026-06-20-fewhash-bloom-autoskip-design.md`

> **Correction vs spec:** the spec proposed computing `g_total_queries` *after* precompute. Code reading shows the preloader thread (which builds blooms) is spawned *before* precompute in `streaming_lookup`. This plan therefore uses the pre-known estimate `total_hashes × filter->chain_len` (available before the preloader spawns) instead. Same break-even, correct ordering.

---

## File structure

| File | Change |
|------|--------|
| `bloom.h` | Declare `bloom_optimal_k`, `bloom_is_worthwhile` |
| `bloom.c` | Implement both; refactor `bloom_create`'s `k` to use `bloom_optimal_k` |
| `test_bloom.c` | Add `test_bloom_worthwhile()` + call it from `test_bloom()` |
| `rtc_decompress.c` | `calloc`→`malloc` + overflow guard for the output table |
| `crackalack_lookup.c` | `bloom_fpr_forced` + `g_build_bloom` globals; set forced in arg parse; compute decision in `streaming_lookup`; gate build in `load_single_table`; banner log |

---

## Task 1: `bloom_optimal_k` + `bloom_is_worthwhile` (pure logic, TDD)

**Files:**
- Modify: `bloom.h`, `bloom.c`, `test_bloom.c`

- [ ] **Step 1: Add the failing tests to `test_bloom.c`**

Insert this function just above `int test_bloom(void)` in `test_bloom.c`:

```c
/* Break-even decision: build the bloom only when it saves more search work
 * than it costs to build.  Few queries against a huge table => skip. */
static int test_bloom_worthwhile(void) {
  /* k must match bloom_create's power-of-two-rounded sizing (observed k=11
   * for ~33.5M elements at fpr 0.01). */
  if (bloom_optimal_k(33554432ULL, 0.01) != 11) {
    printf("bloom_optimal_k(33.5M, 0.01)=%u, expected 11\n",
           bloom_optimal_k(33554432ULL, 0.01));
    return 0;
  }
  /* 2 hashes x 881689 positions vs 33.5M-endpoint table => NOT worthwhile. */
  if (bloom_is_worthwhile(2ULL * 881689ULL, 33554432ULL, 0.01) != 0) {
    printf("worthwhile(2 hashes) should be 0\n"); return 0;
  }
  /* 100 hashes => worthwhile. */
  if (bloom_is_worthwhile(100ULL * 881689ULL, 33554432ULL, 0.01) != 1) {
    printf("worthwhile(100 hashes) should be 1\n"); return 0;
  }
  /* fpr<=0 (disabled) => never worthwhile. */
  if (bloom_is_worthwhile(100ULL * 881689ULL, 33554432ULL, 0.0) != 0) {
    printf("worthwhile(fpr=0) should be 0\n"); return 0;
  }
  /* num_chains==0 => never worthwhile (no table). */
  if (bloom_is_worthwhile(1000000ULL, 0ULL, 0.01) != 0) {
    printf("worthwhile(num_chains=0) should be 0\n"); return 0;
  }
  return 1;
}
```

Then, inside `int test_bloom(void)`, AND the new test into the result. Find the
existing aggregation (e.g. `ok &= test_bloom_roundtrip();` / `ok = ok && ...`)
and add a matching line:

```c
  ok = ok && test_bloom_worthwhile();
```
(Match whatever aggregation idiom `test_bloom()` already uses — read it first; if
it `return test_bloom_roundtrip() && test_bloom_counters();`, change to
`return test_bloom_roundtrip() && test_bloom_counters() && test_bloom_worthwhile();`.)

- [ ] **Step 2: Build a standalone runner and verify it FAILS to compile/link**

`test_bloom.c` is pure C (`#include`s only `stdio.h`, `stdint.h`, `bloom.h`,
`test_bloom.h`). Create a throwaway main and compile:

```bash
printf '#include "test_bloom.h"\n#include <stdio.h>\nint main(){int ok=test_bloom();printf("%s\\n",ok?"PASS":"FAIL");return ok?0:1;}\n' > /tmp/bmain.c
gcc -I. /tmp/bmain.c test_bloom.c bloom.c -lm -o /tmp/btest
```
Expected: **link error** — `undefined reference to bloom_optimal_k` and
`bloom_is_worthwhile` (functions not implemented yet).

- [ ] **Step 3: Declare the functions in `bloom.h`**

Add after the existing `bloom_create` declaration (near line 41):

```c
/* Optimal hash count k for a bloom over num_elements at target_fpr, computed
 * identically to bloom_create (k derived from the power-of-two-rounded bit
 * count).  Returns 0 when no bloom would be built (num_elements==0, target_fpr
 * outside (0,1), or the bit count would exceed bloom_create's cap). */
unsigned int bloom_optimal_k(uint64_t num_elements, double target_fpr);

/* Returns 1 if building a per-table bloom is expected to save more search work
 * than it costs to build, else 0.  num_queries = total precomputed endpoint
 * lookups across all uncracked hashes; num_chains = endpoints in the table;
 * target_fpr = configured FPR (<=0 always returns 0). */
int bloom_is_worthwhile(uint64_t num_queries, uint64_t num_chains, double target_fpr);
```

- [ ] **Step 4: Implement both in `bloom.c`, and refactor `bloom_create`'s k**

`bloom.c` already has `static inline uint64_t next_pow2(...)` (line 36) and
`#include <math.h>` (it uses `log`). Add these two functions *below* `next_pow2`
and *above* `bloom_create`:

```c
unsigned int bloom_optimal_k(uint64_t num_elements, double target_fpr) {
  if (num_elements == 0)                         return 0;
  if (target_fpr <= 0.0 || target_fpr >= 1.0)    return 0;
  const double LN2 = 0.6931471805599453;
  double m_raw = -((double)num_elements) * log(target_fpr) / (LN2 * LN2);
  if (m_raw < 64.0) m_raw = 64.0;
  uint64_t m_pow2 = next_pow2((uint64_t)m_raw);
  if (m_pow2 > (1ULL << 36))                     return 0;   /* matches bloom_create cap */
  double k_raw = ((double)m_pow2 / (double)num_elements) * LN2;
  int k = (int)(k_raw + 0.5);
  if (k < 1)  k = 1;
  if (k > 64) k = 64;
  return (unsigned int)k;
}

int bloom_is_worthwhile(uint64_t num_queries, uint64_t num_chains, double target_fpr) {
  unsigned int k = bloom_optimal_k(num_chains, target_fpr);
  if (k == 0 || num_chains == 0) return 0;
  double build = (double)num_chains * (double)k;
  double save  = (double)num_queries * log2((double)num_chains);
  return (save >= build) ? 1 : 0;
}
```

In `bloom_create`, replace its inline `k` computation (the block at lines ~70-74:
`/* k = round((m/n) * ln 2)... */` through the two clamps that set `int k`) with
a single call so the two paths can never drift:

```c
  /* k computed by the shared helper so bloom_is_worthwhile estimates the exact
   * same build cost. */
  int k = (int)bloom_optimal_k(num_elements, target_fpr);
  if (k < 1) k = 1;   /* defensive: bloom_create already validated inputs above */
```
Leave `bloom_create`'s own `m_pow2` allocation logic untouched (it still needs
`m_pow2` to size `bf->bits`). Do NOT remove the early `num_elements==0` /
`target_fpr` validation in `bloom_create`.

- [ ] **Step 5: Rebuild the standalone runner and verify it PASSES**

```bash
gcc -I. /tmp/bmain.c test_bloom.c bloom.c -lm -o /tmp/btest && /tmp/btest
```
Expected: `PASS` (exit 0). If `bloom_optimal_k(33.5M,0.01)` is not 11, STOP — the
refactor changed `bloom_create`'s sizing; re-check the formula matches the
original.

- [ ] **Step 6: Commit**

```bash
git add bloom.h bloom.c test_bloom.c
git commit -m "feat(bloom): bloom_optimal_k + bloom_is_worthwhile break-even helper

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `rtc_decompress` calloc→malloc (correctness-preserving)

**Files:**
- Modify: `rtc_decompress.c`

- [ ] **Step 1: Establish the byte-identical baseline (pre-change)**

`test_decompress.c` is the existing decompression unit test. Build a standalone
runner first (it `#include`s `rtc_decompress.h` + `test_decompress.h`; check its
includes with `head -15 test_decompress.c` — if it pulls only rtc_decompress/
shared headers, compile directly):

```bash
printf '#include "test_decompress.h"\n#include <stdio.h>\nint main(){int ok=test_decompress();printf("%s\\n",ok?"PASS":"FAIL");return ok?0:1;}\n' > /tmp/dmain.c
gcc -I. /tmp/dmain.c test_decompress.c rtc_decompress.c misc.c charset.c -lgcrypt -lm -o /tmp/dtest 2>/tmp/derr.txt || { echo "link needs more objs:"; cat /tmp/derr.txt | grep undefined | head; }
/tmp/dtest
```
Expected: `PASS`. (If link fails, add the objects naming the undefined symbols
from `/tmp/derr.txt` — likely a couple more `.c` files; this is the known-good
baseline the change must preserve.)

- [ ] **Step 2: Apply the malloc change**

In `rtc_decompress.c`, the output buffer is allocated (line ~65):
```c
uncompressed_table = calloc((size_t)num_chains, sizeof(uint64_t) * 2);
```
Replace with a `malloc` plus an overflow guard mirroring the existing
`chains_bytes` check:
```c
  size_t out_bytes = (size_t)num_chains * (sizeof(uint64_t) * 2);
  if (num_chains != 0 && out_bytes / (sizeof(uint64_t) * 2) != num_chains) {
    fprintf(stderr, "Error: uncompressed table size overflow (num_chains=%"PRIu64").\n", num_chains);
    ret = -7;
    goto done;
  }
  uncompressed_table = malloc(out_bytes);
```
The existing `if (uncompressed_table == NULL) { ... }` error check and the
`done:`/free path stay as-is. This is safe: the decode loop writes both `s` and
`e` for every chain (`table_ptr` advances `2*num_chains` times), fully populating
the buffer, so no slot is ever read uninitialized.

- [ ] **Step 3: Rebuild the standalone decompress test and verify it still PASSES**

```bash
gcc -I. /tmp/dmain.c test_decompress.c rtc_decompress.c misc.c charset.c -lgcrypt -lm -o /tmp/dtest
/tmp/dtest
```
Expected: `PASS` — decompression output unchanged (the test validates decompressed
values against expected). If it fails, the malloc change is wrong — STOP.

- [ ] **Step 4: Commit**

```bash
git add rtc_decompress.c
git commit -m "perf(rtc): malloc (not calloc) the decompress output buffer

The decode loop fully overwrites every entry, so zeroing ~536MB/table is wasted
memory-bandwidth on the load path. Adds an overflow guard for the output size.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Wire the auto-skip into `crackalack_lookup.c`

**Files:**
- Modify: `crackalack_lookup.c`

- [ ] **Step 1: Add the globals**

Near the `bloom_target_fpr` declaration (~line 339), add:

```c
/* Set to 1 when --bloom-fpr was given an explicit value > 0, which forces the
 * bloom on (skips the auto break-even decision). */
int bloom_fpr_forced = 0;

/* Per-run decision: 1 = build per-table blooms, 0 = skip them.  Computed once in
 * streaming_lookup() before the preloader thread spawns; read by
 * load_single_table().  Defaults to 1 so any path that doesn't set it keeps the
 * historical always-build behavior. */
int g_build_bloom = 1;
```

- [ ] **Step 2: Set `bloom_fpr_forced` in arg parsing**

In the `--bloom-fpr` handler (~line 3745), after `bloom_target_fpr = v;`, add:

```c
      bloom_target_fpr = v;
      bloom_fpr_forced = (v > 0.0);   /* explicit positive fpr forces the bloom on */
```

- [ ] **Step 3: Compute the decision in `streaming_lookup`, before the preloader spawns**

In `streaming_lookup()` (the function starting ~line 2624), locate the directory
walk that builds the path list and the subsequent
`pthread_create(&preloader_tid, NULL, streaming_preloading_thread, pargs)`.
**Before** that `pthread_create`, insert:

```c
  /* Decide once whether per-table blooms are worth building for this config
   * group.  Few queries (few hashes) against huge tables => the per-table bloom
   * build costs more than it saves, so skip it (the search handles bf==NULL).
   * Estimate queries as total_hashes * chain_len (known now; the actual
   * precompute runs after the preloader starts). */
  if (bloom_fpr_forced) {
    g_build_bloom = (bloom_target_fpr > 0.0);
  } else {
    uint64_t est_queries = (uint64_t)total_hashes * (uint64_t)filter->chain_len;
    g_build_bloom = (bloom_target_fpr > 0.0) &&
                    bloom_is_worthwhile(est_queries, filter->num_chains, bloom_target_fpr);
  }
  printf("Bloom filter: %s (fpr=%g, ~%u hashes vs %"PRIu64" chains/table).\n",
         g_build_bloom ? "enabled" :
           (bloom_target_fpr <= 0.0 ? "disabled (--bloom-fpr 0)" : "auto-skipped (too few hashes to amortize build)"),
         bloom_target_fpr, total_hashes, filter->num_chains);
  fflush(stdout);
```

(`total_hashes`, `filter`, and `bloom_is_worthwhile` are all in scope — `filter`
is `const rt_parameters *` with `.chain_len` and `.num_chains`; `bloom.h` is
already included via the lookup's includes. If `bloom.h` is not included in
`crackalack_lookup.c`, add `#include "bloom.h"` with the other includes.)

- [ ] **Step 4: Gate the build in `load_single_table`**

In `load_single_table()` replace the bloom build block (lines ~2376-2380):
```c
  pt->bf = bloom_create(num_chains, bloom_target_fpr);
  if (pt->bf != NULL) {
    for (uint64_t c = 0; c < num_chains; c++)
      bloom_insert(pt->bf, rainbow_table[(c * 2) + 1]);
  }
```
with:
```c
  if (g_build_bloom) {
    pt->bf = bloom_create(num_chains, bloom_target_fpr);
    if (pt->bf != NULL) {
      for (uint64_t c = 0; c < num_chains; c++)
        bloom_insert(pt->bf, rainbow_table[(c * 2) + 1]);
    }
  } else {
    pt->bf = NULL;   /* auto-skip: search treats NULL bf as "no pre-filter" */
  }
```
(`bloom_free(NULL)` is already a safe no-op — `bloom.c:139-144` guards `if (bf)`.)

- [ ] **Step 5: Build the whole tool and verify it compiles clean**

On this macOS host:
```bash
make macos 2>&1 | grep -iE "error|warning: .*bloom|warning: .*g_build" || echo "clean build (no bloom/g_build warnings)"
[ -x ./crackalack_lookup ] && echo "crackalack_lookup built"
```
Expected: builds, `crackalack_lookup` present, no new warnings referencing the
added symbols.

- [ ] **Step 6: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat(lookup): auto-skip per-table bloom when too few hashes to amortize

Computes a per-run break-even (total_hashes*chain_len vs num_chains*k) before the
preloader spawns; --bloom-fpr X (X>0) forces it on, --bloom-fpr 0 forces off.
~5.6x on few-hash 5TB NetNTLMv1-7 lookups; no false-negative risk (NULL bf path).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Integration validation on dell3 (GPU, real tables)

**Files:** none (validation only)

This task runs on dell3 (CUDA, RTX 3080 Ti) against the existing 40-table /
13 GB hardlink subset at `/mnt/nvme/bench_subset` (recreate it if absent — see the
note at the end). It needs the repo built at this branch on dell3. Do NOT disturb
any unrelated running jobs; use PID-scoped kills only.

- [ ] **Step 1: Get this branch onto dell3 and build**

```bash
ssh dell3 'cd ~/projects/rainbowcrackalack && git fetch <this-repo-or-remote> && git checkout perf/fewhash-bloom-autoskip && make linux 2>&1 | tail -2'
```
(If pushing to a shared remote isn't desired, `rsync` the four changed files —
`bloom.c bloom.h rtc_decompress.c crackalack_lookup.c` — into dell3's repo and
`make linux`.) Expected: builds, `crackalack_lookup` present.

- [ ] **Step 2: Confirm the unit tests pass on dell3**

```bash
ssh dell3 'cd ~/projects/rainbowcrackalack && gcc -I. -x c - test_bloom.c bloom.c -lm -o /tmp/btest <<<"#include \"test_bloom.h\"
int main(){return test_bloom()?0:1;}" && /tmp/btest && echo BLOOM_TESTS_PASS'
```
Expected: `BLOOM_TESTS_PASS`.

- [ ] **Step 3: Benchmark auto-skip vs forced-on (the core validation)**

```bash
ssh dell3 'bash -c "
cd ~/projects/rainbowcrackalack
SUB=/mnt/nvme/bench_subset; H=/mnt/nvme/rainbowcrackalack/14000
cat \$SUB/*/*.rtc >/dev/null 2>&1   # warm
run(){ local l=\$1; shift; rm -f /tmp/v.pot*; /usr/bin/time -f \"wall=%es user=%Us\" ./crackalack_lookup \$SUB \$H /tmp/v.pot \"\$@\" 2>/tmp/v_\$l.txt >/dev/null; echo \"[\$l \$*] \$(cat /tmp/v_\$l.txt)\"; grep -i \"Bloom filter:\" /tmp/v_\$l.txt; }
run auto
run forced --bloom-fpr 0.01
run off    --bloom-fpr 0
"'
```
Expected: `auto` ≈ `off` (~8 s, "auto-skipped") and ≈5× faster than `forced`
(~45 s, "enabled"). If `auto` is slow (~45 s), the decision/gate is wrong — STOP
and investigate.

- [ ] **Step 4: Confirm cracking correctness is unchanged**

Generate a known-in-table NetNTLMv1-7 hash (chain_len 881689 fast path) and confirm
it cracks identically with the bloom auto-skipped and forced on:

```bash
ssh dell3 'bash -c "
cd ~/projects/rainbowcrackalack
# build gen_known_hash (objects from make linux are present)
gcc build/linux/obj/gen_known_hash.o build/linux/obj/cpu_rt_functions.o build/linux/obj/charset.o build/linux/obj/markov.o -o /tmp/gkh -lgcrypt -lssl -lcrypto -lm 2>/dev/null || \
  { gcc -DUSE_CUDA -I/usr/local/cuda/include -O3 -c gen_known_hash.c -o build/linux/obj/gen_known_hash.o && gcc build/linux/obj/gen_known_hash.o build/linux/obj/cpu_rt_functions.o build/linux/obj/charset.o build/linux/obj/markov.o -o /tmp/gkh -lgcrypt -lssl -lcrypto -lm; }
rm -rf /tmp/v3; mkdir -p /tmp/v3
./crackalack_gen netntlmv1 byte 7 7 0 881689 256 0 >/dev/null 2>&1
mv netntlmv1_byte#7-7_0_881689x256_0.rt /tmp/v3/
./crackalack_sort /tmp/v3/*.rt >/dev/null 2>&1
S=\$(./get_chain /tmp/v3/*.rt 0 | awk \"/Start index:/{print \\\$3}\")
OUT=\$(/tmp/gkh 881689 0 \$S 440000); HASH=\$(echo \"\$OUT\"|awk -F= \"/^hash=/{print \\\$2}\")
rm -f /tmp/v3/a.pot* /tmp/v3/b.pot*
./crackalack_lookup /tmp/v3 \$HASH /tmp/v3/a.pot >/dev/null 2>&1               # auto (skips bloom, tiny table)
./crackalack_lookup /tmp/v3 \$HASH /tmp/v3/b.pot --bloom-fpr 0.01 >/dev/null 2>&1  # forced on
echo \"auto:   \$(cat /tmp/v3/a.pot.hashcat 2>/dev/null || echo MISS)\"
echo \"forced: \$(cat /tmp/v3/b.pot.hashcat 2>/dev/null || echo MISS)\"
rm -f netntlmv1_byte#7-7_0_881689x256_0.rt
"'
```
Expected: both print the **same** `<hash>:<plaintext>` line (a crack), proving the
auto-skip doesn't change results.

- [ ] **Step 5: Record results**

Append the measured `auto`/`forced`/`off` wall times to the spec doc (or note them
in the PR description). No commit of code in this task.

---

## Self-review notes (addressed)

- **Spec coverage:** Component A = Tasks 1 (helper) + 3 (wiring) + 4 (validation);
  Component B = Task 2; testing requirements = Task 1 unit tests, Task 2 byte-
  identical check, Task 4 integration + correctness. Banner/visibility = Task 3
  Step 3. Override semantics (`--bloom-fpr` forced/auto/off) = Task 3 Steps 2-3.
- **Spec deviation (documented):** decision uses pre-precompute estimate
  `total_hashes × chain_len` because the preloader spawns before precompute.
- **Type/name consistency:** `bloom_optimal_k` / `bloom_is_worthwhile` signatures
  match across bloom.h, bloom.c, test_bloom.c, and the Task 3 call site;
  `bloom_fpr_forced` / `g_build_bloom` used consistently in Task 3.
- **No placeholders:** every code step shows complete code; the optional
  memcpy/memset decode micro-opt from the spec is intentionally excluded (YAGNI;
  the malloc fix is the safe, sufficient B).
