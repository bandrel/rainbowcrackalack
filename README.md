# Rainbow Crackalack

[![CPU unit tests](https://github.com/example/rainbowcrackalack/actions/workflows/ci.yml/badge.svg)](https://github.com/example/rainbowcrackalack/actions/workflows/ci.yml)

Original author: [Joe Testa](https://www.positronsecurity.com/company/) ([@therealjoetesta](https://twitter.com/therealjoetesta))

## Origins

This is an independent derivative of [jtesta/rainbowcrackalack](https://github.com/jtesta/rainbowcrackalack) — the original project by Joe Testa (initial commit 2019-06-10, last upstream commit 2021-08-04). A downstream fork, [blurbdust/rainbowcrackalack](https://github.com/blurbdust/rainbowcrackalack), continued the work after jtesta went dormant.

This tree is **not a git fork** of either — the histories share no common ancestor. It re-seeds the codebase and adds CUDA and Metal GPU backends, mask/Markov generation, and other changes. Credit for the original design and OpenCL implementation goes to Joe Testa.

## About

This project produces open-source code to generate rainbow tables as well as use them to look up password hashes.  Currently supports NTLM, MD5, and Net-NTLMv1.  Future releases may support SHA-1, SHA-256, and possibly more.  Linux, Windows, and macOS (Apple Silicon) are supported!

For more information, see the project website: [https://www.rainbowcrackalack.com/](https://www.rainbowcrackalack.com/)

## NTLM Tables

NTLM 8-character tables (93% effective) are available for [free download via Bittorrent](https://www.rainbowcrackalack.com/rainbow_crackalack_ntlm_8.torrent).

NTLM 9-character tables (50% effective) are available for [free download via Bittorrent](https://www.rainbowcrackalack.com/rainbow_crackalack_ntlm_9.torrent).

For convenience, the tables [may also be purchased](https://www.rainbowcrackalack.com/#download) on a USB 3.0 external hard drive.

## Binaries

|Binary               |Purpose                                              |
|----------------------|-----------------------------------------------------|
|`crackalack_gen`      |Generate rainbow tables                              |
|`crackalack_lookup`   |Look up hashes against rainbow tables                |
|`crackalack_verify`   |Verify generated tables for correctness              |
|`crackalack_sort`     |Sort rainbow tables by end index for lookup          |
|`crackalack_unit_tests`|Run GPU-accelerated unit tests                      |
|`crackalack_rtc2rt`   |Decompress .rtc tables to .rt format                 |
|`crackalack_plan`     |Estimate table parameters, recommend chain settings, and train Markov models|
|`perfectify`          |Remove duplicate endpoints from tables               |
|`get_chain`           |Extract a single chain from a table                  |
|`enumerate_chain`     |Walk a chain and print each step                     |

## Examples

#### Generating NTLM 9-character tables

The following command shows how to generate a standard 9-character NTLM table:

    # ./crackalack_gen ntlm ascii-32-95 9 9 0 803000 67108864 0

The arguments are designed to be comparable to those of the original (and now closed-source) rainbow crack tools.  In order, they mean:

|Argument    |Meaning   |
|------------|----------|
|ntlm        |The hash algorithm to use.  Supported values: "ntlm", "md5", "netntlmv1".|
|ascii-32-95 |The character set to use.  This effectively means "all available characters on the US keyboard".|
|9           |The minimum plaintext character length.|
|9           |The maximum plaintext character length.|
|0           |The reduction index.  Not used under standard conditions.|
|803000      |The chain length for a single rainbow chain.|
|67108864    |The number of chains per table (= 64M)|
|0 |The table part index.  Keep all other args the same, and increment this field to generate a single set of tables.|

#### Generating MD5 tables

MD5 8-character and 9-character tables use the same arguments as NTLM:

    # ./crackalack_gen md5 ascii-32-95 8 8 0 422000 67108864 0
    # ./crackalack_gen md5 ascii-32-95 9 9 0 803000 67108864 0

The `--markov` flag works with MD5 the same way it does with NTLM:

    # ./crackalack_gen md5 ascii-32-95 8 8 0 422000 67108864 0 --markov md5_rockyou.markov

#### Sorting tables before lookup

Tables must be sorted by end index before they can be used with `crackalack_lookup`. Pass one or more `.rt` files:

    # ./crackalack_sort ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt

To sort an entire directory of tables in parallel, pass all files at once. The tool auto-detects the number of parallel workers based on available RAM and CPU cores:

    # ./crackalack_sort /export/ntlm8_tables/*.rt

To override the worker count explicitly:

    # ./crackalack_sort --jobs 4 /export/ntlm8_tables/*.rt

`--jobs 0` (or omitting `--jobs`) uses automatic detection, measuring available RAM and CPU cores to pick the largest worker count that fits each table into RAM simultaneously. Override with `--jobs N` to reserve resources for other concurrent processes.

Files that are already sorted are detected and skipped automatically. It is safe to pass an entire directory glob even if some tables were previously sorted.

#### Estimating table size and coverage

Given full generation parameters, `crackalack_plan estimate` prints file size and coverage without generating anything:

    # ./crackalack_plan estimate ntlm ascii-32-95 8 8 422000 67108864

#### Recommending chain parameters for a target coverage

    # ./crackalack_plan recommend ntlm ascii-32-95 8 8 50%

#### Training a Markov model from a wordlist

    # ./crackalack_plan train rockyou.txt

By default, this creates a **position-aware model** with 10 position tables, where different character positions have their own bigram transition probabilities. This captures real password patterns - e.g., capital letters at the start, numbers at the end.

**Position-aware models**

To control the number of position tables, use the `--max-positions` flag:

    # ./crackalack_plan train rockyou.txt ascii-32-95 --max-positions 5

Arguments:
- `<wordlist>` - The password file to train on (one password per line)
- `[charset]` - Character set name (default: `ascii-32-95`)
- `[--max-positions N]` - Number of position-specific bigram tables (default: 10)

Positions 0 through N-1 get unique bigram tables. Positions >= N reuse the last table. This allows the model to be space-efficient for very long passwords while still capturing early-position patterns.

Recommended minimum wordlist size: **1M passwords**. For position-aware models with 10 positions, each position has 95x95 = 9,025 transition parameters; ~1M real training words provide reliable estimates for all common bigrams at each position.

| Wordlist size | Model quality |
|---------------|---------------|
| < 100K        | Poor - rare bigrams unreliable |
| 100K - 1M     | Acceptable |
| 1M - 10M      | Good |
| 10M+          | Excellent (diminishing returns above ~20M) |

rockyou.txt (14.3M entries) is the practical gold standard for general consumer passwords. Quality matters more than size - 1M real leaked passwords from the target environment beat 100M generic dictionary words.

#### Generating tables with a Markov model

Pass `--markov <file.markov>` to `crackalack_gen` to bias chain start points toward the most probable plaintexts per the training corpus:

    # ./crackalack_gen ntlm ascii-32-95 8 8 0 422000 67108864 0 --markov ntlm_rockyou.markov

A 10% coverage Markov table covers 10% of the most probable password space rather than 10% of the alphabetical space - dramatically better real-world crack rates for common passwords.

**Note:** `--markov` requires `min_len == max_len`. It cannot be combined with the NTLM8/NTLM9 fast-path kernels (falls back to the generic Markov kernel with a warning).

#### Looking up hashes against Markov-generated tables

Pass the same `--markov` flag to `crackalack_lookup` when looking up hashes against tables that were generated with `--markov`:

    # ./crackalack_lookup /export/ntlm8_tables/ /home/user/hashes.txt --markov ntlm_rockyou.markov

#### Mask attacks

Hashcat-style masks restrict each plaintext position to a specific character class, so a table can cover a structured password space (e.g. `Capital + 6 lowercase + digit`) with a far smaller keyspace than full-charset coverage. Pass `--mask <mask>` to `crackalack_gen`:

    # 8-char mask: capital + 6 lowercase + digit
    # ./crackalack_gen ntlm ascii-32-95 8 8 0 803000 67108864 0 --mask '?u?l?l?l?l?l?l?d'

**Built-in tokens:** `?l` (a-z), `?u` (A-Z), `?d` (0-9), `?s` (printable non-alphanumeric), `?a` (all printable ASCII), `?b` (all bytes 0x00-0xFF), `?h` (0-9a-f), `?H` (0-9A-F), `??` (a literal `?`).

**Custom charsets:** define up to four with `-1`/`-2`/`-3`/`-4` (aliases `--custom-charset1..4`) and reference them as `?1`-`?4`. Definitions may themselves contain built-in tokens and `\xNN` hex escapes:

    # ?1 = 'abcxyz', 4-char mask ?1?1?l?d
    # ./crackalack_gen ntlm ascii-32-95 4 4 0 803000 67108864 0 --mask '?1?1?l?d' -1 abcxyz

Masks are fixed-length (`min_len` and `max_len` must equal the mask length) and supported for NTLM and MD5 (not NetNTLMv1). The mask — including any custom charsets — is encoded into the `.rt` filename (custom sets as `!N-<hex>` blocks), so tables are **self-describing**: `crackalack_lookup` reconstructs the mask automatically and needs no mask flags:

    # ./crackalack_lookup /export/mask_tables/ /home/user/hashes.txt

`crackalack_verify` and `gen_known_hash` also accept `--mask` and `-1..-4`.

#### Batch masks (.hcmask)

`crackalack_gen --hcmask FILE` generates one table per mask line, using the hashcat `.hcmask` format (`#` comments, blank lines, and per-line inline custom charsets `cs1,...,csN,mask` where the last comma-field is the mask and preceding fields define `?1..?N`; `\,` and `\#` are escapes). The positional length arguments are ignored — each table's length is derived from its own mask:

    # printf '?u?l?l?l?l?l?d\n?d?l,?1?1?1?1\n' > masks.hcmask
    # ./crackalack_gen ntlm ascii-32-95 8 8 0 803000 67108864 0 --hcmask masks.hcmask

`crackalack_verify --hcmask FILE DIR` batch-verifies the generated tables and reports any masks with no matching table (campaign completeness). Lookup needs no `.hcmask` file (tables are self-describing).

#### Mask + Markov (combined)

`--mask` and `--markov` can be combined: the mask hard-restricts each position's charset, and the Markov model orders candidates within that restricted space by bigram probability. With `--markov-keyspace K` the table covers only the **K most-probable in-mask plaintexts** — the smallest, highest-yield tables for structured passwords:

    # ./crackalack_gen ntlm ascii-32-95 8 8 0 803000 67108864 0 --mask '?u?l?l?l?l?l?d' --markov rockyou.markov --markov-keyspace 1000000

Every mask-position character must be a subset of the model's charset. Combined tables are tagged `<mask>-mk<K>` in the filename; at lookup pass `--markov <model>` (the mask is auto-detected from the filename):

    # ./crackalack_lookup /export/mask_markov_tables/ /home/user/hashes.txt --markov rockyou.markov

#### Generating NetNTLMv1 tables

NetNTLMv1 rainbow tables cover the 7-byte DES key fragments used in the Net-NTLMv1 challenge-response protocol. Each fragment is a raw byte value (charset `byte`, length 7), so the keyspace is 256^7 per fragment.

    # ./crackalack_gen netntlmv1 byte 7 7 0 803000 67108864 0

Looking up captured Net-NTLMv1 hashes works the same as NTLM:

    # ./crackalack_lookup /export/netntlmv1_tables/ /home/user/hashes.txt

The hash file should contain 16-hex-character DES fragments (one per line). A full 48-character Net-NTLMv1 response must be split into three 16-character fragments before lookup.

#### Net-NTLMv1 server challenges

Net-NTLMv1 responses are computed against a server challenge, so a rainbow table is only valid for the specific challenge it was generated with. The default challenge is `1122334455667788` (the fixed value commonly forced by relay/downgrade tooling). To target a different captured challenge, pass `--challenge` (16 hex digits) to `crackalack_gen`:

    # ./crackalack_gen netntlmv1 byte 7 7 0 803000 67108864 0 --challenge aabbccddeeff0011

The challenge is encoded into the table filename — non-default challenges get a `-chal<16-hex>` suffix on the charset segment, e.g. `netntlmv1_byte-chalaabbccddeeff0011#7-7_0_803000x67108864_0.rt`. At lookup time `crackalack_lookup` adopts the challenge from the loaded tables automatically, so the same command works regardless of challenge:

    # ./crackalack_lookup /export/netntlmv1_chal_tables/ /home/user/hashes.txt

If you need to override it (e.g. tables without the challenge in their names), pass `--challenge aabbccddeeff0011` to `crackalack_lookup` as well. Note that the precompute cache key includes a non-default challenge, so switching challenges will not produce stale cross-challenge false negatives.

#### Table lookups against NTLM 8-character hashes

The following command shows how to look up a file of NTLM hashes (one per line) against the NTLM 8-character tables:

    # ./crackalack_lookup /export/ntlm8_tables/ /home/user/hashes.txt

#### Optional lookup flags

`crackalack_lookup` accepts several optional flags, given after the table directory and the hash/hash-file arguments:

|Flag             |Purpose                                                                                          |
|------------------|-------------------------------------------------------------------------------------------------|
|`--challenge HEX` |Net-NTLMv1 server challenge as 16 hex digits (default `1122334455667788`). Normally adopted automatically from the loaded tables.|
|`--bloom-fpr X`   |Bloom filter target false-positive rate (default `0.01`; `0` disables the bloom filter).         |
|`--fa-batch N`    |False-alarm batch flush threshold (default `16384`; `1` disables batching).                      |
|`--gpu-search`    |Offload per-table endpoint binary search to the GPU (off by default).                            |
|`-gws GWS`        |Set the GPU global work size.                                                                    |

## Recommended Hardware

The NVIDIA GTX & RTX lines of GPU hardware has been well-tested with the Rainbow Crackalack software, and offer an excellent price/performance ratio.  Specifically, the GTX 1660 Ti or RTX 2060 are the best choices for building a new cracking machine.  [This document](https://docs.google.com/spreadsheets/d/1jigNGvt9SUur_SNH7QDEACapJbrdL_wKYtprM23IDpM/edit?usp=sharing) contains the raw data that backs this recommendation.

However, other modern equipment can work just fine, so you don't necessarily need to purchase something new.  The NVIDIA GTX and AMD Vega product lines are still quite useful for cracking!

## macOS Build (Apple Silicon)

Install prerequisites:

    # brew install libgcrypt

Then build:

    # make clean; make macos

## Windows Build

A 64-bit Windows build can be achieved on an Ubuntu host machine by installing the following prerequisites:

    # apt install mingw-w64 opencl-headers libgcrypt-mingw-w64-dev

Then starting the build with:

    # make clean; make windows

However, if you prefer to build a complete package (which is useful for testing on other Windows machines), run:

    # 7z a windows-build.7z *.exe *.dll CL shared.h

## Linux Build

Two GPU backends are supported on Linux: **OpenCL** (default, portable) and **CUDA** (NVIDIA only, typically faster).

### OpenCL (default)

Works with any OpenCL ICD (NVIDIA, AMD, Intel, PoCL, etc.):

    # apt install opencl-headers libgcrypt20-dev
    $ make clean; make linux

Binaries are written to the project root. `opencl_setup.c` `dlopen()`s `libOpenCL` at runtime, so no OpenCL library is needed at link time.

### CUDA (NVIDIA only)

Kernels are JIT-compiled at runtime via NVRTC. Requires the NVIDIA driver and CUDA toolkit:

    # apt install nvidia-cuda-toolkit libgcrypt20-dev
    $ make clean; make cuda

Binaries are written to the project root (same location as the OpenCL build), so `make clean` is required when switching between backends.

#### NVRTC / toolkit version notes

The `nvidia-cuda-toolkit` package from the Ubuntu repositories may lag behind the installed NVIDIA driver. Two failure modes to watch for:

* **NVRTC version mismatch** at runtime — NVRTC linked against a toolkit newer than the driver can consume, or vice versa.
* **PTX "unsupported toolchain"** at runtime — the generated PTX targets a compute capability the driver does not implement.

Both are resolved by installing a driver-compatible toolkit from NVIDIA's apt repository (e.g. `cuda-toolkit-13-x` for a recent driver, or `cuda-toolkit-12-8` for a driver stuck on CUDA 12) and pointing the build at it:

    $ make clean; make cuda CUDA_PATH=/opt/cuda
    $ make clean; make cuda CUDA_PATH=/usr/local/cuda-12.8

The `CUDA_PATH` variable overrides `/usr/local/cuda` (the default install location).

#### Compiled-PTX disk cache

The CUDA backend caches compiled PTX on disk keyed by an FNV-1a content hash of the resolved kernel source, target arch, and build flags. This makes repeated launches of `crackalack_gen` (e.g. one invocation per mask over a `.hcmask` file) skip the ~0.1–1 s NVRTC compile per kernel. Location: `$RCRACK_KERNEL_CACHE` (set to `off` to disable), else `$XDG_CACHE_HOME/rainbowcrackalack`, else `$HOME/.cache/rainbowcrackalack`. Purely an optimization — any cache miss falls back to compiling.

### Historical note

Earlier releases used `make linux` for the CUDA build. The Makefile targets were flipped so `make linux` is the portable OpenCL build and `make cuda` is the NVIDIA-specific build. `make linux-cuda` remains as an alias for `make cuda`.

## Testing

The test suite has two tiers — a GPU-accelerated suite and a CPU-only suite that needs no GPU. The sanitizer, coverage, and crack-regression harnesses live in the companion **rainbowcrackalack-docs** repo (`code-repo-extracts/scripts/bench/`); see below.

**GPU unit tests** (require a GPU; CUDA on Linux, OpenCL on Windows, Metal on macOS):

    # ./crackalack_unit_tests

These cover the GPU kernels — hashing (NTLM/NTLM9/MD5/Net-NTLMv1), hash-to-index reduction, index-to-plaintext, and chain generation — plus the CPU-only tests below.

**CPU-only unit tests** (no GPU required) build a separate binary and run the host-side logic — hash/pot-file parsing, false-alarm batching, bloom filters, table sorting, decompression, Markov models, Net-NTLMv1 challenge parsing, and the golden vectors:

    # make cpu-tests && ./crackalack_cpu_tests

This is what runs in **CI**: a GitHub Actions workflow builds and runs `crackalack_cpu_tests` on every push and pull request (`build-essential`, `libgcrypt20-dev`, `opencl-headers` — no GPU), so CPU-logic regressions are caught automatically.

**Golden vectors** (`test_golden.c`) pin committed input→output pairs for the rainbow-table primitives (independently-verified NTLM/MD5 vectors plus regression-pinned reductions), giving every GPU backend a fixed ground truth to validate against. They run in both test binaries.

**Sanitizers, coverage, and the crack-regression harness** are maintained in the companion **rainbowcrackalack-docs** repo under `code-repo-extracts/scripts/bench/` (they require a GPU); CI fetches them at run time. That harness includes AddressSanitizer/ThreadSanitizer smoke tests, a coverage runner, and `run_regression.sh` (crack round-trip + differential false-negative checks, plus `mask` / `hcmask` / `mask-markov` end-to-end phases).

Two instrumentation entry points remain in-tree via the Makefile:

|Command                     |Purpose                                                  |
|-----------------------------|---------------------------------------------------------|
|`make tsan-sort`             |ThreadSanitizer over the multi-threaded sort (CPU-only). |
|`make COVERAGE=1 <target>`   |gcov/llvm-cov line-coverage build for the host code.     |

## Change Log
### v1.5.2
 - Added a compiled-PTX disk cache to the CUDA backend (`cuda_setup.c`). NVRTC compilation (~0.1–1s per kernel) was repeated on every process launch, so batch workflows that invoke `crackalack_gen` many times (e.g. `--hcmask`, or one invocation per mask over a large mask set) paid it thousands of times even though the emitted PTX is identical for a given (resolved source, arch, build flags). The PTX is now cached on disk keyed by an FNV-1a content hash and reused across processes, roughly halving per-launch startup. Location honors `RCRACK_KERNEL_CACHE` (set to `off` to disable), else `$XDG_CACHE_HOME/rainbowcrackalack`, else `$HOME/.cache/rainbowcrackalack`. Purely an optimization — any cache miss/error falls back to compiling. Atomic writes (temp + rename) make it safe under the concurrent multi-GPU workers on one host.

### v1.5.1
 - Added `gen_markov_hcmask.sh`, a helper that drives combined mask+Markov generation over a hashcat `.hcmask` file. Native `--hcmask` batch mode is mutually exclusive with `--markov`, so the script parses the `.hcmask` file itself (matching the binary's line/escape rules), derives each mask's length by counting positions (not raw characters), and runs one `crackalack_gen --mask <mask> --markov <model>` invocation per line with `min_len == max_len` set to that mask's length. Honors inline per-line custom charsets (which override global `-1..-4` defaults), `--markov-keyspace`, and `-gws`.

### v1.5
 - Added hashcat-style mask attacks: `--mask` on `crackalack_gen`/`crackalack_verify`/`gen_known_hash` with tokens `?l ?u ?d ?s ?a ?b ?h ?H` and `??` escaping. Masks restrict each position's charset for far smaller tables on structured passwords. Fixed-length; NTLM/MD5.
 - Added custom charsets `?1`-`?4` via `-1`/`-2`/`-3`/`-4` (definitions may contain built-in tokens and `\xNN` hex). Custom sets are encoded into the `.rt` filename (`!N-<hex>` blocks) so mask tables are self-describing — `crackalack_lookup` reconstructs the mask with no extra flags.
 - Added `.hcmask` batch support: `crackalack_gen --hcmask FILE` generates one table per mask line (hashcat format, incl. inline per-line custom charsets); `crackalack_verify --hcmask FILE DIR` batch-verifies and reports missing masks.
 - Added combined mask+Markov generation: `--mask` with `--markov` orders candidates within the mask's restricted per-position charsets by bigram probability, so `--markov-keyspace` covers the top-K most-probable in-mask plaintexts. Generic kernels across CUDA/OpenCL/Metal.
 - Verified end-to-end (gen/sort/verify/mint/lookup) across Metal, CUDA, and OpenCL backends; regression harness (`run_regression.sh`) gained `hcmask` and `mask-markov` phases.

### v1.4
 - Ported the Linux GPU backend from OpenCL to CUDA (runtime kernel compilation via NVRTC; NVIDIA-only). Linux builds now require `nvidia-cuda-toolkit` instead of OpenCL headers. Windows continues to use OpenCL.
 - Added macOS Apple Silicon support via Metal GPU backend.
 - Added `crackalack_sort` tool for sorting tables by end index before lookup, with parallel multi-file sorting and automatic worker-count tuning.
 - Added `crackalack_plan` tool with `estimate`, `recommend`, and `train` subcommands.
 - Added Markov model support: `--markov <file>` flag on both `crackalack_gen` and `crackalack_lookup` for probability-biased table generation and lookup.
 - Added position-aware Markov models: position-specific bigram tables capture real password patterns (e.g., capitals at start, numbers at end). Train with `--max-positions N` to control position table count (default: 10).
 - Fixed NetNTLMv1-7 per-hash precompute fallback ignoring the table's chain length (it assumed the standard length); now honors the host-provided value across the CUDA, OpenCL, and Metal backends.
 - Fixed OpenCL NetNTLMv1-7 lookups loading the wrong false-alarm kernel file.
 - Fixed CUDA build on toolkits >= 13.0: call `cuCtxCreate_v2` explicitly, since CUDA 13+ headers remap the bare `cuCtxCreate` to a 4-arg `cuCtxCreate_v4`. When the installed NVRTC is newer than the GPU driver supports (PTX "unsupported toolchain" at runtime), build against a driver-compatible toolkit, e.g. `make linux CUDA_PATH=/usr/local/cuda-12.8`.
 - Fixed a heap-buffer-overflow in lookup: the JTR pot file was read into a buffer with no room for a NUL terminator, then scanned as a C string, reading past the allocation. Besides the over-read, it could spuriously flag an uncracked hash as already-cracked (silent false negative). This is the likely cause of intermittent "double free or corruption" crashes during NetNTLMv1 lookups.
 - Fixed an out-of-bounds write in the multi-GPU false-alarm result harvest: results were indexed by a running counter across devices instead of by candidate position, overrunning the candidate array once more than one GPU was used. Added a defensive bounds guard so any future mismatch fails loudly instead of corrupting the heap.
 - Added an AddressSanitizer smoke test (`scripts/bench/asan_smoke_test.sh`) and wired it into the regression harness (`run_regression.sh asan-smoke`) to catch heap-safety regressions across the gen/sort/lookup pipeline.

### v1.3 (February 26, 2021)
 - Improved speed of NTLM9 precomputation by 9.5x and false alarm checks by 4.5x!
 - Fixed lookup on AMD ROCm.
 - Added support for pwdump-formatted hash files.
 - Added time estimates for precomputation phase.
 - Disable Intel GPUs when found on systems with AMD or NVIDIA GPUs.
 - Fixed bug in counting tables during lookup.
 - Fixed bug where lookups would continue even though all hashes were cracked.
 - Fixed cache lookup when a single hash in uppercase was provided.
 - Added lookup colors.

### v1.2 (April 2, 2020)
 - Lookup tables are now pre-loaded in parallel to binary searching & false alarm checking, resulting in 30-40% speed improvement (!).

### v1.1 (August 8, 2019)
 - Massive speed improvements (credit Steve Thomas).
 - Finalization of NTLM9 spec.
 - Various bugfixes.

### v1.0 (June 11, 2019)
 - Initial revision.
