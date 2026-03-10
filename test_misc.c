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

    /* is_ntlm8 - reduction_offset==0 is part of the fast-path check; non-zero
     * reduction_offset means a non-zero table_index, which uses the generic path. */
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 0, 422000) != 1)
        { fprintf(stderr, "IN8-01 failed\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 0, 100000) != 0)
        { fprintf(stderr, "IN8-02 failed: wrong chain_len accepted\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_NUMERIC, 8, 8, 0, 422000) != 0)
        { fprintf(stderr, "IN8-03 failed: wrong charset accepted\n"); ok = 0; }
    if (is_ntlm8(HASH_NTLM, CHARSET_ASCII_32_95, 8, 8, 65536, 422000) != 0)
        { fprintf(stderr, "IN8-04 failed: non-zero reduction_offset accepted\n"); ok = 0; }

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

    /* PRP-03: valid mask table - charset_name should contain '?' after decode */
    strncpy(fn, "ntlm_%l%d%d%d#4-4_0_100000x1000000_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-03 failed: mask table not parsed\n"); ok = 0; }
    else if (strchr(p.charset_name, '?') == NULL)
        { fprintf(stderr, "PRP-03b failed: charset_name '%s' missing '?'\n", p.charset_name); ok = 0; }

    /* PRP-04: absolute path stripped */
    strncpy(fn, "/path/to/ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-04 failed: absolute path not handled\n"); ok = 0; }

    /* PRP-05: wrong extension */
    strncpy(fn, "ntlm_ascii-32-95#8-8_0_422000x67108864_0.txt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-05 failed: .txt extension accepted\n"); ok = 0; }

    /* PRP-06: unknown hash type */
    strncpy(fn, "sha1_ascii-32-95#8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-06 failed: sha1 hash accepted\n"); ok = 0; }

    /* PRP-07: malformed (missing '#') */
    strncpy(fn, "ntlm-ascii-32-95-8-8_0_422000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-07 failed: malformed filename accepted\n"); ok = 0; }

    /* PRP-08: zero chain_len */
    strncpy(fn, "ntlm_ascii-32-95#8-8_0_0x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (p.parsed)
        { fprintf(stderr, "PRP-08 failed: zero chain_len accepted\n"); ok = 0; }

    /* PRP-09: plaintext_len_max == MAX_PLAINTEXT_LEN (16) - regression for
     * bug where '<' was used instead of '<=' in parse_rt_params validation. */
    strncpy(fn, "ntlm_ascii-32-95#16-16_0_100000x67108864_0.rt", sizeof(fn));
    parse_rt_params(&p, fn);
    if (!p.parsed)
        { fprintf(stderr, "PRP-09 failed: plaintext_len_max==16 rejected\n"); ok = 0; }
    else if (p.plaintext_len_max != 16)
        { fprintf(stderr, "PRP-09b failed: plaintext_len_max=%u\n", p.plaintext_len_max); ok = 0; }

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

    return ok;
}
