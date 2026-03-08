# Rainbow Crackalack

Author: [Joe Testa](https://www.positronsecurity.com/company/) ([@therealjoetesta](https://twitter.com/therealjoetesta))

## About

This project produces open-source code to generate rainbow tables as well as use them to look up password hashes.  While the current release only supports NTLM, future releases may support MD5, SHA-1, SHA-256, and possibly more.  Both Linux and Windows are supported!

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
|ntlm        |The hash algorithm to use.  Currently only "ntlm" is supported.|
|ascii-32-95 |The character set to use.  This effectively means "all available characters on the US keyboard".|
|9           |The minimum plaintext character length.|
|9           |The maximum plaintext character length.|
|0           |The reduction index.  Not used under standard conditions.|
|803000      |The chain length for a single rainbow chain.|
|67108864    |The number of chains per table (= 64M)|
|0 |The table part index.  Keep all other args the same, and increment this field to generate a single set of tables.|

#### Generating tables with mask charsets

In addition to named charsets (e.g. `ascii-32-95`), `crackalack_gen` accepts hashcat-style mask strings that define a per-position character set:

| Specifier | Characters |
|-----------|------------|
| `?l` | a-z (26) |
| `?u` | A-Z (26) |
| `?d` | 0-9 (10) |
| `?s` | printable non-alphanumeric ASCII: ` !"#$%&'()*+,-./:;<=>?@[\]^_{|}~` (33) |
| `?a` | `?l + ?u + ?d + ?s` (95) |
| `?b` | all 256 byte values |

Literal characters are also supported (e.g. `P?d?d?d` covers P000-P999).

Example - generate a table covering all 4-character passwords matching `[A-Z][a-z][0-9][symbol]`:

    # ./crackalack_gen ntlm '?u?l?d?s' 4 4 0 10000 223080 0

**Note:** mask characters (`?`) are stored as `%` in the output filename to keep filenames shell-safe (e.g. `?u?l?d?s` → `ntlm_%u%l%d%s#4-4_...`).

**Note:** `crackalack_verify -q` (quick mode) skips CPU chain verification for mask tables.

#### Sorting tables before lookup

Tables must be sorted by end index before they can be used with `crackalack_lookup`. Pass one or more `.rt` files:

    # ./crackalack_sort ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt

To sort an entire directory of tables in parallel, pass all files at once. The tool auto-detects the number of parallel workers based on available RAM and CPU cores:

    # ./crackalack_sort /export/ntlm8_tables/*.rt

To override the worker count explicitly:

    # ./crackalack_sort --jobs 4 /export/ntlm8_tables/*.rt

`--jobs 0` (or omitting `--jobs`) uses automatic detection. Files that are already sorted are skipped without being rewritten.

#### Table lookups against NTLM 8-character hashes

The following command shows how to look up a file of NTLM hashes (one per line) against the NTLM 8-character tables:

    # ./crackalack_lookup /export/ntlm8_tables/ /home/user/hashes.txt

## Recommended Hardware

The NVIDIA GTX & RTX lines of GPU hardware has been well-tested with the Rainbow Crackalack software, and offer an excellent price/performance ratio.  Specifically, the GTX 1660 Ti or RTX 2060 are the best choices for building a new cracking machine.  [This document](https://docs.google.com/spreadsheets/d/1jigNGvt9SUur_SNH7QDEACapJbrdL_wKYtprM23IDpM/edit?usp=sharing) contains the raw data that backs this recommendation.

However, other modern equipment can work just fine, so you don't necessarily need to purchase something new.  The NVIDIA GTX and AMD Vega product lines are still quite useful for cracking!

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
