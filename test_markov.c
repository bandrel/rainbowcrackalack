/*
 * Rainbow Crackalack: test_markov.c
 * CPU-only unit tests for markov.h / markov.c.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cpu_rt_functions.h"
#include "markov.h"
#include "shared.h"
#include "test_markov.h"


/* -------------------------------------------------------------------------
 * Group A: markov_build_sorted
 * Tests MS-01 through MS-04
 * ------------------------------------------------------------------------- */

static int group_a(void)
{
    int ok = 1;

    uint64_t pos0_freq[3]    = {10, 30, 20};
    uint64_t bigram_freq[9]  = {
        5, 15, 10,   /* prev='a'(0): b most likely */
        1,  1, 50,   /* prev='b'(1): c most likely */
        8,  2,  4    /* prev='c'(2): a most likely */
    };

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = 3;
    m.max_positions = 1;  /* single bigram table for this test */
    memcpy(m.charset, "abc", 3);
    m.pos0_freq    = pos0_freq;
    m.bigram_freq  = bigram_freq;
    m.sorted_pos0  = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_a: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    /* MS-01: rank-0 for position 0 is 'b' (charset index 1, freq=30) */
    if (m.sorted_pos0[0] != 1) {
        fprintf(stderr, "MS-01 failed: sorted_pos0[0]=%u, expected 1\n",
                (unsigned)m.sorted_pos0[0]);
        ok = 0;
    }

    /* MS-02: rank-1 for position 0 is 'c' (charset index 2, freq=20) */
    if (m.sorted_pos0[1] != 2) {
        fprintf(stderr, "MS-02 failed: sorted_pos0[1]=%u, expected 2\n",
                (unsigned)m.sorted_pos0[1]);
        ok = 0;
    }

    /* MS-03: prev='a'(row 0), rank-0 next is 'b' (index 1, freq=15) */
    if (m.sorted_bigram[0 * 3 + 0] != 1) {
        fprintf(stderr, "MS-03 failed: sorted_bigram[0]=%u, expected 1\n",
                (unsigned)m.sorted_bigram[0 * 3 + 0]);
        ok = 0;
    }

    /* MS-04: prev='b'(row 1), rank-0 next is 'c' (index 2, freq=50) */
    if (m.sorted_bigram[1 * 3 + 0] != 2) {
        fprintf(stderr, "MS-04 failed: sorted_bigram[3]=%u, expected 2\n",
                (unsigned)m.sorted_bigram[1 * 3 + 0]);
        ok = 0;
    }

    /* Nullify stack-backed pointers before markov_free so it doesn't free them. */
    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group B: index_to_plaintext_markov_cpu
 * Tests IMP-01 through IMP-02
 * ------------------------------------------------------------------------- */

static int group_b(void)
{
    int ok = 1;

    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {
        5, 15, 10,
        1,  1, 50,
        8,  2,  4
    };

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = 3;
    m.max_positions = 1;  /* single bigram table for this test */
    memcpy(m.charset, "abc", 3);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_b: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    unsigned char pt[3];

    /* IMP-01: index 0 -> "bc"
     * pos0: sorted_pos0[0]=1 -> 'b'; index=0/3=0
     * pos1: prev='b'(idx 1), sorted_bigram[1*3+0]=2 -> 'c' */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 2, pt);
    if (pt[0] != 'b' || pt[1] != 'c') {
        fprintf(stderr, "IMP-01 failed: got \"%c%c\", expected \"bc\"\n",
                (char)pt[0], (char)pt[1]);
        ok = 0;
    }

    /* IMP-02: index 1 -> "ca"
     * pos0: sorted_pos0[1]=2 -> 'c'; index=1/3=0
     * pos1: prev='c'(idx 2), sorted_bigram[2*3+0]=0 -> 'a' */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(1, &m, 2, pt);
    if (pt[0] != 'c' || pt[1] != 'a') {
        fprintf(stderr, "IMP-02 failed: got \"%c%c\", expected \"ca\"\n",
                (char)pt[0], (char)pt[1]);
        ok = 0;
    }

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group C: markov_train round-trip (train -> save -> load -> compare)
 * Tests MT-01 through MT-03
 * ------------------------------------------------------------------------- */

static int group_c(void)
{
    int ok = 1;
    char corpus_path[128];
    char model_path[128];
    snprintf(corpus_path, sizeof(corpus_path),
             "/tmp/test_markov_corpus_%d.txt", (int)getpid());
    snprintf(model_path, sizeof(model_path),
             "/tmp/test_markov_rt_%d.markov", (int)getpid());

    /* Write synthetic corpus: "ab" x5, "ac" x1, "bc" x3 */
    FILE *fp = fopen(corpus_path, "w");
    if (!fp) {
        fprintf(stderr, "MT: cannot create corpus '%s'\n", corpus_path);
        return 0;
    }
    for (int i = 0; i < 5; i++) fprintf(fp, "ab\n");
    fprintf(fp, "ac\n");
    for (int i = 0; i < 3; i++) fprintf(fp, "bc\n");
    fclose(fp);

    markov_model m;
    memset(&m, 0, sizeof(m));
    if (markov_train(corpus_path, "abc", 3, 0, &m) != 0) {
        fprintf(stderr, "MT: markov_train failed\n");
        remove(corpus_path);
        /* markov_train zeroes the model on failure, so markov_free is safe but a no-op. */
        markov_free(&m);
        return 0;
    }

    /* MT-01: Laplace-smoothed a->b count (5+1=6) > a->c count (1+1=2) */
    if (m.bigram_freq[0 * 3 + 1] <= m.bigram_freq[0 * 3 + 2]) {
        fprintf(stderr, "MT-01 failed: a->b freq=%"PRIu64", a->c freq=%"PRIu64"\n",
                m.bigram_freq[0 * 3 + 1], m.bigram_freq[0 * 3 + 2]);
        ok = 0;
    }

    /* MT-02: sorted_bigram[0*3+0] == 1 (prev='a', rank-0 next is 'b') */
    if (m.sorted_bigram[0 * 3 + 0] != 1) {
        fprintf(stderr, "MT-02 failed: sorted_bigram[0]=%u, expected 1\n",
                (unsigned)m.sorted_bigram[0 * 3 + 0]);
        ok = 0;
    }

    /* MT-03: save, reload, compare */
    if (markov_save(model_path, &m) != 0) {
        fprintf(stderr, "MT-03: markov_save failed\n");
        ok = 0;
    } else {
        markov_model m2;
        memset(&m2, 0, sizeof(m2));
        if (markov_load(model_path, &m2) != 0) {
            fprintf(stderr, "MT-03: markov_load failed\n");
            ok = 0;
        } else {
            if (m2.charset_len != m.charset_len) {
                fprintf(stderr, "MT-03 failed: charset_len %u != %u\n",
                        m2.charset_len, m.charset_len);
                ok = 0;
            }
            if (memcmp(m2.pos0_freq, m.pos0_freq,
                       m.charset_len * sizeof(uint64_t)) != 0) {
                fprintf(stderr, "MT-03 failed: pos0_freq mismatch\n");
                ok = 0;
            }
            if (memcmp(m2.bigram_freq, m.bigram_freq,
                       (size_t)m.charset_len * m.charset_len * sizeof(uint64_t)) != 0) {
                fprintf(stderr, "MT-03 failed: bigram_freq mismatch\n");
                ok = 0;
            }
            markov_free(&m2);
        }
        remove(model_path);
    }

    markov_free(&m);
    remove(corpus_path);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group D: error handling
 * Tests ME-01 through ME-03
 * ------------------------------------------------------------------------- */

static int group_d(void)
{
    int ok = 1;
    markov_model m;

    /* ME-01: load from non-existent file returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_load("/tmp/no_such_file_rca_xyz.markov", &m) != -1) {
        fprintf(stderr, "ME-01 failed: expected -1 for missing file\n");
        ok = 0;
    }

    /* ME-02: load file with wrong magic returns -1
     * Write 12 bytes: "XXXX" + version(1) as uint32 + charset_len(3) as uint32 */
    char bad_path[128];
    snprintf(bad_path, sizeof(bad_path),
             "/tmp/test_markov_bad_%d.markov", (int)getpid());
    FILE *fp = fopen(bad_path, "wb");
    if (!fp) {
        fprintf(stderr, "ME-02: cannot create bad markov file\n");
        ok = 0;
    } else {
        fwrite("XXXX", 1, 4, fp);
        uint32_t ver = 1;
        uint32_t clen = 3;
        fwrite(&ver, sizeof(uint32_t), 1, fp);
        fwrite(&clen, sizeof(uint32_t), 1, fp);
        fclose(fp);

        memset(&m, 0, sizeof(m));
        if (markov_load(bad_path, &m) != -1) {
            fprintf(stderr, "ME-02 failed: expected -1 for bad magic\n");
            ok = 0;
        }
        remove(bad_path);
    }

    /* ME-03: train on empty file returns -1 */
    char empty_path[128];
    snprintf(empty_path, sizeof(empty_path),
             "/tmp/test_markov_empty_%d.txt", (int)getpid());
    fp = fopen(empty_path, "w");
    if (!fp) {
        fprintf(stderr, "ME-03: cannot create empty file\n");
        ok = 0;
    } else {
        fclose(fp);
        memset(&m, 0, sizeof(m));
        if (markov_train(empty_path, "abc", 3, 0, &m) != -1) {
            fprintf(stderr, "ME-03 failed: expected -1 for empty corpus\n");
            ok = 0;
        }
        remove(empty_path);
    }

    return ok;
}


/* -------------------------------------------------------------------------
 * Group E: markov_train edge cases
 * Tests ME-04 through ME-07
 * ------------------------------------------------------------------------- */

static int group_e(void)
{
    int ok = 1;
    markov_model m;

    /* ME-04: charset_len == 0 returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_train("/dev/null", "abc", 0, 0, &m) != -1) {
        fprintf(stderr, "ME-04 failed: expected -1 for charset_len==0\n");
        ok = 0;
    }

    /* ME-05: charset_len > 256 returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_train("/dev/null", "abc", 257, 0, &m) != -1) {
        fprintf(stderr, "ME-05 failed: expected -1 for charset_len==257\n");
        ok = 0;
    }

    /* ME-06: all words outside charset returns -1 */
    {
        char path[128];
        snprintf(path, sizeof(path),
                 "/tmp/test_markov_oob_%d.txt", (int)getpid());
        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "ME-06: cannot create file\n");
            ok = 0;
        } else {
            fprintf(fp, "XYZ\n999\n");
            fclose(fp);

            memset(&m, 0, sizeof(m));
            if (markov_train(path, "abc", 3, 0, &m) != -1) {
                fprintf(stderr, "ME-06 failed: expected -1 for all-OOB corpus\n");
                ok = 0;
            }
            remove(path);
        }
    }

    /* ME-07: single-character words produce valid model */
    {
        char path[128];
        snprintf(path, sizeof(path),
                 "/tmp/test_markov_single_%d.txt", (int)getpid());
        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "ME-07: cannot create file\n");
            ok = 0;
        } else {
            for (int i = 0; i < 10; i++) fprintf(fp, "a\n");
            for (int i = 0; i < 5; i++) fprintf(fp, "b\n");
            fclose(fp);

            memset(&m, 0, sizeof(m));
            if (markov_train(path, "abc", 3, 0, &m) != 0) {
                fprintf(stderr, "ME-07 failed: markov_train returned error\n");
                ok = 0;
            } else {
                /* 'a' should be most frequent at pos0 (10+1=11 vs 5+1=6 vs 0+1=1) */
                if (m.sorted_pos0[0] != 0) {
                    fprintf(stderr, "ME-07 failed: sorted_pos0[0]=%u, expected 0 ('a')\n",
                            (unsigned)m.sorted_pos0[0]);
                    ok = 0;
                }
                /* All bigram counts should be exactly 1 (Laplace only, no observed bigrams) */
                int bigrams_ok = 1;
                for (unsigned int i = 0; i < 9; i++) {
                    if (m.bigram_freq[i] != 1) {
                        bigrams_ok = 0;
                        break;
                    }
                }
                if (!bigrams_ok) {
                    fprintf(stderr, "ME-07 failed: expected uniform bigram freq (all 1)\n");
                    ok = 0;
                }
                markov_free(&m);
            }
            remove(path);
        }
    }

    return ok;
}


/* -------------------------------------------------------------------------
 * Group F: index_to_plaintext_markov_cpu boundary tests
 * Tests IMP-03 through IMP-06
 * ------------------------------------------------------------------------- */

static int group_f(void)
{
    int ok = 1;

    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {
        5, 15, 10,
        1,  1, 50,
        8,  2,  4
    };

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = 3;
    m.max_positions = 1;
    memcpy(m.charset, "abc", 3);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(m.max_positions * 9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_f: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    unsigned char pt[4];

    /* IMP-03: last valid 2-char index (n*n - 1 = 8) wraps correctly */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(8, &m, 2, pt);
    /* index=8: pos0=sorted_pos0[8%3]=sorted_pos0[2]; index=8/3=2
     *          pos1=sorted_bigram[prev*3 + 2%3] */
    if (pt[0] == 0 || pt[1] == 0) {
        fprintf(stderr, "IMP-03 failed: got null chars for max 2-char index\n");
        ok = 0;
    }

    /* IMP-04: index 0 with length 1 -> single most probable char */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 1, pt);
    if (pt[0] != 'b') {
        fprintf(stderr, "IMP-04 failed: got '%c', expected 'b'\n", (char)pt[0]);
        ok = 0;
    }

    /* IMP-05: 3-char plaintext at index 0 -> most probable trigram */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 3, pt);
    /* pos0: sorted_pos0[0]=1 -> 'b'
     * pos1: prev='b'(1), sorted_bigram[1*3+0]=2 -> 'c'
     * pos2: prev='c'(2), sorted_bigram[2*3+0]=0 -> 'a' */
    if (pt[0] != 'b' || pt[1] != 'c' || pt[2] != 'a') {
        fprintf(stderr, "IMP-05 failed: got \"%c%c%c\", expected \"bca\"\n",
                (char)pt[0], (char)pt[1], (char)pt[2]);
        ok = 0;
    }

    /* IMP-06: all indices [0, n^2) produce unique 2-char plaintexts */
    unsigned int seen[9];
    memset(seen, 0, sizeof(seen));
    int unique = 1;
    for (uint64_t idx = 0; idx < 9; idx++) {
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(idx, &m, 2, pt);
        /* Hash the 2-char result to check uniqueness */
        unsigned int key = 0;
        for (unsigned int ci = 0; ci < 3; ci++) {
            if ((unsigned char)m.charset[ci] == pt[0]) {
                for (unsigned int cj = 0; cj < 3; cj++) {
                    if ((unsigned char)m.charset[cj] == pt[1]) {
                        key = ci * 3 + cj;
                    }
                }
            }
        }
        if (seen[key]) {
            fprintf(stderr, "IMP-06 failed: duplicate at index %llu\n",
                    (unsigned long long)idx);
            unique = 0;
            break;
        }
        seen[key] = 1;
    }
    if (!unique) ok = 0;

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group G: Markov model quality / viability tests
 * Tests MQ-01 through MQ-04
 * ------------------------------------------------------------------------- */

static int group_g(void)
{
    int ok = 1;

    /* MQ-01: Common-first ordering.
     * Corpus (charset "abc"): "ba"x20, "ca"x10, "bc"x5, all others x1.
     * "ba" is the most frequent bigram, so index 0 must return "ba". */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq01_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-01: cannot create corpus\n"); ok = 0; goto mq01_done; }
        for (int i = 0; i < 20; i++) fprintf(fp, "ba\n");
        for (int i = 0; i < 10; i++) fprintf(fp, "ca\n");
        for (int i = 0; i <  5; i++) fprintf(fp, "bc\n");
        /* low-frequency fillers */
        fprintf(fp, "ab\nac\nbb\ncb\ncc\naa\n");
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, 0, &m) != 0) {
            fprintf(stderr, "MQ-01: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq01_done;
        }

        unsigned char pt[3];
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(0, &m, 2, pt);
        if (pt[0] != 'b' || pt[1] != 'a') {
            fprintf(stderr, "MQ-01 failed: index 0 returned \"%c%c\", expected \"ba\"\n",
                    (char)pt[0], (char)pt[1]);
            ok = 0;
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq01_done:;

    /* MQ-02: Monotone probability ordering.
     * Corpus (charset "ab"): "aa"x100, "bb"x1.
     * After Laplace smoothing "aa" has far higher probability, so index 0
     * must return "aa" and the index of "bb" must be > 0. */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq02_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-02: cannot create corpus\n"); ok = 0; goto mq02_done; }
        for (int i = 0; i < 100; i++) fprintf(fp, "aa\n");
        fprintf(fp, "bb\n");
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "ab", 2, 0, &m) != 0) {
            fprintf(stderr, "MQ-02: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq02_done;
        }

        unsigned char pt[3];
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(0, &m, 2, pt);
        if (pt[0] != 'a' || pt[1] != 'a') {
            fprintf(stderr, "MQ-02 failed: index 0 returned \"%c%c\", expected \"aa\"\n",
                    (char)pt[0], (char)pt[1]);
            ok = 0;
        }

        /* Find which index gives "bb" and verify it is > 0 */
        int found_bb_at = -1;
        for (uint64_t idx = 0; idx < 4; idx++) {
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu(idx, &m, 2, pt);
            if (pt[0] == 'b' && pt[1] == 'b') {
                found_bb_at = (int)idx;
                break;
            }
        }
        if (found_bb_at <= 0) {
            fprintf(stderr, "MQ-02 failed: \"bb\" found at index %d (expected > 0)\n",
                    found_bb_at);
            ok = 0;
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq02_done:;

    /* MQ-03: Top-N coverage.
     * Corpus (charset "abc"): "ba"x50, "bc"x30, "ca"x20, all others x2.
     * The three most-probable bigrams are "ba", "bc", "ca".
     * Verify all three appear within the first 6 Markov indices (2x slack). */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq03_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-03: cannot create corpus\n"); ok = 0; goto mq03_done; }
        for (int i = 0; i < 50; i++) fprintf(fp, "ba\n");
        for (int i = 0; i < 30; i++) fprintf(fp, "bc\n");
        for (int i = 0; i < 20; i++) fprintf(fp, "ca\n");
        for (int i = 0; i <  2; i++) {
            fprintf(fp, "aa\nab\nac\nbb\ncb\ncc\n");
        }
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, 0, &m) != 0) {
            fprintf(stderr, "MQ-03: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq03_done;
        }

        /* Collect first 6 Markov-ordered plaintexts */
        char first6[6][3];
        for (int idx = 0; idx < 6; idx++) {
            unsigned char pt[3];
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu((uint64_t)idx, &m, 2, pt);
            first6[idx][0] = (char)pt[0];
            first6[idx][1] = (char)pt[1];
            first6[idx][2] = '\0';
        }

        const char *top3[] = {"ba", "bc", "ca"};
        for (int t = 0; t < 3; t++) {
            int found = 0;
            for (int idx = 0; idx < 6; idx++) {
                if (first6[idx][0] == top3[t][0] && first6[idx][1] == top3[t][1]) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "MQ-03 failed: \"%s\" not in first 6 Markov indices\n",
                        top3[t]);
                ok = 0;
            }
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq03_done:;

    /* MQ-04: Uniform corpus does not crash and produces unique plaintexts.
     * Corpus (charset "abc"): each of the 9 bigrams appears exactly twice.
     * After Laplace smoothing all bigram freqs are equal.
     * Verify: no crashes and all 9 indices [0,9) produce unique 2-char plaintexts. */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq04_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-04: cannot create corpus\n"); ok = 0; goto mq04_done; }
        const char *bigrams[] = {"aa","ab","ac","ba","bb","bc","ca","cb","cc"};
        for (int i = 0; i < 9; i++) {
            fprintf(fp, "%s\n", bigrams[i]);
            fprintf(fp, "%s\n", bigrams[i]);
        }
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, 0, &m) != 0) {
            fprintf(stderr, "MQ-04: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq04_done;
        }

        /* All 9 indices must produce unique 2-char plaintexts */
        unsigned int seen[9];
        memset(seen, 0, sizeof(seen));
        int unique = 1;
        for (uint64_t idx = 0; idx < 9; idx++) {
            unsigned char pt[3];
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu(idx, &m, 2, pt);
            int ci = -1, cj = -1;
            for (unsigned int k = 0; k < 3; k++) {
                if ((unsigned char)m.charset[k] == pt[0]) ci = (int)k;
                if ((unsigned char)m.charset[k] == pt[1]) cj = (int)k;
            }
            if (ci < 0 || cj < 0) {
                fprintf(stderr, "MQ-04 failed: char not in charset at idx %llu\n",
                        (unsigned long long)idx);
                unique = 0;
                break;
            }
            int key = ci * 3 + cj;
            if (seen[key]) {
                fprintf(stderr, "MQ-04 failed: duplicate plaintext at idx %llu\n",
                        (unsigned long long)idx);
                unique = 0;
                break;
            }
            seen[key] = 1;
        }
        if (!unique) ok = 0;

        markov_free(&m);
        remove(corpus_path);
    }
mq04_done:;

    return ok;
}


/* -------------------------------------------------------------------------
 * Group H: hash_to_index_markov9 approximation boundary tests
 * Tests H2I9-01 through H2I9-04
 *
 * The GPU fast-path for NTLM9 uses a bit-manipulation trick to approximate
 * hash % 630249409724609375 (95^9) without a full 64-bit division:
 *   tmp = ((hash >> 58) * 29) >> 6
 *   hash -= PST * tmp
 *   if (hash >= PST) hash -= PST
 *
 * This group verifies the approximation matches exact modulo for all
 * boundary values where it's most likely to diverge.
 * ------------------------------------------------------------------------- */

#define MARKOV9_PST 630249409724609375UL

static uint64_t hash_to_index_markov9_approx(uint64_t hash, unsigned int pos)
{
    hash += (uint64_t)pos;  /* wraps on overflow, matching GPU unsigned behavior */
    unsigned int tmp = ((hash >> 58) * 29) >> 6;
    hash -= MARKOV9_PST * tmp;
    if (hash >= MARKOV9_PST)
        hash -= MARKOV9_PST;
    return hash;
}

static uint64_t hash_to_index_markov9_exact(uint64_t hash, unsigned int pos)
{
    uint64_t val = hash + (uint64_t)pos;  /* wraps on overflow */
    return val % MARKOV9_PST;
}

static int group_h(void)
{
    int ok = 1;
    unsigned int test_num = 0;

    struct { uint64_t hash; unsigned int pos; } vectors[] = {
        /* H2I9-01: basic values */
        {0UL, 0},
        {1UL, 0},
        {MARKOV9_PST - 1, 0},          /* just below PST */
        {MARKOV9_PST, 0},              /* exactly PST -> should give 0 */
        {MARKOV9_PST + 1, 0},          /* one above PST */

        /* H2I9-02: near max quotient boundary (2^64/PST ≈ 29.27, max quotient=29) */
        {MARKOV9_PST * 29, 0},
        {MARKOV9_PST * 29 + 1, 0},
        {MARKOV9_PST * 29 - 1, 0},

        /* H2I9-03: UINT64_MAX and overflow */
        {UINT64_MAX, 0},
        {UINT64_MAX, 1},               /* overflow: wraps to 0 */
        {UINT64_MAX, 1000},            /* overflow: wraps to 999 */
        {UINT64_MAX - 999, 1000},      /* overflow: wraps to 0 */
        {UINT64_MAX, UINT32_MAX},      /* extreme overflow */

        /* H2I9-04: exact multiples and neighbors for k=1..15 */
        {MARKOV9_PST * 1, 0},
        {MARKOV9_PST * 1 - 1, 0},
        {MARKOV9_PST * 1 + 1, 0},
        {MARKOV9_PST * 2, 0},
        {MARKOV9_PST * 2 - 1, 0},
        {MARKOV9_PST * 2 + 1, 0},
        {MARKOV9_PST * 5, 0},
        {MARKOV9_PST * 5 - 1, 0},
        {MARKOV9_PST * 5 + 1, 0},
        {MARKOV9_PST * 10, 0},
        {MARKOV9_PST * 10 - 1, 0},
        {MARKOV9_PST * 10 + 1, 0},
        {MARKOV9_PST * 15, 0},
        {MARKOV9_PST * 15 - 1, 0},
        {MARKOV9_PST * 15 + 1, 0},
        {MARKOV9_PST * 20, 0},
        {MARKOV9_PST * 20 - 1, 0},
        {MARKOV9_PST * 20 + 1, 0},
        {MARKOV9_PST * 25, 0},
        {MARKOV9_PST * 25 - 1, 0},
        {MARKOV9_PST * 25 + 1, 0},
        {MARKOV9_PST * 28, 0},
        {MARKOV9_PST * 28 - 1, 0},
        {MARKOV9_PST * 28 + 1, 0},
        {MARKOV9_PST * 29, 0},
        {MARKOV9_PST * 29 - 1, 0},

        /* With nonzero pos on interesting hash values */
        {MARKOV9_PST - 1, 1},          /* hash+pos = PST exactly */
        {MARKOV9_PST - 1, 2},          /* hash+pos = PST+1 */
        {MARKOV9_PST * 29, 100},
        {0UL, 803000},                 /* typical max pos for NTLM9 chains */
    };

    unsigned int num_vectors = sizeof(vectors) / sizeof(vectors[0]);

    for (unsigned int i = 0; i < num_vectors; i++) {
        uint64_t approx = hash_to_index_markov9_approx(vectors[i].hash, vectors[i].pos);
        uint64_t exact  = hash_to_index_markov9_exact(vectors[i].hash, vectors[i].pos);
        test_num++;

        if (approx != exact) {
            fprintf(stderr, "H2I9 test %u failed: hash=%"PRIu64" pos=%u -> approx=%"PRIu64" exact=%"PRIu64"\n",
                    test_num, vectors[i].hash, vectors[i].pos, approx, exact);
            ok = 0;
        }
    }

    return ok;
}


/* -------------------------------------------------------------------------
 * Group I: Cross-path consistency (generic CPU i2p vs NTLM8/9 fast-path simulation)
 * Tests XP-01 (NTLM8) and XP-02 (NTLM9)
 *
 * Simulates the GPU fast-path unrolled index_to_plaintext in C and verifies
 * byte-for-byte match against index_to_plaintext_markov_cpu().
 * ------------------------------------------------------------------------- */

#define ASCII_32_95_LEN 95
#define MARKOV8_PSPACE  6634204312890625UL   /* 95^8 */
#define MARKOV9_PSPACE  630249409724609375UL /* 95^9 */

static const char ASCII_32_95[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

/*
 * Build a uniform ascii-32-95 Markov model with max_positions positions.
 * All frequencies are 1 (equivalent to Laplace-only).
 * Caller must call markov_free() when done (pos0_freq/bigram_freq are heap).
 */
static int build_uniform_ascii95_model(markov_model *m, unsigned int max_positions)
{
    memset(m, 0, sizeof(*m));
    m->charset_len = ASCII_32_95_LEN;
    m->max_positions = max_positions;
    memcpy(m->charset, ASCII_32_95, ASCII_32_95_LEN);

    size_t n = ASCII_32_95_LEN;
    m->pos0_freq = calloc(n, sizeof(uint64_t));
    m->bigram_freq = calloc(max_positions * n * n, sizeof(uint64_t));
    m->sorted_pos0 = malloc(n * sizeof(uint8_t));
    m->sorted_bigram = malloc(max_positions * n * n * sizeof(uint8_t));

    if (!m->pos0_freq || !m->bigram_freq || !m->sorted_pos0 || !m->sorted_bigram) {
        fprintf(stderr, "build_uniform_ascii95_model: OOM\n");
        markov_free(m);
        return -1;
    }

    /* Uniform: all frequencies = 1 */
    for (size_t i = 0; i < n; i++)
        m->pos0_freq[i] = 1;
    for (size_t i = 0; i < max_positions * n * n; i++)
        m->bigram_freq[i] = 1;

    markov_build_sorted(m);
    return 0;
}

/*
 * Simulate the NTLM8 GPU fast-path index_to_plaintext_markov8.
 * Mirrors CL/markov8_functions.cl exactly: index splitting, ci+32 shortcut.
 */
static void index_to_plaintext_markov8_sim(uint64_t index,
                                            const markov_model *model,
                                            unsigned char *plaintext)
{
    unsigned int indexHi = (unsigned int)(index / 81450625UL);  /* 95^4 */
    unsigned int indexLo = (unsigned int)(index - 81450625UL * indexHi);

    unsigned short group_23 = (unsigned short)(indexLo / 9025);
    unsigned short group_01 = (unsigned short)(indexLo - (unsigned int)9025 * group_23);
    unsigned short group_67 = (unsigned short)(indexHi / 9025);
    unsigned short group_45 = (unsigned short)(indexHi - (unsigned int)9025 * group_67);

    unsigned char rank1 = (unsigned char)(group_01 / 95);
    unsigned char rank0 = (unsigned char)(group_01 - (unsigned short)95 * rank1);
    unsigned char rank3 = (unsigned char)(group_23 / 95);
    unsigned char rank2 = (unsigned char)(group_23 - (unsigned short)95 * rank3);
    unsigned char rank5 = (unsigned char)(group_45 / 95);
    unsigned char rank4 = (unsigned char)(group_45 - (unsigned short)95 * rank5);
    unsigned char rank7 = (unsigned char)(group_67 / 95);
    unsigned char rank6 = (unsigned char)(group_67 - (unsigned short)95 * rank7);

    unsigned int ci = model->sorted_pos0[rank0];
    plaintext[0] = ci + 32;

    ci = model->sorted_bigram[ci * 95 + rank1];
    plaintext[1] = ci + 32;

    ci = model->sorted_bigram[9025 + ci * 95 + rank2];
    plaintext[2] = ci + 32;

    ci = model->sorted_bigram[18050 + ci * 95 + rank3];
    plaintext[3] = ci + 32;

    ci = model->sorted_bigram[27075 + ci * 95 + rank4];
    plaintext[4] = ci + 32;

    ci = model->sorted_bigram[36100 + ci * 95 + rank5];
    plaintext[5] = ci + 32;

    ci = model->sorted_bigram[45125 + ci * 95 + rank6];
    plaintext[6] = ci + 32;

    ci = model->sorted_bigram[54150 + ci * 95 + rank7];
    plaintext[7] = ci + 32;
}

/*
 * Simulate the NTLM9 GPU fast-path index_to_plaintext_markov9.
 * Mirrors CL/markov9_functions.cl exactly: unrolled positions, charset[] lookup.
 */
static void index_to_plaintext_markov9_sim(uint64_t index,
                                            const markov_model *model,
                                            unsigned char *plaintext)
{
    unsigned int charset_len = model->charset_len;
    uint64_t cs2 = (uint64_t)charset_len * charset_len;

    unsigned int charset_idx = model->sorted_pos0[index % charset_len];
    plaintext[0] = model->charset[charset_idx];
    index /= charset_len;
    unsigned int prev_charset_idx = charset_idx;

    /* Position 1 (bigram table at position 0) */
    uint64_t offset = (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[1] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 2 (bigram table at position 1) */
    offset = cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[2] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 3 (bigram table at position 2) */
    offset = 2UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[3] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 4 (bigram table at position 3) */
    offset = 3UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[4] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 5 (bigram table at position 4) */
    offset = 4UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[5] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 6 (bigram table at position 5) */
    offset = 5UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[6] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 7 (bigram table at position 6) */
    offset = 6UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[7] = model->charset[charset_idx];
    index /= charset_len;
    prev_charset_idx = charset_idx;

    /* Position 8 (bigram table at position 7) */
    offset = 7UL * cs2 + (uint64_t)prev_charset_idx * charset_len;
    charset_idx = model->sorted_bigram[offset + (index % charset_len)];
    plaintext[8] = model->charset[charset_idx];
}

static int group_i(void)
{
    int ok = 1;

    markov_model m;
    if (build_uniform_ascii95_model(&m, 8) != 0)
        return 0;

    /* Test indices: boundaries + pseudo-random spread */
    uint64_t test_indices_8[] = {
        0, 1, 94, 95, 96, 9024, 9025, 9026,
        81450624, 81450625, 81450626,
        MARKOV8_PSPACE / 2,
        MARKOV8_PSPACE - 1,
        /* Pseudo-random spread via LCG */
        12345, 999999, 1234567890,
        42424242UL, 8888888888UL,
        1111111111111UL, 5555555555555555UL,
        MARKOV8_PSPACE / 3, MARKOV8_PSPACE / 7,
        MARKOV8_PSPACE / 13, MARKOV8_PSPACE / 97,
    };
    unsigned int num_8 = sizeof(test_indices_8) / sizeof(test_indices_8[0]);

    /* XP-01: NTLM8 fast-path vs generic CPU */
    for (unsigned int i = 0; i < num_8; i++) {
        unsigned char pt_cpu[8], pt_sim[8];
        memset(pt_cpu, 0, sizeof(pt_cpu));
        memset(pt_sim, 0, sizeof(pt_sim));

        index_to_plaintext_markov_cpu(test_indices_8[i], &m, 8, pt_cpu);
        index_to_plaintext_markov8_sim(test_indices_8[i], &m, pt_sim);

        if (memcmp(pt_cpu, pt_sim, 8) != 0) {
            fprintf(stderr, "XP-01 failed at index %"PRIu64": cpu=\"%.8s\" sim=\"%.8s\"\n",
                    test_indices_8[i], pt_cpu, pt_sim);
            ok = 0;
        }
    }

    /* XP-02: NTLM9 fast-path vs generic CPU */
    uint64_t test_indices_9[] = {
        0, 1, 94, 95, 96, 9024, 9025, 9026,
        81450624, 81450625, 81450626,
        MARKOV9_PSPACE / 2,
        MARKOV9_PSPACE - 1,
        12345, 999999, 1234567890,
        42424242UL, 8888888888UL,
        1111111111111UL, 5555555555555555UL,
        MARKOV9_PSPACE / 3, MARKOV9_PSPACE / 7,
        MARKOV9_PSPACE / 13, MARKOV9_PSPACE / 97,
    };
    unsigned int num_9 = sizeof(test_indices_9) / sizeof(test_indices_9[0]);

    for (unsigned int i = 0; i < num_9; i++) {
        unsigned char pt_cpu[9], pt_sim[9];
        memset(pt_cpu, 0, sizeof(pt_cpu));
        memset(pt_sim, 0, sizeof(pt_sim));

        index_to_plaintext_markov_cpu(test_indices_9[i], &m, 9, pt_cpu);
        index_to_plaintext_markov9_sim(test_indices_9[i], &m, pt_sim);

        if (memcmp(pt_cpu, pt_sim, 9) != 0) {
            fprintf(stderr, "XP-02 failed at index %"PRIu64": cpu=\"%.9s\" sim=\"%.9s\"\n",
                    test_indices_9[i], pt_cpu, pt_sim);
            ok = 0;
        }
    }

    markov_free(&m);
    return ok;
}


/* -------------------------------------------------------------------------
 * Group J: Keyspace truncation tests
 * Tests KT-01 through KT-03
 *
 * When --markov-keyspace is used with a value less than charset_len^N,
 * the reduction function produces indices in [0, keyspace) rather than
 * [0, charset_len^N).  Verify the chain walk stays in bounds and that
 * all produced plaintexts are valid.
 * ------------------------------------------------------------------------- */

static int group_j(void)
{
    int ok = 1;

    /* Build small model: charset "abc", max_positions=1, full keyspace = 3^2 = 9 */
    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {5, 15, 10,
                                1,  1, 50,
                                8,  2,  4};
    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = 3;
    m.max_positions = 1;
    memcpy(m.charset, "abc", 3);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_j: OOM\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }
    markov_build_sorted(&m);

    /* KT-01: Truncated keyspace=5. Walk a 50-step chain,
     * verify all indices stay in [0,5). */
    {
        uint64_t truncated_keyspace = 5;
        uint64_t index = 0;
        int in_bounds = 1;

        for (unsigned int pos = 0; pos < 50; pos++) {
            if (index >= truncated_keyspace) {
                fprintf(stderr, "KT-01 failed: index=%"PRIu64" >= keyspace=%"PRIu64" at pos %u\n",
                        index, truncated_keyspace, pos);
                in_bounds = 0;
                break;
            }

            unsigned char plaintext[MAX_PLAINTEXT_LEN];
            unsigned char hash[16];
            memset(plaintext, 0, sizeof(plaintext));
            index_to_plaintext_markov_cpu(index, &m, 2, plaintext);
            ntlm_hash((char *)plaintext, 2, hash);
            index = hash_to_index(hash, 16, 0, truncated_keyspace, pos);
        }
        if (!in_bounds) ok = 0;
    }

    /* KT-02: All indices [0, truncated_keyspace) produce valid characters. */
    {
        uint64_t truncated_keyspace = 5;
        for (uint64_t idx = 0; idx < truncated_keyspace; idx++) {
            unsigned char pt[3];
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu(idx, &m, 2, pt);

            int c0_valid = 0, c1_valid = 0;
            for (unsigned int j = 0; j < 3; j++) {
                if (pt[0] == (unsigned char)m.charset[j]) c0_valid = 1;
                if (pt[1] == (unsigned char)m.charset[j]) c1_valid = 1;
            }
            if (!c0_valid || !c1_valid) {
                fprintf(stderr, "KT-02 failed: index %"PRIu64" -> invalid char '%c%c'\n",
                        idx, (char)pt[0], (char)pt[1]);
                ok = 0;
                break;
            }
        }
    }

    /* KT-03: Multiple start points with truncated keyspace all stay in bounds. */
    {
        uint64_t truncated_keyspace = 4;
        uint64_t starts[] = {0, 1, 2, 3};
        for (unsigned int s = 0; s < 4; s++) {
            uint64_t index = starts[s];
            for (unsigned int pos = 0; pos < 30; pos++) {
                if (index >= truncated_keyspace) {
                    fprintf(stderr, "KT-03 failed: start=%"PRIu64" index=%"PRIu64" >= %"PRIu64" at pos %u\n",
                            starts[s], index, truncated_keyspace, pos);
                    ok = 0;
                    goto kt03_done;
                }
                unsigned char plaintext[MAX_PLAINTEXT_LEN];
                unsigned char hash[16];
                memset(plaintext, 0, sizeof(plaintext));
                index_to_plaintext_markov_cpu(index, &m, 2, plaintext);
                ntlm_hash((char *)plaintext, 2, hash);
                index = hash_to_index(hash, 16, 0, truncated_keyspace, pos);
            }
        }
    }
kt03_done:;

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);
    return ok;
}


/* -------------------------------------------------------------------------
 * test_markov - entry point
 * ------------------------------------------------------------------------- */

int test_markov(void)
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

    return ok;
}
