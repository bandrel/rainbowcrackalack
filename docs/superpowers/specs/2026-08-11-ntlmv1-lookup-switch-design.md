# `-ntlmv1` end-to-end NetNTLMv1 cracking for `crackalack_lookup`

## Origin

GitLab issue #1 (gitrdun.trustedsec.net/justin.bollinger/rainbowcrackalack), filed by larry.spohn:
today, cracking a captured NetNTLMv1 hash with rainbow tables requires running `crackalack_lookup`
against block1/block2 by hand, then feeding the output potfile into `ntlmv1-multi` a second time to
recover the full 16-byte NTLM hash. This spec adds a `-ntlmv1` switch that does the split, lookup, and
reassembly in one pass.

## CLI surface

- New flag `-ntlmv1 <capture-or-file>` for `crackalack_lookup`, mutually exclusive with the existing
  positional single-hash/hash-file argument — it replaces that argument:
  `crackalack_lookup <rt_dir> -ntlmv1 <arg>`.
- `<arg>` is tried as a file path first (existing `stat`-based file-vs-string detection, same pattern
  already used for the positional argument). If it opens, every non-blank, non-`#`-prefixed line is
  parsed as one capture (batch mode). Otherwise `<arg>` itself is parsed as a single capture line.
- The existing `--challenge` flag is ignored in this mode — each capture line supplies its own
  challenge.

## Capture line format

Standard NetNTLMv1 capture format: `user::domain:LMresp:NTresp:challenge`, split on `:` into exactly
6 fields (`user`, workstation/blank, `domain`, `LMresp` = 48 hex chars, `NTresp` = 48 hex chars,
`challenge` = 16 hex chars). Malformed lines are reported with a per-line error and skipped; the rest
of a batch file continues processing.

## ESS detection and handling

NTLMv1 has two variants distinguishable from the capture itself:

- **Classic NTLMv1**: `LMresp` is a legitimate 24-byte LM-style response. The 8-byte challenge that was
  actually DES-encrypted is the raw server challenge field, used as-is.
- **NTLMv1 with Extended Session Security (ESS / "NTLM2 Session")**: `LMresp` bytes 8–23 are zero
  padding (bytes 0–7 hold an 8-byte client challenge). The value actually DES-encrypted is
  `MD5(server_challenge || client_challenge)[0:8]`, where the client challenge is generated randomly
  by the victim's OS on every authentication and is not attacker-controlled.

**Detection heuristic**: `LMresp` bytes 8–23 all zero AND bytes 0–7 nonzero ⇒ ESS. An all-zero 24-byte LM response (both ranges zero) is treated as classic/non-ESS, since it's more likely a legitimate "no LM response sent" classic capture than a true ESS capture whose randomly-generated 8-byte client challenge happened to be all zero.

**Why ESS can't use precomputed tables**: because the client challenge is random per-session, the
effective encrypted challenge differs on every capture regardless of what server challenge is forced.
A rainbow table is built for exactly one fixed challenge, so it can never match an ESS capture's
effective challenge (this is independent of which challenge the table was built with, including the
tool's default `1122334455667788`).

**Behavior**: on ESS detection, short-circuit immediately with a distinct message —
`"capture N: ESS/NTLM2-Session detected — client challenge is random per-session, precomputed tables cannot cover this, skipping"`
— and move to the next capture (or exit cleanly if it was the only one). Do not attempt the
challenge-matching step for ESS captures; the message must explain *why*, not report a generic
mismatch.

## Block split

For non-ESS captures, split the 24-byte `NTresp` into three 8-byte DES ciphertext blocks:
`block1 = NTresp[0:8]`, `block2 = NTresp[8:16]`, `block3 = NTresp[16:24]`. Hex-encode `block1`/`block2`
as 16-hex-char strings — the exact format the existing pipeline already accepts as NetNTLMv1
(`HASH_NETNTLMV1`) input.

## Table-challenge matching

Rainbow tables are challenge-specific (the challenge is encoded in the table filename), and the whole
lookup run resolves a single global challenge from the loaded tables (existing logic,
`crackalack_lookup.c:4395-4430`). Sequencing for `-ntlmv1` mode:

1. Load config groups and resolve `g_challenge` from the loaded tables first (existing logic,
   unmodified).
2. Filter parsed captures: only those whose effective challenge equals the resolved `g_challenge` get
   queued into `hashes[]`/`usernames[]` (both `block1_hex` and `block2_hex` per capture, same as
   today's NetNTLMv1 batch-hash input). All other non-ESS mismatches are skipped with:
   `"capture N: challenge <hex> doesn't match loaded tables' challenge <hex>, skipping"`.
3. The existing precompute/GPU-CPU lookup/false-alarm-check pipeline runs completely unmodified on the
   queued `block1_hex`/`block2_hex` entries.

A side table (capture index → `{block1_hex, block2_hex, block3 raw bytes, effective_challenge, user,
domain, nt_response_hex}`) is built during queuing and consulted in the post-lookup pass below. It is
independent of the existing `usernames[]` array (which stays untouched, used only for display as
today).

## Post-lookup reassembly

Hook point: immediately after `free_config_groups(&cg_head)` (`crackalack_lookup.c:4541`), before the
final report banner. At this point `ppi_head` is fully populated with final cracked state and nothing
has been freed yet.

For each queued capture:

1. `ppi_find(ppi_head, block1_hex)` and `ppi_find(ppi_head, block2_hex)`. If either has
   `plaintext == NULL`, print a partial-failure line (which block wasn't found in tables) and move on
   — no reassembly attempted.
2. If both cracked: brute-force `block3`'s 2-byte key against `block3` ciphertext + effective
   challenge. Loop all 65536 candidate 2-byte values, build each as a 7-byte plaintext key
   (`{hi, lo, 0, 0, 0, 0, 0}`), run through the existing `setup_des_key()` /
   `netntlmv1_hash()` (`cpu_rt_functions.c:443`, `:473`), compare ciphertext. Pure CPU, exhaustive, and
   therefore cannot fail — treat a miss as an internal-error assertion (would indicate a decoding bug
   elsewhere, not a real "not found" case).
3. Assemble `key1(7 bytes) || key2(7 bytes) || key3(2 bytes)` = 16-byte NTLM hash, hex-encode (32
   chars). Print:
   `HASH CRACKED (NetNTLMv1, full NTLM hash) => user:domain:<32-hex NTLM hash>`.
4. Append to the existing pot files via the existing `save_cracked_hash()` writer
   (`crackalack_lookup.c:3790-3872`): build a synthetic `precomputed_and_potential_indices` with
   `hash` = the **full original capture line** (`user::domain:LMresp:NTresp:challenge`, i.e. exactly
   what you'd feed hashcat `-m 5500`) and `plaintext` = the recovered 32-hex NTLM hash,
   `index_filename = NULL` (skips the on-disk cache unlink). Call `save_cracked_hash()` with
   `hash_type = HASH_NETNTLMV1` (not `HASH_NTLM`) so the JTR writer does *not* prepend `$NT$` — that
   prefix means "this hash field is an NT hash," which would be wrong here since the hash field is a
   full NetNTLMv1 capture line, not an NT hash. The resulting pot line matches real hashcat `-m 5500`
   pot syntax (`<capture line>:<cracked value>`), just with an NTLM hash in the crack-value slot
   instead of a human password — a real hashcat run against that same capture line with `-m 5500`
   would recognize it as already-cracked and display the NTLM hash in place of a password.

## New source files

`netntlmv1_capture.c` / `netntlmv1_capture.h` (new, root of repo alongside other host `.c`/`.h` files):

```c
typedef struct {
  char *user;
  char *domain;
  unsigned char lm_response[24];
  unsigned char nt_response[24];
  unsigned char server_challenge[8];
  int is_ess;
} netntlmv1_capture;

int  netntlmv1_parse_capture_line(const char *line, netntlmv1_capture *out, char *errbuf, size_t errbuf_len);
int  netntlmv1_capture_is_ess(const netntlmv1_capture *cap);              /* wraps the LMresp[8:24]==0 check */
void netntlmv1_effective_challenge(const netntlmv1_capture *cap, unsigned char out[8]); /* classic only; caller must not call this for ESS captures */
void netntlmv1_split_nt_response(const unsigned char nt_response[24],
                                  unsigned char block1[8], unsigned char block2[8], unsigned char block3[8]);
int  netntlmv1_bruteforce_block3(const unsigned char block3_ct[8], const unsigned char challenge[8],
                                  unsigned char out_2bytes[2]); /* returns 0 on success; exhaustive, should never fail */
void netntlmv1_assemble_ntlm_hash(const unsigned char key1[7], const unsigned char key2[7],
                                   const unsigned char key3[2], unsigned char out_ntlm[16]);
void netntlmv1_free_capture(netntlmv1_capture *cap);
```

`crackalack_lookup.c` changes:
- argv parsing (`main()`, near `crackalack_lookup.c:4187-4234`): new `-ntlmv1 <arg>` flag, mutually
  exclusive with the positional hash/file argument, sets a mode flag + stores the raw arg.
- New static function to load/parse captures from `<arg>` (file-or-string detection, one
  `netntlmv1_capture` per valid line), building the queued `hashes[]`/`usernames[]` arrays and the
  capture side-table, deferred until after `g_challenge` is resolved from loaded tables.
- New static function for the post-lookup reassembly pass described above, called once at the hook
  point.
- `print_usage_and_exit()` gets a `-ntlmv1` usage line.

## Testing

- `tests/test_netntlmv1_capture.c` (new), covering:
  - Valid capture line parsing (classic and ESS).
  - Malformed lines (wrong field count, bad hex length) rejected with an error, not a crash.
  - ESS detection: LMresp with zero padding at [8:24] → ESS; a full-entropy LMresp → classic.
  - Block split correctness against a known 24-byte NTresp.
  - Block3 brute-force round-trip: pick a known 7-byte key, compute its DES ciphertext via
    `netntlmv1_hash()`, confirm `netntlmv1_bruteforce_block3()` recovers the same 2 trailing bytes.
  - Full assembly: known key1/key2/key3 → expected 16-byte NTLM hash.
- One end-to-end test (extends existing NetNTLMv1 test harness): generate tiny NetNTLMv1 tables for a
  known challenge + known plaintext, synthesize a capture line whose NT response is the DES encoding
  of that plaintext's derived hash under that challenge, run `-ntlmv1` against it, confirm the
  reassembled 32-hex NTLM hash matches the known plaintext's real NTLM hash.

## Out of scope

- Batch captures with heterogeneous challenges beyond filtering (no multi-challenge/multi-table-set
  run in one invocation — matches the existing single-global-challenge architecture).
- Automatically chaining into an NTLM-table lookup pass for the recovered hash (user runs that
  separately if desired).
