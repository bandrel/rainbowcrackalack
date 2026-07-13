# Rainbow Crackalack - Agent Guide

GPU-accelerated rainbow table generator and hash lookup tool. C with CUDA (Linux), OpenCL (Windows), and Metal (macOS Apple Silicon) GPU backends. Currently supports NTLM, MD5, and NetNTLMv1 (SHA-1 constant reserved but unimplemented). GPLv3.

## Build

Requires libgcrypt. GPU backend depends on platform.

```bash
# Linux (Ubuntu) - CUDA backend
apt install nvidia-cuda-toolkit libgcrypt20-dev
make clean; make linux

# macOS (Apple Silicon) - Metal backend
brew install libgcrypt
make clean; make macos

# Windows (cross-compile from Ubuntu) - OpenCL backend
apt install mingw-w64 opencl-headers libgcrypt-mingw-w64-dev
make clean; make windows
```

Note: `nvidia-cuda-toolkit` from the Ubuntu repo may lag behind the installed NVIDIA driver. If NVRTC version mismatches surface at runtime, install a newer toolkit from NVIDIA's apt repo (e.g., `cuda-toolkit-13-x`). Use `make linux CUDA_PATH=/opt/cuda` to point at a non-default install location.

`make clean` removes `build/` and all binaries/DLLs from the project root.

## Binaries

| Binary | Purpose |
|--------|---------|
| `crackalack_gen` | Generate rainbow tables |
| `crackalack_lookup` | Look up hashes against rainbow tables |
| `crackalack_verify` | Verify generated tables for correctness |
| `crackalack_unit_tests` | Run GPU-accelerated unit tests |
| `crackalack_rtc2rt` | Decompress .rtc tables to .rt format |
| `perfectify` | Remove duplicate endpoints from tables |
| `get_chain` | Extract a single chain from a table |
| `enumerate_chain` | Walk a chain and print each step |

### Usage examples

```bash
# Generate NTLM 9-char table (part 0)
./crackalack_gen ntlm ascii-32-95 9 9 0 803000 67108864 0

# Lookup hashes against 8-char tables
./crackalack_lookup /path/to/ntlm8_tables/ /path/to/hashes.txt
```

### Mask attacks

Hashcat-style masks restrict each position to a specific character class, letting you cover structured password spaces (e.g. `Capital + 6 lower + digit`) with a much smaller table than full-charset coverage.

**Built-in tokens:** `?l` (lowercase a-z), `?u` (uppercase A-Z), `?d` (digits 0-9), `?s` (special/printable non-alnum), `?a` (all printable ASCII), `?b` (all bytes 0x00-0xFF), `?h` (hex lowercase 0-9a-f), `?H` (hex uppercase 0-9A-F), `??` (literal `?`).

**Custom charsets:** Define up to four with `-1/-2/-3/-4 <chars>` (or `--custom-charset1..4`). Definitions may themselves contain tokens (e.g. `-1 ?d?l`) and `\xNN` hex escapes. Use the slots as `?1 ?2 ?3 ?4` in the mask.

**Constraints:**
- The mask is fixed-length: `min_len` and `max_len` args must equal the mask length.
- Supported for NTLM and MD5. Not supported for NetNTLMv1.
- `--mask` and `--markov` are mutually exclusive.
- `crackalack_verify` and `gen_known_hash` also accept `--mask` and `-1..-4`.

**Filenames are self-describing.** Custom charset definitions are encoded into the `.rt` filename as `!N-<hex>` blocks. `crackalack_lookup` reconstructs the mask and charsets from the filename automatically — no mask flags needed at lookup time.

```bash
# 8-char mask table: capital + 6 lowercase + digit
./crackalack_gen ntlm ascii-32-95 8 8 0 803000 67108864 0 --mask '?u?l?l?l?l?l?l?d'

# 4-char mask table with custom charset ?1 = 'abcxyz', mask = ?1?1?l?d
./crackalack_gen ntlm ascii-32-95 4 4 0 803000 67108864 0 --mask '?1?1?l?d' -1 abcxyz

# Lookup against mask tables — no mask flags needed (self-describing filenames)
./crackalack_lookup /path/to/mask_tables/ /path/to/hashes.txt
```

## Tests

Unit tests require a GPU (CUDA on Linux, OpenCL on Windows, Metal on macOS):

```bash
./crackalack_unit_tests
```

Tests cover: chain generation, NTLM hashing, hash-to-index reduction, and index-to-plaintext conversion. Separate test files exist for standard and NTLM9-optimized paths (e.g., `test_chain.c` vs `test_chain_ntlm9.c`).

## Architecture

### Three-phase pipeline

1. **Generation** (`crackalack_gen`) - GPU computes rainbow chains from start points using iterated hash + reduce cycles. Output: `.rt` binary table files.
2. **Table storage** - Binary `.rt` files store (start_point, end_point) pairs. Compressed `.rtc` and `.rti2` (RTI 2.0 variable-length bit-packed) formats also supported. Table parameters are encoded in the filename: `ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt`.
3. **Lookup** (`crackalack_lookup`) - Uses a pipelined two-phase approach: (1) bulk-load tables into RAM up to an auto-detected memory budget, then (2) batch precompute hashes using GPU + CPU threads in parallel, binary search all loaded tables, and run false alarm checks. If tables exceed RAM, the pipeline processes them in chunks. The GPU/CPU hash split is auto-tuned based on measured timing.

### GPU abstraction layer

All GPU operations go through `gpu_backend.h`, which defines backend-neutral types (`gpu_device`, `gpu_buffer`, `gpu_uint`, etc.) and macros (`CLCREATEARG`, `CLRUNKERNEL`, `CLREADBUFFER`). The macros dispatch to the active backend:

- **CUDA** (`cuda_setup.c`) - CUDA Driver API + NVRTC-based runtime kernel compile. Used on Linux (NVIDIA only). Replaces OpenCL on Linux as of the cuda-port branch.
- **OpenCL** (`opencl_setup.c`) - Dynamic loading of OpenCL via function pointers (`rc_cl*`). Used on Windows.
- **Metal** (`metal_setup.m`) - Objective-C implementation using Metal API. Used on macOS. Compiles `.metal` shaders at runtime with manual `#include` resolution (Metal's runtime compiler doesn't support include paths). Buffer args are stored in a per-kernel side table and bound at dispatch time.

Consumer files use `gpu_backend.h` types and macros - they don't interact with CUDA, OpenCL, or Metal APIs directly. The `USE_METAL` preprocessor define selects the Metal backend; `USE_CUDA` selects CUDA; otherwise OpenCL is used.

### Code organization

**Host code (C):**
- `crackalack_gen.c` / `crackalack_lookup.c` / `crackalack_verify.c` - main entry points
- `gpu_backend.h` - backend-neutral GPU abstraction types, functions, macros
- `cuda_setup.c` - CUDA backend (Linux)
- `opencl_setup.c` - OpenCL backend (Windows)
- `metal_setup.m` - Metal backend (macOS Apple Silicon)
- `compat.h` - pthread_barrier_t polyfill for macOS
- `cpu_rt_functions.c` - CPU-side rainbow table primitives (hash, reduce, chain walk)
- `charset.c` - character set definitions (ascii-32-95 is the primary set)
- `misc.c` - table filename parsing (`parse_rt_params`), file I/O helpers
- `rtc_decompress.c` - compressed table decompression
- `gws.c` - global work size tuning for GPU dispatch
- `verify.c` - table integrity verification
- `test_*.c` - unit test implementations

**GPU kernels:**
- `CL/` - OpenCL kernels (`.cl` files)
- `Metal/` - Metal shaders (`.metal` files), mechanically translated from OpenCL
- `CUDA/` - CUDA kernels (`.cu` files), mechanically translated from OpenCL

All three directories contain the same kernel set: chain generation (`crackalack*`), lookup precomputation (`precompute*`), false alarm checking (`false_alarm_check*`), hash implementations (`ntlm*`), rainbow table primitives (`rt.*`), and test kernels (`test_*`).

### Shared constants (`shared.h`)

```c
#define HASH_NTLM 2        // Primary supported hash type
#define HASH_LM 1          // LanManager (limited support)
#define HASH_NETNTLMV1 9   // NetNTLMv1
#define MAX_PLAINTEXT_LEN 16
#define TABLE_INDEX_TO_REDUCTION_OFFSET(_table_index) (_table_index * 65536)
```

## Key conventions

- **NTLM8/NTLM9 fast paths** - Separate, hand-optimized kernel files exist for 8-char and 9-char NTLM tables. The generic kernel handles other cases. All three phases (generation, precompute, false alarm check) have dedicated NTLM8/NTLM9 variants.
- **Table filename format** - `{hash}_{charset}#{min_len}-{max_len}_{table_index}_{chain_len}x{num_chains}_{part}.rt`
- **Flat source layout** - All `.c` and `.h` files live in the project root. GPU kernels live in `CL/`, `Metal/`, or `CUDA/`. No subdirectories for host code.
- **Build artifacts** - Object files go to `build/{linux,windows}/obj/`. Final binaries are placed in the project root.
- **Cross-platform** - `#ifdef _WIN32` and `#ifdef __APPLE__` guards throughout. Windows builds use mingw-w64 cross-compilation. macOS uses Metal via `#ifdef USE_METAL`. Linux uses CUDA via `#ifdef USE_CUDA`.
- **Kernel loading** - Kernels are loaded at runtime from `CL/` (OpenCL), `Metal/` (Metal), or `CUDA/` (CUDA). The appropriate directory must be present alongside binaries.
- **Metal quirks** - `unsigned long` is 32-bit in Metal (use `ulong` for 64-bit). `long long` is not supported. All pointer parameters in Metal inline functions need explicit `thread`/`device`/`constant` address space qualifiers. Metal's runtime compiler doesn't support `#include` - the host code resolves includes before compilation.
