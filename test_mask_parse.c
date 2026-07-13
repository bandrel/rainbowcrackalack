/*
 * Rainbow Crackalack: test_mask_parse.c
 * CPU-only unit tests for mask_parse.h / mask_parse.c.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu_rt_functions.h"
#include "mask_parse.h"
#include "misc.h"
#include "test_mask_parse.h"


/* MP-01 through MP-04: basic specifier parsing (?u?l?l?d) */
static int group_basic_specifiers(void)
{
    int ok = 1;
    Mask m;

    if (mask_parse("?u?l?l?d", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-01 failed: mask_parse returned non-zero\n");
        return 0;
    }

    /* MP-01: length == 4 */
    if (m.length != 4) {
        fprintf(stderr, "MP-01 failed: length=%d, expected 4\n", m.length);
        ok = 0;
    }

    /* MP-02: position 0 (?u) has 26 chars */
    if (m.positions[0].size != 26) {
        fprintf(stderr, "MP-02 failed: positions[0].size=%u, expected 26\n",
                m.positions[0].size);
        ok = 0;
    }

    /* MP-03: position 3 (?d) has 10 chars */
    if (m.positions[3].size != 10) {
        fprintf(stderr, "MP-03 failed: positions[3].size=%u, expected 10\n",
                m.positions[3].size);
        ok = 0;
    }

    /* MP-04: keyspace == 26*26*26*10 == 175760 */
    uint64_t ks = mask_keyspace(&m);
    if (ks != (uint64_t)26 * 26 * 26 * 10) {
        fprintf(stderr, "MP-04 failed: keyspace=%" PRIu64 ", expected 175760\n", ks);
        ok = 0;
    }

    return ok;
}


/* MP-05 through MP-06: literal character position */
static int group_literal_char(void)
{
    int ok = 1;
    Mask m;

    if (mask_parse("A?d", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-05 failed: mask_parse returned non-zero\n");
        return 0;
    }

    /* MP-05: length == 2, position 0 is literal 'A' with size 1 */
    if (m.length != 2) {
        fprintf(stderr, "MP-05 failed: length=%d, expected 2\n", m.length);
        ok = 0;
    }
    if (m.positions[0].size != 1) {
        fprintf(stderr, "MP-05 failed: positions[0].size=%u, expected 1\n",
                m.positions[0].size);
        ok = 0;
    }
    if (m.positions[0].chars[0] != 'A') {
        fprintf(stderr, "MP-05 failed: positions[0].chars[0]='%c', expected 'A'\n",
                m.positions[0].chars[0]);
        ok = 0;
    }

    /* MP-06: position 1 (?d) has 10 chars */
    if (m.positions[1].size != 10) {
        fprintf(stderr, "MP-06 failed: positions[1].size=%u, expected 10\n",
                m.positions[1].size);
        ok = 0;
    }

    return ok;
}


/* MP-07: is_mask_string detection */
static int group_is_mask_string(void)
{
    int ok = 1;

    /* MP-07a: mask string with '?' returns 1 */
    if (is_mask_string("?u?l") != 1) {
        fprintf(stderr, "MP-07a failed: is_mask_string(\"?u?l\") != 1\n");
        ok = 0;
    }

    /* MP-07b: charset name without '?' returns 0 */
    if (is_mask_string("ascii-32-95") != 0) {
        fprintf(stderr, "MP-07b failed: is_mask_string(\"ascii-32-95\") != 0\n");
        ok = 0;
    }

    return ok;
}


/* MP-08: filename encode/decode roundtrip */
static int group_filename_roundtrip(void)
{
    int ok = 1;
    char encoded[64];
    char decoded[64];

    mask_encode_for_filename("?u?l?d?s", encoded, sizeof(encoded));
    if (strcmp(encoded, "%u%l%d%s") != 0) {
        fprintf(stderr, "MP-08 failed: encoded=\"%s\", expected \"%%u%%l%%d%%s\"\n",
                encoded);
        ok = 0;
    }

    /* decode in-place */
    strncpy(decoded, encoded, sizeof(decoded));
    decoded[sizeof(decoded) - 1] = '\0';
    mask_decode_from_filename(decoded);
    if (strcmp(decoded, "?u?l?d?s") != 0) {
        fprintf(stderr, "MP-08 failed: decoded=\"%s\", expected \"?u?l?d?s\"\n",
                decoded);
        ok = 0;
    }

    /* MP-19: ?h/?H round-trip through filename encode/decode */
    mask_encode_for_filename("?h?H?d", encoded, sizeof(encoded));
    if (strcmp(encoded, "%h%H%d") != 0) {
        fprintf(stderr, "MP-19 failed: encoded=\"%s\", expected \"%%h%%H%%d\"\n",
                encoded);
        ok = 0;
    }
    strncpy(decoded, encoded, sizeof(decoded));
    decoded[sizeof(decoded) - 1] = '\0';
    mask_decode_from_filename(decoded);
    if (strcmp(decoded, "?h?H?d") != 0) {
        fprintf(stderr, "MP-19 failed: decoded=\"%s\", expected \"?h?H?d\"\n",
                decoded);
        ok = 0;
    }

    return ok;
}


/* MP-09: mask_to_gpu_buffers populates data and lens correctly */
static int group_gpu_buffers(void)
{
    int ok = 1;
    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];

    /* parse a two-position mask: literal 'Z' then ?d */
    if (mask_parse("Z?d", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-09 failed: mask_parse returned non-zero\n");
        return 0;
    }

    mask_to_gpu_buffers(&m, mask_data, mask_lens);

    /* MP-09a: lens[0] == 1 (literal 'Z') */
    if (mask_lens[0] != 1) {
        fprintf(stderr, "MP-09a failed: mask_lens[0]=%u, expected 1\n", mask_lens[0]);
        ok = 0;
    }

    /* MP-09b: mask_data[0] == 'Z' */
    if (mask_data[0] != 'Z') {
        fprintf(stderr, "MP-09b failed: mask_data[0]='%c', expected 'Z'\n",
                mask_data[0]);
        ok = 0;
    }

    /* MP-09c: lens[1] == 10 (?d) */
    if (mask_lens[1] != 10) {
        fprintf(stderr, "MP-09c failed: mask_lens[1]=%u, expected 10\n", mask_lens[1]);
        ok = 0;
    }

    return ok;
}


/* MP-10: ?a expands to l+u+d+s = 26+26+10+33 = 95 */
static int group_a_specifier(void)
{
    int ok = 1;
    Mask m;

    if (mask_parse("?a", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-10 failed: mask_parse(\"?a\") returned error\n");
        return 0;
    }

    if (m.positions[0].size != 95) {
        fprintf(stderr, "MP-10 failed: ?a size=%u, expected 95\n",
                m.positions[0].size);
        ok = 0;
    }

    return ok;
}


/* MP-11/MP-12: negative paths */
static int group_negative(void)
{
    int ok = 1;
    Mask m;

    /* MP-11: unknown specifier ?z must return -1 */
    if (mask_parse("?z", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "MP-11 failed: expected -1 for unknown specifier \"?z\"\n");
        ok = 0;
    }

    /* MP-12: trailing bare ? must return -1 */
    if (mask_parse("?u?", &m, NULL, NULL, NULL, NULL) != -1) {
        fprintf(stderr, "MP-12 failed: expected -1 for trailing \"?\" in \"?u?\"\n");
        ok = 0;
    }

    return ok;
}


/* MP-13: fill_plaintext_space_mask — fixed-length pspace semantics */
static int group_fill_plaintext_space_mask(void)
{
    int ok = 1;
    Mask m;

    if (mask_parse("?u?l?l?d", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-13 failed: mask_parse returned non-zero\n");
        return 0;
    }

    uint64_t up[MAX_PLAINTEXT_LEN + 1];
    uint64_t total = fill_plaintext_space_mask(&m, up);

    /* MP-13a: total == 26*26*26*10 == 175760 */
    uint64_t expected = (uint64_t)26 * 26 * 26 * 10;
    if (total != expected) {
        fprintf(stderr, "MP-13a failed: total=%" PRIu64 ", expected %" PRIu64 "\n",
                total, expected);
        ok = 0;
    }

    /* MP-13b: keyspace lives at index m.length */
    if (up[m.length] != total) {
        fprintf(stderr, "MP-13b failed: up[%d]=%" PRIu64 ", expected %" PRIu64 "\n",
                m.length, up[m.length], total);
        ok = 0;
    }

    /* MP-13c: all tiers below m.length are 0 (fixed-length semantics) */
    int i;
    for (i = 0; i < m.length; i++) {
        if (up[i] != 0) {
            fprintf(stderr, "MP-13c failed: up[%d]=%" PRIu64 ", expected 0\n",
                    i, up[i]);
            ok = 0;
        }
    }

    return ok;
}


/* MP-14: index_to_plaintext_mask_cpu — mixed-radix decode */
static int group_index_to_plaintext_mask(void)
{
    int ok = 1;
    Mask m;
    char plaintext[MAX_PLAINTEXT_LEN + 1];
    unsigned int plaintext_len;

    /* mask "?u?d": pos0=A..Z (26), pos1=0..9 (10); keyspace=260 */
    if (mask_parse("?u?d", &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "MP-14 failed: mask_parse returned non-zero\n");
        return 0;
    }

    /* MP-14a: index 0 -> "A0" */
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask_cpu(0, &m, plaintext, &plaintext_len);
    if (plaintext_len != 2 || strcmp(plaintext, "A0") != 0) {
        fprintf(stderr, "MP-14a failed: index 0 -> \"%s\" (len=%u), expected \"A0\"\n",
                plaintext, plaintext_len);
        ok = 0;
    }

    /* MP-14b: index 1 -> "A1" (least-significant position increments first) */
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask_cpu(1, &m, plaintext, &plaintext_len);
    if (plaintext_len != 2 || strcmp(plaintext, "A1") != 0) {
        fprintf(stderr, "MP-14b failed: index 1 -> \"%s\" (len=%u), expected \"A1\"\n",
                plaintext, plaintext_len);
        ok = 0;
    }

    /* MP-14c: index 259 (keyspace-1) -> "Z9" */
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask_cpu(259, &m, plaintext, &plaintext_len);
    if (plaintext_len != 2 || strcmp(plaintext, "Z9") != 0) {
        fprintf(stderr, "MP-14c failed: index 259 -> \"%s\" (len=%u), expected \"Z9\"\n",
                plaintext, plaintext_len);
        ok = 0;
    }

    /* MP-14d: index 10 -> "B0" (second upper char, first digit) */
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask_cpu(10, &m, plaintext, &plaintext_len);
    if (plaintext_len != 2 || strcmp(plaintext, "B0") != 0) {
        fprintf(stderr, "MP-14d failed: index 10 -> \"%s\" (len=%u), expected \"B0\"\n",
                plaintext, plaintext_len);
        ok = 0;
    }

    return ok;
}


/* MP-15: parse_rt_params recognises mask-encoded charset in filename */
static int group_parse_rt_params_mask(void)
{
    int ok = 1;
    rt_parameters p;
    char encoded[128];
    char filename[256];

    /* Build a filename with an encoded mask charset.
     * Original mask: "?u?l?l?d?d?d?d?d"  -> encoded: "%u%l%l%d%d%d%d%d" */
    mask_encode_for_filename("?u?l?l?d?d?d?d?d", encoded, sizeof(encoded));

    /* MP-15a: encoded charset is the filename-safe form */
    if (strcmp(encoded, "%u%l%l%d%d%d%d%d") != 0) {
        fprintf(stderr, "MP-15a failed: encoded=\"%s\", expected \"%%u%%l%%l%%d%%d%%d%%d%%d\"\n",
                encoded);
        ok = 0;
    }

    /* Build filename: ntlm_<encoded>#8-8_0_100000x67108864_0.rt */
    snprintf(filename, sizeof(filename),
             "ntlm_%s#8-8_0_100000x67108864_0.rt", encoded);

    memset(&p, 0, sizeof(p));
    parse_rt_params(&p, filename);

    /* MP-15b: parsed successfully */
    if (!p.parsed) {
        fprintf(stderr, "MP-15b failed: parsed=0 for mask filename \"%s\"\n", filename);
        ok = 0;
    }

    /* MP-15c: is_mask == 1 */
    if (p.is_mask != 1) {
        fprintf(stderr, "MP-15c failed: is_mask=%d, expected 1\n", p.is_mask);
        ok = 0;
    }

    /* MP-15d: decoded mask round-trips to original */
    if (strcmp(p.mask, "?u?l?l?d?d?d?d?d") != 0) {
        fprintf(stderr, "MP-15d failed: p.mask=\"%s\", expected \"?u?l?l?d?d?d?d?d\"\n",
                p.mask);
        ok = 0;
    }

    /* MP-15e: normal charset filename yields is_mask == 0 */
    memset(&p, 0, sizeof(p));
    parse_rt_params(&p, "ntlm_ascii-32-95#8-8_0_422000x67108864_0.rt");
    if (!p.parsed) {
        fprintf(stderr, "MP-15e failed: parsed=0 for normal charset filename\n");
        ok = 0;
    }
    if (p.is_mask != 0) {
        fprintf(stderr, "MP-15e failed: is_mask=%d for normal charset (expected 0)\n",
                p.is_mask);
        ok = 0;
    }

    /* MP-15f: markov filename still parses with markov_keyspace > 0 and is_mask == 0 */
    memset(&p, 0, sizeof(p));
    parse_rt_params(&p, "ntlm_ascii-32-95-mk1000000#8-8_0_422000x67108864_0.rt");
    if (!p.parsed) {
        fprintf(stderr, "MP-15f failed: parsed=0 for markov filename\n");
        ok = 0;
    }
    if (p.markov_keyspace == 0) {
        fprintf(stderr, "MP-15f failed: markov_keyspace=0, expected >0\n");
        ok = 0;
    }
    if (p.is_mask != 0) {
        fprintf(stderr, "MP-15f failed: is_mask=%d for markov filename (expected 0)\n",
                p.is_mask);
        ok = 0;
    }

    return ok;
}


/* MP-16..18: ?h, ?H, and ?? escaping */
static int group_hex_and_escape(void)
{
    int ok = 1;
    Mask m;

    /* MP-16: ?h = 16 lowercase-hex chars */
    if (mask_parse("?h", &m, NULL, NULL, NULL, NULL) != 0 || m.length != 1 ||
        m.positions[0].size != 16 ||
        memcmp(m.positions[0].chars, "0123456789abcdef", 16) != 0) {
        fprintf(stderr, "MP-16 failed: ?h expansion wrong\n"); ok = 0;
    }

    /* MP-17: ?H = 16 uppercase-hex chars */
    if (mask_parse("?H", &m, NULL, NULL, NULL, NULL) != 0 || m.length != 1 ||
        m.positions[0].size != 16 ||
        memcmp(m.positions[0].chars, "0123456789ABCDEF", 16) != 0) {
        fprintf(stderr, "MP-17 failed: ?H expansion wrong\n"); ok = 0;
    }

    /* MP-18: ?? = literal '?' position of size 1 */
    if (mask_parse("a??b", &m, NULL, NULL, NULL, NULL) != 0 || m.length != 3 ||
        m.positions[1].size != 1 || m.positions[1].chars[0] != '?') {
        fprintf(stderr, "MP-18 failed: ?? escaping wrong (len=%d)\n", m.length); ok = 0;
    }

    return ok;
}


/* MP-20..24: expand_charset_def */
static int group_expand_charset_def(void)
{
    int ok = 1;
    char buf[MAX_CHARSET_LEN];
    unsigned int n = 0;

    /* MP-20: plain literals */
    if (expand_charset_def("abc", buf, &n) != 0 || n != 3 ||
        memcmp(buf, "abc", 3) != 0) {
        fprintf(stderr, "MP-20 failed: literal def\n"); ok = 0;
    }

    /* MP-21: embedded built-in tokens (?d?u = 10 + 26 = 36) */
    if (expand_charset_def("?d?u", buf, &n) != 0 || n != 36 ||
        memcmp(buf, "0123456789", 10) != 0 ||
        memcmp(buf + 10, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26) != 0) {
        fprintf(stderr, "MP-21 failed: token-bearing def (n=%u)\n", n); ok = 0;
    }

    /* MP-22: \xNN hex escapes -> raw bytes */
    if (expand_charset_def("\\x41\\x42", buf, &n) != 0 || n != 2 ||
        buf[0] != 0x41 || buf[1] != 0x42) {
        fprintf(stderr, "MP-22 failed: hex def\n"); ok = 0;
    }

    /* MP-23: custom-in-custom is rejected */
    if (expand_charset_def("?1", buf, &n) == 0) {
        fprintf(stderr, "MP-23 failed: ?1 inside def should be rejected\n"); ok = 0;
    }

    /* MP-24: overflow (?b?b = 512 > 256) is rejected */
    if (expand_charset_def("?b?b", buf, &n) == 0) {
        fprintf(stderr, "MP-24 failed: overflow should be rejected\n"); ok = 0;
    }

    return ok;
}


int test_mask_parse(void)
{
    int ok = 1;

    if (!group_basic_specifiers()) ok = 0;
    if (!group_literal_char())     ok = 0;
    if (!group_is_mask_string())   ok = 0;
    if (!group_filename_roundtrip()) ok = 0;
    if (!group_gpu_buffers())      ok = 0;
    if (!group_a_specifier())      ok = 0;
    if (!group_negative())         ok = 0;
    if (!group_fill_plaintext_space_mask()) ok = 0;
    if (!group_index_to_plaintext_mask()) ok = 0;
    if (!group_parse_rt_params_mask()) ok = 0;
    if (!group_hex_and_escape()) ok = 0;
    if (!group_expand_charset_def()) ok = 0;

    return ok;
}
