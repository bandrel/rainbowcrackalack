# Design: `.hcmask` batch support

**Date:** 2026-07-13
**Branch:** `dev/hcmask` (off `master`, which has the mask feature merged)
**Status:** Approved (design), pending implementation plan

## Motivation

The mask attack supports a single `--mask` per invocation. hashcat `.hcmask` files
batch many masks (one per line), optionally with per-line inline custom charsets.
This feature lets a single command generate (or verify) a whole campaign of mask
tables from a `.hcmask` file, with verbatim hashcat file-format compatibility.

`crackalack_lookup` needs **no** changes: mask tables are self-describing (the
mask + custom charsets are encoded in each `.rt` filename), so lookup already
handles a directory of mixed mask tables regardless of how they were generated.

### Scope

- **`crackalack_gen --hcmask FILE`** — batch table GENERATION (one mask per line).
- **`crackalack_verify --hcmask FILE DIR`** — batch VERIFICATION + campaign
  completeness (report masks with no matching table in DIR).
- Lookup: unchanged.
- Out of scope: `--increment` (variable length; N/A to fixed-length tables).

## hashcat `.hcmask` line format (target parity)

Each line is one of:
- A comment: first character is `#` → skipped. (A leading literal `#` in a
  charset is written `\#`; see escaping below.)
- Blank / whitespace-only → skipped.
- A mask, optionally preceded by up to 4 inline custom-charset fields, all
  comma-separated: `cs1,cs2,cs3,cs4,mask`. The **last** comma-field is the mask;
  each preceding field defines `?1`, `?2`, `?3`, `?4` in order.

Examples:
```
# comment
?u?l?l?l?l?l?d           mask only
?d?l,?1?1?1?1            ?1 = '?d?l', mask = ?1?1?1?1
abc,def,?1?2?1?2         ?1 = 'abc', ?2 = 'def', mask = ?1?2?1?2
\#literal,?1?1?1?1?1     ?1 = '#literal', mask = ?1?1?1?1?1
```

### Escaping rules (split layer only)

- `\,` → literal comma inside a field (not a field separator).
- `\#` → literal `#` (so a leading `\#` is not a comment).
- **All other `\` sequences pass through untouched** to `expand_charset_def`,
  which already handles `\xNN` (hex byte) and `\\` (literal backslash). This keeps
  the two escaping layers non-overlapping: the split layer only consumes `\,` and
  `\#`; hex/backslash escapes are resolved later by the existing charset expander.
- Within the mask field, `??` (literal `?`) is handled by the existing
  `mask_parse` as before.

## Architecture

A new shared parser module (`hcmask.c` / `hcmask.h`) plus a `--hcmask FILE` flag on
`crackalack_gen` and `crackalack_verify`. The parser only adds the line/field
split layer; all mask/charset semantics reuse the existing `expand_charset_def`,
`mask_parse`, and `mask_encode_charset_field`.

Rejected alternative: a shell wrapper calling `crackalack_gen` per line. Rejected
because the escaping/CSV rules are error-prone in bash and the chosen invocation
is a flag, not a wrapper.

## Components

### `hcmask.h` / `hcmask.c`

```c
typedef struct {
    char mask[MAX_MASK_STR_LEN];      /* the mask field, e.g. "?1?1?l?d" */
    char cc[4][MAX_CHARSET_LEN + 1];  /* inline custom-charset defs (raw) */
    int  has_cc[4];                   /* 1 if slot N was defined inline */
} HcmaskEntry;

/* Parse one .hcmask line into *out.
 * Returns:  1 = entry produced, 0 = skipped (comment/blank), -1 = parse error. */
int hcmask_parse_line(const char *line, HcmaskEntry *out);

/* Load a whole .hcmask file into a malloc'd array of entries (comments/blanks
 * skipped).  On any bad line, prints "<path>:<lineno>: <reason>" and returns -1.
 * Caller frees *out. Returns 0 on success, -1 on error. */
int hcmask_load(const char *path, HcmaskEntry **out, int *count);
```

`MAX_MASK_STR_LEN` sized to hold the longest legal mask (`MAX_PLAINTEXT_LEN`
positions × longest token `?H`/`?1` = a small fixed bound; use 128).

Field split: scan the line, splitting on unescaped `,`; unescape `\,`→`,` and a
leading `\#`→`#` in place. Max 5 fields; the last is `mask`, earlier fields fill
`cc[0..N-2]` with `has_cc` set. Empty mask field, or >5 fields → error.

### `crackalack_gen --hcmask FILE`

- Positional `min`/`max` length args are **ignored** under `--hcmask`; each
  entry's length is derived from its parsed mask (`min = max = mask.length`).
- The per-table generation core is factored into a routine invoked once per
  entry. Per entry: resolve custom charsets (inline `cc[]` where `has_cc`,
  else the global `-1..-4` flag for that slot, else unset), `mask_parse`,
  derive length, and generate the mask's self-describing table(s) using the
  shared positional params (`table_index`, `chain_len`, `num_chains`, `part`).
- **GPU context + the `crackalack_mask` kernel are set up once and reused across
  entries**; only the `mask_data`/`mask_lens` buffers and length change per entry.
  (If the existing main structure makes one-time setup impractical, per-entry
  setup is an acceptable fallback — GPU init amortizes against table-gen time.
  The plan will pick based on the actual code shape.)
- Charset resolution precedence: inline per-line charset wins; a global `-N`
  flag is the default for a slot no line defines inline.
- A mask referencing `?N` with neither inline nor global charset → error for that
  entry (reuses `mask_parse`'s existing "custom charset not provided" guard),
  reported with the entry's source line; batch aborts (fail fast) so a
  half-generated campaign is obvious.

### `crackalack_verify --hcmask FILE DIR`

- For each entry: reconstruct the expected filename charset field via
  `mask_encode_charset_field`, then locate matching table file(s) in `DIR`
  (glob on the `{hash}_{field}#{len}-{len}_*` prefix).
- Verify each matching table (reusing the existing `--mask` verify path, which
  already reconstructs the mask from the table filename).
- **Report entries with no matching table** as MISSING (campaign completeness),
  and return non-zero if any entry is missing or any table fails verification.

## Interaction / edge cases

- Duplicate mask lines → regenerate/resume the same table (existing `.state`
  resume behavior applies); not treated as an error.
- Two different masks of the same length never collide: the filename charset
  field differs per mask.
- `--hcmask` and a single `--mask` on the same command line → error (mutually
  exclusive; pick one input source).
- `--hcmask` + `--markov` → error for now (mask+Markov combo is a separate,
  later feature; masks and Markov remain mutually exclusive until then).
- NetNTLMv1 + `--hcmask` → error (masks unsupported for NetNTLMv1, per existing
  single-mask rule).

## Testing

### Unit (`test_hcmask.c`, run via `crackalack_cpu_tests`)

- Plain mask line → 1 entry, no custom charsets.
- Inline `?1` line (`?d?l,?1?1?1?1`) → `cc[0]="?d?l"`, `has_cc[0]=1`, mask
  `?1?1?1?1`.
- Multiple inline charsets (`abc,def,?1?2?1?2`) → cc[0]/cc[1] set.
- `\,` escaping → literal comma stays in the field, not a separator.
- `\#` leading escape → not a comment; `#` kept.
- Comment and blank lines → skipped (return 0).
- `> 5` fields → error; empty mask field → error.
- End-to-end parse→resolve: a parsed entry drives `mask_parse` correctly
  (positions/sizes/keyspace match), incl. an inline-charset entry.

### End-to-end (`scripts/bench/test_hcmask.sh`)

A small `.hcmask` (3 lines: a plain mask, an inline-custom-charset mask, and a
comment) →
- `crackalack_gen --hcmask` produces exactly one self-describing table per
  non-comment line (assert filenames);
- `crackalack_verify --hcmask FILE DIR` passes for the present tables and reports
  MISSING for a deliberately-removed one (non-zero exit);
- mint an in-table hash for one mask and confirm `crackalack_lookup` cracks it
  from the batch-generated directory (no mask flags).
Run on Metal (M3 Max) and gpuhost3 CUDA.

## Risk assessment

Low–moderate. The parser is small and fully unit-tested. The only structural risk
is factoring `crackalack_gen`'s per-table generation into a reusable per-entry
routine; the plan will scope that carefully and fall back to per-entry GPU setup
if one-time reuse is impractical. No kernel or `.rt`-format changes.

## Estimated effort

~1–2 days: parser module + tests, gen loop integration, verify batch + completeness,
e2e script. All host-side.
