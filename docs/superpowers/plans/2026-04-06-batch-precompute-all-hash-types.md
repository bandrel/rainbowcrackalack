# Batch Precompute for All Hash Types - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend batch precompute (all hashes in a single GPU dispatch) from Markov NTLM8 only to all supported hash types, reducing precompute time from hours to minutes for multi-hash lookups.

**Architecture:** Create one batch precompute kernel per hash type, following the existing `precompute_markov_ntlm8_batch.cl` pattern. Each batch kernel includes its type's `_functions.cl` and implements the same `gid / chunk_positions` → `hash_idx` dispatch. The host-side `batch_precompute_all_hashes()` is extended with hash-type detection to select the correct batch kernel. All batch kernels share the same kernel arg layout (hashes buffer, num_hashes, chunk_positions, chain_len, pos_start, total_positions, output) with type-specific extras (Markov sorted tables, NetNTLMv1 table_index).

**Tech Stack:** C, OpenCL (`.cl` kernels), Metal (`.metal` shaders)

---

## File Structure

### New OpenCL batch kernels (9 files)

Each follows the `precompute_markov_ntlm8_batch.cl` pattern:

| File | Functions include | Hash size | Chain len | Extras |
|------|-----------------|-----------|-----------|--------|
| `CL/precompute_ntlm8_batch.cl` | `ntlm8_functions.cl` | 16 bytes | 422000 (hardcoded) | none |
| `CL/precompute_ntlm9_batch.cl` | `ntlm9_functions.cl` | 16 bytes | 803000 (hardcoded) | none |
| `CL/precompute_ntlm10_batch.cl` | `ntlm10_functions.cl` | 16 bytes | from buffer | none |
| `CL/precompute_netntlmv1_7_batch.cl` | `netntlmv1_7_functions.cl` | 8 bytes | 881689 (hardcoded) | `__local` S-boxes |
| `CL/precompute_md5_8_batch.cl` | `md5_8_functions.cl` | 16 bytes | from buffer | none |
| `CL/precompute_md5_9_batch.cl` | `md5_9_functions.cl` | 16 bytes | from buffer | none |
| `CL/precompute_markov_ntlm9_batch.cl` | `markov9_functions.cl` | 16 bytes | from buffer | sorted_pos0, sorted_bigram |
| `CL/precompute_markov_ntlm10_batch.cl` | `markov10_functions.cl` | 16 bytes | from buffer | sorted_pos0, sorted_bigram |
| `CL/precompute_batch.cl` | `rt.cl` | from buffer | from buffer | all generic params |

### New Metal batch kernels (9 files)

Mechanical translations of each OpenCL kernel above into `Metal/precompute_*_batch.metal`.

### Modified host code

- `crackalack_lookup.c` — extend `batch_precompute_all_hashes()` (line 1554)

---

## Task 1: Create NTLM8 batch precompute kernel

The simplest non-Markov batch kernel. Serves as the template for Tasks 2-5.

**Files:**
- Create: `CL/precompute_ntlm8_batch.cl`
- Create: `Metal/precompute_ntlm8_batch.metal`

- [ ] **Step 1: Write the OpenCL batch kernel**

Create `CL/precompute_ntlm8_batch.cl`. This is adapted from `CL/precompute_markov_ntlm8_batch.cl` with these changes:
- Include `ntlm8_functions.cl` instead of `markov8_functions.cl`
- Remove Markov-specific args (`g_sorted_pos0`, `g_sorted_bigram`, `g_charset_len`)
- Hardcode chain_len to 422000 (matching `precompute_ntlm8.cl`)
- Call `hash_char_to_index_ntlm8` / `index_to_plaintext_ntlm8` / `hash_ntlm8` / `hash_to_index_ntlm8`
- Hash size stride: `hash_idx * 16` (NTLM hash is 16 bytes)
- The `hash_char_to_index_ntlm8` function takes `(hash_value, pos)` — check `CL/ntlm8_functions.cl` for the exact signature and use it correctly

```c
#include "ntlm8_functions.cl"

__kernel void precompute_ntlm8_batch(
    __global unsigned char *g_hashes,
    __global unsigned int *g_num_hashes,
    __global unsigned int *g_chunk_positions,
    __global unsigned int *g_pos_start,
    __global unsigned int *g_total_positions,
    __global unsigned long *g_output) {

  unsigned int gid = get_global_id(0);
  unsigned int chunk_positions = *g_chunk_positions;
  unsigned int hash_idx = gid / chunk_positions;
  unsigned int local_pos = gid % chunk_positions;

  if (hash_idx >= *g_num_hashes)
    return;

  unsigned int absolute_pos = *g_pos_start + local_pos;
  unsigned int total_positions = *g_total_positions;

  if (absolute_pos >= total_positions)
    return;

  long target_chain_len = 422000L - (long)absolute_pos - 1;

  if (target_chain_len < 1) {
    g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = 0;
    return;
  }

  __global unsigned char *hash = g_hashes + hash_idx * 16;
  unsigned long index = hash_char_to_index_ntlm8(hash, (unsigned int)(target_chain_len - 1));

  unsigned char plaintext[8];
  for (unsigned long i = (unsigned long)target_chain_len; i < 421999; i++) {
    index_to_plaintext_ntlm8(index, plaintext);
    index = hash_to_index_ntlm8(hash_ntlm8(plaintext), (unsigned int)i);
  }

  g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = index;
}
```

**IMPORTANT:** Read `CL/ntlm8_functions.cl` to verify the exact function signatures. The `hash_char_to_index_ntlm8` function may take different parameters than the Markov version (no reduction_offset). Also verify the loop bound — `precompute_ntlm8.cl` uses `421999` as the upper bound (`chain_len - 1`). Match that exactly.

- [ ] **Step 2: Write the Metal batch kernel**

Create `Metal/precompute_ntlm8_batch.metal`. Mechanical translation:
- `__kernel` → `kernel`
- `__global` → `device`
- `get_global_id(0)` → `uint gid [[thread_position_in_grid]]`
- `unsigned long` → `ulong` (Metal `unsigned long` is 32-bit)
- `#include <metal_stdlib>` and `using namespace metal;`
- Buffer params get `[[buffer(N)]]` annotations

- [ ] **Step 3: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 4: Commit**

```bash
git add CL/precompute_ntlm8_batch.cl Metal/precompute_ntlm8_batch.metal
git commit -m "feat: add batch precompute kernel for NTLM8"
```

---

## Task 2: Create NTLM9 and NTLM10 batch precompute kernels

**Files:**
- Create: `CL/precompute_ntlm9_batch.cl`
- Create: `CL/precompute_ntlm10_batch.cl`
- Create: `Metal/precompute_ntlm9_batch.metal`
- Create: `Metal/precompute_ntlm10_batch.metal`

- [ ] **Step 1: Write NTLM9 batch kernel**

Same pattern as Task 1 NTLM8 but:
- Include `ntlm9_functions.cl`
- Hardcode chain_len to 803000, loop bound 802999
- Call `hash_char_to_index_ntlm9` / `index_to_plaintext_ntlm9` / `hash_ntlm9` / `hash_to_index_ntlm9`
- Read `CL/ntlm9_functions.cl` to verify function signatures — NTLM9 uses multiply-shift for hash_to_index, signatures may differ

- [ ] **Step 2: Write NTLM10 batch kernel**

Same pattern but:
- Include `ntlm10_functions.cl`
- Chain_len read from buffer (not hardcoded) — add `__global unsigned long *g_chain_len` arg
- Call `hash_char_to_index_ntlm10` / `index_to_plaintext_ntlm10` / `hash_ntlm10` / `hash_to_index_ntlm10`
- Read `CL/ntlm10_functions.cl` — NTLM10 has no modulo in hash_to_index (overflow is the modulo)
- Read `CL/precompute_ntlm10.cl` to see how chain_len is used from buffer

- [ ] **Step 3: Write Metal variants**

Translate both to Metal following the same rules as Task 1 Step 2.

- [ ] **Step 4: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 5: Commit**

```bash
git add CL/precompute_ntlm9_batch.cl CL/precompute_ntlm10_batch.cl Metal/precompute_ntlm9_batch.metal Metal/precompute_ntlm10_batch.metal
git commit -m "feat: add batch precompute kernels for NTLM9 and NTLM10"
```

---

## Task 3: Create NetNTLMv1-7 batch precompute kernel

This one is special: it must use the `__local` shared-memory S-box optimization from `netntlmv1_fast.cl`.

**Files:**
- Create: `CL/precompute_netntlmv1_7_batch.cl`
- Create: `Metal/precompute_netntlmv1_7_batch.metal`

- [ ] **Step 1: Write the OpenCL batch kernel**

Read `CL/precompute_netntlmv1_7.cl` to see how the shared-memory S-boxes are set up. The batch kernel must:
1. Declare `__local uint32_t l_SB1[64]` through `l_SB8[64]`
2. Call `LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0), l_SB1, ..., l_SB8)`
3. Call `barrier(CLK_LOCAL_MEM_FENCE)`
4. Use `hash_netntlmv1_7_fast(plaintext, l_SB1, ..., l_SB8)` instead of `hash_netntlmv1_7(plaintext)`

Also:
- Include `netntlmv1_7_functions.cl` (which includes `netntlmv1_fast.cl`)
- Hash size stride: `hash_idx * 8` (NetNTLMv1 hash is 8 bytes, not 16)
- Hardcode chain_len to 881689, loop bound 881688
- Read `CL/netntlmv1_7_functions.cl` to check if `hash_char_to_index_netntlmv1_7` takes a `reduction_offset` parameter. For batch precompute, pass 0.

```c
#include "netntlmv1_7_functions.cl"

__kernel void precompute_netntlmv1_7_batch(
    __global unsigned char *g_hashes,
    __global unsigned int *g_num_hashes,
    __global unsigned int *g_chunk_positions,
    __global unsigned int *g_pos_start,
    __global unsigned int *g_total_positions,
    __global unsigned long *g_output) {

  __local uint32_t l_SB1[64], l_SB2[64], l_SB3[64], l_SB4[64];
  __local uint32_t l_SB5[64], l_SB6[64], l_SB7[64], l_SB8[64];
  LOAD_LOCAL_SBOXES(get_local_id(0), get_local_size(0),
                     l_SB1, l_SB2, l_SB3, l_SB4,
                     l_SB5, l_SB6, l_SB7, l_SB8);

  unsigned int gid = get_global_id(0);
  unsigned int chunk_positions = *g_chunk_positions;
  unsigned int hash_idx = gid / chunk_positions;
  unsigned int local_pos = gid % chunk_positions;

  if (hash_idx >= *g_num_hashes)
    return;

  unsigned int absolute_pos = *g_pos_start + local_pos;
  unsigned int total_positions = *g_total_positions;

  if (absolute_pos >= total_positions)
    return;

  long target_chain_len = 881689L - (long)absolute_pos - 1;

  if (target_chain_len < 1) {
    g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = 0;
    return;
  }

  __global unsigned char *hash = g_hashes + hash_idx * 8;  /* 8-byte NetNTLMv1 hash */
  unsigned long index = hash_char_to_index_netntlmv1_7(hash, 0, (unsigned int)(target_chain_len - 1));

  unsigned char plaintext[8];
  for (unsigned long i = (unsigned long)target_chain_len; i < 881688; i++) {
    index_to_plaintext_netntlmv1_7(index, plaintext);
    index = hash_to_index_netntlmv1_7(hash_netntlmv1_7_fast(plaintext, l_SB1, l_SB2, l_SB3, l_SB4, l_SB5, l_SB6, l_SB7, l_SB8), (unsigned int)i);
  }

  g_output[(unsigned long)hash_idx * total_positions + absolute_pos] = index;
}
```

**IMPORTANT:** Read `CL/netntlmv1_7_functions.cl` carefully for the exact signatures of `hash_char_to_index_netntlmv1_7` (it may take a `reduction_offset` parameter — pass 0 for precompute) and `hash_netntlmv1_7_fast` (it takes 8 `__local` S-box pointers).

- [ ] **Step 2: Write Metal variant**

Same translation rules as Task 1, plus:
- `__local` → `threadgroup`
- `barrier(CLK_LOCAL_MEM_FENCE)` → `threadgroup_barrier(mem_flags::mem_threadgroup)`
- Add `uint lid [[thread_index_in_threadgroup]]` and `uint lsz [[threads_per_threadgroup]]` kernel params

- [ ] **Step 3: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 4: Commit**

```bash
git add CL/precompute_netntlmv1_7_batch.cl Metal/precompute_netntlmv1_7_batch.metal
git commit -m "feat: add batch precompute kernel for NetNTLMv1-7 with shared-memory DES"
```

---

## Task 4: Create MD5 8-char and 9-char batch precompute kernels

**Files:**
- Create: `CL/precompute_md5_8_batch.cl`
- Create: `CL/precompute_md5_9_batch.cl`
- Create: `Metal/precompute_md5_8_batch.metal`
- Create: `Metal/precompute_md5_9_batch.metal`

- [ ] **Step 1: Write MD5_8 batch kernel**

Same pattern as NTLM8 but:
- Include `md5_8_functions.cl`
- Read `CL/precompute_md5_8.cl` to find the chain_len — if it's hardcoded, use that value. If read from buffer, add `__global unsigned long *g_chain_len` arg.
- Call `hash_char_to_index_md5_8` / `index_to_plaintext_md5_8` / `hash_md5_8` / `hash_to_index_md5_8`
- Read `CL/md5_8_functions.cl` for exact signatures

- [ ] **Step 2: Write MD5_9 batch kernel**

Same pattern:
- Include `md5_9_functions.cl`
- Read `CL/precompute_md5_9.cl` for chain_len handling
- Call `hash_char_to_index_md5_9` / `index_to_plaintext_md5_9` / `hash_md5_9` / `hash_to_index_md5_9`

- [ ] **Step 3: Write Metal variants**

- [ ] **Step 4: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 5: Commit**

```bash
git add CL/precompute_md5_8_batch.cl CL/precompute_md5_9_batch.cl Metal/precompute_md5_8_batch.metal Metal/precompute_md5_9_batch.metal
git commit -m "feat: add batch precompute kernels for MD5_8 and MD5_9"
```

---

## Task 5: Create Markov NTLM9, NTLM10, and generic batch precompute kernels

**Files:**
- Create: `CL/precompute_markov_ntlm9_batch.cl`
- Create: `CL/precompute_markov_ntlm10_batch.cl`
- Create: `CL/precompute_batch.cl`
- Create: `Metal/precompute_markov_ntlm9_batch.metal`
- Create: `Metal/precompute_markov_ntlm10_batch.metal`
- Create: `Metal/precompute_batch.metal`

- [ ] **Step 1: Write Markov NTLM9 batch kernel**

Adapt from `CL/precompute_markov_ntlm8_batch.cl`:
- Include `markov9_functions.cl` instead of `markov8_functions.cl`
- Read `CL/precompute_markov_ntlm9.cl` for chain_len handling and function names
- Call `hash_char_to_index_markov9` / `index_to_plaintext_markov9` / `hash_ntlm9` / `hash_to_index_markov9`
- Keep `g_sorted_pos0` and `g_sorted_bigram` args (Markov tables)

- [ ] **Step 2: Write Markov NTLM10 batch kernel**

Same pattern with `markov10_functions.cl`. Read `CL/precompute_markov_ntlm10.cl`.

- [ ] **Step 3: Write generic batch kernel**

This handles any hash type not covered by optimized kernels. Read `CL/precompute.cl` for the generic path. The generic batch kernel:
- Includes `string.cl` and `rt.cl` (same as generic precompute)
- Needs all generic params: `g_hash_type`, `g_charset`, `g_charset_len`, `g_plaintext_len_min`, `g_plaintext_len_max`, `g_reduction_offset`, `g_chain_len`
- Calls generic `do_hash()`, `hash_to_index()`, `index_to_plaintext()`
- This is more complex — read `CL/precompute.cl` carefully and adapt its inner loop

- [ ] **Step 4: Write Metal variants for all three**

- [ ] **Step 5: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 6: Commit**

```bash
git add CL/precompute_markov_ntlm9_batch.cl CL/precompute_markov_ntlm10_batch.cl CL/precompute_batch.cl Metal/precompute_markov_ntlm9_batch.metal Metal/precompute_markov_ntlm10_batch.metal Metal/precompute_batch.metal
git commit -m "feat: add batch precompute kernels for Markov NTLM9/10 and generic"
```

---

## Task 6: Wire host-side batch dispatch for all hash types

This is the critical integration task. Extends `batch_precompute_all_hashes()` to detect the hash type and dispatch the correct batch kernel.

**Files:**
- Modify: `crackalack_lookup.c` (function `batch_precompute_all_hashes` at line 1554)

- [ ] **Step 1: Read the existing function**

Read `crackalack_lookup.c` lines 1554-1747. Understand:
- The Markov NTLM8-only gate (lines 1559-1563) — this gets removed
- The kernel loading (line 1617-1620) — path and name must change per type
- The kernel args setup (lines 1632-1645) — Markov-specific args (sorted_pos0, sorted_bigram) only apply to Markov types
- The hash buffer stride (line 1598: `num_hashes * 16`) — must be 8 for NetNTLMv1
- The hex_to_bytes call (line 1604: `hex_to_bytes(hashes[i], 16, ...)`) — hash hex length is 32 for 16-byte hashes, 16 for 8-byte hashes

- [ ] **Step 2: Add kernel path defines**

At the top of `crackalack_lookup.c`, near the existing `PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH` define, add:

```c
#define PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH "precompute_ntlm8_batch.cl"
#define PRECOMPUTE_NTLM9_BATCH_KERNEL_PATH "precompute_ntlm9_batch.cl"
#define PRECOMPUTE_NTLM10_BATCH_KERNEL_PATH "precompute_ntlm10_batch.cl"
#define PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH "precompute_netntlmv1_7_batch.cl"
#define PRECOMPUTE_MD5_8_BATCH_KERNEL_PATH "precompute_md5_8_batch.cl"
#define PRECOMPUTE_MD5_9_BATCH_KERNEL_PATH "precompute_md5_9_batch.cl"
#define PRECOMPUTE_MARKOV_NTLM9_BATCH_KERNEL_PATH "precompute_markov_ntlm9_batch.cl"
#define PRECOMPUTE_MARKOV_NTLM10_BATCH_KERNEL_PATH "precompute_markov_ntlm10_batch.cl"
#define PRECOMPUTE_BATCH_KERNEL_PATH "precompute_batch.cl"
```

Also add the `#ifdef USE_METAL` variants with `.metal` extension (check existing pattern for how this is done).

- [ ] **Step 3: Restructure `batch_precompute_all_hashes()`**

Replace the Markov NTLM8-only gate (lines 1559-1563) with hash type detection that selects `kernel_path`, `kernel_name`, `hash_byte_len`, and flags for which extra args are needed:

```c
char *kernel_path = NULL;
char *kernel_name = NULL;
unsigned int hash_byte_len = 16;  /* default for NTLM/MD5 */
int needs_markov_args = 0;
int needs_chain_len_arg = 0;

if (is_ntlm8(args[0].hash_type, args[0].charset, args[0].plaintext_len_min, args[0].plaintext_len_max)) {
    kernel_path = PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH;
    kernel_name = "precompute_ntlm8_batch";
} else if (is_ntlm9(args[0].hash_type, args[0].charset, args[0].plaintext_len_min, args[0].plaintext_len_max, args[0].reduction_offset, args[0].chain_len)) {
    kernel_path = PRECOMPUTE_NTLM9_BATCH_KERNEL_PATH;
    kernel_name = "precompute_ntlm9_batch";
} else if (is_ntlm10(args[0].hash_type, args[0].charset, args[0].plaintext_len_min, args[0].plaintext_len_max)) {
    kernel_path = PRECOMPUTE_NTLM10_BATCH_KERNEL_PATH;
    kernel_name = "precompute_ntlm10_batch";
    needs_chain_len_arg = 1;
} else if (is_netntlmv1_7(args[0].hash_type, args[0].charset_name, args[0].plaintext_len_min, args[0].plaintext_len_max, args[0].chain_len)) {
    kernel_path = PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH;
    kernel_name = "precompute_netntlmv1_7_batch";
    hash_byte_len = 8;
} else if (is_md5_8(args[0].hash_type, args[0].charset, args[0].plaintext_len_min, args[0].plaintext_len_max)) {
    kernel_path = PRECOMPUTE_MD5_8_BATCH_KERNEL_PATH;
    kernel_name = "precompute_md5_8_batch";
    needs_chain_len_arg = 1;
} else if (is_md5_9(args[0].hash_type, args[0].charset, args[0].plaintext_len_min, args[0].plaintext_len_max)) {
    kernel_path = PRECOMPUTE_MD5_9_BATCH_KERNEL_PATH;
    kernel_name = "precompute_md5_9_batch";
    needs_chain_len_arg = 1;
} else if (args[0].use_markov && is_markov_ntlm8(...)) {
    kernel_path = PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH;
    kernel_name = "precompute_markov_ntlm8_batch";
    needs_markov_args = 1;
} else if (args[0].use_markov && is_markov_ntlm9(...)) {
    kernel_path = PRECOMPUTE_MARKOV_NTLM9_BATCH_KERNEL_PATH;
    kernel_name = "precompute_markov_ntlm9_batch";
    needs_markov_args = 1;
} else if (args[0].use_markov && is_markov_ntlm10(...)) {
    kernel_path = PRECOMPUTE_MARKOV_NTLM10_BATCH_KERNEL_PATH;
    kernel_name = "precompute_markov_ntlm10_batch";
    needs_markov_args = 1;
    needs_chain_len_arg = 1;
} else {
    kernel_path = PRECOMPUTE_BATCH_KERNEL_PATH;
    kernel_name = "precompute_batch";
    needs_chain_len_arg = 1;
}

if (kernel_path == NULL)
    return 0;
```

**IMPORTANT:** Check the exact calling convention for each `is_*` function by reading `misc.c`. They take different parameters (`is_ntlm9` takes `reduction_offset` and `chain_len`, `is_netntlmv1_7` takes `charset_name` instead of `charset`, etc.).

- [ ] **Step 4: Update hash buffer allocation**

Change the hash buffer allocation to use `hash_byte_len`:

```c
unsigned int hash_hex_len = hash_byte_len * 2;
unsigned char *all_hashes_bin = calloc(num_hashes, hash_byte_len);
for (unsigned int i = 0; i < num_hashes; i++)
    hex_to_bytes(hashes[i], hash_hex_len, all_hashes_bin + i * hash_byte_len);
/* ... */
CLCREATEARG_ARRAY(0, hashes_buffer, CL_RO, all_hashes_bin, num_hashes * hash_byte_len);
```

- [ ] **Step 5: Conditionally set kernel args**

The base args (hashes, num_hashes, chunk_positions, pos_start, total_positions, output) are the same for all types. Extra args are conditional:

```c
/* Base args — same for all batch kernels */
CLCREATEARG_ARRAY(0, hashes_buffer, CL_RO, all_hashes_bin, num_hashes * hash_byte_len);
CLCREATEARG(1, num_hashes_buffer, CL_RO, num_hashes_uint, sizeof(gpu_uint));
CLCREATEARG(2, positions_buffer, CL_RO, positions_uint, sizeof(gpu_uint));
CLCREATEARG(3, pos_start_buffer, CL_RO, pos_start_val, sizeof(gpu_uint));
CLCREATEARG(4, total_positions_buffer, CL_RO, positions_uint, sizeof(gpu_uint));
CLCREATEARG_ARRAY(5, output_buffer, CL_WO, all_output, output_bytes);

/* Type-specific extras */
int next_arg = 6;
if (needs_chain_len_arg) {
    CLCREATEARG(next_arg, chain_len_buffer, CL_RO, chain_len_ulong, sizeof(gpu_ulong));
    next_arg++;
}
if (needs_markov_args) {
    CLCREATEARG_ARRAY(next_arg, sorted_pos0_buffer, CL_RO, args[0].sorted_pos0, ...);
    next_arg++;
    CLCREATEARG_ARRAY(next_arg, sorted_bigram_buffer, CL_RO, args[0].sorted_bigram, ...);
    next_arg++;
}
```

**CRITICAL:** The kernel arg indices must match the kernel's `__kernel void` parameter order EXACTLY. Each batch kernel has its own parameter order. The simplest approach is to keep all batch kernels using the SAME arg order: `(hashes, num_hashes, chunk_positions, pos_start, total_positions, output)` as args 0-5, with type-specific extras at args 6+. Design all new batch kernels in Tasks 1-5 with this consistent arg order.

- [ ] **Step 6: Build and verify**

```bash
make clean && make linux
./crackalack_unit_tests
```

- [ ] **Step 7: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat: extend batch precompute dispatch to all hash types"
```

---

## Task 7: Integration test on gpuhost3

Run an end-to-end test with 2 NetNTLMv1 hashes to verify batch precompute produces identical results to sequential.

**Files:** None (testing only)

- [ ] **Step 1: Deploy to gpuhost3**

```bash
scp CL/*batch*.cl crackalack_lookup.c gpuhost3:~/projects/rainbowcrackalack/
ssh gpuhost3 'cd ~/projects/rainbowcrackalack && make clean && make linux'
```

Also copy any new CL files created in Tasks 1-5.

- [ ] **Step 2: Run sequential baseline**

Temporarily disable batch precompute (add `return 0;` at the top of `batch_precompute_all_hashes`) and run:

```bash
echo -e "71A085CE65AF17EA\n734280679DC61744" > /mnt/nvme/bench_hash_2.txt
cd ~/projects/rainbowcrackalack
time ./crackalack_lookup /mnt/nvme/bench_1rt/ /mnt/nvme/bench_hash_2.txt
```

Record the precompute time and total time.

- [ ] **Step 3: Run batch precompute**

Re-enable batch precompute and run:

```bash
time ./crackalack_lookup /mnt/nvme/bench_1rt/ /mnt/nvme/bench_hash_2.txt
```

Verify:
- "Batched precompute" message appears in output
- Results are identical to sequential
- Precompute time is reduced

- [ ] **Step 4: Record results**

Compare sequential vs batch times. Expected: batch should be ~2× faster for 2 hashes (both processed in parallel instead of sequentially).

---

## Merge order

Tasks 1-5 (kernels) are independent and can be implemented in parallel. Task 6 (host wiring) depends on all kernels being committed. Task 7 (testing) depends on Task 6.

```
Tasks 1-5 (parallel) → Task 6 (host wiring) → Task 7 (integration test)
```
