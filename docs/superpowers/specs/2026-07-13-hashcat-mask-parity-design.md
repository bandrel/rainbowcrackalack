# Design: Full hashcat mask parity (custom charsets, `?h`/`?H`, escaping)

**Date:** 2026-07-13
**Branch:** `dev/masks`
**Status:** Approved (design), pending implementation plan

## Motivation

The base mask attack is implemented and GPU-verified (Metal + gpuhost3 CUDA) on
`dev/masks`. It supports the built-in tokens `?l ?u ?d ?s ?a ?b`, literal
characters, filename encoding (`?x`→`%x`), and the full gen→sort→verify→mint→
lookup pipeline across CUDA/OpenCL/Metal.

This design closes the remaining gap to **hashcat mask parity** for everything
meaningful to a *fixed-length* rainbow table:

- **Custom charsets** `?1 ?2 ?3 ?4`, defined via `-1/-2/-3/-4` (aliased
  `--custom-charset1..4`). The parser already routes `?1-?4`
  (`mask_parse.c:53-56`); they are simply fed `NULL` from the CLI today.
- **Hex tokens** `?h` (`0123456789abcdef`) and `?H` (`0123456789ABCDEF`).
- **Escaping** `??` → literal `?`.
- **Token expansion inside custom-set definitions**, e.g. `-1 ?l?d`, `-1 abc`,
  `-1 \x41\x42` — matching hashcat.

### Explicitly out of scope

- `--increment` (variable length; incompatible with fixed-length rainbow tables,
  where each table is a single length).
- `.hcmask` batch files (a convenience batch format; can be scripted externally).

## Key architectural insight: host-side only

Every mask token — built-in or custom — resolves **at parse time** into
per-position expanded charset bytes (`MaskPosition.chars[]`). The existing GPU
buffers already carry these:

```
mask_data[position * MAX_CHARSET_LEN + i]   // the actual charset bytes
mask_lens[position]                          // count of valid chars
```

The GPU kernels (`rt_mask`, `crackalack_mask`, `precompute_mask`,
`false_alarm_check_mask` in CUDA/CL/Metal) are **charset-agnostic**: they index
into `mask_data` and never care whether a position originated from `?l`, `?h`, or
`?1='abc'`.

**Consequences:**

- **No GPU kernel edits** (all three backends untouched).
- **No `.rt` binary-format change.**
- **No precompute cache-key restructuring** (the cache key derives from the
  filename charset field, which will encode the custom sets — see below).

The one approach decision — *expand-at-parse* vs. a *new kernel charset-
indirection path* — is settled in favor of expand-at-parse: it needs zero GPU
work and keeps all backends identical.

## Components

### a. `mask_parse.c` — parser extensions

- Add `?h` / `?H` cases to `build_position` (hex-lower / hex-upper).
- Add `??` → literal `?`. Today `mask_parse.c:93` treats `?` followed by any
  char as a token, so `??` currently errors as "unknown specifier '??'". Handle
  `spec == '?'` as a literal `?` position.
- New helper `expand_charset_def(const char *def, char *out, unsigned int *out_len)`:
  expands a custom-set **definition** string into raw bytes. Supports:
  - literal characters (`abc`)
  - embedded built-in tokens (`?l`, `?u`, `?d`, `?s`, `?a`, `?b`, `?h`, `?H`)
  - `\xNN` hex byte escapes (`\x41`)
  - `??` → literal `?`, `\\` → literal `\`
  Result is capped at `MAX_CHARSET_LEN` (256); overflow is an error.
- `build_position` for `?1-?4` copies the pre-expanded bytes from `c1..c4`.
- **Guards:** a custom-set definition may reference built-in tokens but **not**
  another custom set (`?1` inside `-2` is rejected — prevents recursion /
  ordering ambiguity). Duplicate characters are **kept** (matches hashcat;
  keeps keyspace identical to hashcat's).

### b. `mask_parse.c` — filename codec (persistence)

Tables are self-describing: `crackalack_lookup` reconstructs the entire mask
from the filename charset field alone. Custom-set *definitions* must therefore be
persisted in the filename.

- `mask_encode_for_filename`: emit mask tokens as today (`?x`→`%x`, now including
  `%h %H %1..%4`), then append a definitions block for **each custom set actually
  used**, in the form `!N-<hex>`:

  ```
  mask  '?1?1?d'  with  -1 abc
  ->    %1%1%d!1-616263
  ```

  Multiple sets concatenate: `%1%2%d!1-616263!2-212223`.
- `mask_decode_from_filename` / `parse_rt_params`: strip and parse the trailing
  `!N-<hex>` blocks into `c1..c4`, then re-run `mask_parse` so the loaded `Mask`
  is **byte-identical** to the one used at generation.
- Sentinel choice: `!` + digit + `-` introduces a block. `!` is not a mask-token
  character and not produced by token encoding. The decoder parses definition
  blocks **from the right**, repeatedly matching `!([1-4])-([0-9a-f]*)$`; the
  mask-token portion is whatever remains to the left.
- **Literal-`!` ambiguity:** a mask containing a literal `!` immediately followed
  by a digit `1-4` and `-` and only hex chars to the end (e.g. mask body ending
  `!1-abc`) could be misread as a definitions block. This is pathological, but
  gen **detects it at encode time** — after composing the filename it re-runs the
  decoder and asserts the recovered `Mask` is byte-identical to the original; on
  mismatch it aborts with guidance (reorder the mask or avoid a trailing literal
  `!<digit>-<hex>` sequence). This encode-time round-trip check also guards the
  codec generally.
- **Length guard:** if the composed filename would exceed the filesystem
  component limit (255 bytes), gen aborts with a clear message ("custom charset
  too large to encode in filename; use a smaller set"). Realistic custom sets are
  a handful of characters, so this only trips on pathological input.

### c. CLI plumbing

- Add `-1 / -2 / -3 / -4 <def>` (aliases `--custom-charset1..4`) to:
  - `crackalack_gen` — passed into `mask_parse`; encoded into the output filename.
  - `crackalack_verify` — passed into `mask_parse` for the `--mask` chain walk.
    (Verify may also derive them from the filename; accepting the flags keeps it
    consistent with gen and allows verifying a mask given only the string.)
  - `gen_known_hash` — passed into `mask_parse` when minting an in-table hash.
- `crackalack_lookup` needs **no new flag** — it reconstructs custom sets from
  the filename, preserving the self-describing invariant. (This is the lesson
  from the Markov and NetNTLMv1-challenge cache-key false-negative bugs: never
  require the operator to re-supply a table parameter that the table can carry
  itself.)

## Data-structure impact

None. `MaskPosition` (`chars[256]`, `size`) and `Mask` (`positions[16]`,
`length`) already accommodate any expanded charset. `mask_keyspace` (uint64 with
overflow detection) is unchanged.

## Testing

### Unit (`test_mask_parse.c`)

- `?h` / `?H` expansion (16 chars each, correct case).
- `??` → literal `?`.
- `expand_charset_def`: literal def, token-bearing def (`-1 ?d?l`), hex def
  (`-1 \x41\x42`), overflow rejection, recursion rejection (`?1` inside `-2`).
- Custom-set filename round-trip: `encode` → `decode` → re-parsed `Mask` is
  byte-identical (positions, sizes, chars).
- Length-guard rejection for an over-long custom set.

### GPU parity

The existing `test_chain_mask` and `test_index_to_plaintext_mask` (CUDA/CL/Metal)
already exercise arbitrary per-position charsets; a custom-charset case is just
new input data, requiring no new kernel test.

### End-to-end (`scripts/bench/test_mask_lookup.sh`)

Add a custom-charset case mirroring the existing one:

```
--mask '?1?1?l?d'  -1 'abcxyz'
```

gen → sort → verify (`--mask '?1?1?l?d' -1 abcxyz`) → `gen_known_hash --mask ...
-1 abcxyz` → `crackalack_lookup` (auto-detects mask incl. custom set from
filename). Assert positive crack + negative control (a hash outside the keyspace
must not crack). Run on both Metal (M3 Max) and gpuhost3 CUDA, as with the base
feature.

## Risk assessment

Low. No kernel changes, no binary-format change, no cache-key change. The only
subtle surface is the filename codec, which is fully covered by a round-trip
unit test and the E2E test. Existing (non-custom) mask tables and their
filenames are unaffected: they contain no `!N-` block, so the decoder is a
no-op for them.

## Estimated effort

~1 week, entirely host-side (parser + filename codec + CLI + tests), across the
three consumers (`gen`, `verify`, `gen_known_hash`) plus lookup's filename
decode path.
