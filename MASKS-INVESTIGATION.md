# Investigation: hashcat-style masks (+ Markov) for rainbow tables

Branch: `dev/masks`. Date: 2026-07-10.

## Question
hashcat masks cut brute-force keyspace by fixing a character class per position
(`?u?l?l?l?l?l?l?d` = 1 upper, 6 lower, 1 digit), and Markov ordering is applied on
top. Can we do the same here — masks to shrink the keyspace, Markov to order within
it?

## TL;DR
**Yes, and it's high value — arguably more than Markov alone.** A mask is a sibling
of the existing Markov mode: another pluggable `index_to_plaintext` + reduction over
a smaller keyspace. Crucially, masks and Markov are **complementary, not redundant**:
a mask *hard-restricts which characters are allowed per position* (multiplicative
keyspace cut); Markov *soft-orders the allowed characters by probability*. Together
they make **long, structured passwords table-able** — breaking the ~10-char wall
that kills full-charset tables.

A complete hashcat-style mask implementation already existed and was **removed** in
`d591180`. The commit message cited overlap with Markov, but per the author the real
reason was a deliberate **scope reduction to control variables** — isolating where
Markov's improvements came from without masks as a confound. So masks were shelved,
not rejected; re-introducing them alongside the now-validated Markov work is the
intended next step, and the hashcat model (this request) uses the two *together*.
The old code is recoverable from `d591180^` as a scaffold.

## Why masks matter here — keyspace math
Rainbow tables (even Markov) top out near 10 chars because keyspace grows 95x/char
(see docs `markov-efficacy-findings.md`: full 12-char = 5.4e23 => exabytes). A mask
collapses that:

| pattern | keyspace | ~storage (1x cover) |
|---|--:|--:|
| full 8-char (95^8) | 6.6e15 | 1.06 TB |
| full 12-char (95^12) | 5.4e23 | 86,000,000+ TB (impossible) |
| `?u?l?l?l?l?l?l?d` (8) | 8.0e10 | <0.01 TB |
| `?u?l x7 ?d?d?d?d` (12, Cap+7low+4dig) | 2.1e15 | 0.33 TB |
| `?u?l x9 ?d?d` (12, Cap+9low+2dig) | 1.4e16 | 2.26 TB |
| `?u?l x11 ?d` (14, Cap+12low+1dig) | 2.5e19 | ~3,970 TB |

**A 12-char masked keyspace is smaller than a full 8-char table.** Masks make
11-14 char *structured* passwords (corporate policies: Capital + lowercase + digits/
symbol) feasible — exactly where Markov-on-full-charset is hopeless. This directly
extends the practical frontier established in the length-sweep analysis.

Mask + Markov: order the (already small) masked keyspace by probability, so the
covered fraction hits the most likely passwords first — same win Markov gives today,
now on a keyspace that's orders of magnitude smaller.

## Design (mask mode = Markov sibling)
Add a `--mask <string>` mode parallel to `--markov`:

- **Parse** `?l ?u ?d ?s ?a` + custom `?1-?4` -> per-position charset arrays
  (`Mask{ MaskPosition{chars[],size} positions[]; length }`). Recoverable from
  `d591180^:mask_parse.c/h`, incl. `mask_keyspace()` (product of per-position sizes,
  with overflow guard) and filename encode/decode helpers.
- **Keyspace**: `fill_plaintext_space_mask()` = mixed-radix cumulative product of
  per-position charset sizes (sibling of `fill_plaintext_space_markov_keyspace`).
  Fits uint64 for realistic masks (see table; all <= ~2.5e19 < 2^64).
- **index_to_plaintext_mask** (host + CUDA/CL/Metal): mixed-radix decode where
  digit i selects from position i's charset (like the generic decoder but a
  different charset per position). Sibling of `index_to_plaintext_markov`.
- **Reduction** `hash_to_index`: unchanged — it already reduces mod
  `plaintext_space_total`; mask just supplies a different total + decoder.
- **Mask + Markov combo**: two paths — (a) mask-only (uniform within mask), (b)
  mask+markov (Markov-order within each position's masked charset). (b) is the
  hashcat-equivalent; (a) is a simpler first milestone.
- **Kernel data**: pass per-position charset buffers (flat `mask_data[len*MAXCS]` +
  `mask_lens[len]`) exactly like the markov `sorted_pos0`/`sorted_bigram` buffers
  (args 11-12 slot). Old kernels did this (`mask_to_gpu_buffers`).
- **Filename**: encode the mask in the charset field, e.g.
  `ntlm_?u?l?l?l?l?l?l?d#8-8_0_...rt` (with `?`->safe char via
  `mask_encode_for_filename`), parsed in `misc.c parse_rt_params` like the `-mk`
  suffix. `is_mask_string()` (contains `?`) selects mode.

## Integration points (mapped, current tree)
Mirror the Markov plumbing exactly:
- index/keyspace: `cpu_rt_functions.c` (`index_to_plaintext` :119, `hash_to_index`
  :96, `fill_plaintext_space_*` :59/:68); GPU `{CUDA,CL,Metal}/rt.* + rt_markov.*`.
- flags/threading: `crackalack_gen.c` (markov flag :229-231, kernel dispatch
  :423-445, kernel buffers :358/:651-654), `crackalack_lookup.c` (:200-205,
  :1157-1172, :1283), `gen_known_hash.c` (:95-102), `crackalack_verify.c` (:42).
- filename: `misc.c parse_rt_params` (:452-461 for `-mk`); struct `misc.h` (:56-71).
- charset: `charset.c validate_charset` (:43-56) — add `validate_mask`/`is_mask`.
- fast-path note: NTLM8/9 have hand-optimized kernels; a generic mask kernel is the
  simplest path (masks are structurally like the generic decoder). Optimized
  mask kernels can follow if needed.

## Effort
- Recover + modernize `mask_parse.c/h` from `d591180^` (~1 day; API already existed).
- Host: `index_to_plaintext_mask` + `fill_plaintext_space_mask` + flag threading in
  gen/lookup/verify/gen_known_hash (~2-3 days).
- GPU: `rt_mask.*` + wiring in gen/precompute/false_alarm kernels x3 backends. The
  generic (non-fast-path) route first; the old removed kernels are a reference
  (~3-5 days incl. CUDA/OpenCL/Metal + byte-identical parity check like Markov).
- Tests: resurrect `test_mask*`, add mask+markov end-to-end + a real-hash crack
  (~1-2 days). Regression + CI (~0.5 day).
- Total rough: ~2 weeks for mask-only; +~1 week for the mask+markov combo.
- All within uint64 (no 128-bit needed) for realistic masks.

## Open questions (for decision before building)
1. **Mask-only first, or go straight to mask+markov?** Mask-only is a faster, useful
   milestone; mask+markov is the full hashcat parity.
2. **Custom charsets** (`?1..?4`) — support now or later? (old impl had them.)
3. **Fast-path kernels** — acceptable to run masks on the generic (slower) kernel
   initially, or need NTLM8/9-class optimization from the start?
4. **Resurrect vs rewrite** — start from `d591180^` code (proven, but pre-dates
   later refactors) or reimplement against current architecture?
5. Priority vs other work, given Markov is now on master.

## References
- Old mask feature: `f7cac09`/`9db9e5a` "add hashcat-style mask support"; removed in
  `d591180`. Old files at `d591180^`: mask_parse.c/h, test_mask*, mask kernels.
- Length/keyspace context: docs repo `markov-efficacy-findings.md`, `scoping-128bit-index.md`.
