# MD5 Hash Support - Design

**Date:** 2026-03-08

## Overview

Add MD5 (unsalted) rainbow table generation and lookup to rainbowcrackalack. MD5 is frequently encountered in legacy database dumps and old web application authentication systems. It has no salt and is fully precomputable, making it an ideal rainbow table target.

The implementation mirrors the NTLM structure exactly: compile-time hash type specialization via `HASH_TYPE`, fast-path kernels for 8- and 9-character plaintexts, and Markov chain generation support.

## Architecture

### New constant (`shared.h`)

```c
#define HASH_MD5 3
```

### Key difference from NTLM

MD5 takes raw bytes directly. NTLM encodes plaintext as UTF-16LE before hashing. This simplifies `md5.cl` - no encoding step, the plaintext bytes are passed straight to the MD5 compression function.

Output is 16 bytes in both cases. `hash_to_index` reads the first 8 bytes as a little-endian `uint64_t` and is unchanged.

### New GPU kernel files

Each `.cl` file has a corresponding `.metal` file (mechanically translated).

| File | Purpose |
|------|---------|
| `CL/md5.cl` | MD5 hash function - raw bytes in, 16-byte hash out |
| `CL/crackalack_md5_8.cl` | Fast-path chain generation, 8-char plaintexts |
| `CL/crackalack_md5_9.cl` | Fast-path chain generation, 9-char plaintexts |
| `CL/precompute_md5_8.cl` | Fast-path lookup precompute, 8-char |
| `CL/precompute_md5_9.cl` | Fast-path lookup precompute, 9-char |
| `CL/false_alarm_check_md5_8.cl` | Fast-path false alarm check, 8-char |
| `CL/false_alarm_check_md5_9.cl` | Fast-path false alarm check, 9-char |
| `CL/crackalack_markov_md5.cl` | Markov chain generation |
| `CL/precompute_markov_md5.cl` | Markov precompute |
| `CL/false_alarm_check_markov_md5.cl` | Markov false alarm check |

Fast-path kernels hard-code the plaintext length and inline the padding structure. For 8-char ASCII, the MD5 message block is always 64 bytes with constant padding - the pad bytes and length field can be precomputed and inlined, eliminating the conditional padding logic from the inner loop.

### Modified host files

- `shared.h` - add `HASH_MD5 3`
- `cpu_rt_functions.c` - add `md5_hash()` CPU reference + dispatch case in `do_hash()`
- `crackalack_gen.c` - kernel selection for MD5 paths
- `crackalack_lookup.c` - kernel selection for MD5 paths
- `crackalack_unit_tests.c` - register new test functions
- `Makefile` - add new test object files
- `README.md` - document MD5 usage

## Data Flow

### Generation

Kernel selection in `crackalack_gen`:

```
hash_type == HASH_MD5:
    is_markov             → crackalack_markov_md5.cl
    plaintext_len_max == 8  → crackalack_md5_8.cl
    plaintext_len_max == 9  → crackalack_md5_9.cl
    else                  → crackalack.cl (compiled with HASH_TYPE=HASH_MD5)
```

### Lookup

Same dispatch logic for precompute and false alarm check kernels. False alarm check decodes the endpoint to plaintext via `index_to_plaintext` (or `index_to_plaintext_markov_cpu` for Markov mode), runs MD5, and compares against the target hash. Markov sentinel remains `UINT64_MAX`.

### CPU reference

```c
void md5_hash(char *plaintext, unsigned int plaintext_len, unsigned char *hash) {
    gcry_md_hash_buffer(GCRY_MD_MD5, hash, plaintext, plaintext_len);
}
```

Added to `do_hash()` dispatch:

```c
} else if (hash_type == HASH_MD5) {
    md5_hash(plaintext, plaintext_len, hash);
    *hash_len = 16;
}
```

## Error Handling

No new error conditions. MD5 plugs into all existing guards:

- Unknown hash type: existing exit path in `crackalack_gen`/`crackalack_lookup`
- Kernel load failure: existing GPU backend file-not-found error
- CPU chain verify mismatch: existing fatal error in `crackalack_gen` spot-check
- Markov model not trained: existing Markov guard, unchanged
- Input length: MD5 accepts any length up to `MAX_PLAINTEXT_LEN` (16); no truncation quirks

## Testing

### Unit tests

| File | Coverage |
|------|---------|
| `test_hash_md5.c` + `CL/test_hash_md5.cl` | Known vectors: `""` → `d41d8cd98f00b204e9800998ecf8427e`, `"abc"` → `900150983cd24fb0d6963f7d28e17f72`, `"password"` → `5f4dcc3b5aa765d61d8327deb882cf99` |
| `test_chain_md5_8.c` + `CL/test_chain_md5_8.cl` | GPU chain (8-char) endpoint matches CPU reference |
| `test_chain_md5_9.c` + `CL/test_chain_md5_9.cl` | GPU chain (9-char) endpoint matches CPU reference |

Markov MD5 cases added to `test_markov.c`: train on small wordlist, generate short chain, verify endpoint.

`hash_to_index` has no MD5-specific tests - it is hash-agnostic and already covered.

### End-to-end smoke test

```bash
./crackalack_gen md5 ascii-32-95 1 7 0 10000 1000 0
./crackalack_sort *.rt
./crackalack_lookup . <(echo "5f4dcc3b5aa765d61d8327deb882cf99")  # expects "password"
```
