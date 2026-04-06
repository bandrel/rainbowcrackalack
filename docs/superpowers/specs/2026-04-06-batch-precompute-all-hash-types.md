# Batch Precompute for All Hash Types

## Goal

Extend batch precompute (processing all hashes in a single GPU dispatch) from Markov NTLM8 only to all supported hash types. Eliminates sequential per-hash precompute bottleneck.

## Problem

`crackalack_lookup` precomputes candidate endpoints for each hash before searching tables. Currently, hashes are precomputed sequentially — one GPU dispatch per hash. With 1000 hashes at ~2 min each, precompute alone takes ~33 hours.

A batch precompute path exists for Markov NTLM8 (`precompute_markov_ntlm8_batch.cl`) that processes all hashes in a single dispatch. This needs to be extended to all hash types.

## Supported Hash Types

| Hash type | Kernel | Functions file | Chain len | Hash size |
|-----------|--------|---------------|-----------|-----------|
| NTLM 8-char | `precompute_ntlm8` | `ntlm8_functions.cl` | 422000 | 16 bytes |
| NTLM 9-char | `precompute_ntlm9` | `ntlm9_functions.cl` | 803000 | 16 bytes |
| NTLM 10-char | `precompute_ntlm10` | `ntlm10_functions.cl` | variable | 16 bytes |
| NetNTLMv1 7-char | `precompute_netntlmv1_7` | `netntlmv1_7_functions.cl` | 881689 | 8 bytes |
| MD5 8-char | `precompute_md5_8` | `md5_8_functions.cl` | variable | 16 bytes |
| MD5 9-char | `precompute_md5_9` | `md5_9_functions.cl` | variable | 16 bytes |
| Markov NTLM8 | `precompute_markov_ntlm8` | `markov8_functions.cl` | variable | 16 bytes |
| Markov NTLM9 | `precompute_markov_ntlm9` | `markov9_functions.cl` | variable | 16 bytes |
| Markov NTLM10 | `precompute_markov_ntlm10` | `markov10_functions.cl` | variable | 16 bytes |
| Generic | `precompute` | `rt.cl` | variable | variable |

Markov NTLM8 batch already exists. 9 new batch kernels needed (all non-Markov types + Markov NTLM9/10 + generic).

## Architecture

### Batch kernel pattern

Each batch kernel follows the existing `precompute_markov_ntlm8_batch.cl` pattern:

```
GWS = num_hashes * chunk_positions
hash_idx = gid / chunk_positions
local_pos = gid % chunk_positions
absolute_pos = pos_start + local_pos
target_chain_len = chain_len - absolute_pos - 1

index = hash_char_to_index_TYPE(hash[hash_idx], target_chain_len - 1)
for i in [target_chain_len, chain_len - 1):
    plaintext = index_to_plaintext_TYPE(index)
    hash = hash_TYPE(plaintext)
    index = hash_to_index_TYPE(hash, i)
output[hash_idx * total_positions + absolute_pos] = index
```

Key differences per type:
- **Hash size**: NetNTLMv1 uses 8-byte hashes, all others use 16 bytes. The hash buffer stride must match: `g_hashes + hash_idx * HASH_SIZE`.
- **Functions**: Each type includes its own `_functions.cl` with specialized `hash_char_to_index`, `index_to_plaintext`, `hash`, and `hash_to_index`.
- **Chain length**: Some types hardcode it (NTLM8=422000, NTLM9=803000, NetNTLMv1-7=881689), others read from a buffer.
- **Reduction offset**: Some types (NetNTLMv1-7) include `reduction_offset`/`table_index` in the hash_to_index calculation. For batch precompute, reduction_offset=0 is used during precompute (it's a property of the table being searched, applied during the table-processing phase, not precompute).
- **Markov types**: Need extra kernel args for `sorted_pos0` and `sorted_bigram` constant buffers.
- **Shared memory**: NetNTLMv1-7 batch kernel must use the `__local` S-box optimization from `netntlmv1_fast.cl`.

### Host-side changes

In `crackalack_lookup.c`, `batch_precompute_all_hashes()` (line 1554):

1. Remove the Markov NTLM8-only gate (lines 1559-1563).
2. Add hash type detection to select the correct batch kernel path and name.
3. Adjust hash buffer stride for NetNTLMv1 (8 bytes) vs others (16 bytes).
4. The existing VRAM check, chunking logic, and output buffer management remain unchanged.
5. The `num_hashes < 2` early-return (line 1564) remains — no point batching 1 hash.

### VRAM budget

Output buffer: `num_hashes * chain_len * 8 bytes`

| Scenario | Output buffer size | Fits in 12GB? |
|----------|-------------------|---------------|
| 1000 hashes × NTLM8 (422K) | 3.2 GB | Yes |
| 1000 hashes × NTLM9 (803K) | 6.1 GB | Yes |
| 1000 hashes × NetNTLMv1 (881K) | 6.6 GB | Yes |
| 2000 hashes × NTLM9 (803K) | 12.2 GB | Barely — falls back to sequential |

The existing VRAM check (line 1591: `output_bytes > gpu_mem / 2`) handles overflow gracefully by falling back to per-hash sequential precompute.

## Files to create

### OpenCL batch kernels (9 files)
- `CL/precompute_ntlm8_batch.cl`
- `CL/precompute_ntlm9_batch.cl`
- `CL/precompute_ntlm10_batch.cl`
- `CL/precompute_netntlmv1_7_batch.cl`
- `CL/precompute_md5_8_batch.cl`
- `CL/precompute_md5_9_batch.cl`
- `CL/precompute_markov_ntlm9_batch.cl`
- `CL/precompute_markov_ntlm10_batch.cl`
- `CL/precompute_batch.cl` (generic fallback)

### Metal batch kernels (9 files)
- `Metal/precompute_ntlm8_batch.metal`
- `Metal/precompute_ntlm9_batch.metal`
- `Metal/precompute_ntlm10_batch.metal`
- `Metal/precompute_netntlmv1_7_batch.metal`
- `Metal/precompute_md5_8_batch.metal`
- `Metal/precompute_md5_9_batch.metal`
- `Metal/precompute_markov_ntlm9_batch.metal`
- `Metal/precompute_markov_ntlm10_batch.metal`
- `Metal/precompute_batch.metal` (generic fallback)

### Host code modifications
- `crackalack_lookup.c` — extend `batch_precompute_all_hashes()` to dispatch all types

## Files to modify

- `crackalack_lookup.c` — `batch_precompute_all_hashes()` function (line 1554+)

## Testing

- Unit tests must still pass (`./crackalack_unit_tests`)
- Benchmark with 2 NetNTLMv1 hashes against 1 table on NVMe — batch should produce identical results to sequential
- Benchmark with multiple NTLM8 hashes if test hashes available
- Verify fallback to sequential when VRAM is insufficient

## Expected impact

| Hashes | Sequential | Batched | Speedup |
|--------|-----------|---------|---------|
| 2 (NetNTLMv1) | 4 min | ~2 min | 2× |
| 100 (NTLM8) | 3.3 hours | ~2-3 min | ~60-100× |
| 1000 (NTLM8) | 33 hours | ~5-10 min | ~200-400× |
