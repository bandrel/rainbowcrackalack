# Rainbow Crackalack - Agent Guide

GPU-accelerated rainbow table generator and hash lookup tool. C with OpenCL (Linux/Windows) and Metal (macOS Apple Silicon) GPU backends. Currently supports NTLM only. GPLv3.

## Build

Requires libgcrypt. GPU backend depends on platform.

```bash
# Linux (Ubuntu) - OpenCL backend
apt install opencl-c-headers libgcrypt20-dev
make clean; make linux

# macOS (Apple Silicon) - Metal backend
brew install libgcrypt
make clean; make macos

# Windows (cross-compile from Ubuntu) - OpenCL backend
apt install mingw-w64 opencl-headers libgcrypt-mingw-w64-dev
make clean; make windows
```

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

## Tests

Unit tests require a GPU (OpenCL on Linux/Windows, Metal on macOS):

```bash
./crackalack_unit_tests
```

Tests cover: chain generation, NTLM hashing, hash-to-index reduction, and index-to-plaintext conversion. Separate test files exist for standard and NTLM9-optimized paths (e.g., `test_chain.c` vs `test_chain_ntlm9.c`).

## Architecture

### Three-phase pipeline

1. **Generation** (`crackalack_gen`) - GPU computes rainbow chains from start points using iterated hash + reduce cycles. Output: `.rt` binary table files.
2. **Table storage** - Binary `.rt` files store (start_point, end_point) pairs. Compressed `.rtc` format also supported (`crackalack_rtc2rt` decompresses). Table parameters are encoded in the filename: `ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt`.
3. **Lookup** (`crackalack_lookup`) - GPU-accelerated precomputation of candidate endpoints, binary search in sorted tables, then false alarm checking to confirm matches. Tables are pre-loaded in parallel with search for throughput.

### GPU abstraction layer

All GPU operations go through `gpu_backend.h`, which defines backend-neutral types (`gpu_device`, `gpu_buffer`, `gpu_uint`, etc.) and macros (`CLCREATEARG`, `CLRUNKERNEL`, `CLREADBUFFER`). The macros dispatch to the active backend:

- **OpenCL** (`opencl_setup.c`) - Dynamic loading of OpenCL via function pointers (`rc_cl*`). Used on Linux/Windows.
- **Metal** (`metal_setup.m`) - Objective-C implementation using Metal API. Used on macOS. Compiles `.metal` shaders at runtime with manual `#include` resolution (Metal's runtime compiler doesn't support include paths). Buffer args are stored in a per-kernel side table and bound at dispatch time.

Consumer files use `gpu_backend.h` types and macros - they don't interact with OpenCL or Metal APIs directly. The `USE_METAL` preprocessor define selects the backend at compile time.

### Code organization

**Host code (C):**
- `crackalack_gen.c` / `crackalack_lookup.c` / `crackalack_verify.c` - main entry points
- `gpu_backend.h` - backend-neutral GPU abstraction types, functions, macros
- `opencl_setup.c` - OpenCL backend (Linux/Windows)
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

Both directories contain the same kernel set: chain generation (`crackalack*`), lookup precomputation (`precompute*`), false alarm checking (`false_alarm_check*`), hash implementations (`ntlm*`), rainbow table primitives (`rt.*`), and test kernels (`test_*`).

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
- **Flat source layout** - All `.c` and `.h` files live in the project root. OpenCL kernels live in `CL/`. No subdirectories for host code.
- **Build artifacts** - Object files go to `build/{linux,windows}/obj/`. Final binaries are placed in the project root.
- **Cross-platform** - `#ifdef _WIN32` and `#ifdef __APPLE__` guards throughout. Windows builds use mingw-w64 cross-compilation. macOS uses Metal via `#ifdef USE_METAL`.
- **Kernel loading** - Kernels are loaded at runtime from `CL/` (OpenCL) or `Metal/` (Metal). The appropriate directory must be present alongside binaries.
- **Metal quirks** - `unsigned long` is 32-bit in Metal (use `ulong` for 64-bit). `long long` is not supported. All pointer parameters in Metal inline functions need explicit `thread`/`device`/`constant` address space qualifiers. Metal's runtime compiler doesn't support `#include` - the host code resolves includes before compilation.
