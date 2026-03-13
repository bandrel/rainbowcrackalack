# Rainbow Crackalack

Author: [Joe Testa](https://www.positronsecurity.com/company/) ([@therealjoetesta](https://twitter.com/therealjoetesta))

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

#### Generating NetNTLMv1 tables

NetNTLMv1 rainbow tables cover the 7-byte DES key fragments used in the Net-NTLMv1 challenge-response protocol. Each fragment is a raw byte value (charset `byte`, length 7), so the keyspace is 256^7 per fragment.

    # ./crackalack_gen netntlmv1 byte 7 7 0 803000 67108864 0

Looking up captured Net-NTLMv1 hashes works the same as NTLM:

    # ./crackalack_lookup /export/netntlmv1_tables/ /home/user/hashes.txt

The hash file should contain 16-hex-character DES fragments (one per line). A full 48-character Net-NTLMv1 response must be split into three 16-character fragments before lookup.

#### Table lookups against NTLM 8-character hashes

The following command shows how to look up a file of NTLM hashes (one per line) against the NTLM 8-character tables:

    # ./crackalack_lookup /export/ntlm8_tables/ /home/user/hashes.txt

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

A 64-bit build can be achieved on an Ubuntu host machine by installing the following prerequisites:

    # apt install opencl-c-headers libgcrypt20-dev

Then starting the build with:

    # make clean; make linux

## Change Log
### v1.4
 - Added macOS Apple Silicon support via Metal GPU backend.
 - Added `crackalack_sort` tool for sorting tables by end index before lookup, with parallel multi-file sorting and automatic worker-count tuning.
 - Added `crackalack_plan` tool with `estimate`, `recommend`, and `train` subcommands.
 - Added Markov model support: `--markov <file>` flag on both `crackalack_gen` and `crackalack_lookup` for probability-biased table generation and lookup.
 - Added position-aware Markov models: position-specific bigram tables capture real password patterns (e.g., capitals at start, numbers at end). Train with `--max-positions N` to control position table count (default: 10).

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
