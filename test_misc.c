/*
 * Rainbow Crackalack: test_misc.c
 * CPU-only tests for misc.c, hash_validate.c, and charset.c helper functions.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stdint.h>

#include "charset.h"
#include "cpu_rt_functions.h"
#include "fa_batch.h"
#include "hash_validate.h"
#include "misc.h"
#include "shared.h"
#include "test_misc.h"


/* --- Group A: str_ends_with --- */
static int group_a(void)
{
    int ok = 1;

    if (str_ends_with("foo.rt", ".rt") != 1)     { fprintf(stderr, "SEW-01 failed\n"); ok = 0; }
    if (str_ends_with("foo.rtc", ".rtc") != 1)   { fprintf(stderr, "SEW-02 failed\n"); ok = 0; }
    if (str_ends_with("foo.rtc", ".rt") != 0)    { fprintf(stderr, "SEW-03 failed\n"); ok = 0; }
    if (str_ends_with("foo", ".rt") != 0)         { fprintf(stderr, "SEW-04 failed\n"); ok = 0; }
    if (str_ends_with(".rt", ".rt") != 1)         { fprintf(stderr, "SEW-05 failed\n"); ok = 0; }
    if (str_ends_with(NULL, ".rt") != 0)          { fprintf(stderr, "SEW-06 failed\n"); ok = 0; }
    if (str_ends_with("foo.rt", NULL) != 0)       { fprintf(stderr, "SEW-07 failed\n"); ok = 0; }

    return ok;
}


/* --- Group B: str_to_lowercase --- */
static int group_b(void)
{
    int ok = 1;
    char buf[32];

    strncpy(buf, "HELLO", sizeof(buf));
    str_to_lowercase(buf);
    if (strcmp(buf, "hello") != 0) { fprintf(stderr, "STL-01 failed: got \"%s\"\n", buf); ok = 0; }

    strncpy(buf, "hElLo", sizeof(buf));
    str_to_lowercase(buf);
    if (strcmp(buf, "hello") != 0) { fprintf(stderr, "STL-02 failed: got \"%s\"\n", buf); ok = 0; }

    strncpy(buf, "already", sizeof(buf));
    str_to_lowercase(buf);
    if (strcmp(buf, "already") != 0) { fprintf(stderr, "STL-03 failed: got \"%s\"\n", buf); ok = 0; }

    /* STL-04: empty string - strlen("") == 0 so loop never executes; safe.
     * Note: str_to_lowercase has no NULL guard; passing NULL would crash. */
    strncpy(buf, "", sizeof(buf));
    str_to_lowercase(buf);
    if (strcmp(buf, "") != 0) { fprintf(stderr, "STL-04 failed: got \"%s\"\n", buf); ok = 0; }

    return ok;
}


/* --- Group C: is_ntlm8 / is_ntlm9 --- */
static int group_c(void)
{
    int ok = 1;

    /* is_ntlm8 - matches any NTLM ascii-32-95 8-char config regardless of
     * chain_len or reduction_offset (dynamic chain_len support). */
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 0, 422000) != 1)
        { fprintf(stderr, "IN8-01 failed\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 0, 100000) != 1)
        { fprintf(stderr, "IN8-02 failed: any chain_len should be accepted\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_NUMERIC, 8, 8, 0, 422000) != 0)
        { fprintf(stderr, "IN8-03 failed: wrong charset accepted\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 65536, 422000) != 1)
        { fprintf(stderr, "IN8-04 failed: any reduction_offset should be accepted\n"); ok = 0; }

    /* is_ntlm9 */
    if (is_ntlm9(HASH_NTLM, CHARSET_ASCII_32_95, 9, 9, 0, 803000) != 1)
        { fprintf(stderr, "IN9-01 failed\n"); ok = 0; }
    if (is_ntlm9(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 0, 803000) != 0)
        { fprintf(stderr, "IN9-02 failed: wrong plaintext_len accepted\n"); ok = 0; }
    if (is_ntlm9(HASH_LM, CHARSET_ASCII_32_95, 9, 9, 0, 803000) != 0)
        { fprintf(stderr, "IN9-03 failed: wrong hash_type accepted\n"); ok = 0; }
    if (is_ntlm9(HASH_NTLM, CHARSET_ASCII_32_95, 9, 9, 65536, 803000) != 0)
        { fprintf(stderr, "IN9-04 failed: non-zero reduction_offset accepted\n"); ok = 0; }

    return ok;
}


/* --- Group D: get_rt_log_filename --- */
static int group_d(void)
{
    int ok = 1;
    char result[256] = {0};

    get_rt_log_filename(result, sizeof(result), "foo.rt");
    if (strcmp(result, "foo.rt.log") != 0)
        { fprintf(stderr, "GRLF-01 failed: got \"%s\"\n", result); ok = 0; }

    /* GRLF-02: path with directory separators. */
    memset(result, 0, sizeof(result));
    get_rt_log_filename(result, sizeof(result), "/path/to/table.rt");
    if (strcmp(result, "/path/to/table.rt.log") != 0)
        { fprintf(stderr, "GRLF-02 failed: got \"%s\"\n", result); ok = 0; }

    return ok;
}


/* --- Group E: filepath_join --- */
static int group_e(void)
{
    int ok = 1;
    char result[256] = {0};

    filepath_join(result, sizeof(result), "/dir", "file.rt");
    if (strcmp(result, "/dir/file.rt") != 0)
        { fprintf(stderr, "FJ-01 failed: got \"%s\"\n", result); ok = 0; }

    memset(result, 0, sizeof(result));
    filepath_join(result, sizeof(result), ".", "table.rt");
    if (strcmp(result, "./table.rt") != 0)
        { fprintf(stderr, "FJ-02 failed: got \"%s\"\n", result); ok = 0; }

    return ok;
}


/* --- Group F: hash_str_to_type (declared in hash_validate.h) --- */
static int group_f(void)
{
    int ok = 1;

    if (hash_str_to_type("lm") != HASH_LM)
        { fprintf(stderr, "HSTT-01 failed\n"); ok = 0; }
    if (hash_str_to_type("ntlm") != HASH_NTLM)
        { fprintf(stderr, "HSTT-02 failed\n"); ok = 0; }
    if (hash_str_to_type("netntlmv1") != HASH_NETNTLMV1)
        { fprintf(stderr, "HSTT-03 failed\n"); ok = 0; }
    if (hash_str_to_type("sha1") != HASH_UNDEFINED)
        { fprintf(stderr, "HSTT-04 failed\n"); ok = 0; }
    if (hash_str_to_type("") != HASH_UNDEFINED)
        { fprintf(stderr, "HSTT-05 failed\n"); ok = 0; }

    return ok;
}


/* --- Group G: validate_charset --- */
static int group_g(void)
{
    int ok = 1;

    if (validate_charset("ascii-32-95") == NULL)
        { fprintf(stderr, "VC-01 failed: ascii-32-95 not found\n"); ok = 0; }
    if (validate_charset("numeric") == NULL)
        { fprintf(stderr, "VC-02 failed: numeric not found\n"); ok = 0; }
    if (validate_charset("mixalpha-numeric") == NULL)
        { fprintf(stderr, "VC-03 failed: mixalpha-numeric not found\n"); ok = 0; }
    if (validate_charset("invalid-charset") != NULL)
        { fprintf(stderr, "VC-04 failed: invalid name accepted\n"); ok = 0; }
    if (validate_charset("") != NULL)
        { fprintf(stderr, "VC-05 failed: empty string accepted\n"); ok = 0; }

    return ok;
}


/* --- Group H: parse_rt_params --- */
static int group_h(void)
{
    int ok = 1;
    rt_parameters p;
    char fn[256];

    /* PRP-01: valid NTLM8 */
    strncpy(fn, "ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-01 failed: NTLM8 not parsed\n"); ok = 0; }
    else if (p.hash_type != HASH_NTLM || p.plaintext_len_min != 8 ||
             p.plaintext_len_max != 8 || p.chain_len != 422000 ||
             p.num_chains != 67108864 || p.table_index != 0)
        { fprintf(stderr, "PRP-01b failed: NTLM8 field mismatch\n"); ok = 0; }

    /* PRP-02: valid NTLM9 */
    strncpy(fn, "ntlm_ascii-32-95#9-9_0_803000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-02 failed: NTLM9 not parsed\n"); ok = 0; }
    else if (p.plaintext_len_min != 9 || p.plaintext_len_max != 9 ||
             p.chain_len != 803000)
        { fprintf(stderr, "PRP-02b failed: NTLM9 field mismatch\n"); ok = 0; }

    /* PRP-03: absolute path stripped */
    strncpy(fn, "/path/to/ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-03 failed: absolute path not handled\n"); ok = 0; }

    /* PRP-04: wrong extension */
    strncpy(fn, "ntlm_ascii-32-95#8-8_0_422000x67108864_0.txt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-04 failed: .txt extension accepted\n"); ok = 0; }

    /* PRP-05: unknown hash type */
    strncpy(fn, "sha1_ascii-32-95#8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-05 failed: sha1 hash accepted\n"); ok = 0; }

    /* PRP-06: malformed (missing '#') */
    strncpy(fn, "ntlm-ascii-32-95-8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-06 failed: malformed filename accepted\n"); ok = 0; }

    /* PRP-07: zero chain_len */
    strncpy(fn, "ntlm_ascii-32-95#8-8_0_0x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-07 failed: zero chain_len accepted\n"); ok = 0; }

    /* PRP-08: plaintext_len_max == MAX_PLAINTEXT_LEN (16) - regression for
     * bug where '<' was used instead of '<=' in parse_rt_params validation. */
    strncpy(fn, "ntlm_ascii-32-95#16-16_0_100000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-08 failed: plaintext_len_max==16 rejected\n"); ok = 0; }
    else if (p.plaintext_len_max != 16)
        { fprintf(stderr, "PRP-08b failed: plaintext_len_max=%u\n", p.plaintext_len_max); ok = 0; }

    return ok;
}


/* --- Group I: byte charset regression tests --- */
static int group_i(void)
{
    int ok = 1;

    /* BCS-01: CHARSET_BYTE_LEN must be exactly 256 (was 257 due to duplicate
     * trailing \x00 in the string literal). */
    if (CHARSET_BYTE_LEN != 256)
        { fprintf(stderr, "BCS-01 failed: CHARSET_BYTE_LEN=%d, expected 256\n",
                  (int)CHARSET_BYTE_LEN); ok = 0; }

    /* BCS-02: validate_charset("byte") returns non-NULL. */
    if (validate_charset("byte") == NULL)
        { fprintf(stderr, "BCS-02 failed: byte charset not found\n"); ok = 0; }

    /* BCS-03: fill_plaintext_space_table with charset_len=256 and 7-7 must
     * equal 256^7 = 72057594037927936. This catches the old +1 bug where
     * charset_len was erroneously set to 257. */
    {
        uint64_t pspace_up_to[MAX_PLAINTEXT_LEN] = {0};
        uint64_t pspace = fill_plaintext_space_table(256, 7, 7, pspace_up_to);
        uint64_t expected = 1;
        int i;
        for (i = 0; i < 7; i++) expected *= 256;
        if (pspace != expected)
            { fprintf(stderr, "BCS-03 failed: pspace=%"PRIu64", expected %"PRIu64"\n",
                      pspace, expected); ok = 0; }
    }

    /* BCS-04: fill_plaintext_space_table with ascii-32-95 (len=95) and 8-8
     * must NOT use 96 (the old strlen+1 bug). */
    {
        uint64_t pspace_up_to[MAX_PLAINTEXT_LEN] = {0};
        uint64_t pspace = fill_plaintext_space_table(95, 8, 8, pspace_up_to);
        uint64_t pspace_wrong = 0;
        uint64_t pspace_up_to_wrong[MAX_PLAINTEXT_LEN] = {0};
        pspace_wrong = fill_plaintext_space_table(96, 8, 8, pspace_up_to_wrong);
        if (pspace == pspace_wrong)
            { fprintf(stderr, "BCS-04 failed: pspace with 95 == pspace with 96\n"); ok = 0; }
        /* 95^8 = 6634204312890625 */
        if (pspace != 6634204312890625UL)
            { fprintf(stderr, "BCS-04b failed: pspace=%"PRIu64", expected 6634204312890625\n",
                      pspace); ok = 0; }
    }

    /* BCS-05: parse_rt_params for byte charset table. */
    {
        rt_parameters p;
        char fn[256];
        strncpy(fn, "netntlmv1_byte#7-7_0_881689x134217668_0.rt", sizeof(fn));
        parse_rt_params(&p, fn);
        if (!p.parsed)
            { fprintf(stderr, "BCS-05 failed: byte charset table not parsed\n"); ok = 0; }
        else if (p.hash_type != HASH_NETNTLMV1 || p.plaintext_len_min != 7 ||
                 p.plaintext_len_max != 7)
            { fprintf(stderr, "BCS-05b failed: field mismatch\n"); ok = 0; }
    }

    return ok;
}


/* Construct a minimal ppi node for testing.  Caller frees. */
static precomputed_and_potential_indices *mk_ppi(const char *hash_hex,
                                                 const uint64_t *starts,
                                                 const unsigned int *positions,
                                                 unsigned int n,
                                                 int cracked) {
  precomputed_and_potential_indices *p = calloc(1, sizeof(*p));
  p->hash = strdup(hash_hex);
  if (n > 0) {
    p->potential_start_indices          = calloc(n, sizeof(uint64_t));
    p->potential_start_index_positions  = calloc(n, sizeof(unsigned int));
    memcpy(p->potential_start_indices,         starts,    n * sizeof(uint64_t));
    memcpy(p->potential_start_index_positions, positions, n * sizeof(unsigned int));
    p->num_potential_start_indices      = n;
    p->potential_start_indices_size     = n;
  }
  if (cracked) p->plaintext = strdup("CRACKED");
  return p;
}

static void free_ppi(precomputed_and_potential_indices *p) {
  free(p->hash);
  free(p->username);
  free(p->precomputed_end_indices);
  free(p->potential_start_indices);
  free(p->potential_start_index_positions);
  free(p->plaintext);
  free(p);
}

static int group_j(void) {
  int ok = 1;
  fa_batch_t b = {0};

  /* J-01: init/free is safe and idempotent. */
  if (fa_batch_init(&b, 0, 0) != 0)        { fprintf(stderr, "J-01a init failed\n"); ok = 0; }
  if (b.flush_threshold != 16384)          { fprintf(stderr, "J-01b default threshold wrong\n"); ok = 0; }
  fa_batch_free(&b);
  fa_batch_free(&b);  /* second free should be a no-op */

  /* J-02: explicit threshold honored. */
  fa_batch_init(&b, 100, 0);
  if (b.flush_threshold != 100)            { fprintf(stderr, "J-02 explicit threshold wrong\n"); ok = 0; }

  /* J-03: empty batch does not flush even with force. */
  if (fa_batch_should_flush(&b, 0))        { fprintf(stderr, "J-03a empty flushed (no force)\n"); ok = 0; }
  if (fa_batch_should_flush(&b, 1))        { fprintf(stderr, "J-03b empty flushed (force)\n"); ok = 0; }

  /* J-04: append a single table's candidates. */
  uint64_t s[3]     = { 11, 22, 33 };
  unsigned int p[3] = {  1,  2,  3 };
  precomputed_and_potential_indices *q =
    mk_ppi("0123456789abcdef0123456789abcdef", s, p, 3, 0);

  if (fa_batch_append(&b, q, 0, 1000) != 0)    { fprintf(stderr, "J-04a append failed\n"); ok = 0; }
  if (b.num_candidates != 3)                   { fprintf(stderr, "J-04b count=%u expected 3\n", b.num_candidates); ok = 0; }
  if (b.tables_in_batch != 1)                  { fprintf(stderr, "J-04c tables=%u expected 1\n", b.tables_in_batch); ok = 0; }
  if (b.start_indices[0] != 11 ||
      b.start_indices[1] != 22 ||
      b.start_indices[2] != 33)                { fprintf(stderr, "J-04d start_indices mismatch\n"); ok = 0; }
  if (b.start_index_positions[1] != 2)         { fprintf(stderr, "J-04e position mismatch\n"); ok = 0; }
  if (b.ppi_refs[0] != q)                      { fprintf(stderr, "J-04f ppi_ref mismatch\n"); ok = 0; }

  /* J-05: should_flush is false until threshold reached. */
  if (fa_batch_should_flush(&b, 0))            { fprintf(stderr, "J-05a flushed below threshold\n"); ok = 0; }
  if (!fa_batch_should_flush(&b, 1))           { fprintf(stderr, "J-05b force did not flush\n"); ok = 0; }

  /* J-06: reset keeps capacity, zeroes count. */
  unsigned int cap_before = b.capacity;
  fa_batch_reset(&b);
  if (b.num_candidates != 0)                   { fprintf(stderr, "J-06a num_candidates not zero\n"); ok = 0; }
  if (b.tables_in_batch != 0)                  { fprintf(stderr, "J-06b tables_in_batch not zero\n"); ok = 0; }
  if (b.capacity != cap_before)                { fprintf(stderr, "J-06c capacity changed\n"); ok = 0; }

  /* J-07: cracked ppi is skipped on append. */
  precomputed_and_potential_indices *cracked =
    mk_ppi("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", s, p, 3, 1);
  fa_batch_append(&b, cracked, 0, 1000);
  if (b.num_candidates != 0)                   { fprintf(stderr, "J-07a cracked counted (%u)\n", b.num_candidates); ok = 0; }
  if (b.tables_in_batch != 1)                  { fprintf(stderr, "J-07b tables_in_batch not 1\n"); ok = 0; }

  /* J-08: threshold flush triggers on the right side of equality. */
  fa_batch_t b2 = {0};
  fa_batch_init(&b2, 3, 0);
  fa_batch_append(&b2, q, 0, 1000);            /* contributes 3 */
  if (!fa_batch_should_flush(&b2, 0))          { fprintf(stderr, "J-08 should flush at threshold\n"); ok = 0; }

  /* J-09: --fa-batch=1 (threshold 1) flushes on any non-empty batch. */
  fa_batch_t b3 = {0};
  fa_batch_init(&b3, 1, 0);
  fa_batch_append(&b3, q, 0, 1000);
  if (!fa_batch_should_flush(&b3, 0))          { fprintf(stderr, "J-09 fa-batch=1 should flush\n"); ok = 0; }

  /* J-10: sort orders all four parallel arrays by start_index_positions. */
  fa_batch_t b4 = {0};
  fa_batch_init(&b4, 16384, 0);
  uint64_t   s4[5]  = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 };
  unsigned int p4[5] = {  500,  100,  300,  900,  200 };
  precomputed_and_potential_indices *q4 =
    mk_ppi("0123456789abcdef0123456789abcdef", s4, p4, 5, 0);
  fa_batch_append(&b4, q4, 0, 1000);
  fa_batch_sort_by_position(&b4);

  if (b4.num_candidates != 5)                  { fprintf(stderr, "J-10a count wrong\n"); ok = 0; }
  if (b4.start_index_positions[0] != 100 ||
      b4.start_index_positions[1] != 200 ||
      b4.start_index_positions[2] != 300 ||
      b4.start_index_positions[3] != 500 ||
      b4.start_index_positions[4] != 900)      { fprintf(stderr, "J-10b positions not sorted\n"); ok = 0; }
  /* start_indices must move with positions: position 100 maps to start 0xB0, etc. */
  if (b4.start_indices[0] != 0xB0 ||
      b4.start_indices[1] != 0xE0 ||
      b4.start_indices[2] != 0xC0 ||
      b4.start_indices[3] != 0xA0 ||
      b4.start_indices[4] != 0xD0)             { fprintf(stderr, "J-10c start_indices misaligned\n"); ok = 0; }
  /* ppi_refs all point to the same q4 in this case. */
  if (b4.ppi_refs[0] != q4 ||
      b4.ppi_refs[4] != q4)                    { fprintf(stderr, "J-10d ppi_refs lost\n"); ok = 0; }

  fa_batch_free(&b4);
  free_ppi(q4);

  /* J-11: sort on empty batch is a no-op (no crash). */
  fa_batch_t b5 = {0};
  fa_batch_init(&b5, 16384, 0);
  fa_batch_sort_by_position(&b5);
  if (b5.num_candidates != 0)                  { fprintf(stderr, "J-11 empty batch mutated\n"); ok = 0; }
  fa_batch_free(&b5);

  fa_batch_free(&b);
  fa_batch_free(&b2);
  fa_batch_free(&b3);
  free_ppi(q);
  free_ppi(cracked);
  return ok;
}


/* --- Group K: compute_batch_chunk_size --- */
static int group_k(void)
{
    int ok = 1;

    /* CBC-01: a single hash clamps to the 8192 maximum. */
    if (compute_batch_chunk_size(1) != 8192)
        { fprintf(stderr, "CBC-01 failed: got %u, expected 8192\n",
                  compute_batch_chunk_size(1)); ok = 0; }

    /* CBC-02: 2 hashes preserves the measured sweet spot (16384/2 = 8192). */
    if (compute_batch_chunk_size(2) != 8192)
        { fprintf(stderr, "CBC-02 failed: got %u, expected 8192\n",
                  compute_batch_chunk_size(2)); ok = 0; }

    /* CBC-03: mid-range hash count scales down (16384/5 = 3276). */
    if (compute_batch_chunk_size(5) != 3276)
        { fprintf(stderr, "CBC-03 failed: got %u, expected 3276\n",
                  compute_batch_chunk_size(5)); ok = 0; }

    /* CBC-04: at the floor boundary (16384/64 = 256). */
    if (compute_batch_chunk_size(64) != 256)
        { fprintf(stderr, "CBC-04 failed: got %u, expected 256\n",
                  compute_batch_chunk_size(64)); ok = 0; }

    /* CBC-05: large hash count clamps to the 256 minimum (16384/100 = 163). */
    if (compute_batch_chunk_size(100) != 256)
        { fprintf(stderr, "CBC-05 failed: got %u, expected 256\n",
                  compute_batch_chunk_size(100)); ok = 0; }

    /* CBC-06: very large hash count still clamps to 256, never 0. */
    if (compute_batch_chunk_size(100000) != 256)
        { fprintf(stderr, "CBC-06 failed: got %u, expected 256\n",
                  compute_batch_chunk_size(100000)); ok = 0; }

    /* CBC-07: zero hashes is safe (no divide-by-zero) and yields the max. */
    if (compute_batch_chunk_size(0) != 8192)
        { fprintf(stderr, "CBC-07 failed: got %u, expected 8192\n",
                  compute_batch_chunk_size(0)); ok = 0; }

    return ok;
}


/* --- Group L: build_precompute_cache_charset --- */
static int group_l(void)
{
    int ok = 1;
    char out[128];

    /* BPCC-01: default challenge -> charset verbatim, no suffix. */
    build_precompute_cache_charset(out, sizeof(out), "byte", NETNTLMV1_DEFAULT_CHALLENGE);
    if (strcmp(out, "byte") != 0)
        { fprintf(stderr, "BPCC-01 failed: got \"%s\"\n", out); ok = 0; }

    /* BPCC-02: non-default challenge -> "<charset>-chal<hex>". */
    unsigned char chal[8] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11};
    build_precompute_cache_charset(out, sizeof(out), "byte", chal);
    if (strcmp(out, "byte-chalaabbccddeeff0011") != 0)
        { fprintf(stderr, "BPCC-02 failed: got \"%s\"\n", out); ok = 0; }

    /* BPCC-03: a non-"byte" charset is preserved before the suffix. */
    build_precompute_cache_charset(out, sizeof(out), "ascii-32-95", chal);
    if (strcmp(out, "ascii-32-95-chalaabbccddeeff0011") != 0)
        { fprintf(stderr, "BPCC-03 failed: got \"%s\"\n", out); ok = 0; }

    /* BPCC-04: tiny buffer never overflows and stays NUL-terminated. */
    char small[8];
    build_precompute_cache_charset(small, sizeof(small), "byte", chal);
    if (small[sizeof(small) - 1] != '\0')
        { fprintf(stderr, "BPCC-04 failed: not NUL-terminated\n"); ok = 0; }
    if (strncmp(small, "byte-ch", 7) != 0)
        { fprintf(stderr, "BPCC-04 failed: bad truncated prefix \"%s\"\n", small); ok = 0; }

    return ok;
}


/* --- Group M: parse_hash_file_data --- */
static int group_m(void)
{
    int ok = 1;
    char **hashes = NULL;
    char **usernames = NULL;
    unsigned int num_hashes = 0, previously_cracked = 0;
    int file_format = 0;
    unsigned int i;

    /* M-01: PLAIN format, 3 hashes, uppercase input → lowercased output. */
    {
        char input[] = "AABBCCDDEEFF00112233445566778899\nBBCCDDEEFF00112233445566778899AA\nCCDDEEFF00112233445566778899AABB\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M01: parse returned error\n"); ok = 0; }
        else {
            if (num_hashes != 3)
                { fprintf(stderr, "FAIL PHF-M01: expected 3 hashes, got %u\n", num_hashes); ok = 0; }
            if (file_format != HASH_FILE_FORMAT_PLAIN)
                { fprintf(stderr, "FAIL PHF-M01: wrong format %d\n", file_format); ok = 0; }
            if (hashes && num_hashes >= 1 && strcmp(hashes[0], "aabbccddeeff00112233445566778899") != 0)
                { fprintf(stderr, "FAIL PHF-M01: hashes[0]=\"%s\"\n", hashes[0]); ok = 0; }
        }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-02: CRLF line endings → no trailing \r in stored hash. */
    {
        char input[] = "AABBCCDDEEFF00112233445566778899\r\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M02: parse returned error\n"); ok = 0; }
        else {
            if (num_hashes != 1)
                { fprintf(stderr, "FAIL PHF-M02: expected 1 hash, got %u\n", num_hashes); ok = 0; }
            if (hashes && num_hashes >= 1 && strcmp(hashes[0], "aabbccddeeff00112233445566778899") != 0)
                { fprintf(stderr, "FAIL PHF-M02: hashes[0]=\"%s\"\n", hashes[0]); ok = 0; }
        }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-03: Empty lines interspersed → skipped. */
    {
        char input[] = "aabbccddeeff00112233445566778899\n\nbbccddeeff00112233445566778899aa\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M03: parse returned error\n"); ok = 0; }
        else if (num_hashes != 2)
            { fprintf(stderr, "FAIL PHF-M03: expected 2 hashes, got %u\n", num_hashes); ok = 0; }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-04: Already-cracked skip (PLAIN). */
    {
        char input[] = "aabbccddeeff00112233445566778899\nbbccddeeff00112233445566778899aa\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "aabbccddeeff00112233445566778899", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M04: parse returned error\n"); ok = 0; }
        else {
            if (num_hashes != 1)
                { fprintf(stderr, "FAIL PHF-M04: expected 1 hash, got %u\n", num_hashes); ok = 0; }
            if (previously_cracked != 1)
                { fprintf(stderr, "FAIL PHF-M04: expected previously_cracked=1, got %u\n", previously_cracked); ok = 0; }
            if (hashes && num_hashes >= 1 && strcmp(hashes[0], "bbccddeeff00112233445566778899aa") != 0)
                { fprintf(stderr, "FAIL PHF-M04: hashes[0]=\"%s\"\n", hashes[0]); ok = 0; }
        }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-05: PWDUMP format. */
    {
        char input[] = "Administrator:500:AABBCCDDEEFF00112233445566778899:aabbccddeeff00112233445566778899:::\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M05: parse returned error\n"); ok = 0; }
        else {
            if (num_hashes != 1)
                { fprintf(stderr, "FAIL PHF-M05: expected 1 hash, got %u\n", num_hashes); ok = 0; }
            if (file_format != HASH_FILE_FORMAT_PWDUMP)
                { fprintf(stderr, "FAIL PHF-M05: wrong format %d\n", file_format); ok = 0; }
            if (usernames && num_hashes >= 1 && strcmp(usernames[0], "Administrator") != 0)
                { fprintf(stderr, "FAIL PHF-M05: usernames[0]=\"%s\"\n", usernames[0]); ok = 0; }
            if (hashes && num_hashes >= 1 && strcmp(hashes[0], "aabbccddeeff00112233445566778899") != 0)
                { fprintf(stderr, "FAIL PHF-M05: hashes[0]=\"%s\"\n", hashes[0]); ok = 0; }
        }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-06: NULL pot_contents treated as empty (no matches). */
    {
        char input[] = "aabbccddeeff00112233445566778899\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, NULL, &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M06: parse returned error\n"); ok = 0; }
        else if (num_hashes != 1)
            { fprintf(stderr, "FAIL PHF-M06: expected 1 hash, got %u\n", num_hashes); ok = 0; }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-07: pot_contents=="" (empty string, valid pointer) — no false match. */
    {
        char input[] = "aabbccddeeff00112233445566778899\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M07: parse returned error\n"); ok = 0; }
        else if (num_hashes != 1)
            { fprintf(stderr, "FAIL PHF-M07: expected 1 hash, got %u\n", num_hashes); ok = 0; }
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-08: Regression for NUL-termination — pot_contents is exactly strlen+1 bytes. */
    {
        char input[] = "aabbccddeeff00112233445566778899\n";
        char *pot = strdup("deadbeefdeadbeefdeadbeefdeadbeef");
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, pot, &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) != 0)
            { fprintf(stderr, "FAIL PHF-M08: parse returned error\n"); ok = 0; }
        else {
            if (num_hashes != 1)
                { fprintf(stderr, "FAIL PHF-M08: expected 1 hash, got %u\n", num_hashes); ok = 0; }
            if (previously_cracked != 0)
                { fprintf(stderr, "FAIL PHF-M08: expected previously_cracked=0, got %u\n", previously_cracked); ok = 0; }
        }
        free(pot);
        if (hashes) { for (i = 0; i < num_hashes; i++) free(hashes[i]); free(hashes); }
        if (usernames) { for (i = 0; i < num_hashes; i++) free(usernames[i]); free(usernames); }
    }

    /* M-09: Error path — PWDUMP first line valid, second line malformed (hash
     * cannot be extracted: fewer than 4 colons so hash_start/hash_end stay 0).
     * Expects non-zero return AND out-params NULLed/zeroed. */
    {
        /* First line is well-formed PWDUMP (6 colons); second line has only 2
         * colons so the colon-scanning loop cannot reach the 3rd/4th colon. */
        char input[] =
            "Administrator:500:AABBCCDDEEFF00112233445566778899:aabbccddeeff00112233445566778899:::\n"
            "BADLINE:oops\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) == 0)
            { fprintf(stderr, "FAIL PHF-M09: expected error, got success\n"); ok = 0; }
        if (hashes != NULL)
            { fprintf(stderr, "FAIL PHF-M09: out_hashes not NULL on error\n"); ok = 0; }
        if (usernames != NULL)
            { fprintf(stderr, "FAIL PHF-M09: out_usernames not NULL on error\n"); ok = 0; }
        if (num_hashes != 0)
            { fprintf(stderr, "FAIL PHF-M09: out_num_hashes not 0 on error (got %u)\n", num_hashes); ok = 0; }
        /* Nothing to free: function must have self-cleaned. */
    }

    /* M-10: Error path — PWDUMP line with NT-hash field that is not 32 chars.
     * Expects non-zero return AND out-params NULLed/zeroed. */
    {
        /* The NT-hash field (4th field) is only 8 characters. */
        char input[] = "Administrator:500:AABBCCDD:SHORT:::\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) == 0)
            { fprintf(stderr, "FAIL PHF-M10: expected error, got success\n"); ok = 0; }
        if (hashes != NULL)
            { fprintf(stderr, "FAIL PHF-M10: out_hashes not NULL on error\n"); ok = 0; }
        if (usernames != NULL)
            { fprintf(stderr, "FAIL PHF-M10: out_usernames not NULL on error\n"); ok = 0; }
        if (num_hashes != 0)
            { fprintf(stderr, "FAIL PHF-M10: out_num_hashes not 0 on error (got %u)\n", num_hashes); ok = 0; }
    }

    /* M-11: Error path — unrecognized format (2 colons in first line, neither
     * 0 nor 6).  Expects non-zero return AND out-params NULLed/zeroed. */
    {
        char input[] = "foo:bar:baz\n";
        hashes = NULL; usernames = NULL; num_hashes = 0; previously_cracked = 0; file_format = 0;
        if (parse_hash_file_data(input, "", &hashes, &usernames, &num_hashes, &previously_cracked, &file_format) == 0)
            { fprintf(stderr, "FAIL PHF-M11: expected error, got success\n"); ok = 0; }
        if (hashes != NULL)
            { fprintf(stderr, "FAIL PHF-M11: out_hashes not NULL on error\n"); ok = 0; }
        if (usernames != NULL)
            { fprintf(stderr, "FAIL PHF-M11: out_usernames not NULL on error\n"); ok = 0; }
        if (num_hashes != 0)
            { fprintf(stderr, "FAIL PHF-M11: out_num_hashes not 0 on error (got %u)\n", num_hashes); ok = 0; }
    }

    return ok;
}


/* --- Group N: fa_harvest_candidate_index --- */
static int group_n(void)
{
    int ok = 1;
    unsigned int out = 0xdeadbeef;

    /* N-01..05: in-bounds cases for num_candidates=5, r=0..4 */
    {
        unsigned int r;
        for (r = 0; r < 5; r++) {
            out = 0xdeadbeef;
            if (fa_harvest_candidate_index(r, 5, &out) != 1) {
                fprintf(stderr, "FAIL N-0%u: expected return 1 for r=%u num_candidates=5\n", r+1, r);
                ok = 0;
            } else if (out != r) {
                fprintf(stderr, "FAIL N-0%u: expected out_index=%u got %u\n", r+1, r, out);
                ok = 0;
            }
        }
    }

    /* N-06: out of bounds: r==5, num_candidates=5 -> returns 0, out untouched */
    out = 0xdeadbeef;
    if (fa_harvest_candidate_index(5, 5, &out) != 0) {
        fprintf(stderr, "FAIL N-06: expected return 0 for r=5 num_candidates=5\n");
        ok = 0;
    }
    if (out != 0xdeadbeef) {
        fprintf(stderr, "FAIL N-06: out_index overwritten on failure path (got 0x%x)\n", out);
        ok = 0;
    }

    /* N-07: out of bounds: r==100, num_candidates=5 -> returns 0, out untouched */
    out = 0xdeadbeef;
    if (fa_harvest_candidate_index(100, 5, &out) != 0) {
        fprintf(stderr, "FAIL N-07: expected return 0 for r=100 num_candidates=5\n");
        ok = 0;
    }
    if (out != 0xdeadbeef) {
        fprintf(stderr, "FAIL N-07: out_index overwritten on failure path (got 0x%x)\n", out);
        ok = 0;
    }

    /* N-08: num_candidates==0: any r returns 0, out untouched */
    out = 0xdeadbeef;
    if (fa_harvest_candidate_index(0, 0, &out) != 0) {
        fprintf(stderr, "FAIL N-08: expected return 0 for r=0 num_candidates=0\n");
        ok = 0;
    }
    if (out != 0xdeadbeef) {
        fprintf(stderr, "FAIL N-08: out_index overwritten on failure path (got 0x%x)\n", out);
        ok = 0;
    }

    /* N-09: multi-device invariant -- the key regression guard.
     * Simulate the harvest loop for total_devices=3, each with num_results=5,
     * num_candidates=5.  Walk device=0,1,2 and r=0..4, collecting out_index for
     * every in-bounds call.  Assert:
     *   (a) every returned index is < 5, and
     *   (b) for the same r across different devices, the mapping is IDENTICAL
     *       (== r), NOT a running counter that would climb to 14 and overrun
     *       the 5-entry ppi_refs snapshot -- that is precisely the multi-GPU OOB
     *       heap corruption this helper prevents.
     */
    {
        unsigned int total_devices = 3;
        unsigned int num_results   = 5;
        unsigned int num_candidates = 5;
        unsigned int dev, r;
        /* Store the first device's out_index for each r to compare later. */
        unsigned int first_dev_out[5] = {0};

        for (dev = 0; dev < total_devices; dev++) {
            for (r = 0; r < num_results; r++) {
                out = 0xdeadbeef;
                int ret = fa_harvest_candidate_index(r, num_candidates, &out);

                /* (a) every result is in-bounds and < num_candidates */
                if (ret != 1) {
                    fprintf(stderr, "FAIL N-09a: device=%u r=%u expected in-bounds\n", dev, r);
                    ok = 0;
                    continue;
                }
                if (out >= num_candidates) {
                    fprintf(stderr, "FAIL N-09b: device=%u r=%u out_index=%u >= num_candidates=%u\n",
                            dev, r, out, num_candidates);
                    ok = 0;
                }

                /* (b) same r on a different device yields the same index */
                if (dev == 0) {
                    first_dev_out[r] = out;
                } else {
                    if (out != first_dev_out[r]) {
                        fprintf(stderr, "FAIL N-09c: device=%u r=%u out_index=%u != device0 out_index=%u "
                                "(running-counter bug would give device*n+r here)\n",
                                dev, r, out, first_dev_out[r]);
                        ok = 0;
                    }
                }
            }
        }
    }

    return ok;
}


int test_misc(void)
{
    int ok = 1;

    ok &= group_a();
    ok &= group_b();
    ok &= group_c();
    ok &= group_d();
    ok &= group_e();
    ok &= group_f();
    ok &= group_g();
    ok &= group_h();
    ok &= group_i();
    ok &= group_j();
    ok &= group_k();
    ok &= group_l();
    ok &= group_m();
    ok &= group_n();

    return ok;
}
