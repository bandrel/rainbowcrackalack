# Design: mask+Markov combo

**Date:** 2026-07-13
**Branch:** `dev/mask-markov` (off `master`, which has both Markov and the mask/`.hcmask` features)
**Status:** Approved (design), pending implementation plan

## Motivation

Mask mode hard-restricts each position's charset (multiplicative keyspace cut for
structured passwords). Markov mode orders plaintexts by probability so a table
covering only the top-K (`--markov-keyspace`) catches most real passwords. They
are currently **mutually exclusive**. Combining them — hashcat's model — lets a
table cover only the **top-K most-probable plaintexts that also fit the mask**,
shrinking tables for structured passwords beyond what a uniform mask achieves.

In a rainbow table, Markov's payoff is keyspace **truncation** (ordering only
matters when you take the top-K). So the combo's value is realized when
`--markov-keyspace` truncates within the mask's restricted space.

### Scope (confirmed)

- Combined mode = `--mask ... --markov FILE [--markov-keyspace K]` on
  `crackalack_gen`, `gen_known_hash`, `crackalack_verify`; `crackalack_lookup`
  auto-detects the mask from the filename and takes `--markov FILE` (as Markov
  lookup already requires).
- **Generic kernel only** — one `crackalack_markov_mask` gen/precompute/false-
  alarm set across **CUDA, OpenCL, Metal** (9 kernel files). No hash-length
  fast-paths (`markov_mask_ntlm8/9/10`) in this cut; the generic path handles
  NTLM/MD5 at any length.
- **Approach:** precompute restricted sorted tables host-side; the kernel is a
  Markov-style bigram-conditional decode with per-position variable radix.
- Out of scope: `--increment`, NetNTLMv1 (masks already unsupported there),
  fast-path kernel variants.

## Dependency / assumption

Builds on the base Markov code already merged to `master` (model load, sorted
tables, `-mk` keyspace truncation, Markov lookup requiring `--markov`). The combo
inherits Markov's ordering approximation (per-position marginal ordering, same as
hashcat and the existing Markov kernels) and assumes the base Markov keyspace/
truncation is correct (CI-verified per prior Markov work). No change to base
Markov behavior.

## Architecture

A new **combined mode** selected when both `use_mask` and `use_markov` are set.
Host code precomputes *restricted* Markov sorted tables from `(markov_model,
Mask)`; a new generic kernel decodes indices to plaintexts using those tables
with per-position charset sizes. Keyspace = product of per-position restricted
sizes; `--markov-keyspace K` truncates to the top-K. `.rt` binary format
unchanged.

## Components

### 1. Host precompute (`markov.c` / `markov.h`)

Restricted-table struct (flat, GPU-ready):

```c
typedef struct {
    unsigned int charset_len;                 /* from the markov model */
    char         charset[256];                /* markov charset bytes (copied) */
    unsigned int mask_len;                    /* number of positions */
    unsigned int sizes[MAX_PLAINTEXT_LEN];    /* sz[i] = |mask[i]| (radix per pos) */
    unsigned int max_sz;                      /* max over sizes[] (row stride) */
    /* position 0: first sizes[0] entries are charset indices sorted by pos0 freq */
    uint8_t      r_pos0[256];
    /* positions 1..len-1: for [pos i][prev charset-index], sizes[i] charset
     * indices sorted by bigram_freq[i][prev][cur] desc.
     * layout: r_bigram[(i * charset_len + prev) * max_sz + k] */
    uint8_t     *r_bigram;                    /* malloc'd: mask_len*charset_len*max_sz */
} markov_mask_tables;

/* Build restricted tables from a markov model + parsed mask.
 * Requires every mask-position char to be present in the markov charset;
 * returns -1 (with a clear message naming the offending char/position) otherwise.
 * Returns 0 on success; caller frees out->r_bigram via markov_mask_tables_free(). */
int markov_build_restricted(const markov_model *mk, const Mask *mask,
                            markov_mask_tables *out);
void markov_mask_tables_free(markov_mask_tables *t);

/* Total keyspace = product(sizes[0..mask_len-1]) with uint64 overflow guard. */
uint64_t markov_mask_keyspace(const markov_mask_tables *t);
```

Build algorithm:
- Validate each `mask->positions[i].chars[0..size-1]` ∈ `mk->charset`; map each to
  its charset index. `sizes[i] = mask->positions[i].size`.
- `r_pos0`: the `sizes[0]` position-0 charset indices, sorted by `pos0_freq` desc
  (stable tie-break by charset index, matching `markov_build_sorted`).
- `r_bigram`: for each `i` in `1..mask_len-1`, for each `prev` in
  `0..charset_len-1`, take the `sizes[i]` allowed charset indices and sort them by
  `bigram_freq[i*charset_len*charset_len + prev*charset_len + cur]` desc (same
  tie-break). Rows for `prev` values never used at runtime are still filled
  (cheap, keeps indexing uniform).

CPU decode (for verify + gen_known_hash):

```c
/* Mixed-radix Markov-conditional decode restricted to the mask. */
void index_to_plaintext_markov_mask_cpu(const markov_mask_tables *t,
                                        uint64_t index,
                                        unsigned char *plaintext /* out */,
                                        unsigned int *plaintext_len /* out */);
```
- `i=0`: `ci = r_pos0[index % sizes[0]]; pt[0]=charset[ci]; prev=ci; index/=sizes[0]`
- `i≥1`: `ci = r_bigram[(i*charset_len+prev)*max_sz + index%sizes[i]]; pt[i]=charset[ci]; prev=ci; index/=sizes[i]`

`fill_plaintext_space_markov_mask` mirrors the existing markov fixed-length fill:
tiers below `mask_len` are 0, tier `mask_len` = keyspace (or the truncated
`--markov-keyspace` when smaller).

### 2. GPU kernels (CUDA / OpenCL / Metal)

New generic kernels (mechanically parallel across the three backends), mirroring
the existing `crackalack_markov` / `precompute_markov` / `false_alarm_check_markov`
structure:
- `rt_markov_mask.{cu,cl,metal}` — the `index_to_plaintext_markov_mask` device
  function (same decode as CPU).
- `crackalack_markov_mask` — chain generation.
- `precompute_markov_mask` — lookup precompute.
- `false_alarm_check_markov_mask` — false-alarm chain regen.

Kernel buffers (its own kernel, so no arg-slot collision with mask-only or
markov-only): `g_charset`, `g_r_pos0`, `g_r_bigram`, `g_sizes`, `g_mask_len`,
`g_charset_len`, plus the standard chain args. Host fills them from
`markov_mask_tables`.

### 3. Mode selection + filename (`crackalack_gen.c`, `misc.c`)

- Remove the `use_mask && use_markov` mutual-exclusion **only for the combined
  case**; keep guards for `--hcmask`+`--markov`, and validate: fixed length,
  `mask.length == plaintext_len`, mask ⊆ Markov charset (via
  `markov_build_restricted`), not NetNTLMv1.
- Filename charset field = mask encoding (`mask_encode_charset_field`) **then**
  the `-mk<keyspace>` suffix — same order gen already applies (mask block at
  ~line 1079, then challenge; add the `-mk` append to also run in combined mode).
  Example: `ntlm_%u%l%l%d-mk1000000#4-4_0_1000x512_0.rt`.
- `parse_rt_params` already strips `-mk` (→ `markov_keyspace`) **before** mask
  detection, so a combined filename yields `markov_keyspace>0` **and** `is_mask=1`
  with `mask` = the `%`-encoded field. No parser change expected; add a unit test
  to lock it in.

### 4. Lookup / verify / gen_known_hash

- **`crackalack_lookup`**: when `params->is_mask && params->markov_keyspace>0`,
  require `--markov <model>` (already the rule for Markov lookup), reconstruct the
  mask from the filename (`mask_decode_charset_field`), call
  `markov_build_restricted(model, mask, &tables)`, fill the combined GPU buffers,
  and select the `precompute_markov_mask` / `false_alarm_check_markov_mask`
  kernels. Precompute cache key must include mask + markov-keyspace + model id
  (extend the existing key; this prevents cross-mode/cross-model false negatives —
  the lesson from the challenge/markov cache-key bugs).
- **`crackalack_verify`**: combined table verified via the table-derived mask +
  `--markov model`; chain regen uses `index_to_plaintext_markov_mask_cpu`.
- **`gen_known_hash`**: `--mask ... --markov model` mints an in-table hash using
  the same restricted decode.

## Testing

### Unit (`test_markov_mask.c`, via `crackalack_cpu_tests`)
- `markov_build_restricted`: per-position sizes; `r_pos0` order == pos0_freq desc
  within mask[0]; a spot `r_bigram` row order == bigram desc within mask[i];
  keyspace == ∏ sizes.
- subset violation (mask char not in Markov charset) → error.
- `index_to_plaintext_markov_mask_cpu`: index 0 → most-probable in-mask plaintext;
  a mid-range index decodes to the expected mixed-radix result.
- `parse_rt_params` on a combined filename → `is_mask=1` and `markov_keyspace>0`
  and correct `mask`.

### GPU parity (`test_chain_markov_mask`, per backend)
CPU vs GPU chain/decode equivalence for a small mask+markov config (mirrors the
existing `test_chain_markov` / `test_chain_mask`).

### End-to-end (`scripts/bench/test_mask_markov_lookup.sh`)
With a real `.markov` model: `crackalack_gen --mask '?u?l?l?d' --markov model
[--markov-keyspace K]` → sort → `crackalack_verify` → `gen_known_hash --mask
--markov` mint → `crackalack_lookup --markov model` (mask from filename) positive
crack + negative control. Run on Metal, CUDA, and OpenCL (gpuhost3).

## Risk assessment

Moderate. New kernel across three backends, but it mirrors the existing Markov
kernel closely and all correctness lives in the host precompute (fully unit-
tested) + the CPU/GPU parity test. No `.rt`-format change. Main risks: (1) the
`r_bigram` indexing/stride must match exactly between host fill, CPU decode, and
all three GPU kernels — locked down by the parity test; (2) precompute cache-key
completeness — addressed by including mask+keyspace+model in the key.

## Estimated effort

~1 week: host precompute + CPU decode + keyspace/fill, 9 kernel files, mode
plumbing (gen/lookup/verify/gen_known_hash), cache-key extension, unit + parity +
e2e tests, and three-backend verification.
