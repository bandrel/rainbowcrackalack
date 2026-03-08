/*
 * Rainbow Crackalack: test_mask.c
 * CPU-only tests for mask_parse, mask_keyspace, mask_to_gpu_buffers,
 * fill_plaintext_space_table_mask, and index_to_plaintext_mask.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "cpu_rt_functions.h"
#include "mask_parse.h"
#include "shared.h"
#include "test_mask.h"
#include "verify.h"


/* Parse mask, fill GPU buffers, fill pspace, convert index to plaintext,
 * compare result against expected bytes of length expected_len. */
static int run_i2p_mask_test(const char *mask_str,
                              const char *c1, const char *c2,
                              const char *c3, const char *c4,
                              uint64_t index,
                              const char *expected, unsigned int expected_len,
                              const char *test_id)
{
    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    uint64_t pspace[MAX_PLAINTEXT_LEN + 1];
    char plaintext[MAX_PLAINTEXT_LEN + 1];
    unsigned int plaintext_len = 0;

    if (mask_parse(mask_str, &m, c1, c2, c3, c4) != 0) {
        fprintf(stderr, "%s: mask_parse failed for \"%s\"\n", test_id, mask_str);
        return 0;
    }
    mask_to_gpu_buffers(&m, mask_data, mask_lens);
    fill_plaintext_space_table_mask(mask_lens, (unsigned int)m.length, pspace);
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask(index, mask_lens, mask_data, (unsigned int)m.length,
                            pspace, plaintext, &plaintext_len);

    if (plaintext_len != expected_len) {
        fprintf(stderr, "%s: expected len %u, got %u\n",
                test_id, expected_len, plaintext_len);
        return 0;
    }
    if (memcmp(plaintext, expected, expected_len) != 0) {
        fprintf(stderr, "%s: plaintext mismatch for index %"PRIu64"\n",
                test_id, index);
        return 0;
    }
    return 1;
}


/* --- Group A: is_mask_string --- */
static int group_a(void)
{
    int ok = 1;

    if (is_mask_string("?l") != 1)          { fprintf(stderr, "IS-01 failed\n"); ok = 0; }
    if (is_mask_string("ascii-32-95") != 0)  { fprintf(stderr, "IS-02 failed\n"); ok = 0; }
    if (is_mask_string("") != 0)             { fprintf(stderr, "IS-03 failed\n"); ok = 0; }
    if (is_mask_string(NULL) != 0)           { fprintf(stderr, "IS-04 failed\n"); ok = 0; }
    if (is_mask_string("P?d?d?d") != 1)      { fprintf(stderr, "IS-05 failed\n"); ok = 0; }
    if (is_mask_string("no-question") != 0)  { fprintf(stderr, "IS-06 failed\n"); ok = 0; }

    return ok;
}


/* --- Group B: mask_parse valid masks with keyspace checks --- */
static int group_b(void)
{
    Mask m;
    int ok = 1;

#define CHECK_KS(id, mask_str, c1, c2, c3, c4, expected_ks) \
    do { \
        if (mask_parse((mask_str), &m, (c1), (c2), (c3), (c4)) != 0) { \
            fprintf(stderr, id ": mask_parse returned error\n"); ok = 0; break; \
        } \
        uint64_t ks = mask_keyspace(&m); \
        if (ks != (uint64_t)(expected_ks)) { \
            fprintf(stderr, id ": keyspace expected %"PRIu64" got %"PRIu64"\n", \
                    (uint64_t)(expected_ks), ks); ok = 0; \
        } \
    } while (0)

    CHECK_KS("MP-01", "?d",          NULL,  NULL,  NULL, NULL, 10);
    CHECK_KS("MP-02", "?l",          NULL,  NULL,  NULL, NULL, 26);
    CHECK_KS("MP-03", "?u",          NULL,  NULL,  NULL, NULL, 26);
    CHECK_KS("MP-04", "?s",          NULL,  NULL,  NULL, NULL, 33);
    CHECK_KS("MP-05", "?a",          NULL,  NULL,  NULL, NULL, 95);
    CHECK_KS("MP-06", "?b",          NULL,  NULL,  NULL, NULL, 256);
    CHECK_KS("MP-07", "?d?d",        NULL,  NULL,  NULL, NULL, 100);
    CHECK_KS("MP-08", "?l?d",        NULL,  NULL,  NULL, NULL, 260);
    CHECK_KS("MP-09", "P?d?d?d",     NULL,  NULL,  NULL, NULL, 1000);
    CHECK_KS("MP-10", "?u?l?d?s",    NULL,  NULL,  NULL, NULL, 223080); /* 26*26*10*33 */
    CHECK_KS("MP-11", "?1",          "abc", NULL,  NULL, NULL, 3);
    CHECK_KS("MP-12", "?1?2",        "01",  "xy",  NULL, NULL, 4);

    /* 2-char combos from {?u,?l,?d,?s} */
    CHECK_KS("MP-13", "?u?l",        NULL,  NULL,  NULL, NULL, 676);    /* 26*26 */
    CHECK_KS("MP-14", "?u?d",        NULL,  NULL,  NULL, NULL, 260);    /* 26*10 */
    CHECK_KS("MP-15", "?u?s",        NULL,  NULL,  NULL, NULL, 858);    /* 26*33 */
    CHECK_KS("MP-16", "?l?s",        NULL,  NULL,  NULL, NULL, 858);    /* 26*33 */
    CHECK_KS("MP-17", "?d?s",        NULL,  NULL,  NULL, NULL, 330);    /* 10*33 */
    CHECK_KS("MP-18", "?s?s",        NULL,  NULL,  NULL, NULL, 1089);   /* 33*33 */

    /* 3-char combos from {?u,?l,?d,?s} */
    CHECK_KS("MP-19", "?u?l?d",      NULL,  NULL,  NULL, NULL, 6760);   /* 26*26*10 */
    CHECK_KS("MP-20", "?u?l?s",      NULL,  NULL,  NULL, NULL, 22308);  /* 26*26*33 */
    CHECK_KS("MP-21", "?u?d?s",      NULL,  NULL,  NULL, NULL, 8580);   /* 26*10*33 */
    CHECK_KS("MP-22", "?l?d?s",      NULL,  NULL,  NULL, NULL, 8580);   /* 26*10*33 */

    /* 4-char: all four together */
    CHECK_KS("MP-23", "?u?l?d?s",    NULL,  NULL,  NULL, NULL, 223080); /* 26*26*10*33 */

    /* ?a and ?b multi-position */
    CHECK_KS("MP-24", "?a?a",        NULL,  NULL,  NULL, NULL, 9025);   /* 95*95 */
    CHECK_KS("MP-25", "?a?d",        NULL,  NULL,  NULL, NULL, 950);    /* 95*10 */
    CHECK_KS("MP-26", "?b?b",        NULL,  NULL,  NULL, NULL, 65536);  /* 256*256 */

    /* all 4 custom charsets: c1="ab"(2) c2="xyz"(3) c3="pq"(2) c4="mn"(2) */
    CHECK_KS("MP-27", "?1?2?3?4", "ab", "xyz", "pq", "mn", 24);        /* 2*3*2*2 */

    /* single literal - keyspace 1 */
    CHECK_KS("MP-28", "Z",           NULL,  NULL,  NULL, NULL, 1);

    /* 8-digit mask */
    CHECK_KS("MP-29", "?d?d?d?d?d?d?d?d", NULL, NULL, NULL, NULL, 100000000ULL); /* 10^8 */

    /* exactly MAX_PLAINTEXT_LEN (16) positions */
    CHECK_KS("MP-30", "?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d",
             NULL, NULL, NULL, NULL, 10000000000000000ULL); /* 10^16 */

#undef CHECK_KS

    return ok;
}


/* --- Group C: mask_parse error cases --- */
static int group_c(void)
{
    Mask m;
    int ok = 1;

    /* PE-01: unknown specifier */
    if (mask_parse("?x", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-01 failed: expected -1 for unknown specifier\n"); ok = 0;
    }

    /* PE-02: custom charset ?1 requested but not supplied */
    if (mask_parse("?1", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-02 failed: expected -1 for unset custom charset\n"); ok = 0;
    }

    /* PE-03: 17 positions exceeds MAX_PLAINTEXT_LEN (16) */
    if (mask_parse("?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d",
                   &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-03 failed: expected -1 for overlong mask\n"); ok = 0;
    }

    /* PE-04: NULL mask */
    if (mask_parse(NULL, &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-04 failed: expected -1 for NULL mask\n"); ok = 0;
    }

    /* PE-05: NULL output struct */
    if (mask_parse("?d", NULL, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-05 failed: expected -1 for NULL output\n"); ok = 0;
    }

    /* PE-06: custom charset ?3 requested but not supplied */
    if (mask_parse("?3", &m, "ab", "xy", NULL, NULL) != -1) {
        fprintf(stderr, "PE-06 failed: expected -1 for unset custom charset ?3\n"); ok = 0;
    }

    /* PE-07: custom charset ?4 requested but not supplied */
    if (mask_parse("?4", &m, "ab", "xy", "pq", NULL) != -1) {
        fprintf(stderr, "PE-07 failed: expected -1 for unset custom charset ?4\n"); ok = 0;
    }

    /* PE-08: trailing bare '?' with no specifier */
    if (mask_parse("?l?", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-08 failed: expected -1 for trailing bare '?'\n"); ok = 0;
    }

    /* PE-09: mask that is only a bare '?' */
    if (mask_parse("?", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "PE-09 failed: expected -1 for lone '?'\n"); ok = 0;
    }

    return ok;
}


/* --- Group D: index_to_plaintext_mask --- */
static int group_d(void)
{
    int ok = 1;

#define I2P(id, mask, c1, c2, c3, c4, idx, exp, explen) \
    ok &= run_i2p_mask_test((mask), (c1), (c2), (c3), (c4), \
                             (uint64_t)(idx), (exp), (explen), (id))

    /* ?d */
    I2P("I2P-01", "?d", NULL, NULL, NULL, NULL,  0, "0", 1);
    I2P("I2P-02", "?d", NULL, NULL, NULL, NULL,  9, "9", 1);
    I2P("I2P-03", "?d", NULL, NULL, NULL, NULL,  5, "5", 1);

    /* ?l */
    I2P("I2P-04", "?l", NULL, NULL, NULL, NULL,  0, "a", 1);
    I2P("I2P-05", "?l", NULL, NULL, NULL, NULL, 25, "z", 1);

    /* ?u */
    I2P("I2P-06", "?u", NULL, NULL, NULL, NULL,  0, "A", 1);
    I2P("I2P-07", "?u", NULL, NULL, NULL, NULL, 25, "Z", 1);

    /* ?s  (space=0, ~=32) */
    I2P("I2P-08", "?s", NULL, NULL, NULL, NULL,  0, " ", 1);
    I2P("I2P-09", "?s", NULL, NULL, NULL, NULL, 32, "~", 1);
    I2P("I2P-10", "?s", NULL, NULL, NULL, NULL, 10, "*", 1);

    /* ?a  (?l + ?u + ?d + ?s ordering) */
    I2P("I2P-11", "?a", NULL, NULL, NULL, NULL,  0, "a", 1);
    I2P("I2P-12", "?a", NULL, NULL, NULL, NULL, 25, "z", 1);
    I2P("I2P-13", "?a", NULL, NULL, NULL, NULL, 26, "A", 1);
    I2P("I2P-14", "?a", NULL, NULL, NULL, NULL, 51, "Z", 1);
    I2P("I2P-15", "?a", NULL, NULL, NULL, NULL, 52, "0", 1);
    I2P("I2P-16", "?a", NULL, NULL, NULL, NULL, 61, "9", 1);
    I2P("I2P-17", "?a", NULL, NULL, NULL, NULL, 62, " ", 1);
    I2P("I2P-18", "?a", NULL, NULL, NULL, NULL, 94, "~", 1);

    /* ?b  (NUL byte requires memcmp path) */
    I2P("I2P-19", "?b", NULL, NULL, NULL, NULL,   0, "\x00", 1);
    I2P("I2P-20", "?b", NULL, NULL, NULL, NULL, 255, "\xff", 1);
    I2P("I2P-21", "?b", NULL, NULL, NULL, NULL,  65, "A",    1);

    /* ?d?d */
    I2P("I2P-22", "?d?d", NULL, NULL, NULL, NULL,  0, "00", 2);
    I2P("I2P-23", "?d?d", NULL, NULL, NULL, NULL, 42, "42", 2);
    I2P("I2P-24", "?d?d", NULL, NULL, NULL, NULL, 99, "99", 2);

    /* ?l?d */
    I2P("I2P-25", "?l?d", NULL, NULL, NULL, NULL,   0, "a0", 2);
    I2P("I2P-26", "?l?d", NULL, NULL, NULL, NULL, 259, "z9", 2);
    /* index 26: 26%10=6 -> '6', 26/10=2 -> 'c' */
    I2P("I2P-27", "?l?d", NULL, NULL, NULL, NULL,  26, "c6", 2);

    /* P?d?d?d  (literal prefix) */
    I2P("I2P-28", "P?d?d?d", NULL, NULL, NULL, NULL,   0, "P000", 4);
    I2P("I2P-29", "P?d?d?d", NULL, NULL, NULL, NULL, 999, "P999", 4);
    I2P("I2P-30", "P?d?d?d", NULL, NULL, NULL, NULL, 123, "P123", 4);

    /* 3-position mix */
    I2P("I2P-31", "?u?d?l", NULL, NULL, NULL, NULL, 0, "A0a", 3);

    /* custom charsets ?1/?2 */
    I2P("I2P-32", "?1", "abc", NULL, NULL, NULL, 0, "a", 1);
    I2P("I2P-33", "?1", "abc", NULL, NULL, NULL, 2, "c", 1);
    /* ?1?2 c1="01" c2="ab": index 3 -> 3%2=1->'b', 1%2=1->'1' */
    I2P("I2P-34", "?1?2", "01", "ab", NULL, NULL, 3, "1b", 2);

    /* --- 2-char combos from {?u,?l,?d,?s} --- */

    /* ?u?l (26*26=676) */
    I2P("I2P-44", "?u?l", NULL,NULL,NULL,NULL,   0, "Aa", 2);
    I2P("I2P-45", "?u?l", NULL,NULL,NULL,NULL,  26, "Ba", 2); /* carry: 26/26=1->'B', 26%26=0->'a' */
    I2P("I2P-46", "?u?l", NULL,NULL,NULL,NULL, 675, "Zz", 2); /* last */

    /* ?u?d (26*10=260) */
    I2P("I2P-47", "?u?d", NULL,NULL,NULL,NULL,   0, "A0", 2);
    I2P("I2P-48", "?u?d", NULL,NULL,NULL,NULL,  10, "B0", 2); /* carry */
    I2P("I2P-49", "?u?d", NULL,NULL,NULL,NULL, 259, "Z9", 2); /* last */

    /* ?u?s (26*33=858) */
    I2P("I2P-50", "?u?s", NULL,NULL,NULL,NULL,   0, "A ", 2);
    I2P("I2P-51", "?u?s", NULL,NULL,NULL,NULL, 857, "Z~", 2); /* last */

    /* ?l?s (26*33=858) */
    I2P("I2P-52", "?l?s", NULL,NULL,NULL,NULL,   0, "a ", 2);
    I2P("I2P-53", "?l?s", NULL,NULL,NULL,NULL, 857, "z~", 2); /* last */

    /* ?d?s (10*33=330) */
    I2P("I2P-54", "?d?s", NULL,NULL,NULL,NULL,   0, "0 ", 2);
    I2P("I2P-55", "?d?s", NULL,NULL,NULL,NULL, 329, "9~", 2); /* last */

    /* ?s?s (33*33=1089) */
    I2P("I2P-56", "?s?s", NULL,NULL,NULL,NULL,    0, "  ", 2);
    I2P("I2P-57", "?s?s", NULL,NULL,NULL,NULL,   33, "! ", 2); /* carry: 33/33=1->'!', 33%33=0->' ' */
    I2P("I2P-58", "?s?s", NULL,NULL,NULL,NULL, 1088, "~~", 2); /* last */

    /* --- 3-char combos from {?u,?l,?d,?s} (first and last) --- */

    /* ?u?l?d (26*26*10=6760) */
    I2P("I2P-59", "?u?l?d", NULL,NULL,NULL,NULL,    0, "Aa0", 3);
    I2P("I2P-60", "?u?l?d", NULL,NULL,NULL,NULL, 6759, "Zz9", 3); /* last */

    /* ?u?l?s (26*26*33=22308) */
    I2P("I2P-61", "?u?l?s", NULL,NULL,NULL,NULL,     0, "Aa ", 3);
    I2P("I2P-62", "?u?l?s", NULL,NULL,NULL,NULL, 22307, "Zz~", 3); /* last */

    /* ?u?d?s (26*10*33=8580) */
    I2P("I2P-63", "?u?d?s", NULL,NULL,NULL,NULL,    0, "A0 ", 3);
    I2P("I2P-64", "?u?d?s", NULL,NULL,NULL,NULL, 8579, "Z9~", 3); /* last */

    /* ?l?d?s (26*10*33=8580) */
    I2P("I2P-65", "?l?d?s", NULL,NULL,NULL,NULL,    0, "a0 ", 3);
    I2P("I2P-66", "?l?d?s", NULL,NULL,NULL,NULL, 8579, "z9~", 3); /* last */

    /* --- 4-char: all four together (26*26*10*33=223080) --- */
    I2P("I2P-67", "?u?l?d?s", NULL,NULL,NULL,NULL,      0, "Aa0 ", 4);
    I2P("I2P-68", "?u?l?d?s", NULL,NULL,NULL,NULL, 223079, "Zz9~", 4); /* last */

    /* --- ?a and ?b multi-position --- */

    /* ?a?a (95*95=9025) */
    I2P("I2P-69", "?a?a", NULL,NULL,NULL,NULL,    0, "aa", 2);
    I2P("I2P-70", "?a?a", NULL,NULL,NULL,NULL, 9024, "~~", 2); /* last */

    /* ?a?d (95*10=950) */
    I2P("I2P-71", "?a?d", NULL,NULL,NULL,NULL,   0, "a0", 2);
    I2P("I2P-72", "?a?d", NULL,NULL,NULL,NULL, 949, "~9", 2); /* last */

    /* ?b?b (256*256=65536) */
    I2P("I2P-73", "?b?b", NULL,NULL,NULL,NULL,     0, "\x00\x00", 2);
    I2P("I2P-74", "?b?b", NULL,NULL,NULL,NULL, 65535, "\xff\xff", 2); /* last */

    /* all 4 custom charsets: c1="ab"(2) c2="xyz"(3) c3="pq"(2) c4="mn"(2), KS=24 */
    I2P("I2P-75", "?1?2?3?4", "ab","xyz","pq","mn",  0, "axpm", 4);
    I2P("I2P-76", "?1?2?3?4", "ab","xyz","pq","mn", 23, "bzqn", 4); /* last */

    /* --- Longer masks --- */

    /* 8-digit mask (10^8=100000000) */
    I2P("I2P-77", "?d?d?d?d?d?d?d?d", NULL,NULL,NULL,NULL,        0, "00000000", 8);
    I2P("I2P-78", "?d?d?d?d?d?d?d?d", NULL,NULL,NULL,NULL, 12345678, "12345678", 8);
    I2P("I2P-79", "?d?d?d?d?d?d?d?d", NULL,NULL,NULL,NULL, 99999999, "99999999", 8);

    /* literal prefix + specifier */
    I2P("I2P-80", "admin?d?d?d", NULL,NULL,NULL,NULL,   0, "admin000", 8);
    I2P("I2P-81", "admin?d?d?d", NULL,NULL,NULL,NULL, 999, "admin999", 8);

    /* --- 16-position boundary (MAX_PLAINTEXT_LEN) --- */
    I2P("I2P-82", "?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d?d",
        NULL,NULL,NULL,NULL, 0, "0000000000000000", 16);
    I2P("I2P-83", "?l?l?l?l?l?l?l?l?l?l?l?l?l?l?l?l",
        NULL,NULL,NULL,NULL, 0, "aaaaaaaaaaaaaaaa", 16);

    /* --- Alternating and repeating patterns (6-position) --- */

    /* ?u?l?u?l?u?l (6 positions, all size-26) */
    I2P("I2P-84", "?u?l?u?l?u?l", NULL, NULL, NULL, NULL,
        0, "AaAaAa", 6);
    I2P("I2P-85", "?u?l?u?l?u?l", NULL, NULL, NULL, NULL,
        1, "AaAaAb", 6);
    /* last index = 26^6 - 1 = 308915775 -> "ZzZzZz" */
    I2P("I2P-86", "?u?l?u?l?u?l", NULL, NULL, NULL, NULL,
        308915775UL, "ZzZzZz", 6);

    /* ?l?u?l?u?l (5 positions) */
    I2P("I2P-87", "?l?u?l?u?l", NULL, NULL, NULL, NULL,
        0, "aAaAa", 5);
    /* last index = 26^5 - 1 = 11881375 -> "zZzZz" */
    I2P("I2P-88", "?l?u?l?u?l", NULL, NULL, NULL, NULL,
        11881375UL, "zZzZz", 5);

    /* ?u?l?d?u?l?d (6 positions: 26 26 10 26 26 10) */
    I2P("I2P-89", "?u?l?d?u?l?d", NULL, NULL, NULL, NULL,
        0, "Aa0Aa0", 6);
    /* index 1: rightmost ?d gets '1', rest stay at 0 */
    I2P("I2P-90", "?u?l?d?u?l?d", NULL, NULL, NULL, NULL,
        1, "Aa0Aa1", 6);
    /* index 10: 10%10=0->'0', 10/10=1; 1%26=1->'b'; rest 0 */
    I2P("I2P-91", "?u?l?d?u?l?d", NULL, NULL, NULL, NULL,
        10, "Aa0Ab0", 6);
    /* index 260: 260%10=0->'0', 26; 26%26=0->'a', 1; 1%26=1->'B'; rest 0 */
    I2P("I2P-92", "?u?l?d?u?l?d", NULL, NULL, NULL, NULL,
        260, "Aa0Ba0", 6);

#undef I2P

    return ok;
}


/* --- Group E: fill_plaintext_space_table_mask pspace values --- */
static int group_e(void)
{
    uint64_t pspace[MAX_PLAINTEXT_LEN + 1];
    int ok = 1;

    /* FP-01: single ?d */
    unsigned int lens1[] = {10};
    fill_plaintext_space_table_mask(lens1, 1, pspace);
    if (pspace[1] != 10) { fprintf(stderr, "FP-01a: pspace[1]=%"PRIu64"\n", pspace[1]); ok = 0; }
    if (pspace[0] != 0)  { fprintf(stderr, "FP-01b: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-02: ?l?d */
    unsigned int lens2[] = {26, 10};
    fill_plaintext_space_table_mask(lens2, 2, pspace);
    if (pspace[2] != 260) { fprintf(stderr, "FP-02a: pspace[2]=%"PRIu64"\n", pspace[2]); ok = 0; }
    if (pspace[1] != 0)   { fprintf(stderr, "FP-02b: pspace[1]=%"PRIu64"\n", pspace[1]); ok = 0; }
    if (pspace[0] != 0)   { fprintf(stderr, "FP-02c: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-03: ?d?d?d */
    unsigned int lens3[] = {10, 10, 10};
    fill_plaintext_space_table_mask(lens3, 3, pspace);
    if (pspace[3] != 1000) { fprintf(stderr, "FP-03a: pspace[3]=%"PRIu64"\n", pspace[3]); ok = 0; }
    if (pspace[2] != 0)    { fprintf(stderr, "FP-03b: pspace[2]=%"PRIu64"\n", pspace[2]); ok = 0; }
    if (pspace[0] != 0)    { fprintf(stderr, "FP-03c: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-04: single ?a (95) */
    unsigned int lens4[] = {95};
    fill_plaintext_space_table_mask(lens4, 1, pspace);
    if (pspace[1] != 95) { fprintf(stderr, "FP-04a: pspace[1]=%"PRIu64"\n", pspace[1]); ok = 0; }
    if (pspace[0] != 0)  { fprintf(stderr, "FP-04b: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-05: ?l?s (26*33=858) */
    unsigned int lens5[] = {26, 33};
    fill_plaintext_space_table_mask(lens5, 2, pspace);
    if (pspace[2] != 858) { fprintf(stderr, "FP-05a: pspace[2]=%"PRIu64"\n", pspace[2]); ok = 0; }
    if (pspace[1] != 0)   { fprintf(stderr, "FP-05b: pspace[1]=%"PRIu64"\n", pspace[1]); ok = 0; }
    if (pspace[0] != 0)   { fprintf(stderr, "FP-05c: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-06: ?u?l?d (26*26*10=6760) */
    unsigned int lens6[] = {26, 26, 10};
    fill_plaintext_space_table_mask(lens6, 3, pspace);
    if (pspace[3] != 6760) { fprintf(stderr, "FP-06a: pspace[3]=%"PRIu64"\n", pspace[3]); ok = 0; }
    if (pspace[2] != 0)    { fprintf(stderr, "FP-06b: pspace[2]=%"PRIu64"\n", pspace[2]); ok = 0; }
    if (pspace[0] != 0)    { fprintf(stderr, "FP-06c: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-07: ?u?l?d?s (26*26*10*33=223080) */
    unsigned int lens7[] = {26, 26, 10, 33};
    fill_plaintext_space_table_mask(lens7, 4, pspace);
    if (pspace[4] != 223080) { fprintf(stderr, "FP-07a: pspace[4]=%"PRIu64"\n", pspace[4]); ok = 0; }
    if (pspace[3] != 0)      { fprintf(stderr, "FP-07b: pspace[3]=%"PRIu64"\n", pspace[3]); ok = 0; }
    if (pspace[0] != 0)      { fprintf(stderr, "FP-07c: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    /* FP-08: single ?b (256) */
    unsigned int lens8[] = {256};
    fill_plaintext_space_table_mask(lens8, 1, pspace);
    if (pspace[1] != 256) { fprintf(stderr, "FP-08a: pspace[1]=%"PRIu64"\n", pspace[1]); ok = 0; }
    if (pspace[0] != 0)   { fprintf(stderr, "FP-08b: pspace[0]=%"PRIu64"\n", pspace[0]); ok = 0; }

    return ok;
}


/* --- Group F: verify_rainbowtable with mask tables --- */
static int group_f(void)
{
    int ok = 1;
    unsigned int error_chain = 0;

    /* VR-01: valid mask table with end=0 in first chain must not be rejected.
     * For ?d?d?d?d the keyspace is 10,000.  Index 0 ("0000") is a legitimate
     * endpoint, so is_mask=1 must suppress the end==0 check. */
    uint64_t table_end0[4] = { 0, 0,   /* chain 0: start=0, end=0 */
                                1, 5 }; /* chain 1: start=1, end=5 */
    error_chain = 0;
    if (!verify_rainbowtable(table_end0, 2, VERIFY_TABLE_TYPE_GENERATED,
                             0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-01 failed: mask table with end=0 incorrectly rejected\n");
        ok = 0;
    }

    /* VR-02: non-mask table with end=0 must still be rejected. */
    uint64_t table_end0_plain[2] = { 0, 0 }; /* start=0, end=0 */
    error_chain = 0;
    if (verify_rainbowtable(table_end0_plain, 1, VERIFY_TABLE_TYPE_GENERATED,
                            0, 10000, &error_chain, 0)) {
        fprintf(stderr, "VR-02 failed: non-mask table with end=0 should be rejected\n");
        ok = 0;
    }
    if (error_chain != 0) {
        fprintf(stderr, "VR-02b: error_chain expected 0, got %u\n", error_chain);
        ok = 0;
    }

    /* VR-03: mask table where every chain ends at 0 (degenerate but valid).
     * Happens when the full keyspace reduces to the same endpoint. */
    uint64_t all_zero_ends[6] = { 0, 0,   1, 0,   2, 0 };
    error_chain = 99;
    if (!verify_rainbowtable(all_zero_ends, 3, VERIFY_TABLE_TYPE_GENERATED,
                             0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-03 failed: mask table with all end=0 incorrectly rejected\n");
        ok = 0;
    }

    /* VR-04: mask table with first chain at end=0 and non-sequential start
     * must still be caught by the start-index check. */
    uint64_t bad_start[2] = { 5, 0 }; /* start should be 0 for part_index=0 */
    error_chain = 99;
    if (verify_rainbowtable(bad_start, 1, VERIFY_TABLE_TYPE_GENERATED,
                            0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-04 failed: wrong start index should be rejected\n");
        ok = 0;
    }
    if (error_chain != 0) {
        fprintf(stderr, "VR-04b: error_chain expected 0, got %u\n", error_chain);
        ok = 0;
    }

    /* VR-05: mask table with start index out of bounds must be rejected. */
    uint64_t oob[2] = { 10000, 1 }; /* start >= plaintext_space_total */
    error_chain = 99;
    if (verify_rainbowtable(oob, 1, VERIFY_TABLE_TYPE_GENERATED,
                            10000, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-05 failed: start >= pspace_total should be rejected\n");
        ok = 0;
    }

    /* VR-06: mask table for lookup type - sorted ascending ends including 0. */
    uint64_t lookup_with_zero[4] = { 7, 0,   3, 5 }; /* ends: 0, 5 - sorted */
    error_chain = 99;
    if (!verify_rainbowtable(lookup_with_zero, 2, VERIFY_TABLE_TYPE_LOOKUP,
                             0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-06 failed: lookup mask table with end=0 first should pass\n");
        ok = 0;
    }

    /* VR-07: mask table for lookup type - unsorted ends must be rejected. */
    uint64_t unsorted_lookup[4] = { 7, 5,   3, 3 }; /* ends: 5, 3 - not sorted */
    error_chain = 99;
    if (verify_rainbowtable(unsorted_lookup, 2, VERIFY_TABLE_TYPE_LOOKUP,
                            0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-07 failed: unsorted lookup table should be rejected\n");
        ok = 0;
    }

    /* VR-08: non-mask generated table with valid (non-zero) ends must pass. */
    uint64_t valid_gen[4] = { 0, 42,   1, 99 };
    error_chain = 99;
    if (!verify_rainbowtable(valid_gen, 2, VERIFY_TABLE_TYPE_GENERATED,
                             0, 10000, &error_chain, 0)) {
        fprintf(stderr, "VR-08 failed: valid non-mask table incorrectly rejected\n");
        ok = 0;
    }

    /* VR-09: mask table - end out of bounds must be rejected. */
    uint64_t end_oob[2] = { 0, 10000 }; /* end >= plaintext_space_total */
    error_chain = 99;
    if (verify_rainbowtable(end_oob, 1, VERIFY_TABLE_TYPE_GENERATED,
                            0, 10000, &error_chain, 1)) {
        fprintf(stderr, "VR-09 failed: end >= pspace_total should be rejected\n");
        ok = 0;
    }

    /* VR-10: non-mask table - second chain has end=0, not first.
     * error_chain must point to chain 1. */
    uint64_t second_zero[4] = { 0, 5,   1, 0 };
    error_chain = 99;
    if (verify_rainbowtable(second_zero, 2, VERIFY_TABLE_TYPE_GENERATED,
                            0, 10000, &error_chain, 0)) {
        fprintf(stderr, "VR-10 failed: non-mask table with second chain end=0 should be rejected\n");
        ok = 0;
    }
    if (error_chain != 1) {
        fprintf(stderr, "VR-10b: error_chain expected 1, got %u\n", error_chain);
        ok = 0;
    }

    return ok;
}


/* --- Group G: mask_encode_for_filename / mask_decode_from_filename --- */
static int group_g(void)
{
    int ok = 1;
    char buf[64];

    /* ME-01: basic specifiers replaced */
    mask_encode_for_filename("?l?d", buf, sizeof(buf));
    if (strcmp(buf, "%l%d") != 0)
        { fprintf(stderr, "ME-01 failed: got \"%s\"\n", buf); ok = 0; }

    /* ME-02: literal prefix preserved */
    mask_encode_for_filename("admin?d?d?d", buf, sizeof(buf));
    if (strcmp(buf, "admin%d%d%d") != 0)
        { fprintf(stderr, "ME-02 failed: got \"%s\"\n", buf); ok = 0; }

    /* ME-03: no specifiers - string unchanged */
    mask_encode_for_filename("nospecifiers", buf, sizeof(buf));
    if (strcmp(buf, "nospecifiers") != 0)
        { fprintf(stderr, "ME-03 failed: got \"%s\"\n", buf); ok = 0; }

    /* ME-04 through ME-13: round-trip for all built-in specifiers */
    const char *specifiers[] = {"?l","?u","?d","?s","?a","?b","?1","?2","?3","?4"};
    unsigned int n = (unsigned int)(sizeof(specifiers) / sizeof(specifiers[0]));
    for (unsigned int i = 0; i < n; i++) {
        char encoded[8];
        char decoded[8];
        mask_encode_for_filename(specifiers[i], encoded, sizeof(encoded));
        strncpy(decoded, encoded, sizeof(decoded));
        mask_decode_from_filename(decoded);
        if (strcmp(decoded, specifiers[i]) != 0) {
            fprintf(stderr, "ME-%02u round-trip failed: \"%s\" -> \"%s\" -> \"%s\"\n",
                    4 + i, specifiers[i], encoded, decoded);
            ok = 0;
        }
    }

    /* ME-14: unknown specifier in decode - left unchanged */
    strncpy(buf, "%xinvalid", sizeof(buf));
    mask_decode_from_filename(buf);
    if (strcmp(buf, "%xinvalid") != 0)
        { fprintf(stderr, "ME-14 failed: got \"%s\"\n", buf); ok = 0; }

    return ok;
}


int test_mask(void)
{
    int ok = 1;

    ok &= group_a();
    ok &= group_b();
    ok &= group_c();
    ok &= group_d();
    ok &= group_e();
    ok &= group_f();
    ok &= group_g();

    return ok;
}
