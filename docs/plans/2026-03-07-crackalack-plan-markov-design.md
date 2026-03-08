# crackalack_plan + Markov Rainbow Tables Design

**Date:** 2026-03-07

## Goals

1. **Table estimation** - given full generation parameters, show file size and coverage % without generating anything
2. **Parameter recommendation** - given a mask and a target coverage %, suggest `chain_len` and `num_chains`
3. **Markov-biased generation** - train bigram statistics from a wordlist, use them to bias chain start points toward high-probability plaintexts

## New binary: `crackalack_plan`

CPU-only, no GPU initialization. Three subcommands:

```bash
# Estimate file size + coverage for given parameters
./crackalack_plan estimate ntlm '?l?d?d?d' 4 4 10000 1000000

# Suggest chain_len + num_chains for a target coverage %
./crackalack_plan recommend ntlm '?l?d?d?d' 4 4 50%

# Train bigram statistics from a wordlist, write .markov file
./crackalack_plan train rockyou.txt ntlm_rockyou.markov
```

### `estimate` output

```
=== Table estimate ===
Mask:         ?l?d?d?d
Keyspace:     260,000 plaintexts
Chains:       1,000,000
Chain length: 10,000
File size:    15.26 MB
Coverage:     ~97.3%  (approximate single-table estimate)
```

Coverage formula: `1 - (1 - min(chain_len, keyspace) / keyspace) ^ num_chains`

### `recommend` output

```
Mask:         ?l?d?d?d
Keyspace:     260,000 plaintexts
Target:       50% coverage

Recommended:
  Chain length:  510
  Chains:        500,000
  File size:     7.63 MB
  Coverage:      ~50.1%
```

Recommendation algorithm:
- Set `chain_len = round(sqrt(keyspace))` as the Hellman optimum
- Solve for `num_chains` using `num_chains = ceil(log(1 - target) / log(1 - chain_len / keyspace))`
- Clamp `chain_len` to keyspace if keyspace is small

### `train` - wordlist requirements

Recommended minimum: **1M passwords**. The bigram model has 95×95 = 9,025 transition parameters; ~1M training words provide reliable estimates for all common bigrams.

| Size | Quality |
|------|---------|
| < 100K | Poor - rare bigrams unreliable |
| 100K - 1M | Acceptable |
| 1M - 10M | Good |
| 10M+ | Excellent (diminishing returns above ~20M) |

rockyou.txt (14.3M) is the practical gold standard for general consumer passwords. **Quality matters more than size** - 1M real leaked passwords from the target environment beat 100M generic dictionary words.

Training filters out any word containing characters outside the target charset entirely (not partially).

## `.markov` file format

Binary file, little-endian:

```
[4 bytes]  magic "RCLM"
[4 bytes]  version = 1
[4 bytes]  charset_len (e.g. 95 for ascii-32-95)
[charset_len bytes]  the charset characters, in order

[charset_len × 4 bytes]  position-0 frequency counts (uint32)
[charset_len × charset_len × 4 bytes]  bigram counts[prev][next] (uint32)
```

At load time, raw counts are normalized to floating-point CDFs in memory. These CDFs are passed to the GPU as buffers.

## Changes to `crackalack_gen`

New flag: `--markov <file.markov>`

When present:
1. Load and validate the `.markov` file
2. Build CDF tables in memory (9,120 floats, ~36 KB - fits in GPU constant memory)
3. Upload CDF buffers to GPU
4. Use `index_to_plaintext_markov` kernel instead of standard `index_to_plaintext`
5. Generate chain start points in probability order (index 0 = most probable plaintext)

If `--markov` is combined with NTLM8/NTLM9 fast-path kernels, fall back to the generic Markov kernel with a warning (the fast-path kernels have hardcoded `index_to_plaintext`).

The filename of the generated table is unchanged - Markov usage is not encoded in the filename (it does not affect table format or lookup compatibility).

## How Markov biases the keyspace

`index_to_plaintext_markov(i)` maps:
- index 0 → most probable plaintext (per bigram statistics)
- index 1 → second most probable plaintext
- index N-1 → least probable plaintext

`hash_to_index` is **unchanged** - still maps uniformly to [0, N). The reduction function distributes across the full keyspace identically to standard tables.

Chain start points are generated as indices 0, 1, 2, ..., M-1. This means every chain's starting plaintext is among the M most probable plaintexts in the training corpus. With standard tables, those same M start points would be the M lexicographically first plaintexts (e.g. `aaaa`, `aaab`, ...) - almost all garbage for real-world cracking.

**Practical effect:** a 10% coverage Markov table covers 10% of the most probable password space rather than 10% of the alphabetical space - dramatically better real-world crack rates for common passwords.

## New GPU kernel files

- `CL/index_to_plaintext_markov.cl` / `Metal/index_to_plaintext_markov.metal`

**`index_to_plaintext_markov` algorithm:**
- Position 0: binary search `cdf_pos0[0..charset_len]` to decode first character
- Position i > 0: binary search `cdf_bigram[prev_char * charset_len .. +charset_len]` to decode next character

Both `.cl` and `.metal` variants are required. The `.metal` variant follows the existing Metal quirks (ulong for 64-bit, address space qualifiers on pointer params).

## Error handling

| Condition | Behavior |
|-----------|----------|
| `.markov` wrong magic/version | Clear error, exit 1 |
| Wordlist with zero valid training words | Error before writing file |
| `--markov` with NTLM8/NTLM9 fast path | Warn, fall back to generic Markov kernel |
| `crackalack_plan` unknown subcommand | Print usage, exit 1 |
| `recommend` with keyspace = 1 | Print trivial result (1 chain, 100% coverage) |

## Testing

### CPU-only tests (`test_markov.c`)
- CDF construction: known counts → verify output probabilities sum to 1.0 and ordering is correct
- `index_to_plaintext_markov` CPU reference: small synthetic bigram table, verify index 0 is the most probable plaintext
- Functional: train on a 3-word synthetic corpus, verify most common bigram lands at index 0
- `crackalack_plan estimate`: known-answer checks via command invocation
- `crackalack_plan recommend`: known-answer checks for small keyspaces where the math is hand-verifiable

### GPU tests
- Metal: run on local macOS Apple Silicon machine
- OpenCL: run via `ssh justin@192.168.0.36`
- Both: verify that Markov-generated table produces correct sorted output and that `crackalack_lookup` finds known plaintexts

## Files added/modified

| File | Change |
|------|--------|
| `crackalack_plan.c` | New binary: estimate, recommend, train subcommands |
| `markov.c` / `markov.h` | Shared: .markov load/save, CDF construction, CPU reference index_to_plaintext_markov |
| `CL/index_to_plaintext_markov.cl` | New OpenCL kernel |
| `Metal/index_to_plaintext_markov.metal` | New Metal shader |
| `crackalack_gen.c` | Add `--markov` flag parsing and kernel dispatch |
| `gpu_backend.h` | Any new buffer/kernel helpers needed |
| `test_markov.c` / `test_markov.h` | CPU-only unit tests |
| `crackalack_unit_tests.c` | Wire in `test_markov()` |
| `Makefile` | Add new build targets |
| `README.md` | Document `crackalack_plan` and `--markov` flag |
